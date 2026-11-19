#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace amir_thermometer {

// AMIR wireless fridge/freezer thermometer, 433.92 MHz OOK.
//
// Each message: 8 sync symbols (~750 us mark + ~750 us space), then 40 PWM bits with a
// ~750 us period (~270 us mark = 0, ~500 us mark = 1), then one stop mark. Sent 4x per
// group, the group repeats ~120 ms later, and each sensor sends every ~50 s.
//   b0  random ID (changes on every battery change)
//   b1  bit 7 = battery low, bits 6..4 = channel - 1, bits 3..0 = temperature bits 11..8
//   b2  temperature bits 7..0; degrees C = (raw - 500) / 10
//   b3  0x0A on every unit seen so far
//   b4  LFSR-8 digest of b0..b3 (gen 0x98, key 0xB3)
static const uint8_t MAX_CHANNELS = 8;

class AmirThermometer : public Component, public remote_base::RemoteReceiverListener {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  bool on_receive(remote_base::RemoteReceiveData data) override;

  void set_temperature_sensor(uint8_t channel, sensor::Sensor *s) { this->channels_[channel - 1].temperature = s; }
  void set_battery_low_sensor(uint8_t channel, binary_sensor::BinarySensor *s) {
    this->channels_[channel - 1].battery_low = s;
  }
  void set_timeout(uint32_t timeout_ms) { this->timeout_ms_ = timeout_ms; }

  static uint8_t lfsr_digest8(const uint8_t *msg, int bytes, uint8_t gen, uint8_t key);

 protected:
  struct Channel {
    sensor::Sensor *temperature{nullptr};
    binary_sensor::BinarySensor *battery_low{nullptr};
    uint8_t last_msg[5]{};
    uint32_t last_seen{0};
    bool seen{false};
    bool stale{false};
  };

  void handle_message_(const uint8_t *msg);
  void check_stale_();

  Channel channels_[MAX_CHANNELS];
  uint32_t timeout_ms_{300000};
};

}  // namespace amir_thermometer
}  // namespace esphome
