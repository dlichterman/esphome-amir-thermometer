# ESPHome AMIR fridge/freezer thermometer receiver

This is an ESPHome external component and example config for an ESP32 and a CC1101. They receive **AMIR wireless fridge/freezer thermometers** on 433.92 MHz and publish each sensor's temperature and low-battery flag to Home Assistant.

## Hardware

- ESP32 DevKit V1 (any ESP32 should work; change `board:`)
- A CC1101 module tuned for 433 MHz, with its antenna

| CC1101 | ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCK | GPIO18 |
| MISO (SO) | GPIO19 |
| MOSI (SI) | GPIO23 |
| CSN | GPIO22 |
| GDO2 | GPIO4 (received data → `remote_receiver`) |
| GDO0 | not needed for receive-only |

To use different pins, change the `substitutions:` at the top of [amir-thermometer.yaml](amir-thermometer.yaml).

## Install

1. Copy [amir-thermometer.yaml](amir-thermometer.yaml) into your ESPHome config directory. It pulls the component straight from this repo:

   ```yaml
   external_components:
     - source: github://dlichterman/esphome-amir-thermometer@main
       components: [amir_thermometer]
   ```
2. Make sure your `secrets.yaml` has the keys listed in [secrets.example.yaml](secrets.example.yaml).
3. Flash it:

```powershell
esphome run amir-thermometer.yaml
```

Or add it as a device in the ESPHome dashboard.

Each configured channel gives you two entities:
- **Sensor N Temperature** (°C; Home Assistant can show it in °F)
- **Sensor N Battery Low**

If a sensor goes silent for `timeout` (default 5 min), its temperature becomes *unknown*. Sensors you haven't configured are still logged, as `Unconfigured channel N: ...`, so you can find them.

## Home Assistant alerts

[home-assistant/](home-assistant/) has a blueprint that alerts when a temperature stays out of range, tells you when it's back in range, and warns when a sensor goes silent.

1. Import the blueprint: in Home Assistant go to **Settings → Automations & Scenes → Blueprints → Import Blueprint** and paste:

   ```text
   https://github.com/dlichterman/esphome-amir-thermometer/blob/main/home-assistant/blueprints/automation/amir/temperature_threshold_alert.yaml
   ```

   Or copy it to `config/blueprints/automation/amir/` yourself.
2. Create one automation from it per sensor, or start from [automations.example.yaml](home-assistant/automations.example.yaml). The example has a fridge (33–41 °F), a freezer (−22 to 10 °F) and a battery-low alert. Replace `notify.mobile_app_your_phone` with your own notify service.

The blueprint has these settings:

| Input | Default | What it does |
|---|---|---|
| Low / high threshold | — | Uses the unit Home Assistant displays for the sensor (°F or °C) |
| Time out of range | 15 min | Rides out door openings and defrost cycles |
| Time unknown/unavailable | 15 min | Alerts if the receiver or a sensor stops reporting |
| Notify action | — | Any action; can use `{{ title }}` and `{{ message }}` |

An all-clear is sent only after a real alert, once the temperature has been back in range for 2 minutes.

## Configuration

```yaml
amir_thermometer:
  receiver_id: rf_rx      # your remote_receiver
  timeout: 5min
  sensors:
    - channel: 1          # the sensor number shown on the display (1-8)
      temperature:
        name: "Freezer Temperature"
      battery_low:
        name: "Freezer Battery Low"
```

`temperature` and `battery_low` take all the usual ESPHome sensor and binary_sensor options (filters, icons and so on).

## Protocol

This was decoded from raw captures from a CC1101 sniffer and checked against the thermometer's display.

- OOK PWM at 433.92 MHz, with a ~750 µs bit period. A ~270 µs pulse is a **0** and a ~500 µs pulse is a **1**.
- Each message is 8 sync symbols (~750 µs pulse and ~750 µs gap), then 40 data bits, then a stop pulse.
- The message is sent 4 times per group. The group is repeated about 120 ms later, and each sensor transmits about every 50 s.

| Byte | Meaning |
|---|---|
| b0 | Random sensor ID; changes on every battery change, so don't key on it |
| b1 bit 7 | Battery low |
| b1 bits 6–4 | Channel − 1 |
| b1 bits 3–0, b2 | 12-bit temperature: °C = (raw − 500) / 10 |
| b3 | `0x0A` on every unit seen so far |
| b4 | Checksum: rtl_433-style `lfsr_digest8(b0..b3, gen 0x98, key 0xB3)` |

Example: `F0 9 2 2D 0A ..` is channel 2, battery low, raw 0x22D = 557, which is 5.7 °C / 42.3 °F.

Not yet confirmed: readings below 0 °C (they should work, since −10 °C is raw 400), the battery bit reading 0 with fresh batteries, and whether b3 ever changes.

## Troubleshooting

- **No readings:** set `logger: level: VERBOSE` to see checksum failures, or uncomment `dump: raw` under `remote_receiver` to check that pulses arrive at all. Try `cc1101_frequency` values of 433.90–433.97 MHz.
- **A flood of raw noise in the log:** that's normal for an OOK receiver without a signal. Raise the `remote_receiver` `filter:` (up to ~200 µs), or lower the CC1101 gain, for example `max_lna_gain: 14.6dB` or `magn_target: 33dB`.
