#pragma once
#include "tusb.h"
#include "class/hid/hid_device.h"

namespace esphome::hid_keyboard {
inline constexpr uint8_t REPORT_DESCRIPTOR[] = {TUD_HID_REPORT_DESC_KEYBOARD()};
inline constexpr uint8_t CONFIG_DESCRIPTOR[] = {
    // No remote wakeup advertised: suspend cancels rather than retaining input.
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(REPORT_DESCRIPTOR), 0x81, 8, 1),
};
inline constexpr uint8_t SELF_POWERED_CONFIG_DESCRIPTOR[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN, TUSB_DESC_CONFIG_ATT_SELF_POWERED, 2),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(REPORT_DESCRIPTOR), 0x81, 8, 1),
};

// Local addition (not upstream): optional extra HID interfaces, assembled at
// runtime in this order: keyboard (itf 0, EP 0x81), relative boot mouse (itf 1,
// EP 0x82), absolute pointer (EP 0x83; itf 2 with the mouse, else itf 1).
inline constexpr uint8_t MOUSE_REPORT_DESCRIPTOR[] = {TUD_HID_REPORT_DESC_MOUSE()};
// Absolute pointer: 5 buttons, 16-bit X/Y over 0..32767, relative wheel. Not a
// boot device: it works once the OS has a driver, while BIOS uses the boot mouse.
inline constexpr uint8_t POINTER_REPORT_DESCRIPTOR[] = {
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_MOUSE),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_USAGE(HID_USAGE_DESKTOP_POINTER),
    HID_COLLECTION(HID_COLLECTION_PHYSICAL),
    HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
    HID_USAGE_MIN(1),
    HID_USAGE_MAX(5),
    HID_LOGICAL_MIN(0),
    HID_LOGICAL_MAX(1),
    HID_REPORT_COUNT(5),
    HID_REPORT_SIZE(1),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_REPORT_COUNT(1),
    HID_REPORT_SIZE(3),
    HID_INPUT(HID_CONSTANT),
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_X),
    HID_USAGE(HID_USAGE_DESKTOP_Y),
    HID_LOGICAL_MIN_N(0, 2),
    HID_LOGICAL_MAX_N(0x7fff, 2),
    HID_REPORT_SIZE(16),
    HID_REPORT_COUNT(2),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_USAGE(HID_USAGE_DESKTOP_WHEEL),
    HID_LOGICAL_MIN(0x81),
    HID_LOGICAL_MAX(0x7f),
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(1),
    HID_INPUT(HID_DATA | HID_VARIABLE | HID_RELATIVE),
    HID_COLLECTION_END,
    HID_COLLECTION_END,
};
inline constexpr uint8_t KEYBOARD_ITF[] = {
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(REPORT_DESCRIPTOR), 0x81, 8, 1)};
inline constexpr uint8_t MOUSE_ITF[] = {
    TUD_HID_DESCRIPTOR(1, 0, HID_ITF_PROTOCOL_MOUSE, sizeof(MOUSE_REPORT_DESCRIPTOR), 0x82, 8, 4)};
inline constexpr uint8_t POINTER_ITF_1[] = {
    TUD_HID_DESCRIPTOR(1, 0, HID_ITF_PROTOCOL_NONE, sizeof(POINTER_REPORT_DESCRIPTOR), 0x83, 8, 4)};
inline constexpr uint8_t POINTER_ITF_2[] = {
    TUD_HID_DESCRIPTOR(2, 0, HID_ITF_PROTOCOL_NONE, sizeof(POINTER_REPORT_DESCRIPTOR), 0x83, 8, 4)};
}  // namespace esphome::hid_keyboard
