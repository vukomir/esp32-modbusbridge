#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "Config.h"
#include "WiFiManager.h"
#include <ESPLogger.h>

// Log buffer configuration - streaming optimized
#define LOG_BUFFER_SIZE 20         // Minimal buffer for recent logs only
#define LOG_MESSAGE_MAX_LENGTH 128 // Reduced for memory optimization
#define MAX_WEBSOCKET_CLIENTS 3    // Minimal concurrent connections

/**
 * @brief Web UI server for device configuration and status
 * Provides HTML forms for config, status page, factory reset, and OTA upload
 */
// Forward declaration
class ModbusClient;

class WebUI
{
public:
    WebUI(Config &config, WiFiManager &wifiManager, ModbusClient &modbusClient);
    ~WebUI();

    bool begin(uint16_t port = 80);
    void handleClient();
    void stop();
    bool isRunning() const;

private:
    Config &config;
    WiFiManager &wifiManager;
    ModbusClient &modbusClient;
    WebServer server;
    WebSocketsServer wsServer;
    bool running;

    // Route handlers
    void handleRoot();
    void handleSave();
    void handleStatus();
    void handleReboot();
    void handleFactoryReset();
    void handleOTAForm();
    void handleOTAUpload();
    void handleNotFound();
    void handleCaptivePortal();
    void handleRestartmDNS();
    void handleLogsAPI();
    void handleConsole();
    void handleLogConfig();
    void handleAPIStatus();
    void handleDiagnostics();
    void handleDiagnosticsAPI();

    // HTML generators
    String generateConfigForm();
    String generateStatusPage();
    String generateOTAForm();
    String getHTMLHeader(const String &title);
    String getHTMLFooter();
    String getCSS();

    // Form helpers
    String selectOption(const String &value, const String &current, const String &label);
    String textInput(const String &name, const String &value, const String &label, const String &type = "text");
    String checkboxInput(const String &name, bool checked, const String &label);
    String selectInput(const String &name, const String &current, const String &options, const String &label);
    String generateDeviceOptions(const String &current);

    // Validation
    bool validateForm();
    void sendError(const String &message);
    void sendSuccess(const String &message);
    void sendSuccessWithRedirect(const String &message, const String &redirectUrl, int delayMs);
    String getRedirectUrl(const String &path = "/");

    // Security helpers
    // CSRF token regenerated on each boot. Lives only in RAM; if the device
    // reboots between rendering the form and submitting it, the POST will
    // fail with 403 and the user will need to reload. That is intentional.
    String csrfToken;
    String generateCSRFToken();
    bool validateCSRFToken();          // checks server.arg("_csrf") == csrfToken
    String csrfHiddenInput() const;    // <input type='hidden' name='_csrf' value='...'>
    // Escapes &, <, >, ", ' so config values can be inlined in HTML attributes
    // and text nodes safely. Stored XSS defense.
    static String escapeHtml(const String &s);
    // Renders a confirmation form posting to `action`. Used for /reboot, /factory.
    String confirmActionForm(const String &action, const String &buttonLabel,
                             const String &message, const String &btnClass) const;

    // Log buffer management
    struct LogEntry
    {
        uint64_t timestamp; // Changed to 64-bit to hold Unix timestamps in milliseconds
        ESPLogger::LogLevel level;
        char message[LOG_MESSAGE_MAX_LENGTH];
    };

    static LogEntry logBuffer[LOG_BUFFER_SIZE];
    static int logBufferIndex;
    static int logBufferCount;
    static unsigned long logStartTime;

    static void logCallback(ESPLogger::LogLevel level, const char *message, unsigned long timestamp);
    String escapeJsonString(const String &str);

    // WebSocket handlers and client management
    struct WebSocketClient
    {
        uint8_t id;
        bool connected;
        unsigned long lastPing;
        IPAddress remoteIP;
        String userAgent;
        unsigned long connectTime;
        uint32_t messageCount;
    };

    WebSocketClient wsClients[MAX_WEBSOCKET_CLIENTS];
    uint8_t connectedClients;
    unsigned long lastClientCleanup;

    void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
    static void onWebSocketEventStatic(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
    void broadcastLogMessage(const LogEntry &entry);
    void broadcastToClients(const String &message);
    void addWebSocketClient(uint8_t clientId, IPAddress ip);
    void removeWebSocketClient(uint8_t clientId);
    void cleanupDisconnectedClients();
    String getWebSocketClientsStatus();

    // Console page generators
    String generateConsolePage();
    String generateConsoleCSS();
    String generateConsoleJS();

    static WebUI *instance; // For static callback
};
