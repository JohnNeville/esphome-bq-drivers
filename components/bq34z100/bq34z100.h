#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/core/log.h"

namespace esphome {
namespace bq34z100 {

// Fixed 7-bit I2C address; the BQ34Z100 has no address strap.
static const uint8_t BQ34Z100_ADDR = 0x55;

// --- Standard commands (datasheet SLUSAU1C, Table 7-1) ---------------------
static const uint8_t CMD_CONTROL = 0x00;
static const uint8_t CMD_STATE_OF_CHARGE = 0x02;
static const uint8_t CMD_REMAINING_CAPACITY = 0x04;
static const uint8_t CMD_FULL_CHARGE_CAPACITY = 0x06;
static const uint8_t CMD_VOLTAGE = 0x08;
static const uint8_t CMD_AVERAGE_CURRENT = 0x0A;
static const uint8_t CMD_TEMPERATURE = 0x0C;
static const uint8_t CMD_FLAGS = 0x0E;

// --- Extended commands (Table 7-6) -----------------------------------------
// 0x10/0x11 is AtRate() on write and Current() on read. There is no FlagsB
// register on this part, unlike several of its siblings.
static const uint8_t CMD_CURRENT = 0x10;
static const uint8_t CMD_NOMINAL_AVAILABLE_CAPACITY = 0x14;
static const uint8_t CMD_FULL_AVAILABLE_CAPACITY = 0x16;
static const uint8_t CMD_TIME_TO_EMPTY = 0x18;
static const uint8_t CMD_TIME_TO_FULL = 0x1A;
static const uint8_t CMD_AVAILABLE_ENERGY = 0x24;
static const uint8_t CMD_AVERAGE_POWER = 0x26;
static const uint8_t CMD_INTERNAL_TEMP = 0x2A;
static const uint8_t CMD_CYCLE_COUNT = 0x2C;
static const uint8_t CMD_STATE_OF_HEALTH = 0x2E;
static const uint8_t CMD_CHARGE_VOLTAGE = 0x30;
static const uint8_t CMD_CHARGE_CURRENT = 0x32;
static const uint8_t CMD_PACK_CONFIGURATION = 0x3A;
static const uint8_t CMD_DESIGN_CAPACITY = 0x3C;
static const uint8_t CMD_DATA_FLASH_CLASS = 0x3E;
static const uint8_t CMD_DATA_FLASH_BLOCK = 0x3F;
static const uint8_t CMD_BLOCK_DATA = 0x40;
static const uint8_t CMD_BLOCK_DATA_CHECKSUM = 0x60;
static const uint8_t CMD_BLOCK_DATA_CONTROL = 0x61;

// --- Control() subcommands (Table 7-2) -------------------------------------
static const uint16_t CTRL_CONTROL_STATUS = 0x0000;
static const uint16_t CTRL_DEVICE_TYPE = 0x0001;
static const uint16_t CTRL_FW_VERSION = 0x0002;
static const uint16_t CTRL_HW_VERSION = 0x0003;
static const uint16_t CTRL_CHEM_ID = 0x0008;
static const uint16_t CTRL_BOARD_OFFSET = 0x0009;
static const uint16_t CTRL_CC_OFFSET = 0x000A;
static const uint16_t CTRL_CC_OFFSET_SAVE = 0x000B;
static const uint16_t CTRL_DF_VERSION = 0x000C;
static const uint16_t CTRL_STATIC_CHEM_CHKSUM = 0x0017;
static const uint16_t CTRL_SEALED = 0x0020;
static const uint16_t CTRL_IT_ENABLE = 0x0021;
static const uint16_t CTRL_RESET = 0x0041;
static const uint16_t CTRL_EXIT_CAL = 0x0080;
static const uint16_t CTRL_ENTER_CAL = 0x0081;
static const uint16_t CTRL_OFFSET_CAL = 0x0082;

// DEVICE_TYPE response that identifies this part.
static const uint16_t DEVICE_TYPE_BQ34Z100 = 0x0541;

// --- Data flash locations (Table 7-8) --------------------------------------
static const uint8_t SUBCLASS_DATA = 48;           // Design Capacity / Energy
static const uint8_t OFFSET_DESIGN_CAPACITY = 11;  // I2, mAh
static const uint8_t OFFSET_DESIGN_ENERGY = 13;    // I2, mWh

static const uint8_t SUBCLASS_REGISTERS = 64;
static const uint8_t OFFSET_PACK_CONFIGURATION = 0;    // H2
static const uint8_t OFFSET_PACK_CONFIGURATION_B = 2;  // H1
static const uint8_t OFFSET_NUMBER_OF_SERIES_CELL = 7; // U1

static const uint8_t SUBCLASS_CALIBRATION = 104;
static const uint8_t OFFSET_CC_GAIN = 0;         // F4, TI float
static const uint8_t OFFSET_CC_DELTA = 4;        // F4, TI float
static const uint8_t OFFSET_VOLTAGE_DIVIDER = 14;  // U2, mV full scale

// Pack Configuration bits (Table 7-14). The table is laid out as a 16-bit
// word with the "High Byte" row occupying bits 15..8.
static const uint16_t PACK_CFG_VOLTSEL = 1 << 11;  // 1 = external divider
static const uint16_t PACK_CFG_TEMPS = 1 << 0;     // 1 = external thermistor

// Pack Configuration B bits (Table 7-15). Both of these only take effect
// when the gauge is running a 400-series (LiFePO4) chemistry.
static const uint8_t PACK_CFG_B_LFP_RELAX = 1 << 2;
static const uint8_t PACK_CFG_B_DOD_WT = 1 << 1;

// CC Gain and CC Delta are both derived from the sense resistance. The two
// numerators come from the datasheet's own calibration table, where the
// defaults of 0.47095 and 5.595E5 correspond to a 10.124 mOhm shunt.
static const float CC_GAIN_NUMERATOR = 4.768f;
static const float CC_DELTA_NUMERATOR = 5677445.6f;

// Flags() bits (Table 7-5)
static const uint16_t FLAG_OTC = 1 << 15;
static const uint16_t FLAG_OTD = 1 << 14;
static const uint16_t FLAG_BATHI = 1 << 13;
static const uint16_t FLAG_BATLOW = 1 << 12;
static const uint16_t FLAG_CHG_INH = 1 << 11;
static const uint16_t FLAG_FC = 1 << 9;
static const uint16_t FLAG_CHG = 1 << 8;
static const uint16_t FLAG_OCVTAKEN = 1 << 7;
static const uint16_t FLAG_ISD = 1 << 6;
static const uint16_t FLAG_TDD = 1 << 5;
static const uint16_t FLAG_SOC1 = 1 << 2;
static const uint16_t FLAG_SOCF = 1 << 1;
static const uint16_t FLAG_DSG = 1 << 0;

class BQ34Z100Component;

template<typename T> class BQ34Z100Child : public T, public Component {
 public:
  void set_parent(BQ34Z100Component *parent) { this->parent_ = parent; }

 protected:
  BQ34Z100Component *parent_{nullptr};
};

// One entry per data flash subclass the datasheet's Table 7-8 documents, with
// the number of 32-byte blocks needed to cover its highest offset. Used by the
// data flash dump.
struct DataFlashSubclass {
  uint8_t subclass;
  uint8_t blocks;
};

class BQ34Z100CcOffsetButton;
class BQ34Z100BoardOffsetButton;
class BQ34Z100ItEnableButton;
class BQ34Z100ResetButton;
class BQ34Z100DumpButton;
class BQ34Z100ClearAlertButton;

class BQ34Z100Component : public PollingComponent, public i2c::I2CDevice {
 public:
  void setup() override;
  void update() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_state_of_charge_sensor(sensor::Sensor *s) { state_of_charge_sensor_ = s; }
  void set_voltage_sensor(sensor::Sensor *s) { voltage_sensor_ = s; }
  void set_current_sensor(sensor::Sensor *s) { current_sensor_ = s; }
  void set_average_current_sensor(sensor::Sensor *s) { average_current_sensor_ = s; }
  void set_average_power_sensor(sensor::Sensor *s) { average_power_sensor_ = s; }
  void set_temperature_sensor(sensor::Sensor *s) { temperature_sensor_ = s; }
  void set_internal_temperature_sensor(sensor::Sensor *s) { internal_temperature_sensor_ = s; }
  void set_remaining_capacity_sensor(sensor::Sensor *s) { remaining_capacity_sensor_ = s; }
  void set_full_charge_capacity_sensor(sensor::Sensor *s) { full_charge_capacity_sensor_ = s; }
  void set_available_energy_sensor(sensor::Sensor *s) { available_energy_sensor_ = s; }
  void set_time_to_empty_sensor(sensor::Sensor *s) { time_to_empty_sensor_ = s; }
  void set_time_to_full_sensor(sensor::Sensor *s) { time_to_full_sensor_ = s; }
  void set_cycle_count_sensor(sensor::Sensor *s) { cycle_count_sensor_ = s; }
  void set_state_of_health_sensor(sensor::Sensor *s) { state_of_health_sensor_ = s; }
  void set_chem_id_sensor(sensor::Sensor *s) { chem_id_sensor_ = s; }

  void set_discharging_sensor(binary_sensor::BinarySensor *s) { discharging_sensor_ = s; }
  void set_fully_charged_sensor(binary_sensor::BinarySensor *s) { fully_charged_sensor_ = s; }
  void set_charge_inhibited_sensor(binary_sensor::BinarySensor *s) { charge_inhibited_sensor_ = s; }
  void set_low_charge_sensor(binary_sensor::BinarySensor *s) { low_charge_sensor_ = s; }
  void set_critical_charge_sensor(binary_sensor::BinarySensor *s) { critical_charge_sensor_ = s; }
  void set_battery_high_sensor(binary_sensor::BinarySensor *s) { battery_high_sensor_ = s; }
  void set_battery_low_sensor(binary_sensor::BinarySensor *s) { battery_low_sensor_ = s; }
  void set_over_temperature_sensor(binary_sensor::BinarySensor *s) { over_temperature_sensor_ = s; }

  void set_status_text_sensor(text_sensor::TextSensor *s) { status_text_sensor_ = s; }

  void set_cc_offset_button(BQ34Z100CcOffsetButton *b) { cc_offset_button_ = b; }
  void set_board_offset_button(BQ34Z100BoardOffsetButton *b) { board_offset_button_ = b; }
  void set_it_enable_button(BQ34Z100ItEnableButton *b) { it_enable_button_ = b; }
  void set_reset_button(BQ34Z100ResetButton *b) { reset_button_ = b; }
  void set_dump_button(BQ34Z100DumpButton *b) { dump_button_ = b; }
  void set_clear_alert_button(BQ34Z100ClearAlertButton *b) { clear_alert_button_ = b; }

  // --- Data flash configuration ---
  // Nothing below is written unless apply_configuration is true. Data flash
  // has limited write endurance, so the component reads each value first and
  // writes only what actually differs.
  void set_apply_configuration(bool apply) { apply_configuration_flag_ = apply; }
  void set_unseal_keys(uint16_t key0, uint16_t key1) {
    unseal_key0_ = key0;
    unseal_key1_ = key1;
  }
  void set_design_capacity(int16_t mah) { design_capacity_ = mah; }
  void set_design_energy(int16_t mwh) { design_energy_ = mwh; }
  void set_cell_count(uint8_t count) { cell_count_ = count; }
  void set_sense_resistor(float milliohms) { sense_resistor_ = milliohms; }
  void set_voltage_divider(uint16_t mv) { voltage_divider_ = mv; }
  void set_external_thermistor(bool external) { external_thermistor_ = external; }
  void set_lifepo4_relax(bool enable) { lifepo4_relax_ = enable; }
  // Purely advisory: the chemistry table itself cannot be programmed over
  // I2C, so all this does is warn when the gauge is not running the
  // chemistry the configuration expects.
  void set_expected_chem_id(uint16_t chem_id) { expected_chem_id_ = chem_id; }

  // Called by the child buttons.
  bool run_cc_offset_calibration();
  bool run_board_offset_calibration();
  bool enable_impedance_track();
  bool reset_gauge();
  bool clear_alert();
  // Reads every documented data flash subclass and logs it as hex. Intended
  // for capturing a golden image from a gauge that has been programmed with
  // bqStudio, so the same values can be replayed to other boards over I2C.
  bool dump_data_flash();

 protected:
  bool read_word_(uint8_t cmd, uint16_t &value);
  bool write_word_(uint8_t cmd, uint16_t value);
  bool control_command_(uint16_t subcommand);
  bool control_read_(uint16_t subcommand, uint16_t &value);

  bool unseal_();
  bool seal_();
  bool read_flash_block_(uint8_t subclass, uint8_t block, uint8_t *data);
  bool write_flash_block_(uint8_t subclass, uint8_t block, const uint8_t *data);
  bool update_flash_bytes_(uint8_t subclass, uint8_t offset, const uint8_t *value, uint8_t length);
  bool read_flash_bytes_(uint8_t subclass, uint8_t offset, uint8_t *value, uint8_t length);

  void apply_configuration_();
  bool apply_pack_configuration_();
  bool apply_sense_resistor_();

  void publish_word_(sensor::Sensor *s, uint8_t cmd, float scale, bool is_signed);

  // TI's gauges store F4 data-flash items in a proprietary 4-byte float
  // format, not IEEE 754. Every write is verified by reading the value back
  // and decoding it, so a conversion error shows up in the log instead of
  // silently miscalibrating the shunt.
  static void float_to_ti_(float value, uint8_t *out);
  static float ti_to_float_(const uint8_t *in);

  sensor::Sensor *state_of_charge_sensor_{nullptr};
  sensor::Sensor *voltage_sensor_{nullptr};
  sensor::Sensor *current_sensor_{nullptr};
  sensor::Sensor *average_current_sensor_{nullptr};
  sensor::Sensor *average_power_sensor_{nullptr};
  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *internal_temperature_sensor_{nullptr};
  sensor::Sensor *remaining_capacity_sensor_{nullptr};
  sensor::Sensor *full_charge_capacity_sensor_{nullptr};
  sensor::Sensor *available_energy_sensor_{nullptr};
  sensor::Sensor *time_to_empty_sensor_{nullptr};
  sensor::Sensor *time_to_full_sensor_{nullptr};
  sensor::Sensor *cycle_count_sensor_{nullptr};
  sensor::Sensor *state_of_health_sensor_{nullptr};
  sensor::Sensor *chem_id_sensor_{nullptr};

  binary_sensor::BinarySensor *discharging_sensor_{nullptr};
  binary_sensor::BinarySensor *fully_charged_sensor_{nullptr};
  binary_sensor::BinarySensor *charge_inhibited_sensor_{nullptr};
  binary_sensor::BinarySensor *low_charge_sensor_{nullptr};
  binary_sensor::BinarySensor *critical_charge_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_high_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_low_sensor_{nullptr};
  binary_sensor::BinarySensor *over_temperature_sensor_{nullptr};

  text_sensor::TextSensor *status_text_sensor_{nullptr};

  BQ34Z100CcOffsetButton *cc_offset_button_{nullptr};
  BQ34Z100BoardOffsetButton *board_offset_button_{nullptr};
  BQ34Z100ItEnableButton *it_enable_button_{nullptr};
  BQ34Z100ResetButton *reset_button_{nullptr};
  BQ34Z100DumpButton *dump_button_{nullptr};
  BQ34Z100ClearAlertButton *clear_alert_button_{nullptr};

  bool apply_configuration_flag_{false};
  uint16_t unseal_key0_{0x0414};
  uint16_t unseal_key1_{0x3672};
  int16_t design_capacity_{-1};
  int16_t design_energy_{-1};
  uint8_t cell_count_{0};
  float sense_resistor_{0.0f};
  uint16_t voltage_divider_{0};
  bool external_thermistor_{true};
  bool lifepo4_relax_{false};
  uint16_t expected_chem_id_{0};
  bool setup_ok_{false};
};

class BQ34Z100CcOffsetButton : public BQ34Z100Child<button::Button> {
 public:
  void press_action() override { this->parent_->run_cc_offset_calibration(); }
};

class BQ34Z100BoardOffsetButton : public BQ34Z100Child<button::Button> {
 public:
  void press_action() override { this->parent_->run_board_offset_calibration(); }
};

class BQ34Z100ItEnableButton : public BQ34Z100Child<button::Button> {
 public:
  void press_action() override { this->parent_->enable_impedance_track(); }
};

class BQ34Z100ResetButton : public BQ34Z100Child<button::Button> {
 public:
  void press_action() override { this->parent_->reset_gauge(); }
};

class BQ34Z100DumpButton : public BQ34Z100Child<button::Button> {
 public:
  void press_action() override { this->parent_->dump_data_flash(); }
};

class BQ34Z100ClearAlertButton : public BQ34Z100Child<button::Button> {
 public:
  void press_action() override { this->parent_->clear_alert(); }
};

// --- Actions ---------------------------------------------------------------

template<typename... Ts> class ClearAlertAction : public Action<Ts...> {
 public:
  ClearAlertAction(BQ34Z100Component *parent) : parent_(parent) {}
  void play(Ts... x) override { this->parent_->clear_alert(); }

 protected:
  BQ34Z100Component *parent_;
};

}  // namespace bq34z100
}  // namespace esphome
