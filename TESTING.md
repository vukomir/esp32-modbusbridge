# Testing Guide for Inverter Monitoring System

## Overview

This project includes a comprehensive test suite built with PlatformIO's Unity testing framework. The tests are designed to validate all major components of the inverter monitoring system.

## Test Structure

### Test Categories

1. **Basic Tests** (`test_basic/`)
   - Framework validation
   - Basic C++ functionality
   - Memory operations

2. **Library Tests**
   - `test_config/` - Configuration management
   - `test_esplogger/` - Logging system  
   - `test_modbus_client/` - Modbus communication
   - `test_inverter_factory/` - Device factory
   - `test_solplanet_asw/` - SolPlanet inverter
   - `test_hiking_dds238/` - Hiking meter
   - `test_poller/` - Data polling system

## Running Tests

### Prerequisites

- PlatformIO CLI installed
- ESP32 development board (for hardware tests)
- USB cable for device connection

### Basic Test Execution

```bash
# Run all tests
pio test

# Run specific test
pio test -f test_basic

# Run with verbose output
pio test -v

# Run on specific environment
pio test -e dev
```

### Test Environments

1. **dev** - Development environment with ESP32 hardware
2. **native** - Native environment for basic tests (no hardware required)

### Using the Test Runner Script

A convenient test runner script is provided:

```bash
# Make executable (first time only)
chmod +x scripts/run_tests.sh

# Run all tests with colored output
./scripts/run_tests.sh
```

## Test Development

### Writing New Tests

1. Create a new directory in `test/` following the naming convention `test_<component>/`
2. Create a test file named `test_<component>.cpp`
3. Include required headers and implement test functions
4. Add test to the runner script

### Test Template

```cpp
#include <unity.h>
#include <Arduino.h>
#include "YourLibrary.h"

// Test fixtures
YourClass *testInstance;

void setUp(void) {
    testInstance = new YourClass();
}

void tearDown(void) {
    delete testInstance;
    testInstance = nullptr;
}

void test_your_functionality() {
    // Arrange
    int expected = 42;
    
    // Act
    int result = testInstance->yourMethod();
    
    // Assert
    TEST_ASSERT_EQUAL_INT(expected, result);
}

void setup() {
    delay(2000); // Wait for serial monitor
    
    UNITY_BEGIN();
    RUN_TEST(test_your_functionality);
    UNITY_END();
}

void loop() {
    // Empty - tests run once in setup()
}
```

### Test Best Practices

1. **Use descriptive test names** that explain what is being tested
2. **Follow AAA pattern** - Arrange, Act, Assert
3. **Test one thing at a time** - Keep tests focused
4. **Use proper setup/teardown** - Clean state for each test
5. **Handle hardware dependencies** - Gracefully handle missing hardware
6. **Test error conditions** - Don't just test happy paths

### Available Assertions

Unity provides many assertion macros:

```cpp
// Basic assertions
TEST_ASSERT_TRUE(condition)
TEST_ASSERT_FALSE(condition)
TEST_ASSERT_NULL(pointer)
TEST_ASSERT_NOT_NULL(pointer)

// Numeric assertions
TEST_ASSERT_EQUAL_INT(expected, actual)
TEST_ASSERT_EQUAL_FLOAT(expected, actual, delta)
TEST_ASSERT_GREATER_THAN(threshold, actual)

// String assertions  
TEST_ASSERT_EQUAL_STRING(expected, actual)
TEST_ASSERT_EQUAL_MEMORY(expected, actual, length)

// Array assertions
TEST_ASSERT_EQUAL_INT_ARRAY(expected, actual, elements)
```

## Hardware Testing

### ESP32 Hardware Tests

Tests that require ESP32 hardware include:
- GPIO operations
- Serial communication
- WiFi functionality
- File system operations

### Mock Objects

For components that depend on hardware, consider using mocks:
- Mock WiFi for network tests
- Mock file system for configuration tests
- Mock serial for communication tests

## Continuous Integration

### Local CI Simulation

```bash
# Simulate CI environment
export CI=true
pio test --environment dev
```

### GitHub Actions Integration

The project includes CI configuration that runs tests automatically on:
- Pull requests
- Main branch commits  
- Release tags

## Troubleshooting

### Common Issues

1. **Upload Errors**
   - Ensure ESP32 is connected and recognized
   - Try different USB cable/port
   - Press reset button during upload

2. **Compilation Errors**
   - Check library dependencies in `platformio.ini`
   - Verify include paths
   - Update PlatformIO if needed

3. **Test Failures**
   - Check hardware connections
   - Verify configuration files
   - Review test logs for specific errors

### Debug Mode

Enable verbose output for debugging:

```bash
pio test -vvv -f test_name
```

### Serial Monitor

Monitor test output in real-time:

```bash
pio device monitor --baud 115200
```

## Test Coverage

Current test coverage includes:

- ✅ Configuration management
- ✅ Logging system
- ✅ Modbus communication
- ✅ Device factory pattern
- ✅ Inverter implementations
- ✅ Data polling logic
- ⏳ WiFi management (partial)
- ⏳ MQTT communication (partial)
- ⏳ Web UI (partial)

## Contributing

When adding new features:

1. Write tests first (TDD approach)
2. Ensure tests pass before submitting PR
3. Update documentation if needed
4. Add tests to CI pipeline

## Performance Testing

For performance-critical components:

```cpp
void test_performance() {
    unsigned long startTime = millis();
    
    // Your code here
    yourFunction();
    
    unsigned long duration = millis() - startTime;
    TEST_ASSERT_LESS_THAN(100, duration); // Must complete in <100ms
}
```

## Memory Testing

Monitor memory usage in tests:

```cpp
void test_memory_usage() {
    size_t initialHeap = ESP.getFreeHeap();
    
    // Your code here
    
    size_t finalHeap = ESP.getFreeHeap();
    size_t memoryUsed = initialHeap - finalHeap;
    
    TEST_ASSERT_LESS_THAN(1000, memoryUsed); // Should use <1KB
}
```

## Resources

- [Unity Testing Framework](https://github.com/ThrowTheSwitch/Unity)
- [PlatformIO Testing Guide](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html)
- [ESP32 Testing Best Practices](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/unit-tests.html)
