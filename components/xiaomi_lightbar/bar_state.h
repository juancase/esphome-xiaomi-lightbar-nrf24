#pragma once

#include <cstdint>
#include <vector>

#include "protocol.h"

namespace esphome {
namespace xiaomi_lightbar {

/*
 * The light bar never reports its state, so this class keeps two sets of values:
 *
 *  - requested: the state shown in Home Assistant (requested by it, or set by the remote or a
 *               calibration).
 *  - tracked:   what the bar is believed to show, based on everything sent or overheard.
 *
 * Requests are merged for COALESCE_MS, then the difference between requested and tracked values is
 * sent as one relative command, like turning the remote's wheel, so the bar fades straight to the
 * new value.
 *
 * Only an unknown value (after booting, after the wheel was turned while the bar was off, after
 * reanchor()) is set absolutely: saturate to one end of the scale, then step to the target. After a
 * reset, see apply_reset_().
 *
 * Commands are queued and fetched with take_commands(); time is passed in as now_ms. Both keep the
 * class free of hardware, so it can be tested on the PC.
 */
class BarState {
 public:
  struct OutCommand {
    Command command;
    uint8_t options;
  };

  // Highest brightness and color temperature level of the bar. Both scales are 0 – 15.
  static constexpr uint8_t MAX_LEVEL = 15;
  // How long (in ms) to wait for further requests before sending anything to the bar. Requests
  // arriving in quick succession (e.g. a slider being dragged) are merged into one relative step.
  static constexpr uint32_t COALESCE_MS = 150;
  static constexpr uint32_t MIN_KELVIN = 2700;
  static constexpr uint32_t MAX_KELVIN = 6500;

  // The pairing command is a reset, which also changes brightness and color temperature, see
  // apply_reset_(). The bar is on afterwards (it was just powered on).
  void pair(uint32_t now_ms);

  // Requests, applied by loop() once no further request arrived for COALESCE_MS.
  void request_on_off(bool on, uint32_t now_ms);
  void request_brightness(uint8_t value, uint32_t now_ms);
  void request_kelvin(uint32_t kelvin, uint32_t now_ms);
  void loop(uint32_t now_ms);

  // Calibration: tell the controller what the bar really shows, without sending anything.
  void sync_on_off(bool on, uint32_t now_ms);
  void sync_brightness(uint8_t value, uint32_t now_ms);
  void sync_kelvin(uint32_t kelvin, uint32_t now_ms);
  // Forget tracked brightness and temperature and set them again absolutely.
  void reanchor(uint32_t now_ms);
  // Restore the last published state after a reboot. Brightness / kelvin < 0 mean "not known".
  // Brightness and temperature stay untracked, so the next change is sent absolutely once.
  void restore_state(bool on, int brightness, int kelvin);

  // Keep track of what the original remote did to the bar. Only queues commands after a reset (holding
  // the button), see apply_reset_().
  void track_remote_command(uint8_t command, uint8_t options, uint32_t now_ms);
  // A remote the bar is not paired with: pass its command on to the bar unchanged, and keep track of it.
  void relay_remote_command(uint8_t command, uint8_t options, uint32_t now_ms);

  // Commands to send, in order. Clears the queue.
  std::vector<OutCommand> take_commands();

  bool is_on() const { return this->requested_on_; }
  bool has_brightness() const { return this->has_requested_brightness_; }
  uint8_t brightness() const { return this->requested_brightness_; }
  bool has_kelvin() const { return this->has_requested_temperature_; }
  uint32_t kelvin() const { return this->requested_kelvin_; }
  uint32_t last_activity() const { return this->last_activity_at_; }

  // Clamps a color temperature to the bar's range. Values below 1000 are taken as mireds and converted.
  static uint32_t to_kelvin(uint32_t value);
  static uint8_t kelvin_to_level(uint32_t kelvin);
  static uint32_t level_to_kelvin(uint8_t level);
  // Brightness (0.0 – 1.0) and level convert into each other without loss. Level L is (L + 1) / 16, so
  // the lowest level is not 0.0: ESPHome takes brightness 0.0 as "off", but the bar is still lit then.
  static uint8_t brightness_to_level(float brightness);
  static float level_to_brightness(uint8_t level);

 protected:
  void send_(Command command, uint8_t options = 0x0);
  void apply_();
  void apply_brightness_();
  void apply_temperature_();
  void schedule_(uint32_t now_ms);
  void touch_(uint32_t now_ms) { this->last_activity_at_ = now_ms; }
  void apply_reset_(uint32_t now_ms);
  static uint8_t clamp_level_(int value);

  std::vector<OutCommand> outbox_;

  bool requested_on_{false};
  uint8_t requested_brightness_{0};
  uint32_t requested_kelvin_{0};
  bool has_requested_brightness_{false};
  bool has_requested_temperature_{false};

  bool tracked_on_{false};
  uint8_t tracked_brightness_{0};
  uint8_t tracked_temperature_{0};  // 0 warmest – 15 coldest
  bool brightness_known_{false};
  bool temperature_known_{false};
  // Set when a value was requested since the last apply. An unknown value is only set absolutely
  // (which makes the bar dip to its minimum) if somebody actually asked for it.
  bool brightness_changed_{false};
  bool temperature_changed_{false};

  // Set after a reset: brightness and temperature are taken to their limits once the bar is on.
  bool saturate_pending_{false};
  bool pending_{false};
  uint32_t last_request_at_{0};
  uint32_t last_activity_at_{0};
};

}  // namespace xiaomi_lightbar
}  // namespace esphome
