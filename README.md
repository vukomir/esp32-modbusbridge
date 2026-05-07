# 🔌 ESP32 Inverter Monitoring System

A comprehensive ESP32-based monitoring system for solar inverters and smart meters using Modbus RTU communication. Features real-time data collection, MQTT integration, web-based configuration, and a live console for monitoring.

![ESP32](https://img.shields.io/badge/ESP32-Platform-blue)
![PlatformIO](https://img.shields.io/badge/PlatformIO-Build%20System-green)
![MQTT](https://img.shields.io/badge/MQTT-Protocol-orange)
![Modbus](https://img.shields.io/badge/Modbus-RTU-red)

## 🌟 Features

### 🔧 **Core Functionality**
- **Modbus RTU Communication**: RS485 interface for inverter/meter communication. **Read-only by design** — see [Safety Guarantees](#-safety-guarantees) below.
- **MQTT Integration**: Real-time data publishing to MQTT broker.
- **Web Configuration Interface**: Easy setup and configuration via web browser. CSRF-protected and HTML-escaped.
- **Real-time Console**: Live logging and monitoring via WebSocket.
- **NTP Time Synchronization**: Accurate timestamps for all data.
- **Dual-Core Architecture**: Optimized task distribution on ESP32 (FreeRTOS).

### 📊 **Supported Devices**
- **SolPlanet ASW GEN Inverter**: Solar inverter monitoring.
- **Hiking DDS238 Smart Meter**: Energy consumption monitoring.
- **DDS238 Simulator**: Bench-testing target (no hardware required).
- **Custom JSON Devices**: Add any Modbus device without recompiling — see [Custom Devices Guide](docs/CUSTOM_DEVICES.md).
- **Extensible Architecture**: Easy to add new device types — see [Adding New Devices](#adding-new-devices).

### 🌐 **Network Features**
- **WiFi Management**: Automatic connection with AP fallback.
- **mDNS Support**: Access via hostname (e.g., `modbusbridge.local`).
- **Captive Portal**: Easy initial setup.
- **OTA Updates**: Over-the-air firmware updates.

### 🔍 **Monitoring & Debugging**
- **Live Web Console**: Real-time log streaming with WebSocket.
- **Log Level Control**: Dynamic log verbosity adjustment.
- **System Status**: Memory usage, uptime, connection status.
- **Per-poll retry with exponential backoff**: transient bus errors (CRC mismatch, timeout, partial frame) are retried 3× with 100/200/400 ms backoff before failing the poll.
- **Device-availability MQTT topic**: after several consecutive failed polls the device publishes `offline` to its availability topic so Home Assistant can mark sensors unavailable instead of showing stale retained values.

## 🛠️ Hardware Requirements

### **Required Components**
- **ESP32 Development Board** (WEMOS D1 MINI ESP32 or compatible)
- **MAX485 RS485 Module** (for Modbus RTU communication)
- **USB Cable** (for programming and power)

### **Connections**
```
ESP32 Pin    MAX485 Module
GPIO 16  →   RX (Receive)
GPIO 17  →   TX (Transmit)
GPIO 4   →   DE/RE (Driver Enable/Receiver Enable)
3.3V     →   VCC
GND      →   GND
```

### **Supported Boards**
- WEMOS D1 MINI ESP32 (default)
- Any ESP32 board with similar pinout

## 🚀 Quick Start

### **1. Prerequisites**
- [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- USB cable for ESP32
- MAX485 RS485 module
- Solar inverter or smart meter with Modbus RTU

### **2. Installation**

#### **Using PlatformIO (Recommended)**
```bash
# Clone the repository
git clone https://github.com/vukomir/esp32-modbusbridge.git
cd esp32-modbusbridge

# Install dependencies and build
pio run

# Upload to ESP32
pio run --target upload
```

#### **Using Arduino IDE**
1. Download the source code
2. Install required libraries:
   - ArduinoJson
   - PubSubClient
   - WebSockets
3. Select your ESP32 board
4. Compile and upload

### **3. Initial Setup**

1. **Power on the ESP32**
2. **Connect to WiFi**: The device will create an AP named `modbusbridge-setup`
3. **Access Configuration**: Navigate to `http://192.168.4.1`
4. **Configure Settings**:
   - WiFi credentials
   - MQTT broker details
   - Modbus settings
   - Device model selection

### **4. Access the Device**

Once configured and connected to your WiFi:
- **Web Interface**: `http://modbusbridge.local` or `http://[device-ip]`
- **Configuration**: `/` - Device settings
- **Status**: `/status` - System status and diagnostics
- **Console**: `/console` - Real-time logging and monitoring
- **Updates**: `/update` - Firmware updates

## ⚙️ Configuration

### **WiFi Settings**
- **SSID**: Your WiFi network name
- **Password**: WiFi password
- **Hostname**: Device hostname (default: `modbusbridge`)
- **AP Fallback**: Enable/disable setup mode access point

### **MQTT Configuration**
- **Broker**: MQTT broker address (e.g., `192.168.1.100`)
- **Port**: MQTT port (default: `1883`)
- **Username/Password**: MQTT authentication
- **Topic Prefix**: Custom topic prefix for data
- **Retain Messages**: Enable/disable message retention

### **Modbus Settings**
- **Slave Address**: Device address (default: `1`)
- **Baud Rate**: Communication speed (9600, 19200, 38400, 57600, 115200, default: `9600`)
- **Data Bits**: Number of data bits (7 or 8, default: `8`)
- **Parity**: None, Even, or Odd (default: `N`)
- **Stop Bits**: Number of stop bits (1 or 2, default: `1`)
- **DE/RE Pin**: GPIO pin for RS485 direction control (default: `4`)

### **Device Configuration**
- **Device Model**: Select your inverter/meter model
- **Poll Interval**: Data collection frequency (seconds)
- **Read Storage Registers**: Enable battery/storage monitoring

## 🛡️ Safety Guarantees

This firmware is intended to **monitor** inverters and meters. It must never **change** them. See [`docs/SAFETY.md`](docs/SAFETY.md) for the full statement, the threat model, and how to verify the invariant yourself.

The codebase enforces read-only operation at three layers:

1. **API:** `ModbusClient` exposes only `readHoldingRegisters` (function code `0x03`) and `readInputRegisters` (`0x04`). No write methods are declared.
2. **Frame builder:** `ModbusClient::buildReadFrame` refuses any function code other than `0x03` / `0x04`. A future contributor cannot accidentally pass a write opcode through the existing API.
3. **CI:** `scripts/check_readonly.sh` greps the entire `src/` and `lib/` tree for the Modbus write opcodes (`0x05`, `0x06`, `0x0F`, `0x10`) and write method names. The build fails if any are introduced. See `.github/workflows/test.yml`.

If you need to legitimately use one of those byte values for a non-Modbus reason (e.g. a buffer length constant), add the comment `// MODBUS_WRITE_OPCODE_OK: <reason>` on the same line and CI will allow it.

### Web UI hardening

- **CSRF tokens** are generated per boot and required on every state-changing POST (`/save`, `/reboot`, `/factory`, `/restart_mdns`, `/update`).
- **Destructive endpoints are POST-only.** `/reboot`, `/factory`, and `/restart_mdns` cannot be triggered by `<img src="...">` or other GET-based CSRF tricks.
- **All config-derived strings** (SSID, hostname, MQTT broker, etc.) are HTML-escaped before being inlined into the page. Closes stored XSS via the setup form.
- **Known limitation:** OTA upload validates CSRF *after* the binary has been received. The new image is staged but the auto-restart is blocked. Hardening this further (CSRF check at upload start) is tracked as a follow-up.

## 📡 MQTT Topics

Topics use the structure `{prefix}/{device_type}/{data_type}/{metric}`. The default `{prefix}` is `home` (configurable). `{device_type}` is `inverter` or `meter` depending on the configured model.

```
{prefix}/{device_type}/telemetry/<metric>     # Device readings (voltage, power, energy, ...)
{prefix}/{device_type}/status/availability    # "online" / "offline" of the *monitored device*
                                              # (use this as your HA availability_topic)
{prefix}/{device_type}/status/connection      # ESP32 ↔ MQTT broker connection
{prefix}/{device_type}/status/poll_status     # JSON: success/failed/successful_polls counts
{prefix}/{device_type}/hardware/max485_status # JSON: MAX485/Modbus bus state
{prefix}/{device_type}/hardware/inverter_status # JSON: device responding / not responding
{prefix}/{device_type}/diagnostics/system_info # JSON: heap, uptime, version, build mode
{prefix}/{device_type}/diagnostics/rssi       # WiFi signal strength
{prefix}/{device_type}/diagnostics/uptime     # Seconds since boot
{prefix}/{device_type}/config/config_info     # JSON: active configuration snapshot
```

### **Example Topics**
```
home/inverter/telemetry/active_power
home/inverter/telemetry/energy_today
home/inverter/telemetry/energy_total
home/inverter/telemetry/battery_soc
home/inverter/status/availability
home/inverter/hardware/max485_status
```

Telemetry payloads are JSON `{"value": N, "unit": "X", "timestamp": N, "device_id": "..."}` for numeric metrics and raw scalars for status / state / fault codes.

For the full per-metric list (suggested Home Assistant device classes, units, etc.), subscribe to `{prefix}/{device_type}/telemetry/#` on your broker and inspect the published topic names — they are stable across firmware versions for the same device model.

### **Home Assistant integration**

A complete HA package (MQTT sensors, templates, Riemann integrations, Energy Dashboard wiring) is provided under [`docs/home_assistant/`](docs/home_assistant/README.md). It supports both single-phase and three-phase Solplanet ASW inverters and includes step-by-step setup for the Energy Dashboard (Solar production, Grid import/export, Battery in/out).

## 🔧 Development

### **Project Structure**
```
invertor-monitoring/
├── src/                    # Main application code
│   └── main.cpp           # Application entry point
├── lib/                   # Custom libraries
│   ├── config/           # Configuration management
│   ├── wifi_manager/     # WiFi connection handling
│   ├── webui/            # Web interface
│   ├── mqtt_client/      # MQTT communication
│   ├── modbus_client/    # Modbus RTU client
│   ├── poller/           # Data polling engine
│   ├── inverters/        # Inverter implementations
│   ├── meters/           # Meter implementations
│   └── ESPLogger/        # Logging system
├── include/              # Header files
│   └── constants.h       # Global constants
├── docs/                 # Documentation
└── platformio.ini        # PlatformIO configuration
```

### **Build Environments**
- **`dev`**: Development build with debug information
- **`prod`**: Production build with optimized settings

### **Adding New Devices**

#### **Option 1: JSON Configuration (No Recompilation Required)**

The easiest way to add a new Modbus device:

1. Create a JSON file describing your device's register map (see `docs/example_device.json`)
2. Upload it via the **Diagnostics page** → **Custom Device JSON Manager**
3. Select your device from the **Config page** dropdown
4. Save and reboot

**See [Custom Devices Guide](docs/CUSTOM_DEVICES.md) for complete instructions and JSON format.**

#### **Option 2: C++ Implementation (For Complex Devices)**

For devices requiring custom logic or complex register handling:

1. Create the device class in `lib/inverters/` or `lib/meters/` and implement `InverterInterface`.
2. Register the model in `lib/inverter_core/InverterFactory.cpp`.
3. Add the model constant + display name to `SUPPORTED_DEVICES` in `include/constants.h`.
4. Document the Modbus register map under `docs/<vendor>/`.
5. Add a `test_<device>` Unity suite under `test/` and add it to the compile-only matrix in `.github/workflows/test.yml`.
6. Update the Web UI device picker (the dropdown is generated from `SUPPORTED_DEVICES`, so step 3 may be enough).
7. Verify `scripts/check_readonly.sh` still passes — your new driver must not introduce Modbus write opcodes.

## 🚀 Releases

Download the latest firmware from the [Releases](https://github.com/vukomir/esp32-modbusbridge/releases) page. Build commands are in [Quick Start](#-quick-start).

## 🔍 Troubleshooting

### **Common Issues**

#### **Device Not Connecting to WiFi**
- Check WiFi credentials
- Ensure device is in range
- Try factory reset if needed

#### **Modbus Communication Issues**
- Verify RS485 wiring (RX, TX, DE/RE, VCC, GND)
- Check device address and baud rate
- Ensure MAX485 module is properly connected

#### **MQTT Connection Problems**
- Verify broker address and port
- Check username/password
- Ensure network connectivity

#### **Web Interface Not Accessible**
- Try accessing via IP address
- Check if mDNS is working (`modbusbridge.local`)
- Verify device is connected to WiFi

### **Debug Console**
Access the real-time console at `/console` to:
- View live system logs
- Monitor connection status
- Adjust log levels dynamically
- Track hardware diagnostics

## 📋 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/vukomir/esp32-modbusbridge/issues)
- **Discussions**: [GitHub Discussions](https://github.com/vukomir/esp32-modbusbridge/discussions)
- **Documentation**: Check the `/docs` folder for detailed guides
