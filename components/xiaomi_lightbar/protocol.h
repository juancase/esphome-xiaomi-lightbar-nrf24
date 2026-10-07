#pragma once

#include <cstddef>
#include <cstdint>

/*
 * Radio protocol of the Xiaomi Mi Computer Monitor Light Bar (MJGJD01YL) remote.
 * Plain C++, no Arduino or ESPHome dependency, so it can be tested on the PC.
 *
 * Packet structure (17 bytes):
 *  0 –  7: Preamble (see PREAMBLE)
 *  8 – 10: Remote ID (serial), big-endian
 * 11 – 11: Separator (0xFF)
 * 12 – 12: Sequence counter
 * 13 – 14: Command ID + options
 * 15 – 16: CRC16 checksum over bytes 0 – 14, big-endian
 *
 * Protocol facts from https://github.com/lamperez/xiaomi-lightbar-nrf24. Packet building and the
 * receive realignment are adapted from https://github.com/ebinf/lightbar2mqtt (MIT, see LICENSE).
 */

namespace esphome {
namespace xiaomi_lightbar {

static const size_t PACKET_SIZE = 17;
// Bytes read from the radio when receiving: one more than PACKET_SIZE, see unshift_raw().
static const size_t RAW_SIZE = 18;

static constexpr uint8_t PREAMBLE[8] = {0x53, 0x39, 0x14, 0xDD, 0x1C, 0x49, 0x34, 0x12};
static const uint64_t SEND_ADDRESS = 0x5555555555ULL;
static const uint64_t RECEIVE_ADDRESS = 0xAAAAAAAAAAULL;
static const uint8_t CHANNEL = 68;

// Command IDs as sent by the original remote. Commands 0x02 – 0x05 carry a signed number of
// steps in the options byte.
enum class Command : uint8_t {
  ON_OFF = 0x01,
  COOLER = 0x02,
  WARMER = 0x03,
  BRIGHTER = 0x04,
  DIMMER = 0x05,
  RESET = 0x06,
};

struct Packet {
  uint32_t serial;
  uint8_t seq;
  uint8_t command;
  uint8_t options;
};

// CRC16: polynomial 0x1021, init 0xfffe, no reflection, no final XOR.
uint16_t crc16(const uint8_t *data, size_t len);

void build_packet(uint32_t serial, uint8_t seq, uint8_t command, uint8_t options, uint8_t out[PACKET_SIZE]);

// The receiver is set up with an address that only partially overlaps the real preamble, so the
// payload arrives shifted. This restores the original packet. See
// https://github.com/lamperez/xiaomi-lightbar-nrf24?tab=readme-ov-file#baseband-packet-format
void unshift_raw(const uint8_t raw[RAW_SIZE], uint8_t out[PACKET_SIZE]);

// Returns false if the preamble or the checksum do not match.
bool parse_packet(const uint8_t data[PACKET_SIZE], Packet &out);

// Two step commands of the same kind and direction (e.g. dimmer -1 and dimmer -2) can be sent as
// one (dimmer -3). Returns false if the command takes no steps, the directions differ or the sum
// does not fit in the options byte.
bool merge_steps(uint8_t command, uint8_t first, uint8_t second, uint8_t &merged);

/*
 * Sequence counters per serial.
 *
 *  - tx_seq:  counter used for sending. It follows the remote, so the bar accepts our packets.
 *  - last_rx: last counter received, used to drop the repeated copies of a packet the remote sends
 *             for every key press. Kept apart from tx_seq so our own transmissions never make us
 *             ignore the remote.
 */
class SequenceTable {
 public:
  static constexpr uint8_t MAX_SERIALS = 32;

  // Next counter to send with. Returns false if the serial is new and the table is full.
  bool next_tx(uint32_t serial, uint8_t &seq);
  // True if a received packet is new; false for a repeated or old copy, or if the serial is new and
  // the table is full. `missed`, if given, receives the number of counters skipped since the last one.
  bool accept_rx(uint32_t serial, uint8_t seq, uint8_t *missed = nullptr);

 protected:
  struct Entry {
    uint32_t serial;
    uint8_t tx_seq;
    uint8_t last_rx;
    bool has_rx;
  };
  Entry *find_or_add_(uint32_t serial);

  Entry entries_[MAX_SERIALS]{};
  uint8_t count_{0};
};

}  // namespace xiaomi_lightbar
}  // namespace esphome
