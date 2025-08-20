# CI/CD Testing Guide

This document explains how to run tests in Continuous Integration environments without physical ESP32 hardware.

## Overview

The testing system supports multiple execution modes:

1. **Native Tests** - Run on Linux/macOS/Windows without hardware
2. **Hardware Compilation** - Compile for ESP32 to verify code compatibility  
3. **Code Quality Checks** - Static analysis and formatting validation

## GitHub Actions Integration

### Automatic Testing

Tests run automatically on:
- Push to `main`  branches
- Pull requests to `main`  branches

### Test Matrix

The CI runs tests across multiple configurations:
- **Native tests** for all test suites
- **Hardware compilation** for dev and prod environments  
- **Code quality checks** with static analysis

### Workflow File

The workflow is defined in `.github/workflows/test.yml` and includes:

```yaml
jobs:
  native-tests:     # Run tests without hardware
  hardware-compile: # Compile for ESP32 
  code-quality:     # Static analysis
  test-summary:     # Aggregate results
```

## Local Testing

### Prerequisites

```bash
# Install PlatformIO
pip install platformio

# Update platforms and libraries
pio pkg update
```

### Running Tests Locally

#### Quick Test (Native Only)
```bash
# Run all native tests
pio test -e native

# Run specific test
pio test -e native -f test_config
```

#### Comprehensive Test Suite
```bash
# Using Python test runner
python scripts/run_tests.py

# With specific options
python scripts/run_tests.py --skip-compile --test-suites test_config test_basic
```

#### Manual Commands
```bash
# Native tests (no hardware required)
pio test -e native -f test_basic
pio test -e native -f test_config  
pio test -e native -f test_esplogger
pio test -e native -f test_modbus_client
pio test -e native -f test_inverter_factory
pio test -e native -f test_solplanet_asw
pio test -e native -f test_hiking_dds238
pio test -e native -f test_poller

# Hardware compilation tests
pio run -e dev
pio run -e prod

# Code quality
pio check --skip-packages
```

## Native Testing Architecture

### Mock System

Native tests use a comprehensive mock system:

#### Arduino Framework Mocks
- `test/native_mocks/Arduino.h` - Core Arduino functions
- `test/native_mocks/Arduino.cpp` - Implementation
- Mock implementations for:
  - `String` class with full compatibility
  - `Serial` and `Serial2` communication
  - `ESP` system functions
  - GPIO operations (`pinMode`, `digitalWrite`, `digitalRead`)
  - Timing functions (`millis`, `delay`, etc.)

#### File System Mocks  
- `test/native_mocks/LittleFS.h` - File system interface
- `test/native_mocks/LittleFS.cpp` - In-memory file system
- Full compatibility with ESP32 LittleFS API
- Persistent storage simulation for tests

#### Hardware Abstraction
- Network operations return predictable mock data
- GPIO operations maintain internal state
- Memory allocation uses standard C++ allocators
- Time functions use system clock

### Test Isolation

Each test suite runs in isolation:
- Clean mock state for each test
- No shared global state between tests
- Deterministic behavior regardless of execution order

## Supported Test Suites

### ✅ Native Compatible
These tests run perfectly on native platform:

- **test_basic** - Framework validation
- **test_config** - Configuration management with LittleFS mocks
- **test_esplogger** - Logging system with callback testing
- **test_modbus_client** - Modbus logic without actual RS485
- **test_inverter_factory** - Device factory pattern
- **test_solplanet_asw** - Inverter logic without hardware
- **test_hiking_dds238** - Meter logic without hardware  
- **test_poller** - Polling system with mocked dependencies

### ⚠️ Hardware Dependent  
These tests require actual hardware or are skipped:

- **test_wifi_manager** - Requires WiFi hardware
- **test_mqtt_client** - Requires network connectivity

## Troubleshooting CI Issues

### Common Problems

1. **PlatformIO Installation Issues**
   ```bash
   # Update pip and reinstall
   pip install --upgrade pip
   pip install --force-reinstall platformio
   ```

2. **Library Dependencies**
   ```bash
   # Clear cache and reinstall
   pio pkg update --global
   ```

3. **Native Compilation Errors**
   - Check mock implementations in `test/native_mocks/`
   - Verify include paths in `platformio.ini`
   - Review build flags for native environment

4. **Test Timeouts**
   - Tests have 5-minute timeout in CI
   - Check for infinite loops in test code
   - Verify mock implementations don't block

### Debug Native Tests Locally

```bash
# Run with verbose output
pio test -e native -f test_config -v

# Check build output
pio run -e native -v

# Manual compilation check
pio run -e native -t clean
pio run -e native
```

## Performance Considerations

### CI Optimization

- **Caching**: PlatformIO packages and pip cache
- **Parallel Execution**: Test suites run in parallel matrix
- **Selective Testing**: Only changed components trigger full suite
- **Timeout Management**: Prevent hanging tests

### Local Optimization

- Use `--skip-compile` for faster iteration
- Run specific test suites during development
- Cache PlatformIO dependencies locally

## Adding New Tests

### For Native Compatibility

1. Use existing mocks from `test/native_mocks/`
2. Avoid hardware-specific operations
3. Test logic, not hardware interaction
4. Use dependency injection for hardware components

### Example Test Structure

```cpp
#ifdef NATIVE_BUILD
#include "test/native_mocks/Arduino.h"
#include "test/native_mocks/LittleFS.h"
#else
#include <Arduino.h>
#include <LittleFS.h>
#endif

#include <unity.h>
#include "YourLibrary.h"

void test_your_function() {
    // Test logic without hardware dependencies
    TEST_ASSERT_EQUAL(expected, actual);
}
```

## Integration with Development Workflow

### Pre-commit Testing
```bash
# Quick validation before commit
python scripts/run_tests.py --skip-compile

# Full validation
python scripts/run_tests.py
```

### Pull Request Validation
- All tests must pass before merge
- Compilation must succeed for all environments
- Code quality checks must pass

### Release Testing
- Full test suite execution
- Hardware compilation verification
- Performance regression checks

## Monitoring and Metrics

### Test Results
- GitHub Actions provides detailed test reports
- Failed tests show specific assertion failures
- Compilation errors include full build logs

### Coverage Tracking
- Logic coverage through comprehensive test cases
- Hardware abstraction coverage through mocks
- Integration coverage through end-to-end scenarios

## Future Enhancements

### Planned Improvements
- Hardware-in-the-loop testing with real ESP32
- Performance benchmarking in CI
- Test coverage reporting
- Integration with code quality metrics

### Extensibility
- Easy addition of new mock implementations
- Pluggable test environments
- Custom test runners for specific scenarios
