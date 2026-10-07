# alpicool-esphome

ESPHome external component for Alpicool-protocol 12 V fridges (Alpicool, BrassMonkey,
Ocean Comfort, MAENTUM, …) over BLE, with single- and dual-zone support. An ESP32 near
the fridge holds the BLE connection and publishes to MQTT (or the Home Assistant API),
so it works in a vehicle away from home.

Ported from [Gruni22/alpicool_ha_ble](https://github.com/Gruni22/alpicool_ha_ble) (MIT),
with the protocol as documented by
[klightspeed/BrassMonkeyFridgeMonitor](https://github.com/klightspeed/BrassMonkeyFridgeMonitor) (MIT).

## Quick start

1. Find the fridge's MAC with **nRF Connect** (close the fridge's phone app first; it
   accepts only one connection). It should offer service `0x1234`.
2. Copy `secrets.example.yaml` to `secrets.yaml` and fill it in.
3. Set `fridge_mac` in `alpicool-fridge.yaml`, then:
   ```
   esphome run alpicool-fridge.yaml
   ```

On Windows, if the build fails with "path not found" errors from CMake, the project path is
too long. Set `esphome: build_path:` to something short like `C:/esp/fridge`.

## Entities

| Platform | Keys |
|---|---|
| `climate` | one per `zone: left/right` — on/off, target, current temp, preset Boost (= Max) / Eco |
| `sensor` | `left_/right_current_temperature`, `left_/right_target_temperature`, `battery_percent`, `battery_voltage`, `running_status` (raw, meaning unknown) |
| `binary_sensor` | `online` (connected and receiving status), `dual_zone` |
| `switch` | `power`, `lock` (control panel lock) |
| `select` | `run_mode` (Max/Eco, labels overridable), `battery_saver` (Low/Medium/High) |
| `number` | `left_/right_target_temperature`, `left_/right_hysteresis`, `start_delay` |

Temperatures are always reported in °C. A fridge set to °F is converted both ways.
Hysteresis is a raw difference in whatever unit the fridge uses.

Power and run mode apply to the whole fridge, so changing them on one zone's climate
entity changes the other zone too.

## Hub options

```yaml
alpicool_fridge:
  - id: fridge
    ble_client_id: fridge_ble
    update_interval: 30s      # status poll
    status_timeout: 120s      # values go unavailable after this long without a status
    bind_on_connect: false    # some models only answer after a bind ("APP" + button press)
```

Several fridges: add one `ble_client` and one `alpicool_fridge` entry for each, and set
`alpicool_fridge_id:` on every entity platform. ESPHome allows 3 BLE connections by
default, so the fridge can share an ESP32 with a BLE BMS.

## Status of testing

Tested on hardware (2026-10-07): a dual-zone fridge advertising as `A1-…`, with a
Waveshare ESP32-C6, ESPHome 2026.6.5.

- Connects without bind; status polling works; both zones decode correctly.
- The fridge sends the doubled checksum variant and negotiates MTU 247.
- Writes confirmed on the fridge's display: left target, button lock, run mode
  (Eco → Max → Eco). After the settings-frame writes, the right zone, battery
  protection, hysteresis and start delay were unchanged.
- Not yet exercised on hardware: right target, power, battery saver, hysteresis and
  start delay writes. They use the same frames as the tested ones.

The protocol code is also checked against captures from BrassMonkey, neftaly, a MAENTUM
IceCubeX and the fridge above (`tests/test_protocol.cpp`):
```
g++ -std=c++20 -I components tests/test_protocol.cpp -o test_protocol && ./test_protocol
```

To debug a different model, set `logger: level: VERBOSE` to see every BLE frame. Keep
`wifi: DEBUG` under `logger: logs:`, because ESPHome's VERBOSE Wi-Fi log prints the
Wi-Fi password.

## Credits

- Gruni22/alpicool_ha_ble — MIT, Copyright (c) Gruni22
- klightspeed/BrassMonkeyFridgeMonitor — MIT, Copyright (c) klightspeed
