#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include <map>
#include <string>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/number/number.h"
#include "esphome/components/button/button.h"
#include "esphome/components/select/select.h"
#include "esphome/core/log.h"

namespace esphome {
namespace bq25798 {

// The BQ25798 has a fixed 7-bit I2C address. There is no ADDR strap.
static const uint8_t BQ25798_ADDR = 0x6B;

// --- Register addresses (datasheet SLUSDV2C, Table 7-12) -------------------
// 16-bit registers are big-endian: the listed offset holds the MSB.
static const uint8_t REG00_VSYSMIN = 0x00;
static const uint8_t REG01_VREG = 0x01;         // 16-bit
static const uint8_t REG03_ICHG = 0x03;         // 16-bit
static const uint8_t REG05_VINDPM = 0x05;
static const uint8_t REG06_IINDPM = 0x06;       // 16-bit
static const uint8_t REG08_PRECHG = 0x08;
static const uint8_t REG09_TERM = 0x09;
static const uint8_t REG0A_RECHG = 0x0A;
static const uint8_t REG0E_TIMER = 0x0E;
static const uint8_t REG0F_CHG_CTRL0 = 0x0F;
static const uint8_t REG10_CHG_CTRL1 = 0x10;
static const uint8_t REG11_CHG_CTRL2 = 0x11;
static const uint8_t REG12_CHG_CTRL3 = 0x12;
static const uint8_t REG13_CHG_CTRL4 = 0x13;
static const uint8_t REG14_CHG_CTRL5 = 0x14;
static const uint8_t REG15_MPPT = 0x15;
static const uint8_t REG16_TEMP_CTRL = 0x16;
static const uint8_t REG17_NTC_CTRL0 = 0x17;
static const uint8_t REG18_NTC_CTRL1 = 0x18;
static const uint8_t REG19_ICO_ILIM = 0x19;     // 16-bit
static const uint8_t REG1B_STATUS0 = 0x1B;
static const uint8_t REG1C_STATUS1 = 0x1C;
static const uint8_t REG1D_STATUS2 = 0x1D;
static const uint8_t REG1E_STATUS3 = 0x1E;
static const uint8_t REG1F_STATUS4 = 0x1F;
static const uint8_t REG20_FAULT0 = 0x20;
static const uint8_t REG21_FAULT1 = 0x21;
static const uint8_t REG22_FLAG0 = 0x22;
static const uint8_t REG23_FLAG1 = 0x23;
static const uint8_t REG24_FLAG2 = 0x24;
static const uint8_t REG25_FLAG3 = 0x25;
static const uint8_t REG26_FAULT_FLAG0 = 0x26;
static const uint8_t REG27_FAULT_FLAG1 = 0x27;
static const uint8_t REG2E_ADC_CTRL = 0x2E;
static const uint8_t REG2F_ADC_DIS0 = 0x2F;
static const uint8_t REG30_ADC_DIS1 = 0x30;
static const uint8_t REG31_IBUS_ADC = 0x31;     // 16-bit, signed
static const uint8_t REG33_IBAT_ADC = 0x33;     // 16-bit, signed
static const uint8_t REG35_VBUS_ADC = 0x35;     // 16-bit
static const uint8_t REG37_VAC1_ADC = 0x37;     // 16-bit
static const uint8_t REG39_VAC2_ADC = 0x39;     // 16-bit
static const uint8_t REG3B_VBAT_ADC = 0x3B;     // 16-bit
static const uint8_t REG3D_VSYS_ADC = 0x3D;     // 16-bit
static const uint8_t REG3F_TS_ADC = 0x3F;       // 16-bit
static const uint8_t REG41_TDIE_ADC = 0x41;     // 16-bit, signed
static const uint8_t REG48_PART_INFO = 0x48;

// --- Scaling ---------------------------------------------------------------
// Unlike the BQ25628E, every ADC result on this part is a plain 16-bit count
// with a 1 mV / 1 mA LSB, so no bit shifting is needed.
static const float VREG_STEP_MV = 10.0f;      // REG01, range 3000-18800 mV
static const float ICHG_STEP_MA = 10.0f;      // REG03, range 50-5000 mA
static const float VINDPM_STEP_MV = 100.0f;   // REG05, range 3600-22000 mV
static const float IINDPM_STEP_MA = 10.0f;    // REG06, range 100-3300 mA
static const float VSYSMIN_STEP_MV = 250.0f;  // REG00, offset 2500 mV
static const float VSYSMIN_OFFSET_MV = 2500.0f;
// VSYSMIN is limited to the charger's own one-cell default per cell. Above the pack voltage the charger holds the system
// rail at VSYSMIN and feeds the battery through the battery FET in linear mode, and the difference times the charge current
// is heat in the board (see docs/bq25798-register-behaviour.md).
static const float VSYSMIN_PER_CELL_MAX_V = 3.5f;
static const float ITERM_STEP_MA = 40.0f;     // REG09[4:0], range 40-1000 mA
static const float TS_STEP_PCT = 0.0976563f;  // REG3F, percent of REGN
static const float TDIE_STEP_C = 0.5f;        // REG41, two's complement

static const uint16_t ICHG_MIN_MA = 50;
static const uint16_t ICHG_MAX_MA = 5000;
static const uint16_t ITERM_MIN_MA = 40;
static const uint16_t ITERM_MAX_MA = 1000;

// CHG_STAT, REG1C[7:5]
enum class ChargeStat : uint8_t {
  NOT_CHARGING = 0x00,
  TRICKLE = 0x01,
  PRECHARGE = 0x02,
  FAST_CHARGE_CC = 0x03,
  TAPER_CV = 0x04,
  RESERVED = 0x05,
  TOP_OFF = 0x06,
  TERMINATED = 0x07,
};

// SDRV_CTRL, REG11[2:1]. Only programmable when SFET_PRESENT (REG14[7]) is 1.
enum class ShipFetAction : uint8_t {
  IDLE = 0x00,
  SHUTDOWN = 0x01,
  SHIP_MODE = 0x02,
  SYSTEM_POWER_RESET = 0x03,
};

class BQ25798Component;

// Every child entity shares the same parent pointer plumbing.
template<typename T> class BQ25798Child : public T, public Component {
 public:
  void set_parent(BQ25798Component *parent) { this->parent_ = parent; }

 protected:
  BQ25798Component *parent_{nullptr};
};

class BQ25798ChargingSwitch;
class BQ25798HizSwitch;
class BQ25798MpptSwitch;
class BQ25798StatLedSwitch;
class BQ25798Acdrv1Switch;
class BQ25798Acdrv2Switch;
class BQ25798IcoSwitch;
class BQ25798AdcSleepSwitch;
class BQ25798ChargeCurrentNumber;
class BQ25798ChargeVoltageNumber;
class BQ25798InputCurrentNumber;
class BQ25798VindpmNumber;
class BQ25798MinSysVoltageNumber;
class BQ25798ShipModeButton;
class BQ25798ShutdownButton;
class BQ25798PowerCycleButton;
class BQ25798ResetButton;
class BQ25798ClearInterruptsButton;
class BQ25798InputSelect;
class BQ25798VocRatioSelect;
class BQ25798VocDelaySelect;
class BQ25798VocRateSelect;

class BQ25798Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_vbus_voltage_sensor(sensor::Sensor *s) { vbus_voltage_sensor_ = s; }
  void set_vac1_voltage_sensor(sensor::Sensor *s) { vac1_voltage_sensor_ = s; }
  void set_vac2_voltage_sensor(sensor::Sensor *s) { vac2_voltage_sensor_ = s; }
  void set_vbat_voltage_sensor(sensor::Sensor *s) { vbat_voltage_sensor_ = s; }
  void set_vsys_voltage_sensor(sensor::Sensor *s) { vsys_voltage_sensor_ = s; }
  void set_ibus_current_sensor(sensor::Sensor *s) { ibus_current_sensor_ = s; }
  void set_ibat_current_sensor(sensor::Sensor *s) { ibat_current_sensor_ = s; }
  void set_battery_temperature_sensor(sensor::Sensor *s) { battery_temperature_sensor_ = s; }
  void set_die_temperature_sensor(sensor::Sensor *s) { die_temperature_sensor_ = s; }
  void set_register_corrections_sensor(sensor::Sensor *s) { register_corrections_sensor_ = s; }

  void set_vbus_present_sensor(binary_sensor::BinarySensor *s) { vbus_present_sensor_ = s; }
  void set_vac1_present_sensor(binary_sensor::BinarySensor *s) { vac1_present_sensor_ = s; }
  void set_vac2_present_sensor(binary_sensor::BinarySensor *s) { vac2_present_sensor_ = s; }
  void set_power_good_sensor(binary_sensor::BinarySensor *s) { power_good_sensor_ = s; }
  void set_charging_sensor(binary_sensor::BinarySensor *s) { charging_sensor_ = s; }
  void set_battery_present_sensor(binary_sensor::BinarySensor *s) { battery_present_sensor_ = s; }
  void set_thermal_regulation_sensor(binary_sensor::BinarySensor *s) { thermal_regulation_sensor_ = s; }
  void set_poor_source_sensor(binary_sensor::BinarySensor *s) { poor_source_sensor_ = s; }
  void set_watchdog_expired_sensor(binary_sensor::BinarySensor *s) { watchdog_expired_sensor_ = s; }
  void set_vsys_regulation_sensor(binary_sensor::BinarySensor *s) { vsys_regulation_sensor_ = s; }

  void set_charge_status_text_sensor(text_sensor::TextSensor *s) { charge_status_text_sensor_ = s; }
  void set_input_source_text_sensor(text_sensor::TextSensor *s) { input_source_text_sensor_ = s; }
  void set_charger_fault_text_sensor(text_sensor::TextSensor *s) { charger_fault_text_sensor_ = s; }
  void set_ts_status_text_sensor(text_sensor::TextSensor *s) { ts_status_text_sensor_ = s; }
  void set_last_correction_text_sensor(text_sensor::TextSensor *s) { last_correction_text_sensor_ = s; }

  void set_charging_switch(BQ25798ChargingSwitch *s) { charging_switch_ = s; }
  void set_hiz_switch(BQ25798HizSwitch *s) { hiz_switch_ = s; }
  void set_mppt_switch(BQ25798MpptSwitch *s) { mppt_switch_ = s; }
  void set_stat_led_switch(BQ25798StatLedSwitch *s) { stat_led_switch_ = s; }
  void set_acdrv1_switch(BQ25798Acdrv1Switch *s) { acdrv1_switch_ = s; }
  void set_acdrv2_switch(BQ25798Acdrv2Switch *s) { acdrv2_switch_ = s; }
  void set_ico_switch(BQ25798IcoSwitch *s) { ico_switch_ = s; }
  void set_adc_sleep_switch(BQ25798AdcSleepSwitch *s) { adc_sleep_switch_ = s; }
  void set_adc_off_before_sleep(bool off) { adc_off_before_sleep_ = off; }
  bool adc_off_before_sleep() const { return adc_off_before_sleep_; }

  void set_charge_current_number(BQ25798ChargeCurrentNumber *n) { charge_current_number_ = n; }
  void set_charge_voltage_number(BQ25798ChargeVoltageNumber *n) { charge_voltage_number_ = n; }
  void set_input_current_number(BQ25798InputCurrentNumber *n) { input_current_number_ = n; }
  void set_vindpm_number(BQ25798VindpmNumber *n) { vindpm_number_ = n; }
  void set_min_sys_voltage_number(BQ25798MinSysVoltageNumber *n) { min_sys_voltage_number_ = n; }

  void set_voc_ratio_select(BQ25798VocRatioSelect *s) { voc_ratio_select_ = s; }
  void set_input_select(BQ25798InputSelect *s) { input_select_ = s; }
  void set_active_input_text_sensor(text_sensor::TextSensor *s) { active_input_text_sensor_ = s; }
  void set_voc_delay_select(BQ25798VocDelaySelect *s) { voc_delay_select_ = s; }
  void set_voc_rate_select(BQ25798VocRateSelect *s) { voc_rate_select_ = s; }

  // Battery capacity in mAh. 0 = unknown, in which case the termination
  // current is left at its power-on default rather than derived as C/10.
  void set_battery_capacity(uint16_t mah) { battery_capacity_ = mah; }
  // Full-pack charge voltage, derived from the declared cell chemistry and
  // cell count. It is applied at boot and reapplied whenever the watchdog or
  // a register reset drops the charger back to its PROG-pin defaults, which
  // for a 1s pack means 4.2 V. NAN leaves the register alone.
  void set_charge_voltage_limit(float volts) { charge_voltage_limit_ = volts; }
  void set_default_charge_current(float milliamps) { default_charge_current_ = milliamps; }
  // Cells in series behind the charge voltage limit. A runtime chemistry change scales the
  // per-cell voltage by this.
  void set_cell_count(uint8_t count) { cell_count_ = count; }
  // Change the chemistry, the pack capacity and the default charge current after boot, and
  // apply them now. An empty chemistry or a non-positive number leaves that setting alone. A
  // chemistry this component does not know changes nothing and returns false. The values are
  // kept, so a register reset or a watchdog expiry puts them back rather than the YAML ones.
  //
  // ts_ignore is 1 to set TS_IGNORE, 0 to clear it and -1 to leave it as it is. Setting it turns off the
  // charger's battery-temperature protection, so it is only for a pack with no thermistor behind the
  // TS pin; with an open TS divider the charger reads "too cold" and never charges.
  bool configure_battery(const std::string &chemistry, int capacity_mah, int charge_current_ma, int ts_ignore = -1);
  // Hard ceiling applied to every charge-current write, including the one
  // restored at boot. Boards whose copper is rated below the IC's 5 A must
  // set this; see the SunSprout hub, which is limited to 2000 mA.
  void set_max_charge_current(uint16_t ma) { max_charge_current_ = ma; }
  void set_ts_warm_code(int8_t code) { ts_warm_code_ = code; }
  void set_jeita_warm_code(int8_t code) { jeita_warm_code_ = code; }
  void set_max_charge_temperature(float celsius) { max_charge_temp_c_ = celsius; }
  // Whether an external ship FET is populated on SDRV. The IC locks
  // SDRV_CTRL (ship / shutdown / power reset) at 00 unless SFET_PRESENT is
  // set, and SFET_PRESENT is only meaningful if the FET actually exists.
  void set_ship_fet_present(bool present) { ship_fet_present_ = present; }
  // SDRV_DLY. The charger waits about 10 s before acting on SDRV_CTRL by
  // default, which is a long time to stare at a board you just asked to
  // reboot. False removes the delay.
  void set_ship_fet_action_delay(bool delay) { ship_fet_action_delay_ = delay; }
  // EN_BATOC. Turns the ship FET off when the discharge current exceeds the
  // IC's fixed IBAT_OCP threshold of about 9.3 A. Also gated by
  // SFET_PRESENT. This is a short-circuit backstop, not copper protection:
  // 9.3 A is well above what most board copper is rated for.
  void set_battery_ocp(bool enable) { battery_ocp_ = enable; }
  // Watchdog period as the raw WATCHDOG_2:0 encoding. 0 disables it.
  // Anything non-zero is kicked on every update(); if the MCU stops talking,
  // the charger reverts ICHG and friends to their PROG-pin defaults.
  void set_watchdog(uint8_t encoding) { watchdog_ = encoding; }

  // What the periodic register check does when a register no longer holds what the host asked for:
  // 0 off, 1 watch (log it and count it, change nothing), 2 enforce (write it back and verify). It can be changed
  // while running: the 5 s timer is started or stopped to match, so "off" costs nothing.
  void set_register_check(uint8_t mode);

  // TS_IGNORE, REG18[0]. The charger normally suspends charging (and OTG)
  // whenever the TS pin sits outside the JEITA window, and an unpopulated
  // thermistor reads as "far too cold", so a board with nothing on TS will
  // not charge at all. Setting this tells the charger to treat TS as always
  // good. It gives up temperature qualification entirely, so it is only
  // honest on a board that genuinely has no thermistor.
  void set_ts_ignore(bool ignore) { ts_ignore_ = ignore; }

  // AUTO_INDET_EN, REG11[6]. When set (the power-on default) the charger runs
  // its D+/D- detection (BC1.2 and non-standard adapters) every time VBUS is
  // plugged in and uses the result to set the input current limit. Clearing
  // it stops that, which is what a board wants when D+/D- are not wired to
  // the charger, or are shared with a native USB port that the detection
  // signalling would disturb. It only affects later plug-ins: a detection
  // that already ran before the firmware started is not undone.
  void set_auto_input_detection(bool enable) { auto_input_detection_ = enable; }

  // TS divider description, needed to turn the TS ADC ratio into a
  // temperature. The upper resistor runs from REGN to TS and the lower one
  // from TS to ground, in parallel with the NTC.
  void set_ts_network(float upper_ohms, float lower_ohms, float nominal_ohms, float beta) {
    ts_upper_ohms_ = upper_ohms;
    ts_lower_ohms_ = lower_ohms;
    ts_nominal_ohms_ = nominal_ohms;
    ts_beta_ = beta;
  }

  // Called by the child entities.
  bool enable_charging(bool enable);
  bool enable_hiz(bool enable);
  // index: 0 Auto, 1 Force USB, 2 Force Solar, 3 Off. See README.md.
  bool set_input_selection(size_t index);
  bool enable_mppt(bool enable);
  bool enable_stat_led(bool enable);
  bool enable_acdrv(uint8_t index, bool enable);
  bool enable_ico(bool enable);
  bool set_charge_current(float current_ma);
  bool set_charge_voltage(float voltage_v);
  bool set_input_current(float current_ma);
  bool set_vindpm(float voltage_v);
  bool set_min_sys_voltage(float voltage_v);
  float min_sys_voltage_ceiling() const { return VSYSMIN_PER_CELL_MAX_V * this->cell_count_; }
  bool set_voc_ratio(size_t index);
  bool set_voc_delay(size_t index);
  bool set_voc_rate(size_t index);
  bool ship_fet_action(ShipFetAction action);
  bool reset_registers();
  bool clear_interrupts();
  // Number of times the periodic check found a register that did not hold what the host asked for.
  uint32_t get_corrections() const { return corrections_; }

 protected:
  bool read_u8_(uint8_t reg, uint8_t &value);
  bool write_u8_(uint8_t reg, uint8_t value);
  bool read_u16_(uint8_t reg, uint16_t &value);
  bool write_u16_(uint8_t reg, uint16_t value);
  bool read_s16_(uint8_t reg, int16_t &value);
  bool modify_u8_(uint8_t reg, uint8_t mask, uint8_t value);

  void configure_adc_();
  void apply_battery_settings_();
  void configure_defaults_();
  void publish_control_states_();
  void input_selection_step_();
  void limit_min_sys_voltage_();
  void kick_watchdog_();
  // One burst read of the whole control, status and ADC-control block, compared with what the host
  // last asked for. See docs/bq25798-register-behaviour.md for which fields are enforced.
  void reconcile_();
  void note_flags_(const uint8_t *f);
  void note_correction_(const char *name, const char *detail);
  bool correct_bits_(const char *name, uint8_t reg, uint8_t mask, uint8_t want, uint8_t seen);

  void publish_voltage_(sensor::Sensor *s, uint8_t reg);
  void publish_current_(sensor::Sensor *s, uint8_t reg);
  float ts_percent_to_celsius_(float ratio);

  sensor::Sensor *vbus_voltage_sensor_{nullptr};
  sensor::Sensor *vac1_voltage_sensor_{nullptr};
  sensor::Sensor *vac2_voltage_sensor_{nullptr};
  sensor::Sensor *vbat_voltage_sensor_{nullptr};
  sensor::Sensor *vsys_voltage_sensor_{nullptr};
  sensor::Sensor *ibus_current_sensor_{nullptr};
  sensor::Sensor *ibat_current_sensor_{nullptr};
  sensor::Sensor *battery_temperature_sensor_{nullptr};
  sensor::Sensor *die_temperature_sensor_{nullptr};
  sensor::Sensor *register_corrections_sensor_{nullptr};
  uint32_t published_corrections_{0xFFFFFFFF};

  binary_sensor::BinarySensor *vbus_present_sensor_{nullptr};
  binary_sensor::BinarySensor *vac1_present_sensor_{nullptr};
  binary_sensor::BinarySensor *vac2_present_sensor_{nullptr};
  binary_sensor::BinarySensor *power_good_sensor_{nullptr};
  binary_sensor::BinarySensor *charging_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_present_sensor_{nullptr};
  binary_sensor::BinarySensor *thermal_regulation_sensor_{nullptr};
  binary_sensor::BinarySensor *poor_source_sensor_{nullptr};
  binary_sensor::BinarySensor *watchdog_expired_sensor_{nullptr};
  binary_sensor::BinarySensor *vsys_regulation_sensor_{nullptr};

  text_sensor::TextSensor *charge_status_text_sensor_{nullptr};
  text_sensor::TextSensor *input_source_text_sensor_{nullptr};
  text_sensor::TextSensor *charger_fault_text_sensor_{nullptr};
  text_sensor::TextSensor *ts_status_text_sensor_{nullptr};
  text_sensor::TextSensor *last_correction_text_sensor_{nullptr};

  BQ25798ChargingSwitch *charging_switch_{nullptr};
  BQ25798HizSwitch *hiz_switch_{nullptr};
  BQ25798MpptSwitch *mppt_switch_{nullptr};
  BQ25798StatLedSwitch *stat_led_switch_{nullptr};
  BQ25798Acdrv1Switch *acdrv1_switch_{nullptr};
  BQ25798Acdrv2Switch *acdrv2_switch_{nullptr};
  BQ25798IcoSwitch *ico_switch_{nullptr};
  BQ25798AdcSleepSwitch *adc_sleep_switch_{nullptr};

  BQ25798ChargeCurrentNumber *charge_current_number_{nullptr};
  BQ25798ChargeVoltageNumber *charge_voltage_number_{nullptr};
  BQ25798InputCurrentNumber *input_current_number_{nullptr};
  BQ25798VindpmNumber *vindpm_number_{nullptr};
  BQ25798MinSysVoltageNumber *min_sys_voltage_number_{nullptr};

  BQ25798VocRatioSelect *voc_ratio_select_{nullptr};
  BQ25798InputSelect *input_select_{nullptr};
  text_sensor::TextSensor *active_input_text_sensor_{nullptr};
  bool input_busy_{false};
  int8_t input_requested_{-1};
  uint32_t input_deadline_{0};
  BQ25798VocDelaySelect *voc_delay_select_{nullptr};
  BQ25798VocRateSelect *voc_rate_select_{nullptr};

  float ts_upper_ohms_{0.0f};
  float ts_lower_ohms_{0.0f};
  int8_t ts_warm_code_{-1};
  int8_t jeita_warm_code_{-1};
  float max_charge_temp_c_{NAN};
  bool temp_hold_{false};  // charging held off by max_charge_temperature
  void apply_temperature_limit_(float celsius);
  float ts_nominal_ohms_{10000.0f};
  float ts_beta_{3435.0f};
  bool ts_ignore_{false};
  // Write ADC_EN = 0 in on_shutdown(), so the ADC and the REGN bias it brings up are off while the host sleeps. Setup
  // turns the ADC back on after every wake, so this never changes what the board does while it is awake.
  bool adc_off_before_sleep_{true};
  bool auto_input_detection_{true};

  uint16_t battery_capacity_{0};
  uint8_t cell_count_{1};
  uint16_t max_charge_current_{ICHG_MAX_MA};
  float charge_voltage_limit_{NAN};
  float default_charge_current_{NAN};
  bool ship_fet_present_{false};
  bool ship_fet_action_delay_{true};
  bool battery_ocp_{false};
  uint8_t watchdog_{0};
  bool setup_ok_{false};
  // MPPT self-clears when VBUS falls below the present threshold, so the
  // requested state is remembered and re-asserted rather than assumed sticky.
  bool mppt_requested_{false};

  // What the host last asked for, kept so reconcile_ can tell drift from a deliberate change. -1 means
  // nobody has asked, and the register is left alone.
  int8_t want_en_chg_{-1};
  int8_t want_hiz_{0};
  int16_t want_vreg_code_{-1};
  int16_t want_ichg_code_{-1};
  int16_t want_iterm_code_{-1};
  int8_t want_voc_pct_{-1};
  int8_t want_voc_dly_{-1};
  int8_t want_voc_rate_{-1};
  uint32_t last_hiz_clear_{0};
  uint32_t corrections_{0};
  uint32_t last_drift_log_{0};
  // How many times each register has been found changed since boot, shown with the last correction.
  std::map<std::string, uint32_t> correction_tally_;
  bool mppt_blocked_logged_{false};
  // The flag registers clear on read, so remember when POORSRC and WD were last seen.
  uint32_t last_poor_source_{0};
  uint32_t last_watchdog_flag_{0};
  bool seen_poor_source_{false};
  bool seen_watchdog_flag_{false};
  uint8_t register_check_{1};
};

// --- Switches --------------------------------------------------------------

class BQ25798ChargingSwitch : public BQ25798Child<switch_::Switch> {
 public:
  void write_state(bool state) override {
    if (this->parent_->enable_charging(state))
      this->publish_state(state);
  }
};

class BQ25798HizSwitch : public BQ25798Child<switch_::Switch> {
 public:
  void write_state(bool state) override {
    if (this->parent_->enable_hiz(state))
      this->publish_state(state);
  }
};

class BQ25798MpptSwitch : public BQ25798Child<switch_::Switch> {
 public:
  // Apply restore_mode once the charger has started. ESPHome only restores a switch's state for components that
  // ask for it, so without this the restore_mode in the YAML does nothing. The charger clears EN_MPPT itself
  // whenever VBUS goes away, so the register cannot be the source of this switch's state: the choice is what the
  // switch was last set to, and the driver re-asserts it.
  void setup() override {
    auto initial = this->get_initial_state_with_restore_mode();
    if (!initial.has_value())
      return;
    if (*initial)
      this->turn_on();
    else
      this->turn_off();
  }

  void write_state(bool state) override {
    if (this->parent_->enable_mppt(state))
      this->publish_state(state);
  }
};

class BQ25798StatLedSwitch : public BQ25798Child<switch_::Switch> {
 public:
  void write_state(bool state) override {
    if (this->parent_->enable_stat_led(state))
      this->publish_state(state);
  }
};

class BQ25798Acdrv1Switch : public BQ25798Child<switch_::Switch> {
 public:
  void write_state(bool state) override {
    if (this->parent_->enable_acdrv(1, state))
      this->publish_state(state);
  }
};

class BQ25798Acdrv2Switch : public BQ25798Child<switch_::Switch> {
 public:
  void write_state(bool state) override {
    if (this->parent_->enable_acdrv(2, state))
      this->publish_state(state);
  }
};

class BQ25798IcoSwitch : public BQ25798Child<switch_::Switch> {
 public:
  void write_state(bool state) override {
    if (this->parent_->enable_ico(state))
      this->publish_state(state);
  }
};

// Whether the charger's ADC is switched off just before the host sleeps. No register holds this choice: it lives in the
// driver and in the restored switch state, so it survives deep sleep and reboots through restore_mode (default on).
class BQ25798AdcSleepSwitch : public BQ25798Child<switch_::Switch> {
 public:
  void setup() override {
    auto initial = this->get_initial_state_with_restore_mode();
    if (initial.has_value())
      this->write_state(*initial);
    else
      this->publish_state(this->parent_->adc_off_before_sleep());
  }

  void write_state(bool state) override {
    this->parent_->set_adc_off_before_sleep(state);
    this->publish_state(state);
  }
};

// --- Numbers ---------------------------------------------------------------

class BQ25798ChargeCurrentNumber : public BQ25798Child<number::Number> {
 public:
  void control(float value) override {
    if (this->parent_->set_charge_current(value))
      this->publish_state(value);
  }
};

class BQ25798ChargeVoltageNumber : public BQ25798Child<number::Number> {
 public:
  void control(float value) override {
    if (this->parent_->set_charge_voltage(value))
      this->publish_state(value);
  }
};

class BQ25798InputCurrentNumber : public BQ25798Child<number::Number> {
 public:
  void control(float value) override {
    if (this->parent_->set_input_current(value))
      this->publish_state(value);
  }
};

class BQ25798VindpmNumber : public BQ25798Child<number::Number> {
 public:
  void control(float value) override {
    if (this->parent_->set_vindpm(value))
      this->publish_state(value);
  }
};

class BQ25798MinSysVoltageNumber : public BQ25798Child<number::Number> {
 public:
  void control(float value) override {
    // set_min_sys_voltage() limits the value, so publish what was applied and not what was asked for.
    const float applied = std::min(value, this->parent_->min_sys_voltage_ceiling());
    if (this->parent_->set_min_sys_voltage(value))
      this->publish_state(applied);
  }
};

// --- Buttons ---------------------------------------------------------------
// The three ship-FET actions cut or cycle system power. They do nothing at
// all unless an external ship FET is populated and ship_fet_present is set.

class BQ25798ShipModeButton : public BQ25798Child<button::Button> {
 public:
  void press_action() override { this->parent_->ship_fet_action(ShipFetAction::SHIP_MODE); }
};

class BQ25798ShutdownButton : public BQ25798Child<button::Button> {
 public:
  void press_action() override { this->parent_->ship_fet_action(ShipFetAction::SHUTDOWN); }
};

class BQ25798PowerCycleButton : public BQ25798Child<button::Button> {
 public:
  void press_action() override { this->parent_->ship_fet_action(ShipFetAction::SYSTEM_POWER_RESET); }
};

class BQ25798ResetButton : public BQ25798Child<button::Button> {
 public:
  void press_action() override { this->parent_->reset_registers(); }
};

class BQ25798ClearInterruptsButton : public BQ25798Child<button::Button> {
 public:
  void press_action() override { this->parent_->clear_interrupts(); }
};

// --- Actions ---------------------------------------------------------------

template<typename... Ts> class ConfigureBatteryAction : public Action<Ts...> {
 public:
  ConfigureBatteryAction(BQ25798Component *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, chemistry)
  TEMPLATABLE_VALUE(uint16_t, battery_capacity)
  TEMPLATABLE_VALUE(uint16_t, charge_current)
  TEMPLATABLE_VALUE(bool, ts_ignore)

  void play(Ts... x) override {
    std::string chemistry = this->chemistry_.has_value() ? this->chemistry_.value(x...) : std::string();
    int capacity = this->battery_capacity_.has_value() ? this->battery_capacity_.value(x...) : 0;
    int current = this->charge_current_.has_value() ? this->charge_current_.value(x...) : 0;
    int ts_ignore = this->ts_ignore_.has_value() ? (this->ts_ignore_.value(x...) ? 1 : 0) : -1;
    this->parent_->configure_battery(chemistry, capacity, current, ts_ignore);
  }

 protected:
  BQ25798Component *parent_;
};

template<typename... Ts> class ClearInterruptsAction : public Action<Ts...> {
 public:
  ClearInterruptsAction(BQ25798Component *parent) : parent_(parent) {}
  void play(Ts... x) override { this->parent_->clear_interrupts(); }

 protected:
  BQ25798Component *parent_;
};

// --- Selects (MPPT tuning, REG15) ------------------------------------------

class BQ25798InputSelect : public BQ25798Child<select::Select> {
 public:
  void control(const std::string &value) override {
    auto index = this->index_of(value);
    if (index.has_value() && this->parent_->set_input_selection(*index))
      this->publish_state(value);
  }
};

class BQ25798VocRatioSelect : public BQ25798Child<select::Select> {
 public:
  void control(const std::string &value) override {
    auto index = this->index_of(value);
    if (index.has_value() && this->parent_->set_voc_ratio(*index))
      this->publish_state(value);
  }
};

class BQ25798VocDelaySelect : public BQ25798Child<select::Select> {
 public:
  void control(const std::string &value) override {
    auto index = this->index_of(value);
    if (index.has_value() && this->parent_->set_voc_delay(*index))
      this->publish_state(value);
  }
};

class BQ25798VocRateSelect : public BQ25798Child<select::Select> {
 public:
  void control(const std::string &value) override {
    auto index = this->index_of(value);
    if (index.has_value() && this->parent_->set_voc_rate(*index))
      this->publish_state(value);
  }
};

}  // namespace bq25798
}  // namespace esphome
