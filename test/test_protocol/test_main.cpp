#include <unity.h>

#include <cstring>

#include "protocol.h"

using namespace esphome::xiaomi_lightbar;

// Packets captured from a real remote (serial 0x01B960, ON_OFF), published in
// https://github.com/lamperez/xiaomi-lightbar-nrf24#crc-checksum
static const uint8_t CAPTURED[][PACKET_SIZE] = {
    {0x53, 0x39, 0x14, 0xDD, 0x1C, 0x49, 0x34, 0x12, 0x01, 0xB9, 0x60, 0xFF, 0x79, 0x01, 0x00, 0x38, 0x70},
    {0x53, 0x39, 0x14, 0xDD, 0x1C, 0x49, 0x34, 0x12, 0x01, 0xB9, 0x60, 0xFF, 0x16, 0x01, 0x00, 0x8F, 0x2A},
    {0x53, 0x39, 0x14, 0xDD, 0x1C, 0x49, 0x34, 0x12, 0x01, 0xB9, 0x60, 0xFF, 0x1A, 0x01, 0x00, 0xFA, 0x4B},
    {0x53, 0x39, 0x14, 0xDD, 0x1C, 0x49, 0x34, 0x12, 0x01, 0xB9, 0x60, 0xFF, 0x20, 0x01, 0x00, 0xF8, 0x2F},
};

// Inverse of unshift_raw(): what the radio delivers for a packet (the first 5 bits are missing).
static void shift_to_raw(const uint8_t packet[PACKET_SIZE], uint8_t raw[RAW_SIZE]) {
  for (size_t i = 0; i < PACKET_SIZE; i++) {
    uint8_t next = i + 1 < PACKET_SIZE ? packet[i + 1] : 0;
    raw[i] = (uint8_t) (packet[i] << 5 | next >> 3);
  }
  raw[RAW_SIZE - 1] = 0;
}

void setUp() {}
void tearDown() {}

/* -- CRC ----------------------------------------------------------------------------------------------------- */

void test_crc16_check_value() {
  // Standard CRC catalogue check: CRC of "123456789". Value given by lamperez (reveng).
  const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x6e62, crc16(data, sizeof(data)));
}

void test_crc16_captured_packets() {
  for (const auto &packet : CAPTURED) {
    uint16_t expected = (uint16_t) (packet[15] << 8 | packet[16]);
    TEST_ASSERT_EQUAL_HEX16(expected, crc16(packet, PACKET_SIZE - 2));
  }
}

/* -- Build / parse ------------------------------------------------------------------------------------------- */

void test_build_packet_matches_captures() {
  for (const auto &packet : CAPTURED) {
    uint8_t built[PACKET_SIZE];
    build_packet(0x01B960, packet[12], packet[13], packet[14], built);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(packet, built, PACKET_SIZE);
  }
}

void test_build_packet_layout() {
  uint8_t built[PACKET_SIZE];
  build_packet(0xABCDEF, 0x42, (uint8_t) Command::DIMMER, 0xFE, built);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(PREAMBLE, built, sizeof(PREAMBLE));
  TEST_ASSERT_EQUAL_HEX8(0xAB, built[8]);
  TEST_ASSERT_EQUAL_HEX8(0xCD, built[9]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, built[10]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, built[11]);
  TEST_ASSERT_EQUAL_HEX8(0x42, built[12]);
  TEST_ASSERT_EQUAL_HEX8(0x05, built[13]);
  TEST_ASSERT_EQUAL_HEX8(0xFE, built[14]);
  TEST_ASSERT_EQUAL_HEX16(crc16(built, 15), built[15] << 8 | built[16]);
}

void test_parse_packet_accepts_capture() {
  Packet packet{};
  TEST_ASSERT_TRUE(parse_packet(CAPTURED[0], packet));
  TEST_ASSERT_EQUAL_HEX32(0x01B960, packet.serial);
  TEST_ASSERT_EQUAL_HEX8(0x79, packet.seq);
  TEST_ASSERT_EQUAL_HEX8(0x01, packet.command);
  TEST_ASSERT_EQUAL_HEX8(0x00, packet.options);
}

void test_parse_packet_rejects_corruption() {
  // Flipping any single bit (preamble, payload or checksum) must make the packet invalid.
  for (size_t byte = 0; byte < PACKET_SIZE; byte++) {
    for (int bit = 0; bit < 8; bit++) {
      uint8_t data[PACKET_SIZE];
      memcpy(data, CAPTURED[0], PACKET_SIZE);
      data[byte] ^= 1 << bit;
      Packet packet{};
      TEST_ASSERT_FALSE(parse_packet(data, packet));
    }
  }
}

/* -- Receive shift ------------------------------------------------------------------------------------------- */

void test_unshift_raw_round_trip() {
  for (const auto &packet : CAPTURED) {
    uint8_t raw[RAW_SIZE];
    uint8_t out[PACKET_SIZE];
    shift_to_raw(packet, raw);
    unshift_raw(raw, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(packet, out, PACKET_SIZE);
  }
}

void test_unshift_raw_is_a_5_bit_shift() {
  // The first raw byte starts at bit 5 of the preamble: 0x53 0x39 = 0b01010 011 00111001 ...
  uint8_t raw[RAW_SIZE];
  shift_to_raw(CAPTURED[0], raw);
  TEST_ASSERT_EQUAL_HEX8(0x67, raw[0]);  // 011 00111
  uint8_t out[PACKET_SIZE];
  unshift_raw(raw, out);
  TEST_ASSERT_EQUAL_HEX8(0x53, out[0]);
}

// Generate a radio payload by removing known leading preamble bits. The final
// `shift` bits are outside the packet; deliberately fill them with ones.
static void radio_payload(const uint8_t packet[PACKET_SIZE], uint8_t shift, uint8_t raw[PACKET_SIZE]) {
  for (size_t i = 0; i < PACKET_SIZE; i++) {
    uint8_t next = i + 1 < PACKET_SIZE ? packet[i + 1] : 0xFF;
    raw[i] = shift == 0 ? packet[i] : (uint8_t) (packet[i] << shift | next >> (8 - shift));
  }
}

void test_parse_raw_all_bit_alignments() {
  // Synthetic shifts of the published captures, not additional over-the-air captures.
  for (const auto &captured : CAPTURED) {
    for (uint8_t shift = 0; shift < 8; shift++) {
      uint8_t raw[PACKET_SIZE];  // No 18th byte available, as in the actual SPI read.
      radio_payload(captured, shift, raw);
      Packet packet{};
      TEST_ASSERT_TRUE(parse_raw(raw, packet));
      TEST_ASSERT_EQUAL_HEX32(0x01B960, packet.serial);
      TEST_ASSERT_EQUAL_HEX8(captured[12], packet.seq);
      TEST_ASSERT_EQUAL_HEX8(captured[13], packet.command);
      TEST_ASSERT_EQUAL_HEX8(captured[14], packet.options);
      // Unused trailing bits have no effect on decoding.
      raw[PACKET_SIZE - 1] ^= (1u << shift) - 1;
      TEST_ASSERT_TRUE(parse_raw(raw, packet));
    }
  }
}

void test_parse_raw_capture_derived_vectors() {
  // Derived from ESP32-C3 + nRF24 captures at offsets 1, 3 and 5.
  // Remote IDs were replaced with 0x112233 and CRCs recomputed before shifting
  // back to the captured alignment. Other bytes and unused tail bits are retained.
  static const struct {
    uint8_t raw[PACKET_SIZE];
    uint8_t seq, command, options, shift;
  } vectors[] = {
    {{0xA6, 0x72, 0x29, 0xBA, 0x38, 0x92, 0x68, 0x24, 0x22, 0x44, 0x67, 0xFF, 0x6C, 0x0B, 0xFE, 0x42, 0x3E}, 182, 5, 255, 1},
    {{0x99, 0xC8, 0xA6, 0xE8, 0xE2, 0x49, 0xA0, 0x90, 0x89, 0x11, 0x9F, 0xFD, 0xC8, 0x0F, 0xFE, 0x0F, 0x55}, 185, 1, 255, 3},
    {{0x67, 0x22, 0x9B, 0xA3, 0x89, 0x26, 0x82, 0x42, 0x24, 0x46, 0x7F, 0xF6, 0xC0, 0xBF, 0xE4, 0x23, 0xE7}, 182, 5, 255, 5},
  };
  for (const auto &vector : vectors) {
    Packet packet{};
    TEST_ASSERT_TRUE(parse_raw(vector.raw, packet));
    TEST_ASSERT_EQUAL_HEX32(0x112233, packet.serial);
    TEST_ASSERT_EQUAL_UINT8(vector.seq, packet.seq);
    TEST_ASSERT_EQUAL_UINT8(vector.command, packet.command);
    TEST_ASSERT_EQUAL_UINT8(vector.options, packet.options);
    uint8_t legacy[PACKET_SIZE];
    uint8_t legacy_raw[RAW_SIZE] = {};
    memcpy(legacy_raw, vector.raw, PACKET_SIZE);
    unshift_raw(legacy_raw, legacy);
    TEST_ASSERT_EQUAL(vector.shift == 5, parse_packet(legacy, packet));
  }
}

void test_parse_raw_rejects_corrupted_received_bits() {
  for (uint8_t shift = 0; shift < 8; shift++) {
    uint8_t original[PACKET_SIZE];
    radio_payload(CAPTURED[0], shift, original);
    // Includes every received preamble, payload and CRC bit, excluding only
    // the trailing bits outside the packet and the absent known prefix bits.
    for (size_t bit = 0; bit < PACKET_SIZE * 8 - shift; bit++) {
      uint8_t raw[PACKET_SIZE];
      memcpy(raw, original, sizeof(raw));
      raw[bit / 8] ^= 0x80 >> (bit % 8);
      Packet packet{0xABCDEF, 0x42, 0x04, 0x03};
      TEST_ASSERT_FALSE(parse_raw(raw, packet));
      TEST_ASSERT_EQUAL_HEX32(0xABCDEF, packet.serial);
      TEST_ASSERT_EQUAL_HEX8(0x42, packet.seq);
      TEST_ASSERT_EQUAL_HEX8(0x04, packet.command);
      TEST_ASSERT_EQUAL_HEX8(0x03, packet.options);
    }
  }
}

void test_parse_raw_rejects_bad_separator_with_valid_crc() {
  uint8_t data[PACKET_SIZE];
  memcpy(data, CAPTURED[0], sizeof(data));
  data[11] = 0;
  uint16_t checksum = crc16(data, PACKET_SIZE - 2);
  data[15] = checksum >> 8;
  data[16] = checksum;
  for (uint8_t shift = 0; shift < 8; shift++) {
    uint8_t raw[PACKET_SIZE];
    radio_payload(data, shift, raw);
    Packet packet{};
    TEST_ASSERT_FALSE(parse_raw(raw, packet));
  }
}

void test_parse_raw_rejects_noise() {
  uint8_t raw[PACKET_SIZE] = {};
  Packet packet{};
  TEST_ASSERT_FALSE(parse_raw(raw, packet));
  memset(raw, 0xFF, sizeof(raw));
  TEST_ASSERT_FALSE(parse_raw(raw, packet));
  uint32_t random = 0x12345678;
  for (int sample = 0; sample < 4096; sample++) {
    for (auto &byte : raw) {
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      byte = random;
    }
    TEST_ASSERT_FALSE(parse_raw(raw, packet));
  }
}

/* -- Sequence counters --------------------------------------------------------------------------------------- */

void test_sequence_drops_repeated_copies() {
  SequenceTable table;
  TEST_ASSERT_TRUE(table.accept_rx(1, 10));
  TEST_ASSERT_FALSE(table.accept_rx(1, 10));
  TEST_ASSERT_FALSE(table.accept_rx(1, 10));
  TEST_ASSERT_TRUE(table.accept_rx(1, 11));
  TEST_ASSERT_FALSE(table.accept_rx(1, 9));  // Old packet
}

void test_sequence_counts_missed_packets() {
  SequenceTable table;
  uint8_t missed = 99;
  TEST_ASSERT_TRUE(table.accept_rx(1, 10, &missed));
  TEST_ASSERT_EQUAL_UINT8(0, missed);  // First packet: nothing to compare with
  TEST_ASSERT_TRUE(table.accept_rx(1, 11, &missed));
  TEST_ASSERT_EQUAL_UINT8(0, missed);
  TEST_ASSERT_TRUE(table.accept_rx(1, 14, &missed));
  TEST_ASSERT_EQUAL_UINT8(2, missed);
  TEST_ASSERT_TRUE(table.accept_rx(1, 14 - 100, &missed));  // Big jump backwards: not counted
  TEST_ASSERT_EQUAL_UINT8(0, missed);

  TEST_ASSERT_TRUE(table.accept_rx(2, 254, &missed));
  TEST_ASSERT_TRUE(table.accept_rx(2, 1, &missed));  // 255 and 0 were skipped
  TEST_ASSERT_EQUAL_UINT8(2, missed);
}

void test_sequence_first_packet_is_always_accepted() {
  SequenceTable table;
  TEST_ASSERT_TRUE(table.accept_rx(1, 0));
}

void test_sequence_wraps_around() {
  SequenceTable table;
  TEST_ASSERT_TRUE(table.accept_rx(1, 255));
  TEST_ASSERT_TRUE(table.accept_rx(1, 0));
  TEST_ASSERT_FALSE(table.accept_rx(1, 255));
}

void test_sequence_accepts_big_jump_backwards() {
  // E.g. the remote lost power and started counting again.
  SequenceTable table;
  TEST_ASSERT_TRUE(table.accept_rx(1, 100));
  TEST_ASSERT_FALSE(table.accept_rx(1, 100 - 63));
  TEST_ASSERT_TRUE(table.accept_rx(1, 100 - 64));
}

void test_sequence_serials_are_independent() {
  SequenceTable table;
  TEST_ASSERT_TRUE(table.accept_rx(1, 10));
  TEST_ASSERT_TRUE(table.accept_rx(2, 10));
}

void test_sequence_tx_increments() {
  SequenceTable table;
  uint8_t seq = 0;
  TEST_ASSERT_TRUE(table.next_tx(1, seq));
  TEST_ASSERT_EQUAL_UINT8(1, seq);
  TEST_ASSERT_TRUE(table.next_tx(1, seq));
  TEST_ASSERT_EQUAL_UINT8(2, seq);
}

void test_sequence_sending_does_not_ignore_remote() {
  // Our own transmissions share the serial with the remote. They must not move the receive window.
  SequenceTable table;
  TEST_ASSERT_TRUE(table.accept_rx(1, 10));
  uint8_t seq = 0;
  for (int i = 0; i < 5; i++)
    table.next_tx(1, seq);
  TEST_ASSERT_EQUAL_UINT8(15, seq);
  TEST_ASSERT_TRUE(table.accept_rx(1, 11));
}

void test_sequence_tx_follows_remote() {
  SequenceTable table;
  TEST_ASSERT_TRUE(table.accept_rx(1, 200));
  uint8_t seq = 0;
  table.next_tx(1, seq);
  TEST_ASSERT_EQUAL_UINT8(201, seq);
}

void test_sequence_table_full() {
  SequenceTable table;
  uint8_t seq = 0;
  for (uint32_t serial = 0; serial < SequenceTable::MAX_SERIALS; serial++)
    TEST_ASSERT_TRUE(table.next_tx(serial, seq));
  TEST_ASSERT_FALSE(table.next_tx(1000, seq));
  TEST_ASSERT_FALSE(table.accept_rx(1000, 1));
  TEST_ASSERT_TRUE(table.next_tx(0, seq));  // Known serials keep working
}

void test_merge_steps_same_direction() {
  uint8_t merged = 0;
  TEST_ASSERT_TRUE(merge_steps((uint8_t) Command::DIMMER, 0xFF, 0xFE, merged));
  TEST_ASSERT_EQUAL_UINT8(0xFD, merged);  // -1 + -2 = -3
  TEST_ASSERT_TRUE(merge_steps((uint8_t) Command::BRIGHTER, 0x01, 0x01, merged));
  TEST_ASSERT_EQUAL_UINT8(0x02, merged);
  TEST_ASSERT_TRUE(merge_steps((uint8_t) Command::COOLER, 0x01, 0x03, merged));
  TEST_ASSERT_EQUAL_UINT8(0x04, merged);
  TEST_ASSERT_TRUE(merge_steps((uint8_t) Command::WARMER, 0xF0, 0xFF, merged));
  TEST_ASSERT_EQUAL_UINT8(0xEF, merged);  // -16 + -1 = -17
}

void test_merge_steps_rejects() {
  uint8_t merged = 0x42;
  TEST_ASSERT_FALSE(merge_steps((uint8_t) Command::ON_OFF, 0x00, 0x00, merged));
  TEST_ASSERT_FALSE(merge_steps((uint8_t) Command::RESET, 0x01, 0x01, merged));
  TEST_ASSERT_FALSE(merge_steps((uint8_t) Command::DIMMER, 0xFF, 0x01, merged));  // Opposite directions
  TEST_ASSERT_FALSE(merge_steps((uint8_t) Command::DIMMER, 0x00, 0xFF, merged));
  TEST_ASSERT_FALSE(merge_steps((uint8_t) Command::BRIGHTER, 0x7F, 0x01, merged));  // Does not fit
  TEST_ASSERT_FALSE(merge_steps((uint8_t) Command::DIMMER, 0x80, 0xFF, merged));
  TEST_ASSERT_EQUAL_UINT8(0x42, merged);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_check_value);
  RUN_TEST(test_crc16_captured_packets);
  RUN_TEST(test_build_packet_matches_captures);
  RUN_TEST(test_build_packet_layout);
  RUN_TEST(test_parse_packet_accepts_capture);
  RUN_TEST(test_parse_packet_rejects_corruption);
  RUN_TEST(test_unshift_raw_round_trip);
  RUN_TEST(test_unshift_raw_is_a_5_bit_shift);
  RUN_TEST(test_parse_raw_all_bit_alignments);
  RUN_TEST(test_parse_raw_capture_derived_vectors);
  RUN_TEST(test_parse_raw_rejects_corrupted_received_bits);
  RUN_TEST(test_parse_raw_rejects_bad_separator_with_valid_crc);
  RUN_TEST(test_parse_raw_rejects_noise);
  RUN_TEST(test_sequence_drops_repeated_copies);
  RUN_TEST(test_sequence_counts_missed_packets);
  RUN_TEST(test_sequence_first_packet_is_always_accepted);
  RUN_TEST(test_sequence_wraps_around);
  RUN_TEST(test_sequence_accepts_big_jump_backwards);
  RUN_TEST(test_sequence_serials_are_independent);
  RUN_TEST(test_sequence_tx_increments);
  RUN_TEST(test_sequence_sending_does_not_ignore_remote);
  RUN_TEST(test_sequence_tx_follows_remote);
  RUN_TEST(test_sequence_table_full);
  RUN_TEST(test_merge_steps_same_direction);
  RUN_TEST(test_merge_steps_rejects);
  return UNITY_END();
}
