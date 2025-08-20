#pragma once

#include <Arduino.h>
#include <vector>

struct TelemetryPoint
{
    String name;
    String unit;
    String value; // stringified for flexible publishing
};

class InverterInterface
{
public:
    virtual ~InverterInterface() = default;
    virtual bool begin() = 0;
    virtual bool readBasic(std::vector<TelemetryPoint> &out) = 0;
    virtual bool readStorage(std::vector<TelemetryPoint> &out) = 0; // must be optional; only called if enabled
};
