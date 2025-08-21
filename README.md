# 🔌 ESP32 Inverter Monitoring System

A comprehensive ESP32-based monitoring system for solar inverters and smart meters using Modbus RTU communication. Features real-time data collection, MQTT integration, web-based configuration, and a live console for monitoring.

![ESP32](https://img.shields.io/badge/ESP32-Platform-blue)
![PlatformIO](https://img.shields.io/badge/PlatformIO-Build%20System-green)
![MQTT](https://img.shields.io/badge/MQTT-Protocol-orange)
![Modbus](https://img.shields.io/badge/Modbus-RTU-red)

## 🌟 Features

### 🔧 **Core Functionality**
- **Modbus RTU Communication**: RS485 interface for inverter/meter communication
- **MQTT Integration**: Real-time data publishing to MQTT broker
- **Web Configuration Interface**: Easy setup and configuration via web browser
- **Real-time Console**: Live logging and monitoring via WebSocket
- **NTP Time Synchronization**: Accurate timestamps for all data
- **Dual-Core Architecture**: Optimized task distribution on ESP32

### 📊 **Supported Devices**
- **SolPlanet ASW GEN Inverter**: Solar inverter monitoring
- **Hiking DDS238 Smart Meter**: Energy consumption monitoring
- **Extensible Architecture**: Easy to add new device types

### 🌐 **Network Features**
- **WiFi Management**: Automatic connection with AP fallback
- **mDNS Support**: Access via hostname (e.g., `modbusbridge.local`)
- **Captive Portal**: Easy initial setup
- **OTA Updates**: Over-the-air firmware updates

### 🔍 **Monitoring & Debugging**
- **Live Web Console**: Real-time log streaming with WebSocket
- **Log Level Control**: Dynamic log verbosity adjustment
- **System Status**: Memory usage, uptime, connection status
- **Hardware Diagnostics**: MAX485 module detection and status

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

## 📡 MQTT Topics

The system publishes data to MQTT topics in the following format:

```
{prefix}/status/connection          # Connection status
{prefix}/status/poll_status         # Polling status
{prefix}/status/max485_status       # RS485 module status
{prefix}/status/inverter_status     # Inverter connection status
{prefix}/status/system_info         # System information
{prefix}/metrics/rssi               # WiFi signal strength
{prefix}/metrics/uptime             # Device uptime
{prefix}/config/info                # Configuration information
```

### **Example Topics**
```
modbusbridge/status/connection
modbusbridge/status/poll_status
modbusbridge/metrics/rssi
```

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
1. Create device class in `lib/inverters/` or `lib/meters/`
2. Implement the device interface
3. Add device to supported devices list in `constants.h`
4. Update web interface for device selection

## 🚀 Releases

### **Latest Release**
Download the latest firmware from the [Releases](https://github.com/vukomir/esp32-modbusbridge/releases) page.

### **Building from Source**
```bash
# Development build
pio run -e dev

# Production build
pio run -e prod

# Upload to device
pio run --target upload
```

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

## 🙏 Acknowledgments

- ESP32 Arduino framework
- PlatformIO build system
- ArduinoJson library
- WebSockets library
- PubSubClient library

---

**Built with ❤️ for the solar monitoring community**
