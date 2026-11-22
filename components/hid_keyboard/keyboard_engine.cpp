#include "keyboard_engine.h"
#include "layouts.h"
#include <utility>

namespace esphome::hid_keyboard {

bool ascii_key(uint8_t c, Key &key) {
  key = {};
  if (c >= 'a' && c <= 'z') {
    key = {static_cast<uint8_t>(4 + c - 'a'), 0, true};
    return true;
  }
  if (c >= 'A' && c <= 'Z') {
    key = {static_cast<uint8_t>(4 + c - 'A'), 2, true};
    return true;
  }
  if (c >= '1' && c <= '9') {
    key.code = 0x1e + c - '1';
    return true;
  }
  if (c == '0') {
    key.code = 0x27;
    return true;
  }
  constexpr std::string_view plain = "-=[]\\;'`,./";
  constexpr std::string_view shifted = "_+{}|:\"~<>?";
  constexpr uint8_t codes[] = {0x2d, 0x2e, 0x2f, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38};
  for (size_t i = 0; i < plain.size(); i++) {
    if (c == plain[i] || c == shifted[i]) {
      key = {codes[i], static_cast<uint8_t>(c == shifted[i] ? 2 : 0)};
      return true;
    }
  }
  constexpr std::string_view digits = "!@#$%^&*()";
  for (size_t i = 0; i < digits.size(); i++) {
    if (c == digits[i]) {
      key = {static_cast<uint8_t>(0x1e + i), 2};
      return true;
    }
  }
  switch (c) {
    case ' ':
      key.code = 0x2c;
      return true;
    case '\n':
      key.code = 0x28;
      return true;
    case '\t':
      key.code = 0x2b;
      return true;
    default:
      return false;
  }
}

bool named_key(std::string_view name, Key &key) {
  if (name.size() == 1) return ascii_key(static_cast<uint8_t>(name[0]), key);
  if (name.size() > 16) return false;
  char upper[17]{};
  for (size_t i = 0; i < name.size(); i++) {
    auto c = name[i];
    upper[i] = c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
  }
  std::string_view normalized(upper, name.size());
  struct Entry {
    std::string_view name;
    uint8_t code;
  };
  static constexpr Entry entries[] = {
      {"ENTER", 0x28}, {"RETURN", 0x28},   {"ESC", 0x29},       {"ESCAPE", 0x29},       {"BACKSPACE", 0x2a},
      {"TAB", 0x2b},   {"SPACE", 0x2c},    {"CAPS_LOCK", 0x39}, {"PRINT_SCREEN", 0x46}, {"SCROLL_LOCK", 0x47},
      {"PAUSE", 0x48}, {"INSERT", 0x49},   {"HOME", 0x4a},      {"PAGEUP", 0x4b},       {"DELETE", 0x4c},
      {"END", 0x4d},   {"PAGEDOWN", 0x4e}, {"RIGHT", 0x4f},     {"LEFT", 0x50},         {"DOWN", 0x51},
      {"UP", 0x52},
  };
  for (const auto &entry : entries) {
    if (entry.name == normalized) {
      key = {entry.code, 0};
      return true;
    }
  }
  for (uint8_t i = 1; i <= 12; i++) {
    if (normalized == "F" + std::to_string(i)) {
      key = {static_cast<uint8_t>(0x39 + i), 0};
      return true;
    }
  }
  // Modifier-only keys permit explicit held modifier operations.
  static constexpr std::string_view modifiers[] = {"LEFT_CTRL",  "LEFT_SHIFT",  "LEFT_ALT",  "LEFT_GUI",
                                                   "RIGHT_CTRL", "RIGHT_SHIFT", "RIGHT_ALT", "RIGHT_GUI"};
  for (size_t i = 0; i < 8; i++) {
    if (normalized == modifiers[i]) {
      key = {0, static_cast<uint8_t>(1U << i)};
      return true;
    }
  }
  return false;
}

Result KeyboardEngine::submit_key(Operation operation, std::string_view name, uint8_t modifiers) {
  if (!available_) return result_(Result::UNAVAILABLE);
  Key key;
  size_t consumed = 0;
  uint32_t codepoint = 0;
  if (!name.empty() && decode_utf8(name, consumed, codepoint) && consumed == name.size()) {
    // A single character is typed through the layout; it must be one key press.
    Key keys[2];
    size_t count = 0;
    if (!layout_keys(layout_, codepoint, keys, count) || count != 1) return result_(Result::INVALID_INPUT);
    key = keys[0];
  } else if (!named_key(name, key)) {
    return result_(Result::INVALID_INPUT);
  }
  if (!room_()) return result_(Result::QUEUE_FULL);
  live_clear_();  // The baseline report releases any live keys.
  auto &request = back_();
  request.operation = operation;
  request.key = {key.code, static_cast<uint8_t>(key.modifiers | modifiers), key.letter};
  count_++;
  return result_(Result::ACCEPTED);
}

Result KeyboardEngine::submit_text(std::string_view text) {
  if (!available_) return result_(Result::UNAVAILABLE);
  if (text.size() > max_length_) return result_(Result::TOO_LONG);
  std::vector<Key> keys;
  keys.reserve(text.size());
  for (size_t pos = 0; pos < text.size();) {
    uint32_t codepoint = 0;
    Key resolved[2];
    size_t count = 0;
    if (!decode_utf8(text, pos, codepoint) || !layout_keys(layout_, codepoint, resolved, count)) {
      for (auto &k : keys) k = Key{};  // Do not leave partial passphrases behind.
      return result_(Result::INVALID_INPUT);
    }
    keys.insert(keys.end(), resolved, resolved + count);
  }
  if (keys.empty()) return result_(Result::ACCEPTED);
  if (keys.size() > max_length_ * 2) return result_(Result::TOO_LONG);
  if (!room_()) return result_(Result::QUEUE_FULL);
  live_clear_();
  auto &request = back_();
  request.operation = Operation::TEXT;
  request.keys = std::move(keys);
  count_++;
  return result_(Result::ACCEPTED);
}

Result KeyboardEngine::key_event(uint8_t usage, bool down) {
  if (!available_) return result_(Result::UNAVAILABLE);
  if (count_ != 0 || stage_ != Stage::IDLE || neutral_pending_) return result_(Result::QUEUE_FULL);
  if (usage >= 0xe0 && usage <= 0xe7) {
    const uint8_t bit = static_cast<uint8_t>(1U << (usage - 0xe0));
    live_.modifiers = down ? (live_.modifiers | bit) : (live_.modifiers & ~bit);
  } else if (usage >= 0x04 && usage <= 0xa4) {
    size_t slot = live_.keys.size();
    size_t empty = live_.keys.size();
    for (size_t i = 0; i < live_.keys.size(); i++) {
      if (live_.keys[i] == usage) slot = i;
      if (live_.keys[i] == 0 && empty == live_.keys.size()) empty = i;
    }
    if (down) {
      if (slot == live_.keys.size()) {
        if (empty == live_.keys.size()) return result_(Result::QUEUE_FULL);  // Seven keys: rollover exceeded.
        live_.keys[empty] = usage;
      }
    } else if (slot != live_.keys.size()) {
      live_.keys[slot] = 0;
    }
  } else {
    return result_(Result::INVALID_INPUT);
  }
  live_push_();
  return result_(Result::ACCEPTED);
}

void KeyboardEngine::live_push_() {
  if (live_count_ == LIVE_QUEUE) {
    // Full: fold into the newest pending state rather than dropping the event.
    live_queue_[(live_head_ + live_count_ - 1) % LIVE_QUEUE] = live_;
    return;
  }
  live_queue_[(live_head_ + live_count_) % LIVE_QUEUE] = live_;
  live_count_++;
}

void KeyboardEngine::pop_() {
  // Do not retain passphrases in reusable request strings after completion.
  auto &keys = front_().keys;
  for (auto &k : keys) k = Key{};
  keys.clear();
  head_ = (head_ + 1) % QUEUE_CAPACITY;
  count_--;
  character_ = 0;
}

void KeyboardEngine::cancel(Result reason) {
  while (count_) pop_();
  // Leave an in-flight transfer alone; release must follow its completion.
  stage_ = Stage::IDLE;
  neutral_pending_ = true;
  live_clear_();
  cancelled_inflight_ = waiting_;
  result_(reason);
}

void KeyboardEngine::reset() {
  cancel();
  waiting_ = false;  // USB reset/disconnect has invalidated the old endpoint transfer.
  cancelled_inflight_ = false;
  current_ = {};
}

void KeyboardEngine::set_available(bool available) {
  if (available_ && !available) reset();
  available_ = available;
}

Key KeyboardEngine::active_key_() const {
  const auto &request = queue_[head_];
  Key key = request.key;
  if (request.operation == Operation::TEXT) {
    key = request.keys[character_];
    if (caps_lock_ && key.letter) key.modifiers ^= 2;
  }
  return key;
}

bool KeyboardEngine::next_report(uint32_t now, Report &report) {
  if (!available_ || waiting_) return false;
  if (neutral_pending_) {
    proposed_ = {};
    report = proposed_;
    return true;
  }
  if (stage_ == Stage::HOLD && static_cast<uint32_t>(now - completed_at_) >= press_ms_) stage_ = Stage::RELEASE;
  if (stage_ == Stage::GAP && static_cast<uint32_t>(now - completed_at_) >= gap_ms_) {
    if (front_().operation == Operation::TEXT && ++character_ < front_().keys.size())
      stage_ = Stage::PRESS;
    else {
      pop_();
      stage_ = Stage::IDLE;
    }
  }
  if (stage_ == Stage::HELD) stage_ = Stage::IDLE;
  if (stage_ == Stage::IDLE && count_) stage_ = Stage::BASELINE;
  if (stage_ == Stage::BASELINE || stage_ == Stage::RELEASE)
    proposed_ = {};
  else if (stage_ == Stage::PRESS) {
    const auto key = active_key_();
    proposed_ = {};
    proposed_.modifiers = key.modifiers;
    proposed_.keys[0] = key.code;
  } else if (stage_ == Stage::IDLE && live_count_) {
    proposed_ = live_queue_[live_head_];
    live_head_ = (live_head_ + 1) % LIVE_QUEUE;
    live_count_--;
  } else
    return false;
  report = proposed_;
  return true;
}

void KeyboardEngine::report_submitted(uint32_t /*now*/) {
  waiting_ = true;
}

void KeyboardEngine::report_completed(uint32_t now) {
  if (!waiting_) return;
  waiting_ = false;
  current_ = proposed_;
  completed_at_ = now;
  if (cancelled_inflight_) {
    cancelled_inflight_ = false;
    return;
  }
  if (neutral_pending_) {
    neutral_pending_ = false;
    return;
  }
  switch (stage_) {
    case Stage::BASELINE:
      stage_ = Stage::PRESS;
      break;
    case Stage::PRESS:
      if (front_().operation == Operation::PRESS) {
        pop_();
        stage_ = Stage::HELD;
      } else
        stage_ = Stage::HOLD;
      break;
    case Stage::RELEASE:
      stage_ = Stage::GAP;
      break;
    default:
      break;
  }
}

}  // namespace esphome::hid_keyboard
