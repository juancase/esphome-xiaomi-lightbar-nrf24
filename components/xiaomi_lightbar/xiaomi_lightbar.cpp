#include "xiaomi_lightbar.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <utility>

namespace esphome {
namespace xiaomi_lightbar {

static const char *const TAG = "xiaomi_lightbar";

// nRF24L01 registers and commands (datasheet section 9.1 and 8.3.1).
static const uint8_t REG_CONFIG = 0x00;
static const uint8_t REG_EN_AA = 0x01;
static const uint8_t REG_EN_RXADDR = 0x02;
static const uint8_t REG_SETUP_AW = 0x03;
static const uint8_t REG_SETUP_RETR = 0x04;
static const uint8_t REG_RF_CH = 0x05;
static const uint8_t REG_RF_SETUP = 0x06;
static const uint8_t REG_STATUS = 0x07;
static const uint8_t REG_RX_ADDR_P0 = 0x0A;
static const uint8_t REG_TX_ADDR = 0x10;
static const uint8_t REG_RX_PW_P0 = 0x11;
static const uint8_t REG_FIFO_STATUS = 0x17;
static const uint8_t REG_DYNPD = 0x1C;
static const uint8_t REG_FEATURE = 0x1D;

static const uint8_t CMD_R_REGISTER = 0x00;
static const uint8_t CMD_W_REGISTER = 0x20;
static const uint8_t CMD_R_RX_PAYLOAD = 0x61;
static const uint8_t CMD_W_TX_PAYLOAD = 0xA0;
static const uint8_t CMD_FLUSH_TX = 0xE1;
static const uint8_t CMD_FLUSH_RX = 0xE2;
static const uint8_t CMD_NOP = 0xFF;

// CONFIG: powered up, no CRC (the protocol has its own), PRIM_RX selects receive / transmit.
static const uint8_t CONFIG_PWR_UP = 0x02;
static const uint8_t CONFIG_PRIM_RX = 0x01;
// RF_SETUP: 2 Mbps, maximum output power.
static const uint8_t RF_SETUP_2MBPS_MAX_POWER = 0x0F;
static const uint8_t STATUS_CLEAR_IRQS = 0x70;
static const uint8_t STATUS_TX_DS = 0x20;
static const uint8_t FIFO_STATUS_RX_EMPTY = 0x01;

// Retry the radio set up this often while it is not responding, and check it is still configured
// (e.g. it did not lose power) this often while it works.
static const uint32_t RETRY_INTERVAL_MS = 1000;
static const uint32_t CHECK_INTERVAL_MS = 10000;
// Send queue limit, only reached while the radio is not working.
static const size_t MAX_QUEUED_PACKETS = 16;
// A copy is on air for ~0.2 ms (130 µs to settle + 17 bytes at 2 Mbps). Wait at most this long for
// it before listening again.
static const uint32_t TX_DONE_TIMEOUT_US = 1000;

static const char *command_name(uint8_t command) {
  switch ((Command) command) {
    case Command::ON_OFF:
      return "on/off";
    case Command::COOLER:
      return "cooler";
    case Command::WARMER:
      return "warmer";
    case Command::BRIGHTER:
      return "brighter";
    case Command::DIMMER:
      return "dimmer";
    case Command::RESET:
      return "reset";
  }
  return "unknown";
}

static void address_bytes(uint64_t address, uint8_t out[5]) {
  // Addresses are written least significant byte first.
  for (int i = 0; i < 5; i++)
    out[i] = (address >> (8 * i)) & 0xFF;
}

void XiaomiLightbar::setup() {
  this->spi_setup();
  this->ce_pin_->setup();
  this->ce_pin_->digital_write(false);
  if (!this->init_radio_())
    this->status_set_error(LOG_STR("nRF24 not responding, check the wiring"));
  this->last_check_at_ = millis();
  this->high_freq_.start();
}

void XiaomiLightbar::dump_config() {
  ESP_LOGCONFIG(TAG, "Xiaomi Light Bar (nRF24):");
  LOG_PIN("  CE Pin: ", this->ce_pin_);
  LOG_PIN("  CS Pin: ", this->cs_);
  ESP_LOGCONFIG(TAG, "  Channel: %u", CHANNEL);
  if (!this->radio_ok_)
    ESP_LOGE(TAG, "  nRF24 not responding!");
}

bool XiaomiLightbar::init_radio_() {
  this->ce_pin_->digital_write(false);
  this->transmitting_ = false;

  // Address width 5 bytes. Reading it back tells whether the chip is there at all.
  this->write_register_(REG_SETUP_AW, 0x03);
  if (this->read_register_(REG_SETUP_AW) != 0x03) {
    this->radio_ok_ = false;
    return false;
  }

  uint8_t address[5];
  this->write_register_(REG_EN_AA, 0x00);  // No auto acknowledge, required to disable the CRC.
  this->write_register_(REG_SETUP_RETR, 0xFF);
  this->write_register_(REG_RF_CH, CHANNEL);
  this->write_register_(REG_RF_SETUP, RF_SETUP_2MBPS_MAX_POWER);
  this->write_register_(REG_DYNPD, 0x00);
  this->write_register_(REG_FEATURE, 0x00);
  this->write_register_(REG_EN_RXADDR, 0x01);
  this->write_register_(REG_RX_PW_P0, PACKET_SIZE);
  address_bytes(RECEIVE_ADDRESS, address);
  this->write_register_(REG_RX_ADDR_P0, address, sizeof(address));
  address_bytes(SEND_ADDRESS, address);
  this->write_register_(REG_TX_ADDR, address, sizeof(address));
  this->command_(CMD_FLUSH_TX);
  this->command_(CMD_FLUSH_RX);
  this->write_register_(REG_STATUS, STATUS_CLEAR_IRQS);

  this->radio_ok_ = this->read_register_(REG_RF_CH) == CHANNEL;
  if (this->radio_ok_)
    this->start_listening_();
  return this->radio_ok_;
}

void XiaomiLightbar::start_listening_() {
  this->ce_pin_->digital_write(false);
  this->write_register_(REG_CONFIG, CONFIG_PWR_UP | CONFIG_PRIM_RX);
  this->write_register_(REG_STATUS, STATUS_CLEAR_IRQS);
  this->ce_pin_->digital_write(true);
}

bool XiaomiLightbar::begin_next_packet_() {
  while (!this->tx_queue_.empty()) {
    const auto &next = this->tx_queue_.front();
    uint8_t seq;
    if (this->sequences_.next_tx(next.serial, seq)) {
      build_packet(next.serial, seq, (uint8_t) next.command, next.options, this->packet_.data());
      char hex[PACKET_SIZE * 2 + 1];
      ESP_LOGD(TAG, "Sending %s (0x%02X) to 0x%06" PRIX32 ", seq %u: %s", command_name((uint8_t) next.command),
               next.options, next.serial, seq, format_hex_to(hex, this->packet_));
      this->copies_sent_ = 0;
      return true;
    }
    ESP_LOGE(TAG, "Too many serials, cannot send to 0x%06" PRIX32, next.serial);
    this->tx_queue_.pop_front();
  }
  return false;
}

void XiaomiLightbar::transmit_copy_() {
  this->ce_pin_->digital_write(false);
  this->write_register_(REG_CONFIG, CONFIG_PWR_UP);
  this->write_register_(REG_STATUS, STATUS_CLEAR_IRQS);
  this->command_(CMD_FLUSH_TX);
  this->enable();
  this->write_byte(CMD_W_TX_PAYLOAD);
  this->write_array(this->packet_.data(), this->packet_.size());
  this->disable();
  // A CE pulse of at least 10 µs sends the payload.
  this->ce_pin_->digital_write(true);
  delayMicroseconds(15);
  this->ce_pin_->digital_write(false);
  // Wait until the copy is out (TX_DS) before listening again: switching to receive earlier would cut it.
  const uint32_t started = micros();
  while (!(this->command_(CMD_NOP) & STATUS_TX_DS) && micros() - started < TX_DONE_TIMEOUT_US)
    delayMicroseconds(10);
  this->start_listening_();
  this->copies_sent_++;
  this->last_copy_at_ = millis();
}

void XiaomiLightbar::send_command(uint32_t serial, Command command, uint8_t options) {
  // Add steps to the last command waiting, unless its burst has already started.
  if (!this->tx_queue_.empty() && !(this->transmitting_ && this->tx_queue_.size() == 1)) {
    auto &last = this->tx_queue_.back();
    uint8_t merged;
    if (last.serial == serial && last.command == command &&
        merge_steps((uint8_t) command, last.options, options, merged)) {
      ESP_LOGV(TAG, "Adding %s (0x%02X) to the one waiting: 0x%02X", command_name((uint8_t) command), options,
               merged);
      last.options = merged;
      return;
    }
  }
  if (this->tx_queue_.size() >= MAX_QUEUED_PACKETS) {
    ESP_LOGW(TAG, "Send queue full, dropping %s for 0x%06" PRIX32, command_name((uint8_t) command), serial);
    return;
  }
  this->tx_queue_.push_back(QueuedCommand{serial, command, options});
}

void XiaomiLightbar::loop() {
  const uint32_t now = millis();

  if (!this->radio_ok_) {
    if (now - this->last_check_at_ < RETRY_INTERVAL_MS)
      return;
    this->last_check_at_ = now;
    if (this->init_radio_()) {
      ESP_LOGI(TAG, "nRF24 is responding now");
      this->status_clear_error();
    }
    return;
  }

  if (this->transmitting_) {
    // The radio listens between copies.
    this->poll_receive_();
    if (now - this->last_copy_at_ < TX_INTERVAL_MS)
      return;
    if (this->copies_sent_ < TX_COPIES) {
      this->transmit_copy_();
      return;
    }
    this->tx_queue_.pop_front();
    if (this->begin_next_packet_()) {
      this->transmit_copy_();
      return;
    }
    this->transmitting_ = false;
    return;
  }

  if (this->begin_next_packet_()) {
    this->transmitting_ = true;
    this->transmit_copy_();
    return;
  }

  this->poll_receive_();

  if (now - this->last_check_at_ >= CHECK_INTERVAL_MS) {
    this->last_check_at_ = now;
    if (this->read_register_(REG_RF_CH) != CHANNEL) {
      ESP_LOGW(TAG, "nRF24 lost its configuration, setting it up again");
      if (!this->init_radio_())
        this->status_set_error(LOG_STR("nRF24 not responding, check the wiring"));
    }
  }
}

void XiaomiLightbar::poll_receive_() {
  // Empty the FIFO, but don't get stuck if packets keep arriving.
  for (int i = 0; i < 6; i++) {
    if (this->read_register_(REG_FIFO_STATUS) & FIFO_STATUS_RX_EMPTY)
      break;
    uint8_t raw[RAW_SIZE] = {0};
    this->enable();
    this->write_byte(CMD_R_RX_PAYLOAD);
    this->read_array(raw, PACKET_SIZE);
    this->disable();
    this->write_register_(REG_STATUS, STATUS_CLEAR_IRQS);
    this->handle_raw_(raw);
  }
}

void XiaomiLightbar::handle_raw_(const uint8_t raw[RAW_SIZE]) {
  uint8_t data[PACKET_SIZE];
  unshift_raw(raw, data);
  Packet packet;
  if (!parse_packet(data, packet)) {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERY_VERBOSE
    char hex[PACKET_SIZE * 2 + 1];
    ESP_LOGVV(TAG, "Ignoring invalid packet: %s", format_hex_to(hex, data, PACKET_SIZE));
#endif
    return;
  }
  uint8_t missed;
  if (!this->sequences_.accept_rx(packet.serial, packet.seq, &missed))
    return;
  if (missed > 0)
    ESP_LOGW(TAG, "Missed %u packet(s) from 0x%06" PRIX32 " before seq %u", missed, packet.serial, packet.seq);
  ESP_LOGD(TAG, "Received %s (0x%02X) from 0x%06" PRIX32 ", seq %u", command_name(packet.command), packet.options,
           packet.serial, packet.seq);
  for (const auto &listener : this->listeners_) {
    if (listener.serial == packet.serial)
      listener.callback(packet.command, packet.options);
  }
}

void XiaomiLightbar::add_remote_listener(uint32_t serial, RemoteCallback callback) {
  this->listeners_.push_back(RemoteListener{serial, std::move(callback)});
}

uint8_t XiaomiLightbar::read_register_(uint8_t reg) {
  this->enable();
  this->write_byte(CMD_R_REGISTER | reg);
  uint8_t value = this->read_byte();
  this->disable();
  return value;
}

void XiaomiLightbar::write_register_(uint8_t reg, uint8_t value) { this->write_register_(reg, &value, 1); }

void XiaomiLightbar::write_register_(uint8_t reg, const uint8_t *data, size_t len) {
  this->enable();
  this->write_byte(CMD_W_REGISTER | reg);
  this->write_array(data, len);
  this->disable();
}

uint8_t XiaomiLightbar::command_(uint8_t command) {
  this->enable();
  uint8_t status = this->transfer_byte(command);
  this->disable();
  return status;
}

}  // namespace xiaomi_lightbar
}  // namespace esphome
