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

**Changing the battery at run time.** A device whose owner picks the cell after the firmware
is built (a factory image, say) can call `bq25798.configure_battery` instead of baking the
chemistry in. All three options are optional and templatable:

```yaml
- bq25798.configure_battery:
    id: charger
    chemistry: li_ion          # lifepo4, li_ion, li_ion_4_35 or li_ion_4_40; not custom
    battery_capacity: 2000     # mAh, sets the C/10 termination current
    charge_current: 500        # mA, the default restored after a reset
    ts_ignore: false           # true sets TS_IGNORE: see below
```

The chemistry still sets the charge voltage limit and nothing else does: the action takes a
name from the table above, never a voltage, and an unknown name changes nothing and logs an
error. The new values are kept, so a register reset or a watchdog expiry reapplies them
rather than the YAML ones.

`ts_ignore` sets or clears the charger's `TS_IGNORE` bit at run time, so a device can let its
owner say whether a thermistor is fitted. Setting it turns off the charger's battery-temperature
protection (JEITA charging limits and the hot/cold cut-offs); with an open TS divider and the bit
clear, the charger reads "too cold" and does not charge. Keep `chemistry:` in the YAML as the value used until the action
first runs, and choose one that cannot overcharge any cell you ship for (`lifepo4`, at 3.65 V
per cell, is safe for a lithium-ion cell too; the reverse is not).

**The watchdog resets your settings.** When the I2C watchdog expires, the charger reverts
the charge current, termination current, `EN_CHG` and most timer settings to the defaults
read from the `PROG` pin at power-on. The component disables the watchdog by default. Set
`watchdog: 40s` (or any of `0.5s`, `1s`, `2s`, `20s`, `80s`, `160s`) to keep it, in which
case the component kicks it on every `update()` and reapplies its configuration if it ever
does expire.

**Register check (a diagnostic).** Every 5 s the component reads registers `0x00` to `0x30` in one 49-byte
transaction (about 9.5 ms on a 50 kHz bus) and compares the control bits with what the host last
asked for. The charger changes some bits itself (`EN_HIZ` after a failed poor-source test,
`EN_MPPT` when VBUS drops, the ADC enable on entering HIZ) and none of that raises `/INT`, so this is
how to see it. A board does not need the check to run: MPPT is re-asserted by the normal update, and the
minimum system voltage limit below applies whatever `register_check` is set to. The check costs an I2C read,
some formatting and a log line, so a shipping image should set `register_check: off` and a bench image
should turn it on. `register_check:` chooses what happens on a difference:

| Value | Behaviour |
|---|---|
| `watch` (default) | Logs the difference (at most once a minute) and counts it. Writes nothing. |
| `enforce` | Also writes the value back and reads it to confirm, and clears `EN_HIZ` at most every 30 s. For bench experiments, not recommended: a roof board that never used it started charging at dawn by itself. |
| `off` | No check, and no timer. Nothing below is updated. |

`register_check` can also be changed while running with `set_register_check(0, 1 or 2)` from a lambda (the SunSprout
debug package does this from a select). The optional `register_corrections:` sensor counts the checks (one every 5 s) that found a difference since
boot, in `watch` mode too. A difference that lasts counts on every pass, so the figure is a measure of how long
the charger and the host disagreed, not how many separate events there were. `last_correction:` names the register.

Which fields are checked, and why, is in
[`docs/bq25798-register-behaviour.md`](docs/bq25798-register-behaviour.md). Use `watch` first: if
your board never reports a difference there is nothing for `enforce` to do.

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

**No thermistor fitted means no charging, unless you say `ts_ignore: true`.** The charger
qualifies charging against the `TS` pin, and an unpopulated NTC leaves the divider sitting
well above every JEITA cold threshold — the charger reads "far too cold" and suspends
charging. `ts_ignore: true` sets `TS_IGNORE` (`REG18[0]`), which tells the charger to treat
`TS` as always good:

```yaml
bq25798:
  ts_ignore: true            # charge without temperature qualification
```

This gives up temperature protection entirely, so it is only honest on a board that
genuinely has no thermistor. The bit is cleared by a register reset and by a watchdog
expiry; the component rewrites it in both cases. It is independent of the divider options
above — those only convert the `TS` reading into a temperature, and with `ts_ignore` on and
no NTC fitted the `battery_temperature` sensor will read implausibly cold, because it is
solving for a thermistor that is not there.

**Turning off the charger's D+/D- detection.** On every VBUS plug-in the charger runs its own
BC1.2 and non-standard-adapter detection on its `D+`/`D-` pins and uses the result to set the
input current limit. If those pins are not connected, or share a native USB port that the
detection signalling would disturb, switch it off with `auto_input_detection: false`, which
clears `AUTO_INDET_EN` (`REG11[6]`):

```yaml
bq25798:
  auto_input_detection: false   # default true, the chip's power-on state
```

This only affects plug-ins after the firmware starts; a detection that already ran at power-up
is not undone. The bit is cleared by a register reset and by a watchdog expiry, and the
component rewrites it in both cases.

**The ADC is switched off before the host sleeps.** The component keeps the ADC running
(`ADC_EN`, continuous) while the board is awake, which is what feeds the voltage, current and
temperature sensors. Left running through deep sleep it is the largest quiescent load the
charger adds: the datasheet gives 17 µA typical with the ADC disabled and 540 µA with it enabled
(battery only, `VBAT` = 8 V, §6.5), and with the `TS` channel enabled and `VBAT` above 3.2 V the
`REGN` LDO also runs in battery-only mode (§7.3.4.1) and biases the `TS` divider. In the shutdown
hook, which runs before deep sleep, a restart or an OTA update, the component writes `ADC_EN` = 0
with one-shot selected, the stop the datasheet recommends (§7.3.10). The next boot's setup turns
the ADC back on, so nothing changes while the board is awake. It is on by default:

```yaml
bq25798:
  adc_off_before_sleep: true    # default; false leaves the ADC running through sleep
  adc_off_in_sleep:             # optional switch: change it at run time
    name: "ADC Off Before Sleep"
    disabled_by_default: true
```

The switch is restored across reboots and deep sleep, and starts on. With the switch declared it
is the control: to keep the ADC running in sleep, set `restore_mode: ALWAYS_OFF` on the switch.
`adc_off_before_sleep: false` together with the switch is rejected, because the switch would
override it at boot. The sensors stop updating while the ADC is off, which only matters if
something reads them during the shutdown. Measured on one
hub (no thermistor, gauge enabled, 3.7 V supply, nothing on the user rail), the settled sleep
current fell from 1.06 mA to 0.47 mA. That is one board and one set-up, not a figure for every build.

### Choosing which input powers the board

`input_selection` adds a drop-down that decides which of the two input ports may power the
board, and `active_input` reports which one actually does:

```yaml
bq25798:
  input_selection:
    name: "Input Selection"
  active_input:
    name: "Active Input"
```

| Option | USB (`ACDRV1`) | Solar / DC (`ACDRV2`) |
|---|---|---|
| Auto | allowed | allowed |
| Force USB | allowed | off |
| Force Solar | off | allowed |
| Off | both input drivers forced off | |

- **Why not the per-port switches?** `EN_ACDRV1` and `EN_ACDRV2` only decide whether a driver may
  turn on. Writing 0 does not open an input that is already placed. The datasheet's way to
  open one is `DIS_ACDRV`, so a change that has to open a placed input sets `DIS_ACDRV`, waits for
  `VBUS` to fall below the present threshold (15 s at most), clears `DIS_ACDRV` again and then
  writes the enable bits (the charger forces them to 0 while `DIS_ACDRV` is set). Choosing a mode that keeps the port the board is already running
  from only writes the enable bits, so the supply is not interrupted.
- **Auto** lets the charger decide, as it does by itself: it turns on the path where a valid input
  appears first, port 1 (USB) wins if both appear together, and it swaps by itself if the active one
  fails. Setting both enable bits at once is ignored by the charger, so Auto never does that. If an
  input is already working it is left alone; from a cold start (after Off, say) only the primary
  valid port is enabled.
- **Off** is the same as setting `DIS_ACDRV` by hand: the board runs from the battery and does
  not charge. With no battery fitted it browns out.
- **Force USB and Force Solar are preferences, not locks.** The charger sets `EN_ACDRV1` itself when
  a valid input appears at `VAC1`, and no register makes it reject one. With USB plugged in,
  Force Solar cannot hold: the charger turns USB back on. The enable bits show what the charger
  decided, not what you asked for (once an input is picked it clears the other port's bit, so Auto
  and Force USB read the same). The drop-down therefore keeps showing your choice, and falls back to
  Auto, with a warning in the log such as `Input selection was set to 'Force Solar' but the charger
  overrode it; showing 'Auto'`, only when the charger's behaviour contradicts it. Nothing re-applies
  your choice. Off is reliable, because `DIS_ACDRV` forces both drivers off.
- `active_input` shows `USB`, `Solar` or `None`, worked out from which input has voltage on its
  `VAC` pin, whether its enable bit is set and whether `VBUS` is up. When both ports could be
  feeding the board it reads `USB or Solar`, because the charger does not say which one it chose.
  (`ACRB1_STAT` and `ACRB2_STAT` only report that each FET pair is fitted, not that it conducts.)
- The selection is not remembered across a restart. The charger starts in Auto.

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

### Changing the pack at run time

`bq34z100.configure_pack` sets the design capacity, design energy and `lifepo4_relax` after
boot and writes them to data flash straight away, whatever `apply_configuration` says; calling
the action is the opt-in. All three options are optional and templatable:

```yaml
- bq34z100.configure_pack:
    id: fuel_gauge
    design_capacity: 2000     # mAh
    design_energy: 7400       # mWh, at most 32767
    lifepo4_relax: false      # true sets LFPRelax and DoDWT, false clears them
    external_thermistor: false  # false: use the gauge's own temperature sensor
```

Each value is read first and written only if it differs, so calling it on every boot does
not wear the flash.

`external_thermistor` chooses the gauge's temperature source at run time. With `true` it reads the
thermistor on its TS pin; with `false` it uses its own sensor, which is the right choice when
nothing is fitted, because an open TS input reads about -40 C and the gauge then reports
"Charge inhibited". The `lifepo4_relax` build option only ever sets the bits; passing `false` to
this action also clears them, for a switch from a LiFePO4 to a lithium-ion cell. Like the
build option it does nothing to the chemistry table, which still needs bqStudio (see
[Selecting the battery chemistry](#selecting-the-battery-chemistry)).

### Setup and fault diagnostics

These entities exist so a dashboard, or a companion such as a Home Assistant integration, can
tell a misconfigured or unhappy board from a healthy one without reading logs. All are optional
and diagnostic.

`bq25798`:

```yaml
    poor_source:
      name: "Poor Source Detected"
    watchdog_expired:
      name: "Watchdog Expired"
    ts_status:
      name: "TS Status"
    vsys_regulation:
      name: "In VSYSMIN Regulation"
    last_correction:
      name: "Last Register Correction"
```

- `poor_source` is on when the charger's poor-source test has failed (REG0x22 POORSRC_FLAG). The
  hardware clears that flag when it is read, so the entity stays on for two minutes after the
  charger last raised it. A weak panel at dawn shows up here while `charger_fault` still says OK.
- `watchdog_expired` works the same way for the watchdog flag.
- `vsys_regulation` is on while the battery is below `VSYSMIN` (REG0x1E `VSYS_STAT`). The charger cannot run
  MPPT then, and a LiFePO4 pack sits below the default 3.5 V `VSYSMIN` for most of its range.
- `last_correction` says which register the periodic check last found changed, what it read, what was wanted
  and whether the rewrite took, with a count per register since boot, for example
  `EN_MPPT x7: REG0x15&0x01 read 0x00, wanted 0x01, write did not take`. It works in `watch` mode too, where
  nothing is rewritten. `register_corrections` counts all of them together.
  `unexpected_corrections` is the same count without EN_MPPT and EN_HIZ, which the charger flips by itself at dawn and
  dusk (and EN_HIZ after a failed poor-source test), so a rise in it is worth a look and a rise in the other is not.
- `ts_status` is `Normal`, `Cool`, `Cold`, `Warm` or `Hot`, from REG0x1F. It reads `Ignored` when
  `ts_ignore` is set, because the charger no longer acts on the pin then. `Cold` with a thermistor
  that is not fitted is the usual reason charging refuses to start.

`bq34z100`:

```yaml
    config_status:
      name: "Gauge Setup Status"
    config_problem:
      name: "Gauge Setup Problem"
```

- `config_problem` is on when the gauge does not match what the YAML declares, and `config_status`
  says why: pack parameters declared with `apply_configuration` off, the gauge's design capacity
  differing from the declared one, or a chemistry ID other than `expected_chem_id`. It uses reads
  that need no unseal, so it never changes the gauge. The state is worked out at boot and again
  after each write.

### Alert pin

The gauge ships with its Alert Configuration register at 0, so the `ALERT` pin never goes
low whatever the flags say. `alert_configuration` picks which `Flags()` bits are ORed onto the
pin (datasheet Table 7-19). It is written with the rest of the data flash, so it needs
`apply_configuration: true` too:

```yaml
    apply_configuration: true
    alert_configuration: [low_charge, critical_charge]
```

The names are `over_temperature_charge`, `over_temperature_discharge`, `battery_high`,
`battery_low`, `charge_inhibited`, `fully_charged`, `charging_allowed`, `ocv_taken`,
`internal_short`, `tab_disconnect`, `low_charge`, `critical_charge` and `discharging`. (The `discharging` entity is also
on when the measured current is 10 mA or more out of the pack, because the gauge's own flag only sets above its discharge
threshold, 100 mA by default.) An empty
list writes 0 and turns every alert off. Reading `Flags()` is what `clear_alert` does, and the
component polls it, so the pin may only stay low until the next poll.

`sense_resistor` is converted to the `CC Gain` and `CC Delta` calibration constants, which
are stored in TI's proprietary 4-byte float format rather than IEEE 754. The component
decodes what the gauge actually holds after writing and logs it, so a conversion problem
shows up in the log rather than as a quietly wrong current reading.

### Selecting the battery chemistry

**There is no I2C command that sets the chemistry.** `CHEM_ID` (Control subcommand `0x0008`)
only *reports* the active chemistry ID, and the one selection path TI documents is the
BQChem feature in bqStudio (datasheet §8.1.2.1.6). A gauge running the wrong chemistry
reports a wrong state of charge no matter what else is set.

That does not make it unreachable from I2C. The chemistry is ordinary data flash, and
§7.2.3.1 says data flash is accessible "by use of the BQ34Z100 evaluation software **or by
data flash block transfers**" — the same protocol this component already uses for capacity
and calibration. The datasheet describes capturing the result as a Golden Image File that
"can then be written to multiple battery packs".

So bqStudio is needed **once, to obtain the table contents** — which the datasheet does not
publish — rather than once per board. With the bytes in hand, writing them over I2C is the
flash-stream path this component implements. Programming a chemistry from a declared ID is
not implemented here, because the tables are not public.

### Capturing a golden image

`dump_data_flash` adds a button that unseals the gauge, reads every data flash subclass
documented in Table 7-8, and logs each 32-byte block as

```
DF,<subclass>,<block>,<64 hex characters>
```

Press it on a gauge that bqStudio has already programmed and the log holds a complete
record of that gauge's configuration — chemistry included. That is the practical route to
programming a second board without bqStudio: capture once, replay the blocks over I2C.

It is also a field diagnostic. Comparing a dump against a known-good one catches a gauge
whose data flash has drifted, which `chem_id` alone will not show.

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
