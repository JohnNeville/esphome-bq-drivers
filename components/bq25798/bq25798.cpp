#include "bq25798.h"

#include <cmath>

namespace esphome {
namespace bq25798 {

static const char *const TAG = "bq25798";

// ---------------------------------------------------------------------------
// Register helpers
// ---------------------------------------------------------------------------

bool BQ25798Component::read_u8_(uint8_t reg, uint8_t &value) {
  if (!this->read_byte(reg, &value)) {
    ESP_LOGW(TAG, "Read of register 0x%02X failed", reg);
    return false;
  }
  return true;
}

bool BQ25798Component::write_u8_(uint8_t reg, uint8_t value) {
  if (!this->write_byte(reg, value)) {
    ESP_LOGW(TAG, "Write of 0x%02X to register 0x%02X failed", value, reg);
    return false;
  }
  return true;
}

// All 16-bit registers on this part are big-endian: the named offset holds
// the MSB and the next offset holds the LSB.
bool BQ25798Component::read_u16_(uint8_t reg, uint16_t &value) {
  uint8_t buf[2];
  if (!this->read_bytes(reg, buf, 2)) {
    ESP_LOGW(TAG, "Read of register pair 0x%02X failed", reg);
    return false;
  }
  value = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
  return true;
}

bool BQ25798Component::write_u16_(uint8_t reg, uint16_t value) {
  uint8_t buf[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
  if (!this->write_bytes(reg, buf, 2)) {
    ESP_LOGW(TAG, "Write of 0x%04X to register pair 0x%02X failed", value, reg);
    return false;
  }
  return true;
}

bool BQ25798Component::read_s16_(uint8_t reg, int16_t &value) {
  uint16_t raw;
  if (!this->read_u16_(reg, raw))
    return false;
  value = static_cast<int16_t>(raw);
  return true;
}

bool BQ25798Component::modify_u8_(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t current;
  if (!this->read_u8_(reg, current))
    return false;
  uint8_t updated = (current & ~mask) | (value & mask);
  if (updated == current)
    return true;
  return this->write_u8_(reg, updated);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void BQ25798Component::setup() {
  uint8_t part_info;
  if (!this->read_u8_(REG48_PART_INFO, part_info)) {
    ESP_LOGE(TAG, "BQ25798 not responding at address 0x%02X", this->address_);
    this->mark_failed();
    return;
  }
  // PN sits in bits [5:3]; 011b identifies the BQ25798.
  uint8_t part_number = (part_info >> 3) & 0x07;
  if (part_number != 0x03) {
    ESP_LOGW(TAG, "Unexpected part number %u in REG48 (0x%02X) - continuing anyway", part_number, part_info);
  }

  this->configure_defaults_();
  this->configure_adc_();
  this->publish_control_states_();
  this->setup_ok_ = true;
}

void BQ25798Component::configure_defaults_() {
  // The watchdog resets ICHG, ITERM, EN_CHG and most of the timer settings
  // back to their PROG-pin power-on defaults when it expires. Disabling it
  // is the default here; a configured period is kicked on every update().
  this->modify_u8_(REG10_CHG_CTRL1, 0x07, this->watchdog_ & 0x07);

  // EN_IBAT enables battery discharge-current sensing for the ADC. Without
  // it IBAT reads zero whenever the board runs from the battery alone,
  // which is exactly when the reading matters.
  this->modify_u8_(REG14_CHG_CTRL5, 0x20, 0x20);

  // SFET_PRESENT gates SDRV_CTRL and EN_BATOC. Setting it on a board with
  // no ship FET would expose ship/shutdown controls that silently do
  // nothing, so it follows the declared hardware.
  this->modify_u8_(REG14_CHG_CTRL5, 0x80, this->ship_fet_present_ ? 0x80 : 0x00);

  if (this->ship_fet_present_) {
    // Both of these are locked at 0 until SFET_PRESENT is set, so they are
    // written after it rather than before.
    this->modify_u8_(REG11_CHG_CTRL2, 0x01, this->ship_fet_action_delay_ ? 0x00 : 0x01);
    this->modify_u8_(REG14_CHG_CTRL5, 0x01, this->battery_ocp_ ? 0x01 : 0x00);
  } else if (this->battery_ocp_) {
    ESP_LOGW(TAG, "battery_ocp needs a ship FET to act on; EN_BATOC stays locked at 0");
  }

  // TS_IGNORE. The charger qualifies charging against the TS pin, and an
  // open TS divider reads far colder than any JEITA threshold, so a board
  // with no thermistor fitted never charges. Writing this is the supported
  // way out of that, at the cost of all temperature protection. The bit is
  // cleared by a register reset and by a watchdog expiry, both of which land
  // back here, so it is written on every pass rather than only at boot.
  this->modify_u8_(REG18_NTC_CTRL1, 0x01, this->ts_ignore_ ? 0x01 : 0x00);
  if (this->ts_ignore_) {
    ESP_LOGW(TAG, "TS_IGNORE is set: charging is no longer qualified against the battery temperature");
  }

  // The charger powers up at the PROG pin's defaults, which for a 1s pack
  // means 4.2 V. A LiFePO4 pack needs its own limit applied before any
  // charging happens, so the chemistry-derived value is written here rather
  // than left to a control entity somebody has to remember to set.
  if (!std::isnan(this->charge_voltage_limit_)) {
    this->set_charge_voltage(this->charge_voltage_limit_);
    if (this->charge_voltage_number_ != nullptr)
      this->charge_voltage_number_->publish_state(this->charge_voltage_limit_);
  }
  if (!std::isnan(this->default_charge_current_)) {
    this->set_charge_current(this->default_charge_current_);
    if (this->charge_current_number_ != nullptr)
      this->charge_current_number_->publish_state(this->default_charge_current_);
  }

  // Derive the termination current as C/10 when the pack size is known.
  if (this->battery_capacity_ > 0) {
    uint16_t iterm_ma = this->battery_capacity_ / 10;
    iterm_ma = clamp<uint16_t>(iterm_ma, ITERM_MIN_MA, ITERM_MAX_MA);
    uint8_t code = static_cast<uint8_t>(iterm_ma / static_cast<uint16_t>(ITERM_STEP_MA));
    this->modify_u8_(REG09_TERM, 0x1F, code);
    ESP_LOGD(TAG, "Termination current set to %u mA (C/10 of %u mAh)", iterm_ma, this->battery_capacity_);
  }
}

void BQ25798Component::configure_adc_() {
  // ADC_EN, continuous conversion, 15-bit effective resolution. The 12-bit
  // power-on default is the one setting the datasheet marks "not
  // recommended".
  if (!this->write_u8_(REG2E_ADC_CTRL, 0x80)) {
    ESP_LOGE(TAG, "Failed to enable the ADC");
    return;
  }
  // Clear every per-channel disable bit.
  this->write_u8_(REG2F_ADC_DIS0, 0x00);
  this->write_u8_(REG30_ADC_DIS1, 0x00);
}

void BQ25798Component::kick_watchdog_() {
  if (this->watchdog_ == 0)
    return;
  this->modify_u8_(REG10_CHG_CTRL1, 0x08, 0x08);
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------

bool BQ25798Component::enable_charging(bool enable) {
  return this->modify_u8_(REG0F_CHG_CTRL0, 0x20, enable ? 0x20 : 0x00);
}

bool BQ25798Component::enable_hiz(bool enable) {
  return this->modify_u8_(REG0F_CHG_CTRL0, 0x04, enable ? 0x04 : 0x00);
}

bool BQ25798Component::enable_mppt(bool enable) {
  this->mppt_requested_ = enable;
  if (enable) {
    // EN_ICO, FORCE_VINDPM_DET and EN_MPPT are mutually exclusive: the
    // datasheet allows only one of the three to be set at a time, and
    // writing EN_MPPT while another is active is silently ignored.
    if (!this->modify_u8_(REG0F_CHG_CTRL0, 0x10, 0x00))
      return false;
    if (!this->modify_u8_(REG13_CHG_CTRL4, 0x02, 0x00))
      return false;
    if (this->ico_switch_ != nullptr)
      this->ico_switch_->publish_state(false);
  }
  return this->modify_u8_(REG15_MPPT, 0x01, enable ? 0x01 : 0x00);
}

bool BQ25798Component::enable_stat_led(bool enable) {
  // DIS_STAT is active-high, so the switch state is inverted.
  return this->modify_u8_(REG13_CHG_CTRL4, 0x10, enable ? 0x00 : 0x10);
}

bool BQ25798Component::enable_acdrv(uint8_t index, bool enable) {
  uint8_t mask = (index == 1) ? 0x40 : 0x80;
  if (enable) {
    // DIS_ACDRV forces both gate drivers off regardless of these bits.
    if (!this->modify_u8_(REG12_CHG_CTRL3, 0x80, 0x00))
      return false;
  }
  return this->modify_u8_(REG13_CHG_CTRL4, mask, enable ? mask : 0x00);
}

bool BQ25798Component::enable_ico(bool enable) {
  if (enable && this->mppt_requested_) {
    ESP_LOGW(TAG, "Refusing to enable ICO while MPPT is enabled - the two are mutually exclusive");
    return false;
  }
  return this->modify_u8_(REG0F_CHG_CTRL0, 0x10, enable ? 0x10 : 0x00);
}

bool BQ25798Component::set_charge_current(float current_ma) {
  if (std::isnan(current_ma))
    return false;
  uint16_t ceiling = std::min<uint16_t>(this->max_charge_current_, ICHG_MAX_MA);
  uint16_t requested = static_cast<uint16_t>(lroundf(current_ma));
  uint16_t limited = clamp<uint16_t>(requested, ICHG_MIN_MA, ceiling);
  if (limited != requested) {
    ESP_LOGW(TAG, "Charge current %u mA clamped to %u mA", requested, limited);
  }
  uint16_t code = static_cast<uint16_t>(limited / static_cast<uint16_t>(ICHG_STEP_MA));
  return this->write_u16_(REG03_ICHG, code & 0x01FF);
}

bool BQ25798Component::set_charge_voltage(float voltage_v) {
  if (std::isnan(voltage_v))
    return false;
  uint16_t mv = clamp<uint16_t>(static_cast<uint16_t>(lroundf(voltage_v * 1000.0f)), 3000, 18800);
  uint16_t code = static_cast<uint16_t>(mv / static_cast<uint16_t>(VREG_STEP_MV));
  return this->write_u16_(REG01_VREG, code & 0x07FF);
}

bool BQ25798Component::set_input_current(float current_ma) {
  if (std::isnan(current_ma))
    return false;
  uint16_t ma = clamp<uint16_t>(static_cast<uint16_t>(lroundf(current_ma)), 100, 3300);
  uint16_t code = static_cast<uint16_t>(ma / static_cast<uint16_t>(IINDPM_STEP_MA));
  return this->write_u16_(REG06_IINDPM, code & 0x01FF);
}

bool BQ25798Component::set_vindpm(float voltage_v) {
  if (std::isnan(voltage_v))
    return false;
  uint16_t mv = clamp<uint16_t>(static_cast<uint16_t>(lroundf(voltage_v * 1000.0f)), 3600, 22000);
  uint8_t code = static_cast<uint8_t>(mv / static_cast<uint16_t>(VINDPM_STEP_MV));
  return this->write_u8_(REG05_VINDPM, code);
}

bool BQ25798Component::set_min_sys_voltage(float voltage_v) {
  if (std::isnan(voltage_v))
    return false;
  uint16_t mv = clamp<uint16_t>(static_cast<uint16_t>(lroundf(voltage_v * 1000.0f)), 2500, 16000);
  uint8_t code = static_cast<uint8_t>((mv - static_cast<uint16_t>(VSYSMIN_OFFSET_MV)) /
                                      static_cast<uint16_t>(VSYSMIN_STEP_MV));
  return this->modify_u8_(REG00_VSYSMIN, 0x3F, code & 0x3F);
}

bool BQ25798Component::set_voc_ratio(size_t index) {
  return this->modify_u8_(REG15_MPPT, 0xE0, static_cast<uint8_t>((index & 0x07) << 5));
}

bool BQ25798Component::set_voc_delay(size_t index) {
  return this->modify_u8_(REG15_MPPT, 0x18, static_cast<uint8_t>((index & 0x03) << 3));
}

bool BQ25798Component::set_voc_rate(size_t index) {
  return this->modify_u8_(REG15_MPPT, 0x06, static_cast<uint8_t>((index & 0x03) << 1));
}

bool BQ25798Component::ship_fet_action(ShipFetAction action) {
  if (!this->ship_fet_present_) {
    ESP_LOGW(TAG, "Ship-FET action ignored: no ship FET declared, so SDRV_CTRL is locked at 00");
    return false;
  }
  uint8_t value = static_cast<uint8_t>(static_cast<uint8_t>(action) << 1);
  return this->modify_u8_(REG11_CHG_CTRL2, 0x06, value);
}

bool BQ25798Component::reset_registers() {
  // REG_RST self-clears. Every configured value has to be reapplied after it.
  if (!this->modify_u8_(REG09_TERM, 0x40, 0x40))
    return false;
  ESP_LOGI(TAG, "Registers reset to defaults; reapplying configuration");
  this->configure_defaults_();
  this->configure_adc_();
  this->publish_control_states_();
  return true;
}

bool BQ25798Component::clear_interrupts() {
  // Reading registers 0x22 to 0x27 (Charger Flag 0-3 and FAULT Flag 0-1) clears
  // all latched interrupt flags and releases the active-low open-drain /INT pin,
  // preventing continuous current leakage through its pull-up resistor.
  uint8_t flags[6];
  if (!this->read_bytes(REG22_FLAG0, flags, sizeof(flags))) {
    ESP_LOGW(TAG, "Failed to read flag registers to clear interrupts");
    return false;
  }
  ESP_LOGD(TAG, "Cleared charger flags (0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X)",
           flags[0], flags[1], flags[2], flags[3], flags[4], flags[5]);
  return true;
}

void BQ25798Component::on_shutdown() {
  ESP_LOGI(TAG, "Shutting down; clearing pending interrupts on /INT");
  this->clear_interrupts();
}

// ---------------------------------------------------------------------------
// Read-back of control state, so Home Assistant reflects the chip at boot
// ---------------------------------------------------------------------------

void BQ25798Component::publish_control_states_() {
  uint8_t ctrl0, ctrl4, mppt;
  if (this->read_u8_(REG0F_CHG_CTRL0, ctrl0)) {
    if (this->charging_switch_ != nullptr)
      this->charging_switch_->publish_state((ctrl0 & 0x20) != 0);
    if (this->hiz_switch_ != nullptr)
      this->hiz_switch_->publish_state((ctrl0 & 0x04) != 0);
    if (this->ico_switch_ != nullptr)
      this->ico_switch_->publish_state((ctrl0 & 0x10) != 0);
  }
  if (this->read_u8_(REG13_CHG_CTRL4, ctrl4)) {
    if (this->stat_led_switch_ != nullptr)
      this->stat_led_switch_->publish_state((ctrl4 & 0x10) == 0);
    if (this->acdrv1_switch_ != nullptr)
      this->acdrv1_switch_->publish_state((ctrl4 & 0x40) != 0);
    if (this->acdrv2_switch_ != nullptr)
      this->acdrv2_switch_->publish_state((ctrl4 & 0x80) != 0);
  }
  if (this->read_u8_(REG15_MPPT, mppt)) {
    this->mppt_requested_ = (mppt & 0x01) != 0;
    if (this->mppt_switch_ != nullptr)
      this->mppt_switch_->publish_state(this->mppt_requested_);
    if (this->voc_ratio_select_ != nullptr) {
      const auto &options = this->voc_ratio_select_->traits.get_options();
      size_t index = (mppt >> 5) & 0x07;
      if (index < options.size())
        this->voc_ratio_select_->publish_state(options[index]);
    }
    if (this->voc_delay_select_ != nullptr) {
      const auto &options = this->voc_delay_select_->traits.get_options();
      size_t index = (mppt >> 3) & 0x03;
      if (index < options.size())
        this->voc_delay_select_->publish_state(options[index]);
    }
    if (this->voc_rate_select_ != nullptr) {
      const auto &options = this->voc_rate_select_->traits.get_options();
      size_t index = (mppt >> 1) & 0x03;
      if (index < options.size())
        this->voc_rate_select_->publish_state(options[index]);
    }
  }

  uint16_t raw;
  if (this->charge_current_number_ != nullptr && this->read_u16_(REG03_ICHG, raw))
    this->charge_current_number_->publish_state((raw & 0x01FF) * ICHG_STEP_MA);
  if (this->charge_voltage_number_ != nullptr && this->read_u16_(REG01_VREG, raw))
    this->charge_voltage_number_->publish_state((raw & 0x07FF) * VREG_STEP_MV / 1000.0f);
  if (this->input_current_number_ != nullptr && this->read_u16_(REG06_IINDPM, raw))
    this->input_current_number_->publish_state((raw & 0x01FF) * IINDPM_STEP_MA);

  uint8_t byte;
  if (this->vindpm_number_ != nullptr && this->read_u8_(REG05_VINDPM, byte))
    this->vindpm_number_->publish_state(byte * VINDPM_STEP_MV / 1000.0f);
  if (this->min_sys_voltage_number_ != nullptr && this->read_u8_(REG00_VSYSMIN, byte))
    this->min_sys_voltage_number_->publish_state(((byte & 0x3F) * VSYSMIN_STEP_MV + VSYSMIN_OFFSET_MV) / 1000.0f);
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

void BQ25798Component::publish_voltage_(sensor::Sensor *s, uint8_t reg) {
  if (s == nullptr)
    return;
  uint16_t raw;
  if (this->read_u16_(reg, raw))
    s->publish_state(raw / 1000.0f);  // 1 mV per LSB
}

void BQ25798Component::publish_current_(sensor::Sensor *s, uint8_t reg) {
  if (s == nullptr)
    return;
  int16_t raw;
  if (this->read_s16_(reg, raw))
    s->publish_state(static_cast<float>(raw));  // 1 mA per LSB, two's complement
}

void BQ25798Component::update() {
  if (!this->setup_ok_)
    return;

  this->kick_watchdog_();

  // MPPT clears itself whenever VBUS drops below the present threshold, so
  // a configured-on MPPT has to be re-asserted once the input returns.
  if (this->mppt_requested_) {
    uint8_t mppt;
    if (this->read_u8_(REG15_MPPT, mppt) && (mppt & 0x01) == 0) {
      ESP_LOGD(TAG, "MPPT was cleared by the charger; re-enabling");
      this->modify_u8_(REG15_MPPT, 0x01, 0x01);
    }
  }

  this->publish_voltage_(this->vbus_voltage_sensor_, REG35_VBUS_ADC);
  this->publish_voltage_(this->vac1_voltage_sensor_, REG37_VAC1_ADC);
  this->publish_voltage_(this->vac2_voltage_sensor_, REG39_VAC2_ADC);
  this->publish_voltage_(this->vbat_voltage_sensor_, REG3B_VBAT_ADC);
  this->publish_voltage_(this->vsys_voltage_sensor_, REG3D_VSYS_ADC);
  this->publish_current_(this->ibus_current_sensor_, REG31_IBUS_ADC);
  this->publish_current_(this->ibat_current_sensor_, REG33_IBAT_ADC);

  if (this->die_temperature_sensor_ != nullptr) {
    int16_t raw;
    if (this->read_s16_(REG41_TDIE_ADC, raw))
      this->die_temperature_sensor_->publish_state(raw * TDIE_STEP_C);
  }

  if (this->battery_temperature_sensor_ != nullptr) {
    uint16_t raw;
    if (this->read_u16_(REG3F_TS_ADC, raw))
      this->battery_temperature_sensor_->publish_state(this->ts_percent_to_celsius_(raw * TS_STEP_PCT / 100.0f));
  }

  uint8_t status0, status1, status2;
  if (this->read_u8_(REG1B_STATUS0, status0)) {
    if (this->vbus_present_sensor_ != nullptr)
      this->vbus_present_sensor_->publish_state((status0 & 0x01) != 0);
    if (this->vac1_present_sensor_ != nullptr)
      this->vac1_present_sensor_->publish_state((status0 & 0x02) != 0);
    if (this->vac2_present_sensor_ != nullptr)
      this->vac2_present_sensor_->publish_state((status0 & 0x04) != 0);
    if (this->power_good_sensor_ != nullptr)
      this->power_good_sensor_->publish_state((status0 & 0x08) != 0);
    if ((status0 & 0x20) != 0) {
      ESP_LOGW(TAG, "Watchdog expired - charge settings reverted to PROG defaults; reapplying");
      this->configure_defaults_();
      this->publish_control_states_();
    }
  }

  if (this->read_u8_(REG1C_STATUS1, status1)) {
    auto charge_stat = static_cast<ChargeStat>((status1 >> 5) & 0x07);
    if (this->charging_sensor_ != nullptr) {
      bool charging = charge_stat == ChargeStat::TRICKLE || charge_stat == ChargeStat::PRECHARGE ||
                      charge_stat == ChargeStat::FAST_CHARGE_CC || charge_stat == ChargeStat::TAPER_CV ||
                      charge_stat == ChargeStat::TOP_OFF;
      this->charging_sensor_->publish_state(charging);
    }
    if (this->charge_status_text_sensor_ != nullptr) {
      const char *text;
      switch (charge_stat) {
        case ChargeStat::NOT_CHARGING: text = "Not charging"; break;
        case ChargeStat::TRICKLE: text = "Trickle charge"; break;
        case ChargeStat::PRECHARGE: text = "Pre-charge"; break;
        case ChargeStat::FAST_CHARGE_CC: text = "Fast charge (CC)"; break;
        case ChargeStat::TAPER_CV: text = "Taper charge (CV)"; break;
        case ChargeStat::TOP_OFF: text = "Top-off timer"; break;
        case ChargeStat::TERMINATED: text = "Charge terminated"; break;
        default: text = "Reserved"; break;
      }
      this->charge_status_text_sensor_->publish_state(text);
    }
    if (this->input_source_text_sensor_ != nullptr) {
      const char *text;
      switch ((status1 >> 1) & 0x0F) {
        case 0x0: text = "No input"; break;
        case 0x1: text = "USB SDP (500 mA)"; break;
        case 0x2: text = "USB CDP (1.5 A)"; break;
        case 0x3: text = "USB DCP (3.25 A)"; break;
        case 0x4: text = "Adjustable HVDCP (1.5 A)"; break;
        case 0x5: text = "Unknown adapter (3 A)"; break;
        case 0x6: text = "Non-standard adapter"; break;
        case 0x7: text = "OTG mode"; break;
        case 0x8: text = "Unqualified adapter"; break;
        case 0xB: text = "Powered directly from VBUS"; break;
        case 0xC: text = "Backup mode"; break;
        default: text = "Reserved"; break;
      }
      this->input_source_text_sensor_->publish_state(text);
    }
  }

  if (this->read_u8_(REG1D_STATUS2, status2)) {
    if (this->battery_present_sensor_ != nullptr)
      this->battery_present_sensor_->publish_state((status2 & 0x01) != 0);
    if (this->thermal_regulation_sensor_ != nullptr)
      this->thermal_regulation_sensor_->publish_state((status2 & 0x04) != 0);
  }

  if (this->charger_fault_text_sensor_ != nullptr) {
    uint8_t fault0, fault1;
    if (this->read_u8_(REG20_FAULT0, fault0) && this->read_u8_(REG21_FAULT1, fault1)) {
      std::string faults;
      auto append = [&faults](const char *name) {
        if (!faults.empty())
          faults += ", ";
        faults += name;
      };
      if (fault0 & 0x80) append("IBAT regulation");
      if (fault0 & 0x40) append("VBUS OVP");
      if (fault0 & 0x20) append("VBAT OVP");
      if (fault0 & 0x10) append("IBUS OCP");
      if (fault0 & 0x08) append("IBAT OCP");
      if (fault0 & 0x04) append("Converter OCP");
      if (fault0 & 0x02) append("VAC2 OVP");
      if (fault0 & 0x01) append("VAC1 OVP");
      if (fault1 & 0x80) append("VSYS short");
      if (fault1 & 0x40) append("VSYS OVP");
      if (fault1 & 0x20) append("OTG OVP");
      if (fault1 & 0x10) append("OTG UVP");
      if (fault1 & 0x04) append("Thermal shutdown");
      this->charger_fault_text_sensor_->publish_state(faults.empty() ? "OK" : faults);
    }
  }
}

// ---------------------------------------------------------------------------
// TS pin conversion
// ---------------------------------------------------------------------------

// The TS ADC reports the pin voltage as a fraction of REGN. The pin sits at
// the junction of an upper resistor to REGN and the parallel combination of
// a lower resistor and the NTC to ground, so the NTC resistance is recovered
// from that ratio and then converted with the beta equation.
float BQ25798Component::ts_percent_to_celsius_(float ratio) {
  if (this->ts_upper_ohms_ <= 0.0f || this->ts_lower_ohms_ <= 0.0f)
    return NAN;
  if (!(ratio > 0.0f) || ratio >= 1.0f)
    return NAN;

  float parallel = ratio * this->ts_upper_ohms_ / (1.0f - ratio);
  float inverse = 1.0f / parallel - 1.0f / this->ts_lower_ohms_;
  if (inverse <= 0.0f)
    return NAN;
  float ntc_ohms = 1.0f / inverse;

  const float kelvin_25c = 298.15f;
  float inv_t = 1.0f / kelvin_25c + logf(ntc_ohms / this->ts_nominal_ohms_) / this->ts_beta_;
  return 1.0f / inv_t - 273.15f;
}

// ---------------------------------------------------------------------------

void BQ25798Component::dump_config() {
  ESP_LOGCONFIG(TAG, "BQ25798:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Communication with the BQ25798 failed");
    return;
  }
  LOG_UPDATE_INTERVAL(this);
  ESP_LOGCONFIG(TAG, "  Max charge current: %u mA", this->max_charge_current_);
  if (!std::isnan(this->charge_voltage_limit_))
    ESP_LOGCONFIG(TAG, "  Charge voltage limit: %.2f V", this->charge_voltage_limit_);
  if (this->battery_capacity_ > 0)
    ESP_LOGCONFIG(TAG, "  Battery capacity: %u mAh", this->battery_capacity_);
  ESP_LOGCONFIG(TAG, "  Ship FET populated: %s", YESNO(this->ship_fet_present_));
  if (this->ship_fet_present_) {
    ESP_LOGCONFIG(TAG, "    Action delay: %s", this->ship_fet_action_delay_ ? "10 s" : "none");
    ESP_LOGCONFIG(TAG, "    Battery OCP: %s", ONOFF(this->battery_ocp_));
  }
  ESP_LOGCONFIG(TAG, "  Watchdog: %s", this->watchdog_ == 0 ? "disabled" : "enabled");
  ESP_LOGCONFIG(TAG, "  TS ignore: %s", ONOFF(this->ts_ignore_));
}

}  // namespace bq25798
}  // namespace esphome
