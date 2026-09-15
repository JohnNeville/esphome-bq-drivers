# BQ34Z100-G1 command and data flash notes

Extracted from TI datasheet **SLUSAU1C** (May 2012, revised May 2021).
I2C address is fixed at `0x55`. All words are **little-endian** (low byte first) — the
opposite of the BQ25798.

## Standard commands (Table 7-1)

| Code | Name | Unit |
|---|---|---|
| `0x00` | `Control()` | subcommand, see below |
| `0x02` | `StateOfCharge()` | % |
| `0x04` | `RemainingCapacity()` | mAh |
| `0x06` | `FullChargeCapacity()` | mAh |
| `0x08` | `Voltage()` | mV |
| `0x0A` | `AverageCurrent()` | mA, signed |
| `0x0C` | `Temperature()` | 0.1 K |
| `0x0E` | `Flags()` | bitfield |

## Extended commands (Table 7-6)

| Code | Name | Unit |
|---|---|---|
| `0x10` | `AtRate()` on write, `Current()` on read | mA, signed |
| `0x14` | `NominalAvailableCapacity()` | mAh |
| `0x16` | `FullAvailableCapacity()` | mAh |
| `0x18` | `TimeToEmpty()` | minutes, `0xFFFF` = unknown |
| `0x1A` | `TimeToFull()` | minutes, `0xFFFF` = unknown |
| `0x24` | `AvailableEnergy()` | 10 mWh |
| `0x26` | `AveragePower()` | 10 mW |
| `0x2A` | `InternalTemp()` | 0.1 K |
| `0x2C` | `CycleCount()` | counts |
| `0x2E` | `StateOfHealth()` | % |
| `0x30` | `ChargeVoltage()` | mV |
| `0x32` | `ChargeCurrent()` | mA |
| `0x3A` | `PackConfiguration()` | bitfield |
| `0x3C` | `DesignCapacity()` | mAh |
| `0x3E` | `DataFlashClass()` | subclass select |
| `0x3F` | `DataFlashBlock()` | block select |
| `0x40`–`0x5F` | `BlockData()` | 32-byte window |
| `0x60` | `BlockDataChecksum()` | `255 − (sum of the 32 bytes)` |
| `0x61` | `BlockDataControl()` | write `0x00` to select data flash |

There is **no `FlagsB` register** on this part, unlike several siblings in the family.
`0x10`/`0x11` doing double duty as `AtRate` (write) and `Current` (read) is deliberate.

## `Control()` subcommands (Table 7-2)

| Data | Name | Sealed? |
|---|---|---|
| `0x0000` | `CONTROL_STATUS` | yes |
| `0x0001` | `DEVICE_TYPE` — returns `0x0541` | yes |
| `0x0002` | `FW_VERSION` | yes |
| `0x0003` | `HW_VERSION` | yes |
| `0x0005` | `RESET_DATA` | no |
| `0x0007` | `PREV_MACWRITE` | no |
| `0x0008` | `CHEM_ID` — **reports** the chemistry, cannot set it | yes |
| `0x0009` | `BOARD_OFFSET` | no |
| `0x000A` | `CC_OFFSET` | no |
| `0x000B` | `CC_OFFSET_SAVE` | no |
| `0x000C` | `DF_VERSION` | yes |
| `0x0010` | `SET_FULLSLEEP` | no |
| `0x0017` | `STATIC_CHEM_CHKSUM` | yes |
| `0x0018` | `CURRENT` | yes |
| `0x0020` | `SEALED` | no |
| `0x0021` | `IT_ENABLE` | no |
| `0x002D` | `CAL_ENABLE` | no |
| `0x0041` | `RESET` | no |
| `0x0080` | `EXIT_CAL` | no |
| `0x0081` | `ENTER_CAL` | no |
| `0x0082` | `OFFSET_CAL` | no |

`CONTROL_STATUS` bit 13 is `[SS]` (sealed) and bit 14 is `[FAS]` (full access).

## `Flags()` bits (Table 7-5)

High byte: `OTC[15]`, `OTD[14]`, `BATHI[13]`, `BATLOW[12]`, `CHG_INH[11]`, `FC[9]`, `CHG[8]`
Low byte: `OCVTAKEN[7]`, `ISD[6]`, `TDD[5]`, `SOC1[2]`, `SOCF[1]`, `DSG[0]`

## Sealing

Two key words sent back to back through `Control()`, with nothing written in between.
The keys live in data flash and can only be changed in FULL ACCESS mode. **The byte order
sent is the reverse of the byte order read back** — if `Unseal Key 0` reads `0x1234` and
`0x5678`, send `0x3412` then `0x7856`.

Factory defaults are `0x0414` then `0x3672` for unseal, `0xFFFF` twice for full access.

## Data flash access (unsealed)

1. `BlockDataControl()` ← `0x00` (selects data flash, not the authentication block)
2. `DataFlashClass()` ← subclass id
3. `DataFlashBlock()` ← `offset / 32`
4. Read or write 32 bytes at `BlockData()` (`0x40`)
5. On write, follow with `BlockDataChecksum()` ← `255 − (sum of the 32 bytes mod 256)`
6. Allow ~200 ms for the flash write to settle

## Data flash locations used (Table 7-8)

| Subclass | Offset | Type | Name | Unit |
|---|---|---|---|---|
| 48 (Data) | 11 | I2 | Design Capacity | mAh |
| 48 (Data) | 13 | I2 | Design Energy | mWh |
| 48 (Data) | 55 | S5 | Device Chemistry | string |
| 64 (Registers) | 0 | H2 | Pack Configuration | flags |
| 64 (Registers) | 2 | H1 | Pack Configuration B | flags |
| 64 (Registers) | 7 | U1 | Number of series cell | |
| 80 (IT Cfg) | 0 | U1 | Load Select | |
| 82 (State) | 0 | I2 | Qmax Cell 0 | |
| 104 (Calibration) | 0 | F4 | CC Gain | |
| 104 (Calibration) | 4 | F4 | CC Delta | |
| 104 (Calibration) | 8 | I2 | CC Offset | |
| 104 (Calibration) | 10 | I1 | Board Offset | |
| 104 (Calibration) | 14 | U2 | Voltage Divider | mV |
| 107 | 1 | U1 | Deadband | |

Note: the prose in §7.2.6.15.1 says Pack Configuration B is at "subclass 64, offset 0".
That is a copy-paste error; Table 7-8 gives offset 2, which is the one to use.

### Pack Configuration (64/0, Table 7-14)

`RESCAP[15]`, `CAL_EN[14]`, `VOLTSEL[11]`, `IWAKE[10]`, `RSNS1[9]`, `RSNS0[8]`,
`X10[7]`, `RESFACTSTEP[6]`, `SLEEP[5]`, `RMFCC[4]`, `TEMPS[0]`

- `VOLTSEL` — 1 selects the external voltage divider. The internal divider is single-cell
  only. Default 0.
- `TEMPS` — 1 selects the external thermistor for `Temperature()`. Default 1.

### Pack Configuration B (64/2, Table 7-15)

`CHGDoDEoC[7]`, `VconsEN[5]`, `JEITA[3]`, `LFPRelax[2]`, `DoDWT[1]`, `FConvEN[0]`

`LFPRelax` and `DoDWT` are the LiFePO4 support bits, and only take effect when the gauge
is running a 400-series chemistry.

### Sense resistor calibration

Both constants scale inversely with the shunt, taken from the datasheet's own calibration
table where the defaults correspond to a 10.124 mΩ shunt:

```
CC Gain  = 4.768      / R_sense_mOhm
CC Delta = 5677445.6  / R_sense_mOhm
```

### The F4 format

`F4` data flash items are **not IEEE 754**. The layout is four bytes,
`[exponent + 128, mantissa_hi | sign, mantissa_mid, mantissa_lo]`, decoding as:

```
exponent = byte0 - 128
negative = byte1 & 0x80
M        = (byte1 & 0x7F) + byte2/256 + byte3/65536
value    = (M + 128) * 2^(exponent - 8)
```

Encoding normalises with `frexp` so the mantissa lands in `[0.5, 1)`, then takes
`256 * mantissa - 128` as the fixed-point mantissa. The datasheet does not document this
format, so the driver verifies every `F4` write by reading it back and decoding it.

Check: `CC Gain` for a 10.124 mΩ shunt is `0.470960`, which encodes to `7f 71 21 ae` and
decodes back to `0.470960` — matching the datasheet's stated default of `0.47095`.

## What cannot be done over I2C

Loading a chemistry table. `CHEM_ID` reports the active chemistry but there is no
documented command to change it; that needs bqStudio with an EV2400 and a `.bqz` file.
