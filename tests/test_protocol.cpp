// Host tests for components/alpicool_fridge/protocol.h.
//
// Build and run with any C++20 compiler, e.g.:
//   g++ -std=c++20 -I components tests/test_protocol.cpp -o test_protocol && ./test_protocol
//
// Captures marked BM come from BrassMonkeyFridgeMonitor's README, NE from
// neftaly/esphome-alpicool, MAENTUM from Gruni22/alpicool_ha_ble's tests.

#include <cstdio>
#include <string>

#include "alpicool_fridge/protocol.h"

using namespace esphome::alpicool_fridge;

static int failures = 0;

#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      failures++; \
    } \
  } while (0)

static std::vector<uint8_t> hex(const std::string &s) {
  std::vector<uint8_t> out;
  std::string digits;
  for (char c : s) {
    if (c != ' ')
      digits += c;
  }
  for (size_t i = 0; i + 1 < digits.size(); i += 2)
    out.push_back(static_cast<uint8_t>(std::stoul(digits.substr(i, 2), nullptr, 16)));
  return out;
}

static std::vector<Frame> feed_all(FrameReader &reader, const std::vector<uint8_t> &bytes) {
  std::vector<Frame> frames;
  reader.feed(bytes.data(), bytes.size(), [&frames](const Frame &f) { frames.push_back(f); });
  return frames;
}

static std::vector<Frame> read_frames(const std::vector<uint8_t> &bytes) {
  FrameReader reader;
  return feed_all(reader, bytes);
}

// BM: on, Max, target -15, range -20..20, current -13, 100 %, 12.3 V
static const char *BM_STATUS_1 = "fe fe 15 01 00 01 00 00 f1 14 ec 02 00 00 00 00 00 00 f3 64 0c 03 05 6c";
// BM: status right before the SET capture below
static const char *BM_STATUS_2 = "fe fe 15 01 00 01 00 00 ec 14 ec 02 00 00 00 00 00 00 f7 7f 0b 01 05 83";

static void test_build_fixed_frames() {
  CHECK(build_query() == hex("fefe03010200"));  // BM, NE
  CHECK(build_bind() == hex("fefe030001ff"));   // BM
}

static void test_build_set_target() {
  CHECK(build_set_target(false, -18) == hex("fefe0405ee02f3"));  // NE
  // BM logs "fe fe 03 05 ec 02 f1"; the checksum only fits length 04.
  CHECK(build_set_target(false, -20) == hex("fefe0405ec02f1"));
  CHECK(build_set_target(true, 4)[3] == CMD_SET_RIGHT_TARGET);
}

static void test_parse_bm_status() {
  auto frames = read_frames(hex(BM_STATUS_1));
  CHECK(frames.size() == 1);
  if (frames.size() != 1)
    return;
  CHECK(frames[0].command == CMD_QUERY);
  CHECK(frames[0].checksum == ChecksumState::VALID);
  CHECK(frames[0].data.size() == STATUS_LEN_SINGLE_ZONE);
  FridgeStatus s;
  CHECK(parse_status(frames[0].data, s));
  CHECK(!s.locked);
  CHECK(s.powered_on);
  CHECK(s.run_mode == 0);
  CHECK(s.left.target == -15);
  CHECK(s.temp_max == 20);
  CHECK(s.temp_min == -20);
  CHECK(s.left.hysteresis == 2);
  CHECK(s.unit == UNIT_CELSIUS);
  CHECK(s.left.current == -13);
  CHECK(s.battery_percent == 100);
  CHECK(s.battery_voltage_int == 12 && s.battery_voltage_dec == 3);
  CHECK(!s.has_right);
  CHECK(!s.has_running_status);
}

static void test_battery_unknown() {
  auto frames = read_frames(hex(BM_STATUS_2));
  FridgeStatus s;
  CHECK(frames.size() == 1 && parse_status(frames[0].data, s));
  CHECK(s.battery_percent == BATTERY_PERCENT_UNKNOWN);
  CHECK(s.left.current == -9);
}

static void test_build_set_matches_app_capture() {
  // BM: the app changes battery protection to high from BM_STATUS_2.
  auto frames = read_frames(hex(BM_STATUS_2));
  FridgeStatus s;
  CHECK(frames.size() == 1 && parse_status(frames[0].data, s));
  s.battery_saver = 2;
  CHECK(build_set(s) == hex("fe fe 11 02 00 01 00 02 ec 14 ec 02 00 00 00 00 00 00 04 00"));
}

static void test_set_answer_is_status() {
  // BM: the fridge answers that SET with its full new status.
  auto frames = read_frames(hex("fe fe 15 02 00 01 00 02 ec 14 ec 02 00 00 00 00 00 00 f7 7f 0b 01 05 86"));
  CHECK(frames.size() == 1);
  if (frames.size() != 1)
    return;
  CHECK(frames[0].command == CMD_SET);
  CHECK(!is_set_echo(frames[0].data.size()));
  FridgeStatus s;
  CHECK(parse_status(frames[0].data, s));
  CHECK(s.battery_saver == 2);
}

static void test_set_echo_is_not_status() {
  auto frames = read_frames(hex("fe fe 11 02 00 01 00 02 ec 14 ec 02 00 00 00 00 00 00 04 00"));
  CHECK(frames.size() == 1 && is_set_echo(frames[0].data.size()));
}

static void test_bind_response() {
  auto frames = read_frames(hex("fefe0400010201"));  // BM
  CHECK(frames.size() == 1);
  CHECK(frames.size() == 1 && frames[0].command == CMD_BIND && frames[0].checksum == ChecksumState::VALID);
}

static void test_fragmented_notifications() {
  // At the default MTU a status arrives as 20 + 4 bytes.
  auto bytes = hex(BM_STATUS_1);
  FrameReader reader;
  CHECK(feed_all(reader, std::vector<uint8_t>(bytes.begin(), bytes.begin() + 20)).empty());
  auto frames = feed_all(reader, std::vector<uint8_t>(bytes.begin() + 20, bytes.end()));
  CHECK(frames.size() == 1 && frames[0].command == CMD_QUERY);
}

static void test_split_header() {
  // A notification ending in the first header byte.
  auto bytes = hex(BM_STATUS_1);
  FrameReader reader;
  CHECK(feed_all(reader, {0xFE}).empty());
  auto frames = feed_all(reader, std::vector<uint8_t>(bytes.begin() + 1, bytes.end()));
  CHECK(frames.size() == 1);
}

static void test_two_frames_in_one_notification() {
  auto bytes = hex("fe fe 04 05 ee 02 f3");  // target echo
  auto status = hex(BM_STATUS_1);
  bytes.insert(bytes.end(), status.begin(), status.end());
  auto frames = read_frames(bytes);
  CHECK(frames.size() == 2);
  CHECK(frames.size() == 2 && frames[0].command == CMD_SET_LEFT_TARGET && frames[1].command == CMD_QUERY);
}

static void test_resync_after_garbage() {
  auto bytes = hex("00 12 fe 34 fe fe fe");
  auto status = hex(BM_STATUS_1);
  bytes.insert(bytes.end(), status.begin(), status.end());
  auto frames = read_frames(bytes);
  CHECK(frames.size() == 1 && frames[0].command == CMD_QUERY);
}

static void test_doubled_and_bad_checksums() {
  auto bytes = hex(BM_STATUS_1);
  uint16_t doubled = checksum(bytes.data(), bytes.size() - 2) * 2;
  bytes[bytes.size() - 2] = doubled >> 8;
  bytes[bytes.size() - 1] = doubled & 0xFF;
  auto frames = read_frames(bytes);
  CHECK(frames.size() == 1 && frames[0].checksum == ChecksumState::DOUBLED);

  bytes[bytes.size() - 1] ^= 0x55;
  frames = read_frames(bytes);
  CHECK(frames.size() == 1 && frames[0].checksum == ChecksumState::MISMATCH);
}

static void test_maentum_long_single_zone_status() {
  // MAENTUM IceCubeX: 42 data bytes in three notifications, 0x80 for the missing zone.
  FrameReader reader;
  std::vector<Frame> frames;
  for (const char *chunk : {"FEFE2D01000101020414EC020000FDFD00000564", "0E06000000000000000080000A00000000000000",
                            "0000000000000635"}) {
    auto got = feed_all(reader, hex(chunk));
    frames.insert(frames.end(), got.begin(), got.end());
  }
  CHECK(frames.size() == 1);
  if (frames.size() != 1)
    return;
  CHECK(frames[0].data.size() == 42);
  FridgeStatus s;
  CHECK(parse_status(frames[0].data, s));
  CHECK(!s.has_right);
  CHECK(s.has_running_status);
  CHECK(s.left.target == 4);
  CHECK(s.temp_max == 20);
  CHECK(s.temp_min == -20);
  // A single-zone SET stays 14 data bytes even though the status was long.
  CHECK(build_set(s).size() == 4 + 14 + 2);
}

static std::vector<uint8_t> dual_zone_status_frame() {
  // Synthetic: BM_STATUS_1 plus a second zone with target 4, current 6.
  auto data = read_frames(hex(BM_STATUS_1))[0].data;
  for (uint8_t b : {0x04, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x06, 0x01})
    data.push_back(b);
  return build_frame(CMD_QUERY, data);
}

static void test_dual_zone() {
  auto frame_bytes = dual_zone_status_frame();
  CHECK(frame_bytes.size() > 20);  // arrives fragmented in practice
  FrameReader reader;
  auto frames = feed_all(reader, std::vector<uint8_t>(frame_bytes.begin(), frame_bytes.begin() + 20));
  auto rest = feed_all(reader, std::vector<uint8_t>(frame_bytes.begin() + 20, frame_bytes.end()));
  frames.insert(frames.end(), rest.begin(), rest.end());
  CHECK(frames.size() == 1);
  if (frames.size() != 1)
    return;
  FridgeStatus s;
  CHECK(parse_status(frames[0].data, s));
  CHECK(s.has_right);
  CHECK(s.right.target == 4);
  CHECK(s.right.hysteresis == 1);
  CHECK(s.right.current == 6);
  CHECK(s.running_status == 1);
  CHECK(s.left.current == -13);

  s.powered_on = false;
  auto set = build_set(s);
  CHECK(set.size() == 4 + 25 + 2);
  CHECK(set[2] == 25 + 3);
  CHECK(set[4 + 1] == 0);   // powered_on
  CHECK(set[4 + 14] == 4);  // right target
  CHECK(set[4 + 15] == 0 && set[4 + 16] == 0);
  CHECK(set[4 + 17] == 1);  // right hysteresis
  CHECK(set[4 + 22] == 0 && set[4 + 23] == 0 && set[4 + 24] == 0);
  // The SET echo of a dual-zone fridge must not be mistaken for a status.
  auto echo = read_frames(set);
  CHECK(echo.size() == 1 && is_set_echo(echo[0].data.size()));
}

static void test_status_equality() {
  FridgeStatus a, b;
  CHECK(parse_status(read_frames(hex(BM_STATUS_1))[0].data, a));
  CHECK(parse_status(read_frames(hex(BM_STATUS_1))[0].data, b));
  CHECK(a == b);
  b.right.target = 3;
  CHECK(!(a == b));
}

static void test_real_dual_zone_capture() {
  // A1- dual-zone fridge on an ESP32-C6, 2026-10-07: on, mode 1, left 1/4 �C,
  // right 4/4 �C, 100 %, 13.3 V. 30 data bytes; bytes 19/20 are zero as BM documents.
  auto frames = read_frames(hex("FE FE 21 01 00 01 01 00 04 14 EC 02 00 00 FD FD FD 00 01 64 0D 03 04 00 00 02 FD FD FD "
                                "00 04 00 00 00 13 26"));
  CHECK(frames.size() == 1);
  if (frames.size() != 1)
    return;
  // This firmware sends the doubled checksum (0x0993 * 2 = 0x1326).
  CHECK(frames[0].checksum == ChecksumState::DOUBLED);
  CHECK(frames[0].data.size() == 30);
  FridgeStatus s;
  CHECK(parse_status(frames[0].data, s));
  CHECK(s.powered_on && !s.locked);
  CHECK(s.run_mode == 1 && s.battery_saver == 0);
  CHECK(s.left.target == 4 && s.left.current == 1 && s.left.hysteresis == 2);
  CHECK(s.left.tc_hot == -3 && s.left.tc_halt == 0);
  CHECK(s.has_right);
  CHECK(s.right.target == 4 && s.right.current == 4 && s.right.hysteresis == 2);
  CHECK(s.right.tc_cold == -3 && s.right.tc_halt == 0);
  CHECK(s.battery_percent == 100 && s.battery_voltage_int == 13 && s.battery_voltage_dec == 3);
  // The SET built from it carries the right zone's settings back unchanged.
  auto set = build_set(s);
  CHECK(set.size() == 4 + 25 + 2);
  // Zone 1 block: identical to the first 14 status bytes.
  CHECK(std::vector<uint8_t>(set.begin() + 4, set.begin() + 4 + 14) ==
        std::vector<uint8_t>(frames[0].data.begin(), frames[0].data.begin() + 14));
  // Zone 2 block: target, 0, 0, hysteresis, corrections, 0, 0, 0.
  CHECK(std::vector<uint8_t>(set.begin() + 4 + 14, set.end() - 2) ==
        hex("04 00 00 02 FD FD FD 00 00 00 00"));
}

static void test_short_status_rejected() {
  FridgeStatus s;
  CHECK(!parse_status(std::vector<uint8_t>(17, 0), s));
}

int main() {
  test_build_fixed_frames();
  test_build_set_target();
  test_parse_bm_status();
  test_battery_unknown();
  test_build_set_matches_app_capture();
  test_set_answer_is_status();
  test_set_echo_is_not_status();
  test_bind_response();
  test_fragmented_notifications();
  test_split_header();
  test_two_frames_in_one_notification();
  test_resync_after_garbage();
  test_doubled_and_bad_checksums();
  test_maentum_long_single_zone_status();
  test_dual_zone();
  test_status_equality();
  test_real_dual_zone_capture();
  test_short_status_rejected();
  if (failures == 0)
    std::printf("All protocol tests passed\n");
  return failures == 0 ? 0 : 1;
}
