#pragma once

#include <Arduino.h>
#include <vector>

struct TelemetryPoint
{
    String name;
    String unit;
    String value; // stringified for flexible publishing

    // Internal metadata (not published to MQTT by default)
    uint16_t addr;      // Register address (decimal, e.g., 31319)
    String type;        // Data type: U16, S16, U32, S32, E16, B16
    float scale;        // Scaling factor/gain (0.1, 0.01, 1.0, etc.)

    // Default constructor
    TelemetryPoint() : name(""), unit(""), value(""), addr(0), type(""), scale(1.0f) {}

    // Constructor for backward compatibility (auto-populates metadata as unknown)
    TelemetryPoint(const String& n, const String& u, const String& v)
        : name(n), unit(u), value(v), addr(0), type(""), scale(1.0f) {}

    // Full constructor with metadata
    TelemetryPoint(const String& n, const String& u, const String& v,
                   uint16_t a, const String& t, float s)
        : name(n), unit(u), value(v), addr(a), type(t), scale(s) {}
};

class InverterInterface
{
public:
    virtual ~InverterInterface() = default;
    virtual bool begin() = 0;
    virtual bool readBasic(std::vector<TelemetryPoint> &out) = 0;
    virtual bool readStorage(std::vector<TelemetryPoint> &out) = 0; // must be optional; only called if enabled
};
