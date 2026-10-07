#include "alpicool_fridge.h"

#ifdef USE_ESP32

#include <cmath>

#include "alpicool_entities.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::alpicool_fridge {

static const char *const TAG = "alpicool_fridge";

// Usable ATT payload at the default MTU of 23. Longer frames (the dual-zone SET
// is 31 bytes) are split; some fridges silently drop oversized writes.
static const size_t MAX_WRITE_SIZE = 20;
// Pause between writes so the fridge can reassemble chunks.
static const uint32_t WRITE_INTERVAL_MS = 150;
// Delay before re-reading the status after a change.
static const uint32_t REFRESH_AFTER_WRITE_MS = 1000;

void AlpicoolFridge::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                         esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_DISCONNECT_EVT: {
      this->node_state = espbt::ClientState::IDLE;
      this->write_handle_ = 0;
      this->notify_handle_ = 0;
      this->reader_.clear();
      this->tx_queue_.clear();
      // Keep the last values until status_timeout so a short drop doesn't blank everything.
      this->set_online_(false);
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto *write_chr = this->parent_->get_characteristic(SERVICE_UUID, WRITE_CHARACTERISTIC_UUID);
      auto *notify_chr = this->parent_->get_characteristic(SERVICE_UUID, NOTIFY_CHARACTERISTIC_UUID);
      if (write_chr == nullptr || notify_chr == nullptr) {
        ESP_LOGE(TAG, "[%s] Service 0x1234 with characteristics 0x1235/0x1236 not found, not an Alpicool fridge?",
                 this->parent_->address_str());
        break;
      }
      this->write_handle_ = write_chr->handle;
      this->notify_handle_ = notify_chr->handle;
      // Some fridges drop larger unacknowledged writes even though they
      // advertise write-without-response, so prefer acknowledged writes.
      this->write_type_ = (write_chr->properties & ESP_GATT_CHAR_PROP_BIT_WRITE) ? ESP_GATT_WRITE_TYPE_RSP
                                                                                  : ESP_GATT_WRITE_TYPE_NO_RSP;
      auto status = esp_ble_gattc_register_for_notify(this->parent_->get_gattc_if(), this->parent_->get_remote_bda(),
                                                      this->notify_handle_);
      if (status) {
        ESP_LOGW(TAG, "esp_ble_gattc_register_for_notify failed, status=%d", status);
      }
      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      if (param->reg_for_notify.handle != this->notify_handle_)
        break;
      this->node_state = espbt::ClientState::ESTABLISHED;
      ESP_LOGI(TAG, "[%s] Connected", this->parent_->address_str());
      if (this->bind_on_connect_) {
        // The fridge shows "APP" and answers once its button is pressed.
        this->enqueue_(build_bind());
      }
      this->enqueue_(build_query());
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      if (param->notify.handle != this->notify_handle_)
        break;
      ESP_LOGV(TAG, "Notification: %s", format_hex_pretty(param->notify.value, param->notify.value_len).c_str());
      this->reader_.feed(param->notify.value, param->notify.value_len,
                         [this](const Frame &frame) { this->on_frame_(frame); });
      break;
    }
    default:
      break;
  }
}

void AlpicoolFridge::loop() {
  if (this->node_state != espbt::ClientState::ESTABLISHED || this->tx_queue_.empty())
    return;
  uint32_t now = millis();
  if (now - this->last_write_ms_ < WRITE_INTERVAL_MS)
    return;
  this->last_write_ms_ = now;

  auto chunk = std::move(this->tx_queue_.front());
  this->tx_queue_.pop_front();
  ESP_LOGV(TAG, "Write: %s", format_hex_pretty(chunk).c_str());
  auto status = esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(),
                                         this->write_handle_, chunk.size(), chunk.data(), this->write_type_,
                                         ESP_GATT_AUTH_REQ_NONE);
  if (status) {
    ESP_LOGW(TAG, "esp_ble_gattc_write_char failed, status=%d", status);
  }
}

void AlpicoolFridge::update() {
  // A switched-off fridge is normal, not an error: log transitions only, never per poll.
  if (this->has_status_ && millis() - this->last_status_ms_ > this->status_timeout_) {
    ESP_LOGI(TAG, "[%s] No status for %us, values now unavailable", this->parent_->address_str(),
             static_cast<unsigned>(this->status_timeout_ / 1000));
    this->has_status_ = false;
    this->publish_offline_();
  }
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    this->set_online_(false);
    return;
  }
  this->enqueue_(build_query());
}

void AlpicoolFridge::on_frame_(const Frame &frame) {
  if (frame.checksum == ChecksumState::MISMATCH) {
    // Seen on some firmwares; header and length already matched, so accept it.
    ESP_LOGV(TAG, "Checksum mismatch on command 0x%02X, accepting anyway", frame.command);
  }
  FridgeStatus status;
  switch (frame.command) {
    case CMD_QUERY:
      if (parse_status(frame.data, status)) {
        this->handle_status_(status);
      } else {
        ESP_LOGW(TAG, "Status too short: %u bytes", static_cast<unsigned>(frame.data.size()));
      }
      break;
    case CMD_SET:
      // The fridge answers SET with either an echo or its full new status.
      if (!is_set_echo(frame.data.size()) && parse_status(frame.data, status)) {
        this->handle_status_(status);
      }
      break;
    case CMD_BIND:
      ESP_LOGI(TAG, "Bind confirmed");
      break;
    case CMD_SET_LEFT_TARGET:
    case CMD_SET_RIGHT_TARGET:
      break;
    default:
      ESP_LOGD(TAG, "Unhandled command 0x%02X", frame.command);
      break;
  }
}

void AlpicoolFridge::handle_status_(const FridgeStatus &status) {
  // MQTT states are retained and resent on reconnect, so unchanged polls
  // don't need publishing; this keeps mobile data use down.
  bool changed = !this->has_status_ || !(status == this->status_);
  this->status_ = status;
  this->has_status_ = true;
  this->last_status_ms_ = millis();
  this->set_online_(true);
  if (!changed)
    return;
  ESP_LOGD(TAG, "Status: power=%d mode=%u left=%d/%d right=%s%d/%d bat=%u%% %u.%uV", status.powered_on,
           status.run_mode, status.left.current, status.left.target, status.has_right ? "" : "(none) ",
           status.right.current, status.right.target, status.battery_percent, status.battery_voltage_int,
           status.battery_voltage_dec);
  this->publish_state_();
}

float AlpicoolFridge::to_celsius(int8_t raw) const {
  if (this->status_.unit == UNIT_FAHRENHEIT)
    return (raw - 32) * 5.0f / 9.0f;
  return raw;
}

int8_t AlpicoolFridge::from_celsius(float celsius) const {
  float raw = this->status_.unit == UNIT_FAHRENHEIT ? celsius * 9.0f / 5.0f + 32.0f : celsius;
  return static_cast<int8_t>(clamp<long>(lroundf(raw), -127, 127));
}

void AlpicoolFridge::publish_state_() {
  const FridgeStatus &s = this->status_;
#ifdef USE_BINARY_SENSOR
  if (this->dual_zone_binary_sensor_ != nullptr)
    this->dual_zone_binary_sensor_->publish_state(s.has_right);
#endif
#ifdef USE_SENSOR
  const ZoneStatus *zones[2] = {&s.left, s.has_right ? &s.right : nullptr};
  for (int z = 0; z < 2; z++) {
    if (this->current_temperature_sensor_[z] != nullptr)
      this->current_temperature_sensor_[z]->publish_state(zones[z] ? this->to_celsius(zones[z]->current) : NAN);
    if (this->target_temperature_sensor_[z] != nullptr)
      this->target_temperature_sensor_[z]->publish_state(zones[z] ? this->to_celsius(zones[z]->target) : NAN);
  }
  if (this->battery_percent_sensor_ != nullptr) {
    bool known = s.battery_percent != BATTERY_PERCENT_UNKNOWN && s.battery_percent <= 100;
    this->battery_percent_sensor_->publish_state(known ? s.battery_percent : NAN);
  }
  if (this->battery_voltage_sensor_ != nullptr)
    this->battery_voltage_sensor_->publish_state(s.battery_voltage_int + s.battery_voltage_dec / 10.0f);
  if (this->running_status_sensor_ != nullptr)
    this->running_status_sensor_->publish_state(s.has_running_status ? s.running_status : NAN);
#endif
  for (int z = 0; z < 2; z++) {
    if (this->climate_[z] != nullptr)
      this->climate_[z]->update_state(z == ZONE_LEFT || s.has_right ? &s : nullptr);
  }
  if (this->switch_[SWITCH_POWER] != nullptr)
    this->switch_[SWITCH_POWER]->publish_state(s.powered_on);
  if (this->switch_[SWITCH_LOCK] != nullptr)
    this->switch_[SWITCH_LOCK]->publish_state(s.locked);

  auto publish_select = [](AlpicoolSelect *select, uint8_t index) {
    if (select == nullptr)
      return;
    if (select->has_index(index)) {
      select->publish_state(static_cast<size_t>(index));
    } else {
      ESP_LOGW(TAG, "Fridge reported option %u, but select '%s' has no such option", index,
               select->get_name().c_str());
    }
  };
  publish_select(this->select_[SELECT_RUN_MODE], s.run_mode);
  publish_select(this->select_[SELECT_BATTERY_SAVER], s.battery_saver);

  auto publish_number = [](AlpicoolNumber *number, float value) {
    if (number != nullptr)
      number->publish_state(value);
  };
  publish_number(this->number_[NUMBER_LEFT_TARGET], this->to_celsius(s.left.target));
  publish_number(this->number_[NUMBER_LEFT_HYSTERESIS], s.left.hysteresis);
  publish_number(this->number_[NUMBER_START_DELAY], s.start_delay);
  if (s.has_right) {
    publish_number(this->number_[NUMBER_RIGHT_TARGET], this->to_celsius(s.right.target));
    publish_number(this->number_[NUMBER_RIGHT_HYSTERESIS], s.right.hysteresis);
  }
}

void AlpicoolFridge::publish_offline_() {
#ifdef USE_SENSOR
  sensor::Sensor *sensors[] = {
      this->current_temperature_sensor_[0], this->current_temperature_sensor_[1], this->target_temperature_sensor_[0],
      this->target_temperature_sensor_[1],  this->battery_percent_sensor_,        this->battery_voltage_sensor_,
      this->running_status_sensor_,
  };
  for (auto *sensor : sensors) {
    if (sensor != nullptr)
      sensor->publish_state(NAN);
  }
#endif
  for (auto *climate : this->climate_) {
    if (climate != nullptr)
      climate->update_state(nullptr);
  }
  this->set_online_(false);
}

void AlpicoolFridge::set_online_(bool online) {
  bool changed = online != this->online_;
  this->online_ = online;
  if (changed)
    ESP_LOGI(TAG, "[%s] Fridge %s", this->parent_->address_str(), online ? "online" : "offline");
#ifdef USE_BINARY_SENSOR
  if (this->online_binary_sensor_ != nullptr && (changed || !this->online_binary_sensor_->has_state()))
    this->online_binary_sensor_->publish_state(online);
#endif
}

bool AlpicoolFridge::can_write_() const {
  if (this->node_state != espbt::ClientState::ESTABLISHED) {
    ESP_LOGW(TAG, "[%s] Not connected, ignoring command", this->parent_->address_str());
    return false;
  }
  if (!this->has_status_) {
    // SET always carries every setting, so it can't be built without a status.
    ESP_LOGW(TAG, "[%s] No status received yet, ignoring command", this->parent_->address_str());
    return false;
  }
  return true;
}

void AlpicoolFridge::enqueue_(const std::vector<uint8_t> &frame) {
  for (size_t i = 0; i < frame.size(); i += MAX_WRITE_SIZE) {
    size_t end = std::min(i + MAX_WRITE_SIZE, frame.size());
    this->tx_queue_.emplace_back(frame.begin() + i, frame.begin() + end);
  }
}

void AlpicoolFridge::request_status_soon_() {
  // Named, so a burst of changes results in one query.
  this->set_timeout("refresh", REFRESH_AFTER_WRITE_MS, [this]() {
    if (this->node_state == espbt::ClientState::ESTABLISHED)
      this->enqueue_(build_query());
  });
}

void AlpicoolFridge::send_settings_(const FridgeStatus &status) {
  this->enqueue_(build_set(status));
  // Optimistic: show the change right away, the next status confirms it.
  this->status_ = status;
  this->publish_state_();
  this->request_status_soon_();
}

void AlpicoolFridge::set_target_temperature(Zone zone, float celsius) {
  if (!this->can_write_())
    return;
  if (zone == ZONE_RIGHT && !this->status_.has_right) {
    ESP_LOGW(TAG, "Fridge has no second zone");
    return;
  }
  int8_t raw = this->from_celsius(celsius);
  if (this->status_.temp_min < this->status_.temp_max)
    raw = clamp(raw, this->status_.temp_min, this->status_.temp_max);
  ESP_LOGI(TAG, "Set %s target to %d", zone == ZONE_LEFT ? "left" : "right", raw);
  this->enqueue_(build_set_target(zone == ZONE_RIGHT, raw));
  (zone == ZONE_LEFT ? this->status_.left : this->status_.right).target = raw;
  this->publish_state_();
  this->request_status_soon_();
}

void AlpicoolFridge::set_power(bool on) {
  if (!this->can_write_())
    return;
  FridgeStatus status = this->status_;
  status.powered_on = on;
  this->send_settings_(status);
}

void AlpicoolFridge::set_lock(bool locked) {
  if (!this->can_write_())
    return;
  FridgeStatus status = this->status_;
  status.locked = locked;
  this->send_settings_(status);
}

void AlpicoolFridge::set_run_mode(uint8_t run_mode) {
  if (!this->can_write_())
    return;
  FridgeStatus status = this->status_;
  status.run_mode = run_mode;
  this->send_settings_(status);
}

void AlpicoolFridge::set_battery_saver(uint8_t level) {
  if (!this->can_write_())
    return;
  FridgeStatus status = this->status_;
  status.battery_saver = level;
  this->send_settings_(status);
}

void AlpicoolFridge::set_hysteresis(Zone zone, float value) {
  if (!this->can_write_())
    return;
  if (zone == ZONE_RIGHT && !this->status_.has_right) {
    ESP_LOGW(TAG, "Fridge has no second zone");
    return;
  }
  FridgeStatus status = this->status_;
  (zone == ZONE_LEFT ? status.left : status.right).hysteresis = static_cast<int8_t>(lroundf(value));
  this->send_settings_(status);
}

void AlpicoolFridge::set_start_delay(uint8_t minutes) {
  if (!this->can_write_())
    return;
  FridgeStatus status = this->status_;
  status.start_delay = minutes;
  this->send_settings_(status);
}

void AlpicoolFridge::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Alpicool fridge:\n"
                "  Bind on connect: %s\n"
                "  Status timeout: %us",
                YESNO(this->bind_on_connect_), static_cast<unsigned>(this->status_timeout_ / 1000));
  LOG_UPDATE_INTERVAL(this);
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Online", this->online_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Dual zone", this->dual_zone_binary_sensor_);
#endif
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Left current temperature", this->current_temperature_sensor_[ZONE_LEFT]);
  LOG_SENSOR("  ", "Left target temperature", this->target_temperature_sensor_[ZONE_LEFT]);
  LOG_SENSOR("  ", "Right current temperature", this->current_temperature_sensor_[ZONE_RIGHT]);
  LOG_SENSOR("  ", "Right target temperature", this->target_temperature_sensor_[ZONE_RIGHT]);
  LOG_SENSOR("  ", "Battery percent", this->battery_percent_sensor_);
  LOG_SENSOR("  ", "Battery voltage", this->battery_voltage_sensor_);
  LOG_SENSOR("  ", "Running status", this->running_status_sensor_);
#endif
}

// --- Entities -------------------------------------------------------------

climate::ClimateTraits AlpicoolClimate::traits() {
  climate::ClimateTraits traits;
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.add_supported_mode(climate::CLIMATE_MODE_OFF);
  traits.add_supported_mode(climate::CLIMATE_MODE_COOL);
  // Max/Eco. Custom presets are mid-deprecation in ESPHome, so use the standard ones.
  traits.add_supported_preset(climate::CLIMATE_PRESET_BOOST);
  traits.add_supported_preset(climate::CLIMATE_PRESET_ECO);
  // Defaults; overridable with `visual:` in YAML.
  traits.set_visual_min_temperature(-20);
  traits.set_visual_max_temperature(20);
  traits.set_visual_target_temperature_step(1);
  traits.set_visual_current_temperature_step(1);
  return traits;
}

void AlpicoolClimate::control(const climate::ClimateCall &call) {
  if (call.get_target_temperature().has_value())
    this->parent_->set_target_temperature(this->zone_, *call.get_target_temperature());
  if (call.get_preset().has_value())
    this->parent_->set_run_mode(*call.get_preset() == climate::CLIMATE_PRESET_ECO ? 1 : 0);
  if (call.get_mode().has_value())
    this->parent_->set_power(*call.get_mode() != climate::CLIMATE_MODE_OFF);
}

void AlpicoolClimate::update_state(const FridgeStatus *status) {
  if (status == nullptr) {
    this->current_temperature = NAN;
    this->target_temperature = NAN;
  } else {
    const ZoneStatus &zone = this->zone_ == ZONE_LEFT ? status->left : status->right;
    this->current_temperature = this->parent_->to_celsius(zone.current);
    this->target_temperature = this->parent_->to_celsius(zone.target);
    this->mode = status->powered_on ? climate::CLIMATE_MODE_COOL : climate::CLIMATE_MODE_OFF;
    this->set_preset_(status->run_mode == 1 ? climate::CLIMATE_PRESET_ECO : climate::CLIMATE_PRESET_BOOST);
  }
  this->publish_state();
}

void AlpicoolClimate::dump_config() { LOG_CLIMATE("", "Alpicool climate", this); }

void AlpicoolSwitch::write_state(bool state) {
  if (this->type_ == SWITCH_POWER) {
    this->parent_->set_power(state);
  } else {
    this->parent_->set_lock(state);
  }
}

void AlpicoolSwitch::dump_config() { LOG_SWITCH("", "Alpicool switch", this); }

void AlpicoolSelect::control(size_t index) {
  if (this->type_ == SELECT_RUN_MODE) {
    this->parent_->set_run_mode(index);
  } else {
    this->parent_->set_battery_saver(index);
  }
}

void AlpicoolSelect::dump_config() { LOG_SELECT("", "Alpicool select", this); }

void AlpicoolNumber::control(float value) {
  switch (this->type_) {
    case NUMBER_LEFT_TARGET:
      this->parent_->set_target_temperature(ZONE_LEFT, value);
      break;
    case NUMBER_RIGHT_TARGET:
      this->parent_->set_target_temperature(ZONE_RIGHT, value);
      break;
    case NUMBER_LEFT_HYSTERESIS:
      this->parent_->set_hysteresis(ZONE_LEFT, value);
      break;
    case NUMBER_RIGHT_HYSTERESIS:
      this->parent_->set_hysteresis(ZONE_RIGHT, value);
      break;
    case NUMBER_START_DELAY:
      this->parent_->set_start_delay(static_cast<uint8_t>(lroundf(value)));
      break;
  }
}

void AlpicoolNumber::dump_config() { LOG_NUMBER("", "Alpicool number", this); }

}  // namespace esphome::alpicool_fridge

#endif  // USE_ESP32
