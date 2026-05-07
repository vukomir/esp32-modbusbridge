#include "GenericModbusDevice.h"
#include <ESPLogger.h>
#include <LittleFS.h>
#include <math.h>
#include <stdlib.h>
#include <errno.h>

namespace
{
    // Returns true if s starts with a 0x/0X prefix (hex literal).
    bool hasHexPrefix(const char *s)
    {
        return s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
    }

    // Strict unsigned-integer-string parser. Accepts only:
    //   - decimal: one or more digits, no leading zeros (except "0" itself)
    //   - hex:     "0x" or "0X" prefix followed by one or more hex digits
    // Rejects: empty, leading whitespace, leading sign, leading zeros (which
    // strtoul would auto-detect as octal — a footgun for register addresses),
    // partial matches, and overflow beyond UINT32_MAX. Result fits in uint32_t.
    bool parseUnsigned32(const char *s, uint32_t &out)
    {
        if (!s || !*s) return false;

        int base;
        const char *digits;
        if (hasHexPrefix(s))
        {
            base = 16;
            digits = s + 2;
            if (!*digits) return false;  // bare "0x"
        }
        else
        {
            // Decimal only. No leading sign, no leading whitespace, no octal.
            if (s[0] < '0' || s[0] > '9') return false;
            // Reject multi-digit leading zero (would be octal under base=0).
            if (s[0] == '0' && s[1] != '\0') return false;
            base = 10;
            digits = s;
        }

        char *endptr = nullptr;
        errno = 0;
        unsigned long v = strtoul(digits, &endptr, base);
        if (endptr == digits || *endptr != '\0') return false;
        if (errno == ERANGE) return false;
        if (v > 0xFFFFFFFFul) return false;  // 32-bit truncation guard
        out = (uint32_t)v;
        return true;
    }

    // Strict signed parser for nan_value. Accepts an optional leading '-'
    // followed by the same forms parseUnsigned32 accepts. Reinterprets bits
    // so that "-1" becomes 0xFFFFFFFF (matches wire 0xFFFF for int16 / 0xFFFFFFFF
    // for int32 sentinels). Rejects whitespace, malformed input, overflow.
    bool parseSigned32AsBits(const char *s, uint32_t &out)
    {
        if (!s || !*s) return false;
        if (s[0] != '-') return false;          // signed path is only for negatives
        if (!s[1]) return false;                // bare "-"

        // Parse the magnitude using the strict unsigned path.
        uint32_t magnitude;
        if (!parseUnsigned32(s + 1, magnitude)) return false;
        if (magnitude > 0x80000000ul) return false;  // exceeds INT32_MIN
        // Two's complement reinterpret. -magnitude == ~magnitude + 1 (mod 2^32).
        out = (uint32_t)(-(int64_t)magnitude);
        return true;
    }
}

GenericModbusDevice::GenericModbusDevice(ModbusClient &modbus, Config &config, const String &jsonFile)
    : modbus(modbus), config(config), jsonFilePath(jsonFile), slaveAddr(1), initialized(false), schemaVersion(1)
{
    slaveAddr = config.getInt("rtu_addr", 1);
}

bool GenericModbusDevice::begin()
{
    if (initialized)
    {
        return true;
    }

    ESPLogger::info("Initializing Generic Modbus Device from JSON: %s", jsonFilePath.c_str());

    if (!loadJSON())
    {
        ESPLogger::error("Failed to load JSON configuration");
        return false;
    }

    ESPLogger::info("Loaded device: %s (%s) [schema=%d, %u flat regs, %u groups]",
                    deviceName.c_str(), deviceId.c_str(), schemaVersion,
                    (unsigned)registers.size(), (unsigned)groups.size());

    initialized = true;
    return true;
}

bool GenericModbusDevice::loadJSON()
{
    if (!LittleFS.exists(jsonFilePath))
    {
        ESPLogger::error("JSON device config not found: %s", jsonFilePath.c_str());
        ESPLogger::error("Upload the JSON file via /diagnostics -> Device JSON Manager");
        return false;
    }

    File file = LittleFS.open(jsonFilePath, "r");
    if (!file)
    {
        ESPLogger::error("Failed to open JSON file: %s", jsonFilePath.c_str());
        return false;
    }

    size_t fileSize = file.size();
    if (fileSize > MAX_JSON_FILE_BYTES)
    {
        ESPLogger::error("JSON file too large: %u bytes (max %u)", (unsigned)fileSize, (unsigned)MAX_JSON_FILE_BYTES);
        file.close();
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error)
    {
        ESPLogger::error("JSON parse error: %s", error.c_str());
        return false;
    }

    deviceId = doc["device_id"] | "unknown";
    deviceName = doc["name"] | "Unknown Device";

    String parseErr;
    if (!parseDoc(doc, schemaVersion, registers, groups, parseErr))
    {
        ESPLogger::error("Device JSON validation failed: %s", parseErr.c_str());
        return false;
    }

    if (registers.empty() && groups.empty())
    {
        ESPLogger::error("No registers or groups defined");
        return false;
    }

    return true;
}

bool GenericModbusDevice::isSafeDeviceFilename(const String &name)
{
    if (name.length() == 0 || name.length() > 64) return false;
    if (name.indexOf('/') >= 0) return false;
    if (name.indexOf('\\') >= 0) return false;
    if (name.indexOf("..") >= 0) return false;
    if (name.charAt(0) == '.') return false;  // hidden files / "." / ".."
    return name.endsWith(".json");
}

bool GenericModbusDevice::validateJSON(const String &content, String &errorOut)
{
    if (content.length() > MAX_JSON_FILE_BYTES)
    {
        errorOut = "File too large (";
        errorOut += String((unsigned)content.length());
        errorOut += " bytes, max ";
        errorOut += String((unsigned)MAX_JSON_FILE_BYTES);
        errorOut += ")";
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error)
    {
        errorOut = "JSON parse error: ";
        errorOut += error.c_str();
        return false;
    }

    int schema = 1;
    std::vector<RegisterDef> regs;
    std::vector<RegisterGroup> grps;
    if (!parseDoc(doc, schema, regs, grps, errorOut))
    {
        return false;
    }

    if (regs.empty() && grps.empty())
    {
        errorOut = "No registers or groups defined";
        return false;
    }

    return true;
}

bool GenericModbusDevice::parseDoc(const JsonDocument &doc,
                                   int &schemaOut,
                                   std::vector<RegisterDef> &registersOut,
                                   std::vector<RegisterGroup> &groupsOut,
                                   String &errorOut)
{
    int schema = doc["schema_version"] | 1;
    if (schema < 1 || schema > MAX_SUPPORTED_SCHEMA_VERSION)
    {
        errorOut = "Unsupported schema_version: ";
        errorOut += String(schema);
        errorOut += " (firmware supports 1.." + String(MAX_SUPPORTED_SCHEMA_VERSION) + ")";
        return false;
    }
    schemaOut = schema;

    JsonArrayConst regsArray = doc["registers"].as<JsonArrayConst>();
    if (!regsArray.isNull())
    {
        registersOut.reserve(regsArray.size());
        for (JsonVariantConst v : regsArray)
        {
            RegisterDef def;
            if (!parseRegisterDef(v.as<JsonObjectConst>(), schema, def, errorOut))
            {
                return false;
            }
            registersOut.push_back(std::move(def));
        }
    }

    JsonArrayConst groupsArray = doc["groups"].as<JsonArrayConst>();
    if (!groupsArray.isNull())
    {
        if (schema < 2)
        {
            errorOut = "groups[] requires schema_version >= 2";
            return false;
        }
        groupsOut.reserve(groupsArray.size());
        for (JsonVariantConst v : groupsArray)
        {
            RegisterGroup grp;
            if (!parseGroup(v.as<JsonObjectConst>(), schema, grp, errorOut))
            {
                return false;
            }
            groupsOut.push_back(std::move(grp));
        }
    }

    if (!checkDuplicateNames(registersOut, groupsOut, errorOut))
    {
        return false;
    }

    return true;
}

bool GenericModbusDevice::parseField(JsonObjectConst obj, int schemaVer, Field &out, String &errorOut)
{
    out.offset = (uint8_t)(obj["offset"] | 0);
    out.name = obj["name"] | "";
    out.type = obj["type"] | "uint16";
    out.unit = obj["unit"] | "";
    out.hasNanSentinel = false;
    out.nanSentinel = 0;
    out.bits.clear();

    if (out.name.length() == 0)
    {
        errorOut = "field with empty name";
        return false;
    }

    if (out.type != "uint16" && out.type != "int16" && out.type != "uint32" && out.type != "int32")
    {
        errorOut = "field '" + out.name + "': unknown type '" + out.type + "'";
        return false;
    }

    // scale: optional, defaults to 1.0; if present must be a JSON number.
    // Reject silent string fallback (`"scale": "0.1"` was previously becoming 1.0).
    JsonVariantConst scaleV = obj["scale"];
    if (scaleV.isNull())
    {
        out.scale = 1.0f;
    }
    else if (scaleV.is<float>() || scaleV.is<double>() ||
             scaleV.is<int>() || scaleV.is<long>() ||
             scaleV.is<unsigned int>() || scaleV.is<unsigned long>())
    {
        out.scale = scaleV.as<float>();
    }
    else
    {
        errorOut = "field '" + out.name + "': scale must be a number";
        return false;
    }

    // Schema-version gate: v2-only keys must be rejected on schema 1, not silently
    // ignored. A typo / version mismatch otherwise debugs as "my sentinel doesn't fire."
    if (schemaVer < 2)
    {
        if (!obj["nan_value"].isNull())
        {
            errorOut = "field '" + out.name + "': nan_value requires schema_version 2";
            return false;
        }
        if (!obj["bits"].isNull())
        {
            errorOut = "field '" + out.name + "': bits[] requires schema_version 2";
            return false;
        }
    }
    else
    {
        JsonVariantConst nanV = obj["nan_value"];
        if (!nanV.isNull())
        {
            if (nanV.is<const char *>())
            {
                // String form. Accept signed (`-1`) or unsigned (`0xFFFFFFFF`).
                // For signed types (int16/int32) users typically write -1 to mean
                // "wire returns 0xFFFF/0xFFFFFFFF"; the bit-reinterpret matches that.
                const char *s = nanV.as<const char *>();
                bool ok = (s[0] == '-') ? parseSigned32AsBits(s, out.nanSentinel)
                                        : parseUnsigned32(s, out.nanSentinel);
                if (!ok)
                {
                    errorOut = "field '" + out.name + "': nan_value '" + s + "' is not a valid integer";
                    return false;
                }
            }
            else if (nanV.is<unsigned int>() || nanV.is<unsigned long>())
            {
                // Unsigned numeric (covers 0..0xFFFFFFFF under USE_LONG_LONG=0).
                out.nanSentinel = nanV.as<uint32_t>();
            }
            else if (nanV.is<int>() || nanV.is<long>())
            {
                // Signed numeric (covers -2^31..2^31-1). Reinterpret bits so that
                // `nan_value: -1` matches wire 0xFFFF (int16) or 0xFFFFFFFF (int32).
                out.nanSentinel = (uint32_t)nanV.as<int32_t>();
            }
            else
            {
                errorOut = "field '" + out.name + "': nan_value must be a hex/decimal string or integer";
                return false;
            }
            out.hasNanSentinel = true;
        }

        JsonArrayConst bitsArr = obj["bits"].as<JsonArrayConst>();
        if (!bitsArr.isNull())
        {
            if (out.type != "uint16")
            {
                errorOut = "field '" + out.name + "': bits[] only supported on uint16";
                return false;
            }
            if (!parseBits(bitsArr, out.bits, errorOut))
            {
                errorOut = "field '" + out.name + "': " + errorOut;
                return false;
            }
        }
    }

    return true;
}

bool GenericModbusDevice::parseBits(JsonArrayConst arr, std::vector<BitField> &out, String &errorOut)
{
    out.reserve(arr.size());
    for (JsonVariantConst v : arr)
    {
        JsonObjectConst bitObj = v.as<JsonObjectConst>();
        BitField b;
        b.name = bitObj["name"] | "";
        if (b.name.length() == 0)
        {
            errorOut = "bit entry with empty name";
            return false;
        }

        JsonVariantConst maskV = bitObj["mask"];
        uint32_t mask32 = 0;
        if (maskV.is<const char *>())
        {
            if (!parseUnsigned32(maskV.as<const char *>(), mask32))
            {
                errorOut = "bit '" + b.name + "': mask '" + maskV.as<const char *>() +
                           "' is not a valid integer";
                return false;
            }
        }
        else if (maskV.is<unsigned int>() || maskV.is<unsigned long>() ||
                 maskV.is<int>() || maskV.is<long>())
        {
            mask32 = maskV.as<uint32_t>();
        }
        else
        {
            errorOut = "bit '" + b.name + "': mask must be a hex/decimal string or integer";
            return false;
        }

        // Reject mask=0xFFFF: that's the entire word and defeats the point of bits[].
        // If the raw word is what you want, drop bits[] and publish the field directly.
        if (mask32 == 0 || mask32 >= 0xFFFF)
        {
            errorOut = "bit '" + b.name + "': mask must be 0x0001..0xFFFE";
            return false;
        }
        b.mask = (uint16_t)mask32;

        JsonVariantConst shiftV = bitObj["shift"];
        if (!shiftV.isNull())
        {
            int sh = shiftV.as<int>();
            if (sh < 0 || sh > 15)
            {
                errorOut = "bit '" + b.name + "': shift must be 0..15";
                return false;
            }
            b.shift = (uint8_t)sh;
        }
        else
        {
            uint8_t sh = 0;
            uint16_t m = b.mask;
            while ((m & 1) == 0 && sh < 16)
            {
                m >>= 1;
                sh++;
            }
            b.shift = sh;
        }

        out.push_back(std::move(b));
    }
    return true;
}

bool GenericModbusDevice::parseRegisterDef(JsonObjectConst obj, int schemaVer, RegisterDef &out, String &errorOut)
{
    JsonVariantConst addrV = obj["addr"];
    if (!addrV.is<const char *>())
    {
        const char *n = obj["name"] | "?";
        errorOut = String("register '") + n + "': addr must be a string (e.g. \"0x0010\" or \"100\")";
        return false;
    }
    uint32_t addr32 = 0;
    if (!parseUnsigned32(addrV.as<const char *>(), addr32) || addr32 > 0xFFFF)
    {
        const char *n = obj["name"] | "?";
        errorOut = String("register '") + n + "': addr '" + addrV.as<const char *>() +
                   "' is not a valid 16-bit register address";
        return false;
    }
    out.addr = (uint16_t)addr32;

    out.fc = obj["fc"] | 4;
    if (out.fc != 3 && out.fc != 4)
    {
        const char *n = obj["name"] | "?";
        errorOut = String("register '") + n + "': fc must be 3 or 4";
        return false;
    }

    out.isStorage = obj["storage"] | false;

    // `offset` is meaningful only inside a group. On a flat register it would be
    // silently discarded; reject so users notice the schema mismatch.
    if (!obj["offset"].isNull())
    {
        const char *n = obj["name"] | "?";
        errorOut = String("register '") + n + "': offset is only valid inside groups[].fields";
        return false;
    }

    // Reuse parseField for type/scale/unit/nan/bits.
    if (!parseField(obj, schemaVer, out.field, errorOut))
    {
        return false;
    }
    out.field.offset = 0;

    return true;
}

bool GenericModbusDevice::parseGroup(JsonObjectConst obj, int schemaVer, RegisterGroup &out, String &errorOut)
{
    out.name = obj["name"] | "";
    if (out.name.length() == 0)
    {
        errorOut = "group with empty name";
        return false;
    }

    JsonVariantConst startV = obj["start"];
    if (!startV.is<const char *>())
    {
        errorOut = "group '" + out.name + "': start must be a string (e.g. \"0x0000\" or \"0\")";
        return false;
    }
    uint32_t start32 = 0;
    if (!parseUnsigned32(startV.as<const char *>(), start32) || start32 > 0xFFFF)
    {
        errorOut = "group '" + out.name + "': start '" + startV.as<const char *>() +
                   "' is not a valid 16-bit register address";
        return false;
    }
    out.startAddr = (uint16_t)start32;

    int countVal = obj["count"] | 0;
    if (countVal <= 0 || countVal > MAX_GROUP_COUNT)
    {
        errorOut = "group '" + out.name + "': count must be 1.." + String(MAX_GROUP_COUNT);
        return false;
    }
    out.count = (uint8_t)countVal;

    out.fc = obj["fc"] | 4;
    if (out.fc != 3 && out.fc != 4)
    {
        errorOut = "group '" + out.name + "': fc must be 3 or 4";
        return false;
    }

    out.isStorage = obj["storage"] | false;

    JsonArrayConst fieldsArr = obj["fields"].as<JsonArrayConst>();
    if (fieldsArr.isNull() || fieldsArr.size() == 0)
    {
        errorOut = "group '" + out.name + "': fields[] required and non-empty";
        return false;
    }

    out.fields.reserve(fieldsArr.size());

    // Track word coverage to detect overlaps and unmapped offsets.
    bool covered[MAX_GROUP_COUNT] = {false};
    int unmappedHint = 0;

    for (JsonVariantConst v : fieldsArr)
    {
        Field f;
        if (!parseField(v.as<JsonObjectConst>(), schemaVer, f, errorOut))
        {
            errorOut = "group '" + out.name + "': " + errorOut;
            return false;
        }

        uint8_t ws = wordSize(f.type);
        if ((int)f.offset + (int)ws > (int)out.count)
        {
            errorOut = "group '" + out.name + "' field '" + f.name + "': offset " + String(f.offset) +
                       " + size " + String(ws) + " exceeds count " + String(out.count);
            return false;
        }

        for (uint8_t k = 0; k < ws; k++)
        {
            if (covered[f.offset + k])
            {
                errorOut = "group '" + out.name + "' field '" + f.name + "': overlaps another field at offset " + String(f.offset + k);
                return false;
            }
            covered[f.offset + k] = true;
        }

        out.fields.push_back(std::move(f));
    }

    for (uint8_t k = 0; k < out.count; k++)
    {
        if (!covered[k]) unmappedHint++;
    }
    if (unmappedHint > 0)
    {
        ESPLogger::info("Group '%s' has %d unmapped word(s) of %u (gaps allowed)",
                        out.name.c_str(), unmappedHint, out.count);
    }

    return true;
}

bool GenericModbusDevice::checkDuplicateNames(const std::vector<RegisterDef> &regs,
                                              const std::vector<RegisterGroup> &grps,
                                              String &errorOut)
{
    // Walk each published name once and verify no prior occurrence collides.
    // Bitfields publish per-bit names instead of the field name; flat regs
    // and group fields without bits publish their own name.
    std::vector<String> seen;
    seen.reserve(regs.size() + grps.size() * 4);

    auto add = [&](const String &n) -> bool {
        for (const auto &s : seen)
        {
            if (s == n)
            {
                errorOut = "duplicate telemetry name: '" + n + "'";
                return false;
            }
        }
        seen.push_back(n);
        return true;
    };

    auto addField = [&](const Field &f) -> bool {
        if (f.bits.empty())
        {
            return add(f.name);
        }
        for (const auto &b : f.bits)
        {
            if (!add(b.name)) return false;
        }
        return true;
    };

    for (const auto &r : regs)
    {
        if (!addField(r.field)) return false;
    }
    for (const auto &g : grps)
    {
        for (const auto &f : g.fields)
        {
            if (!addField(f)) return false;
        }
    }
    return true;
}

uint8_t GenericModbusDevice::wordSize(const String &type)
{
    if (type == "uint32" || type == "int32") return 2;
    return 1;
}

bool GenericModbusDevice::decodeValue(const uint16_t *words, const Field &f, float &valueOut, uint32_t &rawOut)
{
    if (f.type == "uint16")
    {
        rawOut = words[0];
        valueOut = (float)words[0] * f.scale;
    }
    else if (f.type == "int16")
    {
        int16_t s = (int16_t)words[0];
        rawOut = (uint32_t)(uint16_t)words[0];
        valueOut = (float)s * f.scale;
    }
    else if (f.type == "uint32")
    {
        uint32_t v = ((uint32_t)words[0] << 16) | words[1];
        rawOut = v;
        valueOut = (float)v * f.scale;
    }
    else if (f.type == "int32")
    {
        int32_t s = (int32_t)(((uint32_t)words[0] << 16) | words[1]);
        rawOut = (uint32_t)s;
        valueOut = (float)s * f.scale;
    }
    else
    {
        return false;
    }
    return true;
}

void GenericModbusDevice::emitField(const Field &f, const uint16_t *words, std::vector<TelemetryPoint> &out)
{
    float value;
    uint32_t raw;
    if (!decodeValue(words, f, value, raw))
    {
        return;
    }

    // NaN sentinel: if the raw value matches, suppress publish entirely.
    if (f.hasNanSentinel)
    {
        uint32_t mask = (wordSize(f.type) == 1) ? 0xFFFFu : 0xFFFFFFFFu;
        if ((raw & mask) == (f.nanSentinel & mask))
        {
            return;
        }
    }

    if (f.bits.empty())
    {
        out.push_back({f.name, f.unit, String(value, 2)});
        return;
    }

    // Bitfield: emit one boolean-or-extracted-int per named bit, suppress numeric.
    uint16_t word = words[0];
    for (const auto &b : f.bits)
    {
        uint16_t extracted = (word & b.mask) >> b.shift;
        // If mask is single-bit, extracted is 0 or 1. If multi-bit, extracted is the field value.
        out.push_back({b.name, "", String((unsigned)extracted)});
    }
}

bool GenericModbusDevice::readFlatRegister(const RegisterDef &reg, std::vector<TelemetryPoint> &out)
{
    uint16_t data[2] = {0, 0};
    uint8_t count = wordSize(reg.field.type);

    bool ok = false;
    if (reg.fc == 3)
    {
        ok = modbus.readHoldingRegisters(slaveAddr, reg.addr, count, data);
    }
    else if (reg.fc == 4)
    {
        ok = modbus.readInputRegisters(slaveAddr, reg.addr, count, data);
    }

    if (!ok)
    {
        ESPLogger::debug("Failed to read register %s (0x%04X)", reg.field.name.c_str(), reg.addr);
        return false;
    }

    emitField(reg.field, data, out);
    return true;
}

bool GenericModbusDevice::readGroup(const RegisterGroup &grp, std::vector<TelemetryPoint> &out)
{
    // Stack-allocated; max 250 bytes. ModbusClient runtime guard caps count at 125.
    uint16_t buf[MAX_GROUP_COUNT];

    bool ok = false;
    if (grp.fc == 3)
    {
        ok = modbus.readHoldingRegisters(slaveAddr, grp.startAddr, grp.count, buf);
    }
    else if (grp.fc == 4)
    {
        ok = modbus.readInputRegisters(slaveAddr, grp.startAddr, grp.count, buf);
    }

    if (!ok)
    {
        ESPLogger::warn("Group '%s' read failed (start=0x%04X count=%u fc=%u) - %u field(s) skipped this cycle",
                        grp.name.c_str(), grp.startAddr, grp.count, grp.fc, (unsigned)grp.fields.size());
        return false;
    }

    for (const auto &f : grp.fields)
    {
        emitField(f, &buf[f.offset], out);
    }
    return true;
}

uint32_t GenericModbusDevice::pollBudgetMs() const
{
    int pollSec = config.getInt("poll_interval_sec", 10);
    if (pollSec < 1) pollSec = 1;
    uint32_t budget = (uint32_t)pollSec * 500u; // half the poll interval
    if (budget > 5000u) budget = 5000u;
    if (budget < 500u) budget = 500u;
    return budget;
}

// Internal: shared loop body for readBasic / readStorage. The per-poll Modbus time
// budget covers BOTH flat-register and group reads, since either can stack
// retry-backoff delays that blow past the poll interval.
bool GenericModbusDevice::readPhase(bool storagePhase, std::vector<TelemetryPoint> &out)
{
    int flatOk = 0, flatFail = 0, flatBudgetSkip = 0;
    int groupOk = 0, groupFail = 0, groupBudgetSkip = 0;

    const uint32_t startMs = millis();
    const uint32_t budget = pollBudgetMs();
    bool budgetTripped = false;

    auto budgetExpired = [&]() -> bool {
        if (budgetTripped) return true;
        if (millis() - startMs > budget)
        {
            ESPLogger::warn("Per-poll Modbus budget %ums exceeded during %s read; skipping remaining reads",
                            (unsigned)budget, storagePhase ? "storage" : "basic");
            budgetTripped = true;
            return true;
        }
        return false;
    };

    for (const auto &reg : registers)
    {
        if (reg.isStorage != storagePhase) continue;
        if (budgetExpired()) { flatBudgetSkip++; continue; }
        if (readFlatRegister(reg, out)) flatOk++;
        else flatFail++;
    }

    for (const auto &grp : groups)
    {
        if (grp.isStorage != storagePhase) continue;
        if (budgetExpired()) { groupBudgetSkip++; continue; }
        if (readGroup(grp, out)) groupOk++;
        else groupFail++;
    }

    if (flatFail || groupFail || flatBudgetSkip || groupBudgetSkip)
    {
        ESPLogger::info("%s read: flat_ok=%d flat_fail=%d flat_budget=%d / group_ok=%d group_fail=%d group_budget=%d",
                        storagePhase ? "Storage" : "Basic",
                        flatOk, flatFail, flatBudgetSkip,
                        groupOk, groupFail, groupBudgetSkip);
    }

    // Per-group / per-flat isolation: anything successfully read is published.
    return (flatOk + groupOk) > 0;
}

bool GenericModbusDevice::readBasic(std::vector<TelemetryPoint> &out)
{
    out.clear();
    if (!initialized)
    {
        ESPLogger::error("Device not initialized");
        return false;
    }
    return readPhase(false, out);
}

bool GenericModbusDevice::readStorage(std::vector<TelemetryPoint> &out)
{
    out.clear();
    if (!initialized) return false;
    return readPhase(true, out);
}
