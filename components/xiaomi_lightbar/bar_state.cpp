#include "bar_state.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace esphome {
namespace xiaomi_lightbar {

void BarState::send_(Command command, uint8_t options) { this->outbox_.push_back(OutCommand{command, options}); }

std::vector<BarState::OutCommand> BarState::take_commands() {
  std::vector<OutCommand> commands;
  std::swap(commands, this->outbox_);
  return commands;
}

void BarState::pair(uint32_t now_ms) {
  this->send_(Command::RESET);
  // Pairing only works right after the bar was powered on, and it always powers on lit.
  this->requested_on_ = true;
  this->tracked_on_ = true;
  this->apply_reset_(now_ms);
}

// The levels a reset leaves cannot be relied on, so they are driven to their limits (full brightness,
// warmest light) with one relative command each, which works from any starting level. Both are known
// afterwards, so the remote can be followed right away. This goes through the coalescing window, so
// nothing is sent while the remote keeps repeating the reset (holding the button).
void BarState::apply_reset_(uint32_t now_ms) {
  this->requested_brightness_ = MAX_LEVEL;
  this->has_requested_brightness_ = true;
  this->brightness_known_ = false;
  this->brightness_changed_ = false;

  this->requested_kelvin_ = MIN_KELVIN;
  this->has_requested_temperature_ = true;
  this->temperature_known_ = false;
  this->temperature_changed_ = false;

  this->saturate_pending_ = true;
  this->schedule_(now_ms);
}

/* -- Requests ------------------------------------------------------------------------------------------------ */

void BarState::request_on_off(bool on, uint32_t now_ms) {
  this->requested_on_ = on;
  this->schedule_(now_ms);
}

void BarState::request_brightness(uint8_t value, uint32_t now_ms) {
  this->requested_brightness_ = clamp_level_(value);
  this->has_requested_brightness_ = true;
  this->brightness_changed_ = true;
  this->schedule_(now_ms);
}

void BarState::request_kelvin(uint32_t kelvin, uint32_t now_ms) {
  this->requested_kelvin_ = to_kelvin(kelvin);
  this->has_requested_temperature_ = true;
  this->temperature_changed_ = true;
  this->schedule_(now_ms);
}

void BarState::schedule_(uint32_t now_ms) {
  this->pending_ = true;
  this->last_request_at_ = now_ms;
  this->touch_(now_ms);
}

void BarState::loop(uint32_t now_ms) {
  // Unsigned subtraction, so millis() wrapping around is handled correctly.
  if (this->pending_ && now_ms - this->last_request_at_ >= COALESCE_MS)
    this->apply_();
}

void BarState::apply_() {
  this->pending_ = false;

  if (this->requested_on_ && !this->tracked_on_) {
    this->send_(Command::ON_OFF);
    this->tracked_on_ = true;
  }

  // Brightness and temperature are only sent while the bar is on. While it is off, the requested
  // values are kept and sent as soon as it is turned on again.
  if (this->requested_on_ && this->tracked_on_) {
    if (this->saturate_pending_) {
      this->saturate_pending_ = false;
      this->send_(Command::BRIGHTER, MAX_LEVEL + 1);
      this->send_(Command::WARMER, (uint8_t) (0x0 - (MAX_LEVEL + 1)));
      this->tracked_brightness_ = MAX_LEVEL;
      this->tracked_temperature_ = 0;
      this->brightness_known_ = true;
      this->temperature_known_ = true;
    }
    this->apply_brightness_();
    this->apply_temperature_();
  }

  if (!this->requested_on_ && this->tracked_on_) {
    this->send_(Command::ON_OFF);
    this->tracked_on_ = false;
  }
}

// The options byte of commands 0x02 – 0x05 is a signed number of steps. The encodings below are the
// ones the original remote uses for a wheel turn: 0x04 +n / 0x05 -n for brightness, 0x02 +n (cooler)
// and 0x03 -n (warmer) for the color temperature. One step is one of the 16 levels.
void BarState::apply_brightness_() {
  if (!this->has_requested_brightness_ || (!this->brightness_known_ && !this->brightness_changed_))
    return;
  this->brightness_changed_ = false;

  if (this->brightness_known_) {
    int8_t steps = (int8_t) this->requested_brightness_ - (int8_t) this->tracked_brightness_;
    if (steps != 0)
      this->send_(steps > 0 ? Command::BRIGHTER : Command::DIMMER, (uint8_t) steps);
  } else {
    // Current value unknown: saturate to the minimum first, then go up to the desired value. See
    // https://github.com/lamperez/xiaomi-lightbar-nrf24?tab=readme-ov-file#command-codes
    this->send_(Command::DIMMER, (uint8_t) (0x0 - 16));
    this->send_(Command::BRIGHTER, this->requested_brightness_);
  }
  this->tracked_brightness_ = this->requested_brightness_;
  this->brightness_known_ = true;
}

void BarState::apply_temperature_() {
  if (!this->has_requested_temperature_ || (!this->temperature_known_ && !this->temperature_changed_))
    return;
  this->temperature_changed_ = false;

  uint8_t level = kelvin_to_level(this->requested_kelvin_);
  if (this->temperature_known_) {
    int8_t steps = (int8_t) level - (int8_t) this->tracked_temperature_;
    if (steps != 0)
      this->send_(steps > 0 ? Command::COOLER : Command::WARMER, (uint8_t) steps);
  } else {
    // Current value unknown: saturate to the warmest value first, then go to the desired value.
    this->send_(Command::COOLER, (uint8_t) (0x0 - 16));
    this->send_(Command::WARMER, level);
  }
  this->tracked_temperature_ = level;
  this->temperature_known_ = true;
}

/* -- Calibration --------------------------------------------------------------------------------------------- */

void BarState::sync_on_off(bool on, uint32_t now_ms) {
  this->requested_on_ = on;
  this->tracked_on_ = on;
  this->touch_(now_ms);
}

void BarState::sync_brightness(uint8_t value, uint32_t now_ms) {
  this->requested_brightness_ = clamp_level_(value);
  this->has_requested_brightness_ = true;
  this->tracked_brightness_ = this->requested_brightness_;
  this->brightness_known_ = true;
  this->touch_(now_ms);
}

void BarState::sync_kelvin(uint32_t kelvin, uint32_t now_ms) {
  this->requested_kelvin_ = to_kelvin(kelvin);
  this->has_requested_temperature_ = true;
  this->tracked_temperature_ = kelvin_to_level(this->requested_kelvin_);
  this->temperature_known_ = true;
  this->touch_(now_ms);
}

void BarState::reanchor(uint32_t now_ms) {
  this->brightness_known_ = false;
  this->temperature_known_ = false;
  this->brightness_changed_ = true;
  this->temperature_changed_ = true;
  this->schedule_(now_ms);
}

void BarState::restore_state(bool on, int brightness, int kelvin) {
  this->requested_on_ = on;
  this->tracked_on_ = on;
  if (brightness >= 0) {
    this->requested_brightness_ = clamp_level_(brightness);
    this->has_requested_brightness_ = true;
  }
  if (kelvin > 0) {
    this->requested_kelvin_ = to_kelvin(kelvin);
    this->has_requested_temperature_ = true;
  }
}

/* -- Original remote ----------------------------------------------------------------------------------------- */

void BarState::track_remote_command(uint8_t command, uint8_t options, uint32_t now_ms) {
  int8_t steps = (int8_t) options;
  switch ((Command) command) {
    case Command::ON_OFF:
      this->tracked_on_ = !this->tracked_on_;
      this->requested_on_ = this->tracked_on_;
      break;

    case Command::BRIGHTER:
    case Command::DIMMER:
      // We can't tell whether the bar reacts to the wheel while it is off, so forget the value then.
      if (this->tracked_on_ && this->brightness_known_) {
        this->tracked_brightness_ = clamp_level_(this->tracked_brightness_ + steps);
        this->requested_brightness_ = this->tracked_brightness_;
        this->has_requested_brightness_ = true;
      } else {
        this->brightness_known_ = false;
      }
      break;

    case Command::COOLER:
    case Command::WARMER:
      if (this->tracked_on_ && this->temperature_known_) {
        this->tracked_temperature_ = clamp_level_(this->tracked_temperature_ + steps);
        this->requested_kelvin_ = level_to_kelvin(this->tracked_temperature_);
        this->has_requested_temperature_ = true;
      } else {
        this->temperature_known_ = false;
      }
      break;

    case Command::RESET:
      // Holding the button resets the bar.
      this->apply_reset_(now_ms);
      break;
  }
  this->touch_(now_ms);
}

void BarState::relay_remote_command(uint8_t command, uint8_t options, uint32_t now_ms) {
  this->send_((Command) command, options);
  this->track_remote_command(command, options, now_ms);
}

/* -- Helpers ------------------------------------------------------------------------------------------------- */

uint8_t BarState::clamp_level_(int value) { return (uint8_t) std::clamp(value, 0, (int) MAX_LEVEL); }

uint32_t BarState::to_kelvin(uint32_t value) {
  if (value > 0 && value < 1000)
    value = (1000000 + value / 2) / value;
  return std::clamp(value, MIN_KELVIN, MAX_KELVIN);
}

// The 16 levels are evenly spaced in mireds, as in lightbar2mqtt.
uint8_t BarState::kelvin_to_level(uint32_t kelvin) {
  const float coldest = 1000000.0f / MAX_KELVIN;
  const float warmest = 1000000.0f / MIN_KELVIN;
  float mireds = 1000000.0f / kelvin;
  return (uint8_t) ((warmest - mireds) / (warmest - coldest) * MAX_LEVEL + 0.5f);
}

uint32_t BarState::level_to_kelvin(uint8_t level) {
  const float coldest = 1000000.0f / MAX_KELVIN;
  const float warmest = 1000000.0f / MIN_KELVIN;
  float mireds = warmest - level * (warmest - coldest) / MAX_LEVEL;
  return (uint32_t) (1000000.0f / mireds + 0.5f);
}

uint8_t BarState::brightness_to_level(float brightness) {
  // The small margin keeps exact multiples of 1/16 (as published by level_to_brightness) on their level.
  return clamp_level_((int) std::ceil(brightness * (MAX_LEVEL + 1) - 0.001f) - 1);
}

float BarState::level_to_brightness(uint8_t level) { return (clamp_level_(level) + 1) / float(MAX_LEVEL + 1); }

}  // namespace xiaomi_lightbar
}  // namespace esphome
