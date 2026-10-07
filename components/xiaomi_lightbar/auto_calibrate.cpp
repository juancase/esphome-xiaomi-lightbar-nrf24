#include "auto_calibrate.h"

#ifdef USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE

#include "esphome/core/log.h"

#include "xiaomi_lightbar_light.h"

namespace esphome {
namespace xiaomi_lightbar {

static const char *const TAG = "xiaomi_lightbar.auto_calibrate";

/* -- Switch -------------------------------------------------------------------------------------------------- */

void AutoCalibrateSwitch::setup() {
  // The switch stores its state itself, according to restore_mode (on by default).
  this->publish_state(this->get_initial_state_with_restore_mode().value_or(true));
}

void AutoCalibrateSwitch::write_state(bool state) {
  this->publish_state(state);
  this->parent_->on_auto_calibrate_changed();
}

void AutoCalibrateSwitch::dump_config() { LOG_SWITCH("", "Auto calibrate", this); }

/* -- Time ---------------------------------------------------------------------------------------------------- */

void AutoCalibrateTime::setup() {
  datetime::TimeEntityRestoreState restored;
  this->pref_ = this->make_entity_preference<datetime::TimeEntityRestoreState>();
  if (this->pref_.load(&restored)) {
    restored.apply(this);
    return;
  }
  this->hour_ = this->initial_value_.hour;
  this->minute_ = this->initial_value_.minute;
  this->second_ = this->initial_value_.second;
  this->publish_state();
}

void AutoCalibrateTime::control(const datetime::TimeCall &call) {
  if (call.get_hour().has_value())
    this->hour_ = *call.get_hour();
  if (call.get_minute().has_value())
    this->minute_ = *call.get_minute();
  if (call.get_second().has_value())
    this->second_ = *call.get_second();
  this->publish_state();

  datetime::TimeEntityRestoreState state{this->hour_, this->minute_, this->second_};
  this->pref_.save(&state);
  this->parent_->on_auto_calibrate_changed();
}

void AutoCalibrateTime::dump_config() { LOG_DATETIME_TIME("", "Auto calibrate time", this); }

/* -- Number -------------------------------------------------------------------------------------------------- */

void AutoCalibrateNumber::setup() {
  float value;
  this->pref_ = this->make_entity_preference<float>();
  if (!this->pref_.load(&value))
    value = this->initial_value_;
  this->publish_state(value);
}

void AutoCalibrateNumber::control(float value) {
  this->publish_state(value);
  this->pref_.save(&value);
}

void AutoCalibrateNumber::dump_config() { LOG_NUMBER("", "Auto calibrate quiet minutes", this); }

}  // namespace xiaomi_lightbar
}  // namespace esphome

#endif  // USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE
