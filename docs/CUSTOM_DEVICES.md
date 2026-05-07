# Custom Device JSON Configuration

This firmware supports adding new Modbus devices without recompiling firmware by uploading JSON configuration files.

## Schema versions

| Version | Status | Adds |
|---|---|---|
| 1 (or `schema_version` omitted) | Legacy, fully supported | Flat `registers[]` only |
| 2 | Current | `groups[]` (block reads), per-field `bits[]` decoding, per-field `nan_value` sentinel |

A schema 2 file MUST set `"schema_version": 2`. Older files keep working unchanged.

### Migration notes

- **`offset` on flat registers is now rejected.** Previous firmware silently ignored an `offset` key on a `registers[*]` entry; current firmware errors at validation. `offset` is meaningful only inside `groups[*].fields[*]`. Remove the key from any flat register entries that had it — even `"offset": 0` is rejected (was a no-op before).
- **Address strings are validated strictly.** `addr` and `start` must be either decimal (`"123"`, `"0"`) or `0x`-prefixed hex (`"0x007B"`). Multi-digit leading zeros (`"010"`), leading whitespace (`"  100"`), and signed values (`"-1"`) are now rejected — previously some of these silently mis-parsed (`"010"` was octal 8). Same rule applies to `mask` strings inside `bits[]` and to string-form `nan_value`.

## JSON format (schema 2)

```json
{
  "device_id": "unique_identifier",
  "name": "Human-readable Device Name",
  "description": "Optional documentation only, not used by firmware",
  "schema_version": 2,
  "registers": [
    {
      "addr": "0x0050",
      "name": "device_state",
      "fc": 4,
      "type": "uint16",
      "scale": 1.0,
      "unit": "",
      "storage": false
    }
  ],
  "groups": [
    {
      "name": "ac_block",
      "fc": 4,
      "start": "0x0000",
      "count": 18,
      "storage": false,
      "fields": [
        { "offset": 0,  "name": "voltage",      "type": "uint16", "scale": 0.1,  "unit": "V" },
        { "offset": 10, "name": "active_power", "type": "int32",  "scale": 1.0,  "unit": "W" },
        {
          "offset": 17, "name": "alarm_word", "type": "uint16", "scale": 1.0, "unit": "",
          "bits": [
            { "mask": "0x0001", "name": "fault_grid" },
            { "mask": "0x00F0", "name": "operating_state" }
          ]
        }
      ]
    }
  ]
}
```

## When to use `groups` vs flat `registers`

**Use a group** when the registers are contiguous (or close) on the device's Modbus map. One group = one Modbus transaction = ~10-100x faster than the equivalent flat reads. The compiled SolPlanet driver uses block reads of 18-23 registers; you can do the same in JSON.

**Use a flat `registers` entry** when:
- the register stands alone (no neighbors you also want),
- the register sits inside a range that has reserved/forbidden offsets you must skip, or
- you want each register's success/failure tracked independently.

You can mix both in the same file.

## Field definitions

### Top level

| Field | Required | Description |
|---|---|---|
| `device_id` | yes | Unique identifier (no spaces, alphanumeric + `_`) |
| `name` | yes | Display name in UI |
| `description` | no | Documentation only (firmware ignores) |
| `schema_version` | no (default 1) | `1` or `2`. Must be `2` to use `groups`, `bits`, or `nan_value`. |
| `registers` | no | Array of flat single-register reads |
| `groups` | no | Array of grouped block reads (schema 2 only) |

At least one of `registers` or `groups` must be non-empty.

### Flat register entry (`registers[*]`)

| Field | Required | Description |
|---|---|---|
| `addr` | yes | Register address; hex `"0x0010"` or decimal `"100"` |
| `name` | yes | Telemetry point name (becomes MQTT topic suffix) |
| `fc` | yes | Modbus function code: `3` (Read Holding) or `4` (Read Input) |
| `type` | yes | `"uint16"`, `"int16"`, `"uint32"`, or `"int32"` |
| `scale` | yes | Multiplier applied to raw value |
| `unit` | yes | `"V"`, `"A"`, `"W"`, `"kWh"`, `"%"`, `""`, etc. |
| `storage` | no | `true` = read by `readStorage()` (less frequent battery poll). Default `false`. |
| `nan_value` | no, schema 2 | Raw value treated as "no data" — suppress publish when matched. Hex string or integer. |
| `bits` | no, schema 2, `uint16` only | See bitfield decoding below. |
| `comment` | no | Documentation only |

### Group (`groups[*]`)

| Field | Required | Description |
|---|---|---|
| `name` | yes | Used in logs / diagnostics. Not published. |
| `fc` | yes | `3` or `4`. **All fields in a group share one FC** (Modbus does not allow mixing). |
| `start` | yes | Address of word 0 of the block |
| `count` | yes | Number of 16-bit words to read in one transaction. **Range 1..125.** |
| `storage` | no | Whole-group flag. Default `false`. |
| `fields` | yes | Non-empty array of `Field` definitions (see below) |

### Field within a group (`groups[*].fields[*]`)

Same shape as a flat register entry, **except** `addr` and `fc` are dropped (inherited from the group), and `offset` is added:

| Field | Required | Description |
|---|---|---|
| `offset` | yes | Word offset from `start`. Must satisfy `offset + word_size(type) <= count`. |
| `name`, `type`, `scale`, `unit` | yes | Same as flat |
| `nan_value` | no | Same as flat |
| `bits` | no, `uint16` only | See below |

Gaps in offsets are allowed (e.g. `count: 10` with fields at offsets 0, 1, 5, 8 — the firmware logs the unmapped count once at boot). Two fields cannot overlap the same word range.

### Bitfield decoding (`bits[]`)

When a `uint16` field has a non-empty `bits[]` array:
- the field's numeric value is **NOT** published,
- one telemetry point is emitted per named bit,
- `value = (raw & mask) >> shift` (single-bit masks publish 0/1; multi-bit masks publish the extracted integer),
- `shift` is auto-derived from the mask's trailing zeros if not specified (so `mask: "0x0001"` → shift 0; `mask: "0x00F0"` → shift 4 → values 0..15).
- `mask` must be in the range `0x0001..0xFFFE`. `0xFFFF` (entire word) is rejected — drop `bits[]` and publish the field directly if you want the raw word.

```json
{
  "offset": 0, "name": "alarm_word", "type": "uint16", "scale": 1.0, "unit": "",
  "bits": [
    { "mask": "0x0001", "name": "fault_grid" },
    { "mask": "0x0002", "name": "fault_pv" },
    { "mask": "0x00F0", "name": "operating_state" }
  ]
}
```

### NaN sentinel (`nan_value`)

Some devices return a sentinel like `0xFFFF` (uint16) or `0xFFFFFFFF` (uint32) when a register is not currently meaningful (e.g. battery SOC when no battery installed). Set `nan_value` to suppress publishing in that case — the telemetry point is simply omitted from the cycle.

Accepted forms:
- **Hex/decimal string**: `"nan_value": "0xFFFFFFFF"`, `"nan_value": "65535"`, or `"nan_value": "-1"`. Negative values are bit-reinterpreted (so `"-1"` matches wire `0xFFFF` for `int16` or `0xFFFFFFFF` for `int32`).
- **JSON integer**: `"nan_value": 65535`, `"nan_value": 4294967295`, or `"nan_value": -1` for signed types.
- Malformed strings (e.g. `"foobar"`), wrong types (boolean, null, object), and presence on schema-1 docs are all rejected at validation time with a clear error.

## Failure semantics

**Per-flat-register failure** is independent: a single failed flat register is silently skipped, others publish. (Unchanged from schema 1.)

**Per-group failure** is all-or-nothing **for that one group**: if the group's Modbus transaction fails (after the client's 3-attempt retry), all of its fields are skipped this cycle. **Other groups and flat registers in the same poll still publish normally.** A failure log line is emitted per failed group.

If you put 20 registers in a group and one of them is forbidden by the device, the group's read returns Modbus exception 0x02 and the entire group is dropped that cycle. **Always verify each register address with the diagnostics page (Manual Register Probe) before bundling them into a group.** When debugging a group that fails persistently, split it in half until you find the offending register.

## Per-poll time budget

To prevent one flaky read from blocking the entire poll cycle on retry-backoff, **all** Modbus reads in a single `readBasic` (or `readStorage`) call share a cumulative time budget of `min(poll_interval / 2, 5 seconds)`, floor 500 ms. Once exceeded, remaining reads (flat or group) are skipped this cycle, logged once, and retried on the next cycle. Both flat-register and group reads are subject to the budget.

## Limitations

- **Single device per ESP32** — one Modbus device at a time.
- **No string/text registers** — only numeric (`uint16`, `int16`, `uint32`, `int32`).
- **32-bit byte order** is fixed: big-endian high-word-first. Little-endian word-order devices are not supported in this version.
- **Group `count` ≤ 125** (Modbus spec). Validation rejects larger.
- **Bitfields only on `uint16`.**
- **Total field count** practical ceiling ~150, depending on poll interval and Modbus timeout.
- **Function codes**: only FC 0x03 (Read Holding) and FC 0x04 (Read Input). No coils, no writes.
- **No computed values** — derive `power = V × A` in Home Assistant templates.
- **JSON file size cap: 16 KB.** Larger files are rejected at upload and at load time.
- **`description` and `comment` fields** are firmware-ignored documentation.

## Workflow

### 1. Create JSON configuration

Use `docs/example_device.json` as a template.

### 2. Upload

1. Config page → enable "Enable Diagnostic Tools" → save
2. Diagnostics page → Custom Device JSON Manager
3. Choose file → Upload JSON

The upload endpoint validates the JSON before writing — schema, registers, groups, offsets, bitfields, name uniqueness, and size are all checked. Errors appear in the response, not just the log.

### 3. Select device

Config page → Device Model dropdown → your device shows up as `"<name> (Custom)"` → save → device reboots.

### 4. Verify

Status page should show telemetry. If not:
- Diagnostics → Manual Register Probe to verify individual registers exist.
- Diagnostics → Modbus Frame Log to inspect bus traffic.
- If a group fails: split it in half repeatedly to isolate the bad register.

## MQTT topics

```
{prefix}/{device_type}/telemetry/{name}     value (JSON or raw, per mqtt_json_format config)
{prefix}/{device_type}/status/availability  "online" / "offline"
{prefix}/{device_type}/status/poll_status   JSON poll statistics
```

For custom devices `{device_type}` is `custom`. The telemetry topic uses the `name` field exactly — bitfield bit names and NaN-suppressed fields follow the same rule. **Names must be unique across all flat registers, groups, and bitfields.** The validator rejects collisions.

## Best practices

1. **Verify every register with the diagnostics probe before grouping.** Group reads are all-or-nothing.
2. **Group only contiguous ranges you trust.** Don't grow a group past the first reserved/forbidden register.
3. **Use `nan_value` for sentinels.** A register that intermittently returns `0xFFFF` will produce a "65535" entity in HA without it.
4. **Use `bits[]` for status/fault registers.** A raw uint16 of `0x0031` is useless; `fault_grid=1, operating_state=3` is what you actually want in HA.
5. **Use `storage: true` only for slow-changing battery / BMS data.** Reduces Modbus traffic.
6. **Test scaling factors.** SOC/SOH usually return 0-100 directly (`scale: 1.0`), not 0-10000 (`scale: 0.01`). Compare raw bytes vs expected real-world value.

## Need help?

1. Check Modbus frame log in diagnostics.
2. Verify register addresses against device datasheet.
3. Test with the manufacturer's Modbus tool first, if available.
4. File an issue with frame log output and device model.
