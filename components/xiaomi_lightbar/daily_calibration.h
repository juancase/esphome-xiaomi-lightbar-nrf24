#pragma once

#include <cstdint>

namespace esphome {
namespace xiaomi_lightbar {

/*
 * Decides when the daily calibration runs: once a day, at a given local time, but only after the bar
 * was left alone for a while. While somebody uses it, the calibration is postponed (until midnight at
 * the latest; then it waits for the next day).
 *
 * The first check after booting (or after reset()) never runs it: if today's time has already passed,
 * the calibration waits for tomorrow. Otherwise changing the time or switching the calibration on would
 * run it right away.
 *
 * Time is passed in, so the class can be tested without hardware. `day` is any number that changes once
 * a day, `minute_of_day` is 0 – 1439, all *_ms values are millis().
 */
class DailyCalibration {
 public:
  bool check(int day, int minute_of_day, int target_minute, uint32_t now_ms, uint32_t last_activity_ms,
             uint32_t quiet_ms);
  // Call when the time or the switch changed: the next check behaves like the first one after booting.
  void reset() { this->initialized_ = false; }

 protected:
  bool initialized_{false};
  bool has_run_{false};
  int last_run_day_{0};
};

}  // namespace xiaomi_lightbar
}  // namespace esphome
