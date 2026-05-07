# Testing Guide

PlatformIO-based test suite using the Unity framework. Most tests are designed to run on the host (`native` env) via the mocks in `test/native_mocks/`. A subset requires the ESP32 hardware (`dev` env).

## Test directories

```
test/
├── native_mocks/          Arduino + LittleFS shims so tests build on the native env
├── test_basic/            Framework smoke tests
├── test_config/           Config — LittleFS-backed settings
├── test_esplogger/        Logging library
├── test_modbus_client/    Modbus RTU framing, CRC, retries
├── test_inverter_factory/ InverterFactory model dispatch
├── test_solplanet_asw/    SolPlanet ASW register parsing
├── test_hiking_dds238/    Hiking DDS238 meter
├── test_poller/           Poll loop + availability publishing
├── test_mqtt_client/      MQTT publish/subscribe — SKIPPED on native (PubSubClient not available)
└── test_wifi_manager/     Wi-Fi STA/AP fallback — SKIPPED on native (real radio required)
```

`test_mqtt_client` and `test_wifi_manager` are excluded from `pio test -e native` because their dependencies (`PubSubClient`, `WebSockets`, real Wi-Fi) are not stubbable. They run only against ESP32 hardware via `pio test -e dev`.

## Common commands

```bash
# Host-side: fastest path, no hardware
pio test -e native

# Host-side, single suite
pio test -e native -f test_modbus_client

# ESP32 hardware
pio test -e dev

# ESP32 hardware, single suite
pio test -e dev -f test_basic

# CI-style runner (used by GitHub Actions)
python scripts/run_tests.py

# Read-only invariant check (CI uses this)
./scripts/check_readonly.sh
```

## What CI runs

`.github/workflows/test.yml`:

1. **Compile-only verification** of every `test_*` suite under `-e dev`.
2. Full `dev` and `prod` firmware builds with size analysis.
3. `pio check --skip-packages` static analysis.
4. `scripts/check_readonly.sh` — fails the build if any Modbus write opcode (`0x05`, `0x06`, `0x0F`, `0x10`) or write method name appears in `src/` or `lib/`. See `docs/SAFETY.md` and the Safety section in the top-level README for the rationale.

## Writing a new test

1. Create `test/test_<component>/test_<component>.cpp`.
2. Use Unity's `RUN_TEST` inside `setup()`. Put `loop()` empty.
3. If the code under test pulls in any Arduino-only header, gate it with `#ifndef NATIVE_BUILD` or stub the header in `test/native_mocks/`.
4. Add the suite to the compile-only matrix in `.github/workflows/test.yml`.
5. Avoid `delay()`, blocking I/O, real network calls — they make CI flake.

Test template:

```cpp
#include <unity.h>
#include "YourLibrary.h"

void setUp(void) {}
void tearDown(void) {}

void test_does_thing(void) {
    YourClass c;
    TEST_ASSERT_EQUAL_INT(42, c.compute());
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_does_thing);
    UNITY_END();
}

void loop() {}
```

## Debugging failures

```bash
# Verbose Unity output
pio test -e native -vvv -f test_<name>

# Serial monitor for hardware tests
pio device monitor --baud 115200
```

If a host-side test pulls an ESP-only symbol, the link error in `pio test -e native` will tell you which symbol — add it to the appropriate file under `test/native_mocks/`.

## Resources

- [Unity assertions reference](http://www.throwtheswitch.org/unity)
- [PlatformIO unit testing](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html)
