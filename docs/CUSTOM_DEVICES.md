# Custom Device JSON Configuration

This firmware supports adding new Modbus devices without recompiling firmware by uploading JSON configuration files.

## JSON Format

Each device configuration is a JSON file with the following structure:

```json
{
  "device_id": "unique_identifier",
  "name": "Human-readable Device Name",
  "registers": [
    {
      "addr": "0x0000",
      "name": "register_name",
      "fc": 4,
      "type": "uint16",
      "scale": 1.0,
      "unit": "V",
      "storage": false
    }
  ]
}
```

### Field Definitions

**Top Level:**
- `device_id` (string): Unique identifier for the device (no spaces, alphanumeric + underscore)
- `name` (string): Display name shown in UI
- `registers` (array): Array of register definitions

**Register Definition:**
- `addr` (string): Register address in hex (e.g., `"0x0000"`) or decimal (e.g., `"100"`)
- `name` (string): Telemetry point name (will appear in MQTT topic)
- `fc` (number): Modbus function code
  - `3` = Read Holding Registers (FC 0x03)
  - `4` = Read Input Registers (FC 0x04)
- `type` (string): Data type
  - `"uint16"` = Unsigned 16-bit integer (1 register)
  - `"int16"` = Signed 16-bit integer (1 register)
  - `"uint32"` = Unsigned 32-bit integer (2 registers, big-endian)
  - `"int32"` = Signed 32-bit integer (2 registers, big-endian)
- `scale` (number): Scaling factor applied to raw value (e.g., `0.1` for 235 → 23.5V)
- `unit` (string): Unit of measurement (e.g., `"V"`, `"A"`, `"W"`, `"kWh"`, `"%"`)
- `storage` (boolean, optional): Set to `true` for battery/storage registers (read less frequently). Defaults to `false`.

### Register Addressing

- **Hex format:** `"0x0000"` to `"0xFFFF"`
- **Decimal format:** `"0"` to `"65535"`
- For 32-bit types (`uint32`, `int32`), the address is the **first register** of the pair
  - Example: `addr: "0x0006"` for `uint32` reads registers 0x0006 and 0x0007

### Storage Registers

Registers marked with `"storage": true` are read separately via `readStorage()` and published to different MQTT topics (battery data). Use this for:
- Battery voltage, current, power, SOC
- Storage system metrics
- Any data that should be polled less frequently

## Workflow

### 1. Create JSON Configuration

Use `docs/example_device.json` as a template. Consult your device's Modbus register map documentation.

**Example:** `my_inverter.json`
```json
{
  "device_id": "my_inv_v1",
  "name": "My Custom Inverter",
  "registers": [
    {
      "addr": "0x0000",
      "name": "voltage",
      "fc": 4,
      "type": "uint16",
      "scale": 0.1,
      "unit": "V"
    },
    {
      "addr": "0x0010",
      "name": "power",
      "fc": 4,
      "type": "uint32",
      "scale": 1,
      "unit": "W"
    }
  ]
}
```

### 2. Upload JSON File

1. Enable diagnostics: Go to **Config page** → Enable "Enable Diagnostic Tools" → Save
2. Go to **Diagnostics page** (link appears after enabling)
3. Scroll to **"Custom Device JSON Manager"**
4. Click **"Choose File"**, select your `.json` file
5. Click **"📤 Upload JSON"**
6. File is saved to `/devices/` directory on the ESP32

### 3. Select Device in Config

1. Go to **Config page**
2. Under **"Device Model"** dropdown, your device appears as **"My Custom Inverter (Custom)"**
3. Select it and click **"Save Configuration"**
4. Device will reboot and load your custom device

### 4. Verify Operation

After reboot:
1. Check **Status page** for telemetry data
2. If no data appears:
   - Go to **Diagnostics page**
   - Use **"Manual Register Probe"** to test individual registers
   - Check **"Modbus Frame Log"** for errors (timeouts, exceptions)
   - Verify slave address in config matches your device

## MQTT Topics

Custom devices publish to the **generic topic structure**:

**Basic telemetry (storage=false):**
```
{prefix}/{device_type}/telemetry/{register_name}
```

**Storage registers (storage=true):**
```
{prefix}/{device_type}/telemetry/{register_name}
```
(Note: Storage registers use the same topic structure but are read less frequently)

**Status and availability:**
```
{prefix}/{device_type}/status/availability → "online" or "offline"
{prefix}/{device_type}/status/poll_status → JSON with poll statistics
```

**Example:** For register `"name": "voltage"` with prefix `home` and device type `inverter`:
```
home/inverter/telemetry/voltage → {"value":230.5,"unit":"V","timestamp":123456,"device_id":"abc123"}
```

**JSON Payload Format:**
When `mqtt_json_format: true` (default), each value is published as JSON:
```json
{
  "value": 230.5,
  "unit": "V",
  "timestamp": 1234567890,
  "device_id": "esp32-abc123"
}
```

**Simple Format:**
When `mqtt_json_format: false`, only the value is published:
```
230.5
```

## Troubleshooting

### Device not appearing in dropdown
- Verify JSON file uploaded successfully (use "📋 List Files")
- Check file has `.json` extension
- Reboot device or refresh config page

### No telemetry data after selection
1. **Wrong slave address:** Check config matches your device (default is 1)
2. **Invalid registers:** Use diagnostics to probe registers individually
3. **Wrong function code:** Some devices only support FC 0x03 or 0x04, not both
4. **Modbus timeout:** Check RS485 wiring (A, B, GND), termination resistors, baud rate

### Malformed JSON
Upload will fail if JSON is invalid. Validate your JSON using an online validator before uploading.

### Register reads fail with exception 0x02
"Illegal Data Address" means the register doesn't exist. Verify register addresses match your device's datasheet.

## Best Practices

1. **Start small:** Test with 2-3 critical registers first, then expand
2. **Use diagnostics:** Probe each register manually before adding to JSON
3. **Document your sources:** Add comments to JSON (not parsed by firmware, but useful for documentation)
4. **Version your configs:** Name files like `device_v1.json`, `device_v2.json`
5. **Test storage flag:** Battery data changes slowly; marking as storage reduces Modbus traffic
6. **Verify scaling factors:** Don't trust manufacturer docs blindly - test actual values:
   - SOC/SOH often return 0-100 directly (scale: 1.0), not 0-10000 (scale: 0.01)
   - Temperature scales vary: some use 0.1, others 1.0, check with diagnostics
   - Always compare raw Modbus value vs. expected real-world value

## Limitations

- **Single device per ESP32:** One Modbus device at a time (one inverter OR one meter)
- **No string/text registers:** Only numeric data types supported (uint16, int16, uint32, int32)
- **32-bit byte order:** Must be big-endian (high word in first register, low word in second)
- **Register count:** Maximum ~100 registers per device (limited by poll interval and Modbus timeout)
- **Function codes:** Only FC 0x03 (Read Holding) and FC 0x04 (Read Input) supported
  - No support for coils (FC 0x01, 0x02) or write operations (FC 0x05, 0x06, 0x10)
- **No computed values:** Cannot calculate power from V×A in JSON (do this in Home Assistant templates)
- **Comments field:** The `"comment"` field in JSON is for documentation only - firmware ignores it

## Example Files

See `docs/example_device.json` for a complete example with:
- Mixed data types (uint16, uint32, int16, int32)
- Basic and storage registers
- Proper scaling factors
- Common units (V, A, W, kWh, Hz, °C, %)

## Need Help?

If your device isn't working:
1. Check Modbus frame log in diagnostics
2. Verify register addresses against device datasheet
3. Test with manufacturer's Modbus tool first (if available)
4. File an issue with frame log output and device model
