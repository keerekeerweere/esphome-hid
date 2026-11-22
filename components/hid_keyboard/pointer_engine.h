#pragma once

#include "keyboard_engine.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::hid_keyboard {

// Local addition (not upstream): absolute pointer, VNC PointerEvent style. Each
// event carries the full state (position + button mask), so the engine only has
// to preserve button transitions; pure position updates coalesce to the latest.
struct PointerReport {
  uint8_t buttons{0};
  uint16_t x{0}, y{0};
  int8_t wheel{0};  // One-shot: only the report that carries it scrolls.
  bool operator==(const PointerReport &o) const {
    return buttons == o.buttons && x == o.x && y == o.y && wheel == o.wheel;
  }
};

inline constexpr uint16_t POINTER_MAX = 0x7fff;

class PointerEngine {
 public:
  static constexpr size_t CAPACITY = 32;
  void set_available(bool available) {
    if (available_ && !available) {
      available_ = false;  // Transport is gone: nothing to release.
      reset();
    }
    available_ = available;
  }
  // x, y in 0..32767 across the whole screen; buttons bit0..4 (left, right, middle,
  // back, forward); wheel -127..127 notches (positive up).
  Result event(int x, int y, int buttons, int wheel);
  // Drop queued input; send a release-all report at the last position.
  void cancel();
  // Transport reset: drop queued input; release only if a button was held.
  void reset();
  bool next_report(PointerReport &report);
  bool busy() const { return count_ != 0; }
  Result last_result() const { return last_result_; }

 protected:
  Result result_(Result result) { return last_result_ = result; }
  std::array<PointerReport, CAPACITY> queue_{};
  size_t head_{0}, count_{0};
  PointerReport last_{};  // Latest logical state accepted.
  bool available_{false};
  Result last_result_{Result::ACCEPTED};
};

}  // namespace esphome::hid_keyboard
