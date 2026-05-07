#include "WebUI.h"
#include "ModbusClient.h"
#include <ESPLogger.h>
#include "constants.h"
#include <Update.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Static log buffer initialization
WebUI::LogEntry WebUI::logBuffer[LOG_BUFFER_SIZE];
int WebUI::logBufferIndex = 0;
int WebUI::logBufferCount = 0;
unsigned long WebUI::logStartTime = 0;
WebUI *WebUI::instance = nullptr;

WebUI::WebUI(Config &config, WiFiManager &wifiManager, ModbusClient &modbusClient)
    : config(config), wifiManager(wifiManager), modbusClient(modbusClient), server(80), wsServer(81), running(false),
      connectedClients(0), lastClientCleanup(0)
{
    instance = this;

    // Initialize WebSocket client array
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++)
    {
        wsClients[i].id = 255; // Invalid ID
        wsClients[i].connected = false;
        wsClients[i].lastPing = 0;
        wsClients[i].connectTime = 0;
        wsClients[i].messageCount = 0;
    }
}

WebUI::~WebUI()
{
    stop();
}

bool WebUI::begin(uint16_t port)
{
    if (running)
    {
        return true;
    }

    ESPLogger::info("Web UI starting on port %d...", port);
    ESPLogger::info("Free heap before WebUI init: %u bytes", ESP.getFreeHeap());

    // Initialize log buffer
    logStartTime = millis();
    ESPLogger::setLogCallback(logCallback);

    // Generate CSRF token for this boot. Token lifetime = uptime; any open
    // browser tab from a previous boot will get a 403 on POST and need to reload.
    csrfToken = generateCSRFToken();
    ESPLogger::info("CSRF token generated for this session (32 chars)");

    // Setup WebSocket server on port 81
    ESPLogger::info("Initializing WebSocket server on port 81...");
    wsServer.onEvent(onWebSocketEventStatic);
    wsServer.begin();
    ESPLogger::info("WebSocket server started on port 81 - waiting for connections");

    // Give WebSocket server a moment to initialize
    delay(100);
    ESPLogger::info("WebSocket server initialization complete");

    // Setup routes
    server.on("/", HTTP_GET, [this]()
              { handleRoot(); });
    server.on("/save", HTTP_POST, [this]()
              { handleSave(); });
    server.on("/status", HTTP_GET, [this]()
              { handleStatus(); });
    server.on("/console", HTTP_GET, [this]()
              { handleConsole(); });
    server.on("/api/logs", HTTP_GET, [this]()
              { handleLogsAPI(); });
    server.on("/api/log/config", HTTP_POST, [this]()
              { handleLogConfig(); });
    server.on("/api/status", HTTP_GET, [this]()
              { handleAPIStatus(); });
    server.on("/diagnostics", HTTP_GET, [this]()
              { handleDiagnostics(); });
    server.on("/api/diagnostics", HTTP_POST, [this]()
              { handleDiagnosticsAPI(); });
    // Destructive endpoints are POST-only and CSRF-validated.
    // The handlers themselves call validateCSRFToken() and 403 on mismatch.
    server.on("/reboot", HTTP_POST, [this]()
              { handleReboot(); });
    server.on("/restart_mdns", HTTP_POST, [this]()
              { handleRestartmDNS(); });
    server.on("/factory", HTTP_POST, [this]()
              { handleFactoryReset(); });
    server.on("/update", HTTP_GET, [this]()
              { handleOTAForm(); });
    server.on("/update", HTTP_POST, [this]()
              { handleOTAUpload(); }, [this]()
              { 
                  HTTPUpload& upload = server.upload();
                  if (upload.status == UPLOAD_FILE_START) {
                      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
                      if (!Update.begin(maxSketchSpace)) {
                          ESPLogger::error("OTA Update begin failed: error %d", Update.getError());
                      }
                  } else if (upload.status == UPLOAD_FILE_WRITE) {
                      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                          ESPLogger::error("OTA Update write failed: error %d", Update.getError());
                      }
                  } else if (upload.status == UPLOAD_FILE_END) {
                      if (Update.end(true)) {
                          ESPLogger::info("OTA Update Success: %zu bytes", upload.totalSize);
                      } else {
                          ESPLogger::error("OTA Update end failed: error %d", Update.getError());
                      }
                  } });
    // Add captive portal detection routes
    server.on("/generate_204", HTTP_GET, [this]()
              { handleCaptivePortal(); }); // Android
    server.on("/hotspot-detect.html", HTTP_GET, [this]()
              { handleCaptivePortal(); }); // iOS
    server.on("/library/test/success.html", HTTP_GET, [this]()
              { handleCaptivePortal(); }); // iOS
    server.on("/ncsi.txt", HTTP_GET, [this]()
              { handleCaptivePortal(); }); // Windows
    server.on("/connectivity-check.html", HTTP_GET, [this]()
              { handleCaptivePortal(); }); // Chrome
    server.on("/redirect", HTTP_GET, [this]()
              { handleCaptivePortal(); });
    server.on("/fwlink/", HTTP_GET, [this]()
              { handleCaptivePortal(); });

    server.onNotFound([this]()
                      { handleNotFound(); });

    // Enable CORS for API endpoints
    server.enableCORS(true);

    // Add security headers - collect specific headers for logging
    const char *headerKeys[] = {"User-Agent", "X-Forwarded-For"};
    server.collectHeaders(headerKeys, 2);

    server.begin();
    running = true;
    ESPLogger::info("Web UI started successfully on port %d", port);
    ESPLogger::info("Free heap after WebUI init: %u bytes", ESP.getFreeHeap());
    return true;
}

void WebUI::handleClient()
{
    if (running)
    {
        server.handleClient();
        wsServer.loop();

        // Periodic cleanup of disconnected clients (every 5 minutes)
        if (millis() - lastClientCleanup > 300000)
        {
            cleanupDisconnectedClients();
            lastClientCleanup = millis();
        }
    }
}

void WebUI::stop()
{
    if (running)
    {
        server.stop();
        running = false;
        ESPLogger::info("Web UI stopped");
    }
}

bool WebUI::isRunning() const
{
    return running;
}

void WebUI::handleRoot()
{
    String html = getHTMLHeader("Configuration");
    html += generateConfigForm();
    html += getHTMLFooter();
    server.send(200, "text/html", html);
}

void WebUI::handleSave()
{
    if (!validateCSRFToken())
    {
        server.send(403, "text/plain", "CSRF token missing or invalid. Reload the form and try again.");
        return;
    }

    if (!validateForm())
    {
        return;
    }

    // Check if WiFi credentials are being changed
    String currentSSID = config.getString("wifi_ssid");
    String newSSID = server.arg("wifi_ssid");
    bool wifiChanged = (newSSID != currentSSID && !newSSID.isEmpty());
    bool isInAPMode = wifiManager.isAPMode();

    // Update configuration from form data
    for (int i = 0; i < server.args(); i++)
    {
        String name = server.argName(i);
        String value = server.arg(i);

        if (name == "wifi_ssid" || name == "wifi_password" ||
            name == "ap_ssid" || name == "ap_password" || name == "hostname" ||
            name == "mqtt_broker" || name == "mqtt_username" ||
            name == "mqtt_password" || name == "mqtt_topic_prefix" ||
            name == "parity" || name == "device_model" || name == "log_level")
        {
            config.setString(name, value);
        }
        else if (name == "mqtt_port" || name == "rtu_addr" ||
                 name == "baudrate" || name == "data_bits" || name == "stop_bits" ||
                 name == "rs485_de_re_pin" || name == "poll_interval_sec")
        {
            config.setInt(name, value.toInt());
        }
        else if (name == "ap_enabled" || name == "retain" ||
                 name == "read_storage_regs")
        {
            config.setBool(name, true); // checkbox present means true
        }
    }

    // Handle unchecked checkboxes (not present in form data)
    if (!server.hasArg("ap_enabled"))
        config.setBool("ap_enabled", false);
    if (!server.hasArg("retain"))
        config.setBool("retain", false);
    if (!server.hasArg("read_storage_regs"))
        config.setBool("read_storage_regs", false);

    // Apply log level changes immediately
    String newLogLevel = config.getString("log_level", "info");
    ESPLogger::setLevel(ESPLogger::stringToLogLevel(newLogLevel.c_str()));
    ESPLogger::info("Log level changed to: %s", newLogLevel.c_str());

    if (config.save())
    {
        // Send appropriate response based on configuration changes
        if (wifiChanged && isInAPMode)
        {
            sendSuccessWithRedirect(
                "WiFi configuration saved! Device will connect to your network and reboot...",
                getRedirectUrl("/"),
                15000 // 15 seconds to allow connection
            );
        }
        else if (wifiChanged)
        {
            sendSuccessWithRedirect(
                "WiFi configuration changed! Device will reconnect and reboot...",
                getRedirectUrl("/status"),
                8000 // 8 seconds for reconnection
            );
        }
        else
        {
            sendSuccessWithRedirect(
                "Configuration saved! Device will reboot...",
                getRedirectUrl("/status"),
                5000 // 5 seconds for normal reboot
            );
        }

        delay(3000);
        ESP.restart();
    }
    else
    {
        sendError("Failed to save configuration");
    }
}

void WebUI::handleStatus()
{
    String html = getHTMLHeader("Status");
    html += generateStatusPage();
    html += getHTMLFooter();
    server.send(200, "text/html", html);
}

void WebUI::handleReboot()
{
    if (!validateCSRFToken())
    {
        server.send(403, "text/plain", "CSRF token missing or invalid.");
        return;
    }
    sendSuccessWithRedirect(
        "Device is rebooting...",
        getRedirectUrl("/"),
        8000 // 8 seconds for reboot
    );
    delay(2000);
    ESP.restart();
}

void WebUI::handleFactoryReset()
{
    if (!validateCSRFToken())
    {
        server.send(403, "text/plain", "CSRF token missing or invalid.");
        return;
    }
    if (config.factoryReset())
    {
        // After factory reset, device will return to AP mode
        sendSuccessWithRedirect(
            "Factory reset completed! Device will restart in setup mode...",
            "http://192.168.4.1/",
            10000 // 10 seconds for full restart
        );
        delay(3000);
        ESP.restart();
    }
    else
    {
        sendError("Factory reset failed");
    }
}

void WebUI::handleRestartmDNS()
{
    if (!validateCSRFToken())
    {
        server.send(403, "text/plain", "CSRF token missing or invalid.");
        return;
    }
    if (wifiManager.isAPMode())
    {
        sendError("mDNS is not available in AP mode. Connect to WiFi first.");
        return;
    }

    if (!wifiManager.isConnected())
    {
        sendError("Device must be connected to WiFi to use mDNS.");
        return;
    }

    // Restart mDNS service
    wifiManager.restartmDNS();

    sendSuccessWithRedirect(
        "mDNS service restarted! Try accessing via hostname now...",
        getRedirectUrl("/status"),
        3000 // 3 seconds delay
    );
}

void WebUI::logCallback(ESPLogger::LogLevel level, const char *message, unsigned long timestamp)
{
    // Convert to NTP timestamp immediately if available
    uint64_t logTimestamp = timestamp; // Default to millis()

    // Check if NTP is available and convert timestamp
    time_t now = time(nullptr);
    if (now > 1000000000)
    { // Valid NTP time
        // Use 64-bit timestamp in milliseconds
        logTimestamp = (uint64_t)now * 1000ULL; // Current NTP time in milliseconds
    }

    // Store log entry in circular buffer with NTP timestamp
    logBuffer[logBufferIndex].timestamp = logTimestamp;
    logBuffer[logBufferIndex].level = level;
    strncpy(logBuffer[logBufferIndex].message, message, LOG_MESSAGE_MAX_LENGTH - 1);
    logBuffer[logBufferIndex].message[LOG_MESSAGE_MAX_LENGTH - 1] = '\0';

    // Broadcast to WebSocket clients if instance exists
    if (instance != nullptr)
    {
        instance->broadcastLogMessage(logBuffer[logBufferIndex]);
    }

    // Update circular buffer index
    logBufferIndex = (logBufferIndex + 1) % LOG_BUFFER_SIZE;

    // Update count (max LOG_BUFFER_SIZE)
    if (logBufferCount < LOG_BUFFER_SIZE)
    {
        logBufferCount++;
    }
}

String WebUI::escapeJsonString(const String &str)
{
    String escaped = "";
    for (int i = 0; i < str.length(); i++)
    {
        char c = str.charAt(i);
        switch (c)
        {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '<':
            escaped += "\\u003c"; // XSS prevention
            break;
        case '>':
            escaped += "\\u003e"; // XSS prevention
            break;
        case '&':
            escaped += "\\u0026"; // XSS prevention
            break;
        case '\'':
            escaped += "\\u0027"; // XSS prevention
            break;
        default:
            // Filter out control characters for security
            if (c >= 32 || c == '\t' || c == '\n' || c == '\r')
            {
                escaped += c;
            }
            break;
        }
    }
    return escaped;
}

void WebUI::handleLogsAPI()
{
    // Send only recent logs to prevent memory issues
    int recentCount = min(logBufferCount, 20); // Limit to 20 recent logs
    String json = "{\"logs\":[";

    // Calculate starting index for reading recent logs
    int startIndex = (logBufferCount < LOG_BUFFER_SIZE) ? 0 : logBufferIndex;
    int startOffset = max(0, logBufferCount - recentCount);

    for (int i = 0; i < recentCount; i++)
    {
        int index = (startIndex + startOffset + i) % LOG_BUFFER_SIZE;
        const LogEntry &entry = logBuffer[index];

        if (i > 0)
            json += ",";

        json += "{";
        json += "\"timestamp\":" + String(entry.timestamp) + ",";
        json += "\"level\":\"" + String(ESPLogger::logLevelToString(entry.level)) + "\",";
        json += "\"message\":\"" + escapeJsonString(String(entry.message)) + "\"";
        json += "}";
    }

    json += "],";
    json += "\"count\":" + String(recentCount) + ",";
    json += "\"total\":" + String(logBufferCount) + ",";
    json += "\"bufferSize\":" + String(LOG_BUFFER_SIZE) + "}";

    server.send(200, "application/json", json);
}

void WebUI::onWebSocketEventStatic(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    if (instance != nullptr)
    {
        instance->onWebSocketEvent(num, type, payload, length);
    }
}

void WebUI::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_DISCONNECTED:
        ESPLogger::info("WebSocket client %u disconnected", num);
        removeWebSocketClient(num);
        break;

    case WStype_CONNECTED:
    {
        IPAddress ip = wsServer.remoteIP(num);

        // Rate limiting: Check if we have too many connections from the same IP
        int connectionsFromIP = 0;
        for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++)
        {
            if (wsClients[i].connected && wsClients[i].remoteIP == ip)
            {
                connectionsFromIP++;
            }
        }

        if (connectionsFromIP >= 3)
        { // Max 3 connections per IP
            ESPLogger::warn("WebSocket connection rejected: too many connections from %s", ip.toString().c_str());
            wsServer.disconnect(num);
            return;
        }

        if (connectedClients >= MAX_WEBSOCKET_CLIENTS)
        {
            ESPLogger::warn("WebSocket connection rejected: maximum clients reached");
            wsServer.disconnect(num);
            return;
        }

        ESPLogger::info("WebSocket client %u connected from %s", num, ip.toString().c_str());
        addWebSocketClient(num, ip);

        // Check for duplicate connections from same IP and disconnect old ones
        for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++)
        {
            if (wsClients[i].connected && wsClients[i].remoteIP == ip && wsClients[i].id != num)
            {
                ESPLogger::info("Disconnecting old WebSocket client %u from same IP", wsClients[i].id);
                wsServer.disconnect(wsClients[i].id);
                removeWebSocketClient(wsClients[i].id);
            }
        }

        // Clear old logs from buffer to force fresh timestamps
        if (logBufferCount > 5)
        {
            ESPLogger::info("Clearing old log buffer for fresh NTP timestamps");
            logBufferCount = 0;
            logBufferIndex = 0;
        }

        // Send welcome message with system info
        String welcomeMsg = "{\"type\":\"welcome\",\"message\":\"Connected to " + String(DEVICE_NAME) +
                            " v" + String(FIRMWARE_VERSION) + "\",\"timestamp\":" + String(millis()) +
                            ",\"clients\":" + String(connectedClients) + "}";
        wsServer.sendTXT(num, welcomeMsg);

        // Send lightweight init message with time debug info
        time_t currentTime = time(nullptr);
        bool ntpActive = wifiManager.isNTPSynced();
        String debugInfo = "Console connected - NTP: " + String(ntpActive ? "YES" : "NO") +
                           ", UnixTime: " + String(currentTime) +
                           ", Millis: " + String(millis());
        String initMsg = "{\"type\":\"init\",\"message\":\"" + debugInfo + "\"}";
        wsServer.sendTXT(num, initMsg);

        // Send only the most recent log entry if available
        if (logBufferCount > 0)
        {
            int lastIndex = (logBufferIndex - 1 + LOG_BUFFER_SIZE) % LOG_BUFFER_SIZE;
            const LogEntry &entry = logBuffer[lastIndex];

            // Use timestamp directly from log buffer (already converted in logCallback)
            uint64_t realTime = entry.timestamp;
            time_t now = time(nullptr);
            bool useNTP = (now > 1000000000) && (realTime > 1000000000000ULL); // Check if timestamp is Unix time

            // Build JSON safely with proper escaping
            String escapedMessage = escapeJsonString(String(entry.message));

            String json = "{\"type\":\"log\",";
            json += "\"timestamp\":" + String((uint64_t)realTime) + ",";
            json += "\"level\":\"" + String(ESPLogger::logLevelToString(entry.level)) + "\",";
            json += "\"message\":\"" + escapedMessage + "\",";
            json += "\"ntp\":" + String(useNTP ? "true" : "false") + "}";

            wsServer.sendTXT(num, json);
        }
        break;
    }

    case WStype_TEXT:
    {
        String message = String((char *)payload);
        ESPLogger::info("WebSocket client %u sent: %s", num, message.c_str());

        // Handle client messages (ping, commands, etc.)
        if (message == "ping")
        {
            wsServer.sendTXT(num, "{\"type\":\"pong\",\"timestamp\":" + String(millis()) + "}");

            // Update client ping time
            for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++)
            {
                if (wsClients[i].id == num && wsClients[i].connected)
                {
                    wsClients[i].lastPing = millis();
                    wsClients[i].messageCount++;
                    break;
                }
            }
        }
        else if (message.startsWith("{"))
        {
            // Handle JSON commands (log level changes, etc.)
            // This will be processed by the log config handler
        }
        break;
    }

    case WStype_ERROR:
        ESPLogger::error("WebSocket client %u error", num);
        removeWebSocketClient(num);
        break;

    case WStype_PONG:
        ESPLogger::debug("WebSocket client %u pong received", num);
        // Update client ping time
        for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++)
        {
            if (wsClients[i].id == num && wsClients[i].connected)
            {
                wsClients[i].lastPing = millis();
                break;
            }
        }
        break;

    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
        ESPLogger::debug("WebSocket client %u fragment", num);
        break;

    default:
        break;
    }
}

void WebUI::broadcastLogMessage(const LogEntry &entry)
{
    if (connectedClients == 0)
        return; // Skip if no clients

    // Use timestamp directly from log buffer (already converted in logCallback)
    uint64_t realTime = entry.timestamp;
    time_t now = time(nullptr);
    bool useNTP = (now > 1000000000) && (realTime > 1000000000000ULL); // Check if timestamp is Unix time

    // Build JSON safely with proper escaping
    String escapedMessage = escapeJsonString(String(entry.message));

    // Use String concatenation for safer JSON building
    String json = "{\"type\":\"log\",";
    json += "\"timestamp\":" + String((uint64_t)realTime) + ",";
    json += "\"level\":\"" + String(ESPLogger::logLevelToString(entry.level)) + "\",";
    json += "\"message\":\"" + escapedMessage + "\",";
    json += "\"ntp\":" + String(useNTP ? "true" : "false") + "}";

    wsServer.broadcastTXT(json);
}

void WebUI::handleOTAForm()
{
    String html = getHTMLHeader("Firmware Update");
    html += generateOTAForm();
    html += getHTMLFooter();
    server.send(200, "text/html", html);
}

void WebUI::handleOTAUpload()
{
    // KNOWN ISSUE: CSRF validation here is post-upload — by this point the
    // upload chunk handler has already called Update.end(true), staging the
    // new firmware. A CSRF attacker can therefore still flash the device.
    // Closing this gap requires moving the upload-chunk handler into a method
    // that can refuse Update.begin() if CSRF is invalid; that's a refactor
    // for a separate change. For now we at least block the *reboot* from
    // happening, which gives the legitimate user a chance to react if the
    // upload was triggered without their consent.
    if (!validateCSRFToken())
    {
        ESPLogger::error("OTA: CSRF validation failed on upload completion");
        server.send(403, "text/plain", "CSRF token missing or invalid. Firmware may have been staged but reboot is blocked.");
        return;
    }

    if (Update.hasError())
    {
        sendError("OTA Update failed!");
    }
    else
    {
        sendSuccessWithRedirect(
            "Firmware update successful! Device will reboot and restart...",
            getRedirectUrl("/"),
            10000 // 10 seconds for reboot and startup
        );
        delay(2000);
        ESP.restart();
    }
}

void WebUI::handleNotFound()
{
    // Check if we're in AP mode for captive portal functionality
    if (wifiManager.isAPMode())
    {
        // Captive portal: redirect all requests to the configuration page
        ESPLogger::info("Captive portal redirect: %s -> /", server.uri().c_str());

        // For common captive portal detection URLs, redirect to config page
        String uri = server.uri();
        if (uri.indexOf("generate_204") >= 0 ||       // Android
            uri.indexOf("hotspot-detect") >= 0 ||     // iOS
            uri.indexOf("ncsi.txt") >= 0 ||           // Windows
            uri.indexOf("connectivity-check") >= 0 || // Chrome
            uri == "/redirect" ||
            uri == "/fwlink/" ||
            uri.startsWith("/fwlink"))
        {
            // Send redirect response
            server.sendHeader("Location", "http://" + wifiManager.getIPAddress() + "/", true);
            server.send(302, "text/plain", "Redirecting to setup page...");
            return;
        }

        // For any other request in AP mode, serve the configuration page directly
        handleRoot();
        return;
    }

    // Normal 404 handling for STA mode
    String message = "File Not Found\n\n";
    message += "URI: " + server.uri() + "\n";
    message += "Method: " + String((server.method() == HTTP_GET) ? "GET" : "POST") + "\n";
    server.send(404, "text/plain", message);
}

void WebUI::handleCaptivePortal()
{
    // Handle captive portal detection requests
    if (wifiManager.isAPMode())
    {
        ESPLogger::info("Captive portal detection: %s", server.uri().c_str());

        // Redirect to the main configuration page
        server.sendHeader("Location", "http://" + wifiManager.getIPAddress() + "/", true);
        server.send(302, "text/html", "<!DOCTYPE html><html><head><title>Redirecting...</title><meta http-equiv='refresh' content='0;url=/'></head><body>Redirecting to configuration...</body></html>");
    }
    else
    {
        // Not in AP mode, send normal response
        server.send(204, "text/plain", "");
    }
}

String WebUI::generateConfigForm()
{
    String html = "<div class='container'>";

    // Show setup mode notification if in AP mode
    if (wifiManager.isAPMode())
    {
        html += "<div class='alert alert-warning'>";
        html += "📡 <strong>Setup Mode Active</strong><br>";
        html += "You are connected to the device's setup network: <strong>" + escapeHtml(wifiManager.getSSID()) + "</strong><br>";
        html += "Configure WiFi settings below to connect the device to your network.<br>";
        html += "Once connected, you can access the device at <strong>http://" + escapeHtml(wifiManager.getHostname()) + ".local</strong>";
        html += "</div>";
    }

    html += "<div class='card'>";
    html += "<div class='card-header'>⚙️ Device Configuration</div>";
    html += "<form method='POST' action='/save' onsubmit='return validateForm()'>";
    html += csrfHiddenInput();

    // WiFi Configuration
    html += "<fieldset><legend>WiFi Settings</legend>";
    html += textInput("wifi_ssid", config.getString("wifi_ssid"), "WiFi SSID");
    html += textInput("wifi_password", config.getString("wifi_password"), "WiFi Password", "password");
    html += textInput("hostname", config.getString("hostname"), "Device Hostname");
    html += "<div style='font-size:0.75rem;color:var(--text-light);margin-top:-0.5rem;margin-bottom:1rem'>Used for DHCP and mDNS (e.g., hostname.local)</div>";
    html += checkboxInput("ap_enabled", config.getBool("ap_enabled"), "Enable AP Fallback");
    html += textInput("ap_ssid", config.getString("ap_ssid"), "AP SSID");
    html += textInput("ap_password", config.getString("ap_password"), "AP Password", "password");
    html += "</fieldset>";

    // MQTT Configuration
    html += "<fieldset><legend>MQTT Settings</legend>";
    html += textInput("mqtt_broker", config.getString("mqtt_broker"), "MQTT Broker");
    html += textInput("mqtt_port", String(config.getInt("mqtt_port")), "MQTT Port", "number");
    html += textInput("mqtt_username", config.getString("mqtt_username"), "MQTT Username");
    html += textInput("mqtt_password", config.getString("mqtt_password"), "MQTT Password", "password");
    html += textInput("mqtt_topic_prefix", config.getString("mqtt_topic_prefix"), "Topic Prefix");
    html += checkboxInput("retain", config.getBool("retain"), "Retain Messages");
    html += "</fieldset>";

    // Modbus Configuration
    html += "<fieldset><legend>Modbus/RS485 Settings</legend>";
    html += textInput("rtu_addr", String(config.getInt("rtu_addr")), "Slave Address", "number");
    html += selectInput("baudrate", String(config.getInt("baudrate")),
                        selectOption("9600", String(config.getInt("baudrate")), "9600") +
                            selectOption("19200", String(config.getInt("baudrate")), "19200") +
                            selectOption("38400", String(config.getInt("baudrate")), "38400") +
                            selectOption("57600", String(config.getInt("baudrate")), "57600") +
                            selectOption("115200", String(config.getInt("baudrate")), "115200"),
                        "Baud Rate");
    html += selectInput("data_bits", String(config.getInt("data_bits")),
                        selectOption("7", String(config.getInt("data_bits")), "7 bits") +
                            selectOption("8", String(config.getInt("data_bits")), "8 bits"),
                        "Data Bits");
    html += selectInput("parity", config.getString("parity"),
                        selectOption("N", config.getString("parity"), "None") +
                            selectOption("E", config.getString("parity"), "Even") +
                            selectOption("O", config.getString("parity"), "Odd"),
                        "Parity");
    html += selectInput("stop_bits", String(config.getInt("stop_bits")),
                        selectOption("1", String(config.getInt("stop_bits")), "1 bit") +
                            selectOption("2", String(config.getInt("stop_bits")), "2 bits"),
                        "Stop Bits");
    html += textInput("rs485_de_re_pin", String(config.getInt("rs485_de_re_pin")), "DE/RE Pin", "number");
    html += "</fieldset>";

    // Inverter Configuration
    html += "<fieldset><legend>Inverter Settings</legend>";
    html += selectInput("device_model", config.getString("device_model"),
                        generateDeviceOptions(config.getString("device_model")),
                        "Device Model");
    html += checkboxInput("read_storage_regs", config.getBool("read_storage_regs"), "Read Battery/Storage Registers");
    html += textInput("poll_interval_sec", String(config.getInt("poll_interval_sec")), "Poll Interval (seconds)", "number");
    html += "</fieldset>";

    // System Configuration
    html += "<fieldset><legend>System Settings</legend>";
    String logLevelOptions = selectOption("error", config.getString("log_level"), "Error - Critical issues only") +
                             selectOption("warn", config.getString("log_level"), "Warning - Issues and critical") +
                             selectOption("info", config.getString("log_level"), "Info - Normal operation") +
                             selectOption("debug", config.getString("log_level"), "Debug - All messages");
    html += selectInput("log_level", config.getString("log_level"), logLevelOptions, "Log Level");
    html += "<div style='font-size:0.75rem;color:var(--text-light);margin-top:-0.5rem;margin-bottom:1rem'>Controls verbosity of serial console and system logs</div>";
    html += "</fieldset>";

    html += "<div class='button-group'>";
    html += "<button type='submit' class='btn-primary'>💾 Save Configuration</button>";
    html += "</div>";
    html += "</form>";

    // Quick actions — destructive routes are POST + CSRF protected.
    // Each is its own <form> because GET-based clicks (img-tag CSRF) used
    // to be enough to wipe the device. confirm() runs in onsubmit so a
    // misclick can be cancelled.
    html += "<div class='card' style='margin-top:1rem;'>";
    html += "<div class='card-header'>🔧 Quick Actions</div>";
    html += "<div class='button-group'>";

    html += confirmActionForm("/reboot",
                              "🔄 Reboot Device",
                              "Reboot the device now?",
                              "btn-warning");
    html += confirmActionForm("/factory",
                              "🏭 Factory Reset",
                              "⚠️ This will erase ALL settings and restart the device. Continue?",
                              "btn-danger");
    html += "</div>";
    html += "</div>";

    html += "</div>"; // Close card
    html += "</div>"; // Close container

    // JavaScript for form validation only — confirmations live in the
    // onsubmit handlers of confirmActionForm().
    html += "<script>";
    html += "function validateForm(){";
    html += "var ssid=document.querySelector('input[name=\"wifi_ssid\"]').value;";
    html += "var broker=document.querySelector('input[name=\"mqtt_broker\"]').value;";
    html += "if(!ssid.trim()){alert('WiFi SSID is required');return false;}";
    html += "if(!broker.trim()){alert('MQTT Broker is required');return false;}";
    html += "return true;";
    html += "}";
    html += "</script>";

    return html;
}

String WebUI::generateStatusPage()
{
    String html = "<div class='container'>";

    // System Status Cards
    html += "<div class='grid grid-3'>";

    // Connection Status
    html += "<div class='card'>";
    html += "<div class='card-header'>🌐 Connection Status</div>";
    html += "<div class='status-grid'>";

    // WiFi Status
    String wifiStatus = wifiManager.isAPMode() ? "AP Mode" : (wifiManager.isConnected() ? "Connected" : "Disconnected");
    String wifiClass = wifiManager.isConnected() ? "success" : (wifiManager.isAPMode() ? "warning" : "danger");
    html += "<div class='status-card " + wifiClass + "'>";
    html += "<div class='status-value'>" + wifiStatus + "</div>";
    html += "<div class='status-label'>WiFi Status</div>";
    html += "</div>";

    if (!wifiManager.isAPMode() && wifiManager.isConnected())
    {
        int rssi = wifiManager.getRSSI();
        String rssiClass = rssi > -50 ? "success" : (rssi > -70 ? "warning" : "danger");
        html += "<div class='status-card " + rssiClass + "'>";
        html += "<div class='status-value'>" + String(rssi) + " dBm</div>";
        html += "<div class='status-label'>Signal Strength</div>";
        html += "</div>";
    }

    html += "</div>";

    // Network details — all values that originate from config or the WiFi
    // stack are HTML-escaped because an SSID, hostname, or device-model name
    // can contain attacker-controlled bytes (esp. via the captive-portal
    // setup step where the user types whatever).
    html += "<div style='margin-top:1rem;font-size:0.875rem;color:var(--text-light)'>";
    html += "<div><strong>SSID:</strong> " + escapeHtml(wifiManager.getSSID()) + "</div>";
    html += "<div><strong>IP:</strong> " + escapeHtml(wifiManager.getIPAddress()) + "</div>";
    if (!wifiManager.isAPMode())
    {
        html += "<div><strong>Hostname:</strong> " + escapeHtml(wifiManager.getNetworkHostname()) + "</div>";
        String mdnsStatus = wifiManager.ismDNSActive() ? "✅ Active" : "❌ Inactive";
        html += "<div><strong>mDNS Status:</strong> " + mdnsStatus + "</div>";
        String hostname = wifiManager.getHostname();
        String mdnsUrl = "http://" + hostname + ".local";
        html += "<div><strong>mDNS Access:</strong> <a href='" + escapeHtml(mdnsUrl) + "' target='_blank'>" + escapeHtml(hostname) + ".local</a></div>";
        String ip = wifiManager.getIPAddress();
        html += "<div><strong>Direct IP:</strong> <a href='http://" + escapeHtml(ip) + "' target='_blank'>" + escapeHtml(ip) + "</a></div>";
    }
    html += "</div>";
    html += "</div>";

    // Device Information
    html += "<div class='card'>";
    html += "<div class='card-header'>🔧 Device Information</div>";
    html += "<div style='margin-bottom:1rem'>";
    html += "<div class='status-card'>";
    html += "<div class='status-value'>" + escapeHtml(wifiManager.getDeviceId()) + "</div>";
    html += "<div class='status-label'>Device ID</div>";
    html += "</div>";
    html += "</div>";

    html += "<div style='font-size:0.875rem;color:var(--text-light)'>";
    html += "<div><strong>MAC:</strong> " + escapeHtml(wifiManager.getMACAddress()) + "</div>";
    html += "<div><strong>Model:</strong> " + escapeHtml(config.getString("device_model")) + "</div>";
    html += "<div><strong>Version:</strong> " + escapeHtml(String(FIRMWARE_VERSION)) + "</div>";
    html += "<div><strong>Build Mode:</strong> " + escapeHtml(String(BUILD_MODE)) + "</div>";
    html += "<div><strong>Git:</strong> " + escapeHtml(String(GIT_HASH)) + " (" + escapeHtml(String(GIT_BRANCH)) + ")</div>";
    html += "<div><strong>Built:</strong> " + escapeHtml(String(BUILD_TIMESTAMP)) + "</div>";
    html += "<div><strong>Poll Interval:</strong> " + String(config.getInt("poll_interval_sec")) + "s</div>";
    html += "<div><strong>Log Level:</strong> " + escapeHtml(config.getString("log_level", "info")) + "</div>";
    html += "</div>";
    html += "</div>";

    // System Resources
    html += "<div class='card'>";
    html += "<div class='card-header'>📊 System Resources</div>";
    html += "<div class='status-grid'>";

    // Memory status
    uint32_t freeHeap = ESP.getFreeHeap();
    String memClass = freeHeap > 100000 ? "success" : (freeHeap > 50000 ? "warning" : "danger");
    html += "<div class='status-card " + memClass + "'>";
    html += "<div class='status-value'>" + String(freeHeap / 1024) + " KB</div>";
    html += "<div class='status-label'>Free Memory</div>";
    html += "</div>";

    // Uptime
    unsigned long uptimeSeconds = millis() / 1000;
    unsigned long days = uptimeSeconds / 86400;
    unsigned long hours = (uptimeSeconds % 86400) / 3600;
    unsigned long minutes = (uptimeSeconds % 3600) / 60;

    String uptimeStr;
    if (days > 0)
        uptimeStr = String(days) + "d " + String(hours) + "h";
    else if (hours > 0)
        uptimeStr = String(hours) + "h " + String(minutes) + "m";
    else
        uptimeStr = String(minutes) + "m " + String(uptimeSeconds % 60) + "s";

    html += "<div class='status-card success'>";
    html += "<div class='status-value'>" + uptimeStr + "</div>";
    html += "<div class='status-label'>Uptime</div>";
    html += "</div>";

    html += "</div>";
    html += "</div>";

    html += "</div>"; // Close grid

    // Actions — destructive routes are POST + CSRF protected.
    html += "<div class='card'>";
    html += "<div class='card-header'>🔧 Device Actions</div>";
    html += "<div class='button-group'>";
    html += "<button class='btn-secondary' onclick='location.reload()'>🔄 Refresh</button>";

    // Add diagnostics link if enabled
    if (!wifiManager.isAPMode())
    {
        html += confirmActionForm("/restart_mdns",
                                  "📡 Restart mDNS",
                                  "Restart the mDNS responder?",
                                  "btn-primary");
    }
    html += confirmActionForm("/reboot",
                              "🔄 Reboot",
                              "Reboot the device now?",
                              "btn-warning");
    html += confirmActionForm("/factory",
                              "🏭 Factory Reset",
                              "⚠️ This will erase ALL settings and restart the device. Continue?",
                              "btn-danger");
    html += "</div>";
    html += "</div>";

    html += "</div>"; // Close container

    // Add auto-refresh
    html += "<script>";
    html += "setTimeout(function(){location.reload();}, 30000);"; // Auto-refresh every 30 seconds
    html += "</script>";

    return html;
}

String WebUI::generateOTAForm()
{
    String html = "<div class='container'>";
    html += "<div class='card'>";
    html += "<div class='card-header'>🔄 Firmware Update</div>";

    html += "<div class='alert alert-warning'>";
    html += "⚠️ <strong>Warning:</strong> Do not power off the device during firmware update! ";
    html += "The process may take several minutes. Device will automatically reboot when complete.";
    html += "</div>";

    html += "<form method='POST' action='/update' enctype='multipart/form-data' onsubmit='return validateUpload()'>";
    // CSRF token must come BEFORE the file input so the multipart parser
    // populates server.arg("_csrf") before the firmware bytes start streaming.
    // See KNOWN ISSUE in handleOTAUpload(): we currently validate post-upload,
    // which is too late to prevent flash damage from a CSRF attacker.
    html += csrfHiddenInput();
    html += "<div class='form-group'>";
    html += "<label for='firmware'>📁 Select Firmware File (.bin):</label>";
    html += "<input type='file' name='firmware' accept='.bin' required style='padding:0.5rem;'>";
    html += "<div style='font-size:0.875rem;color:var(--text-light);margin-top:0.5rem'>";
    html += "Supported formats: .bin files only. Maximum size: 4MB";
    html += "</div>";
    html += "</div>";

    html += "<div class='button-group'>";
    html += "<button type='submit' class='btn-warning'>🚀 Upload Firmware</button>";
    html += "<button type='button' class='btn-secondary' onclick='location.href=\"/\"'>❌ Cancel</button>";
    html += "</div>";
    html += "</form>";

    html += "</div>"; // Close card
    html += "</div>"; // Close container

    // JavaScript for upload validation and progress
    html += "<script>";
    html += "function validateUpload(){";
    html += "var file = document.querySelector('input[type=\"file\"]').files[0];";
    html += "if(!file){alert('Please select a firmware file');return false;}";
    html += "if(!file.name.endsWith('.bin')){alert('Please select a .bin file');return false;}";
    html += "if(file.size > 4*1024*1024){alert('File too large (max 4MB)');return false;}";
    html += "if(confirm('Start firmware update? Device will reboot automatically when complete.')){";
    html += "document.querySelector('button[type=\"submit\"]').innerHTML='<span class=\"loading\"></span> Uploading...';";
    html += "document.querySelector('button[type=\"submit\"]').disabled=true;";
    html += "return true;";
    html += "}";
    html += "return false;";
    html += "}";
    html += "</script>";

    return html;
}

String WebUI::getHTMLHeader(const String &title)
{
    String html = "<!DOCTYPE html><html lang='en'><head>";
    html += "<meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<meta name='description' content='Modbus Bridge - Device Monitoring and Control'>";
    html += "<title>" + title + " - Modbus Bridge</title>";
    html += "<link rel='icon' type='image/svg+xml' href='data:image/svg+xml,<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"><text y=\".9em\" font-size=\"90\">⚡</text></svg>'>";
    html += getCSS();
    html += "</head><body>";

    // Header with navigation. Hamburger toggle is hidden on desktop (>=769px)
    // and shown as a tap target on mobile via the @media block in getCSS().
    html += "<header class='header'>";
    html += "<div class='container'>";
    html += "<div class='logo'>Modbus Bridge</div>";
    html += "<button class='nav-toggle' type='button' aria-label='Toggle navigation' "
            "aria-controls='primary-nav' aria-expanded='false' onclick='toggleNav(this)'>"
            "<span class='nav-toggle-icon' aria-hidden='true'>&#9776;</span></button>";
    html += "<nav class='nav' id='primary-nav'>";
    String configClass = (title == "Configuration") ? String("active") : String("");
    String statusClass = (title == "Status") ? String("active") : String("");
    String consoleClass = (title == "Console") ? String("active") : String("");
    String diagnosticsClass = (title == "Diagnostics") ? String("active") : String("");
    String updateClass = (title == "Firmware Update") ? String("active") : String("");
    html += "<a href='/' class='" + configClass + "'>⚙️ Config</a>";
    html += "<a href='/status' class='" + statusClass + "'>📊 Status</a>";
    html += "<a href='/console' class='" + consoleClass + "'>🖥️ Console</a>";
    html += "<a href='/diagnostics' class='" + diagnosticsClass + "'>🔬 Diagnostics</a>";
    html += "<a href='/update' class='" + updateClass + "'>🔄 Update</a>";
    html += "</nav>";
    html += "</div>";
    html += "</header>";
    // Mobile nav toggle. Inline so every page gets it without a separate fetch.
    html += "<script>"
            "function toggleNav(b){"
              "var n=document.getElementById('primary-nav');"
              "var open=n.classList.toggle('open');"
              "b.setAttribute('aria-expanded',open?'true':'false');"
            "}"
            "document.addEventListener('click',function(e){"
              "var n=document.getElementById('primary-nav');"
              "var b=document.querySelector('.nav-toggle');"
              "if(!n||!b||!n.classList.contains('open'))return;"
              "if(!n.contains(e.target)&&!b.contains(e.target)){"
                "n.classList.remove('open');"
                "b.setAttribute('aria-expanded','false');"
              "}"
            "});"
            "</script>";
    html += "<main class='main'>";

    return html;
}

String WebUI::getHTMLFooter()
{
    String html = "</main>";
    html += "<footer style='text-align:center;padding:2rem;color:var(--text-light);font-size:0.875rem'>";
    html += "Modbus Bridge " + escapeHtml(String(FIRMWARE_VERSION)) + " • Device ID: " + escapeHtml(wifiManager.getDeviceId());
    if (!wifiManager.isAPMode())
    {
        String h = escapeHtml(wifiManager.getHostname());
        html += " • <a href='http://" + h + ".local' style='color:var(--primary)'>http://" + h + ".local</a>";
    }
    html += "</footer>";
    html += "</body></html>";
    return html;
}

String WebUI::getCSS()
{
    return "<style>"
           ":root{--primary:#2563eb;--primary-dark:#1d4ed8;--success:#059669;--warning:#d97706;--danger:#dc2626;--bg:#f8fafc;--surface:#ffffff;--text:#1e293b;--text-light:#64748b;--border:#e2e8f0;--border-focus:#3b82f6}"
           "*{box-sizing:border-box}body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;margin:0;padding:0;background:var(--bg);color:var(--text);line-height:1.6}"
           ".header{background:linear-gradient(135deg,var(--primary),var(--primary-dark));color:white;padding:1rem 0;box-shadow:0 2px 10px rgba(0,0,0,0.1)}"
           ".header .container{display:flex;align-items:center;justify-content:space-between}"
           ".logo{display:flex;align-items:center;gap:0.5rem;font-size:1.25rem;font-weight:bold}"
           ".logo::before{content:'⚡';font-size:1.5rem}"
           ".nav{display:flex;gap:1rem}"
           ".nav a{color:white;text-decoration:none;padding:0.5rem 1rem;border-radius:0.375rem;transition:background 0.2s}"
           ".nav a:hover,.nav a.active{background:rgba(255,255,255,0.1)}"
           ".nav-toggle{display:none;background:transparent;border:0;color:white;cursor:pointer;padding:0.5rem 0.75rem;border-radius:0.375rem;line-height:1;min-width:44px;min-height:44px}"
           ".nav-toggle:hover{background:rgba(255,255,255,0.1)}"
           ".nav-toggle-icon{font-size:1.5rem;display:block;line-height:1}"
           ".container{max-width:1200px;margin:0 auto;padding:0 1rem}"
           ".main{padding:2rem 0}"
           ".card{background:var(--surface);border-radius:0.5rem;box-shadow:0 1px 3px rgba(0,0,0,0.1);padding:1.5rem;margin-bottom:1.5rem}"
           ".card-header{display:flex;align-items:center;gap:0.5rem;margin-bottom:1rem;font-size:1.125rem;font-weight:600}"
           ".grid{display:grid;gap:1.5rem}"
           ".grid-2{grid-template-columns:repeat(auto-fit,minmax(300px,1fr))}"
           ".grid-3{grid-template-columns:repeat(auto-fit,minmax(250px,1fr))}"
           ".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:1rem}"
           ".status-card{background:linear-gradient(135deg,#f8f9fa,#e9ecef);border-radius:0.5rem;padding:1rem;text-align:center;border-left:4px solid var(--primary)}"
           ".status-card.success{border-left-color:var(--success)}"
           ".status-card.warning{border-left-color:var(--warning)}"
           ".status-card.danger{border-left-color:var(--danger)}"
           ".status-value{font-size:1.5rem;font-weight:bold;margin-bottom:0.25rem}"
           ".status-label{font-size:0.875rem;color:var(--text-light)}"
           "fieldset{border:1px solid var(--border);border-radius:0.5rem;padding:1rem;margin:1rem 0}"
           "legend{padding:0 0.5rem;font-weight:600;color:var(--text)}"
           ".form-group{margin:1rem 0}"
           "label{display:block;margin-bottom:0.5rem;font-weight:500;color:var(--text)}"
           "input,select,textarea{width:100%;padding:0.75rem;border:1px solid var(--border);border-radius:0.375rem;font-size:0.875rem;transition:border-color 0.2s,box-shadow 0.2s}"
           "input:focus,select:focus{outline:none;border-color:var(--border-focus);box-shadow:0 0 0 3px rgba(59,130,246,0.1)}"
           "input[type=checkbox]{width:auto;margin-right:0.5rem}"
           ".checkbox-group{display:flex;align-items:center;gap:0.5rem}"
           ".button-group{display:flex;gap:0.75rem;justify-content:center;margin:1.5rem 0}"
           "button{padding:0.75rem 1.5rem;border:none;border-radius:0.375rem;cursor:pointer;font-size:0.875rem;font-weight:500;transition:all 0.2s;text-decoration:none;display:inline-flex;align-items:center;gap:0.5rem}"
           "button:hover{transform:translateY(-1px);box-shadow:0 4px 12px rgba(0,0,0,0.15)}"
           ".btn-primary{background:var(--primary);color:white}"
           ".btn-primary:hover{background:var(--primary-dark)}"
           ".btn-secondary{background:#6b7280;color:white}"
           ".btn-secondary:hover{background:#4b5563}"
           ".btn-success{background:var(--success);color:white}"
           ".btn-warning{background:var(--warning);color:white}"
           ".btn-danger{background:var(--danger);color:white}"
           ".alert{padding:1rem;border-radius:0.375rem;margin:1rem 0}"
           ".alert-warning{background:#fef3c7;border:1px solid #f59e0b;color:#92400e}"
           ".alert-success{background:#d1fae5;border:1px solid #10b981;color:#065f46}"
           ".alert-danger{background:#fee2e2;border:1px solid #ef4444;color:#991b1b}"
           ".loading{display:inline-block;width:1rem;height:1rem;border:2px solid #f3f3f3;border-top:2px solid var(--primary);border-radius:50%;animation:spin 1s linear infinite}"
           "@keyframes spin{0%{transform:rotate(0deg)}100%{transform:rotate(360deg)}}"
           ".badge{display:inline-block;padding:0.25rem 0.5rem;font-size:0.75rem;font-weight:500;border-radius:0.25rem}"
           ".badge-success{background:#d1fae5;color:#065f46}"
           ".badge-warning{background:#fef3c7;color:#92400e}"
           ".badge-danger{background:#fee2e2;color:#991b1b}"
           ".text-center{text-align:center}"
           ".mt-4{margin-top:1rem}"
           ".mb-4{margin-bottom:1rem}"
           "@media(max-width:768px){"
             ".container{padding:0 0.75rem}"
             ".main{padding:1rem 0}"
             ".grid-2,.grid-3{grid-template-columns:1fr}"
             ".card{padding:1rem;margin-bottom:1rem}"
             // Header layout: logo + hamburger on row 1, drawer drops below as row 2.
             ".header .container{flex-wrap:wrap;align-items:center;gap:0.5rem;padding:0 0.75rem}"
             ".nav-toggle{display:inline-flex;align-items:center;justify-content:center}"
             // Drawer is hidden by default; .open (set by toggleNav JS) reveals it.
             // flex-basis:100% forces it onto its own row inside the wrapping flex.
             ".nav{display:none;flex-basis:100%;flex-direction:column;gap:0.25rem;"
                  "padding-top:0.5rem;border-top:1px solid rgba(255,255,255,0.15)}"
             ".nav.open{display:flex}"
             ".nav a{flex:none;width:100%;text-align:left;padding:0.75rem 1rem;font-size:1rem;border-radius:0.375rem}"
             ".button-group{flex-direction:column;gap:0.5rem}"
             ".button-group button{width:100%;justify-content:center}"
             ".status-grid{grid-template-columns:repeat(auto-fit,minmax(140px,1fr))}"
             ".status-value{font-size:1.25rem}"
             // 16px font-size on inputs prevents iOS Safari auto-zoom on focus.
             // Scoped to mobile so desktop keeps the denser 0.875rem.
             "input,select,textarea{font-size:16px}"
           "}"
           "</style>";
}

String WebUI::selectOption(const String &value, const String &current, const String &label)
{
    // value/label may originate from config; escape both. `current` only used for comparison.
    String selected = (value == current) ? " selected" : "";
    return "<option value='" + escapeHtml(value) + "'" + selected + ">" + escapeHtml(label) + "</option>";
}

String WebUI::textInput(const String &name, const String &value, const String &label, const String &type)
{
    // `value` is config-controlled and could contain attacker-stored content -> escape.
    // `name`, `type`, `label` are compile-time constants from our own code, but escape
    // anyway in case future callers pass dynamic values.
    String html = "<div class='form-group'>";
    html += "<label for='" + escapeHtml(name) + "'>" + escapeHtml(label) + ":</label>";
    html += "<input type='" + escapeHtml(type) + "' name='" + escapeHtml(name) + "' id='" + escapeHtml(name) + "' value='" + escapeHtml(value) + "'>";
    html += "</div>";
    return html;
}

String WebUI::checkboxInput(const String &name, bool checked, const String &label)
{
    String html = "<div class='form-group'>";
    html += "<div class='checkbox-group'>";
    html += "<input type='checkbox' name='" + escapeHtml(name) + "' id='" + escapeHtml(name) + "'";
    if (checked)
        html += " checked";
    html += ">";
    html += "<label for='" + escapeHtml(name) + "'>" + escapeHtml(label) + "</label>";
    html += "</div>";
    html += "</div>";
    return html;
}

String WebUI::selectInput(const String &name, const String &current, const String &options, const String &label)
{
    // NOTE: `options` is the raw HTML <option> list pre-built by selectOption() (which
    // already escapes). Do NOT re-escape it — it's already safe.
    String html = "<div class='form-group'>";
    html += "<label for='" + escapeHtml(name) + "'>" + escapeHtml(label) + ":</label>";
    html += "<select name='" + escapeHtml(name) + "' id='" + escapeHtml(name) + "'>" + options + "</select>";
    html += "</div>";
    return html;
}

bool WebUI::validateForm()
{
    // Basic validation - could be expanded
    if (server.hasArg("mqtt_port"))
    {
        int port = server.arg("mqtt_port").toInt();
        if (port < 1 || port > 65535)
        {
            sendError("Invalid MQTT port");
            return false;
        }
    }
    return true;
}

// --- Security helpers (XSS, CSRF) ---

String WebUI::escapeHtml(const String &s)
{
    // Escapes the five HTML metacharacters. Sufficient for inlining values
    // into text nodes and double-quoted attribute values. Do NOT use this for
    // values inlined into JavaScript context, URLs, or single-quoted attributes
    // without re-checking — those need their own escapers.
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); ++i)
    {
        char c = s[i];
        switch (c)
        {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&#39;";  break;
        default:   out += c;        break;
        }
    }
    return out;
}

String WebUI::generateCSRFToken()
{
    // 16 bytes of randomness, hex-encoded -> 32 char token. esp_random is
    // a hardware RNG on ESP32 and is more than enough for CSRF.
    char buf[33];
    for (int i = 0; i < 16; ++i)
    {
        uint8_t b = (uint8_t)(esp_random() & 0xFF);
        sprintf(&buf[i * 2], "%02x", b);
    }
    buf[32] = '\0';
    return String(buf);
}

bool WebUI::validateCSRFToken()
{
    if (csrfToken.isEmpty())
    {
        ESPLogger::error("CSRF: no token generated yet — refusing POST");
        return false;
    }
    if (!server.hasArg("_csrf"))
    {
        ESPLogger::warn("CSRF: missing _csrf param from %s", server.client().remoteIP().toString().c_str());
        return false;
    }
    String submitted = server.arg("_csrf");
    if (submitted != csrfToken)
    {
        ESPLogger::warn("CSRF: token mismatch from %s", server.client().remoteIP().toString().c_str());
        return false;
    }
    return true;
}

String WebUI::csrfHiddenInput() const
{
    // csrfToken is hex, no escaping needed, but be defensive anyway.
    return "<input type='hidden' name='_csrf' value='" + escapeHtml(csrfToken) + "'>";
}

String WebUI::confirmActionForm(const String &action, const String &buttonLabel,
                                const String &message, const String &btnClass) const
{
    String html = "<form method='POST' action='" + escapeHtml(action) + "' "
                  "onsubmit=\"return confirm('" + escapeHtml(message) + "');\" "
                  "style='display:inline'>";
    html += csrfHiddenInput();
    html += "<button type='submit' class='" + escapeHtml(btnClass) + "'>"
            + escapeHtml(buttonLabel) + "</button>";
    html += "</form>";
    return html;
}

void WebUI::sendError(const String &message)
{
    String html = getHTMLHeader("Error");
    html += "<div class='container'>";
    html += "<h2>Error</h2>";
    html += "<div class='warning'>" + escapeHtml(message) + "</div>";
    html += "<button onclick='history.back()'>Go Back</button>";
    html += "</div>";
    html += getHTMLFooter();
    server.send(400, "text/html", html);
}

void WebUI::sendSuccess(const String &message)
{
    String html = getHTMLHeader("Success");
    html += "<div class='container'>";
    html += "<h2>Success</h2>";
    html += "<div style='background:#d4edda;border:1px solid #c3e6cb;color:#155724;padding:10px;border-radius:4px;margin:15px 0'>";
    html += escapeHtml(message) + "</div>";
    html += "</div>";
    html += getHTMLFooter();
    server.send(200, "text/html", html);
}

String WebUI::getRedirectUrl(const String &path)
{
    // Check if user is in AP mode
    if (wifiManager.isAPMode())
    {
        return "http://192.168.4.1" + path;
    }

    // Check how the user accessed the device (IP vs hostname)
    String hostHeader = "";
    if (server.hasHeader("Host"))
    {
        hostHeader = server.header("Host");
    }

    // If accessed via IP address, redirect to IP
    if (hostHeader.indexOf('.') != -1 &&
        (hostHeader.startsWith("192.168.") ||
         hostHeader.startsWith("10.") ||
         hostHeader.startsWith("172.") ||
         hostHeader.indexOf("192.168.4.1") != -1))
    {
        // Extract IP address from host header (remove port if present)
        String ip = hostHeader;
        int portIndex = ip.indexOf(':');
        if (portIndex != -1)
        {
            ip = ip.substring(0, portIndex);
        }
        return "http://" + ip + path;
    }

    // Default to mDNS hostname for all other cases
    return "http://" + wifiManager.getHostname() + ".local" + path;
}

void WebUI::sendSuccessWithRedirect(const String &message, const String &redirectUrl, int delayMs)
{
    String html = getHTMLHeader("Success");
    html += "<div class='container'>";
    html += "<div class='card'>";
    html += "<div class='card-header'>✅ Success</div>";

    html += "<div class='alert alert-success'>";
    html += message;
    html += "</div>";

    html += "<div class='text-center'>";
    html += "<div class='loading'></div>";
    html += "<p>Redirecting in <span id='countdown'>" + String(delayMs / 1000) + "</span> seconds...</p>";
    html += "<p><a href='" + redirectUrl + "' class='btn-primary'>Continue Now</a></p>";
    html += "</div>";

    html += "</div>"; // Close card
    html += "</div>"; // Close container

    // Add JavaScript for countdown and redirect
    html += "<script>";
    html += "var countdown = " + String(delayMs / 1000) + ";";
    html += "var countdownElement = document.getElementById('countdown');";
    html += "var timer = setInterval(function() {";
    html += "  countdown--;";
    html += "  countdownElement.textContent = countdown;";
    html += "  if (countdown <= 0) {";
    html += "    clearInterval(timer);";
    html += "    window.location.href = '" + redirectUrl + "';";
    html += "  }";
    html += "}, 1000);";
    html += "</script>";

    html += getHTMLFooter();
    server.send(200, "text/html", html);
}

String WebUI::generateDeviceOptions(const String &current)
{
    String options = "";

    // Add built-in devices
    for (size_t i = 0; i < SUPPORTED_DEVICES_COUNT; i++)
    {
        options += selectOption(SUPPORTED_DEVICES[i].model, current, SUPPORTED_DEVICES[i].displayName);
    }

    // Add custom JSON devices from /devices/ directory
    if (LittleFS.begin())
    {
        if (LittleFS.exists("/devices"))
        {
            File dir = LittleFS.open("/devices");
            if (dir && dir.isDirectory())
            {
                File file = dir.openNextFile();
                while (file)
                {
                    String filename = String(file.name());
                    if (filename.endsWith(".json") && !file.isDirectory())
                    {
                        // Try to parse JSON to get device name
                        String displayName = filename; // Fallback to filename
                        JsonDocument doc;
                        DeserializationError error = deserializeJson(doc, file);
                        if (!error && doc.containsKey("name"))
                        {
                            displayName = doc["name"].as<String>() + " (Custom)";
                        }
                        else
                        {
                            displayName = filename + " (Custom)";
                        }

                        // Option value is "json:filename.json"
                        String value = "json:" + filename;
                        options += selectOption(value, current, displayName);
                    }
                    file.close();
                    file = dir.openNextFile();
                }
                dir.close();
            }
        }
    }

    return options;
}

// New WebSocket client management functions
void WebUI::addWebSocketClient(uint8_t clientId, IPAddress ip)
{
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++)
    {
        if (!wsClients[i].connected)
        {
            wsClients[i].id = clientId;
            wsClients[i].connected = true;
            wsClients[i].lastPing = millis();
            wsClients[i].remoteIP = ip;
            wsClients[i].connectTime = millis();
            wsClients[i].messageCount = 0;
            connectedClients++;
            ESPLogger::info("WebSocket client %u registered (slot %d), total clients: %d", clientId, i, connectedClients);
            break;
        }
    }
}

void WebUI::removeWebSocketClient(uint8_t clientId)
{
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++)
    {
        if (wsClients[i].id == clientId && wsClients[i].connected)
        {
            wsClients[i].connected = false;
            wsClients[i].id = 255;
            connectedClients--;
            ESPLogger::info("WebSocket client %u removed (slot %d), total clients: %d", clientId, i, connectedClients);
            break;
        }
    }
}

void WebUI::cleanupDisconnectedClients()
{
    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++)
    {
        if (wsClients[i].connected)
        {
            // Use a more conservative approach - only clean up session data, not disconnect clients
            // The WStype_DISCONNECTED event handler will clean up properly disconnected clients
            if (millis() - wsClients[i].lastPing > 3600000)
            {
                // Only reset session statistics after 1 hour of complete inactivity
                // Don't disconnect the client, just reset our tracking data to prevent memory bloat
                wsClients[i].lastPing = millis(); // Reset ping time to prevent repeated resets
                wsClients[i].messageCount = 0;    // Reset message count
                // Keep client connected, just clean up session stats silently
            }
        }
    }
}

void WebUI::broadcastToClients(const String &message)
{
    if (connectedClients > 0)
    {
        String msg = message; // Create non-const copy for the WebSocket library
        wsServer.broadcastTXT(msg);
    }
}

String WebUI::getWebSocketClientsStatus()
{
    String status = "{\"connectedClients\":" + String(connectedClients) + ",\"clients\":[";
    bool first = true;

    for (int i = 0; i < MAX_WEBSOCKET_CLIENTS; i++)
    {
        if (wsClients[i].connected)
        {
            if (!first)
                status += ",";
            status += "{\"id\":" + String(wsClients[i].id) +
                      ",\"ip\":\"" + wsClients[i].remoteIP.toString() + "\"" +
                      ",\"connected\":" + String((millis() - wsClients[i].connectTime) / 1000) +
                      ",\"messages\":" + String(wsClients[i].messageCount) + "}";
            first = false;
        }
    }

    status += "]}";
    return status;
}

// New route handlers
void WebUI::handleConsole()
{
    String html = getHTMLHeader("Console");
    html += generateConsolePage();
    html += getHTMLFooter();
    server.send(200, "text/html", html);
}

void WebUI::handleLogConfig()
{
    if (server.method() != HTTP_POST)
    {
        server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
        return;
    }

    // Add security headers
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
    server.sendHeader("X-XSS-Protection", "1; mode=block");

    String body = server.arg("plain");
    if (body.length() == 0 || body.length() > 256)
    { // Input size validation
        server.send(400, "application/json", "{\"error\":\"Invalid request body size\"}");
        return;
    }

    // Input validation: only allow alphanumeric characters and basic JSON syntax
    for (int i = 0; i < body.length(); i++)
    {
        char c = body.charAt(i);
        if (!isalnum(c) && c != '{' && c != '}' && c != '"' && c != ':' && c != ' ' && c != '_')
        {
            server.send(400, "application/json", "{\"error\":\"Invalid characters in request\"}");
            return;
        }
    }

    // Simple JSON parsing for log level with validation
    if (body.indexOf("\"level\":") >= 0)
    {
        int start = body.indexOf("\"level\":\"") + 9;
        int end = body.indexOf("\"", start);
        if (start > 8 && end > start && (end - start) < 10)
        { // Reasonable length check
            String newLevel = body.substring(start, end);
            newLevel.toLowerCase();

            // Strict validation of allowed log levels
            if (newLevel != "error" && newLevel != "warn" && newLevel != "info" && newLevel != "debug")
            {
                server.send(400, "application/json", "{\"error\":\"Invalid log level value\"}");
                return;
            }

            ESPLogger::LogLevel level = ESPLogger::stringToLogLevel(newLevel.c_str());
            ESPLogger::setLevel(level);
            config.setString("log_level", newLevel);
            config.save();

            ESPLogger::info("Log level changed to: %s via API", newLevel.c_str());

            // Broadcast level change to all WebSocket clients
            String notification = "{\"type\":\"levelChange\",\"level\":\"" + escapeJsonString(newLevel) +
                                  "\",\"timestamp\":" + String(millis()) + "}";
            broadcastToClients(notification);

            server.send(200, "application/json", "{\"success\":true,\"level\":\"" + escapeJsonString(newLevel) + "\"}");
            return;
        }
    }

    server.send(400, "application/json", "{\"error\":\"Invalid log level format\"}");
}

void WebUI::handleAPIStatus()
{
    // Add security headers
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("X-Frame-Options", "DENY");
    server.sendHeader("X-XSS-Protection", "1; mode=block");
    server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

    String json = "{";
    json += "\"device\":\"" + String(DEVICE_NAME) + "\",";
    json += "\"version\":\"" + String(FIRMWARE_VERSION) + "\",";
    json += "\"uptime\":" + String(millis() / 1000) + ",";
    json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"logLevel\":\"" + String(ESPLogger::logLevelToString(ESPLogger::getLevel())) + "\",";
    json += "\"wifi\":{";
    json += "\"connected\":" + String(wifiManager.isConnected() ? "true" : "false") + ",";
    json += "\"ssid\":\"" + wifiManager.getSSID() + "\",";
    json += "\"ip\":\"" + wifiManager.getIPAddress() + "\",";
    json += "\"rssi\":" + String(wifiManager.getRSSI());
    json += "},";
    json += "\"ntp\":{";
    json += "\"synced\":" + String(wifiManager.isNTPSynced() ? "true" : "false") + ",";
    json += "\"time\":\"" + wifiManager.getCurrentTime() + "\"";
    json += "},";
    json += "\"websocket\":" + getWebSocketClientsStatus() + ",";
    json += "\"logBuffer\":{\"count\":" + String(logBufferCount) + ",\"size\":" + String(LOG_BUFFER_SIZE) + "}";
    json += "}";

    server.send(200, "application/json", json);
}

String WebUI::generateConsolePage()
{
    // Use standard page layout like other pages
    String html = "<div class='container'>";

    // Console card with header
    html += "<div class='card'>";
    html += "<div class='card-header'>🖥️ Real-time Console";
    if (wifiManager.isNTPSynced())
    {
        html += " <span style='float:right;font-size:0.875rem;'>🕐 NTP Synchronized</span>";
    }
    else
    {
        html += " <span style='float:right;font-size:0.875rem;'>⏰ Local Time</span>";
    }
    html += "</div>";

    // Control buttons (removed connect/disconnect - auto-connects)
    html += "<div class='button-group'>";
    html += "<button id='clearBtn' class='btn-secondary'>🗑️ Clear</button>";
    html += "<button id='scrollBtn' class='btn-secondary'>📜 Follow: ON</button>";
    html += "<button id='bottomBtn' class='btn-secondary'>⬇️ Bottom</button>";
    html += "<span id='connectionStatus' style='margin-left:1rem;font-weight:bold;color:#6b7280;'>Connecting...</span>";
    html += "</div>";

    // Console output area with scroll indicator
    html += "<div style='position:relative;'>";
    html += "<div id='consoleOutput' style='background:#0f172a;color:#e2e8f0;font-family:monospace;height:400px;overflow-y:auto;padding:1rem;border:1px solid var(--border);border-radius:0.375rem;margin:1rem 0;'>";
    html += "<div style='color:#10b981;'>🚀 Console ready - connecting automatically...</div>";
    html += "</div>";
    html += "<div id='scrollIndicator' class='scroll-indicator'>📜 Scroll up to pause auto-follow</div>";
    html += "</div>";

    html += "</div>"; // Close card

    // Log level controls card
    html += "<div class='card'>";
    html += "<div class='card-header'>⚙️ Log Level Controls</div>";
    html += "<div class='button-group'>";
    html += "<button class='btn btn-level btn-secondary' data-level='error'>❌ Error</button>";
    html += "<button class='btn btn-level btn-secondary' data-level='warn'>⚠️ Warning</button>";
    html += "<button class='btn btn-level btn-secondary' data-level='info'>ℹ️ Info</button>";
    html += "<button class='btn btn-level btn-secondary' data-level='debug'>🔍 Debug</button>";
    html += "</div>";
    html += "<div style='font-size:0.875rem;color:var(--text-light);margin-top:1rem;'>";
    html += "Click a level button to change the device's log verbosity in real-time.";
    html += "</div>";
    html += "</div>"; // Close card

    html += "</div>"; // Close container

    html += generateConsoleCSS();
    html += generateConsoleJS();

    return html;
}

// Store CSS in PROGMEM to save RAM
const char CONSOLE_CSS[] PROGMEM = R"(
<style>
.log-error{color:#ef4444;}
.log-warn{color:#f59e0b;}
.log-info{color:#06b6d4;}
.log-debug{color:#10b981;}
.btn-level.active{background:var(--success);color:white;}
#consoleOutput{position:relative;}
#consoleOutput::-webkit-scrollbar{width:8px;}
#consoleOutput::-webkit-scrollbar-track{background:#1e293b;}
#consoleOutput::-webkit-scrollbar-thumb{background:#64748b;border-radius:4px;}
#consoleOutput::-webkit-scrollbar-thumb:hover{background:#94a3b8;}
.scroll-indicator{position:absolute;top:10px;right:10px;background:rgba(59,130,246,0.9);color:white;padding:4px 8px;border-radius:4px;font-size:0.75rem;display:none;}
</style>
)";

String WebUI::generateConsoleCSS()
{
    return String(FPSTR(CONSOLE_CSS));
}

// Store JavaScript in PROGMEM to save RAM
const char CONSOLE_JS[] PROGMEM = R"(
<script>
let ws,connected=false,msgCount=0,autoScroll=true,userScrolled=false,reconnecting=false;
const output=document.getElementById('consoleOutput');
function connect(){
if(connected||reconnecting)return;
reconnecting=true;
ws=new WebSocket((location.protocol==='https:'?'wss:':'ws:')+'//'+location.hostname+':81');
ws.onopen=()=>{connected=true;reconnecting=false;updateConnectionStatus('🟢 Connected','#10b981');};
ws.onmessage=e=>{const d=JSON.parse(e.data);if(d.type==='log')addLog(d);else if(d.type==='init')addLog({level:'info',message:d.message,timestamp:Date.now()});};
ws.onclose=()=>{connected=false;reconnecting=false;updateConnectionStatus('🔴 Disconnected','#ef4444');setTimeout(connect,3000);};
ws.onerror=()=>{connected=false;reconnecting=false;updateConnectionStatus('⚠️ Error','#f59e0b');setTimeout(connect,5000);};
}
function updateConnectionStatus(text,color){
const status=document.getElementById('connectionStatus');
if(status){status.textContent=text;status.style.color=color;}
}
function addLog(log){
msgCount++;if(msgCount>100){const children=output.children;for(let i=0;i<50;i++)children[0].remove();msgCount=50;}
const div=document.createElement('div');
div.className='log-'+log.level;
const icons={error:'❌',warn:'⚠️',info:'ℹ️',debug:'🔍'};
const levels={error:'ERR',warn:'WRN',info:'INF',debug:'DBG'};
const logTime=new Date(log.timestamp).toLocaleTimeString();
div.innerHTML='['+logTime+'] <span style="font-weight:bold;">'+icons[log.level]+' ['+levels[log.level]+']</span> '+log.message;
output.appendChild(div);
if(autoScroll&&!userScrolled)output.scrollTop=output.scrollHeight;
}
function toggleScroll(){
autoScroll=!autoScroll;
document.getElementById('scrollBtn').textContent='📜 Follow: '+(autoScroll?'ON':'OFF');
document.getElementById('scrollBtn').className='btn '+(autoScroll?'btn-primary':'btn-secondary');
if(autoScroll){userScrolled=false;output.scrollTop=output.scrollHeight;}
}
function scrollToBottom(){output.scrollTop=output.scrollHeight;userScrolled=false;}
function detectUserScroll(){
const isAtBottom=output.scrollTop+output.clientHeight>=output.scrollHeight-5;
userScrolled=!isAtBottom;
const indicator=document.getElementById('scrollIndicator');
if(userScrolled&&autoScroll){indicator.style.display='block';}else{indicator.style.display='none';}
}
function changeLevel(level){if(connected)fetch('/api/log/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({level})});}
document.addEventListener('DOMContentLoaded',()=>{
document.getElementById('clearBtn').onclick=()=>{output.innerHTML='';msgCount=0;userScrolled=false;};
document.getElementById('scrollBtn').onclick=toggleScroll;
document.getElementById('bottomBtn').onclick=scrollToBottom;
output.addEventListener('scroll',detectUserScroll);
document.querySelectorAll('.btn-level').forEach(btn=>btn.onclick=()=>changeLevel(btn.dataset.level));
updateConnectionStatus('🔄 Connecting...','#f59e0b');
setTimeout(connect,1000);
});
</script>
)";

String WebUI::generateConsoleJS()
{
    return String(FPSTR(CONSOLE_JS));
}
