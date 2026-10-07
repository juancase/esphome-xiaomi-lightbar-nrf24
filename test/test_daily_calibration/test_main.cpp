#include <unity.h>

#include "daily_calibration.h"

using namespace esphome::xiaomi_lightbar;

static constexpr int AT_4 = 4 * 60;
static constexpr uint32_t QUIET = 60 * 60 * 1000UL;
// Long after booting, with no activity since then.
static constexpr uint32_t IDLE = 10 * QUIET;

void setUp() {}
void tearDown() {}

void test_runs_once_at_the_given_time() {
  DailyCalibration calibration;
  TEST_ASSERT_FALSE(calibration.check(1, AT_4 - 1, AT_4, IDLE, 0, QUIET));
  TEST_ASSERT_TRUE(calibration.check(1, AT_4, AT_4, IDLE, 0, QUIET));
  TEST_ASSERT_FALSE(calibration.check(1, AT_4 + 1, AT_4, IDLE, 0, QUIET));
  TEST_ASSERT_FALSE(calibration.check(1, 23 * 60, AT_4, IDLE, 0, QUIET));
}

void test_runs_again_the_next_day() {
  DailyCalibration calibration;
  calibration.check(1, AT_4 - 1, AT_4, IDLE, 0, QUIET);
  TEST_ASSERT_TRUE(calibration.check(1, AT_4, AT_4, IDLE, 0, QUIET));
  TEST_ASSERT_FALSE(calibration.check(2, 0, AT_4, IDLE, 0, QUIET));
  TEST_ASSERT_TRUE(calibration.check(2, AT_4, AT_4, IDLE, 0, QUIET));
}

void test_booting_after_the_time_waits_for_tomorrow() {
  DailyCalibration calibration;
  TEST_ASSERT_FALSE(calibration.check(1, AT_4 + 30, AT_4, IDLE, 0, QUIET));
  TEST_ASSERT_FALSE(calibration.check(1, AT_4 + 31, AT_4, IDLE, 0, QUIET));
  TEST_ASSERT_TRUE(calibration.check(2, AT_4, AT_4, IDLE, 0, QUIET));
}

void test_postponed_while_the_bar_is_used() {
  DailyCalibration calibration;
  calibration.check(1, AT_4 - 1, AT_4, IDLE, 0, QUIET);
  const uint32_t used_at = IDLE - 1000;
  TEST_ASSERT_FALSE(calibration.check(1, AT_4, AT_4, IDLE, used_at, QUIET));
  TEST_ASSERT_FALSE(calibration.check(1, AT_4 + 59, AT_4, used_at + QUIET - 1, used_at, QUIET));
  TEST_ASSERT_TRUE(calibration.check(1, AT_4 + 59, AT_4, used_at + QUIET, used_at, QUIET));
}

void test_postponed_past_midnight_waits_for_the_next_time() {
  DailyCalibration calibration;
  calibration.check(1, 22 * 60, 23 * 60, IDLE, IDLE, QUIET);
  TEST_ASSERT_FALSE(calibration.check(1, 23 * 60, 23 * 60, IDLE, IDLE, QUIET));
  // Next day, before the time: not due, even though the bar is idle now.
  TEST_ASSERT_FALSE(calibration.check(2, 30, 23 * 60, IDLE + QUIET, IDLE, QUIET));
  TEST_ASSERT_TRUE(calibration.check(2, 23 * 60, 23 * 60, IDLE + QUIET, IDLE, QUIET));
}

void test_no_quiet_time_runs_right_away() {
  DailyCalibration calibration;
  calibration.check(1, AT_4 - 1, AT_4, IDLE, 0, 0);
  TEST_ASSERT_TRUE(calibration.check(1, AT_4, AT_4, IDLE, IDLE, 0));
}

void test_changing_the_time_to_the_past_waits_for_tomorrow() {
  DailyCalibration calibration;
  calibration.check(1, 10 * 60, 23 * 60, IDLE, 0, QUIET);
  calibration.reset();
  TEST_ASSERT_FALSE(calibration.check(1, 10 * 60, 9 * 60, IDLE, 0, QUIET));
  TEST_ASSERT_TRUE(calibration.check(2, 9 * 60, 9 * 60, IDLE, 0, QUIET));
}

void test_changing_the_time_to_later_today_runs_again() {
  DailyCalibration calibration;
  calibration.check(1, AT_4 - 1, AT_4, IDLE, 0, QUIET);
  TEST_ASSERT_TRUE(calibration.check(1, AT_4, AT_4, IDLE, 0, QUIET));
  calibration.reset();
  TEST_ASSERT_FALSE(calibration.check(1, AT_4 + 1, 5 * 60, IDLE, 0, QUIET));
  TEST_ASSERT_TRUE(calibration.check(1, 5 * 60, 5 * 60, IDLE, 0, QUIET));
}

void test_millis_wrapping_around() {
  DailyCalibration calibration;
  calibration.check(1, AT_4 - 1, AT_4, 0, 0, QUIET);
  const uint32_t used_at = 0xFFFFFFFFUL - 1000;
  TEST_ASSERT_FALSE(calibration.check(1, AT_4, AT_4, used_at + 2000, used_at, QUIET));
  TEST_ASSERT_TRUE(calibration.check(1, AT_4 + 60, AT_4, used_at + QUIET, used_at, QUIET));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_runs_once_at_the_given_time);
  RUN_TEST(test_runs_again_the_next_day);
  RUN_TEST(test_booting_after_the_time_waits_for_tomorrow);
  RUN_TEST(test_postponed_while_the_bar_is_used);
  RUN_TEST(test_postponed_past_midnight_waits_for_the_next_time);
  RUN_TEST(test_no_quiet_time_runs_right_away);
  RUN_TEST(test_changing_the_time_to_the_past_waits_for_tomorrow);
  RUN_TEST(test_changing_the_time_to_later_today_runs_again);
  RUN_TEST(test_millis_wrapping_around);
  return UNITY_END();
}
