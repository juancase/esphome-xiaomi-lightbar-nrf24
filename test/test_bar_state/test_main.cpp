#include <unity.h>

#include <cmath>
#include <vector>

#include "bar_state.h"

using namespace esphome::xiaomi_lightbar;
using Out = BarState::OutCommand;

static void assert_commands(const std::vector<Out> &expected, const std::vector<Out> &actual, int line) {
  UNITY_TEST_ASSERT_EQUAL_UINT(expected.size(), actual.size(), line, "number of commands");
  for (size_t i = 0; i < expected.size(); i++) {
    UNITY_TEST_ASSERT_EQUAL_HEX8((uint8_t) expected[i].command, (uint8_t) actual[i].command, line, "command");
    UNITY_TEST_ASSERT_EQUAL_HEX8(expected[i].options, actual[i].options, line, "options");
  }
}
#define ASSERT_COMMANDS(state, ...) assert_commands(std::vector<Out>{__VA_ARGS__}, (state).take_commands(), __LINE__)
#define ASSERT_NO_COMMANDS(state) assert_commands({}, (state).take_commands(), __LINE__)

// A bar that is on with known brightness and temperature level.
static BarState known_bar(uint8_t brightness, uint32_t kelvin) {
  BarState state;
  state.sync_on_off(true, 0);
  state.sync_brightness(brightness, 0);
  state.sync_kelvin(kelvin, 0);
  return state;
}

void setUp() {}
void tearDown() {}

/* -- Coalescing ---------------------------------------------------------------------------------------------- */

void test_nothing_is_sent_before_the_window_ends() {
  BarState state = known_bar(5, 2700);
  state.request_brightness(8, 1000);
  state.loop(1000);
  state.loop(1149);
  ASSERT_NO_COMMANDS(state);
  state.loop(1150);
  ASSERT_COMMANDS(state, {Command::BRIGHTER, 3});
}

void test_quick_requests_are_merged_into_one_step() {
  BarState state = known_bar(5, 2700);
  state.request_brightness(6, 0);
  state.loop(100);
  state.request_brightness(7, 100);
  state.loop(200);
  state.request_brightness(10, 200);
  state.loop(349);
  ASSERT_NO_COMMANDS(state);
  state.loop(350);
  ASSERT_COMMANDS(state, {Command::BRIGHTER, 5});
  state.loop(1000);
  ASSERT_NO_COMMANDS(state);
}

void test_millis_wrapping_around() {
  BarState state = known_bar(5, 2700);
  state.request_brightness(6, 0xFFFFFFC0);
  state.loop(0x10);
  ASSERT_NO_COMMANDS(state);
  state.loop(86);  // 0xFFFFFFC0 + 150
  ASSERT_COMMANDS(state, {Command::BRIGHTER, 1});
}

/* -- Brightness ---------------------------------------------------------------------------------------------- */

void test_first_brightness_is_set_absolutely_then_relative() {
  BarState state;
  state.request_on_off(true, 0);
  state.request_brightness(8, 0);
  state.loop(150);
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0}, {Command::DIMMER, 0xF0}, {Command::BRIGHTER, 8});

  state.request_brightness(11, 1000);
  state.loop(1150);
  ASSERT_COMMANDS(state, {Command::BRIGHTER, 3});

  state.request_brightness(9, 2000);
  state.loop(2150);
  ASSERT_COMMANDS(state, {Command::DIMMER, 0xFE});
  TEST_ASSERT_EQUAL_UINT8(9, state.brightness());
}

void test_same_brightness_sends_nothing() {
  BarState state = known_bar(5, 2700);
  state.request_brightness(5, 0);
  state.loop(150);
  ASSERT_NO_COMMANDS(state);
}

void test_brightness_is_clamped() {
  BarState state = known_bar(5, 2700);
  state.request_brightness(200, 0);
  state.loop(150);
  ASSERT_COMMANDS(state, {Command::BRIGHTER, 10});
  TEST_ASSERT_EQUAL_UINT8(15, state.brightness());
}

void test_unknown_value_not_requested_is_left_alone() {
  // Only brightness is asked for. The unknown temperature must not be set (it would dip the bar).
  BarState state;
  state.request_on_off(true, 0);
  state.request_brightness(4, 0);
  state.loop(150);
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0}, {Command::DIMMER, 0xF0}, {Command::BRIGHTER, 4});
  TEST_ASSERT_FALSE(state.has_kelvin());
}

/* -- Temperature --------------------------------------------------------------------------------------------- */

void test_first_temperature_is_set_absolutely_then_relative() {
  BarState state;
  state.sync_on_off(true, 0);
  state.request_kelvin(6500, 0);
  state.loop(150);
  ASSERT_COMMANDS(state, {Command::COOLER, 0xF0}, {Command::WARMER, 15});

  state.request_kelvin(2700, 1000);
  state.loop(1150);
  ASSERT_COMMANDS(state, {Command::WARMER, 0xF1});

  state.request_kelvin(BarState::level_to_kelvin(4), 2000);
  state.loop(2150);
  ASSERT_COMMANDS(state, {Command::COOLER, 4});
}

void test_kelvin_levels_round_trip() {
  TEST_ASSERT_EQUAL_UINT8(0, BarState::kelvin_to_level(2700));
  TEST_ASSERT_EQUAL_UINT8(15, BarState::kelvin_to_level(6500));
  TEST_ASSERT_EQUAL_UINT32(2700, BarState::level_to_kelvin(0));
  TEST_ASSERT_EQUAL_UINT32(6500, BarState::level_to_kelvin(15));
  for (uint8_t level = 0; level <= BarState::MAX_LEVEL; level++)
    TEST_ASSERT_EQUAL_UINT8(level, BarState::kelvin_to_level(BarState::level_to_kelvin(level)));
}

void test_to_kelvin_accepts_mireds_and_clamps() {
  TEST_ASSERT_EQUAL_UINT32(4000, BarState::to_kelvin(250));   // mireds
  TEST_ASSERT_EQUAL_UINT32(6500, BarState::to_kelvin(153));   // 6536 K
  TEST_ASSERT_EQUAL_UINT32(2700, BarState::to_kelvin(1000));  // Kelvin, below the range
  TEST_ASSERT_EQUAL_UINT32(6500, BarState::to_kelvin(10000));
  TEST_ASSERT_EQUAL_UINT32(2700, BarState::to_kelvin(0));
}

/* -- On / off ------------------------------------------------------------------------------------------------ */

void test_values_wait_while_off() {
  BarState state;
  state.request_brightness(8, 0);
  state.loop(150);
  ASSERT_NO_COMMANDS(state);
  TEST_ASSERT_EQUAL_UINT8(8, state.brightness());

  state.request_on_off(true, 1000);
  state.loop(1150);
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0}, {Command::DIMMER, 0xF0}, {Command::BRIGHTER, 8});
}

void test_turning_off_with_a_change_only_sends_off() {
  BarState state = known_bar(5, 2700);
  state.request_on_off(false, 0);
  state.request_brightness(9, 10);
  state.loop(160);
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0});

  // The change is kept and sent relatively when the bar is turned on again.
  state.request_on_off(true, 1000);
  state.loop(1150);
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0}, {Command::BRIGHTER, 4});
}

void test_on_twice_sends_one_toggle() {
  BarState state;
  state.request_on_off(true, 0);
  state.loop(150);
  state.request_on_off(true, 1000);
  state.loop(1150);
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0});
}

/* -- Original remote ----------------------------------------------------------------------------------------- */

void test_remote_toggle_never_echoes() {
  BarState state;
  state.track_remote_command(0x01, 0x00, 0);
  TEST_ASSERT_TRUE(state.is_on());
  state.track_remote_command(0x01, 0x00, 10);
  TEST_ASSERT_FALSE(state.is_on());
  state.loop(1000);
  ASSERT_NO_COMMANDS(state);
}

void test_remote_wheel_updates_known_values() {
  BarState state = known_bar(5, 2700);
  state.track_remote_command(0x04, 0x03, 0);  // Brighter +3
  TEST_ASSERT_EQUAL_UINT8(8, state.brightness());
  state.track_remote_command(0x05, 0xF0, 0);  // Dimmer -16
  TEST_ASSERT_EQUAL_UINT8(0, state.brightness());
  state.track_remote_command(0x04, 0x7F, 0);
  TEST_ASSERT_EQUAL_UINT8(15, state.brightness());
  state.track_remote_command(0x02, 0x04, 0);  // Cooler +4
  TEST_ASSERT_EQUAL_UINT32(BarState::level_to_kelvin(4), state.kelvin());
  state.loop(1000);
  ASSERT_NO_COMMANDS(state);

  // Tracked values follow the remote: the next request is relative to them.
  state.request_brightness(13, 2000);
  state.loop(2150);
  ASSERT_COMMANDS(state, {Command::DIMMER, 0xFE});
}

void test_remote_wheel_while_off_forgets_the_value() {
  BarState state = known_bar(5, 2700);
  state.track_remote_command(0x01, 0x00, 0);  // Off
  state.track_remote_command(0x04, 0x02, 0);  // Wheel while off
  state.request_on_off(true, 1000);
  state.loop(1150);
  // Brightness is unknown now but nobody asked for it, so it is not set absolutely.
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0});

  state.request_brightness(4, 2000);
  state.loop(2150);
  ASSERT_COMMANDS(state, {Command::DIMMER, 0xF0}, {Command::BRIGHTER, 4});
}

void test_remote_reset_saturates_once_the_remote_is_quiet() {
  BarState state = known_bar(5, 6500);
  state.track_remote_command(0x06, 0x00, 0);
  TEST_ASSERT_EQUAL_UINT8(BarState::MAX_LEVEL, state.brightness());
  TEST_ASSERT_EQUAL_UINT32(BarState::MIN_KELVIN, state.kelvin());
  // Holding the button sends the reset again: nothing goes out while the remote keeps sending.
  state.loop(100);
  state.track_remote_command(0x06, 0x01, 100);
  state.loop(200);
  ASSERT_NO_COMMANDS(state);
  state.loop(250);
  ASSERT_COMMANDS(state, {Command::BRIGHTER, 0x10}, {Command::WARMER, 0xF0});

  // Both values are known now: the remote is followed and later requests are relative.
  state.track_remote_command(0x05, 0xFE, 1000);
  TEST_ASSERT_EQUAL_UINT8(13, state.brightness());
  state.request_brightness(6, 2000);
  state.request_kelvin(BarState::level_to_kelvin(3), 2000);
  state.loop(2150);
  ASSERT_COMMANDS(state, {Command::DIMMER, 0xF9}, {Command::COOLER, 0x03});
}

void test_reset_while_off_saturates_when_turned_on() {
  BarState state = known_bar(5, 6500);
  state.sync_on_off(false, 0);
  state.track_remote_command(0x06, 0x00, 0);
  state.loop(1000);
  ASSERT_NO_COMMANDS(state);
  state.request_on_off(true, 2000);
  state.loop(2150);
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0}, {Command::BRIGHTER, 0x10}, {Command::WARMER, 0xF0});
}

void test_remote_updates_last_activity() {
  BarState state;
  state.track_remote_command(0x01, 0x00, 1234);
  TEST_ASSERT_EQUAL_UINT32(1234, state.last_activity());
}

// What the light does after a remote command: publish the state, then get it back in write_state()
// as brightness (0.0 – 1.0) and mireds. Requesting those values again must not send anything.
static void request_published_state(BarState &state, uint32_t now_ms) {
  float brightness = BarState::level_to_brightness(state.brightness());
  float mireds = 1000000.0f / state.kelvin();
  state.request_brightness(BarState::brightness_to_level(brightness), now_ms);
  state.request_kelvin((uint32_t) lroundf(1000000.0f / mireds), now_ms);
  state.request_on_off(state.is_on(), now_ms);
}

void test_published_remote_state_never_echoes() {
  BarState state = known_bar(5, 2700);
  const uint8_t commands[][2] = {
      {0x04, 0x03}, {0x05, 0xF0}, {0x04, 0x01}, {0x02, 0x04}, {0x03, 0xFF}, {0x02, 0x7F}, {0x01, 0x00}, {0x01, 0x00},
  };
  uint32_t now = 0;
  for (const auto &command : commands) {
    state.track_remote_command(command[0], command[1], now);
    request_published_state(state, now);
    state.loop(now + 1000);
    ASSERT_NO_COMMANDS(state);
    now += 2000;
  }
}

void test_relay_passes_the_command_on_and_tracks_it() {
  BarState state = known_bar(5, 2700);
  state.relay_remote_command(0x04, 0x03, 0);
  ASSERT_COMMANDS(state, {Command::BRIGHTER, 0x03});
  TEST_ASSERT_EQUAL_UINT8(8, state.brightness());
  state.relay_remote_command(0x01, 0x00, 0);
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0});
  TEST_ASSERT_FALSE(state.is_on());

  request_published_state(state, 1000);
  state.loop(2000);
  ASSERT_NO_COMMANDS(state);
}

/* -- Brightness scale ---------------------------------------------------------------------------------------- */

void test_brightness_levels_round_trip() {
  for (uint8_t level = 0; level <= BarState::MAX_LEVEL; level++)
    TEST_ASSERT_EQUAL_UINT8(level, BarState::brightness_to_level(BarState::level_to_brightness(level)));
}

void test_lowest_level_is_not_off() {
  // ESPHome takes brightness 0.0 as "off".
  TEST_ASSERT_TRUE(BarState::level_to_brightness(0) > 0.0f);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, BarState::level_to_brightness(15));
}

void test_brightness_to_level_limits() {
  TEST_ASSERT_EQUAL_UINT8(0, BarState::brightness_to_level(0.0f));
  TEST_ASSERT_EQUAL_UINT8(0, BarState::brightness_to_level(1.0f / 255));  // Lowest value in Home Assistant
  TEST_ASSERT_EQUAL_UINT8(7, BarState::brightness_to_level(0.5f));
  TEST_ASSERT_EQUAL_UINT8(15, BarState::brightness_to_level(1.0f));
  TEST_ASSERT_EQUAL_UINT8(15, BarState::brightness_to_level(1.5f));
}
/* -- Calibration / restore / pair ---------------------------------------------------------------------------- */

void test_sync_sends_nothing() {
  BarState state = known_bar(5, 4000);
  TEST_ASSERT_TRUE(state.is_on());
  TEST_ASSERT_EQUAL_UINT8(5, state.brightness());
  TEST_ASSERT_EQUAL_UINT32(4000, state.kelvin());
  state.loop(1000);
  ASSERT_NO_COMMANDS(state);
}

void test_reanchor_sets_both_values_absolutely() {
  BarState state = known_bar(5, 2700);
  state.reanchor(0);
  state.loop(150);
  ASSERT_COMMANDS(state, {Command::DIMMER, 0xF0}, {Command::BRIGHTER, 5}, {Command::COOLER, 0xF0}, {Command::WARMER, 0});
}

void test_reanchor_while_off_waits_until_on() {
  BarState state = known_bar(5, 2700);
  state.sync_on_off(false, 0);
  state.reanchor(0);
  state.loop(150);
  ASSERT_NO_COMMANDS(state);
  state.request_on_off(true, 1000);
  state.loop(1150);
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0}, {Command::DIMMER, 0xF0}, {Command::BRIGHTER, 5}, {Command::COOLER, 0xF0},
                  {Command::WARMER, 0});
}

void test_sync_drops_a_pending_toggle() {
  BarState state = known_bar(5, 2700);
  state.sync_on_off(false, 0);
  state.request_on_off(true, 0);
  // The bar turned out to be on already (e.g. the "bar is on" button was pressed before the window ended).
  state.sync_on_off(true, 100);
  state.loop(150);
  ASSERT_NO_COMMANDS(state);
  TEST_ASSERT_TRUE(state.is_on());
}

void test_restore_state_keeps_values_untracked() {
  BarState state;
  state.restore_state(true, 7, 4000);
  TEST_ASSERT_TRUE(state.is_on());
  TEST_ASSERT_EQUAL_UINT8(7, state.brightness());
  TEST_ASSERT_EQUAL_UINT32(4000, state.kelvin());
  state.loop(1000);
  ASSERT_NO_COMMANDS(state);

  state.request_brightness(9, 2000);
  state.loop(2150);
  ASSERT_COMMANDS(state, {Command::DIMMER, 0xF0}, {Command::BRIGHTER, 9});
}

void test_restore_state_with_unknown_values() {
  BarState state;
  state.restore_state(false, -1, 0);
  TEST_ASSERT_FALSE(state.is_on());
  TEST_ASSERT_FALSE(state.has_brightness());
  TEST_ASSERT_FALSE(state.has_kelvin());
}

void test_pair_sends_reset_then_saturates() {
  BarState state = known_bar(5, 6500);
  state.pair(0);
  ASSERT_COMMANDS(state, {Command::RESET, 0});
  TEST_ASSERT_TRUE(state.is_on());
  TEST_ASSERT_EQUAL_UINT8(BarState::MAX_LEVEL, state.brightness());
  TEST_ASSERT_EQUAL_UINT32(BarState::MIN_KELVIN, state.kelvin());
  state.loop(150);
  ASSERT_COMMANDS(state, {Command::BRIGHTER, 0x10}, {Command::WARMER, 0xF0});
  state.request_brightness(14, 1000);
  state.loop(1150);
  ASSERT_COMMANDS(state, {Command::DIMMER, 0xFF});
}

void test_pair_marks_the_bar_on() {
  // The bar was off before being power-cycled for pairing; it is lit afterwards.
  BarState state;
  state.sync_on_off(false, 0);
  state.pair(0);
  ASSERT_COMMANDS(state, {Command::RESET, 0});
  TEST_ASSERT_TRUE(state.is_on());
  state.request_on_off(true, 1000);
  state.loop(1150);
  // No toggle, only the saturation after the reset.
  ASSERT_COMMANDS(state, {Command::BRIGHTER, 0x10}, {Command::WARMER, 0xF0});
  state.request_on_off(false, 2000);
  state.loop(2150);
  ASSERT_COMMANDS(state, {Command::ON_OFF, 0});
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_is_sent_before_the_window_ends);
  RUN_TEST(test_quick_requests_are_merged_into_one_step);
  RUN_TEST(test_millis_wrapping_around);
  RUN_TEST(test_first_brightness_is_set_absolutely_then_relative);
  RUN_TEST(test_same_brightness_sends_nothing);
  RUN_TEST(test_brightness_is_clamped);
  RUN_TEST(test_unknown_value_not_requested_is_left_alone);
  RUN_TEST(test_first_temperature_is_set_absolutely_then_relative);
  RUN_TEST(test_kelvin_levels_round_trip);
  RUN_TEST(test_to_kelvin_accepts_mireds_and_clamps);
  RUN_TEST(test_values_wait_while_off);
  RUN_TEST(test_turning_off_with_a_change_only_sends_off);
  RUN_TEST(test_on_twice_sends_one_toggle);
  RUN_TEST(test_remote_toggle_never_echoes);
  RUN_TEST(test_remote_wheel_updates_known_values);
  RUN_TEST(test_remote_wheel_while_off_forgets_the_value);
  RUN_TEST(test_remote_reset_saturates_once_the_remote_is_quiet);
  RUN_TEST(test_reset_while_off_saturates_when_turned_on);
  RUN_TEST(test_remote_updates_last_activity);
  RUN_TEST(test_published_remote_state_never_echoes);
  RUN_TEST(test_relay_passes_the_command_on_and_tracks_it);
  RUN_TEST(test_brightness_levels_round_trip);
  RUN_TEST(test_lowest_level_is_not_off);
  RUN_TEST(test_brightness_to_level_limits);
  RUN_TEST(test_sync_sends_nothing);
  RUN_TEST(test_reanchor_sets_both_values_absolutely);
  RUN_TEST(test_reanchor_while_off_waits_until_on);
  RUN_TEST(test_sync_drops_a_pending_toggle);
  RUN_TEST(test_restore_state_keeps_values_untracked);
  RUN_TEST(test_restore_state_with_unknown_values);
  RUN_TEST(test_pair_sends_reset_then_saturates);
  RUN_TEST(test_pair_marks_the_bar_on);
  return UNITY_END();
}
