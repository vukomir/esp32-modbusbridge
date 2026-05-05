#include "WiFiManager.h"
#include <ESPLogger.h>
#include "constants.h"

WiFiManager::WiFiManager(Config &config)
    : config(config), apMode(false), initialized(false),
      lastReconnectAttempt(0), reconnectInterval(MIN_RECONNECT_INTERVAL),
      reconnectAttempts(0), ntpSynced(false)
{
}

WiFiManager::~WiFiManager()
{
    if (apMode)
    {
        dnsServer.stop();
    }
}

bool WiFiManager::begin()
{
    if (initialized)
    {
        return true;
    }

    ESPLogger::info("WiFi manager starting...");
    ESPLogger::info("Free heap before WiFi init: %u bytes", ESP.getFreeHeap());

    WiFi.mode(WIFI_OFF);
    delay(100);

    // Try STA mode first
    if (connectSTA())
    {
        ESPLogger::info("🎉 WiFi connection established in begin()");
        ESPLogger::info("📡 SSID: %s", getSSID().c_str());
        ESPLogger::info("🌐 IP: %s (via getIPAddress())", getIPAddress().c_str());
        ESPLogger::info("📍 Direct IP: %s (via WiFi.localIP())", WiFi.localIP().toString().c_str());
        ESPLogger::info("🔗 WiFi Status: %d", WiFi.status());
        ESPLogger::info("📊 AP Mode: %s", apMode ? "true" : "false");
        ESPLogger::info("🔧 Is Connected: %s", isConnected() ? "true" : "false");
        ESPLogger::info("WiFi manager started successfully");
        ESPLogger::info("Free heap after WiFi init: %u bytes", ESP.getFreeHeap());
        initialized = true;
        return true;
    }

    // Fall back to AP mode
    if (config.getBool("ap_enabled", true))
    {
        if (startAP())
        {
            ESPLogger::info("Started AP mode: %s", config.getString("ap_ssid").c_str());
            initialized = true;
            return true;
        }
    }

    ESPLogger::error("Failed to initialize WiFi");
    return false;
}

bool WiFiManager::connectSTA()
{
    String ssid = config.getString("wifi_ssid");
    String password = config.getString("wifi_password");

    if (ssid.isEmpty())
    {
        ESPLogger::info("No WiFi SSID configured");
        return false;
    }

    ESPLogger::info("Connecting to WiFi: %s", ssid.c_str());

    // Set custom hostname for DHCP
    String hostname = getHostname();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostname.c_str());
    ESPLogger::info("Setting hostname: %s", hostname.c_str());

    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long startTime = millis();
    const unsigned long timeout = 15000; // 15 seconds

    while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        apMode = false;
        reconnectAttempts = 0;
        reconnectInterval = MIN_RECONNECT_INTERVAL;

        ESPLogger::info("✅ WiFi connected successfully!");
        ESPLogger::info("📡 IP address: %s", WiFi.localIP().toString().c_str());
        ESPLogger::info("🌐 Gateway: %s", WiFi.gatewayIP().toString().c_str());
        ESPLogger::info("🔍 DNS: %s", WiFi.dnsIP().toString().c_str());

        // Wait a bit for network to stabilize before starting services
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Setup NTP time synchronization
        setupNTP();

        // Note: mDNS setup will be handled by handleConnection() loop

        return true;
    }

    ESPLogger::error("Failed to connect to WiFi: %s", ssid.c_str());
    return false;
}

bool WiFiManager::startAP()
{
    String apSSID = config.getString("ap_ssid", "modbusbridge-setup");
    String apPassword = config.getString("ap_password", "setup1234");

    WiFi.mode(WIFI_AP);

    bool success;
    if (apPassword.length() >= 8)
    {
        success = WiFi.softAP(apSSID.c_str(), apPassword.c_str());
    }
    else
    {
        success = WiFi.softAP(apSSID.c_str());
    }

    if (success)
    {
        apMode = true;

        // Start DNS server for captive portal
        dnsServer.start(53, "*", WiFi.softAPIP());

        ESPLogger::info("AP started - SSID: %s", apSSID.c_str());
        ESPLogger::info("AP IP: %s", WiFi.softAPIP().toString().c_str());
        return true;
    }

    ESPLogger::error("Failed to start AP");
    return false;
}

void WiFiManager::handleConnection()
{
    if (!initialized)
    {
        return;
    }

    if (apMode)
    {
        dnsServer.processNextRequest();
        return;
    }

    // Check STA connection
    if (WiFi.status() != WL_CONNECTED)
    {
        unsigned long now = millis();
        if (now - lastReconnectAttempt >= reconnectInterval)
        {
            ESPLogger::warn("WiFi disconnected (status: %d), attempting reconnection...", WiFi.status());
            ESPLogger::info("Current IP: %s", WiFi.localIP().toString().c_str());
            reconnect();
            lastReconnectAttempt = now;
        }
    }
    else
    {
        // Connected - ensure mDNS and NTP are running
        static bool mdnsSetupDone = false;
        static bool ntpSetupDone = false;
        static unsigned long lastIP = 0;
        unsigned long currentIP = WiFi.localIP();

        if (!mdnsSetupDone || currentIP != lastIP)
        {
            if (currentIP != 0)
            {
                ESPLogger::info("🔄 Starting mDNS for IP: %s", WiFi.localIP().toString().c_str());
                setupmDNS();
                mdnsSetupDone = true;
                lastIP = currentIP;
            }
        }

        // Setup NTP if not done yet and we have a stable connection
        if (!ntpSetupDone && currentIP != 0 && initialized)
        {
            ESPLogger::info("🕐 Setting up NTP (delayed setup)...");
            setupNTP();
            ntpSetupDone = true;
        }
    }
}

void WiFiManager::reconnect()
{
    reconnectAttempts++;

    if (reconnectAttempts > MAX_RECONNECT_ATTEMPTS)
    {
        ESPLogger::error("Max reconnection attempts reached, starting AP mode");
        if (config.getBool("ap_enabled", true))
        {
            startAP();
        }
        return;
    }

    if (!connectSTA())
    {
        updateReconnectInterval();
    }
}

void WiFiManager::updateReconnectInterval()
{
    // Exponential backoff
    reconnectInterval = min(reconnectInterval * 2, MAX_RECONNECT_INTERVAL);
    ESPLogger::info("Next reconnect attempt in %lu seconds", reconnectInterval / 1000);
}

bool WiFiManager::isConnected() const
{
    return initialized && !apMode && WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::isAPMode() const
{
    return apMode;
}

String WiFiManager::getIPAddress() const
{
    if (apMode)
    {
        return WiFi.softAPIP().toString();
    }
    else if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0))
    {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}

int WiFiManager::getRSSI() const
{
    if (isConnected())
    {
        return WiFi.RSSI();
    }
    return 0;
}

String WiFiManager::getSSID() const
{
    if (apMode)
    {
        return config.getString("ap_ssid", "modbusbridge-setup");
    }
    else if (WiFi.status() == WL_CONNECTED)
    {
        return WiFi.SSID();
    }
    return "";
}

String WiFiManager::getMACAddress() const
{
    return WiFi.macAddress();
}

String WiFiManager::getDeviceId() const
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    return macToDeviceId(mac);
}

String WiFiManager::macToDeviceId(const uint8_t *mac) const
{
    // Use last 3 bytes of MAC as device ID
    char deviceId[7];
    sprintf(deviceId, "%02x%02x%02x", mac[3], mac[4], mac[5]);
    return String(deviceId);
}

String WiFiManager::getHostname() const
{
    // Get hostname from config, with fallback
    String hostname = config.getString("hostname", "modbusbridge");

    // Clean hostname (ensure it's DNS-safe)
    hostname.toLowerCase();
    hostname.replace(" ", "-");
    hostname.replace("_", "-");

    // Remove any invalid characters
    String cleanHostname = "";
    for (int i = 0; i < hostname.length(); i++)
    {
        char c = hostname.charAt(i);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')
        {
            cleanHostname += c;
        }
    }

    // Ensure it's not empty and doesn't start/end with dash
    if (cleanHostname.isEmpty())
        cleanHostname = "modbusbridge";
    if (cleanHostname.startsWith("-"))
        cleanHostname = cleanHostname.substring(1);
    if (cleanHostname.endsWith("-"))
        cleanHostname = cleanHostname.substring(0, cleanHostname.length() - 1);

    return cleanHostname;
}

String WiFiManager::getNetworkHostname() const
{
    if (isConnected())
    {
        // Get the actual hostname set on the network interface
        return WiFi.getHostname();
    }
    return getHostname();
}

bool WiFiManager::setupmDNS()
{
    // Only setup mDNS in STA mode when connected to WiFi
    if (!isConnected() || isAPMode())
    {
        ESPLogger::debug("Skipping mDNS setup - not in STA mode or not connected");
        return false;
    }

    // Ensure we have a valid IP address
    if (WiFi.localIP() == IPAddress(0, 0, 0, 0))
    {
        ESPLogger::warn("No valid IP address, delaying mDNS setup");
        return false;
    }

    // Stop any existing mDNS first
    MDNS.end();
    vTaskDelay(pdMS_TO_TICKS(500)); // Longer delay to ensure clean shutdown

    String hostname = getHostname();
    ESPLogger::info("🚀 Starting mDNS setup...");
    ESPLogger::info("📡 Hostname: %s", hostname.c_str());
    ESPLogger::info("🌐 IP Address: %s", WiFi.localIP().toString().c_str());
    ESPLogger::info("📶 WiFi Status: %s", WiFi.status() == WL_CONNECTED ? "Connected" : "Not Connected");

    // Try to start mDNS with retries
    for (int attempt = 1; attempt <= 3; attempt++)
    {
        ESPLogger::info("mDNS start attempt %d/3 for hostname: %s", attempt, hostname.c_str());

        if (MDNS.begin(hostname.c_str()))
        {
            // Add service advertisement
            MDNS.addService("http", "tcp", 80);
            MDNS.addServiceTxt("http", "tcp", "device", "Modbus Bridge");
            MDNS.addServiceTxt("http", "tcp", "model", config.getString("device_model", "unknown"));
            MDNS.addServiceTxt("http", "tcp", "version", String(FIRMWARE_VERSION));
            MDNS.addServiceTxt("http", "tcp", "type", "modbus-gateway");
            MDNS.addServiceTxt("http", "tcp", "path", "/");

            // Set instance name for better identification
            MDNS.setInstanceName("Modbus Bridge (" + getDeviceId() + ")");

            ESPLogger::info("✅ mDNS started successfully on attempt %d", attempt);
            ESPLogger::info("📡 Access device at: http://%s.local", hostname.c_str());
            ESPLogger::info("🌐 IP Address: %s", WiFi.localIP().toString().c_str());
            ESPLogger::info("🔧 Service: %s._http._tcp.local", hostname.c_str());
            return true;
        }
        else
        {
            ESPLogger::warn("❌ mDNS start attempt %d failed for hostname: %s", attempt, hostname.c_str());
            if (attempt < 3)
            {
                vTaskDelay(pdMS_TO_TICKS(1000)); // Wait before retry
            }
        }
    }

    ESPLogger::error("🚨 Failed to start mDNS after 3 attempts for hostname: %s", hostname.c_str());
    ESPLogger::error("💡 Try accessing via IP address: http://%s", WiFi.localIP().toString().c_str());
    return false;
}

void WiFiManager::restartmDNS()
{
    ESPLogger::info("Restarting mDNS...");
    MDNS.end();
    vTaskDelay(pdMS_TO_TICKS(500));
    setupmDNS();
}

bool WiFiManager::ismDNSActive() const
{
    // Check if mDNS is active (only meaningful in STA mode)
    if (isAPMode() || !isConnected())
    {
        return false;
    }

    // Unfortunately, ESP32 mDNS library doesn't provide a direct status check
    // We assume it's active if we're in STA mode and connected
    return true;
}

void WiFiManager::disconnect()
{
    // Stop mDNS if running
    if (!apMode && isConnected())
    {
        MDNS.end();
    }

    if (apMode)
    {
        dnsServer.stop();
        WiFi.softAPdisconnect(true);
    }
    else
    {
        WiFi.disconnect(true);
    }
    apMode = false;
    initialized = false;
    ntpSynced = false;
}

// NTP time synchronization implementation
void WiFiManager::setupNTP()
{
    // Check WiFi status directly and add detailed logging
    ESPLogger::info("🕐 NTP setup check - WiFi status: %d, apMode: %s, initialized: %s",
                    WiFi.status(), apMode ? "true" : "false", initialized ? "true" : "false");

    if (apMode || WiFi.status() != WL_CONNECTED)
    {
        ESPLogger::debug("Skipping NTP setup - WiFi not ready (status: %d, apMode: %s)",
                         WiFi.status(), apMode ? "true" : "false");
        return;
    }

    ESPLogger::info("🕐 Setting up NTP time synchronization...");

    // Configure NTP with timezone (UTC+0 for now, can be made configurable)
    configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

    // Set timezone to UTC for consistency
    setenv("TZ", "UTC0", 1);
    tzset();

    ESPLogger::info("⏳ Waiting for NTP synchronization...");

    // Wait for time sync (up to 10 seconds)
    int attempts = 0;
    while (attempts < 20)
    {
        time_t now = time(nullptr);
        if (now > 1000000000)
        { // Valid timestamp (after year 2001)
            struct tm *timeinfo = localtime(&now);
            ESPLogger::info("✅ NTP synchronized: %04d-%02d-%02d %02d:%02d:%02d UTC",
                            timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                            timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
            ntpSynced = true;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        attempts++;
    }

    ESPLogger::warn("⚠️ NTP synchronization timeout after 10 seconds - using system time");
    ntpSynced = false;
}

bool WiFiManager::isNTPSynced() const
{
    return ntpSynced && !apMode && isConnected();
}

String WiFiManager::getCurrentTime() const
{
    if (!isNTPSynced())
    {
        // Return relative time if NTP not available
        unsigned long seconds = millis() / 1000;
        unsigned long hours = seconds / 3600;
        unsigned long minutes = (seconds % 3600) / 60;
        seconds = seconds % 60;
        char timeStr[10];
        snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", hours, minutes, seconds);
        return String(timeStr);
    }

    time_t now = time(nullptr);
    if (now > 1000000000)
    { // Valid timestamp
        struct tm *timeinfo = localtime(&now);
        char timeStr[20];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
        return String(timeStr);
    }

    // Fallback to system time
    unsigned long seconds = millis() / 1000;
    unsigned long hours = seconds / 3600;
    unsigned long minutes = (seconds % 3600) / 60;
    seconds = seconds % 60;
    char timeStr[10];
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", hours, minutes, seconds);
    return String(timeStr);
}
