# BQ25798 register behaviour: who changes what, and what the driver does about it

Extracted from TI datasheet **SLUSDV2C** (June 2026), section 7.5 (register field tables, "Reset by"
column) and the feature-description sections. This complements
[`bq25798-registers.md`](bq25798-registers.md), which is the plain register map.

It exists to drive the reconcile list in the driver: for every field the host cares about, who can
change it behind the host's back, what puts it back to a default, what value the hub wants, and
whether the driver should **enforce** it (re-write on drift) or only **watch** it (read, publish,
never write).

## Legend

**Who changes it**

- **H**: only the host (an I2C write). Nothing on the charger moves it.
- **C**: the charger changes it by itself (state machine, self-clearing, plug-in or unplug).
- **H+C**: the host sets it and the charger can also change it.

**Reset by** (the datasheet's own column)

- **WD**: watchdog expiry. With `WATCHDOG = 0` (disabled, the hub setting) this never happens.
- **RST**: `REG_RST` (`REG09[6]`), or a power-on reset.
- **POR/PROG**: the power-on value comes from the PROG pin strap (cell count, VREG, VSYSMIN, ICHG
  default, PWM frequency).

**Policy**

- **Enforce**: if the live value differs from the wanted value, write it back and verify the write.
- **Watch**: read it, publish it, log a change. Never write it.
- **Event**: a self-clearing trigger bit. Write once, expect it to read back as 0.
- **Leave**: not used on this hardware. Not read, not written.

## Datasheet facts that change the plan

1. **`VREG` (charge voltage limit) is reset only by `REG_RST`, not by the watchdog.** Table 7-15
   lists only `REG_RST`. `ICHG`, `ITERM`, `IPRECHG`, `EN_CHG`, `EN_TERM`, the timers,
   `TS_IGNORE`, `AUTO_INDET_EN`, `ADC_EN` and the JEITA settings *are* reset by the watchdog. So a
   watchdog expiry on a LiFePO4 board would not push `VREG` back to the 4.2 V strap, but it would
   silently drop `TS_IGNORE`, the charge current and the ADC. Verify this on hardware (reproduce a
   watchdog expiry on `.48`) before relying on it.
2. **`VINDPM` is the one register that survives everything.** Table 7-17: reset to 3.6 V on adapter
   unplug, set from the measured VBUS when an adapter plugs in, **not** reset by `REG_RST` or the
   watchdog. MPPT and `FORCE_VINDPM_DET` rewrite it as well. It is a floor for VBUS (the charger
   backs off input current to hold VBUS above it), so the host cannot treat a value it wrote as
   permanent.
3. **`EN_HIZ` is cleared by the charger on adapter plug-in** (Table 7-25) and set by the charger on
   a failed poor-source test and by `IBUS_OCP`. The host can clear it at any time.
4. **`EN_MPPT` self-clears** when VBUS falls below the present threshold. `EN_ICO`,
   `FORCE_VINDPM_DET` and `EN_MPPT` are mutually exclusive.
5. **`DIS_ACDRV` is reset when VAC falls below the present threshold**, and `EN_ACDRV1/2` lock at 0
   if no ACFET-RBFET pair is detected at power-on.
6. **No INT for host writes or silent charger changes.** The INT pin pulses on *status* edges (below).
   A register the charger rewrote without a status edge, such as `VINDPM` after plug-in or `EN_MPPT`
   clearing, gives no interrupt on its own.

## Bench confirmation of the watchdog resets (2026-10-17)

Run on two hub boards, one Li-ion (`VREG` poked to 4.00 V) and one LiFePO4 (`VREG` poked to 3.50 V).
Known non-default values were written, the watchdog was set to 0.5 s and left to expire with no kick,
and the 49-byte block `0x00`..`0x30` was read 2 s later. Both boards gave the same result.

| Register | Written | After the expiry |
|---|---|---|
| `VREG` | 4.00 V / 3.50 V | **Kept** |
| `VINDPM` | 4.5 V | **Kept** |
| `IINDPM` | 1.5 A | **Kept** |
| `WATCHDOG` field | 0.5 s | **Kept**, so the watchdog keeps expiring until it is written to 0 |
| `ICHG` | 300 mA | Reset to the PROG default, 1000 mA |
| `ITERM`, `VRECHG`, safety timers, JEITA (`REG17`) | changed | Reset to defaults |
| `EN_CHG` | 0 | Reset to 1 (`STOP_WD_CHG` was 0) |
| `TS_IGNORE`, `EN_IBAT`, `EN_BATOC` | set | Cleared |
| `ADC_EN` | 1 | Cleared |
| `VSYSMIN` | 3.75 V / 3.25 V | Reset to the 3.5 V strap value. Table 7-14 lists only `REG_RST` for this field, so the table is wrong here. |

`WD_STAT` (`0x1B` bit 5) and the watchdog flag (`0x22` bit 5) were set. A watchdog expiry therefore does
not move the charge voltage limit, but it does silently turn off the ADC, the thermistor bypass and
the battery-discharge current sensing, and restores a 1 A charge current.

## EN_HIZ latch on a slowly rising or weak source (bench, 2026-10-18)

Measured with a programmable supply on a hub's solar input (SunSprout bench tests):

- Stepping the supply **up** from 3 V in 0.5 V steps, the charger set `EN_HIZ` at 4.0 V (power good false,
  `VBUS_STAT` = 8, "not qualified adapter") and it **stayed set through 12 V with 0.8 A available**. Switching the
  supply on directly at 12 V did the same. Nothing cleared it within about 2.5 minutes, so the charger's own
  retry (the datasheet says 7 minutes) did not help in that time.
- A host write of `EN_HIZ = 0` cleared it at once, power good returned and the input worked.
- A panel rising slowly at dawn never produces an unplug and plug-in, which is the other thing that clears the
  bit, so it can sit latched while the light improves.
- On a real panel in weak morning light the same write cleared it, but the charger set it again within 3.5 s,
  because the start-up check still failed. Clearing it on a timer (`register_check: enforce` does so every 30 s)
  retries until the panel can pass.
- Entering HIZ also stops MPPT from updating `VINDPM`, so a stale high `VINDPM` stays in place while latched.
- Thresholds with the supply stepping up and down: input present and power good down to 3.5 V, lost at 3.0 V,
  back at 3.5 V going up.

## Register table

Only fields the driver reads or writes, or that could surprise it, are listed. Registers whose every
bit is reserved or unused here are left out. Offsets are from the datasheet register map.

### Voltage, current and threshold limits

| Reg.field | Meaning | Who | Reset by | Wanted on the hub | Policy |
|---|---|---|---|---|---|
| `00 VSYSMIN[5:0]` | Minimum system voltage | H | RST, POR/PROG, **and WD (measured, not in Table 7-14)** | Config value (default 3.5 V on 1S) | Enforce |
| `01 VREG[10:0]` | Charge voltage limit | H | RST, POR/PROG | From chemistry: 4.20 V Li-ion, 3.60 V LiFePO4 | **Enforce** (safety: wrong value overcharges) |
| `03 ICHG[8:0]` | Fast-charge current limit | H | WD, RST | `charge_current` setting | Enforce |
| `05 VINDPM[7:0]` | Input voltage floor | H+C | not reset by RST or WD, 3.6 V on unplug | Auto, or user number | Watch (publish live; enforce only if the user set it explicitly) |
| `06 IINDPM[8:0]` | Input current limit | H+C | RST | User number. D+/D- detection loads it at plug-in when `AUTO_INDET_EN` is 1 | Watch, enforce if the user set it |
| `08 VBAT_LOWV[7:6]` | Pre-charge to fast-charge threshold | H | RST | Default (`3h`, 71.4% of VREG) | Watch |
| `08 IPRECHG[5:0]` | Pre-charge current | H | WD, RST | Default 120 mA | Watch |
| `09 ITERM[4:0]` | Termination current | H | WD, RST | C/10 of capacity, or config | Enforce |
| `09 STOP_WD_CHG` | Watchdog expiry disables charging | H | none listed | 0 | Watch |
| `0A CELL[7:6]` | Cell count | POR/PROG | none listed | 1S | Watch (a change means a reset happened) |
| `0A TRECHG[5:4]`, `VRECHG[3:0]` | Recharge deglitch, recharge offset | H | WD, RST | Default (1024 ms, 200 mV) | Watch |
| `0B VOTG`, `0D IOTG` | OTG regulation | H | WD, RST | Unused (no OTG on the hub) | Leave |

### Timers and charge control

| Reg.field | Meaning | Who | Reset by | Wanted | Policy |
|---|---|---|---|---|---|
| `0E TOPOFF_TMR[7:6]` | Top-off timer | H | WD, RST | 0 (disabled) | Watch |
| `0E EN_TRICHG_TMR`, `EN_PRECHG_TMR`, `EN_CHG_TMR` | Safety timer enables | H | WD, RST | 1 (default). Do not disable: they are the safety net | **Enforce 1** |
| `0E CHG_TMR[2:1]` | Fast-charge timer length | H | WD, RST | Default 12 h | Watch |
| `0E TMR2X_EN` | Timers run at half speed in DPM or thermal regulation | H | WD, RST | 1 (default) | Watch |
| `0D PRECHG_TMR` | Pre-charge timer length | H | WD, RST | Default 2 h | Watch |
| `0F EN_CHG[5]` | Charging on or off | H (charger clears it on WD only if `STOP_WD_CHG`) | WD, RST | User switch | Enforce |
| `0F EN_ICO[4]` | Input current optimiser | H | RST | 0 while MPPT is on | Enforce 0 when MPPT is wanted |
| `0F FORCE_ICO[3]` | Start ICO | H, self-clears | WD, RST | 0 | Event |
| `0F EN_HIZ[2]` | High-impedance input | H+C | RST, **cleared on plug-in** | Follows the input selection (1 for Off, 0 for Auto/Solar/USB) | Enforce with recovery (see below) |
| `0F EN_TERM[1]` | Termination enabled | H | WD, RST | 1 | Enforce |
| `0F EN_AUTO_IBATDIS[7]`, `FORCE_IBATDIS[6]` | Battery discharge on OVP | H | RST | Defaults (1, 0) | Watch |
| `0F EN_BACKUP[0]` | Backup mode | H (cleared by `BKUP_ACFET1_ON`) | WD, RST | 0 | Watch |

### Control registers 1 to 5

| Reg.field | Meaning | Who | Reset by | Wanted | Policy |
|---|---|---|---|---|---|
| `10 WATCHDOG[2:0]` | Watchdog period | H | RST | 0 (disabled) | Enforce (a `REG_RST` returns it to 40 s) |
| `10 WD_RST[3]` | Kick the watchdog | H, self-clears | WD, RST | n/a | Event |
| `10 VAC_OVP[5:4]` | Input over-voltage threshold | H | RST | Default 26 V | Watch |
| `10 VBUS_BACKUP[7:6]` | Backup mode trigger | H | RST | Default | Leave |
| `11 AUTO_INDET_EN[6]` | D+/D- detection at plug-in | H | WD, RST | Config (default 1) | Enforce |
| `11 FORCE_INDET[7]` | Run D+/D- detection now | H, self-clears | WD, RST | 0 | Event |
| `11 EN_12V`, `EN_9V`, `HVDCP_EN` | HVDCP handshake | H | RST | 0 | Watch |
| `11 SDRV_CTRL[2:1]` | Ship FET command | H, self-clears | RST | 0 (idle) | Event (never enforce) |
| `11 SDRV_DLY[0]` | 10 s delay before the ship FET action | H | RST | Config | Enforce (only with a ship FET) |
| `12 DIS_ACDRV[7]` | Force both ACFET drivers off | H+C (`IBUS_OCP` sets it) | VAC below present | 0 | Watch |
| `12 EN_OTG[6]` | OTG mode | H | WD, RST | 0 | Enforce 0 (no OTG hardware) |
| `12 DIS_LDO`, `PFM_*`, `*_OOA`, `WKUP_DLY` | Efficiency and wake options | H | WD or RST | Defaults | Watch |
| `13 EN_ACDRV1/2[6:7]` | ACFET drivers | H | none listed | Config | Watch |
| `13 PWM_FREQ[5]` | Switching frequency | POR/PROG | none listed | Strap value | Watch |
| `13 DIS_STAT[4]` | STAT pin off | H | WD, RST | 0 | Watch |
| `13 FORCE_VINDPM_DET[1]` | Measure VOC and set VINDPM once | H, self-clears | RST | 0 | Event |
| `13 EN_IBUS_OCP[0]` | Input over-current protection | H | RST | 1 | **Enforce 1** |
| `13 DIS_VSYS_SHORT`, `DIS_VOTG_UVP` | Hiccup protections | H | RST | 0 | Enforce 0 |
| `14 SFET_PRESENT[7]` | Ship FET populated | H | none listed | Hardware config | Enforce |
| `14 EN_IBAT[5]` | Discharge current sensing in battery-only or HIZ | H | WD, RST | 1 | **Enforce 1** |
| `14 IBAT_REG[4:3]` | OTG discharge regulation | H | WD, RST | Default | Leave |
| `14 EN_IINDPM[2]` | Use the internal IINDPM register | H | WD, RST | 1 | Enforce 1 |
| `14 EN_EXTILIM[1]` | Honour the `ILIM_HIZ` pin | H | RST | 1 | Watch |
| `14 EN_BATOC[0]` | Battery over-current opens the ship FET | H | WD, RST | Config (needs a ship FET) | Enforce |

### MPPT, thermal and NTC

| Reg.field | Meaning | Who | Reset by | Wanted | Policy |
|---|---|---|---|---|---|
| `15 EN_MPPT[0]` | MPPT on | H+C (clears when VBUS drops) | RST | User switch | Enforce (existing re-assert, move to the fast tier) |
| `15 VOC_PCT[7:5]`, `VOC_DLY[4:3]`, `VOC_RATE[2:1]` | MPPT ratio, delay, rate | H | RST | User selects | Enforce |
| `16 TREG[7:6]`, `TSHUT[5:4]` | Thermal regulation and shutdown thresholds | H | WD, RST | Defaults (120 C, 150 C) | Watch |
| `16 VBUS_PD_EN`, `VAC*_PD_EN` | Pull-downs | H | RST | 0 | Watch |
| `17 JEITA_VSET/ISETH/ISETC` | JEITA charge voltage and current by temperature | H | WD, RST | Defaults | Watch |
| `18 TS_COOL/WARM/BHOT/BCOLD` | NTC thresholds | H | WD, RST | Defaults | Watch |
| `18 TS_IGNORE[0]` | Ignore the thermistor | H | WD, RST | From `TS Enable` | **Enforce** (already re-written every pass) |
| `19 ICO_ILIM[8:0]` | ICO result | C | n/a | n/a | Watch (read only) |

### Masks (interrupt routing)

| Reg | Who | Reset by | Wanted | Policy |
|---|---|---|---|---|
| `28` Charger Mask 0 (IINDPM, VINDPM, WD, POORSRC, PG, AC2, AC1, VBUS present) | H | RST | 0, so every event pulses INT. Optionally mask IINDPM and VINDPM, which toggle often under MPPT | Enforce |
| `29` Charger Mask 1 (CHG, ICO, VBUS_STAT, TREG, VBAT present, BC1.2) | H | RST | 0 | Enforce |
| `2A` Charger Mask 2 (DPDM, ADC done, VSYS, timers) | H | RST | 0 (`ADC_DONE` only applies to one-shot mode) | Enforce |
| `2B` Charger Mask 3 (VBATOTG, TS cold, cool, warm, hot) | H | WD, RST | 0 | Enforce |
| `2C`, `2D` Fault Mask 0 and 1 | H | RST | 0 | Enforce |

### ADC

| Reg.field | Meaning | Who | Reset by | Wanted | Policy |
|---|---|---|---|---|---|
| `2E ADC_EN[7]` | ADC on | H (and **off when HIZ is entered unless re-enabled**) | WD, RST | 1 | **Enforce 1** |
| `2E ADC_RATE[6]` | Continuous or one-shot | H | RST | 0 (continuous) | Enforce |
| `2E ADC_SAMPLE[5:4]` | Resolution | H | RST | Default (`3h`, 12-bit, datasheet marks it not recommended) | Watch, consider `0h` (15-bit) |
| `2E ADC_AVG`, `ADC_AVG_INIT` | Averaging | H | RST | 0 | Watch |
| `2F`, `30` ADC Function Disable | Per-channel disable | H | RST | 0 (all enabled) | Enforce |
| `31`..`45` ADC results | Measurements | C | n/a | n/a | Read on the slow tier |

### Status, fault and flag registers (all read-only, all Watch)

| Reg | Contents | Notes for the driver |
|---|---|---|
| `1B` Status 0 | IINDPM, VINDPM, WD expired, PG, AC2/AC1/VBUS present | `PG` and `VBUS_PRESENT` are the plug-in cues |
| `1C` Status 1 | `CHG_STAT[7:5]`, `VBUS_STAT[4:1]`, BC1.2 done | |
| `1D` Status 2 | ICO status, TREG, DPDM busy, VBAT present | |
| `1E` Status 3 | ACFET placement, ADC done, VSYS min regulation, timer expiry bits | |
| `1F` Status 4 | VBATOTG low, TS cold, cool, warm, hot | With `TS_IGNORE` all four read 0 |
| `20`, `21` Fault status | IBAT_REG, VBUS/VBAT OVP, IBUS/IBAT/CONV OCP, VAC OVP, VSYS short/OVP, OTG OVP/UVP, TSHUT | Live state |
| `22`..`25` Flags (charger 0..3) | Latched edges for the status bits | Clear on read |
| `26`, `27` Fault flags 0 and 1 | Latched edges for the fault bits | Clear on read |

## Interrupt behaviour

- INT pulses on the **rising edge of a flag** whose mask bit is 0. The flag bits latch and are cleared
  by reading the flag register. A second event of the same kind while the flag is still set does not
  produce another pulse, so read-and-clear after each pulse.
- Status registers (`1B`..`21`) show the live state and are not cleared by reading.
- There is **no interrupt** when the host writes a register, and none when the charger rewrites a
  control bit without a matching status edge (`VINDPM` after plug-in, `EN_MPPT` clearing, `EN_HIZ`
  clearing on plug-in, a reset returning a register to its default). Control registers therefore need
  polling regardless of INT.
- A watchdog expiry does produce an INT (`WD_MASK`), and `WD_STAT` shows it, but only if the watchdog
  is enabled. With it disabled the driver cannot learn about a reset from INT. It has to notice from
  the read-back (`CELL`, `WATCHDOG` or `TS_IGNORE` back at its default).

## Reading several registers at once

The BQ25798 auto-increments the register pointer across a multi-byte read, and a read can run across
register boundaries. So registers do not need one transaction each.

Proposed tiers, using the datasheet offsets:

| Tier | Period | Registers | Bytes |
|---|---|---|---|
| Events | On INT, plus a 1 to 2 s backstop | `1B`..`21` status and fault, `22`..`27` flags | 13 |
| Control | 5 to 10 s | `00`..`19` limits, timers, control, MPPT, NTC, ICO | 26 |
| ADC | 30 s, and on demand | `2E`..`30` control, `31`..`45` results | 21 (or one 49-byte read of `00`..`30`) |

On the hub's 50 kHz internal bus a byte costs about 180 us, so the events tier is about 2.4 ms, the
control tier about 4.7 ms and a whole-map read about 9.5 ms. At the periods above that is well under
1% of bus time. If the ESP32-C5 low-power I2C controller limits the read length (unconfirmed), split
the control tier at `0x10`.

## Reconcile list (what the driver should hold, in order of importance)

1. `VREG`, `ITERM`, `ICHG`, `EN_CHG`: charge safety. Enforce, verify after each write.
2. `EN_HIZ`: re-clear when the input selection wants a source and the charger set it (poor source).
   Retry with back-off and a cap, then publish a fault the user can see.
3. `ADC_EN`, `2F`, `30`: enforce so measurements never go quiet after HIZ or a reset.
4. `EN_MPPT`, `VOC_PCT`: re-assert when cleared by a VBUS loss.
5. `TS_IGNORE`, `AUTO_INDET_EN`, `EN_IBAT`, `EN_IINDPM`, `EN_IBUS_OCP`, safety timer enables,
   `EN_TERM`: enforce, because the watchdog and `REG_RST` return them to defaults.
6. Masks: enforce so INT keeps reporting what the fast tier relies on.
7. Everything marked Watch: publish a drift counter and log, never write.
