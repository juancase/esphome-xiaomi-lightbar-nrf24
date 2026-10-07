#include "daily_calibration.h"

namespace esphome {
namespace xiaomi_lightbar {

bool DailyCalibration::check(int day, int minute_of_day, int target_minute, uint32_t now_ms,
                             uint32_t last_activity_ms, uint32_t quiet_ms) {
  const bool due = minute_of_day >= target_minute;

  if (!this->initialized_) {
    // First check: if today's time has already passed, count today as done.
    this->initialized_ = true;
    this->has_run_ = due;
    this->last_run_day_ = day;
  }

  if (!due || (this->has_run_ && day == this->last_run_day_))
    return false;
  // Don't overrule the tracked state while somebody is actually using the bar. Unsigned subtraction,
  // so millis() wrapping around is handled correctly.
  if (now_ms - last_activity_ms < quiet_ms)
    return false;

  this->has_run_ = true;
  this->last_run_day_ = day;
  return true;
}

}  // namespace xiaomi_lightbar
}  // namespace esphome
