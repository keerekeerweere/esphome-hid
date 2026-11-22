#include "pointer_engine.h"

namespace esphome::hid_keyboard {

Result PointerEngine::event(int x, int y, int buttons, int wheel) {
  if (!available_) return result_(Result::UNAVAILABLE);
  if (x < 0 || x > POINTER_MAX || y < 0 || y > POINTER_MAX || buttons < 0 || buttons > 0x1f || wheel < -127 ||
      wheel > 127)
    return result_(Result::INVALID_INPUT);
  PointerReport report;
  report.buttons = static_cast<uint8_t>(buttons);
  report.x = static_cast<uint16_t>(x);
  report.y = static_cast<uint16_t>(y);
  report.wheel = static_cast<int8_t>(wheel);
  // A pure move replaces a pending pure move with the same buttons: only the
  // latest position matters, and the queue stays short while the mouse is flooded.
  if (count_ > 0 && report.wheel == 0) {
    PointerReport &tail = queue_[(head_ + count_ - 1) % CAPACITY];
    if (tail.wheel == 0 && tail.buttons == report.buttons) {
      tail = report;
      last_ = report;
      return result_(Result::ACCEPTED);
    }
  }
  if (count_ == CAPACITY) return result_(Result::QUEUE_FULL);
  queue_[(head_ + count_) % CAPACITY] = report;
  count_++;
  last_ = report;
  last_.wheel = 0;
  return result_(Result::ACCEPTED);
}

void PointerEngine::cancel() {
  count_ = 0;
  last_.buttons = 0;
  last_.wheel = 0;
  if (available_) {
    queue_[head_] = last_;
    count_ = 1;
  }
  result_(Result::CANCELLED);
}

void PointerEngine::reset() {
  const bool held = last_.buttons != 0;
  count_ = 0;
  last_.buttons = 0;
  last_.wheel = 0;
  if (held && available_) {
    queue_[head_] = last_;
    count_ = 1;
  }
}

bool PointerEngine::next_report(PointerReport &report) {
  if (!available_ || count_ == 0) return false;
  report = queue_[head_];
  head_ = (head_ + 1) % CAPACITY;
  count_--;
  return true;
}

}  // namespace esphome::hid_keyboard
