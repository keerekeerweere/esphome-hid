#pragma once

#include "keyboard_engine.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace esphome::hid_keyboard {

// Local addition (not upstream): relative boot-protocol mouse. Pure
// scheduling logic, main-loop only, like KeyboardEngine, so it is host-testable.
struct MouseReport {
  uint8_t buttons{0};
  int8_t dx{0}, dy{0}, wheel{0};
  uint16_t delay_ms{0};  // Minimum wait after this report before the next one.
};

inline constexpr uint8_t MOUSE_LEFT = 1, MOUSE_RIGHT = 2, MOUSE_MIDDLE = 4, MOUSE_BACK = 8, MOUSE_FORWARD = 16;
inline constexpr uint8_t MOUSE_ALL_BUTTONS = 0x1f;

// Case-insensitive: LEFT, RIGHT, MIDDLE, BACK, FORWARD.
bool mouse_button(std::string_view name, uint8_t &mask);

class MouseEngine {
 public:
  static constexpr size_t CAPACITY = 64;
  static constexpr int MAX_DISTANCE = 8000;  // Per axis, per request; also bounded by CAPACITY.

  void configure(uint16_t click_ms, uint16_t gap_ms) {
    click_ms_ = click_ms;
    gap_ms_ = gap_ms;
  }
  void set_available(bool available) {
    if (available_ && !available) {
      available_ = false;  // Transport is gone: nothing to release.
      reset();
    }
    available_ = available;
  }
  // Moves are split into <=127 steps. The request is accepted whole or not at all.
  Result move(int dx, int dy, int wheel);
  Result press(uint8_t buttons);
  Result release(uint8_t buttons);
  Result click(uint8_t buttons);
  // Drop queued input and send a release-all report (if available).
  void cancel();
  // Transport reset/disconnect: drop queued input; release only if a button was held.
  void reset();
  // Pops the next report if its predecessor's delay has elapsed.
  bool next_report(uint32_t now, MouseReport &report);
  bool busy() const { return count_ != 0; }
  uint8_t buttons() const { return buttons_; }
  Result last_result() const { return last_result_; }

 protected:
  Result result_(Result result) { return last_result_ = result; }
  size_t free_() const { return CAPACITY - count_; }
  void push_(const MouseReport &report);
  std::array<MouseReport, CAPACITY> queue_{};
  size_t head_{0}, count_{0};
  uint16_t click_ms_{30}, gap_ms_{8};
  uint32_t sent_at_{0};
  uint16_t sent_delay_{0};
  uint8_t buttons_{0};
  bool available_{false};
  Result last_result_{Result::ACCEPTED};
};

}  // namespace esphome::hid_keyboard
