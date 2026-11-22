#include "mouse_engine.h"
#include <algorithm>
#include <cstdlib>

namespace esphome::hid_keyboard {

bool mouse_button(std::string_view name, uint8_t &mask) {
  struct Entry {
    std::string_view name;
    uint8_t mask;
  };
  static constexpr Entry entries[] = {{"LEFT", MOUSE_LEFT},     {"RIGHT", MOUSE_RIGHT}, {"MIDDLE", MOUSE_MIDDLE},
                                      {"BACK", MOUSE_BACK},     {"FORWARD", MOUSE_FORWARD}};
  if (name.size() > 7) return false;
  char upper[8]{};
  for (size_t i = 0; i < name.size(); i++) upper[i] = name[i] >= 'a' && name[i] <= 'z' ? name[i] - ('a' - 'A') : name[i];
  const std::string_view normalized(upper, name.size());
  for (const auto &entry : entries) {
    if (entry.name == normalized) {
      mask = entry.mask;
      return true;
    }
  }
  return false;
}

void MouseEngine::push_(const MouseReport &report) {
  queue_[(head_ + count_) % CAPACITY] = report;
  count_++;
}

static size_t steps(int value) {
  return static_cast<size_t>((std::abs(value) + 126) / 127);
}

Result MouseEngine::move(int dx, int dy, int wheel) {
  if (!available_) return result_(Result::UNAVAILABLE);
  if (std::abs(dx) > MAX_DISTANCE || std::abs(dy) > MAX_DISTANCE || std::abs(wheel) > MAX_DISTANCE)
    return result_(Result::INVALID_INPUT);
  const size_t needed = std::max({steps(dx), steps(dy), steps(wheel)});
  if (needed == 0) return result_(Result::ACCEPTED);
  if (needed > free_()) return result_(Result::QUEUE_FULL);
  while (dx != 0 || dy != 0 || wheel != 0) {
    MouseReport report;
    report.buttons = buttons_;
    report.dx = static_cast<int8_t>(std::clamp(dx, -127, 127));
    report.dy = static_cast<int8_t>(std::clamp(dy, -127, 127));
    report.wheel = static_cast<int8_t>(std::clamp(wheel, -127, 127));
    report.delay_ms = gap_ms_;
    dx -= report.dx;
    dy -= report.dy;
    wheel -= report.wheel;
    push_(report);
  }
  return result_(Result::ACCEPTED);
}

Result MouseEngine::press(uint8_t buttons) {
  if (!available_) return result_(Result::UNAVAILABLE);
  if (buttons == 0 || (buttons & ~MOUSE_ALL_BUTTONS)) return result_(Result::INVALID_INPUT);
  if (free_() < 1) return result_(Result::QUEUE_FULL);
  buttons_ |= buttons;
  MouseReport report;
  report.buttons = buttons_;
  report.delay_ms = gap_ms_;
  push_(report);
  return result_(Result::ACCEPTED);
}

Result MouseEngine::release(uint8_t buttons) {
  if (!available_) return result_(Result::UNAVAILABLE);
  if (buttons == 0 || (buttons & ~MOUSE_ALL_BUTTONS)) return result_(Result::INVALID_INPUT);
  if (free_() < 1) return result_(Result::QUEUE_FULL);
  buttons_ &= ~buttons;
  MouseReport report;
  report.buttons = buttons_;
  report.delay_ms = gap_ms_;
  push_(report);
  return result_(Result::ACCEPTED);
}

Result MouseEngine::click(uint8_t buttons) {
  if (!available_) return result_(Result::UNAVAILABLE);
  if (buttons == 0 || (buttons & ~MOUSE_ALL_BUTTONS)) return result_(Result::INVALID_INPUT);
  if (free_() < 2) return result_(Result::QUEUE_FULL);
  MouseReport down;
  down.buttons = buttons_ | buttons;
  down.delay_ms = click_ms_;  // Hold long enough for hosts to register the press.
  push_(down);
  buttons_ &= ~buttons;
  MouseReport up;
  up.buttons = buttons_;
  up.delay_ms = gap_ms_;
  push_(up);
  return result_(Result::ACCEPTED);
}

void MouseEngine::cancel() {
  count_ = 0;
  buttons_ = 0;
  sent_delay_ = 0;
  if (available_) push_({});
  result_(Result::CANCELLED);
}

void MouseEngine::reset() {
  const bool held = buttons_ != 0;
  count_ = 0;
  buttons_ = 0;
  sent_delay_ = 0;
  if (held && available_) push_({});
}

bool MouseEngine::next_report(uint32_t now, MouseReport &report) {
  if (!available_ || count_ == 0) return false;
  if (static_cast<uint32_t>(now - sent_at_) < sent_delay_) return false;
  report = queue_[head_];
  head_ = (head_ + 1) % CAPACITY;
  count_--;
  sent_at_ = now;
  sent_delay_ = report.delay_ms;
  return true;
}

}  // namespace esphome::hid_keyboard
