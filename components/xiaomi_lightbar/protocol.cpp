#include "protocol.h"

#include <cstring>

namespace esphome {
namespace xiaomi_lightbar {

uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xfffe;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t) data[i] << 8;
    for (int bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000) ? (uint16_t) ((crc << 1) ^ 0x1021) : (uint16_t) (crc << 1);
  }
  return crc;
}

void build_packet(uint32_t serial, uint8_t seq, uint8_t command, uint8_t options, uint8_t out[PACKET_SIZE]) {
  memcpy(out, PREAMBLE, sizeof(PREAMBLE));
  out[8] = (serial >> 16) & 0xFF;
  out[9] = (serial >> 8) & 0xFF;
  out[10] = serial & 0xFF;
  out[11] = 0xFF;
  out[12] = seq;
  out[13] = command;
  out[14] = options;
  uint16_t checksum = crc16(out, PACKET_SIZE - 2);
  out[15] = checksum >> 8;
  out[16] = checksum & 0xFF;
}

void unshift_raw(const uint8_t raw[RAW_SIZE], uint8_t out[PACKET_SIZE]) {
  // The first 5 bits of the packet (0b01010) are taken as part of the receive address, so the payload
  // arrives 5 bits early. Put them back and realign: out[i] = raw[i - 1] << 3 | raw[i] >> 5.
  out[0] = 0x50 | raw[0] >> 5;
  for (size_t i = 1; i < PACKET_SIZE; i++)
    out[i] = ((raw[i - 1] >> 1) & 0x0F) << 4 | (raw[i - 1] & 0x01) << 3 | raw[i] >> 5;
}

bool parse_packet(const uint8_t data[PACKET_SIZE], Packet &out) {
  if (memcmp(data, PREAMBLE, sizeof(PREAMBLE)) != 0)
    return false;
  uint16_t checksum = (uint16_t) (data[15] << 8 | data[16]);
  if (crc16(data, PACKET_SIZE - 2) != checksum)
    return false;
  out.serial = (uint32_t) data[8] << 16 | (uint32_t) data[9] << 8 | data[10];
  out.seq = data[12];
  out.command = data[13];
  out.options = data[14];
  return true;
}

bool merge_steps(uint8_t command, uint8_t first, uint8_t second, uint8_t &merged) {
  if (command < (uint8_t) Command::COOLER || command > (uint8_t) Command::DIMMER)
    return false;
  const int a = (int8_t) first;
  const int b = (int8_t) second;
  if (a == 0 || b == 0 || (a > 0) != (b > 0))
    return false;
  const int sum = a + b;
  if (sum > INT8_MAX || sum < INT8_MIN)
    return false;
  merged = (uint8_t) (int8_t) sum;
  return true;
}

SequenceTable::Entry *SequenceTable::find_or_add_(uint32_t serial) {
  for (uint8_t i = 0; i < this->count_; i++) {
    if (this->entries_[i].serial == serial)
      return &this->entries_[i];
  }
  if (this->count_ >= MAX_SERIALS)
    return nullptr;
  Entry *entry = &this->entries_[this->count_++];
  *entry = Entry{serial, 0, 0, false};
  return entry;
}

bool SequenceTable::next_tx(uint32_t serial, uint8_t &seq) {
  Entry *entry = this->find_or_add_(serial);
  if (entry == nullptr)
    return false;
  seq = ++entry->tx_seq;
  return true;
}

bool SequenceTable::accept_rx(uint32_t serial, uint8_t seq, uint8_t *missed) {
  Entry *entry = this->find_or_add_(serial);
  if (entry == nullptr)
    return false;
  // The remote sends every press as a burst of identical packets; only the first may be handled. The
  // difference is taken as int8_t so the 8-bit counter can wrap around. A big jump backwards (e.g. the
  // remote lost power) is accepted.
  int8_t difference = (int8_t) (uint8_t) (seq - entry->last_rx);
  if (entry->has_rx && difference <= 0 && difference > -64)
    return false;
  if (missed != nullptr)
    *missed = entry->has_rx && difference > 0 ? difference - 1 : 0;
  entry->last_rx = seq;
  entry->has_rx = true;
  entry->tx_seq = seq;
  return true;
}

}  // namespace xiaomi_lightbar
}  // namespace esphome
