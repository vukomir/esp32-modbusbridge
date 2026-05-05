#pragma once

// HardwareSerial mock for native testing
#ifdef NATIVE_BUILD

#include "Arduino.h"

// HardwareSerial is just an alias to Stream/MockSerial for native builds
typedef MockSerial HardwareSerial;

#endif // NATIVE_BUILD
