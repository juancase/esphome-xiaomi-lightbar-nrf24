#include "xiaomi_lightbar_remote.h"

#include "esphome/core/log.h"

namespace esphome {
namespace xiaomi_lightbar {

static const char *const TAG = "xiaomi_lightbar.remote";

void XiaomiLightbarRemote::setup() {
  this->hub_->add_remote_listener(this->serial_, [this](uint8_t command, uint8_t) { this->on_command_(command); });
}

// The gestures of the remote: turning the wheel changes the brightness, turning it while pressed
// changes the color temperature, holding the button resets the bar.
void XiaomiLightbarRemote::on_command_(uint8_t command) {
  switch ((Command) command) {
    case Command::ON_OFF:
      this->trigger("press");
      break;
    case Command::COOLER:
      this->trigger("press_rotate_right");
      break;
    case Command::WARMER:
      this->trigger("press_rotate_left");
      break;
    case Command::BRIGHTER:
      this->trigger("rotate_right");
      break;
    case Command::DIMMER:
      this->trigger("rotate_left");
      break;
    case Command::RESET:
      this->trigger("hold");
      break;
  }
}

void XiaomiLightbarRemote::dump_config() {
  LOG_EVENT("", "Xiaomi Light Bar remote", this);
  ESP_LOGCONFIG(TAG, "  Serial: 0x%06" PRIX32, this->serial_);
}

}  // namespace xiaomi_lightbar
}  // namespace esphome
