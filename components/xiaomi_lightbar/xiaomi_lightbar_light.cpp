#include "xiaomi_lightbar_light.h"

#include <cmath>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace xiaomi_lightbar {

static const char *const TAG = "xiaomi_lightbar.light";

light::LightTraits XiaomiLightbarLight::get_traits() {
  auto traits = light::LightTraits();
  traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
  traits.set_min_mireds(1000000.0f / BarState::MAX_KELVIN);
  traits.set_max_mireds(1000000.0f / BarState::MIN_KELVIN);
  return traits;
}

void XiaomiLightbarLight::setup() {
  this->hub_->add_remote_listener(
      this->serial_, [this](uint8_t command, uint8_t options) { this->on_remote_(command, options, false); });
  for (uint32_t remote : this->remotes_) {
    this->hub_->add_remote_listener(
        remote, [this](uint8_t command, uint8_t options) { this->on_remote_(command, options, true); });
  }
#ifdef USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE
  if (this->clock_ != nullptr)
    this->set_interval(1000, [this]() { this->check_auto_calibrate_(); });
#endif
}

void XiaomiLightbarLight::write_state(light::LightState *state) {
  // Raw values, without gamma correction: the bar has its own brightness curve over 16 levels.
  const auto &values = state->current_values;
  const bool on = values.is_on();
  const uint8_t level = BarState::brightness_to_level(values.get_brightness());
  const float mireds = values.get_color_temperature();
  const int kelvin = mireds > 0.0f ? (int) lroundf(1000000.0f / mireds) : -1;
  const uint32_t now = millis();

  if (!this->restored_) {
    this->restored_ = true;
    this->bar_.restore_state(on, level, kelvin);
    ESP_LOGD(TAG, "0x%06" PRIX32 " restored: %s, level %u, %d K", this->serial_, on ? "on" : "off", level, kelvin);
    return;
  }

  // Only request what differs from BarState. After a remote command was published, nothing does.
  if (!this->bar_.has_brightness() || level != this->bar_.brightness())
    this->bar_.request_brightness(level, now);
  if (kelvin > 0 && (!this->bar_.has_kelvin() ||
                     BarState::kelvin_to_level(BarState::to_kelvin(kelvin)) !=
                         BarState::kelvin_to_level(this->bar_.kelvin())))
    this->bar_.request_kelvin(kelvin, now);
  this->bar_.request_on_off(on, now);
}

void XiaomiLightbarLight::loop() {
  this->bar_.loop(millis());
  for (const auto &command : this->bar_.take_commands())
    this->hub_->send_command(this->serial_, command.command, command.options);
}

void XiaomiLightbarLight::on_remote_(uint8_t command, uint8_t options, bool relay) {
  if (relay) {
    this->bar_.relay_remote_command(command, options, millis());
  } else {
    this->bar_.track_remote_command(command, options, millis());
  }
  this->publish_bar_state_();
}

void XiaomiLightbarLight::publish_bar_state_() {
  auto call = this->state_->make_call();
  call.set_state(this->bar_.is_on());
  if (this->bar_.has_brightness())
    call.set_brightness(BarState::level_to_brightness(this->bar_.brightness()));
  if (this->bar_.has_kelvin())
    call.set_color_temperature(1000000.0f / this->bar_.kelvin());
  call.perform();
}

void XiaomiLightbarLight::pair() {
  ESP_LOGI(TAG, "Pairing the bar with 0x%06" PRIX32, this->serial_);
  this->bar_.pair(millis());
  // The bar is on now, and BarState takes it to full brightness and warmest light. Show it in Home
  // Assistant; the write_state() that follows finds nothing more to send.
  this->publish_bar_state_();
}

void XiaomiLightbarLight::calibrate(bool on) {
  ESP_LOGI(TAG, "Calibration: 0x%06" PRIX32 " is %s", this->serial_, on ? "on" : "off");
  this->bar_.sync_on_off(on, millis());
  this->publish_bar_state_();
}

void XiaomiLightbarLight::resend() {
  ESP_LOGI(TAG, "Calibration: setting brightness and temperature of 0x%06" PRIX32 " again", this->serial_);
  this->bar_.reanchor(millis());
}

#ifdef USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE
void XiaomiLightbarLight::check_auto_calibrate_() {
  if (!this->auto_calibrate_enabled_->state)
    return;
  const ESPTime now = this->clock_->now();
  if (!now.is_valid())
    return;

  const float quiet_minutes = this->auto_calibrate_quiet_minutes_->state;
  const uint32_t quiet_ms = std::isnan(quiet_minutes) ? 0 : (uint32_t) quiet_minutes * 60UL * 1000UL;
  if (this->daily_calibration_.check(now.year * 1000 + now.day_of_year, now.hour * 60 + now.minute,
                                     this->auto_calibrate_time_->minute_of_day(), millis(),
                                     this->bar_.last_activity(), quiet_ms)) {
    ESP_LOGI(TAG, "Scheduled calibration: assuming 0x%06" PRIX32 " is off", this->serial_);
    this->bar_.sync_on_off(false, millis());
    this->publish_bar_state_();
  }
}
#endif

void LightButton::press_action() {
  switch (this->action_) {
    case LightAction::PAIR:
      this->parent_->pair();
      break;
    case LightAction::CALIBRATE_ON:
      this->parent_->calibrate(true);
      break;
    case LightAction::CALIBRATE_OFF:
      this->parent_->calibrate(false);
      break;
    case LightAction::RESEND:
      this->parent_->resend();
      break;
  }
}

void XiaomiLightbarLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Xiaomi Light Bar:");
  ESP_LOGCONFIG(TAG, "  Serial: 0x%06" PRIX32, this->serial_);
  for (uint32_t remote : this->remotes_)
    ESP_LOGCONFIG(TAG, "  Relaying remote: 0x%06" PRIX32, remote);
#ifdef USE_XIAOMI_LIGHTBAR_AUTO_CALIBRATE
  if (this->clock_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Daily calibration: %s at %02u:%02u", this->auto_calibrate_enabled_->state ? "on" : "off",
                  this->auto_calibrate_time_->hour, this->auto_calibrate_time_->minute);
#endif
}

}  // namespace xiaomi_lightbar
}  // namespace esphome
