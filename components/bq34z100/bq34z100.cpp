#include "bq34z100.h"

#include <cmath>

#include "esphome/core/helpers.h"

namespace esphome {
namespace bq34z100 {

static const char *const TAG = "bq34z100";

// Data flash is written a 32-byte block at a time.
static const uint8_t FLASH_BLOCK_SIZE = 32;
// The datasheet asks for a settling delay after each data flash write.
static const uint32_t FLASH_WRITE_DELAY_MS = 200;
static const uint32_t FLASH_SETUP_DELAY_MS = 5;
// 65535 means "not computable yet" for both of the time-remaining commands.
static const uint16_t TIME_UNKNOWN = 0xFFFF;

// ---------------------------------------------------------------------------
// Low-level access
// ---------------------------------------------------------------------------

// Every word on this part is little-endian: the low byte comes first.
bool BQ34Z100Component::read_word_(uint8_t cmd, uint16_t &value) {
  uint8_t buf[2];
  if (!this->read_bytes(cmd, buf, 2)) {
    ESP_LOGW(TAG, "Read of command 0x%02X failed", cmd);
    return false;
  }
  value = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
  return true;
}

bool BQ34Z100Component::write_word_(uint8_t cmd, uint16_t value) {
  uint8_t buf[2] = {static_cast<uint8_t>(value & 0xFF), static_cast<uint8_t>(value >> 8)};
  if (!this->write_bytes(cmd, buf, 2)) {
    ESP_LOGW(TAG, "Write of 0x%04X to command 0x%02X failed", value, cmd);
    return false;
  }
  return true;
}

bool BQ34Z100Component::control_command_(uint16_t subcommand) {
  return this->write_word_(CMD_CONTROL, subcommand);
}

bool BQ34Z100Component::control_read_(uint16_t subcommand, uint16_t &value) {
  if (!this->control_command_(subcommand))
    return false;
  delay(FLASH_SETUP_DELAY_MS);
  return this->read_word_(CMD_CONTROL, value);
}

// ---------------------------------------------------------------------------
// TI's proprietary 4-byte float format for F4 data flash items
// ---------------------------------------------------------------------------
//
// Layout is [exponent+128, mantissa_hi | sign, mantissa_mid, mantissa_lo],
// where the value is (M + 128) * 2^(exponent - 8) and M is the mantissa read
// as a fixed-point number. It is not IEEE 754 and the datasheet does not
// describe it, so every write made with it is verified by reading back.

void BQ34Z100Component::float_to_ti_(float value, uint8_t *out) {
  bool negative = value < 0.0f;
  float magnitude = fabsf(value);

  int exponent = 0;
  // frexpf yields magnitude = mantissa * 2^exponent with mantissa in
  // [0.5, 1), which is exactly the normalisation this format wants.
  float mantissa = frexpf(magnitude, &exponent);
  if (magnitude == 0.0f) {
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    return;
  }
  exponent = clamp(exponent, -128, 127);

  float scaled = 256.0f * mantissa - 128.0f;  // lands in [0, 128)
  uint8_t hi = static_cast<uint8_t>(scaled);
  scaled = 256.0f * (scaled - hi);
  uint8_t mid = static_cast<uint8_t>(scaled);
  scaled = 256.0f * (scaled - mid);
  uint8_t lo = static_cast<uint8_t>(lroundf(scaled));

  out[0] = static_cast<uint8_t>(exponent + 128);
  out[1] = negative ? (hi | 0x80) : (hi & 0x7F);
  out[2] = mid;
  out[3] = lo;
}

float BQ34Z100Component::ti_to_float_(const uint8_t *in) {
  int exponent = static_cast<int>(in[0]) - 128;
  bool negative = (in[1] & 0x80) != 0;
  float mantissa = static_cast<float>(in[1] & 0x7F) + in[2] / 256.0f + in[3] / 65536.0f;
  float value = (mantissa + 128.0f) * ldexpf(1.0f, exponent - 8);
  return negative ? -value : value;
}

// ---------------------------------------------------------------------------
// Data flash access
// ---------------------------------------------------------------------------

bool BQ34Z100Component::unseal_() {
  // The two key words are sent back to back through Control() with nothing
  // in between. Both default to TI's factory values unless the pack was
  // programmed with its own.
  if (!this->control_command_(this->unseal_key0_))
    return false;
  if (!this->control_command_(this->unseal_key1_))
    return false;
  delay(FLASH_SETUP_DELAY_MS);

  uint16_t status;
  if (!this->control_read_(CTRL_CONTROL_STATUS, status))
    return false;
  // [SS] is bit 13 of CONTROL_STATUS and is clear once the part is unsealed.
  if ((status & 0x2000) != 0) {
    ESP_LOGE(TAG, "Unseal failed (CONTROL_STATUS 0x%04X) - check the unseal keys", status);
    return false;
  }
  return true;
}

bool BQ34Z100Component::seal_() { return this->control_command_(CTRL_SEALED); }

bool BQ34Z100Component::read_flash_block_(uint8_t subclass, uint8_t block, uint8_t *data) {
  // 0x00 selects data flash rather than the authentication block.
  if (!this->write_byte(CMD_BLOCK_DATA_CONTROL, 0x00))
    return false;
  if (!this->write_byte(CMD_DATA_FLASH_CLASS, subclass))
    return false;
  if (!this->write_byte(CMD_DATA_FLASH_BLOCK, block))
    return false;
  delay(FLASH_SETUP_DELAY_MS);
  return this->read_bytes(CMD_BLOCK_DATA, data, FLASH_BLOCK_SIZE);
}

bool BQ34Z100Component::write_flash_block_(uint8_t subclass, uint8_t block, const uint8_t *data) {
  if (!this->write_byte(CMD_BLOCK_DATA_CONTROL, 0x00))
    return false;
  if (!this->write_byte(CMD_DATA_FLASH_CLASS, subclass))
    return false;
  if (!this->write_byte(CMD_DATA_FLASH_BLOCK, block))
    return false;
  delay(FLASH_SETUP_DELAY_MS);
  if (!this->write_bytes(CMD_BLOCK_DATA, data, FLASH_BLOCK_SIZE))
    return false;

  // The gauge only commits the block once a matching checksum arrives.
  uint8_t sum = 0;
  for (uint8_t i = 0; i < FLASH_BLOCK_SIZE; i++)
    sum += data[i];
  if (!this->write_byte(CMD_BLOCK_DATA_CHECKSUM, static_cast<uint8_t>(255 - sum)))
    return false;
  delay(FLASH_WRITE_DELAY_MS);
  return true;
}

bool BQ34Z100Component::read_flash_bytes_(uint8_t subclass, uint8_t offset, uint8_t *value, uint8_t length) {
  uint8_t block = offset / FLASH_BLOCK_SIZE;
  uint8_t index = offset % FLASH_BLOCK_SIZE;
  if (index + length > FLASH_BLOCK_SIZE) {
    ESP_LOGE(TAG, "Data flash field at subclass %u offset %u straddles a block boundary", subclass, offset);
    return false;
  }
  uint8_t buffer[FLASH_BLOCK_SIZE];
  if (!this->read_flash_block_(subclass, block, buffer))
    return false;
  memcpy(value, buffer + index, length);
  return true;
}

bool BQ34Z100Component::update_flash_bytes_(uint8_t subclass, uint8_t offset, const uint8_t *value, uint8_t length) {
  uint8_t block = offset / FLASH_BLOCK_SIZE;
  uint8_t index = offset % FLASH_BLOCK_SIZE;
  if (index + length > FLASH_BLOCK_SIZE) {
    ESP_LOGE(TAG, "Data flash field at subclass %u offset %u straddles a block boundary", subclass, offset);
    return false;
  }

  uint8_t buffer[FLASH_BLOCK_SIZE];
  if (!this->read_flash_block_(subclass, block, buffer))
    return false;

  // Flash endurance is finite, so an unchanged value is never rewritten.
  if (memcmp(buffer + index, value, length) == 0) {
    ESP_LOGD(TAG, "Subclass %u offset %u already holds the wanted value", subclass, offset);
    return true;
  }

  memcpy(buffer + index, value, length);
  if (!this->write_flash_block_(subclass, block, buffer))
    return false;

  // Read back rather than trust the write, so a bad checksum or a botched
  // encoding is caught here instead of showing up as a wrong reading later.
  uint8_t verify[FLASH_BLOCK_SIZE];
  if (!this->read_flash_block_(subclass, block, verify))
    return false;
  if (memcmp(verify + index, value, length) != 0) {
    ESP_LOGE(TAG, "Data flash write to subclass %u offset %u did not stick", subclass, offset);
    return false;
  }
  ESP_LOGI(TAG, "Data flash subclass %u offset %u updated", subclass, offset);
  return true;
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void BQ34Z100Component::apply_configuration_() {
  if (!this->unseal_()) {
    ESP_LOGE(TAG, "Could not unseal the gauge; skipping data flash configuration");
    return;
  }

  if (this->design_capacity_ >= 0) {
    uint8_t buf[2] = {static_cast<uint8_t>(this->design_capacity_ >> 8),
                      static_cast<uint8_t>(this->design_capacity_ & 0xFF)};
    this->update_flash_bytes_(SUBCLASS_DATA, OFFSET_DESIGN_CAPACITY, buf, 2);
  }

  if (this->design_energy_ >= 0) {
    uint8_t buf[2] = {static_cast<uint8_t>(this->design_energy_ >> 8),
                      static_cast<uint8_t>(this->design_energy_ & 0xFF)};
    this->update_flash_bytes_(SUBCLASS_DATA, OFFSET_DESIGN_ENERGY, buf, 2);
  }

  if (this->cell_count_ > 0) {
    uint8_t buf[1] = {this->cell_count_};
    this->update_flash_bytes_(SUBCLASS_REGISTERS, OFFSET_NUMBER_OF_SERIES_CELL, buf, 1);
  }

  if (this->voltage_divider_ > 0) {
    uint8_t buf[2] = {static_cast<uint8_t>(this->voltage_divider_ >> 8),
                      static_cast<uint8_t>(this->voltage_divider_ & 0xFF)};
    this->update_flash_bytes_(SUBCLASS_CALIBRATION, OFFSET_VOLTAGE_DIVIDER, buf, 2);
  }

  this->apply_pack_configuration_();
  this->apply_sense_resistor_();

  this->seal_();
}

bool BQ34Z100Component::apply_pack_configuration_() {
  uint8_t raw[2];
  if (!this->read_flash_bytes_(SUBCLASS_REGISTERS, OFFSET_PACK_CONFIGURATION, raw, 2))
    return false;
  uint16_t config = (static_cast<uint16_t>(raw[0]) << 8) | raw[1];

  // VOLTSEL picks the external divider. The internal one only covers a
  // single cell, so anything with a divider network has to select it.
  if (this->voltage_divider_ > 0) {
    config |= PACK_CFG_VOLTSEL;
  } else {
    config &= ~PACK_CFG_VOLTSEL;
  }
  if (this->external_thermistor_) {
    config |= PACK_CFG_TEMPS;
  } else {
    config &= ~PACK_CFG_TEMPS;
  }

  uint8_t updated[2] = {static_cast<uint8_t>(config >> 8), static_cast<uint8_t>(config & 0xFF)};
  bool ok = this->update_flash_bytes_(SUBCLASS_REGISTERS, OFFSET_PACK_CONFIGURATION, updated, 2);

  if (this->lifepo4_relax_) {
    uint8_t config_b;
    if (this->read_flash_bytes_(SUBCLASS_REGISTERS, OFFSET_PACK_CONFIGURATION_B, &config_b, 1)) {
      // LFPRelax and DoDWT only do anything once the gauge is running a
      // 400-series chemistry, which cannot be loaded over I2C.
      uint8_t wanted = config_b | PACK_CFG_B_LFP_RELAX | PACK_CFG_B_DOD_WT;
      ok &= this->update_flash_bytes_(SUBCLASS_REGISTERS, OFFSET_PACK_CONFIGURATION_B, &wanted, 1);
    }
  }
  return ok;
}

bool BQ34Z100Component::apply_sense_resistor_() {
  if (this->sense_resistor_ <= 0.0f)
    return true;

  float cc_gain = CC_GAIN_NUMERATOR / this->sense_resistor_;
  float cc_delta = CC_DELTA_NUMERATOR / this->sense_resistor_;

  uint8_t gain_bytes[4];
  uint8_t delta_bytes[4];
  float_to_ti_(cc_gain, gain_bytes);
  float_to_ti_(cc_delta, delta_bytes);

  bool ok = this->update_flash_bytes_(SUBCLASS_CALIBRATION, OFFSET_CC_GAIN, gain_bytes, 4);
  ok &= this->update_flash_bytes_(SUBCLASS_CALIBRATION, OFFSET_CC_DELTA, delta_bytes, 4);

  // Decode what the gauge actually holds now and log it, so a conversion
  // bug is visible without a bench setup.
  uint8_t verify[4];
  if (this->read_flash_bytes_(SUBCLASS_CALIBRATION, OFFSET_CC_GAIN, verify, 4)) {
    float stored = ti_to_float_(verify);
    ESP_LOGI(TAG, "CC Gain for a %.3f mOhm shunt: wanted %.6f, gauge holds %.6f", this->sense_resistor_, cc_gain,
             stored);
    if (fabsf(stored - cc_gain) > fabsf(cc_gain) * 0.001f) {
      ESP_LOGE(TAG, "CC Gain did not round-trip - current readings will be wrong");
      ok = false;
    }
  }
  return ok;
}

// ---------------------------------------------------------------------------
// Data flash dump
// ---------------------------------------------------------------------------

// Every subclass documented in Table 7-8, with enough 32-byte blocks to cover
// the highest offset listed for it. Subclasses outside this list are either
// undocumented or unused on this part.
static const DataFlashSubclass DATA_FLASH_MAP[] = {
    {2, 1},    // Safety
    {32, 1},   // Charge Inhibit Cfg
    {34, 1},   // Charge
    {36, 1},   // Charge Termination
    {48, 2},   // Data - highest documented offset is 55
    {49, 1},   // Discharge
    {56, 1},   // Manufacturer Data
    {58, 1},   // Lifetime Data
    {59, 1},   // Lifetime Temp Samples
    {60, 1},   // Integrity Data
    {64, 1},   // Registers
    {66, 1},   // Lifetime Resolution
    {67, 1},   // Lifetime Res 2
    {68, 1},   // Power
    {80, 3},   // IT Cfg - highest documented offset is 91
    {81, 1},   // Current Thresholds
    {82, 1},   // State
    {88, 1},   // Chem Data
    {89, 1},   // R_a0x Calibration
    {104, 1},  // Calibration
    {107, 1},  // Current
};

bool BQ34Z100Component::dump_data_flash() {
  if (!this->unseal_()) {
    ESP_LOGE(TAG, "Could not unseal the gauge; data flash cannot be read");
    return false;
  }

  ESP_LOGI(TAG, "---- BQ34Z100 data flash dump begins ----");
  ESP_LOGI(TAG, "Format: DF,<subclass>,<block>,<32 bytes as hex>");

  uint16_t chem_id = 0;
  if (this->control_read_(CTRL_CHEM_ID, chem_id))
    ESP_LOGI(TAG, "Chemistry ID: 0x%04X (%u)", chem_id, chem_id);

  uint8_t buffer[FLASH_BLOCK_SIZE];
  size_t ok = 0, failed = 0;
  for (const auto &entry : DATA_FLASH_MAP) {
    for (uint8_t block = 0; block < entry.blocks; block++) {
      if (this->read_flash_block_(entry.subclass, block, buffer)) {
        ESP_LOGI(TAG, "DF,%u,%u,%s", entry.subclass, block,
                 format_hex(buffer, FLASH_BLOCK_SIZE).c_str());
        ok++;
      } else {
        ESP_LOGW(TAG, "DF,%u,%u,READ FAILED", entry.subclass, block);
        failed++;
      }
      // The gauge needs a moment between block transfers, and this keeps the
      // dump from starving the rest of the loop.
      delay(FLASH_SETUP_DELAY_MS);
    }
  }

  ESP_LOGI(TAG, "---- data flash dump ends: %u blocks read, %u failed ----", static_cast<unsigned>(ok),
           static_cast<unsigned>(failed));

  this->seal_();
  return failed == 0;
}

// ---------------------------------------------------------------------------
// Calibration actions
// ---------------------------------------------------------------------------

bool BQ34Z100Component::run_cc_offset_calibration() {
  ESP_LOGI(TAG, "Starting coulomb counter offset calibration; keep the pack at rest");
  if (!this->unseal_())
    return false;
  bool ok = this->control_command_(CTRL_CC_OFFSET);
  if (ok) {
    // The routine runs for a while; CC_OFFSET_SAVE commits the result.
    delay(FLASH_WRITE_DELAY_MS);
    ok = this->control_command_(CTRL_CC_OFFSET_SAVE);
  }
  this->seal_();
  return ok;
}

bool BQ34Z100Component::run_board_offset_calibration() {
  ESP_LOGI(TAG, "Starting board offset calibration; no current must flow through the shunt");
  if (!this->unseal_())
    return false;
  bool ok = this->control_command_(CTRL_BOARD_OFFSET);
  this->seal_();
  return ok;
}

bool BQ34Z100Component::enable_impedance_track() {
  ESP_LOGI(TAG, "Enabling the Impedance Track algorithm");
  if (!this->unseal_())
    return false;
  bool ok = this->control_command_(CTRL_IT_ENABLE);
  this->seal_();
  return ok;
}

bool BQ34Z100Component::reset_gauge() {
  ESP_LOGW(TAG, "Issuing a full gauge reset");
  if (!this->unseal_())
    return false;
  bool ok = this->control_command_(CTRL_RESET);
  return ok;
}

// ---------------------------------------------------------------------------
// Setup and polling
// ---------------------------------------------------------------------------

void BQ34Z100Component::setup() {
  uint16_t device_type;
  if (!this->control_read_(CTRL_DEVICE_TYPE, device_type)) {
    ESP_LOGE(TAG, "BQ34Z100 not responding at address 0x%02X", this->address_);
    this->mark_failed();
    return;
  }
  if (device_type != DEVICE_TYPE_BQ34Z100) {
    ESP_LOGW(TAG, "DEVICE_TYPE reported 0x%04X, expected 0x%04X", device_type, DEVICE_TYPE_BQ34Z100);
  }

  uint16_t chem_id;
  if (this->control_read_(CTRL_CHEM_ID, chem_id)) {
    ESP_LOGI(TAG, "Gauge chemistry ID: 0x%04X", chem_id);
    if (this->expected_chem_id_ != 0 && chem_id != this->expected_chem_id_) {
      // The chemistry table is not programmable over I2C: it has to be
      // loaded once with bqStudio and an EV2400, from a .bqz chemistry file.
      ESP_LOGE(TAG,
               "Gauge is running chemistry 0x%04X but the configuration expects 0x%04X. "
               "State of charge will be wrong. Load the correct chemistry with bqStudio - "
               "it cannot be programmed over I2C.",
               chem_id, this->expected_chem_id_);
    }
  }

  if (this->apply_configuration_flag_) {
    this->apply_configuration_();
  } else if (this->design_capacity_ >= 0 || this->cell_count_ > 0 || this->sense_resistor_ > 0.0f ||
             this->voltage_divider_ > 0) {
    // Values are declared but writing them is off. Say so rather than letting
    // someone assume a configured gauge is a calibrated one.
    ESP_LOGW(TAG,
             "Pack parameters are configured but apply_configuration is false, so the gauge's "
             "data flash is untouched. Set apply_configuration: true once, with a cell attached, "
             "to write them.");
  }

  this->setup_ok_ = true;
}

void BQ34Z100Component::publish_word_(sensor::Sensor *s, uint8_t cmd, float scale, bool is_signed) {
  if (s == nullptr)
    return;
  uint16_t raw;
  if (!this->read_word_(cmd, raw))
    return;
  float value = is_signed ? static_cast<float>(static_cast<int16_t>(raw)) : static_cast<float>(raw);
  s->publish_state(value * scale);
}

void BQ34Z100Component::update() {
  if (!this->setup_ok_)
    return;

  this->publish_word_(this->state_of_charge_sensor_, CMD_STATE_OF_CHARGE, 1.0f, false);
  this->publish_word_(this->voltage_sensor_, CMD_VOLTAGE, 0.001f, false);
  this->publish_word_(this->current_sensor_, CMD_CURRENT, 1.0f, true);
  this->publish_word_(this->average_current_sensor_, CMD_AVERAGE_CURRENT, 1.0f, true);
  // AveragePower is reported in units of 10 mW.
  this->publish_word_(this->average_power_sensor_, CMD_AVERAGE_POWER, 0.01f, true);
  this->publish_word_(this->remaining_capacity_sensor_, CMD_REMAINING_CAPACITY, 1.0f, false);
  this->publish_word_(this->full_charge_capacity_sensor_, CMD_FULL_CHARGE_CAPACITY, 1.0f, false);
  // AvailableEnergy is reported in units of 10 mWh.
  this->publish_word_(this->available_energy_sensor_, CMD_AVAILABLE_ENERGY, 0.01f, false);
  this->publish_word_(this->cycle_count_sensor_, CMD_CYCLE_COUNT, 1.0f, false);
  this->publish_word_(this->state_of_health_sensor_, CMD_STATE_OF_HEALTH, 1.0f, false);

  // Both temperature commands report in tenths of a kelvin.
  uint16_t raw;
  if (this->temperature_sensor_ != nullptr && this->read_word_(CMD_TEMPERATURE, raw))
    this->temperature_sensor_->publish_state(raw / 10.0f - 273.15f);
  if (this->internal_temperature_sensor_ != nullptr && this->read_word_(CMD_INTERNAL_TEMP, raw))
    this->internal_temperature_sensor_->publish_state(raw / 10.0f - 273.15f);

  // 65535 means the gauge cannot work the figure out yet, which is not the
  // same as "a very long time".
  if (this->time_to_empty_sensor_ != nullptr && this->read_word_(CMD_TIME_TO_EMPTY, raw))
    this->time_to_empty_sensor_->publish_state(raw == TIME_UNKNOWN ? NAN : raw);
  if (this->time_to_full_sensor_ != nullptr && this->read_word_(CMD_TIME_TO_FULL, raw))
    this->time_to_full_sensor_->publish_state(raw == TIME_UNKNOWN ? NAN : raw);

  if (this->chem_id_sensor_ != nullptr) {
    uint16_t chem_id;
    if (this->control_read_(CTRL_CHEM_ID, chem_id))
      this->chem_id_sensor_->publish_state(chem_id);
  }

  uint16_t flags;
  if (this->read_word_(CMD_FLAGS, flags)) {
    if (this->discharging_sensor_ != nullptr)
      this->discharging_sensor_->publish_state((flags & FLAG_DSG) != 0);
    if (this->fully_charged_sensor_ != nullptr)
      this->fully_charged_sensor_->publish_state((flags & FLAG_FC) != 0);
    if (this->charge_inhibited_sensor_ != nullptr)
      this->charge_inhibited_sensor_->publish_state((flags & FLAG_CHG_INH) != 0);
    if (this->low_charge_sensor_ != nullptr)
      this->low_charge_sensor_->publish_state((flags & FLAG_SOC1) != 0);
    if (this->critical_charge_sensor_ != nullptr)
      this->critical_charge_sensor_->publish_state((flags & FLAG_SOCF) != 0);
    if (this->battery_high_sensor_ != nullptr)
      this->battery_high_sensor_->publish_state((flags & FLAG_BATHI) != 0);
    if (this->battery_low_sensor_ != nullptr)
      this->battery_low_sensor_->publish_state((flags & FLAG_BATLOW) != 0);
    if (this->over_temperature_sensor_ != nullptr)
      this->over_temperature_sensor_->publish_state((flags & (FLAG_OTC | FLAG_OTD)) != 0);

    if (this->status_text_sensor_ != nullptr) {
      std::string status;
      auto append = [&status](const char *name) {
        if (!status.empty())
          status += ", ";
        status += name;
      };
      if (flags & FLAG_OTC) append("Over-temperature (charge)");
      if (flags & FLAG_OTD) append("Over-temperature (discharge)");
      if (flags & FLAG_BATHI) append("Battery high");
      if (flags & FLAG_BATLOW) append("Battery low");
      if (flags & FLAG_CHG_INH) append("Charge inhibited");
      if (flags & FLAG_FC) append("Fully charged");
      if (flags & FLAG_CHG) append("Charge allowed");
      if (flags & FLAG_ISD) append("Internal short");
      if (flags & FLAG_TDD) append("Tab disconnect");
      if (flags & FLAG_SOCF) append("Critical charge");
      else if (flags & FLAG_SOC1) append("Low charge");
      if (flags & FLAG_DSG) append("Discharging");
      this->status_text_sensor_->publish_state(status.empty() ? "Idle" : status);
    }
  }
}

void BQ34Z100Component::dump_config() {
  ESP_LOGCONFIG(TAG, "BQ34Z100:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication with the BQ34Z100 failed");
    return;
  }
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  Data flash configuration: %s", this->apply_configuration_flag_ ? "applied" : "left alone");
  if (this->sense_resistor_ > 0.0f)
    ESP_LOGCONFIG(TAG, "  Sense resistor: %.3f mOhm", this->sense_resistor_);
  if (this->design_capacity_ >= 0)
    ESP_LOGCONFIG(TAG, "  Design capacity: %d mAh", this->design_capacity_);
  if (this->expected_chem_id_ != 0)
    ESP_LOGCONFIG(TAG, "  Expected chemistry ID: 0x%04X", this->expected_chem_id_);
}

}  // namespace bq34z100
}  // namespace esphome
