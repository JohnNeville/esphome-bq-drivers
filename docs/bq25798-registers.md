# BQ25798 register notes

Extracted from TI datasheet **SLUSDV2C** (May 2020, revised June 2026), section 7.5.
Kept here so later work does not have to re-derive it from the PDF.

I2C address is fixed at `0x6B`. There is no address strap.

## Register map (Table 7-12)

16-bit registers are big-endian: the listed offset holds the MSB.

| Offset | Name | Width | Notes |
|---|---|---|---|
| `0x00` | Minimal System Voltage | 8 | `VSYSMIN[5:0]`, offset 2500 mV, 250 mV/step, 2500–16000 mV |
| `0x01` | Charge Voltage Limit | 16 | `VREG[10:0]`, 10 mV/step, 3000–18800 mV |
| `0x03` | Charge Current Limit | 16 | `ICHG[8:0]`, 10 mA/step, 50–5000 mA. **Reset by WATCHDOG** |
| `0x05` | Input Voltage Limit | 8 | `VINDPM[7:0]`, 100 mV/step, 3600–22000 mV. Not reset by watchdog or `REG_RST`; reset to 3600 mV on adapter unplug |
| `0x06` | Input Current Limit | 16 | `IINDPM[8:0]`, 10 mA/step, 100–3300 mA |
| `0x08` | Precharge Control | 8 | `VBAT_LOWV[7:6]`, `IPRECHG[5:0]` 40 mA/step |
| `0x09` | Termination Control | 8 | `REG_RST[6]`, `STOP_WD_CHG[5]`, `ITERM[4:0]` 40 mA/step, 40–1000 mA |
| `0x0A` | Re-charge Control | 8 | `CELL[7:6]`, `TRECHG[5:4]`, `VRECHG[3:0]` |
| `0x0E` | Timer Control | 8 | `TOPOFF_TMR[7:6]`, charge-timer enables, `CHG_TMR[2:1]` |
| `0x0F` | Charger Control 0 | 8 | `EN_CHG[5]`, `EN_ICO[4]`, `FORCE_ICO[3]`, `EN_HIZ[2]`, `EN_TERM[1]` |
| `0x10` | Charger Control 1 | 8 | `VAC_OVP[5:4]`, `WD_RST[3]`, `WATCHDOG[2:0]` |
| `0x11` | Charger Control 2 | 8 | `SDRV_CTRL[2:1]`, `SDRV_DLY[0]` |
| `0x12` | Charger Control 3 | 8 | `DIS_ACDRV[7]`, `EN_OTG[6]`, `WKUP_DLY[3]` |
| `0x13` | Charger Control 4 | 8 | `EN_ACDRV2[7]`, `EN_ACDRV1[6]`, `PWM_FREQ[5]`, `DIS_STAT[4]`, `FORCE_VINDPM_DET[1]` |
| `0x14` | Charger Control 5 | 8 | `SFET_PRESENT[7]`, `EN_IBAT[5]`, `IBAT_REG[4:3]`, `EN_IINDPM[2]`, `EN_EXTILIM[1]`, `EN_BATOC[0]` |
| `0x15` | MPPT Control | 8 | `VOC_PCT[7:5]`, `VOC_DLY[4:3]`, `VOC_RATE[2:1]`, `EN_MPPT[0]` |
| `0x16` | Temperature Control | 8 | `TREG[7:6]`, `TSHUT[5:4]`, pull-down enables, `BKUP_ACFET1_ON[0]` |
| `0x17` | NTC Control 0 | 8 | `JEITA_VSET[7:5]`, `JEITA_ISETH[4:3]`, `JEITA_ISETC[2:1]` |
| `0x18` | NTC Control 1 | 8 | `TS_COOL[7:6]`, `TS_WARM[5:4]`, `BHOT[3:2]`, `BCOLD[1]`, `TS_IGNORE[0]` |
| `0x19` | ICO Current Limit | 16 | Result of input current optimisation |
| `0x1B`–`0x1F` | Charger Status 0–4 | 8 | See below |
| `0x20`–`0x21` | FAULT Status 0/1 | 8 | See below |
| `0x22`–`0x27` | Charger / FAULT Flags | 8 | Latched, clear on read |
| `0x28`–`0x2D` | Charger / FAULT Masks | 8 | Interrupt masks |
| `0x2E` | ADC Control | 8 | `ADC_EN[7]`, `ADC_RATE[6]`, `ADC_SAMPLE[5:4]`, `ADC_AVG[3]` |
| `0x2F`–`0x30` | ADC Function Disable 0/1 | 8 | Per-channel disable bits |
| `0x31` | IBUS ADC | 16 | **Signed**, 1 mA/LSB |
| `0x33` | IBAT ADC | 16 | **Signed**, 1 mA/LSB |
| `0x35` | VBUS ADC | 16 | 1 mV/LSB, 0–30000 mV |
| `0x37` | VAC1 ADC | 16 | 1 mV/LSB |
| `0x39` | VAC2 ADC | 16 | 1 mV/LSB |
| `0x3B` | VBAT ADC | 16 | 1 mV/LSB (remote sense, `VBATP`) |
| `0x3D` | VSYS ADC | 16 | 1 mV/LSB |
| `0x3F` | TS ADC | 16 | 0.0976563 %/LSB, percent of `REGN` |
| `0x41` | TDIE ADC | 16 | **Signed**, 0.5 °C/LSB, −40 to 150 °C |
| `0x43`/`0x45` | D+ / D− ADC | 16 | 1 mV/LSB |
| `0x47` | DPDM Driver | 8 | |
| `0x48` | Part Information | 8 | `PN[5:3]` = `011b` for the BQ25798 |

Unlike the BQ25628E, no ADC result needs bit shifting — every one is a plain 16-bit count
with a 1 mV or 1 mA LSB.

## Status registers

**`0x1B` Charger Status 0** — `IINDPM_STAT[7]`, `VINDPM_STAT[6]`, `WD_STAT[5]`,
`PG_STAT[3]`, `AC2_PRESENT[2]`, `AC1_PRESENT[1]`, `VBUS_PRESENT[0]`

**`0x1C` Charger Status 1** — `CHG_STAT[7:5]`, `VBUS_STAT[4:1]`, `BC1.2_DONE[0]`

`CHG_STAT`: 0 not charging · 1 trickle · 2 pre-charge · 3 fast charge (CC) ·
4 taper (CV) · 5 reserved · 6 top-off timer · 7 terminated

`VBUS_STAT`: 0 no input · 1 USB SDP · 2 USB CDP · 3 USB DCP · 4 adjustable HVDCP ·
5 unknown adapter · 6 non-standard adapter · 7 OTG · 8 unqualified adapter ·
`0xB` powered directly from VBUS · `0xC` backup mode

**`0x1D` Charger Status 2** — `ICO_STAT[7:6]`, `TREG_STAT[2]`, `DPDM_STAT[1]`,
`VBAT_PRESENT[0]`

**`0x1E` Charger Status 3** — `ACRB2_STAT[7]`, `ACRB1_STAT[6]`, `ADC_DONE[5]`,
`VSYS_STAT[4]`, timer-expiry bits `[3:1]`

**`0x1F` Charger Status 4** — `VBATOTG_LOW[4]`, `TS_COLD[3]`, `TS_COOL[2]`, `TS_WARM[1]`,
`TS_HOT[0]`

**`0x20` FAULT Status 0** — `IBAT_REG[7]`, `VBUS_OVP[6]`, `VBAT_OVP[5]`, `IBUS_OCP[4]`,
`IBAT_OCP[3]`, `CONV_OCP[2]`, `VAC2_OVP[1]`, `VAC1_OVP[0]`

**`0x21` FAULT Status 1** — `VSYS_SHORT[7]`, `VSYS_OVP[6]`, `OTG_OVP[5]`, `OTG_UVP[4]`,
`TSHUT[2]`

## Behaviours that constrain a driver

- **`ICHG` is reset by the watchdog.** So are `IPRECHG`, `ITERM`, `EN_CHG`, `EN_TERM` and
  the timer settings. `WATCHDOG` defaults to `101b` = 40 s. Either disable it or kick
  `WD_RST` faster than the period.
- **`EN_ICO`, `FORCE_VINDPM_DET` and `EN_MPPT` are mutually exclusive.** Only one may be
  set at a time; writes to a second are ignored.
- **`EN_MPPT` self-clears** when VBUS falls below `VBUS_PRESENT`.
- **`SFET_PRESENT` gates `SDRV_CTRL` and `EN_BATOC`.** Both are locked at 0 until it is set,
  and it should only be set when an external ship FET is actually populated on `SDRV`.
  `SDRV_CTRL` (`0x11[2:1]`): 0 idle · 1 shutdown · 2 ship mode · 3 system power reset.
  `SDRV_DLY` (`0x11[0]`) adds a ~10 s delay before the action, and is 0 (delay on) at POR.
  The IC ignores shutdown and ship mode while an adapter is present.
- **`IBAT_OCP` is a fixed threshold of about 9.3 A** and is not adjustable. `EN_BATOC`
  (`0x14[0]`) only decides whether crossing it opens the ship FET; the `IBAT_OCP_STAT` and
  `IBAT_OCP_FLAG` bits report the event either way.
- **`EN_IBAT` must be set** for the ADC to measure battery discharge current in
  battery-only or HIZ mode — exactly when that reading matters.
- **`EN_EXTILIM` defaults to 1**, so an external `ILIM_HIZ` resistor divider is honoured
  out of the box.
- **`ACDRV1`/`ACDRV2` lock at 0** if the IC detects no ACFET-RBFET pair at power-on.
  `DIS_ACDRV` (`0x12[7]`) forces both off regardless.

## MPPT (`0x15`)

| Field | Values |
|---|---|
| `VOC_PCT[7:5]` | 0.5625, 0.625, 0.6875, 0.75, 0.8125, **0.875**, 0.9375, 1.0 |
| `VOC_DLY[4:3]` | 50 ms, **300 ms**, 2 s, 5 s |
| `VOC_RATE[2:1]` | 30 s, **2 min**, 10 min, 30 min |
| `EN_MPPT[0]` | **0 = disabled** |

Bold is the power-on default. The charger measures the open-circuit VBUS voltage every
`VOC_RATE`, waits `VOC_DLY` after it stops switching, then sets `VINDPM` to `VOC_PCT` of
the measured open-circuit voltage.
