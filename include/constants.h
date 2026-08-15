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
#define DDS238_SIMULATOR_MODEL "dds238_simulator"

// SolPlanet ASW Register Addresses (Modbus)
// Note: Both GEN and HYBRID series use the same register map (31xxx INPUT registers, FC 0x04)
// Phase configuration is auto-detected from register 31001
#define ASW_DEVICE_TYPE_ADDR 0x03E8  // Register 31001: ASCII '1'=Single phase, '3'=Three phase

// Device List Structure
struct DeviceInfo
{
    const char *model;
    const char *displayName;
    const char *type;
};

// Supported Devices List
static const DeviceInfo SUPPORTED_DEVICES[] = {
    {SOLPLANET_ASW_MODEL, "SolPlanet ASW (GEN & HYBRID)", "inverter"},
    {HIKING_DDS238_MODEL, "Hiking DDS238 Smart Meter", "meter"},
    {DDS238_SIMULATOR_MODEL, "DDS238 Energy Meter Simulator", "meter"}};

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
// Availability of the *device under monitoring* (NOT the ESP32 itself).
// Published as "online" / "offline" by the Poller after K consecutive read failures.
// Used by Home Assistant via `availability_topic` to mark sensors unavailable
// instead of showing the last retained value as live.
#define MQTT_METRIC_AVAILABILITY "availability"

// ---------------------------------------------------------------------------
// Log store (lib/log_store) - RTC-RAM ring buffer for post-mortem boot logs
// ---------------------------------------------------------------------------

// Ring payload size. RTC slow RAM gives 7664 usable bytes (memory.ld reserves
// 512 of the 8192 for the SDK); 7168 + 32 bytes of region header leaves ~464
// bytes of slack for a future SDK bump. Overflowing the segment is a link-time
// ASSERT, not a runtime surprise.
#define LOG_STORE_DATA_BYTES 7168

// The data area is split into two independent rings. Boot-window records go to
// the first, everything afterwards to the second, and each evicts only its own
// oldest. Without this split a steady WARN/ERROR source - the known RS485 CRC
// noise emits an ERROR plus a WARN every ~25s - fills the whole ring and evicts
// the startup narrative within ~16 minutes, which defeats the entire point.
// Must be a multiple of 4 and smaller than LOG_STORE_DATA_BYTES.
//
// Sized from a real info-level boot: the startup narrative runs to roughly 90
// records at ~68 bytes each (many lines carry 4-byte emoji), so 3584 truncated
// it part way through WiFi init. 4608 covers the run through MQTT connect and
// the first poll, leaving 2560 - about 38 records - for the rolling segment,
// which only needs to hold the previous session's final moments.
#define LOG_STORE_BOOT_BYTES 4608

// Longest message body retained per record. Lines above this are stored
// truncated and flagged; ESPLogger's own format buffer is 256 bytes.
#define LOG_STORE_MAX_MSG 160

// Everything that passes the active log level is captured for this long after
// boot, so the startup narrative survives in full. After that only WARN/ERROR
// are kept, to stop routine poll chatter evicting the boot log.
#define LOG_BOOT_WINDOW_MS 60000

// An ERROR re-opens full capture for this long, so the aftermath of a failure
// is recorded rather than just the failure line itself.
#define LOG_ERROR_WINDOW_MS 10000

// Minimum gap between re-arms, so a sustained error storm cannot hold the
// capture window permanently open and fill the ring with routine lines.
#define LOG_ERROR_REARM_MIN_GAP_MS 60000

#endif // CONSTANTS_H
