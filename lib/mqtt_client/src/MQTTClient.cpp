#include "MQTTClient.h"
#include <ESPLogger.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Locking
//
// PubSubClient is not thread-safe: it keeps ONE buffer for both directions and
// writes each packet to the socket in several pieces. This wrapper is used from
// two tasks - mqttTask runs loop() (keepalive pings, inbound packets, reconnect)
// and pollerTask publishes telemetry - and the poller has the higher priority,
// so it can preempt mqttTask in the middle of a packet. The result is a
// corrupted byte stream, which the broker answers by dropping the connection.
// At a 10s poll interval that collision was rare; at 1s, with ~60 publishes a
// second, it showed up as failed publishes and reconnects every few minutes.
//
// Every use of the underlying client therefore goes through this lock. It is
// recursive because the public methods call each other (handleConnection ->
// publishStatus -> publishJson).
// ---------------------------------------------------------------------------
namespace
{
class MqttLock
{
public:
#ifdef ESP32
    MqttLock(SemaphoreHandle_t mutex, uint32_t timeoutMs)
        : mutex(mutex),
          held(mutex != NULL && xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE)
    {
    }
    ~MqttLock()
    {
        if (held)
        {
            xSemaphoreGiveRecursive(mutex);
        }
    }
    // True if it is safe to touch the client. If the mutex could not be
    // created we carry on unlocked, which is no worse than before.
    bool ok() const { return held || mutex == NULL; }

private:
    SemaphoreHandle_t mutex;
    bool held;
#else
    MqttLock(void *, uint32_t) {}
    bool ok() const { return true; }
#endif
};
} // namespace

MQTTClient::MQTTClient(Config &config, WiFiManager &wifiManager)
    : config(config), wifiManager(wifiManager), mqttClient(wifiClient),
      initialized(false), lastConnectAttempt(0), connectInterval(MIN_CONNECT_INTERVAL),
      connectAttempts(0), lastStatusPublish(0), clientMutex(NULL), connectedState(false)
{
}

MQTTClient::~MQTTClient()
{
    disconnect();
}

bool MQTTClient::begin()
{
    ESPLogger::info("Initializing MQTT client...");

#ifdef ESP32
    // Created here rather than in the constructor: this object is a global,
    // and its constructor runs before the RTOS is ready to hand out mutexes.
    if (clientMutex == NULL)
    {
        clientMutex = xSemaphoreCreateRecursiveMutex();
        if (clientMutex == NULL)
        {
            ESPLogger::error("Failed to create MQTT client mutex - continuing without locking");
        }
    }
#endif

    if (!validateMQTTConfig())
    {
        ESPLogger::error("Invalid MQTT configuration");
        return false;
    }

    String broker = config.getString("mqtt_broker");
    int port = config.getInt("mqtt_port", 1883);

    mqttClient.setServer(broker.c_str(), port);
    mqttClient.setSocketTimeout(10); // 10 second timeout
    mqttClient.setKeepAlive(60);     // 60 second keepalive

    // Enable debug info
    wifiClient.setTimeout(10000); // 10 second WiFi timeout

    initialized = true;

    ESPLogger::info("MQTT client initialized successfully - Broker: %s:%d", broker.c_str(), port);
    ESPLogger::info("Free heap after MQTT init: %u bytes", ESP.getFreeHeap());

    return true;
}

void MQTTClient::handleConnection()
{
    if (!initialized)
    {
        return;
    }

    // Held for the whole pass. A publish from the poller waits for it (briefly
    // - see LOCK_TIMEOUT_PUBLISH_MS) instead of cutting into loop().
    MqttLock lock(clientMutex, LOCK_TIMEOUT_LOOP_MS);
    if (!lock.ok())
    {
        return; // the poller is mid-burst; try again on the next pass
    }

    // Don't attempt MQTT in AP mode or when WiFi is disconnected
    if (!wifiManager.isConnected() || wifiManager.isAPMode())
    {
        // If we were connected to MQTT, mark as disconnected
        if (mqttClient.connected())
        {
            ESPLogger::info("WiFi disconnected - closing MQTT connection");
            mqttClient.disconnect();
        }
        connectedState = false;
        return;
    }

    unsigned long now = millis();

    // Handle reconnection
    if (!mqttClient.connected())
    {
        // Publish the state before the (slow) reconnect, so the poller skips
        // its telemetry instead of queueing up behind the lock.
        if (connectedState)
        {
            ESPLogger::warn("MQTT connection lost (state %d)", mqttClient.state());
        }
        connectedState = false;
        if (now - lastConnectAttempt >= connectInterval)
        {
            reconnect();
            lastConnectAttempt = now;
        }
    }
    else
    {
        connectedState = true;

        // Periodic status publishing
        if (now - lastStatusPublish >= STATUS_PUBLISH_INTERVAL)
        {
            publishStatus("online");
            publishRSSI();
            publishUptime();
            lastStatusPublish = now;
        }
    }

    mqttClient.loop();
    connectedState = mqttClient.connected();
}

bool MQTTClient::isConnected()
{
    // Deliberately lock-free: this is called from several tasks, often once per
    // telemetry point. It returns the state last observed under the lock
    // (handleConnection runs every 200ms, and every publish refreshes it).
    return initialized && connectedState;
}

bool MQTTClient::lockedPublish(const char *topic, const char *payload, bool retain)
{
    MqttLock lock(clientMutex, LOCK_TIMEOUT_PUBLISH_MS);
    if (!lock.ok())
    {
        // mqttTask is holding the client, most likely reconnecting. Dropping
        // one publish is better than stalling the Modbus poll behind it.
        return false;
    }
    if (!mqttClient.connected())
    {
        connectedState = false;
        return false;
    }
    bool ok = mqttClient.publish(topic, payload, retain);
    if (!ok)
    {
        connectedState = mqttClient.connected();
    }
    return ok;
}

bool MQTTClient::isInitialized() const
{
    return initialized;
}

void MQTTClient::disconnect()
{
    {
        MqttLock lock(clientMutex, LOCK_TIMEOUT_LOOP_MS);
        if (lock.ok() && initialized && mqttClient.connected())
        {
            connectedState = true; // so the farewell below is not short-circuited
            publishStatus("offline");
            mqttClient.disconnect();
        }
    }
    connectedState = false;
    initialized = false;
}

bool MQTTClient::connect()
{
    if (!initialized)
    {
        ESPLogger::error("MQTT connect failed - not initialized");
        return false;
    }

    // Normally entered from handleConnection(), which already holds the lock
    // (it is recursive); taken here as well because connect() is public.
    MqttLock lock(clientMutex, LOCK_TIMEOUT_LOOP_MS);
    if (!lock.ok())
    {
        return false;
    }

    if (!wifiManager.isConnected())
    {
        ESPLogger::debug("MQTT connect skipped - WiFi not connected");
        return false;
    }

    if (wifiManager.isAPMode())
    {
        ESPLogger::debug("MQTT connect skipped - device in AP mode");
        return false;
    }

    // Network diagnostics
    String broker = config.getString("mqtt_broker", "192.168.1.10");
    int port = config.getInt("mqtt_port", 1883);

    ESPLogger::info("Network diagnostics:");
    ESPLogger::info("- WiFi status: %d", WiFi.status());
    ESPLogger::info("- Local IP: %s", WiFi.localIP().toString().c_str());
    ESPLogger::info("- Gateway: %s", WiFi.gatewayIP().toString().c_str());
    ESPLogger::info("- DNS: %s", WiFi.dnsIP().toString().c_str());
    ESPLogger::info("- MQTT broker: %s:%d", broker.c_str(), port);

    // Test basic connectivity to broker
    WiFiClient testClient;
    ESPLogger::info("Testing TCP connection to MQTT broker...");
    if (testClient.connect(broker.c_str(), port))
    {
        ESPLogger::info("✓ TCP connection to MQTT broker successful");
        testClient.stop();
    }
    else
    {
        ESPLogger::error("✗ TCP connection to MQTT broker FAILED");
        ESPLogger::error("  This means the broker is unreachable or port is blocked");
        return false;
    }

    // Reset MQTT client with fresh connection to ensure clean state
    mqttClient.setClient(wifiClient);
    mqttClient.setServer(broker.c_str(), port);

    String clientId = "esp-" + getDeviceId();
    String username = config.getString("mqtt_username", "");
    String password = config.getString("mqtt_password", "");

    // Debug logging
    ESPLogger::info("MQTT connecting as: %s", clientId.c_str());
    // Presence and length only, matching the password line below. Logs are now
    // retained across reboots and downloadable over an unauthenticated web UI,
    // so the username must not appear verbatim; length is enough to diagnose a
    // stray space or an empty config field.
    ESPLogger::info("Username: %s (length: %d)", username.length() > 0 ? "[SET]" : "[EMPTY]", username.length());
    ESPLogger::info("Password: %s (length: %d)", password.length() > 0 ? "[SET]" : "[EMPTY]", password.length());

    // Small delay to ensure TCP connection is fully closed
    delay(100);

    bool connected = false;

    // Try method 1: Simple connection
    if (username.isEmpty())
    {
        ESPLogger::info("Connecting without authentication");
        connected = mqttClient.connect(clientId.c_str());
    }
    else
    {
        ESPLogger::info("Connecting with authentication");
        connected = mqttClient.connect(clientId.c_str(), username.c_str(), password.c_str());
    }

    // If simple method fails, try with LWT
    if (!connected)
    {
        ESPLogger::info("Simple connection failed, trying with LWT...");
        String willTopic = buildTopic("status");

        if (username.isEmpty())
        {
            connected = mqttClient.connect(clientId.c_str(), willTopic.c_str(), 1, true, "offline");
        }
        else
        {
            connected = mqttClient.connect(clientId.c_str(), username.c_str(), password.c_str(),
                                           willTopic.c_str(), 1, true, "offline");
        }
    }

    connectedState = connected;

    if (connected)
    {
        ESPLogger::info("MQTT connected successfully as: %s", clientId.c_str());
        connectAttempts = 0;
        connectInterval = MIN_CONNECT_INTERVAL;
        publishConnectStatus();
        return true;
    }
    else
    {
        int state = mqttClient.state();
        ESPLogger::error("MQTT connection failed, state: %d", state);
        // Decode the error state
        switch (state)
        {
        case -4:
            ESPLogger::error("MQTT_CONNECTION_TIMEOUT");
            break;
        case -3:
            ESPLogger::error("MQTT_CONNECTION_LOST");
            break;
        case -2:
            ESPLogger::error("MQTT_CONNECT_FAILED - Connection refused");
            break;
        case -1:
            ESPLogger::error("MQTT_DISCONNECTED");
            break;
        case 1:
            ESPLogger::error("MQTT_CONNECT_BAD_PROTOCOL");
            break;
        case 2:
            ESPLogger::error("MQTT_CONNECT_BAD_CLIENT_ID");
            break;
        case 3:
            ESPLogger::error("MQTT_CONNECT_UNAVAILABLE");
            break;
        case 4:
            ESPLogger::error("MQTT_CONNECT_BAD_CREDENTIALS");
            break;
        case 5:
            ESPLogger::error("MQTT_CONNECT_UNAUTHORIZED");
            break;
        default:
            ESPLogger::error("Unknown MQTT error state");
            break;
        }
        return false;
    }
}

void MQTTClient::reconnect()
{
    connectAttempts++;

    if (connectAttempts > MAX_CONNECT_ATTEMPTS)
    {
        ESPLogger::error("Max MQTT connection attempts reached");
        connectInterval = MAX_CONNECT_INTERVAL; // Back off for longer
        connectAttempts = 0;                    // Reset for next cycle
        return;
    }

    if (!connect())
    {
        updateConnectInterval();
    }
}

void MQTTClient::updateConnectInterval()
{
    // Exponential backoff
    connectInterval = min(connectInterval * 2, MAX_CONNECT_INTERVAL);
    ESPLogger::info("Next MQTT connect attempt in %lu seconds", connectInterval / 1000);
}

bool MQTTClient::publish(const String &metric, const String &value, bool retain)
{
    if (!isConnected())
    {
        return false;
    }

    String topic = buildTopic(metric);
    bool useRetain = retain && config.getBool("retain", true);

    bool success = lockedPublish(topic.c_str(), value.c_str(), useRetain);
    if (success)
    {
        ESPLogger::debug("Published: %s = %s", topic.c_str(), value.c_str());
    }
    else
    {
        ESPLogger::error("Failed to publish: %s", topic.c_str());
    }

    return success;
}

bool MQTTClient::publishStatus(const String &status)
{
    // Use ArduinoJson to avoid heap fragmentation
    StaticJsonDocument<256> doc;
    doc["status"] = status;
    doc["timestamp"] = millis();
    doc["uptime"] = millis() / 1000;
    doc["device_id"] = getDeviceId();

    String jsonStatus;
    serializeJson(doc, jsonStatus);
    return publishJson(MQTT_DATA_TYPE_STATUS, MQTT_METRIC_CONNECTION, jsonStatus, true);
}

bool MQTTClient::publishRSSI()
{
    if (!wifiManager.isConnected())
    {
        return false;
    }

    StaticJsonDocument<256> doc;
    doc["rssi"] = wifiManager.getRSSI();
    doc["unit"] = "dBm";
    doc["timestamp"] = millis();
    doc["uptime"] = millis() / 1000;
    doc["device_id"] = getDeviceId();

    String jsonRSSI;
    serializeJson(doc, jsonRSSI);
    return publishJson(MQTT_DATA_TYPE_DIAGNOSTICS, MQTT_METRIC_RSSI, jsonRSSI, true);
}

bool MQTTClient::publishUptime()
{
    unsigned long uptimeSeconds = millis() / 1000;

    StaticJsonDocument<256> doc;
    doc["uptime"] = uptimeSeconds;
    doc["unit"] = "seconds";
    doc["timestamp"] = millis();
    doc["device_id"] = getDeviceId();

    String jsonUptime;
    serializeJson(doc, jsonUptime);
    return publishJson(MQTT_DATA_TYPE_STATUS, MQTT_METRIC_UPTIME, jsonUptime, true);
}

// New generic format publishing methods

bool MQTTClient::publishTelemetry(const String &metric, const String &value, const String &unit, bool retain)
{
    if (!isConnected())
    {
        return false;
    }

    String topic = buildGenericTopic("telemetry", metric);
    bool useRetain = retain && config.getBool("mqtt_retain", true);

    String payload = value;

    // Add JSON format if enabled
    if (config.getBool("mqtt_json_format", true) && !unit.isEmpty())
    {
        StaticJsonDocument<256> doc;
        doc["value"] = serialized(value); // Use raw JSON value, not quoted string
        doc["unit"] = unit;
        doc["timestamp"] = millis();
        doc["device_id"] = getDeviceId();

        payload = "";
        serializeJson(doc, payload);
    }

    bool success = lockedPublish(topic.c_str(), payload.c_str(), useRetain);
    if (success)
    {
        ESPLogger::debug("Published telemetry: %s = %s", topic.c_str(), payload.c_str());
    }
    else
    {
        ESPLogger::error("Failed to publish telemetry: %s", topic.c_str());
    }

    return success;
}

bool MQTTClient::publishStatus(const String &metric, const String &value, bool retain)
{
    if (!isConnected())
    {
        return false;
    }

    String topic = buildGenericTopic("status", metric);
    bool useRetain = retain && config.getBool("mqtt_retain", true);

    bool success = lockedPublish(topic.c_str(), value.c_str(), useRetain);
    if (success)
    {
        ESPLogger::debug("Published status: %s = %s", topic.c_str(), value.c_str());
    }
    else
    {
        ESPLogger::error("Failed to publish status: %s", topic.c_str());
    }

    return success;
}

bool MQTTClient::publishDiagnostics(const String &metric, const String &value, bool retain)
{
    if (!isConnected())
    {
        return false;
    }

    String topic = buildGenericTopic("diagnostics", metric);
    bool useRetain = retain && config.getBool("mqtt_retain", true);

    bool success = lockedPublish(topic.c_str(), value.c_str(), useRetain);
    if (success)
    {
        ESPLogger::debug("Published diagnostics: %s = %s", topic.c_str(), value.c_str());
    }
    else
    {
        ESPLogger::error("Failed to publish diagnostics: %s", topic.c_str());
    }

    return success;
}

bool MQTTClient::publishHardware(const String &metric, const String &value, bool retain)
{
    if (!isConnected())
    {
        return false;
    }

    String topic = buildGenericTopic("hardware", metric);
    bool useRetain = retain && config.getBool("mqtt_retain", true);

    bool success = lockedPublish(topic.c_str(), value.c_str(), useRetain);
    if (success)
    {
        ESPLogger::debug("Published hardware: %s = %s", topic.c_str(), value.c_str());
    }
    else
    {
        ESPLogger::error("Failed to publish hardware: %s", topic.c_str());
    }

    return success;
}

bool MQTTClient::publishConfig(const String &metric, const String &value, bool retain)
{
    if (!isConnected())
    {
        return false;
    }

    String topic = buildGenericTopic("config", metric);
    bool useRetain = retain && config.getBool("mqtt_retain", true);

    bool success = lockedPublish(topic.c_str(), value.c_str(), useRetain);
    if (success)
    {
        ESPLogger::debug("Published config: %s = %s", topic.c_str(), value.c_str());
    }
    else
    {
        ESPLogger::error("Failed to publish config: %s", topic.c_str());
    }

    return success;
}

bool MQTTClient::publishAvailability(bool online, bool retain)
{
    // Publish to {prefix}/{deviceType}/status/availability with the canonical
    // string values "online" / "offline" (matches Home Assistant's defaults).
    // Retained so a fresh subscriber sees the last known state.
    const char *payload = online ? "online" : "offline";
    return publishStatus(MQTT_METRIC_AVAILABILITY, payload, retain);
}

bool MQTTClient::publishJson(const String &dataType, const String &metric, const String &jsonPayload, bool retain)
{
    if (!isConnected())
    {
        return false;
    }

    // Validate payload size against MQTT_MAX_PACKET_SIZE (512 bytes)
    // Leave 50 bytes headroom for MQTT headers and topic
    const size_t maxPayloadSize = 450;
    if (jsonPayload.length() > maxPayloadSize)
    {
        ESPLogger::error("JSON payload too large: %d bytes (max %d) for topic %s/%s",
                         jsonPayload.length(), maxPayloadSize, dataType.c_str(), metric.c_str());
        ESPLogger::error("Payload will be truncated by PubSubClient! Consider splitting or reducing data.");
        // Continue anyway but warn - truncation will happen
    }

    String topic = buildGenericTopic(dataType, metric);
    bool useRetain = retain && config.getBool("mqtt_retain", true);

    bool success = lockedPublish(topic.c_str(), jsonPayload.c_str(), useRetain);
    if (success)
    {
        ESPLogger::debug("Published JSON: %s = %s", topic.c_str(), jsonPayload.c_str());
    }
    else
    {
        ESPLogger::error("Failed to publish JSON: %s (payload size: %d bytes)", topic.c_str(), jsonPayload.length());
    }

    return success;
}

String MQTTClient::buildTopic(const String &metric) const
{
    String prefix = config.getString("mqtt_topic_prefix", "home/solar");
    String deviceId = getDeviceId();
    return prefix + "/" + deviceId + "/" + metric;
}

String MQTTClient::buildGenericTopic(const String &dataType, const String &metric) const
{
    // Build topic: <prefix>/<device_type>/<data_type>/<metric>
    String prefix = getTopicPrefix();
    String deviceType = getDeviceType();

    return prefix + "/" + deviceType + "/" + dataType + "/" + metric;
}

String MQTTClient::getTopicPrefix() const
{
    return config.getString("mqtt_topic_prefix", "home");
}

String MQTTClient::getDeviceType() const
{
    String deviceModel = config.getString("device_model", DEFAULT_DEVICE_MODEL);
    return InverterFactory::getDeviceType(deviceModel);
}

String MQTTClient::getDeviceId() const
{
    String deviceIdConfig = config.getString("device_id", "auto");

    if (deviceIdConfig == "auto" || deviceIdConfig.isEmpty())
    {
        // Use MAC address last 6 characters (default)
        return wifiManager.getDeviceId();
    }
    else if (deviceIdConfig == "mac")
    {
        // Use full MAC address
        return wifiManager.getMACAddress();
    }
    else if (deviceIdConfig.startsWith("custom:"))
    {
        // Use custom user-defined ID
        return deviceIdConfig.substring(7); // Remove "custom:" prefix
    }
    else if (deviceIdConfig == "serial")
    {
        // TODO: Get device serial number from Modbus (if available)
        // For now, fall back to MAC
        return wifiManager.getDeviceId();
    }
    else
    {
        // Direct value or fallback to MAC
        return deviceIdConfig.isEmpty() ? wifiManager.getDeviceId() : deviceIdConfig;
    }
}

bool MQTTClient::validateMQTTConfig() const
{
    String broker = config.getString("mqtt_broker");
    if (broker.isEmpty())
    {
        return false;
    }

    int port = config.getInt("mqtt_port", 1883);
    if (port < 1 || port > 65535)
    {
        return false;
    }

    return true;
}

void MQTTClient::publishConnectStatus()
{
    // Publish connection status
    publishStatus("online");

    // Publish system info as JSON using ArduinoJson
    StaticJsonDocument<512> sysDoc;
    sysDoc["device_id"] = getDeviceId();
    sysDoc["mac_address"] = wifiManager.getMACAddress();
    sysDoc["ip_address"] = wifiManager.getIPAddress();
    sysDoc["hostname"] = wifiManager.getNetworkHostname();
    sysDoc["firmware_version"] = FIRMWARE_VERSION;
    sysDoc["build_mode"] = BUILD_MODE;
    sysDoc["git_hash"] = GIT_HASH;
    sysDoc["git_branch"] = GIT_BRANCH;
    sysDoc["build_date"] = BUILD_DATE;
    sysDoc["build_time"] = BUILD_TIME;
    sysDoc["device_name"] = DEVICE_NAME;
    sysDoc["free_heap"] = ESP.getFreeHeap();
    sysDoc["timestamp"] = millis();
    sysDoc["uptime"] = millis() / 1000;

    if (!wifiManager.isAPMode())
    {
        sysDoc["wifi_ssid"] = wifiManager.getSSID();
        sysDoc["wifi_rssi"] = wifiManager.getRSSI();
    }

    String systemInfo;
    serializeJson(sysDoc, systemInfo);
    publishJson(MQTT_DATA_TYPE_STATUS, MQTT_METRIC_SYSTEM_INFO, systemInfo, true);

    // Publish configuration info as JSON using ArduinoJson
    StaticJsonDocument<256> cfgDoc;
    cfgDoc["device_model"] = config.getString("device_model");
    cfgDoc["device_type"] = getDeviceType();
    cfgDoc["poll_interval"] = config.getInt("poll_interval_sec");
    cfgDoc["read_storage"] = config.getBool("read_storage_regs");
    cfgDoc["mqtt_json_format"] = config.getBool("mqtt_json_format", true);
    cfgDoc["timestamp"] = millis();

    String configInfo;
    serializeJson(cfgDoc, configInfo);
    publishJson(MQTT_DATA_TYPE_CONFIG, MQTT_METRIC_CONFIG_INFO, configInfo, true);

    // Also publish individual status items for backward compatibility during transition
    publishRSSI();
    publishUptime();
}
