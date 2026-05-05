# Home Assistant Setup Guide

Complete guide for integrating your ESP32 Modbus Solar Monitoring System with Home Assistant.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Step-by-Step Setup](#step-by-step-setup)
- [Energy Dashboard Configuration](#energy-dashboard-configuration)
- [Dashboard Examples](#dashboard-examples)
- [Automation Examples](#automation-examples)
- [Consumption Tracking](#consumption-tracking)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

✅ **Required:**
- Home Assistant installed and running
- MQTT broker (Mosquitto recommended)
- ESP32 Modbus bridge configured and connected to MQTT
- SolPlanet ASW inverter connected via RS485

✅ **Optional:**
- Smart meter (e.g., Hiking DDS238) for consumption tracking
- Multiple PV strings for individual monitoring
- Battery storage system (if your inverter supports it)

---

## Quick Start

### 1. Verify MQTT Connection

First, confirm your ESP32 is publishing data. Connect to your MQTT broker:

```bash
# Subscribe to all topics from your ESP32
mosquitto_sub -h YOUR_MQTT_BROKER_IP -t "home/#" -v

# You should see output like:
# home/inverter/telemetry/active_power {"value":2350,"unit":"W","timestamp":123456}
# home/inverter/telemetry/energy_today {"value":12.5,"unit":"kWh","timestamp":123456}
# home/inverter/status/availability online
```

### 2. Check ESP32 Web UI

Open `http://modbusbridge.local` (or your ESP32's IP address):
- Verify **Status** page shows successful polls
- Check **Diagnostics** page for Modbus communication health
- Note your MQTT topic prefix (default: `home`)

### 3. Add MQTT Integration to Home Assistant

Go to **Settings** → **Devices & Services** → **Add Integration** → search for **MQTT**.

Configure your MQTT broker details if not already done.

---

## Step-by-Step Setup

### Step 1: Configure MQTT Sensors

Edit your `configuration.yaml` file and add the following sensors.

> **💡 Tip:** The `value_template` handles both simple (`"123.4"`) and JSON (`{"value":123.4,"unit":"W"}`) payloads automatically.

```yaml
mqtt:
  sensor:
    # ========================================
    # SOLAR PRODUCTION - Power & Energy
    # ========================================
    
    - name: "Solar Active Power"
      unique_id: solar_active_power
      state_topic: "home/inverter/telemetry/active_power"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "W"
      device_class: power
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
          payload_available: "online"
          payload_not_available: "offline"
      device:
        identifiers: ["esp32_solplanet_bridge"]
        name: "SolPlanet Solar Inverter"
        model: "SolPlanet ASW GEN"
        manufacturer: "SolPlanet"
        sw_version: "1.0.0"
        configuration_url: "http://modbusbridge.local"

    - name: "Solar Daily Production"
      unique_id: solar_daily_production
      state_topic: "home/inverter/telemetry/energy_today"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "kWh"
      device_class: energy
      state_class: total_increasing
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Total Production"
      unique_id: solar_total_production
      state_topic: "home/inverter/telemetry/energy_total"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "kWh"
      device_class: energy
      state_class: total_increasing
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Total Hours"
      unique_id: solar_total_hours
      state_topic: "home/inverter/telemetry/hours_total"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "h"
      device_class: duration
      state_class: total_increasing
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    # ========================================
    # GRID MEASUREMENTS - THREE-PHASE
    # ========================================

    # === PHASE L1 (First Phase) ===
    - name: "Solar L1 Voltage"
      unique_id: solar_l1_voltage
      state_topic: "home/inverter/telemetry/l1_voltage"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar L1 Current"
      unique_id: solar_l1_current
      state_topic: "home/inverter/telemetry/l1_current"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "A"
      device_class: current
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    # === PHASE L2 (Second Phase) ===
    - name: "Solar L2 Voltage"
      unique_id: solar_l2_voltage
      state_topic: "home/inverter/telemetry/l2_voltage"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar L2 Current"
      unique_id: solar_l2_current
      state_topic: "home/inverter/telemetry/l2_current"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "A"
      device_class: current
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    # === PHASE L3 (Third Phase) ===
    - name: "Solar L3 Voltage"
      unique_id: solar_l3_voltage
      state_topic: "home/inverter/telemetry/l3_voltage"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar L3 Current"
      unique_id: solar_l3_current
      state_topic: "home/inverter/telemetry/l3_current"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "A"
      device_class: current
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    # === LINE-TO-LINE VOLTAGES (3-Phase) ===
    - name: "Solar RS Line Voltage"
      unique_id: solar_rs_line_voltage
      state_topic: "home/inverter/telemetry/rs_line_voltage"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar RT Line Voltage"
      unique_id: solar_rt_line_voltage
      state_topic: "home/inverter/telemetry/rt_line_voltage"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar ST Line Voltage"
      unique_id: solar_st_line_voltage
      state_topic: "home/inverter/telemetry/st_line_voltage"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    # === GRID FREQUENCY (Common for all phases) ===
    - name: "Solar Grid Frequency"
      unique_id: solar_grid_frequency
      state_topic: "home/inverter/telemetry/grid_frequency"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "Hz"
      device_class: frequency
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Power Factor"
      unique_id: solar_power_factor
      state_topic: "home/inverter/telemetry/power_factor"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      device_class: power_factor
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Apparent Power"
      unique_id: solar_apparent_power
      state_topic: "home/inverter/telemetry/apparent_power"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "VA"
      device_class: apparent_power
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Reactive Power"
      unique_id: solar_reactive_power
      state_topic: "home/inverter/telemetry/reactive_power"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "var"
      device_class: reactive_power
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    # ========================================
    # PV STRING MONITORING (String 1)
    # ========================================

    - name: "Solar PV1 Voltage"
      unique_id: solar_pv1_voltage
      state_topic: "home/inverter/telemetry/pv1_voltage"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar PV1 Current"
      unique_id: solar_pv1_current
      state_topic: "home/inverter/telemetry/pv1_current"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "A"
      device_class: current
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar PV1 Power"
      unique_id: solar_pv1_power
      state_topic: "home/inverter/telemetry/pv1_power"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "W"
      device_class: power
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    # ========================================
    # PV STRING 2 (if you have multiple strings)
    # Copy the above pattern for pv2_voltage, pv2_current, pv2_power
    # Repeat for pv3, pv4, etc. as needed
    # ========================================

    # ========================================
    # BATTERY STORAGE (if enabled)
    # ========================================

    - name: "Battery Power"
      unique_id: battery_power
      state_topic: "home/inverter/telemetry/battery_power"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "W"
      device_class: power
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Battery Voltage"
      unique_id: battery_voltage
      state_topic: "home/inverter/telemetry/battery_voltage"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Battery Current"
      unique_id: battery_current
      state_topic: "home/inverter/telemetry/battery_current"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "A"
      device_class: current
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Battery SOC"
      unique_id: battery_soc
      state_topic: "home/inverter/telemetry/battery_soc"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "%"
      device_class: battery
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Battery SOH"
      unique_id: battery_soh
      state_topic: "home/inverter/telemetry/battery_soh"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "%"
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Battery Temperature"
      unique_id: battery_temperature
      state_topic: "home/inverter/telemetry/battery_temperature"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    # ========================================
    # TEMPERATURES
    # ========================================

    - name: "Solar Internal Temperature"
      unique_id: solar_internal_temp
      state_topic: "home/inverter/telemetry/temperature_internal"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Inverter U Temperature"
      unique_id: solar_inverter_u_temp
      state_topic: "home/inverter/telemetry/temperature_inverter_u"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Boost Temperature"
      unique_id: solar_boost_temp
      state_topic: "home/inverter/telemetry/temperature_boost"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "°C"
      device_class: temperature
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    # ========================================
    # SYSTEM STATUS
    # ========================================

    - name: "Solar Inverter State"
      unique_id: solar_inverter_state
      state_topic: "home/inverter/telemetry/device_state_text"
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Error Code"
      unique_id: solar_error_code
      state_topic: "home/inverter/telemetry/error_code"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Error Message"
      unique_id: solar_error_message
      state_topic: "home/inverter/telemetry/error_message"
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Warning Code"
      unique_id: solar_warning_code
      state_topic: "home/inverter/telemetry/warning_code"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

    - name: "Solar Bus Voltage"
      unique_id: solar_bus_voltage
      state_topic: "home/inverter/telemetry/bus_voltage"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability:
        - topic: "home/inverter/status/availability"
      device:
        identifiers: ["esp32_solplanet_bridge"]

  # ========================================
  # BINARY SENSORS
  # ========================================

  binary_sensor:
    - name: "Solar Inverter Online"
      unique_id: solar_inverter_online
      state_topic: "home/inverter/status/availability"
      payload_on: "online"
      payload_off: "offline"
      device_class: connectivity
      device:
        identifiers: ["esp32_solplanet_bridge"]
        name: "SolPlanet Solar Inverter"
        model: "SolPlanet ASW GEN"
        manufacturer: "SolPlanet"

    - name: "Solar Inverter Fault"
      unique_id: solar_inverter_fault
      state_topic: "home/inverter/telemetry/device_state_text"
      payload_on: "Fault"
      payload_off: "Normal"
      device_class: problem
      device:
        identifiers: ["esp32_solplanet_bridge"]
```

### Step 2: Restart Home Assistant

After editing `configuration.yaml`:
1. Go to **Developer Tools** → **YAML**
2. Click **Check Configuration** to verify syntax
3. Click **Restart** to reload configuration

### Step 3: Verify Entities

Go to **Developer Tools** → **States** and search for `solar`. You should see all your new entities:
- `sensor.solar_active_power`
- `sensor.solar_daily_production`
- `sensor.solar_total_production`
- `binary_sensor.solar_inverter_online`
- etc.

---

## Energy Dashboard Configuration

### Add Solar Production

1. Go to **Settings** → **Dashboards** → **Energy**
2. Click **Add Solar Production**
3. Select **`sensor.solar_total_production`**
4. Click **Save**

Home Assistant will now track:
- ☀️ Daily solar production
- 📊 Monthly/yearly trends
- 💰 Energy cost savings (if you configure electricity prices)

### Add Grid Consumption (Optional)

If you have a smart meter connected to the same RS485 bus:

1. Add meter sensors to your `configuration.yaml` (see [Consumption Tracking](#consumption-tracking))
2. In Energy Dashboard, click **Add Grid Consumption**
3. Select your grid import/export sensors
4. Configure electricity pricing

### Add Battery Storage (Optional)

If you enabled battery storage (`read_storage_regs: true`):

1. Click **Add Battery System**
2. Energy going in to battery: `sensor.battery_charge_today_kwh`
3. Energy coming out of battery: `sensor.battery_discharge_today_kwh`
4. Click **Save**

---

## Dashboard Examples

### Basic Solar Dashboard

Create a new dashboard: **Settings** → **Dashboards** → **Add Dashboard**

```yaml
title: Solar Production
views:
  - title: Overview
    path: overview
    cards:
      # Real-time Power Gauge
      - type: gauge
        entity: sensor.solar_active_power
        name: Current Solar Production
        min: 0
        max: 5000  # Adjust to your inverter's max power rating
        needle: true
        severity:
          green: 1000
          yellow: 500
          red: 0

      # Daily Production
      - type: sensor
        entity: sensor.solar_daily_production
        name: Today's Production
        graph: line
        hours_to_show: 24
        detail: 2

      # Total Production Counter
      - type: entity
        entity: sensor.solar_total_production
        name: Total Lifetime Production
        icon: mdi:solar-power

      # PV String Comparison
      - type: horizontal-stack
        cards:
          - type: gauge
            entity: sensor.solar_pv1_power
            name: PV String 1
            min: 0
            max: 2500
          - type: gauge
            entity: sensor.solar_pv2_power
            name: PV String 2
            min: 0
            max: 2500

      # Grid Metrics - Three-Phase
      - type: entities
        title: "Grid Connection - Phase Voltages"
        entities:
          - entity: sensor.solar_l1_voltage
            name: "L1 Voltage"
          - entity: sensor.solar_l2_voltage
            name: "L2 Voltage"
          - entity: sensor.solar_l3_voltage
            name: "L3 Voltage"
          - entity: sensor.solar_grid_frequency
            name: Frequency
          - entity: binary_sensor.solar_inverter_online
            name: Connection Status

      - type: entities
        title: "Grid Connection - Phase Currents"
        entities:
          - entity: sensor.solar_l1_current
            name: "L1 Current"
          - entity: sensor.solar_l2_current
            name: "L2 Current"
          - entity: sensor.solar_l3_current
            name: "L3 Current"
          - entity: sensor.solar_power_factor
            name: Power Factor

      - type: entities
        title: "Line-to-Line Voltages"
        entities:
          - entity: sensor.solar_rs_line_voltage
            name: "R-S Voltage"
          - entity: sensor.solar_rt_line_voltage
            name: "R-T Voltage"
          - entity: sensor.solar_st_line_voltage
            name: "S-T Voltage"

      # System Status
      - type: entities
        title: Inverter Status
        entities:
          - entity: sensor.solar_inverter_state
            name: State
          - entity: sensor.solar_internal_temperature
            name: Internal Temp
          - entity: sensor.solar_boost_temperature
            name: Boost Temp
          - entity: sensor.solar_error_code
            name: Error Code
          - entity: sensor.solar_total_hours
            name: Operating Hours
```

### Battery Dashboard (if enabled)

```yaml
title: Battery Storage
cards:
  - type: gauge
    entity: sensor.battery_soc
    name: Battery Charge
    min: 0
    max: 100
    needle: true
    severity:
      green: 50
      yellow: 20
      red: 0

  - type: entity
    entity: sensor.battery_power
    name: Battery Power
    icon: mdi:battery-charging

  - type: entities
    title: Battery Details
    entities:
      - sensor.battery_voltage
      - sensor.battery_current
      - sensor.battery_temperature
      - sensor.battery_soh
```

### Advanced Energy Flow Card

Install **Power Flow Card Plus** from HACS, then:

```yaml
type: custom:power-flow-card-plus
entities:
  solar:
    entity: sensor.solar_active_power
  grid:
    entity: sensor.grid_power  # From your smart meter
  battery:
    entity: sensor.battery_power
  home:
    entity: sensor.home_consumption  # Calculated template sensor
```

---

## Automation Examples

### Daily Production Report

```yaml
automation:
  - id: solar_daily_report
    alias: "Solar Daily Production Report"
    description: "Send notification with daily solar production"
    trigger:
      - platform: time
        at: "20:00:00"
    condition:
      - condition: numeric_state
        entity_id: sensor.solar_daily_production
        above: 0
    action:
      - service: notify.mobile_app_your_phone
        data:
          title: "☀️ Solar Production Today"
          message: >
            Generated {{ states('sensor.solar_daily_production') }} kWh today.
            Total lifetime: {{ states('sensor.solar_total_production') }} kWh
            Peak power: {{ state_attr('sensor.solar_active_power', 'max_value') }} W
```

### Inverter Fault Alert

```yaml
automation:
  - id: solar_fault_alert
    alias: "Solar Inverter Fault Alert"
    description: "Alert when inverter reports a fault"
    trigger:
      - platform: state
        entity_id: sensor.solar_inverter_state
        to: "Fault"
    action:
      - service: notify.mobile_app_your_phone
        data:
          title: "⚠️ Solar Inverter Fault"
          message: >
            Inverter has reported a fault condition.
            Error code: {{ states('sensor.solar_error_code') }}
            Message: {{ states('sensor.solar_error_message') }}
          data:
            priority: high
            ttl: 0
            channel: alerts
```

### Offline Alert

```yaml
automation:
  - id: solar_offline_alert
    alias: "Solar Inverter Offline Alert"
    description: "Alert when solar monitoring goes offline"
    trigger:
      - platform: state
        entity_id: binary_sensor.solar_inverter_online
        to: "off"
        for:
          minutes: 5
    action:
      - service: notify.mobile_app_your_phone
        data:
          title: "🔴 Solar Inverter Offline"
          message: "Solar monitoring system has been offline for 5 minutes. Check RS485 connection."
          data:
            priority: high
```

### Low Performance Warning

```yaml
automation:
  - id: solar_low_performance
    alias: "Solar Low Performance Warning"
    description: "Alert if solar production is abnormally low during daylight"
    trigger:
      - platform: numeric_state
        entity_id: sensor.solar_active_power
        below: 100
        for:
          hours: 2
    condition:
      - condition: sun
        after: sunrise
        after_offset: "01:00:00"
      - condition: sun
        before: sunset
        before_offset: "-01:00:00"
    action:
      - service: notify.maintenance
        data:
          title: "⚠️ Low Solar Performance"
          message: "Solar production has been below 100W for 2 hours during daylight. Check system."
```

### High Temperature Alert

```yaml
automation:
  - id: solar_high_temp_alert
    alias: "Solar High Temperature Alert"
    description: "Alert when inverter temperature exceeds safe threshold"
    trigger:
      - platform: numeric_state
        entity_id: sensor.solar_internal_temperature
        above: 70  # Adjust based on your inverter specs
        for:
          minutes: 10
    action:
      - service: notify.mobile_app_your_phone
        data:
          title: "🌡️ High Inverter Temperature"
          message: >
            Inverter internal temperature is {{ states('sensor.solar_internal_temperature') }}°C
            Check ventilation and ambient conditions.
```

---

## Consumption Tracking

To track **household power consumption**, add a smart meter to your RS485 bus.

### Supported Meters

- **Hiking DDS238** (tested, works with this system)
- **SDM120** / **SDM220** / **SDM630** (Modbus RTU compatible)
- Any Modbus RTU energy meter

### Hardware Setup

1. Connect meter to the same RS485 bus as your inverter:
   ```
   ESP32 MAX485 → Inverter (slave ID 1)
                → Meter (slave ID 2)
   ```

2. Configure meter in ESP32 web UI:
   - Go to `http://modbusbridge.local`
   - Set **Device Model** to your meter type
   - Set **Modbus Address** (e.g., `2` if inverter is `1`)
   - Set **Poll Interval** (10-30 seconds recommended)

### Add Meter Sensors to Home Assistant

```yaml
mqtt:
  sensor:
    # Grid Power Consumption
    - name: "Grid Power Consumption"
      unique_id: grid_power_consumption
      state_topic: "home/meter/telemetry/active_power"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "W"
      device_class: power
      state_class: measurement
      availability:
        - topic: "home/meter/status/availability"
      device:
        identifiers: ["esp32_energy_meter"]
        name: "Energy Meter"
        model: "Hiking DDS238"
        manufacturer: "Hiking"

    # Total Energy Consumed
    - name: "Total Energy Consumed"
      unique_id: total_energy_consumed
      state_topic: "home/meter/telemetry/energy_total"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "kWh"
      device_class: energy
      state_class: total_increasing
      availability:
        - topic: "home/meter/status/availability"
      device:
        identifiers: ["esp32_energy_meter"]

    # Grid Voltage
    - name: "Grid Voltage"
      unique_id: grid_voltage
      state_topic: "home/meter/telemetry/voltage"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "V"
      device_class: voltage
      state_class: measurement
      availability:
        - topic: "home/meter/status/availability"
      device:
        identifiers: ["esp32_energy_meter"]

    # Grid Current
    - name: "Grid Current"
      unique_id: grid_current
      state_topic: "home/meter/telemetry/current"
      value_template: >
        {% if value_json is defined %}
          {{ value_json.value }}
        {% else %}
          {{ value }}
        {% endif %}
      unit_of_measurement: "A"
      device_class: current
      state_class: measurement
      availability:
        - topic: "home/meter/status/availability"
      device:
        identifiers: ["esp32_energy_meter"]
```

### Calculate Self-Consumption

Create template sensors to calculate how much solar you're using vs. exporting:

```yaml
template:
  - sensor:
      # Home Consumption (Solar + Grid Import)
      - name: "Home Consumption"
        unique_id: home_consumption
        unit_of_measurement: "W"
        device_class: power
        state_class: measurement
        state: >
          {% set solar = states('sensor.solar_active_power') | float(0) %}
          {% set grid = states('sensor.grid_power_consumption') | float(0) %}
          {{ (solar + grid) | round(0) }}

      # Solar Self Consumption (Solar - Grid Export)
      - name: "Solar Self Consumption"
        unique_id: solar_self_consumption
        unit_of_measurement: "W"
        device_class: power
        state_class: measurement
        state: >
          {% set solar = states('sensor.solar_active_power') | float(0) %}
          {% set grid = states('sensor.grid_power_consumption') | float(0) %}
          {% if grid < 0 %}
            {{ (solar + grid) | round(0) }}
          {% else %}
            {{ solar | round(0) }}
          {% endif %}

      # Grid Export (negative grid power means export)
      - name: "Grid Export"
        unique_id: grid_export
        unit_of_measurement: "W"
        device_class: power
        state_class: measurement
        state: >
          {% set grid = states('sensor.grid_power_consumption') | float(0) %}
          {{ (grid * -1) if grid < 0 else 0 }}

      # Self-Consumption Percentage
      - name: "Solar Self Consumption Percentage"
        unique_id: solar_self_consumption_pct
        unit_of_measurement: "%"
        state: >
          {% set solar = states('sensor.solar_active_power') | float(0) %}
          {% set self_use = states('sensor.solar_self_consumption') | float(0) %}
          {% if solar > 0 %}
            {{ ((self_use / solar) * 100) | round(1) }}
          {% else %}
            0
          {% endif %}
```

---

## Troubleshooting

### No Entities Showing Up

**Check MQTT topics:**
```bash
mosquitto_sub -h YOUR_BROKER_IP -t "home/#" -v
```

If you see no data:
1. Check ESP32 web UI → **Status** page
2. Verify MQTT broker IP and credentials
3. Check ESP32 logs: `pio device monitor --baud 115200`

**Check Home Assistant MQTT integration:**
1. Go to **Settings** → **Devices & Services**
2. Click **MQTT** → **Configure**
3. Test by publishing: **Developer Tools** → **MQTT** → publish to `test/topic`

### Entities Show "Unavailable"

**Check availability topic:**
```bash
mosquitto_sub -h YOUR_BROKER_IP -t "home/inverter/status/availability" -v
```

Should show: `online`

If it shows `offline`:
1. Check Modbus communication on ESP32 diagnostics page
2. Verify RS485 wiring (A/B terminals, termination resistor)
3. Check inverter slave address matches ESP32 config

### Values Are Wrong or Stale

**Check retained messages:**
```bash
mosquitto_sub -h YOUR_BROKER_IP -t "home/inverter/telemetry/#" -v --retained-only
```

Clear retained messages if needed:
```bash
mosquitto_pub -h YOUR_BROKER_IP -t "home/inverter/telemetry/active_power" -r -n
```

### Energy Dashboard Not Working

**Requirements:**
- Sensor must have `state_class: total_increasing`
- Unit must be `kWh`
- Values must be cumulative (not reset daily)

Use `sensor.solar_total_production` for energy dashboard, NOT `sensor.solar_daily_production`.

### High CPU Usage in Home Assistant

**Reduce MQTT traffic:**
1. Increase poll interval in ESP32 config (10-30 seconds)
2. Disable unused telemetry (e.g., storage if you don't have battery)
3. Use recorder exclude filter:

```yaml
recorder:
  exclude:
    entities:
      - sensor.solar_error_code
      - sensor.solar_warning_code
```

---

## Additional Resources

- **Full Integration Documentation:** See `HOME_ASSISTANT_INTEGRATION_PROMPT.md`
- **MQTT Topic Reference:** See ESP32 web UI → Diagnostics
- **Modbus Register Map:** `docs/solplanet/solplanet_modbus.md`
- **ESP32 Configuration:** `http://modbusbridge.local`

---

## Quick Checklist

- [ ] MQTT broker configured in Home Assistant
- [ ] ESP32 publishing to MQTT (verify with `mosquitto_sub`)
- [ ] Sensors added to `configuration.yaml`
- [ ] Home Assistant restarted
- [ ] Entities visible in **Developer Tools** → **States**
- [ ] Energy Dashboard configured with `sensor.solar_total_production`
- [ ] Dashboard created with solar cards
- [ ] Automations set up for alerts
- [ ] (Optional) Smart meter added for consumption tracking
- [ ] (Optional) Battery sensors configured if storage enabled

---

**🎉 Congratulations!** Your solar monitoring is now integrated with Home Assistant.

For questions or issues, check the ESP32 diagnostics page or open an issue on GitHub.
