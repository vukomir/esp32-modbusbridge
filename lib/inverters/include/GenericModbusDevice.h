#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "InverterInterface.h"
#include "Config.h"
#include "ModbusClient.h"

/**
 * @brief Generic Modbus device that reads configuration from JSON
 *
 * Allows adding new devices without recompiling firmware.
 * JSON format defines registers, scaling, and units.
 *
 * Schema versions:
 *   missing or 1 - flat registers[] only (legacy).
 *   2            - adds groups[], per-field bits[], per-field nan_value.
 */
class GenericModbusDevice : public InverterInterface
{
public:
    explicit GenericModbusDevice(ModbusClient &modbus, Config &config, const String &jsonFile);
    bool begin() override;
    bool readBasic(std::vector<TelemetryPoint> &out) override;
    bool readStorage(std::vector<TelemetryPoint> &out) override;

    // File-size cap (bytes). Enforced both here and at WebUI upload time.
    // 16 KB caps ArduinoJson v7 dynamic-doc heap usage at ~32-48 KB during parse,
    // which fits comfortably under the ~100 KB free-heap budget on this board
    // after WiFi/MQTT/WebSocket buffers are live.
    static constexpr size_t MAX_JSON_FILE_BYTES = 16384;

    // Supported schema versions. Schema 1 is implicit (no schema_version key).
    static constexpr int MAX_SUPPORTED_SCHEMA_VERSION = 2;

    // Modbus spec hard cap on registers per transaction (FC 03/04).
    static constexpr uint8_t MAX_GROUP_COUNT = 125;

    // Validate JSON content without instantiating the device. Used by the
    // WebUI upload handler to surface parse errors to the user before write.
    // Returns true if valid; on false, errorOut contains a human-readable message.
    static bool validateJSON(const String &content, String &errorOut);

    // Validate a user-supplied filename intended to be concatenated into
    // /devices/<filename>. Rejects path traversal, absolute paths, hidden files,
    // and anything that isn't a plain *.json basename. Single source of truth used
    // by the WebUI upload/delete handlers AND by InverterFactory when loading a
    // `device_model = "json:<filename>"` entry from config — both can be attacker-
    // influenced (HTTP body / config write).
    static bool isSafeDeviceFilename(const String &name);

private:
    ModbusClient &modbus;
    Config &config;
    String jsonFilePath;
    uint8_t slaveAddr;
    bool initialized;

    // Bitfield decode for a uint16 field. Emits one telemetry point per entry.
    struct BitField
    {
        uint16_t mask;   // mask within the 16-bit word
        uint8_t shift;   // computed from mask's trailing zeros if not specified
        String name;     // telemetry point name (becomes MQTT topic suffix)
    };

    // A single named value within a flat-read register (legacy) or a group.
    // Flat path uses RegisterDef which embeds a Field plus addr+fc.
    struct Field
    {
        uint8_t offset;              // word offset from group start (groups only; 0 for flat)
        String name;                 // telemetry point name
        String type;                 // "uint16", "int16", "uint32", "int32"
        float scale;
        String unit;
        bool hasNanSentinel;
        uint32_t nanSentinel;        // raw value treated as "no data"
        std::vector<BitField> bits;  // empty = publish numeric value; non-empty = publish bits, suppress numeric
    };

    // Legacy flat register entry (one Modbus transaction per entry).
    struct RegisterDef
    {
        uint16_t addr;
        uint8_t fc;
        bool isStorage;
        Field field;
    };

    // Block-read group: one Modbus transaction reads `count` words starting at `startAddr`,
    // and fields[*].offset slices named values out of the response.
    struct RegisterGroup
    {
        String name;                 // log/diag only, not published
        uint16_t startAddr;
        uint8_t count;
        uint8_t fc;
        bool isStorage;
        std::vector<Field> fields;
    };

    int schemaVersion;
    std::vector<RegisterDef> registers;
    std::vector<RegisterGroup> groups;
    String deviceName;
    String deviceId;

    bool loadJSON();

    // Shared parse routines used by loadJSON and validateJSON.
    // populates out* on success; appends error message to errorOut on failure.
    static bool parseDoc(const JsonDocument &doc,
                         int &schemaOut,
                         std::vector<RegisterDef> &registersOut,
                         std::vector<RegisterGroup> &groupsOut,
                         String &errorOut);
    static bool parseField(JsonObjectConst obj, int schemaVer, Field &out, String &errorOut);
    static bool parseBits(JsonArrayConst arr, std::vector<BitField> &out, String &errorOut);
    static bool parseRegisterDef(JsonObjectConst obj, int schemaVer, RegisterDef &out, String &errorOut);
    static bool parseGroup(JsonObjectConst obj, int schemaVer, RegisterGroup &out, String &errorOut);
    static bool checkDuplicateNames(const std::vector<RegisterDef> &regs,
                                    const std::vector<RegisterGroup> &grps,
                                    String &errorOut);

    // Helpers used at read time.
    static uint8_t wordSize(const String &type);
    static bool decodeValue(const uint16_t *words, const Field &f, float &valueOut, uint32_t &rawOut);

    // Read paths.
    bool readFlatRegister(const RegisterDef &reg, std::vector<TelemetryPoint> &out);
    bool readGroup(const RegisterGroup &grp, std::vector<TelemetryPoint> &out);
    void emitField(const Field &f, const uint16_t *words, std::vector<TelemetryPoint> &out);
    // Phase implementation shared by readBasic (storagePhase=false) and readStorage (true).
    // The per-poll budget covers flat AND group reads in this phase.
    bool readPhase(bool storagePhase, std::vector<TelemetryPoint> &out);

    // Per-poll budget. Returns ms cap derived from poll_interval_sec config
    // (half of poll interval, capped at 5000 ms, floor 500 ms).
    uint32_t pollBudgetMs() const;
};
