#pragma once

#include "keyboard_engine.h"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace esphome::hid_keyboard {

// Local addition (not upstream): host keyboard layouts. USB HID reports carry
// physical key positions, so typing text on a non-US host means sending the
// position/modifier combination that produces each character on that layout.
enum class Layout : uint8_t { QWERTY_US, QWERTY_UK, AZERTY_BE, AZERTY_FR, QWERTZ_DE };
constexpr size_t LAYOUT_COUNT = 5;

const char *layout_name(Layout layout);
bool layout_from_name(std::string_view name, Layout &layout);

// Maps one Unicode code point to at most two key presses (a dead key is
// followed by Space so the bare accent character is produced).
bool layout_keys(Layout layout, uint32_t codepoint, Key out[2], size_t &count);

// Decodes one UTF-8 sequence at text[pos]; rejects overlong/surrogate/invalid input.
bool decode_utf8(std::string_view text, size_t &pos, uint32_t &codepoint);

}  // namespace esphome::hid_keyboard
