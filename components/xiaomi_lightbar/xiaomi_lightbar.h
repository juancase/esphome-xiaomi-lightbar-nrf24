#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/components/spi/spi.h"

#include "protocol.h"

namespace esphome {
namespace xiaomi_lightbar {

/*
 * Hub: drives the nRF24L01 over SPI and sends / receives packets of the light bar protocol.
 *
 * Each packet is sent TX_COPIES times, TX_INTERVAL_MS apart, like the original remote. The copies are
 * spread over loop() calls and the radio returns to receive mode after each one, so the remote is
 * still heard during a burst (~200 ms). Steps that arrive meanwhile are merged into the command
 * waiting in the queue (see merge_steps()), so the bar keeps up with a fast-turned wheel.
 *
 * loop() runs continuously: the RX FIFO holds only 3 packets, and with the default loop interval it
 * fills up with copies of one packet while the remote is turned quickly, losing the next one.
 */
class XiaomiLightbar : public Component,
                       public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW,
                                             spi::CLOCK_PHASE_LEADING, spi::DATA_RATE_8MHZ> {
 public:
  static constexpr uint8_t TX_COPIES = 20;
  static constexpr uint32_t TX_INTERVAL_MS = 10;

  void set_ce_pin(GPIOPin *ce_pin) { this->ce_pin_ = ce_pin; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Queues a command addressed to this serial.
  void send_command(uint32_t serial, Command command, uint8_t options);

  // Called for every new command received from this serial (repeated copies are filtered out).
  using RemoteCallback = std::function<void(uint8_t command, uint8_t options)>;
  void add_remote_listener(uint32_t serial, RemoteCallback callback);

 protected:
  bool init_radio_();
  void start_listening_();
  bool begin_next_packet_();
  void transmit_copy_();
  void poll_receive_();
  void handle_raw_(const uint8_t raw[RAW_SIZE]);

  uint8_t read_register_(uint8_t reg);
  void write_register_(uint8_t reg, uint8_t value);
  void write_register_(uint8_t reg, const uint8_t *data, size_t len);
  uint8_t command_(uint8_t command);

  GPIOPin *ce_pin_{nullptr};
  HighFrequencyLoopRequester high_freq_;
  bool radio_ok_{false};
  uint32_t last_check_at_{0};

  SequenceTable sequences_;
  struct QueuedCommand {
    uint32_t serial;
    Command command;
    uint8_t options;
  };
  // The front one is being sent (if transmitting_); its packet is built when the burst starts.
  std::deque<QueuedCommand> tx_queue_;
  std::array<uint8_t, PACKET_SIZE> packet_{};
  bool transmitting_{false};
  uint8_t copies_sent_{0};
  uint32_t last_copy_at_{0};

  struct RemoteListener {
    uint32_t serial;
    RemoteCallback callback;
  };
  std::vector<RemoteListener> listeners_;
};

}  // namespace xiaomi_lightbar
}  // namespace esphome
