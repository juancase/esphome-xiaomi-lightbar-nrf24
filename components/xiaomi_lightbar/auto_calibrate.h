#pragma once

#include "esphome/core/defines.h"

#ifdef USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE

#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/core/time.h"
#include "esphome/components/datetime/time_entity.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace xiaomi_lightbar {

class XiaomiLightbarLight;

/*
 * The settings of the daily calibration, changeable from Home Assistant. All three keep their value
 * across reboots. The switch and the time tell the light when they change; the quiet minutes are read
 * at every check.
 */

class AutoCalibrateSwitch : public switch_::Switch, public Component {
 public:
  void set_parent(XiaomiLightbarLight *parent) { this->parent_ = parent; }
  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;

  XiaomiLightbarLight *parent_{nullptr};
};

class AutoCalibrateTime : public datetime::TimeEntity, public Component {
 public:
  void set_parent(XiaomiLightbarLight *parent) { this->parent_ = parent; }
  void set_initial_value(ESPTime initial_value) { this->initial_value_ = initial_value; }
  void setup() override;
  void dump_config() override;

  int minute_of_day() const { return this->hour_ * 60 + this->minute_; }

 protected:
  void control(const datetime::TimeCall &call) override;

  XiaomiLightbarLight *parent_{nullptr};
  ESPTime initial_value_{};
  ESPPreferenceObject pref_;
};

class AutoCalibrateNumber : public number::Number, public Component {
 public:
  void set_initial_value(float initial_value) { this->initial_value_ = initial_value; }
  void setup() override;
  void dump_config() override;

 protected:
  void control(float value) override;

  float initial_value_{0};
  ESPPreferenceObject pref_;
};

}  // namespace xiaomi_lightbar
}  // namespace esphome

#endif  // USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE
