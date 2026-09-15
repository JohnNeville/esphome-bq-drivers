#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
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
class BQ25798ChargeCurrentNumber;
class BQ25798ChargeVoltageNumber;
class BQ25798InputCurrentNumber;
class BQ25798VindpmNumber;
class BQ25798MinSysVoltageNumber;
class BQ25798ShipModeButton;
class BQ25798ShutdownButton;
class BQ25798PowerCycleButton;
class BQ25798ResetButton;
class BQ25798VocRatioSelect;
class BQ25798VocDelaySelect;
class BQ25798VocRateSelect;

class BQ25798Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
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

  void set_vbus_present_sensor(binary_sensor::BinarySensor *s) { vbus_present_sensor_ = s; }
  void set_vac1_present_sensor(binary_sensor::BinarySensor *s) { vac1_present_sensor_ = s; }
  void set_vac2_present_sensor(binary_sensor::BinarySensor *s) { vac2_present_sensor_ = s; }
  void set_power_good_sensor(binary_sensor::BinarySensor *s) { power_good_sensor_ = s; }
  void set_charging_sensor(binary_sensor::BinarySensor *s) { charging_sensor_ = s; }
  void set_battery_present_sensor(binary_sensor::BinarySensor *s) { battery_present_sensor_ = s; }
  void set_thermal_regulation_sensor(binary_sensor::BinarySensor *s) { thermal_regulation_sensor_ = s; }

  void set_charge_status_text_sensor(text_sensor::TextSensor *s) { charge_status_text_sensor_ = s; }
  void set_input_source_text_sensor(text_sensor::TextSensor *s) { input_source_text_sensor_ = s; }
  void set_charger_fault_text_sensor(text_sensor::TextSensor *s) { charger_fault_text_sensor_ = s; }

  void set_charging_switch(BQ25798ChargingSwitch *s) { charging_switch_ = s; }
  void set_hiz_switch(BQ25798HizSwitch *s) { hiz_switch_ = s; }
  void set_mppt_switch(BQ25798MpptSwitch *s) { mppt_switch_ = s; }
  void set_stat_led_switch(BQ25798StatLedSwitch *s) { stat_led_switch_ = s; }
  void set_acdrv1_switch(BQ25798Acdrv1Switch *s) { acdrv1_switch_ = s; }
  void set_acdrv2_switch(BQ25798Acdrv2Switch *s) { acdrv2_switch_ = s; }
  void set_ico_switch(BQ25798IcoSwitch *s) { ico_switch_ = s; }

  void set_charge_current_number(BQ25798ChargeCurrentNumber *n) { charge_current_number_ = n; }
  void set_charge_voltage_number(BQ25798ChargeVoltageNumber *n) { charge_voltage_number_ = n; }
  void set_input_current_number(BQ25798InputCurrentNumber *n) { input_current_number_ = n; }
  void set_vindpm_number(BQ25798VindpmNumber *n) { vindpm_number_ = n; }
  void set_min_sys_voltage_number(BQ25798MinSysVoltageNumber *n) { min_sys_voltage_number_ = n; }

  void set_voc_ratio_select(BQ25798VocRatioSelect *s) { voc_ratio_select_ = s; }
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
  // Hard ceiling applied to every charge-current write, including the one
  // restored at boot. Boards whose copper is rated below the IC's 5 A must
  // set this; see the SunSprout hub, which is limited to 2000 mA.
  void set_max_charge_current(uint16_t ma) { max_charge_current_ = ma; }
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
  bool enable_mppt(bool enable);
  bool enable_stat_led(bool enable);
  bool enable_acdrv(uint8_t index, bool enable);
  bool enable_ico(bool enable);
  bool set_charge_current(float current_ma);
  bool set_charge_voltage(float voltage_v);
  bool set_input_current(float current_ma);
  bool set_vindpm(float voltage_v);
  bool set_min_sys_voltage(float voltage_v);
  bool set_voc_ratio(size_t index);
  bool set_voc_delay(size_t index);
  bool set_voc_rate(size_t index);
  bool ship_fet_action(ShipFetAction action);
  bool reset_registers();

 protected:
  bool read_u8_(uint8_t reg, uint8_t &value);
  bool write_u8_(uint8_t reg, uint8_t value);
  bool read_u16_(uint8_t reg, uint16_t &value);
  bool write_u16_(uint8_t reg, uint16_t value);
  bool read_s16_(uint8_t reg, int16_t &value);
  bool modify_u8_(uint8_t reg, uint8_t mask, uint8_t value);

  void configure_adc_();
  void configure_defaults_();
  void publish_control_states_();
  void kick_watchdog_();

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

  binary_sensor::BinarySensor *vbus_present_sensor_{nullptr};
  binary_sensor::BinarySensor *vac1_present_sensor_{nullptr};
  binary_sensor::BinarySensor *vac2_present_sensor_{nullptr};
  binary_sensor::BinarySensor *power_good_sensor_{nullptr};
  binary_sensor::BinarySensor *charging_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_present_sensor_{nullptr};
  binary_sensor::BinarySensor *thermal_regulation_sensor_{nullptr};

  text_sensor::TextSensor *charge_status_text_sensor_{nullptr};
  text_sensor::TextSensor *input_source_text_sensor_{nullptr};
  text_sensor::TextSensor *charger_fault_text_sensor_{nullptr};

  BQ25798ChargingSwitch *charging_switch_{nullptr};
  BQ25798HizSwitch *hiz_switch_{nullptr};
  BQ25798MpptSwitch *mppt_switch_{nullptr};
  BQ25798StatLedSwitch *stat_led_switch_{nullptr};
  BQ25798Acdrv1Switch *acdrv1_switch_{nullptr};
  BQ25798Acdrv2Switch *acdrv2_switch_{nullptr};
  BQ25798IcoSwitch *ico_switch_{nullptr};

  BQ25798ChargeCurrentNumber *charge_current_number_{nullptr};
  BQ25798ChargeVoltageNumber *charge_voltage_number_{nullptr};
  BQ25798InputCurrentNumber *input_current_number_{nullptr};
  BQ25798VindpmNumber *vindpm_number_{nullptr};
  BQ25798MinSysVoltageNumber *min_sys_voltage_number_{nullptr};

  BQ25798VocRatioSelect *voc_ratio_select_{nullptr};
  BQ25798VocDelaySelect *voc_delay_select_{nullptr};
  BQ25798VocRateSelect *voc_rate_select_{nullptr};

  float ts_upper_ohms_{0.0f};
  float ts_lower_ohms_{0.0f};
  float ts_nominal_ohms_{10000.0f};
  float ts_beta_{3435.0f};

  uint16_t battery_capacity_{0};
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
    if (this->parent_->set_min_sys_voltage(value))
      this->publish_state(value);
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

// --- Selects (MPPT tuning, REG15) ------------------------------------------

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
