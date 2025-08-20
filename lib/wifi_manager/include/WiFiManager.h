#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <time.h>
#include "Config.h"

/**
 * @brief WiFi connection manager with STA and AP fallback
 * Handles WiFi connection, reconnection, and AP mode for configuration
 */
class WiFiManager
{
public:
    WiFiManager(Config &config);
    ~WiFiManager();

    bool begin();
    void handleConnection();
    bool isConnected() const;
    bool isAPMode() const;
    String getIPAddress() const;
    int getRSSI() const;
    String getSSID() const;
    String getMACAddress() const;
    String getDeviceId() const;
    String getHostname() const;
    String getNetworkHostname() const;

    // mDNS management
    void restartmDNS();
    bool ismDNSActive() const;

    // NTP time synchronization
    void setupNTP();
    bool isNTPSynced() const;
    String getCurrentTime() const;

    // Connection management
    bool connectSTA();
    bool startAP();
    void disconnect();
    void reconnect();

    // mDNS management
    bool setupmDNS();

private:
    Config &config;
    DNSServer dnsServer;
    bool apMode;
    bool initialized;
    unsigned long lastReconnectAttempt;
    unsigned long reconnectInterval;
    int reconnectAttempts;
    bool ntpSynced;

    static const unsigned long MIN_RECONNECT_INTERVAL = 5000;   // 5 seconds
    static const unsigned long MAX_RECONNECT_INTERVAL = 300000; // 5 minutes
    static const int MAX_RECONNECT_ATTEMPTS = 10;

    void updateReconnectInterval();
    String macToDeviceId(const uint8_t *mac) const;
};
