#pragma once

#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"

#ifdef USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE
#include "esphome/components/time/real_time_clock.h"
#include "auto_calibrate.h"
#include "daily_calibration.h"
#endif

#include "bar_state.h"
#include "xiaomi_lightbar.h"

namespace esphome {
namespace xiaomi_lightbar {

/*
 * One light bar. ESPHome passes absolute values; BarState turns them into relative steps and the hub
 * sends them.
 *
 * The first write_state() after booting carries the restored state. It is taken as what the bar shows
 * and nothing is sent, so rebooting the controller never toggles the bar.
 *
 * Commands from the bar's own serial (the remote it is paired with) are only tracked, since the bar
 * already obeyed them; commands from `remotes` are relayed. Either way the new state is published, and
 * the write_state() that follows finds nothing to send because BarState already holds those values (no
 * echo). The calibration buttons work the same way.
 */
class XiaomiLightbarLight : public Component, public light::LightOutput {
 public:
  void set_hub(XiaomiLightbar *hub) { this->hub_ = hub; }
  void set_serial(uint32_t serial) { this->serial_ = serial; }
  void add_remote(uint32_t serial) { this->remotes_.push_back(serial); }

  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override { this->state_ = state; }
  void write_state(light::LightState *state) override;

  void setup() override;
  void loop() override;
  void dump_config() override;

  // Pair the bar with this serial: power-cycle the bar, then call this within 10 seconds. The bar
  // blinks when it accepts the serial. Afterwards it is set to full brightness and warmest light.
  void pair();
  // Calibration: tell the controller whether the bar is on, without sending anything.
  void calibrate(bool on);
  // Calibration: set brightness and color temperature again absolutely (now, or once the bar is on).
  void resend();

#ifdef USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE
  // Once a day, assume the bar is off, unless it was used within the last quiet minutes.
  void set_auto_calibrate(time::RealTimeClock *clock, AutoCalibrateSwitch *enabled, AutoCalibrateTime *time,
                          AutoCalibrateNumber *quiet_minutes) {
    this->clock_ = clock;
    this->auto_calibrate_enabled_ = enabled;
    this->auto_calibrate_time_ = time;
    this->auto_calibrate_quiet_minutes_ = quiet_minutes;
  }
  void on_auto_calibrate_changed() { this->daily_calibration_.reset(); }
#endif

 protected:
  void on_remote_(uint8_t command, uint8_t options, bool relay);
  void publish_bar_state_();

  XiaomiLightbar *hub_{nullptr};
  light::LightState *state_{nullptr};
  uint32_t serial_{0};
  std::vector<uint32_t> remotes_;
  BarState bar_;

  bool restored_{false};

#ifdef USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE
  void check_auto_calibrate_();

  time::RealTimeClock *clock_{nullptr};
  AutoCalibrateSwitch *auto_calibrate_enabled_{nullptr};
  AutoCalibrateTime *auto_calibrate_time_{nullptr};
  AutoCalibrateNumber *auto_calibrate_quiet_minutes_{nullptr};
  DailyCalibration daily_calibration_;
#endif
};

enum class LightAction { PAIR, CALIBRATE_ON, CALIBRATE_OFF, RESEND };

class LightButton : public button::Button {
 public:
  void set_parent(XiaomiLightbarLight *parent) { this->parent_ = parent; }
  void set_action(LightAction action) { this->action_ = action; }

 protected:
  void press_action() override;

  XiaomiLightbarLight *parent_{nullptr};
  LightAction action_{LightAction::PAIR};
};

}  // namespace xiaomi_lightbar
}  // namespace esphome
