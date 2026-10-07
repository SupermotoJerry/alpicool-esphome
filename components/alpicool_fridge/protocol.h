#pragma once

// Alpicool BLE fridge protocol: framing, status decoding and command building.
//
// Kept free of ESPHome dependencies so it can be unit tested on a host.
//
// Sources (see README.md for details):
// - klightspeed/BrassMonkeyFridgeMonitor (MIT): GATT layout, frame format, status/SET layout
// - Gruni22/alpicool_ha_ble (MIT): chunked writes, SET echo vs. status detection,
//   0x80 "no second zone" sentinel, 0x7F "battery unknown"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome::alpicool_fridge {

static const uint8_t CMD_BIND = 0x00;
static const uint8_t CMD_QUERY = 0x01;
static const uint8_t CMD_SET = 0x02;
static const uint8_t CMD_RESET = 0x04;
static const uint8_t CMD_SET_LEFT_TARGET = 0x05;
static const uint8_t CMD_SET_RIGHT_TARGET = 0x06;

static const uint8_t FRAME_HEADER = 0xFE;
// Sanity limit for the length byte. The longest documented frame is a 42 byte
// status (MAENTUM IceCubeX); anything above this is treated as a stray 0xFE.
static const uint8_t MAX_FRAME_LENGTH = 64;

static const size_t STATUS_LEN_SINGLE_ZONE = 18;
static const size_t STATUS_LEN_DUAL_ZONE = 28;
// SET carries 14 data bytes (single zone) or 25 (dual zone). The fridge may echo
// them back, with or without counting the checksum, so these sizes are never a status.
static const size_t SET_ECHO_SIZES[] = {12, 14, 23, 25};

static const uint8_t BATTERY_PERCENT_UNKNOWN = 0x7F;
static const int8_t NO_ZONE_TEMPERATURE = -128;

static const uint8_t UNIT_CELSIUS = 0;
static const uint8_t UNIT_FAHRENHEIT = 1;

struct ZoneStatus {
  int8_t target{0};
  int8_t hysteresis{1};
  int8_t tc_hot{0};
  int8_t tc_mid{0};
  int8_t tc_cold{0};
  int8_t tc_halt{0};
  int8_t current{0};

  bool operator==(const ZoneStatus &) const = default;
};

struct FridgeStatus {
  bool locked{false};
  bool powered_on{false};
  uint8_t run_mode{0};
  uint8_t battery_saver{0};
  int8_t temp_max{20};
  int8_t temp_min{-20};
  uint8_t start_delay{0};
  uint8_t unit{UNIT_CELSIUS};
  uint8_t battery_percent{BATTERY_PERCENT_UNKNOWN};
  uint8_t battery_voltage_int{0};
  uint8_t battery_voltage_dec{0};
  ZoneStatus left;
  bool has_right{false};
  ZoneStatus right;
  bool has_running_status{false};
  uint8_t running_status{0};

  bool operator==(const FridgeStatus &) const = default;
};

enum class ChecksumState : uint8_t { VALID, DOUBLED, MISMATCH };

struct Frame {
  uint8_t command;
  std::vector<uint8_t> data;  // bytes between the command byte and the checksum
  ChecksumState checksum;
};

inline uint16_t checksum(const uint8_t *data, size_t len) {
  uint16_t sum = 0;
  for (size_t i = 0; i < len; i++)
    sum += data[i];
  return sum;
}

// FE FE <len> <cmd> <data...> <sum_hi> <sum_lo>; len counts cmd + data + checksum.
inline std::vector<uint8_t> build_frame(uint8_t command, const std::vector<uint8_t> &data = {}) {
  std::vector<uint8_t> frame{FRAME_HEADER, FRAME_HEADER, static_cast<uint8_t>(data.size() + 3), command};
  frame.insert(frame.end(), data.begin(), data.end());
  uint16_t sum = checksum(frame.data(), frame.size());
  frame.push_back(sum >> 8);
  frame.push_back(sum & 0xFF);
  return frame;
}

// fe fe 03 00 01 ff / fe fe 03 01 02 00: no data, the trailing bytes are the checksum.
inline std::vector<uint8_t> build_bind() { return build_frame(CMD_BIND); }
inline std::vector<uint8_t> build_query() { return build_frame(CMD_QUERY); }

inline std::vector<uint8_t> build_set_target(bool right, int8_t target) {
  return build_frame(right ? CMD_SET_RIGHT_TARGET : CMD_SET_LEFT_TARGET, {static_cast<uint8_t>(target)});
}

// SET always carries every setting; unchanged ones come from the last status.
inline std::vector<uint8_t> build_set(const FridgeStatus &s) {
  std::vector<uint8_t> data{
      static_cast<uint8_t>(s.locked),
      static_cast<uint8_t>(s.powered_on),
      s.run_mode,
      s.battery_saver,
      static_cast<uint8_t>(s.left.target),
      static_cast<uint8_t>(s.temp_max),
      static_cast<uint8_t>(s.temp_min),
      static_cast<uint8_t>(s.left.hysteresis),
      s.start_delay,
      s.unit,
      static_cast<uint8_t>(s.left.tc_hot),
      static_cast<uint8_t>(s.left.tc_mid),
      static_cast<uint8_t>(s.left.tc_cold),
      static_cast<uint8_t>(s.left.tc_halt),
  };
  if (s.has_right) {
    // BrassMonkey documents bytes 1, 2 and 8-10 of this block as always zero.
    const uint8_t right[] = {
        static_cast<uint8_t>(s.right.target),
        0,
        0,
        static_cast<uint8_t>(s.right.hysteresis),
        static_cast<uint8_t>(s.right.tc_hot),
        static_cast<uint8_t>(s.right.tc_mid),
        static_cast<uint8_t>(s.right.tc_cold),
        static_cast<uint8_t>(s.right.tc_halt),
        0,
        0,
        0,
    };
    data.insert(data.end(), right, right + sizeof(right));
  }
  return build_frame(CMD_SET, data);
}

inline bool is_set_echo(size_t data_len) {
  for (size_t len : SET_ECHO_SIZES) {
    if (data_len == len)
      return true;
  }
  return false;
}

// Decodes the data of a Query (or SET) response. Returns false if it is too short.
inline bool parse_status(const std::vector<uint8_t> &p, FridgeStatus &out) {
  if (p.size() < STATUS_LEN_SINGLE_ZONE)
    return false;
  auto s8 = [&p](size_t i) { return static_cast<int8_t>(p[i]); };
  FridgeStatus s;
  s.locked = p[0] != 0;
  s.powered_on = p[1] != 0;
  s.run_mode = p[2];
  s.battery_saver = p[3];
  s.left.target = s8(4);
  s.temp_max = s8(5);
  s.temp_min = s8(6);
  s.left.hysteresis = s8(7);
  s.start_delay = p[8];
  s.unit = p[9];
  s.left.tc_hot = s8(10);
  s.left.tc_mid = s8(11);
  s.left.tc_cold = s8(12);
  s.left.tc_halt = s8(13);
  s.left.current = s8(14);
  s.battery_percent = p[15];
  s.battery_voltage_int = p[16];
  s.battery_voltage_dec = p[17];
  if (p.size() >= STATUS_LEN_DUAL_ZONE) {
    s.has_running_status = true;
    s.running_status = p[27];
    // A single-zone MAENTUM IceCubeX sends a long status with 0x80 as the
    // temperature of the zone it does not have.
    s.has_right = s8(26) != NO_ZONE_TEMPERATURE;
    if (s.has_right) {
      s.right.target = s8(18);
      s.right.hysteresis = s8(21);
      s.right.tc_hot = s8(22);
      s.right.tc_mid = s8(23);
      s.right.tc_cold = s8(24);
      s.right.tc_halt = s8(25);
      s.right.current = s8(26);
    }
  }
  out = s;
  return true;
}

// Reassembles notifications into frames. Responses longer than the default MTU
// arrive split across notifications, and one notification may hold two frames
// (e.g. a SET echo followed by the new status).
class FrameReader {
 public:
  template<typename Callback> void feed(const uint8_t *data, size_t len, Callback &&on_frame) {
    this->buffer_.insert(this->buffer_.end(), data, data + len);
    if (this->buffer_.size() > 4 * MAX_FRAME_LENGTH) {
      // Garbage that never forms a frame; don't grow without bound.
      this->buffer_.clear();
      return;
    }
    while (true) {
      size_t start = 0;
      while (start + 1 < this->buffer_.size() &&
             !(this->buffer_[start] == FRAME_HEADER && this->buffer_[start + 1] == FRAME_HEADER))
        start++;
      if (start + 1 >= this->buffer_.size()) {
        // Keep a trailing 0xFE, it may be the first half of the next header.
        bool keep_last = !this->buffer_.empty() && this->buffer_.back() == FRAME_HEADER;
        this->buffer_.erase(this->buffer_.begin(), this->buffer_.end() - (keep_last ? 1 : 0));
        return;
      }
      this->buffer_.erase(this->buffer_.begin(), this->buffer_.begin() + start);
      if (this->buffer_.size() < 3)
        return;
      uint8_t length = this->buffer_[2];
      if (length < 3 || length > MAX_FRAME_LENGTH) {
        // Not a real header (e.g. FE FE FE ...): skip one byte and resync.
        this->buffer_.erase(this->buffer_.begin());
        continue;
      }
      size_t total = 3 + length;
      if (this->buffer_.size() < total)
        return;

      Frame frame;
      frame.command = this->buffer_[3];
      frame.data.assign(this->buffer_.begin() + 4, this->buffer_.begin() + total - 2);
      uint16_t received = (this->buffer_[total - 2] << 8) | this->buffer_[total - 1];
      uint16_t expected = checksum(this->buffer_.data(), total - 2);
      if (received == expected) {
        frame.checksum = ChecksumState::VALID;
      } else if (received == static_cast<uint16_t>(expected * 2)) {
        frame.checksum = ChecksumState::DOUBLED;
      } else {
        frame.checksum = ChecksumState::MISMATCH;
      }
      this->buffer_.erase(this->buffer_.begin(), this->buffer_.begin() + total);
      on_frame(frame);
    }
  }

  void clear() { this->buffer_.clear(); }

 protected:
  std::vector<uint8_t> buffer_;
};

}  // namespace esphome::alpicool_fridge
