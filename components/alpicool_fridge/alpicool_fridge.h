#pragma once

#ifdef USE_ESP32

#include <deque>
#include <vector>

#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

#include "protocol.h"

namespace esphome::alpicool_fridge {

namespace espbt = esphome::esp32_ble_tracker;

static const uint16_t SERVICE_UUID = 0x1234;
static const uint16_t WRITE_CHARACTERISTIC_UUID = 0x1235;
static const uint16_t NOTIFY_CHARACTERISTIC_UUID = 0x1236;

enum Zone : uint8_t { ZONE_LEFT = 0, ZONE_RIGHT = 1 };

class AlpicoolClimate;
class AlpicoolSwitch;
class AlpicoolSelect;
class AlpicoolNumber;

enum SwitchType : uint8_t { SWITCH_POWER, SWITCH_LOCK };
enum SelectType : uint8_t { SELECT_RUN_MODE, SELECT_BATTERY_SAVER };
enum NumberType : uint8_t {
  NUMBER_LEFT_TARGET,
  NUMBER_RIGHT_TARGET,
  NUMBER_LEFT_HYSTERESIS,
  NUMBER_RIGHT_HYSTERESIS,
  NUMBER_START_DELAY,
};

class AlpicoolFridge : public PollingComponent, public ble_client::BLEClientNode {
 public:
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_bind_on_connect(bool bind_on_connect) { this->bind_on_connect_ = bind_on_connect; }
  void set_status_timeout(uint32_t status_timeout) { this->status_timeout_ = status_timeout; }

#ifdef USE_BINARY_SENSOR
  void set_online_binary_sensor(binary_sensor::BinarySensor *s) { this->online_binary_sensor_ = s; }
  void set_dual_zone_binary_sensor(binary_sensor::BinarySensor *s) { this->dual_zone_binary_sensor_ = s; }
#endif
#ifdef USE_SENSOR
  void set_current_temperature_sensor(Zone zone, sensor::Sensor *s) { this->current_temperature_sensor_[zone] = s; }
  void set_target_temperature_sensor(Zone zone, sensor::Sensor *s) { this->target_temperature_sensor_[zone] = s; }
  void set_battery_percent_sensor(sensor::Sensor *s) { this->battery_percent_sensor_ = s; }
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->battery_voltage_sensor_ = s; }
  void set_running_status_sensor(sensor::Sensor *s) { this->running_status_sensor_ = s; }
#endif
  void set_climate(Zone zone, AlpicoolClimate *climate) { this->climate_[zone] = climate; }
  void set_switch(SwitchType type, AlpicoolSwitch *sw) { this->switch_[type] = sw; }
  void set_select(SelectType type, AlpicoolSelect *select) { this->select_[type] = select; }
  void set_number(NumberType type, AlpicoolNumber *number) { this->number_[type] = number; }

  // Setters used by the entities. All of them need a status first, because the
  // fridge only accepts complete settings frames.
  void set_target_temperature(Zone zone, float celsius);
  void set_power(bool on);
  void set_lock(bool locked);
  void set_run_mode(uint8_t run_mode);
  void set_battery_saver(uint8_t level);
  void set_hysteresis(Zone zone, float value);
  void set_start_delay(uint8_t minutes);

  bool has_status() const { return this->has_status_; }
  const FridgeStatus &status() const { return this->status_; }
  float to_celsius(int8_t raw) const;
  int8_t from_celsius(float celsius) const;

 protected:
  void on_frame_(const Frame &frame);
  void handle_status_(const FridgeStatus &status);
  void send_settings_(const FridgeStatus &status);
  void enqueue_(const std::vector<uint8_t> &frame);
  void request_status_soon_();
  bool can_write_() const;
  void publish_state_();
  void publish_offline_();
  void set_online_(bool online);

  bool bind_on_connect_{false};
  uint32_t status_timeout_{120000};

  uint16_t write_handle_{0};
  uint16_t notify_handle_{0};
  esp_gatt_write_type_t write_type_{ESP_GATT_WRITE_TYPE_RSP};

  FrameReader reader_;
  // Chunks waiting to be written; spaced out so the fridge can reassemble them.
  std::deque<std::vector<uint8_t>> tx_queue_;
  uint32_t last_write_ms_{0};

  FridgeStatus status_;
  bool has_status_{false};
  bool online_{false};
  uint32_t last_status_ms_{0};

#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *online_binary_sensor_{nullptr};
  binary_sensor::BinarySensor *dual_zone_binary_sensor_{nullptr};
#endif
#ifdef USE_SENSOR
  sensor::Sensor *current_temperature_sensor_[2]{nullptr, nullptr};
  sensor::Sensor *target_temperature_sensor_[2]{nullptr, nullptr};
  sensor::Sensor *battery_percent_sensor_{nullptr};
  sensor::Sensor *battery_voltage_sensor_{nullptr};
  sensor::Sensor *running_status_sensor_{nullptr};
#endif
  AlpicoolClimate *climate_[2]{nullptr, nullptr};
  AlpicoolSwitch *switch_[2]{nullptr, nullptr};
  AlpicoolSelect *select_[2]{nullptr, nullptr};
  AlpicoolNumber *number_[5]{nullptr, nullptr, nullptr, nullptr, nullptr};
};

}  // namespace esphome::alpicool_fridge

#endif  // USE_ESP32
