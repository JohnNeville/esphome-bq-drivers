#include "bq25798.h"

#include <cmath>

namespace esphome {
namespace bq25798 {

static const char *const TAG = "bq25798";

// How often reconcile_ reads the register block. One 49-byte read is about 9.5 ms on the hub's
// 50 kHz internal bus, so this is under 0.2% of the bus.
static const uint32_t RECONCILE_INTERVAL_MS = 5000;
// The charger sets EN_HIZ when its poor-source test fails and retries by itself every 7 minutes. The
// host can clear it sooner, but not more often than this.
static const uint32_t HIZ_CLEAR_MIN_GAP_MS = 30000;
// The POORSRC and WD flags clear on read, so the entities stay on this long after the charger last raised them.
static const uint32_t FLAG_HOLD_MS = 120000;
// In watch mode the same drift is reported at most this often.
static const uint32_t DRIFT_LOG_GAP_MS = 60000;

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
  if (this->register_check_ != 0)
    this->set_interval("reconcile", RECONCILE_INTERVAL_MS, [this]() { this->reconcile_(); });
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

  // AUTO_INDET_EN. Like TS_IGNORE it is cleared back to its default by a
  // register reset or a watchdog expiry, so it is written on every pass.
  this->modify_u8_(REG11_CHG_CTRL2, 0x40, this->auto_input_detection_ ? 0x40 : 0x00);
  if (this->ts_ignore_) {
    ESP_LOGW(TAG, "TS_IGNORE is set: charging is no longer qualified against the battery temperature");
  }

  this->apply_battery_settings_();
}

// Charge voltage limit, charge current and termination current. Called from
// configure_defaults_ and again when configure_battery changes them at run time.
void BQ25798Component::apply_battery_settings_() {
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
    this->want_iterm_code_ = code;
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
  this->want_en_chg_ = enable ? 1 : 0;
  return this->modify_u8_(REG0F_CHG_CTRL0, 0x20, enable ? 0x20 : 0x00);
}

bool BQ25798Component::enable_hiz(bool enable) {
  this->want_hiz_ = enable ? 1 : 0;
  return this->modify_u8_(REG0F_CHG_CTRL0, 0x04, enable ? 0x04 : 0x00);
}

// Input selection. The charger's EN_ACDRV bits alone do not open an input that is already
// placed; DIS_ACDRV (REG0x12 bit 7) forces both drivers off, and the datasheet's way to swap
// inputs is: DIS_ACDRV = 1, wait for VBUS to fall below the present threshold, DIS_ACDRV = 0.
// DIS_ACDRV forces the EN_ACDRV bits to 0 while it is set, so they are written again after it
// is cleared; with both inputs valid the charger does not turn either on by itself. When the
// board is already running from a port that the new choice keeps, only the EN bits are written,
// so the supply is not interrupted.
static const size_t INPUT_AUTO = 0, INPUT_USB = 1, INPUT_SOLAR = 2, INPUT_OFF = 3;
static const uint32_t INPUT_VBUS_WAIT_MS = 15000;

// Enable bits to write for a mode. The charger ignores a write that sets both bits at once, so
// Auto enables only the primary valid port (port 1, USB, first), which is what the charger itself
// does when two inputs appear together. If neither port has a valid input yet, both stay clear and
// the charger picks one when an input appears.
static uint8_t input_enable_bits(size_t mode, uint8_t status0) {
  if (mode == INPUT_USB)
    return 0x40;
  if (mode == INPUT_SOLAR)
    return 0x80;
  if ((status0 & 0x02) != 0)
    return 0x40;
  if ((status0 & 0x04) != 0)
    return 0x80;
  return 0x00;
}

bool BQ25798Component::set_input_selection(size_t index) {
  if (index > INPUT_OFF)
    return false;
  if (this->input_busy_) {
    ESP_LOGW(TAG, "An input selection change is still in progress");
    return false;
  }
  uint8_t ctrl3, ctrl4, status0;
  if (!this->read_u8_(REG12_CHG_CTRL3, ctrl3) || !this->read_u8_(REG13_CHG_CTRL4, ctrl4) ||
      !this->read_u8_(REG1B_STATUS0, status0))
    return false;

  this->input_requested_ = static_cast<int8_t>(index);
  if (index == INPUT_OFF)
    return this->modify_u8_(REG12_CHG_CTRL3, 0x80, 0x80);

  const bool want1 = index == INPUT_AUTO || index == INPUT_USB;
  const bool want2 = index == INPUT_AUTO || index == INPUT_SOLAR;
  const uint8_t en_bits = input_enable_bits(index, status0);
  const bool drivers_disabled = (ctrl3 & 0x80) != 0;
  // A port may be feeding the board when it has voltage on its VAC pin, its enable bit is set and
  // VBUS is up. If both could be, assume either may be. (ACRB_STAT only says the FET pair exists.)
  const bool vbus_present = (status0 & 0x01) != 0;
  const bool feeding1 = vbus_present && !drivers_disabled && (status0 & 0x02) != 0 && (ctrl4 & 0x40) != 0;
  const bool feeding2 = vbus_present && !drivers_disabled && (status0 & 0x04) != 0 && (ctrl4 & 0x80) != 0;
  const bool must_open = (feeding1 && !want1) || (feeding2 && !want2);

  if (!must_open && !drivers_disabled) {
    // Auto leaves an input that is already working alone.
    if (index == INPUT_AUTO && (feeding1 || feeding2))
      return true;
    return this->modify_u8_(REG13_CHG_CTRL4, 0xC0, en_bits);
  }

  if (!drivers_disabled && !this->modify_u8_(REG12_CHG_CTRL3, 0x80, 0x80))
    return false;
  if (!this->modify_u8_(REG13_CHG_CTRL4, 0xC0, en_bits))
    return false;
  this->input_busy_ = true;
  this->input_deadline_ = millis() + INPUT_VBUS_WAIT_MS;
  this->set_timeout("input_selection", 250, [this]() { this->input_selection_step_(); });
  return true;
}

void BQ25798Component::input_selection_step_() {
  uint8_t status0 = 0;
  const bool vbus_present = this->read_u8_(REG1B_STATUS0, status0) && (status0 & 0x01) != 0;
  if (vbus_present && static_cast<int32_t>(millis() - this->input_deadline_) < 0) {
    this->set_timeout("input_selection", 250, [this]() { this->input_selection_step_(); });
    return;
  }
  if (vbus_present)
    ESP_LOGW(TAG, "VBUS still present after %u ms; turning the input drivers back on anyway", INPUT_VBUS_WAIT_MS);
  this->modify_u8_(REG12_CHG_CTRL3, 0x80, 0x00);
  // Clearing DIS_ACDRV leaves both enable bits at 0, so say which port may turn on.
  this->read_u8_(REG1B_STATUS0, status0);
  this->modify_u8_(REG13_CHG_CTRL4, 0xC0, input_enable_bits(static_cast<size_t>(this->input_requested_), status0));
  this->input_busy_ = false;
  this->publish_control_states_();
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

// Per-cell charge voltage for each chemistry configure_battery accepts, in volts. Keep in step
// with CHEMISTRY_CELL_VOLTAGE in __init__.py, which builds the compile-time value from the same
// names. "custom" is deliberately absent: it needs a voltage nobody can pick by mistake.
static float chemistry_cell_voltage(const std::string &name) {
  if (name == "lifepo4")
    return 3.65f;
  if (name == "li_ion")
    return 4.20f;
  if (name == "li_ion_4_35")
    return 4.35f;
  if (name == "li_ion_4_40")
    return 4.40f;
  return NAN;
}

bool BQ25798Component::configure_battery(const std::string &chemistry, int capacity_mah, int charge_current_ma,
                                         int ts_ignore) {
  float limit = this->charge_voltage_limit_;
  if (!chemistry.empty()) {
    float per_cell = chemistry_cell_voltage(chemistry);
    if (std::isnan(per_cell)) {
      ESP_LOGE(TAG, "Unknown chemistry '%s': battery settings left as they were", chemistry.c_str());
      return false;
    }
    limit = per_cell * this->cell_count_;
  }
  if (this->is_failed()) {
    ESP_LOGW(TAG, "The charger did not start, so the battery settings were not applied");
    return false;
  }
  this->charge_voltage_limit_ = limit;
  if (capacity_mah > 0)
    this->battery_capacity_ = clamp<int>(capacity_mah, 1, 20000);
  if (charge_current_ma > 0)
    this->default_charge_current_ = static_cast<float>(charge_current_ma);
  this->apply_battery_settings_();
  if (ts_ignore >= 0) {
    // Kept in the member as well: configure_defaults_ writes it again after a register reset or
    // a watchdog expiry, and must not put the build-time value back.
    this->ts_ignore_ = ts_ignore == 1;
    this->modify_u8_(REG18_NTC_CTRL1, 0x01, this->ts_ignore_ ? 0x01 : 0x00);
    // Read it back, so the log shows what the charger holds and not just what was asked for.
    uint8_t ntc_ctrl1 = 0;
    if (this->read_u8_(REG18_NTC_CTRL1, ntc_ctrl1)) {
      ESP_LOGI(TAG, "NTC_CTRL1 (REG0x18) reads 0x%02X: TS_IGNORE %s", ntc_ctrl1, (ntc_ctrl1 & 0x01) ? "set" : "clear");
    } else {
      ESP_LOGW(TAG, "Could not read NTC_CTRL1 (REG0x18) back");
    }
    if (this->ts_ignore_) {
      ESP_LOGW(TAG, "TS_IGNORE is set: charging is no longer qualified against the battery temperature");
    } else {
      ESP_LOGI(TAG, "TS_IGNORE is clear: charging is qualified against the battery temperature");
    }
  }
  ESP_LOGI(TAG, "Battery settings applied: charge voltage limit %.2f V, %u mAh, %.0f mA", this->charge_voltage_limit_,
           this->battery_capacity_, this->default_charge_current_);
  return true;
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
  this->want_ichg_code_ = code & 0x01FF;
  return this->write_u16_(REG03_ICHG, code & 0x01FF);
}

bool BQ25798Component::set_charge_voltage(float voltage_v) {
  if (std::isnan(voltage_v))
    return false;
  uint16_t mv = clamp<uint16_t>(static_cast<uint16_t>(lroundf(voltage_v * 1000.0f)), 3000, 18800);
  uint16_t code = static_cast<uint16_t>(mv / static_cast<uint16_t>(VREG_STEP_MV));
  this->want_vreg_code_ = code & 0x07FF;
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
  this->want_voc_pct_ = static_cast<int8_t>(index & 0x07);
  return this->modify_u8_(REG15_MPPT, 0xE0, static_cast<uint8_t>((index & 0x07) << 5));
}

bool BQ25798Component::set_voc_delay(size_t index) {
  this->want_voc_dly_ = static_cast<int8_t>(index & 0x03);
  return this->modify_u8_(REG15_MPPT, 0x18, static_cast<uint8_t>((index & 0x03) << 3));
}

bool BQ25798Component::set_voc_rate(size_t index) {
  this->want_voc_rate_ = static_cast<int8_t>(index & 0x03);
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
  this->note_flags_(flags);
  return true;
}

void BQ25798Component::note_flags_(const uint8_t *f) {
  const uint32_t now = millis();
  if (f[0] & 0x10) {  // POORSRC_FLAG
    this->last_poor_source_ = now;
    this->seen_poor_source_ = true;
  }
  if (f[0] & 0x20) {  // WD_FLAG
    this->last_watchdog_flag_ = now;
    this->seen_watchdog_flag_ = true;
  }
  if (this->poor_source_sensor_ != nullptr)
    this->poor_source_sensor_->publish_state(this->seen_poor_source_ && now - this->last_poor_source_ < FLAG_HOLD_MS);
  if (this->watchdog_expired_sensor_ != nullptr)
    this->watchdog_expired_sensor_->publish_state(this->seen_watchdog_flag_ &&
                                                  now - this->last_watchdog_flag_ < FLAG_HOLD_MS);
}

void BQ25798Component::on_shutdown() {
  ESP_LOGI(TAG, "Shutting down; clearing pending interrupts on /INT");
  this->clear_interrupts();
  if (!this->adc_off_before_sleep_ || this->is_failed())
    return;
  // ADC_EN = 0 with one-shot conversion selected: datasheet 7.3.10 says the lowest quiescent current needs ADC_EN
  // cleared (not just the channels disabled) and that one-shot is the graceful way to stop. The ADC is also what keeps
  // the REGN LDO running in battery-only mode while the TS channel is on (7.3.4.1), so this drops that bias too.
  // The register keeps its value through deep sleep because the charger stays powered; setup() writes it back on wake.
  if (this->write_u8_(REG2E_ADC_CTRL, 0x40))
    ESP_LOGD(TAG, "ADC switched off for sleep (ADC_EN = 0)");
  else
    ESP_LOGW(TAG, "Could not switch the ADC off for sleep");
}

// ---------------------------------------------------------------------------
// Reconcile: notice and undo changes the host did not make
// ---------------------------------------------------------------------------
// The charger changes some control bits itself (EN_HIZ after a failed poor-source test, EN_MPPT when
// VBUS drops) and a register reset or watchdog expiry returns others to their defaults. None of that
// raises /INT, so the control registers are read back on a timer. Which fields are enforced, and why,
// is in docs/bq25798-register-behaviour.md.

bool BQ25798Component::correct_bits_(const char *name, uint8_t reg, uint8_t mask, uint8_t want, uint8_t seen) {
  if ((seen & mask) == want)
    return false;
  this->corrections_++;
  if (this->register_check_ < 2) {
    if ((this->last_drift_log_ == 0 || millis() - this->last_drift_log_ >= DRIFT_LOG_GAP_MS)) {
      this->last_drift_log_ = millis();
      ESP_LOGW(TAG, "%s (REG0x%02X & 0x%02X) reads 0x%02X, the host asked for 0x%02X (watch only, not changed)", name,
               reg, mask, seen & mask, want);
    }
    return false;
  }
  ESP_LOGW(TAG, "%s (REG0x%02X & 0x%02X) read 0x%02X, expected 0x%02X: writing it back", name, reg, mask, seen & mask,
           want);
  this->modify_u8_(reg, mask, want);
  uint8_t now;
  if (this->read_u8_(reg, now) && (now & mask) != want)
    ESP_LOGE(TAG, "%s did not take: REG0x%02X reads 0x%02X (locked, or another setting overrides it)", name, reg, now);
  return true;
}

void BQ25798Component::reconcile_() {
  if (!this->setup_ok_ || this->input_busy_)
    return;
  // 0x00 to 0x30 covers every control, status, flag and ADC-control register in one transaction.
  uint8_t r[REG30_ADC_DIS1 + 1];
  if (!this->read_bytes(REG00_VSYSMIN, r, sizeof(r))) {
    ESP_LOGW(TAG, "Reconcile read of REG0x00 to REG0x30 failed");
    return;
  }

  // The flag registers were cleared by that read, so log whatever they held.
  const uint8_t *f = &r[REG22_FLAG0];
  if (f[0] & 0x30) {  // WD, POORSRC
    ESP_LOGI(TAG, "Charger flags: %s%s", (f[0] & 0x20) ? "watchdog expired " : "",
             (f[0] & 0x10) ? "poor source detected" : "");
  }
  this->note_flags_(f);
  if (f[4] != 0 || f[5] != 0)
    ESP_LOGW(TAG, "Fault flags: 0x%02X 0x%02X", f[4], f[5]);
  else if (f[0] || f[1] || f[2] || f[3])
    ESP_LOGV(TAG, "Charger flags: 0x%02X 0x%02X 0x%02X 0x%02X", f[0], f[1], f[2], f[3]);

  bool changed = false;
  // Safety-relevant first.
  if (this->want_vreg_code_ >= 0) {
    const uint16_t seen = ((static_cast<uint16_t>(r[REG01_VREG]) << 8) | r[REG01_VREG + 1]) & 0x07FF;
    if (seen != this->want_vreg_code_) {
      this->corrections_++;
      if (this->register_check_ < 2) {
        if ((this->last_drift_log_ == 0 || millis() - this->last_drift_log_ >= DRIFT_LOG_GAP_MS)) {
          this->last_drift_log_ = millis();
          ESP_LOGW(TAG, "Charge voltage limit reads %u mV, the host asked for %u mV (watch only, not changed)", seen * 10,
                   this->want_vreg_code_ * 10);
        }
      } else {
        ESP_LOGW(TAG, "Charge voltage limit read %u mV, expected %u mV: writing it back", seen * 10,
                 this->want_vreg_code_ * 10);
        this->write_u16_(REG01_VREG, static_cast<uint16_t>(this->want_vreg_code_));
        changed = true;
      }
    }
  }
  if (this->want_ichg_code_ >= 0) {
    const uint16_t seen = ((static_cast<uint16_t>(r[REG03_ICHG]) << 8) | r[REG03_ICHG + 1]) & 0x01FF;
    if (seen != this->want_ichg_code_) {
      this->corrections_++;
      if (this->register_check_ < 2) {
        if ((this->last_drift_log_ == 0 || millis() - this->last_drift_log_ >= DRIFT_LOG_GAP_MS)) {
          this->last_drift_log_ = millis();
          ESP_LOGW(TAG, "Charge current limit reads %u mA, the host asked for %u mA (watch only, not changed)", seen * 10,
                   this->want_ichg_code_ * 10);
        }
      } else {
        ESP_LOGW(TAG, "Charge current limit read %u mA, expected %u mA: writing it back", seen * 10,
                 this->want_ichg_code_ * 10);
        this->write_u16_(REG03_ICHG, static_cast<uint16_t>(this->want_ichg_code_));
        changed = true;
      }
    }
  }
  if (this->want_iterm_code_ >= 0)
    changed |= this->correct_bits_("ITERM", REG09_TERM, 0x1F, static_cast<uint8_t>(this->want_iterm_code_), r[REG09_TERM]);
  if (this->want_en_chg_ >= 0)
    changed |= this->correct_bits_("EN_CHG", REG0F_CHG_CTRL0, 0x20, this->want_en_chg_ ? 0x20 : 0x00, r[REG0F_CHG_CTRL0]);
  changed |= this->correct_bits_("EN_TERM", REG0F_CHG_CTRL0, 0x02, 0x02, r[REG0F_CHG_CTRL0]);
  changed |= this->correct_bits_("Safety timers", REG0E_TIMER, 0x38, 0x38, r[REG0E_TIMER]);
  changed |= this->correct_bits_("WATCHDOG", REG10_CHG_CTRL1, 0x07, this->watchdog_ & 0x07, r[REG10_CHG_CTRL1]);

  // EN_HIZ: the charger sets it after a failed poor-source test and by itself retries every 7 minutes.
  // A host that wants the input open clears it sooner, but not in a tight loop.
  const bool hiz_seen = (r[REG0F_CHG_CTRL0] & 0x04) != 0;
  if (hiz_seen != (this->want_hiz_ != 0)) {
    if (this->register_check_ < 2) {
      this->corrections_++;
      if ((this->last_drift_log_ == 0 || millis() - this->last_drift_log_ >= DRIFT_LOG_GAP_MS)) {
        this->last_drift_log_ = millis();
        ESP_LOGW(TAG, "EN_HIZ is %s but the host asked for %s (status0 0x%02X, VBUS %s; watch only, not changed)",
                 hiz_seen ? "set" : "clear", this->want_hiz_ ? "set" : "clear", r[REG1B_STATUS0],
                 (r[REG1B_STATUS0] & 0x01) ? "present" : "absent");
      }
    } else if (this->want_hiz_ != 0 || (this->last_hiz_clear_ == 0 || millis() - this->last_hiz_clear_ >= HIZ_CLEAR_MIN_GAP_MS)) {
      this->last_hiz_clear_ = millis();
      ESP_LOGW(TAG, "EN_HIZ is %s but the host wants it %s (status0 0x%02X, VBUS %s): correcting",
               hiz_seen ? "set" : "clear", this->want_hiz_ ? "set" : "clear", r[REG1B_STATUS0],
               (r[REG1B_STATUS0] & 0x01) ? "present" : "absent");
      this->corrections_++;
      this->modify_u8_(REG0F_CHG_CTRL0, 0x04, this->want_hiz_ ? 0x04 : 0x00);
      changed = true;
    }
  }

  changed |= this->correct_bits_("AUTO_INDET_EN", REG11_CHG_CTRL2, 0x40, this->auto_input_detection_ ? 0x40 : 0x00,
                                 r[REG11_CHG_CTRL2]);
  changed |= this->correct_bits_("EN_OTG", REG12_CHG_CTRL3, 0x40, 0x00, r[REG12_CHG_CTRL3]);
  changed |= this->correct_bits_("EN_IBUS_OCP", REG13_CHG_CTRL4, 0x01, 0x01, r[REG13_CHG_CTRL4]);
  changed |= this->correct_bits_("Hiccup protections", REG13_CHG_CTRL4, 0x0C, 0x00, r[REG13_CHG_CTRL4]);
  changed |= this->correct_bits_("EN_IBAT", REG14_CHG_CTRL5, 0x20, 0x20, r[REG14_CHG_CTRL5]);
  changed |= this->correct_bits_("EN_IINDPM", REG14_CHG_CTRL5, 0x04, 0x04, r[REG14_CHG_CTRL5]);
  changed |= this->correct_bits_("SFET_PRESENT", REG14_CHG_CTRL5, 0x80, this->ship_fet_present_ ? 0x80 : 0x00,
                                 r[REG14_CHG_CTRL5]);
  // The charger holds EN_MPPT at 0 whenever VBUS is below its present threshold, and it cannot be set while EN_HIZ is 1
  // (datasheet 7.3.6.3). Without this guard, enforce rewrites it every 5 s all night and counts each one as a correction.
  if (this->mppt_requested_ && (r[REG1B_STATUS0] & 0x01) != 0 && !hiz_seen)
    changed |= this->correct_bits_("EN_MPPT", REG15_MPPT, 0x01, 0x01, r[REG15_MPPT]);
  if (this->want_voc_pct_ >= 0)
    changed |= this->correct_bits_("VOC_PCT", REG15_MPPT, 0xE0, static_cast<uint8_t>(this->want_voc_pct_ << 5),
                                   r[REG15_MPPT]);
  if (this->want_voc_dly_ >= 0)
    changed |= this->correct_bits_("VOC_DLY", REG15_MPPT, 0x18, static_cast<uint8_t>(this->want_voc_dly_ << 3),
                                   r[REG15_MPPT]);
  if (this->want_voc_rate_ >= 0)
    changed |= this->correct_bits_("VOC_RATE", REG15_MPPT, 0x06, static_cast<uint8_t>(this->want_voc_rate_ << 1),
                                   r[REG15_MPPT]);
  changed |= this->correct_bits_("TS_IGNORE", REG18_NTC_CTRL1, 0x01, this->ts_ignore_ ? 0x01 : 0x00, r[REG18_NTC_CTRL1]);
  // ADC: on, continuous, every channel enabled. Entering HIZ or a reset turns the ADC off.
  changed |= this->correct_bits_("ADC_EN / ADC_RATE", REG2E_ADC_CTRL, 0xC0, 0x80, r[REG2E_ADC_CTRL]);
  changed |= this->correct_bits_("ADC channel disables 0", REG2F_ADC_DIS0, 0xFE, 0x00, r[REG2F_ADC_DIS0]);
  changed |= this->correct_bits_("ADC channel disables 1", REG30_ADC_DIS1, 0xF0, 0x00, r[REG30_ADC_DIS1]);

  if (changed)
    this->publish_control_states_();

  // Report the running count, but only when it moves (and once at boot so the entity has a value).
  if (this->register_corrections_sensor_ != nullptr && this->corrections_ != this->published_corrections_) {
    this->published_corrections_ = this->corrections_;
    this->register_corrections_sensor_->publish_state(static_cast<float>(this->corrections_));
  }
}

// ---------------------------------------------------------------------------
// Read-back of control state, so Home Assistant reflects the chip at boot
// ---------------------------------------------------------------------------

void BQ25798Component::publish_control_states_() {
  uint8_t ctrl0, ctrl3, ctrl4, mppt;
  if (this->read_u8_(REG0F_CHG_CTRL0, ctrl0)) {
    if (this->charging_switch_ != nullptr)
      this->charging_switch_->publish_state((ctrl0 & 0x20) != 0);
    if (this->hiz_switch_ != nullptr)
      this->hiz_switch_->publish_state((ctrl0 & 0x04) != 0);
    if (this->ico_switch_ != nullptr)
      this->ico_switch_->publish_state((ctrl0 & 0x10) != 0);
  }
  if (this->input_select_ != nullptr && !this->input_busy_ && this->read_u8_(REG12_CHG_CTRL3, ctrl3) &&
      this->read_u8_(REG13_CHG_CTRL4, ctrl4)) {
    // The enable bits show what the charger decided, not what was asked for: once an input is
    // picked the charger clears the other port's bit, so Auto and Force USB read the same. The
    // drop-down therefore keeps showing the request, and only changes when the charger's behaviour
    // contradicts it (a forced port was overridden, or the drivers were switched off or on).
    const bool en1 = (ctrl4 & 0x40) != 0, en2 = (ctrl4 & 0x80) != 0;
    uint8_t status0_now;
    const bool have_status = this->read_u8_(REG1B_STATUS0, status0_now);
    const bool vac1 = have_status && (status0_now & 0x02) != 0;
    const bool vac2 = have_status && (status0_now & 0x04) != 0;
    const bool disabled = (ctrl3 & 0x80) != 0;

    int shown;
    if (this->input_requested_ < 0) {
      // Nothing requested yet (just booted): show what the registers say.
      if (disabled || (!en1 && !en2))
        shown = INPUT_OFF;
      else if (en1 && en2)
        shown = INPUT_AUTO;
      else
        shown = en1 ? INPUT_USB : INPUT_SOLAR;
    } else {
      shown = this->input_requested_;
      if (shown == INPUT_OFF) {
        if (!disabled)
          shown = INPUT_AUTO;
      } else if (disabled) {
        shown = INPUT_OFF;
      } else if (shown == INPUT_USB && en2 && vac2) {
        shown = INPUT_AUTO;
      } else if (shown == INPUT_SOLAR && en1 && vac1) {
        shown = INPUT_AUTO;
      }
    }
    const auto &options = this->input_select_->traits.get_options();
    if (static_cast<size_t>(shown) < options.size()) {
      if (this->input_requested_ >= 0 && shown != this->input_requested_) {
        ESP_LOGW(TAG, "Input selection was set to '%s' but the charger overrode it; showing '%s'",
                 std::string(options[this->input_requested_]).c_str(), std::string(options[shown]).c_str());
      }
      this->input_requested_ = static_cast<int8_t>(shown);
      this->input_select_->publish_state(options[shown]);
    }
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
    // EN_MPPT is not read back into the switch or into mppt_requested_. The charger clears it by itself whenever
    // VBUS goes away (every night on solar), so the register says "off" at dawn and after a reboot in the dark.
    // Publishing that would overwrite the saved choice, the switch would restore itself to off, and the board
    // would start the panel without MPPT: the charger then drags the panel down to VINDPM, fails its start-up
    // check and latches EN_HIZ. The switch holds what was asked for, and reconcile_ and update() re-assert it.
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
    if (this->active_input_text_sensor_ != nullptr) {
      uint8_t c3, c4;
      std::string active = "Unknown";
      if (this->read_u8_(REG12_CHG_CTRL3, c3) && this->read_u8_(REG13_CHG_CTRL4, c4)) {
        const bool vbus = (status0 & 0x01) != 0;
        const bool cand1 = (c3 & 0x80) == 0 && (status0 & 0x02) != 0 && (c4 & 0x40) != 0;
        const bool cand2 = (c3 & 0x80) == 0 && (status0 & 0x04) != 0 && (c4 & 0x80) != 0;
        if (!vbus || (!cand1 && !cand2))
          active = "None";
        else if (cand1 && cand2)
          active = "USB or Solar";
        else
          active = cand1 ? "USB" : "Solar";
      }
      this->active_input_text_sensor_->publish_state(active);
    }
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

  if (this->ts_status_text_sensor_ != nullptr) {
    uint8_t status4;
    if (this->read_u8_(REG1F_STATUS4, status4)) {
      // REG0x1F bits 3..0: TS_COLD, TS_COOL, TS_WARM, TS_HOT. With TS_IGNORE set the charger no
      // longer acts on them, and with no thermistor fitted the pin reads cold, so say so.
      const char *text = "Normal";
      if (this->ts_ignore_)
        text = "Ignored";
      else if (status4 & 0x08)
        text = "Cold";
      else if (status4 & 0x04)
        text = "Cool";
      else if (status4 & 0x02)
        text = "Warm";
      else if (status4 & 0x01)
        text = "Hot";
      this->ts_status_text_sensor_->publish_state(text);
    }
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
  if (!std::isnan(this->charge_voltage_limit_)) {
    ESP_LOGCONFIG(TAG, "  Charge voltage limit: %.2f V", this->charge_voltage_limit_);
  }
  if (this->battery_capacity_ > 0) {
    ESP_LOGCONFIG(TAG, "  Battery capacity: %u mAh", this->battery_capacity_);
  }
  ESP_LOGCONFIG(TAG, "  Ship FET populated: %s", YESNO(this->ship_fet_present_));
  if (this->ship_fet_present_) {
    ESP_LOGCONFIG(TAG, "    Action delay: %s", this->ship_fet_action_delay_ ? "10 s" : "none");
    ESP_LOGCONFIG(TAG, "    Battery OCP: %s", ONOFF(this->battery_ocp_));
  }
  ESP_LOGCONFIG(TAG, "  Watchdog: %s", this->watchdog_ == 0 ? "disabled" : "enabled");
  ESP_LOGCONFIG(TAG, "  TS ignore: %s", ONOFF(this->ts_ignore_));
  ESP_LOGCONFIG(TAG, "  Automatic D+/D- input detection: %s", ONOFF(this->auto_input_detection_));
  ESP_LOGCONFIG(TAG, "  ADC off before sleep: %s", ONOFF(this->adc_off_before_sleep_));
}

}  // namespace bq25798
}  // namespace esphome
