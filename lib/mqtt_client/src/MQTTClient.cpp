#include "MQTTClient.h"
#include <ESPLogger.h>

MQTTClient::MQTTClient(Config &config, WiFiManager &wifiManager)
    : config(config), wifiManager(wifiManager), mqttClient(wifiClient),
      initialized(false), lastConnectAttempt(0), connectInterval(MIN_CONNECT_INTERVAL),
      connectAttempts(0), lastStatusPublish(0)
{
}

MQTTClient::~MQTTClient()
{
    disconnect();
}

bool MQTTClient::begin()
{
    ESPLogger::info("Initializing MQTT client...");
    ESPLogger::info("Free heap before MQTT init: %u bytes", ESP.getFreeHeap());

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

    // Don't attempt MQTT in AP mode or when WiFi is disconnected
    if (!wifiManager.isConnected() || wifiManager.isAPMode())
    {
        // If we were connected to MQTT, mark as disconnected
        if (mqttClient.connected())
        {
            ESPLogger::info("WiFi disconnected - closing MQTT connection");
            mqttClient.disconnect();
        }
        return;
    }

    unsigned long now = millis();

    // Handle reconnection
    if (!mqttClient.connected())
    {
        if (now - lastConnectAttempt >= connectInterval)
        {
            reconnect();
            lastConnectAttempt = now;
        }
    }
    else
    {
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
}

bool MQTTClient::isConnected()
{
    return initialized && mqttClient.connected();
}

bool MQTTClient::isInitialized() const
{
    return initialized;
}

void MQTTClient::disconnect()
{
    if (isConnected())
    {
        publishStatus("offline");
        mqttClient.disconnect();
    }
    initialized = false;
}

bool MQTTClient::connect()
{
    if (!initialized)
    {
        ESPLogger::error("MQTT connect failed - not initialized");
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
    ESPLogger::info("Username: '%s' (length: %d)", username.c_str(), username.length());
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

    bool success = mqttClient.publish(topic.c_str(), value.c_str(), useRetain);
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
    // Use new format with JSON
    String jsonStatus = "{"
                        "\"status\":\"" +
                        status + "\","
                                 "\"timestamp\":" +
                        String(millis()) + ","
                                           "\"uptime\":" +
                        String(millis() / 1000) + ","
                                                  "\"device_id\":\"" +
                        getDeviceId() + "\""
                                        "}";
    return publishJson(MQTT_DATA_TYPE_STATUS, MQTT_METRIC_CONNECTION, jsonStatus, true);
}

bool MQTTClient::publishRSSI()
{
    if (!wifiManager.isConnected())
    {
        return false;
    }
    // Use new format with JSON
    String jsonRSSI = "{"
                      "\"rssi\":" +
                      String(wifiManager.getRSSI()) + ","
                                                      "\"unit\":\"dBm\","
                                                      "\"timestamp\":" +
                      String(millis()) + ","
                                         "\"uptime\":" +
                      String(millis() / 1000) + ","
                                                "\"device_id\":\"" +
                      getDeviceId() + "\""
                                      "}";
    return publishJson(MQTT_DATA_TYPE_DIAGNOSTICS, MQTT_METRIC_RSSI, jsonRSSI, true);
}

bool MQTTClient::publishUptime()
{
    unsigned long uptimeSeconds = millis() / 1000;
    // Use new format with JSON
    String jsonUptime = "{"
                        "\"uptime\":" +
                        String(uptimeSeconds) + ","
                                                "\"unit\":\"seconds\","
                                                "\"timestamp\":" +
                        String(millis()) + ","
                                           "\"device_id\":\"" +
                        getDeviceId() + "\""
                                        "}";
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
        payload = "{\"value\":" + value + ",\"unit\":\"" + unit + "\",\"timestamp\":" + String(millis()) + ",\"device_id\":\"" + getDeviceId() + "\"}";
    }

    bool success = mqttClient.publish(topic.c_str(), payload.c_str(), useRetain);
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

    bool success = mqttClient.publish(topic.c_str(), value.c_str(), useRetain);
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

    bool success = mqttClient.publish(topic.c_str(), value.c_str(), useRetain);
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

    bool success = mqttClient.publish(topic.c_str(), value.c_str(), useRetain);
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

    bool success = mqttClient.publish(topic.c_str(), value.c_str(), useRetain);
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

bool MQTTClient::publishJson(const String &dataType, const String &metric, const String &jsonPayload, bool retain)
{
    if (!isConnected())
    {
        return false;
    }

    String topic = buildGenericTopic(dataType, metric);
    bool useRetain = retain && config.getBool("mqtt_retain", true);

    bool success = mqttClient.publish(topic.c_str(), jsonPayload.c_str(), useRetain);
    if (success)
    {
        ESPLogger::debug("Published JSON: %s = %s", topic.c_str(), jsonPayload.c_str());
    }
    else
    {
        ESPLogger::error("Failed to publish JSON: %s", topic.c_str());
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

    // Publish system info as JSON
    String systemInfo = "{"
                        "\"device_id\":\"" +
                        getDeviceId() + "\","
                                        "\"mac_address\":\"" +
                        wifiManager.getMACAddress() + "\","
                                                      "\"ip_address\":\"" +
                        wifiManager.getIPAddress() + "\","
                                                     "\"hostname\":\"" +
                        wifiManager.getNetworkHostname() + "\","
                                                           "\"firmware_version\":\"" +
                        String(FIRMWARE_VERSION) + "\","
                                                   "\"build_mode\":\"" +
                        String(BUILD_MODE) + "\","
                                             "\"git_hash\":\"" +
                        String(GIT_HASH) + "\","
                                           "\"git_branch\":\"" +
                        String(GIT_BRANCH) + "\","
                                             "\"build_date\":\"" +
                        String(BUILD_DATE) + "\","
                                             "\"build_time\":\"" +
                        String(BUILD_TIME) + "\","
                                             "\"device_name\":\"" +
                        String(DEVICE_NAME) + "\","
                                              "\"free_heap\":" +
                        String(ESP.getFreeHeap()) + ","
                                                    "\"timestamp\":" +
                        String(millis()) + ","
                                           "\"uptime\":" +
                        String(millis() / 1000);

    if (!wifiManager.isAPMode())
    {
        systemInfo += ",\"wifi_ssid\":\"" + wifiManager.getSSID() + "\"";
        systemInfo += ",\"wifi_rssi\":" + String(wifiManager.getRSSI());
    }

    systemInfo += "}";
    publishJson(MQTT_DATA_TYPE_STATUS, MQTT_METRIC_SYSTEM_INFO, systemInfo, true);

    // Publish configuration info as JSON
    String configInfo = "{"
                        "\"device_model\":\"" +
                        config.getString("device_model") + "\","
                                                           "\"device_type\":\"" +
                        getDeviceType() + "\","
                                          "\"poll_interval\":" +
                        String(config.getInt("poll_interval_sec")) + ","
                                                                     "\"read_storage\":" +
                        String(config.getBool("read_storage_regs") ? "true" : "false") + ","
                                                                                         "\"mqtt_json_format\":" +
                        String(config.getBool("mqtt_json_format", true) ? "true" : "false") + ","
                                                                                              "\"timestamp\":" +
                        String(millis()) +
                        "}";
    publishJson(MQTT_DATA_TYPE_CONFIG, MQTT_METRIC_CONFIG_INFO, configInfo, true);

    // Also publish individual status items for backward compatibility during transition
    publishRSSI();
    publishUptime();
}
