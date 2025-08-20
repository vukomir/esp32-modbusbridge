# Unit Tests for Inverter Monitoring System

This directory contains comprehensive unit tests for all libraries in the inverter monitoring system.

## Test Framework

- **Framework**: Unity (PlatformIO's built-in test framework)
- **Target**: ESP32 (Wemos D1 Mini 32)
- **Build System**: PlatformIO

## Test Structure

Each library has its own test directory with comprehensive test coverage:

### Core Libraries
- `test_config/` - Configuration management tests
- `test_esplogger/` - Logging system tests

### Communication Libraries  
- `test_modbus_client/` - Modbus RTU client tests
- `test_mqtt_client/` - MQTT client tests
- `test_wifi_manager/` - WiFi management tests

### Device Libraries
- `test_inverter_factory/` - Device factory pattern tests
- `test_solplanet_asw/` - SolPlanet inverter tests
- `test_hiking_dds238/` - Hiking DDS238 meter tests

### System Libraries
- `test_poller/` - Data polling system tests
- `test_webui/` - Web interface tests

## Running Tests

### Run All Tests
```bash
pio test
```

### Run Specific Test
```bash
pio test -f test_config
pio test -f test_modbus_client
```

### Run Tests with Verbose Output
```bash
pio test -v
```

### Run Tests on Specific Environment
```bash
pio test -e dev
```

## Test Categories

### Unit Tests
- Test individual functions and methods
- Mock external dependencies where needed
- Focus on logic validation and edge cases

### Integration Tests  
- Test interaction between components
- Validate configuration handling
- Test error propagation

### Hardware Tests
- Test hardware detection (MAX485, etc.)
- Validate pin configurations
- Test serial communication setup

## Test Coverage

The tests cover:

✅ **Configuration Management**
- Key/value storage and retrieval
- Type conversion and validation
- Persistence and factory reset
- JSON serialization

✅ **Logging System**
- Log level filtering
- Message formatting
- Callback functionality
- Memory management

✅ **Modbus Communication**
- Frame building and parsing
- CRC calculation and validation
- Register operations
- Error handling

✅ **Device Management**
- Factory pattern implementation
- Device initialization
- Telemetry data structures
- Phase detection logic

✅ **System Integration**
- Component initialization
- Memory usage tracking
- Error propagation
- State management

## Test Environment Setup

The tests are designed to run on ESP32 hardware but include mocks and stubs for:
- File system operations (LittleFS)
- Network operations (WiFi, MQTT)
- Hardware interfaces (Serial, GPIO)
- External devices (inverters, meters)

## Continuous Integration

Tests are integrated with the CI/CD pipeline and run automatically on:
- Pull requests
- Main branch commits
- Release builds

## Writing New Tests

When adding new functionality:

1. Create test file in appropriate `test_<library>/` directory
2. Follow naming convention: `test_<library>.cpp`
3. Include comprehensive test cases:
   - Happy path scenarios
   - Error conditions
   - Edge cases
   - Boundary conditions

4. Update this README with new test information

## Test Results

Test results include:
- Pass/fail status for each test
- Memory usage information
- Performance metrics
- Coverage reports

## Debugging Tests

For debugging failed tests:
- Use `TEST_ASSERT_*` macros for detailed failure information
- Add debug output with `TEST_MESSAGE()`
- Run individual tests for focused debugging
- Check serial monitor output for detailed logs
