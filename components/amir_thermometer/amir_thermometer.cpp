#include "amir_thermometer.h"

#include <cmath>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace amir_thermometer {

static const char *const TAG = "amir_thermometer";

// Timing windows (microseconds), generous to cover sensor clock drift and receiver skew.
static const uint32_t SYNC_MIN = 620, SYNC_MAX = 1100;
static const uint32_t ZERO_MIN = 150, ONE_MIN = 370, ONE_MAX = 620;
static const uint32_t REPEAT_WINDOW_MS = 5000;  // identical messages within this are the same send
static const uint8_t MSG_BITS = 40;

uint8_t AmirThermometer::lfsr_digest8(const uint8_t *msg, int bytes, uint8_t gen, uint8_t key) {
  uint8_t sum = 0;
  for (int k = 0; k < bytes; k++) {
    for (int i = 7; i >= 0; i--) {
      if ((msg[k] >> i) & 1)
        sum ^= key;
      key = (key & 1) ? (key >> 1) ^ gen : key >> 1;
    }
  }
  return sum;
}

void AmirThermometer::setup() {
  this->set_interval("stale", 10000, [this]() { this->check_stale_(); });
}

void AmirThermometer::dump_config() {
  ESP_LOGCONFIG(TAG, "AMIR thermometer:");
  ESP_LOGCONFIG(TAG, "  Timeout: %u s", (unsigned) (this->timeout_ms_ / 1000));
  for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
    const Channel &ch = this->channels_[i];
    if (ch.temperature == nullptr && ch.battery_low == nullptr)
      continue;
    ESP_LOGCONFIG(TAG, "  Channel %u:", i + 1);
    if (ch.temperature != nullptr)
      LOG_SENSOR("    ", "Temperature", ch.temperature);
    if (ch.battery_low != nullptr)
      LOG_BINARY_SENSOR("    ", "Battery low", ch.battery_low);
  }
}

bool AmirThermometer::on_receive(remote_base::RemoteReceiveData data) {
  const int32_t n = data.size();
  uint8_t msg[5];
  int nbits = -1;  // -1 = waiting for a sync symbol
  bool decoded = false;

  int32_t i = 0;
  while (i < n) {
    if (data[i] <= 0) {  // skip until a mark
      i++;
      continue;
    }
    // One symbol = a mark followed by a space; merge any same-sign runs.
    uint32_t mark = 0, space = 0;
    while (i < n && data[i] > 0)
      mark += data[i++];
    while (i < n && data[i] < 0)
      space += -data[i++];

    if (mark >= SYNC_MIN && mark < SYNC_MAX && space >= SYNC_MIN && space < SYNC_MAX) {
      nbits = 0;
      memset(msg, 0, sizeof(msg));
      continue;
    }
    if (nbits < 0)
      continue;

    int bit;
    if (mark >= ZERO_MIN && mark < ONE_MIN) {
      bit = 0;
    } else if (mark >= ONE_MIN && mark < ONE_MAX) {
      bit = 1;
    } else {
      nbits = -1;
      continue;
    }
    if (bit)
      msg[nbits / 8] |= 0x80 >> (nbits % 8);
    if (++nbits == MSG_BITS) {
      nbits = -1;
      if (lfsr_digest8(msg, 4, 0x98, 0xB3) == msg[4]) {
        this->handle_message_(msg);
        decoded = true;
      } else {
        ESP_LOGV(TAG, "Checksum mismatch: %02X %02X %02X %02X %02X", msg[0], msg[1], msg[2], msg[3], msg[4]);
      }
    }
  }
  return decoded;
}

void AmirThermometer::handle_message_(const uint8_t *msg) {
  const uint8_t channel = ((msg[1] >> 4) & 0x07) + 1;
  const bool battery_low = (msg[1] & 0x80) != 0;
  const uint16_t raw = ((msg[1] & 0x0F) << 8) | msg[2];
  const float temp_c = ((int) raw - 500) / 10.0f;
  const uint32_t now = millis();

  Channel &ch = this->channels_[channel - 1];
  if (ch.seen && memcmp(ch.last_msg, msg, 5) == 0 && now - ch.last_seen < REPEAT_WINDOW_MS) {
    ch.last_seen = now;  // one of the 8 repeats of a message we already published
    return;
  }
  memcpy(ch.last_msg, msg, 5);
  ch.last_seen = now;
  ch.seen = true;
  ch.stale = false;

  if (ch.temperature == nullptr && ch.battery_low == nullptr) {
    ESP_LOGI(TAG, "Unconfigured channel %u: %.1f C, battery %s (id 0x%02X) - add it under sensors:", channel,
             temp_c, battery_low ? "low" : "ok", msg[0]);
    return;
  }
  ESP_LOGD(TAG, "Channel %u: %.1f C, battery %s (id 0x%02X, b3 0x%02X)", channel, temp_c,
           battery_low ? "low" : "ok", msg[0], msg[3]);
  if (ch.temperature != nullptr)
    ch.temperature->publish_state(temp_c);
  if (ch.battery_low != nullptr)
    ch.battery_low->publish_state(battery_low);
}

void AmirThermometer::check_stale_() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
    Channel &ch = this->channels_[i];
    if (!ch.seen || ch.stale || now - ch.last_seen < this->timeout_ms_)
      continue;
    ch.stale = true;
    ESP_LOGW(TAG, "Channel %u: no data for %u s", i + 1, (unsigned) (this->timeout_ms_ / 1000));
    if (ch.temperature != nullptr)
      ch.temperature->publish_state(NAN);
  }
}

}  // namespace amir_thermometer
}  // namespace esphome
