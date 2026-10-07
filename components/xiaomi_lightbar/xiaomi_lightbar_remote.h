#pragma once

#include "esphome/core/component.h"
#include "esphome/components/event/event.h"

#include "xiaomi_lightbar.h"

namespace esphome {
namespace xiaomi_lightbar {

/*
 * One original remote as an event entity: every press or wheel turn fires an event, whether or not a
 * bar listens to that serial.
 */
class XiaomiLightbarRemote : public event::Event, public Component {
 public:
  void set_hub(XiaomiLightbar *hub) { this->hub_ = hub; }
  void set_serial(uint32_t serial) { this->serial_ = serial; }

  void setup() override;
  void dump_config() override;

 protected:
  void on_command_(uint8_t command);

  XiaomiLightbar *hub_{nullptr};
  uint32_t serial_{0};
};

}  // namespace xiaomi_lightbar
}  // namespace esphome
