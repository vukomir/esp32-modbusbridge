# Home Assistant Integration

Home Assistant configuration for the SolPlanet Inverter MQTT bridge. Package
files cover everything the firmware publishes under `home/inverter/...`:

| File | Purpose |
|------|---------|
| `packages/solplanet_inverter.yaml` | Primary MQTT sensors + binary sensors. Energy, power, battery state, faults. Wires into HA Energy Dashboard. |
| `packages/solplanet_diagnostics.yaml` | **Three-phase variant.** Per-string PV, per-phase grid AC (L1/L2/L3), per-phase EPS, line-to-line voltages, temperatures, runtime/grid metadata. |
| `packages/solplanet_diagnostics_1ph.yaml` | **Single-phase variant.** Same as above with L2/L3 and line-to-line voltages stripped. |
| `packages/solplanet_templates.yaml` | Derived sensors: estimated house consumption, solar self-consumption, battery direction, status helpers. |
| `packages/solplanet_integrations.yaml` | Riemann-sum integrations for grid import/export kWh, used because the firmware grid-flow counters are non-incrementing on this Solplanet model. |

Pick **either** `solplanet_diagnostics.yaml` (3ph) **or** `solplanet_diagnostics_1ph.yaml` (1ph) — never both.
Determine which by reading register 31001 of your inverter (firmware reports `'1'` for single-phase, `'3'` for three-phase) or by checking the model number.

The DDS238 grid meter is intentionally **not** modelled here. The inverter is
the sole source of truth for grid, solar, and battery telemetry.

---

## Install

1. **Enable packages** in `configuration.yaml`. The `packages:` key MUST be
   nested under `homeassistant:` — placing it at the root causes
   `Integration 'packages' not found`.

   ```yaml
   homeassistant:
     # ...your existing options (name, latitude, unit_system, etc.) stay here...
     packages: !include_dir_named packages
   ```

   If you don't already have a `homeassistant:` block, add the whole snippet.
   Indentation is YAML-significant — `packages:` is two spaces in.

2. **Copy** the package files from this repo into your HA config directory.
   For three-phase inverters:

   ```
   /config/packages/solplanet_inverter.yaml
   /config/packages/solplanet_diagnostics.yaml
   /config/packages/solplanet_templates.yaml
   /config/packages/solplanet_integrations.yaml
   ```

   For single-phase inverters use the `_1ph` diagnostics file instead:

   ```
   /config/packages/solplanet_inverter.yaml
   /config/packages/solplanet_diagnostics_1ph.yaml
   /config/packages/solplanet_templates.yaml
   /config/packages/solplanet_integrations.yaml
   ```

3. **Validate** (Settings → Developer Tools → YAML → Check Configuration).
4. **Restart** HA.

After restart you'll have a single device named **SolPlanet Inverter** with
~80 entities. Diagnostic entities are tagged `entity_category: diagnostic`
and hidden from the default device card unless you click "Show diagnostics".

---

## Entity ID convention

HA derives MQTT entity IDs as `<device_name_slug>_<entity_name_slug>`. With
`device.name = "SolPlanet Inverter"`, the resulting IDs are:

```
sensor.solplanet_inverter_<entity_slug>
binary_sensor.solplanet_inverter_<entity_slug>
```

Templates in `solplanet_templates.yaml` reference that pattern. **Verify after
first install** — go to Settings → Devices → SolPlanet Inverter and confirm
the entity IDs match. If your HA version doesn't prefix the device name (older
HA), you'll need to find/replace `solplanet_inverter_` with the actual prefix
in the templates file.

---

## HA Energy Dashboard wiring

Settings → Dashboards → Energy.

### Solar production
- **Solar production**: `sensor.solplanet_inverter_solar_energy_total`
  - Cumulative life-of-inverter kWh. Direct match.

### Grid
- **Grid consumption**: `sensor.solplanet_inverter_grid_consumption_today`
  - Daily counter. Wired with `state_class: total` + `last_reset_value_template`
    so HA correctly accumulates across midnight.
- **Return to grid**: `sensor.solplanet_inverter_grid_export_total`
  - Cumulative life-of-inverter kWh. Direct match.

> ⚠️ **Asymmetry note.** Grid consumption uses a daily counter because the
> firmware only publishes `consumption_today` (no cumulative `consumption_total`).
> Return to grid uses cumulative because `grid_export_total` is published.
> The two will look correct day-to-day, but if your inverter's day boundary
> drifts vs. HA's local midnight you'll see boundary glitches in Grid Consumption.

### Battery storage
- **Energy going in to the battery**: `sensor.solplanet_inverter_battery_charge_today`
- **Energy coming out of the battery**: `sensor.solplanet_inverter_battery_discharge_today`
  - Both are daily-counter sensors using `state_class: total` + `last_reset`.
    Same drift caveat as Grid Consumption.

### Optional: Individual devices
Not relevant — this integration covers a single inverter.

---

## Recommended Lovelace cards

A minimal energy overview dashboard:

```yaml
type: vertical-stack
cards:
  - type: gauge
    entity: sensor.solplanet_inverter_battery_soc
    name: Battery SOC
    min: 0
    max: 100
    severity:
      green: 50
      yellow: 20
      red: 0

  - type: glance
    entities:
      - entity: sensor.solplanet_inverter_ac_active_power
        name: Solar
      - entity: sensor.solplanet_inverter_battery_power
        name: Battery
      - entity: sensor.solplanet_house_consumption
        name: House
      - entity: sensor.solplanet_solar_status
        name: Status

  - type: entities
    entities:
      - sensor.solplanet_inverter_solar_energy_today
      - sensor.solplanet_inverter_solar_energy_total
      - sensor.solplanet_inverter_grid_export_today
      - sensor.solplanet_inverter_grid_consumption_today
      - sensor.solplanet_inverter_battery_charge_today
      - sensor.solplanet_inverter_battery_discharge_today
```

For the official Energy Distribution card use HA's built-in
`type: energy-distribution` (no entity config needed once Energy Dashboard is set up).

---

## Topic → entity mapping

All topics under `home/inverter/...`. Entity IDs assume HA's
`<device_name>_<entity_name>` slug pattern.

### Direct HA Energy inputs
| Topic | Entity | State class |
|-------|--------|-------------|
| `telemetry/energy_total` | `sensor.solplanet_inverter_solar_energy_total` | `total_increasing` |
| `telemetry/grid_export_total` | `sensor.solplanet_inverter_grid_export_total` | `total_increasing` |
| `telemetry/consumption_today` | `sensor.solplanet_inverter_grid_consumption_today` | `total` (last_reset) |
| `telemetry/battery_charge_today` | `sensor.solplanet_inverter_battery_charge_today` | `total` (last_reset) |
| `telemetry/battery_discharge_today` | `sensor.solplanet_inverter_battery_discharge_today` | `total` (last_reset) |

### Other energy
| Topic | Entity |
|-------|--------|
| `telemetry/energy_today` | `sensor.solplanet_inverter_solar_energy_today` |
| `telemetry/grid_export_today` | `sensor.solplanet_inverter_grid_export_today` |
| `telemetry/pv_energy_today` | `sensor.solplanet_inverter_pv_energy_today` |
| `telemetry/pv_energy_total` | `sensor.solplanet_inverter_pv_energy_total` |
| `telemetry/eps_consumption_today` | `sensor.solplanet_inverter_eps_consumption_today` |
| `telemetry/eps_consumption_total` | `sensor.solplanet_inverter_eps_consumption_total` |
| `telemetry/generation_today_ac` | `sensor.solplanet_inverter_ac_generation_today` |
| `telemetry/generator_energy_today` | `sensor.solplanet_inverter_generator_energy_today` |

### Power / state
| Topic | Entity |
|-------|--------|
| `telemetry/active_power` | `sensor.solplanet_inverter_ac_active_power` |
| `telemetry/apparent_power` | `sensor.solplanet_inverter_ac_apparent_power` |
| `telemetry/reactive_power` | `sensor.solplanet_inverter_ac_reactive_power` |
| `telemetry/grid_frequency` | `sensor.solplanet_inverter_ac_frequency` |
| `telemetry/power_factor` | `sensor.solplanet_inverter_ac_power_factor` |
| `telemetry/pv_total_power` | `sensor.solplanet_inverter_pv_total_power` |
| `telemetry/eps_active_power` | `sensor.solplanet_inverter_eps_active_power` |
| `telemetry/battery_power` | `sensor.solplanet_inverter_battery_power` |
| `telemetry/battery_soc` | `sensor.solplanet_inverter_battery_soc` |
| `telemetry/battery_soh` | `sensor.solplanet_inverter_battery_soh` |
| `telemetry/battery_status_text` | `sensor.solplanet_inverter_battery_status` |
| `telemetry/device_state_text` | `sensor.solplanet_inverter_state` |
| `telemetry/grid_connection` | `sensor.solplanet_inverter_grid_connection` |
| `status/availability` | `binary_sensor.solplanet_inverter_online` |
| `hardware/max485_status` | `binary_sensor.solplanet_inverter_max485_connected` |

### Diagnostics (full list)
See `solplanet_diagnostics.yaml`. Per-string PV, per-phase grid AC, per-phase
EPS, line-to-line voltages, temperatures, modbus address, grid code,
operating hours, battery cycles.

---

## Templates available

From `solplanet_templates.yaml`:

| Entity | What it is |
|--------|-----------|
| `sensor.solplanet_house_consumption` | Estimated whole-house W. **Heuristic.** |
| `sensor.solplanet_solar_self_consumption` | Solar W consumed locally (not exported). |
| `sensor.solplanet_solar_self_consumption_pct` | % of solar production used locally. |
| `sensor.solplanet_solar_status` | Producing / Low Production / Idle / Standby / Fault. |
| `sensor.solplanet_battery_direction` | Charging / Discharging / Idle. |
| `sensor.solplanet_battery_charge_power` / `_discharge_power` | Always-positive directional power. |
| `binary_sensor.solplanet_solar_producing` | True when AC active power > 10 W. |
| `binary_sensor.solplanet_solar_has_fault` | True when device state is `Fault`. |
| `binary_sensor.solplanet_battery_low` | True when SOC < 20 %. |
| `binary_sensor.solplanet_inverter_high_temperature` | True when internal temp > 70 °C. |
| `binary_sensor.solplanet_inverter_has_warning` | True when warning code > 0. |

---

## Known limitations

1. **No cumulative grid-import sensor.** Firmware only publishes
   `consumption_today`. The daily-counter + `last_reset` workaround is correct
   if and only if the inverter resets at the same boundary HA's `today_at('00:00')`
   does. Time-zone or NTP drift on the inverter will misreport at the boundary.
2. **No cumulative battery in/out sensors.** Same workaround applies; same caveat.
3. **`solplanet_house_consumption` is an estimate, not a measurement.** Without a clamp
   meter the firmware cannot directly measure house demand. The template uses
   `ac_active_power + eps_active_power` which is correct only when the inverter
   serves all loads; in islanded EPS mode the math may double-count.
4. **`battery_comm_status` is intentionally not exposed.** The published value
   `"a"` looks like a single byte stringified — likely a firmware bug. Surface
   it once the firmware is fixed.
5. **Renaming the device in HA breaks templates.** The templates reference
   entity IDs derived from the device name slug. Don't rename the device after
   first install — or update template entity references if you do.
6. **`unique_id` collision after migration.** If you previously installed any
   loose Solplanet/meter YAML files, delete those entities from the MQTT
   integration UI before the new package loads, otherwise you'll get
   duplicates. See "Migration from a previous setup" below.

---

## Migration from a previous setup

If you previously installed loose YAML files (single-device MQTT sensor lists,
DDS238 meter integration, etc.):

1. **Remove old `!include` lines** from `configuration.yaml` that point at
   any of the previous Solplanet/meter YAMLs.
2. **Delete stale entities** under Settings → Devices → MQTT — the old
   "SolPlanet Solar Inverter" device and any orphaned `Solar *` / "Grid *" /
   `Battery *` entities that show `Unavailable`.
3. **Restart HA.** Entities will re-register from the new package files
   under a single device named "SolPlanet Inverter."
4. If your Energy Dashboard was configured against legacy entity IDs,
   re-pick the entities under Settings → Energy.
