# ESPHome components for TI battery-management ICs

ESPHome external components for two Texas Instruments parts that have no upstream
ESPHome support:

| Component | Part | I2C address | Role |
|---|---|---|---|
| `bq25798` | BQ25798 | `0x6B` (fixed) | Buck-boost charger, dual input, on-chip MPPT |
| `bq34z100` | BQ34Z100-G1 | `0x55` (fixed) | Impedance Track fuel gauge |

Neither component is board-specific. They were written for the
[SunSprout hub](https://github.com/JohnNeville/SunSprout), but they talk to the chips,
not to that board.

## Use

```yaml
external_components:
  - source: github://JohnNeville/esphome-bq-drivers
    components: [bq25798, bq34z100]
```

See [`tests/test-bq25798.yaml`](tests/test-bq25798.yaml) and
[`tests/test-bq34z100.yaml`](tests/test-bq34z100.yaml) for configurations that exercise
every option.

## `bq25798`

A top-level component. Every entity is optional; configure only the ones you want.

```yaml
bq25798:
  i2c_id: bus_internal
  update_interval: 15s
  chemistry: lifepo4         # sets the charge voltage limit; see below
  cell_count: 1
  battery_capacity: 3000     # mAh, sets the termination current to C/10
  max_charge_current: 2000   # mA, hard ceiling on every charge-current write
  vbat_voltage:
    name: "Battery Voltage"
  enable_mppt:
    name: "Enable MPPT"
```

### Things worth knowing

**Declare the chemistry; do not expose the charge voltage as a control.** The charger
reads its `PROG` resistor at power-on and comes up configured for a **4.2 V** cell. That is
correct for lithium-ion and destructive for LiFePO4. `chemistry` is how the limit gets set:
the component derives the charge voltage from the chemistry and the cell count, writes it
during setup, and rewrites it after any register reset.

| `chemistry` | Volts per cell |
|---|---|
| `lifepo4` | 3.65 |
| `li_ion` | 4.20 |
| `li_ion_4_35` | 4.35 |
| `li_ion_4_40` | 4.40 |
| `custom` | whatever `cell_voltage` says |

`chemistry` and the `charge_voltage` number entity are mutually exclusive, and one of them
is required. A limit that protects the cell should not be something anyone can drag in Home
Assistant, so exposing the entity means opting out of the derived value entirely.

**The watchdog resets your settings.** When the I2C watchdog expires, the charger reverts
the charge current, termination current, `EN_CHG` and most timer settings to the defaults
read from the `PROG` pin at power-on. The component disables the watchdog by default. Set
`watchdog: 40s` (or any of `0.5s`, `1s`, `2s`, `20s`, `80s`, `160s`) to keep it, in which
case the component kicks it on every `update()` and reapplies its configuration if it ever
does expire.

**`max_charge_current` is a real safety control.** The IC will happily source 5 A. In buck
mode the charge current can exceed the input current, so a board whose `SYS` and `BAT`
copper is rated below that must set this, not just cap the number entity. The clamp is
applied inside every write, including the one restored at boot.

**Ship mode needs a ship FET.** `ship_mode`, `shutdown` and `power_cycle` drive
`SDRV_CTRL`, which the IC locks at `00` unless `SFET_PRESENT` is set — and setting that on
a board with no external FET on `SDRV` just creates controls that silently do nothing. Set
`ship_fet_present: true` only if the FET is actually populated; the component rejects those
three controls without it rather than let them no-op.

With a ship FET fitted, two more options open up. `ship_fet_action_delay: false` removes the
charger's default ~10 s wait before it acts on `SDRV_CTRL`, which matters if `power_cycle`
is being used as a reboot button. `battery_ocp: true` sets `EN_BATOC`, opening the ship FET
above the IC's **fixed ~9.3 A** `IBAT_OCP` threshold — a dead-short backstop, not copper
protection; `max_charge_current` is what guards the copper.

**MPPT is exclusive and self-clearing.** `EN_MPPT`, `EN_ICO` and `FORCE_VINDPM_DET` are
mutually exclusive; enabling MPPT clears the other two. The charger also clears `EN_MPPT`
itself whenever VBUS drops below the present threshold, so the component re-asserts it on
each poll rather than assuming it stuck.

**Battery temperature needs the divider described.** The `TS` ADC reports a ratio of
`REGN`, not a temperature. Supply `ts_resistor_upper` and `ts_resistor_lower` (and
`ts_nominal_resistance` / `ts_beta` if your NTC is not a 10 kΩ 3435 part) for
`battery_temperature` to mean anything.

## `bq34z100`

A `sensor` platform, because that is where the bulk of its entities live.

```yaml
sensor:
  - platform: bq34z100
    i2c_id: bus_internal
    update_interval: 30s
    state_of_charge:
      name: "Battery Level"
    voltage:
      name: "Battery Voltage"
```

### Data flash configuration

The gauge stores its pack parameters in data flash. Writing them is opt-in:

```yaml
    apply_configuration: true
    design_capacity: 6000      # mAh
    design_energy: 19200       # mWh
    cell_count: 1
    sense_resistor: 10.0       # mΩ, sets CC Gain and CC Delta
    voltage_divider: 5000      # mV full scale; also selects VOLTSEL
    lifepo4_relax: true
```

Data flash has limited write endurance, so each value is read first and written only if it
differs, then read back to confirm it stuck. `apply_configuration` defaults to `false`, so
you can declare the right values in a board package and flip the switch once when a cell is
attached; with it off, the component logs a warning at boot saying the values are declared
but the flash is untouched.

`sense_resistor` is converted to the `CC Gain` and `CC Delta` calibration constants, which
are stored in TI's proprietary 4-byte float format rather than IEEE 754. The component
decodes what the gauge actually holds after writing and logs it, so a conversion problem
shows up in the log rather than as a quietly wrong current reading.

### What this cannot do

**It cannot program the battery chemistry.** `CHEM_ID` (Control subcommand `0x0008`) only
*reports* the chemistry ID. TI provides no documented I2C path to load a chemistry table —
that takes bqStudio and an EV2400 with a `.bqz` chemistry file, once, at the bench. A gauge
running the wrong chemistry reports a wrong state of charge no matter what else is set.

Set `expected_chem_id` and the component will read the gauge's ID at boot and log an error
if it does not match, so at least the problem is visible. LiFePO4 chemistries are in the
400 series; `lifepo4_relax` sets the `LFPRelax` and `DoDWT` bits in Pack Configuration B,
which only do anything once a 400-series chemistry is loaded.

## Sources

Register maps, scaling and data flash offsets are taken from the TI datasheets:
BQ25798 SLUSDV2C and BQ34Z100 SLUSAU1C. Per-register notes are in
[`docs/`](docs/).

## Licence

MIT, the usual choice for ESPHome external components. The SunSprout hardware project
these were written for uses CERN-OHL-S-2.0, but that licence is written for hardware
designs, not firmware.
