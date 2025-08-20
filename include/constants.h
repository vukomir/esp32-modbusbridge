#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <Arduino.h>

// MQTT Configuration
#define MQTT_MAX_PACKET_SIZE 512

// Device Information
#define DEVICE_NAME "Modbus-Bridge"

// Version information (defined by build system or defaults)
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

#ifndef GIT_HASH
#define GIT_HASH "unknown"
#endif

#ifndef GIT_BRANCH
#define GIT_BRANCH "unknown"
#endif

// Build information from C++ built-ins
#define BUILD_DATE __DATE__
#define BUILD_TIME __TIME__
#define BUILD_TIMESTAMP BUILD_DATE " " BUILD_TIME

// Build mode detection
#ifdef PRODUCTION_BUILD
#define BUILD_MODE "production"
#elif defined(DEBUG_MODE)
#define BUILD_MODE "development"
#else
#define BUILD_MODE "unknown"
#endif

// Device Models
#define DEFAULT_DEVICE_MODEL "solplanet_asw_gen"
#define SOLPLANET_ASW_MODEL "solplanet_asw_gen"
#define HIKING_DDS238_MODEL "hiking_dds238"

// Device List Structure
struct DeviceInfo
{
    const char *model;
    const char *displayName;
    const char *type;
};

// Supported Devices List
static const DeviceInfo SUPPORTED_DEVICES[] = {
    {SOLPLANET_ASW_MODEL, "SolPlanet ASW GEN", "inverter"},
    {HIKING_DDS238_MODEL, "Hiking DDS238 Smart Meter", "meter"}};

#define SUPPORTED_DEVICES_COUNT (sizeof(SUPPORTED_DEVICES) / sizeof(DeviceInfo))

// Topic Configuration - Data Types
#define MQTT_DATA_TYPE_STATUS "status"
#define MQTT_DATA_TYPE_DIAGNOSTICS "diagnostics"
#define MQTT_DATA_TYPE_HARDWARE "hardware"
#define MQTT_DATA_TYPE_CONFIG "config"

// Topic Configuration - Metrics
#define MQTT_METRIC_CONNECTION "connection"
#define MQTT_METRIC_POLL_STATUS "poll_status"
#define MQTT_METRIC_MAX485_STATUS "max485_status"
#define MQTT_METRIC_INVERTER_STATUS "inverter_status"
#define MQTT_METRIC_SYSTEM_INFO "system_info"
#define MQTT_METRIC_RSSI "rssi"
#define MQTT_METRIC_UPTIME "uptime"
#define MQTT_METRIC_CONFIG_INFO "config_info"

#endif // CONSTANTS_H
