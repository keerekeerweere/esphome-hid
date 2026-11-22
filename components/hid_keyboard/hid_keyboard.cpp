#include "hid_keyboard.h"
#include "usb_descriptors.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "tinyusb_default_config.h"
#include "device/dcd.h"
#include "device/usbd_pvt.h"
#include <algorithm>
#include <cstring>

namespace esphome::hid_keyboard {
static const char* const TAG = "hid_keyboard";
// ESP-IDF supplies ISR-safe stdatomic operations on the S3, including its
// PSRAM workaround. The toolchain's is_always_lock_free flag is false with
// that workaround, so do not override SDK atomics or compiler feature macros.
std::atomic<uint32_t> HIDKeyboard::generation_{0}, HIDKeyboard::mailbox_{FREE}, HIDKeyboard::request_generation_{0},
    HIDKeyboard::request_report_{0}, HIDKeyboard::request_report2_{0}, HIDKeyboard::snapshot_{0},
    HIDKeyboard::snapshot2_{0}, HIDKeyboard::snapshot_generation_{0};
// Match the pinned TinyUSB driver's reset idle rate, so GET_IDLE and actual
// retransmission behavior agree without modifying the class driver's internals.
std::atomic<uint8_t> HIDKeyboard::idle_rate_{0}, HIDKeyboard::leds_{0};
std::atomic<bool> HIDKeyboard::usb_available_{false};
std::atomic<uint32_t> HIDKeyboard::pointer_epoch_{0};
HIDKeyboard::Channel HIDKeyboard::rel_ch_, HIDKeyboard::abs_ch_;
uint32_t HIDKeyboard::usb_last_report_at_{0};

void HIDKeyboard::setup() {
  // IDs were set by code generation before setup.
  const uint16_t vendor = device_descriptor_.idVendor, product = device_descriptor_.idProduct;
  device_descriptor_ = {};
  device_descriptor_.bLength = sizeof(tusb_desc_device_t);
  device_descriptor_.bDescriptorType = TUSB_DESC_DEVICE;
  device_descriptor_.bcdUSB = 0x0200;
  device_descriptor_.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE;
  device_descriptor_.idVendor = vendor;
  device_descriptor_.idProduct = product;
  device_descriptor_.bcdDevice = 0x0100;
  device_descriptor_.iManufacturer = 1;
  device_descriptor_.iProduct = 2;
  device_descriptor_.iSerialNumber = 3;
  device_descriptor_.bNumConfigurations = 1;
  get_mac_address_into_buffer(serial_);
  auto config = TINYUSB_DEFAULT_CONFIG();
  config.port = TINYUSB_PORT_FULL_SPEED_0;
  config.phy.self_powered = vbus_pin_ >= 0;
  config.phy.vbus_monitor_io = vbus_pin_;
  config.descriptor.device = &device_descriptor_;
  config.descriptor.string = strings_;
  config.descriptor.string_count = 4;
  build_config_descriptor_();
  config.descriptor.full_speed_config = config_descriptor_;
  config.event_cb = [](tinyusb_event_t* event, void*) {
    if (event->id == TINYUSB_EVENT_ATTACHED) {
      protocol_changed();
      usb_available_.store(true);
      tud_sof_cb_enable(true);
    } else if (event->id == TINYUSB_EVENT_RESUMED) {
      usb_available_.store(true);
    } else {
      usb_available_.store(false);
      protocol_changed();
    }
  };
  const auto result = tinyusb_driver_install(&config);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "USB driver install failed: %s", esp_err_to_name(result));
    mark_failed();
    return;
  }
  initialized_ = true;
}

bool HIDKeyboard::is_connected() const {
  return initialized_ && usb_available_.load() && tud_mounted() && !tud_suspended();
}

void HIDKeyboard::sync_lifecycle_() {
  const auto generation = generation_.load();
  if (generation != observed_generation_) {
    observed_generation_ = generation;
    engine_.reset();
    engine_transfer_ = false;
    snapshot_.store(0);
    snapshot2_.store(0);
  }
  engine_.set_available(is_connected());
  engine_.set_caps_lock((leds_.load() & 2) != 0);
}

void HIDKeyboard::accept_(Result result) {
  last_was_mouse_ = false;
  if (result != Result::ACCEPTED) ESP_LOGW(TAG, "Keyboard request rejected: %s", last_result_message());
}

const char* HIDKeyboard::last_result_message() const {
  switch (engine_.last_result()) {
    case Result::ACCEPTED:
      return "accepted";
    case Result::UNAVAILABLE:
      return "USB host unavailable";
    case Result::INVALID_INPUT:
      return "unsupported key, character or modifier";
    case Result::TOO_LONG:
      return "text exceeds max_text_length";
    case Result::QUEUE_FULL:
      return "request queue full";
    case Result::CANCELLED:
      return "cancelled";
    case Result::USB_TIMEOUT:
      return "USB transfer failed or timed out";
  }
  return "unknown";
}

bool HIDKeyboard::send_(const Report &report) {
  if (inflight_ || !is_connected() || mailbox_.load() != FREE) return false;
  const auto generation = generation_.load();
  if (generation != observed_generation_) return false;
  request_generation_.store(generation);
  // Six keys do not fit one 32-bit atomic; mailbox_.store(REQUESTED) below publishes both words.
  request_report_.store(report.modifiers | (static_cast<uint32_t>(report.keys[0]) << 8) |
                        (static_cast<uint32_t>(report.keys[1]) << 16) | (static_cast<uint32_t>(report.keys[2]) << 24));
  request_report2_.store(report.keys[3] | (static_cast<uint32_t>(report.keys[4]) << 8) |
                         (static_cast<uint32_t>(report.keys[5]) << 16));
  mailbox_.store(REQUESTED);
  inflight_ = true;
  engine_transfer_ = true;
  sent_at_ = millis();
  engine_.report_submitted(sent_at_);
  return true;
}

void HIDKeyboard::build_config_descriptor_() {
  // Interface order is fixed: keyboard, then relative mouse, then absolute pointer.
  const uint8_t interfaces = 1 + (mouse_enabled_ ? 1 : 0) + (pointer_enabled_ ? 1 : 0);
  const uint16_t total = TUD_CONFIG_DESC_LEN + interfaces * TUD_HID_DESC_LEN;
  const bool self_powered = vbus_pin_ >= 0;
  uint8_t *out = config_descriptor_;
  const uint8_t header[] = {TUD_CONFIG_DESCRIPTOR(1, interfaces, 0, total,
                                                  self_powered ? TUSB_DESC_CONFIG_ATT_SELF_POWERED : 0,
                                                  self_powered ? 2 : 100)};
  std::memcpy(out, header, sizeof(header));
  out += sizeof(header);
  std::memcpy(out, KEYBOARD_ITF, sizeof(KEYBOARD_ITF));
  out += sizeof(KEYBOARD_ITF);
  uint8_t instance = 1;
  rel_ch_.instance.store(0xff);
  abs_ch_.instance.store(0xff);
  if (mouse_enabled_) {
    std::memcpy(out, MOUSE_ITF, sizeof(MOUSE_ITF));
    out += sizeof(MOUSE_ITF);
    rel_ch_.instance.store(instance++);
  }
  if (pointer_enabled_) {
    const uint8_t *itf = mouse_enabled_ ? POINTER_ITF_2 : POINTER_ITF_1;
    std::memcpy(out, itf, TUD_HID_DESC_LEN);
    abs_ch_.instance.store(instance++);
  }
}

void HIDKeyboard::mouse_sync_() {
  mouse_engine_.set_available(initialized_ && is_connected());
  pointer_engine_.set_available(initialized_ && is_connected());
}

void HIDKeyboard::mouse_accept_(Result result) {
  last_was_mouse_ = true;
  mouse_result_ = result;
  if (result != Result::ACCEPTED) ESP_LOGW(TAG, "Mouse request rejected: %s", last_mouse_message());
}

const char *HIDKeyboard::last_mouse_message() const {
  switch (mouse_result_) {
    case Result::ACCEPTED:
      return "accepted";
    case Result::UNAVAILABLE:
      return mouse_enabled_ || pointer_enabled_ ? "USB host unavailable" : "mouse/pointer is not enabled";
    case Result::INVALID_INPUT:
      return "unknown button or value out of range";
    case Result::QUEUE_FULL:
      return "mouse queue full";
    case Result::CANCELLED:
      return "cancelled";
    default:
      return "USB transfer failed";
  }
}

void HIDKeyboard::mouse_button_(Op op, StringRef name) {
  mouse_sync_();
  uint8_t mask = 0;
  if (!mouse_enabled_) return mouse_accept_(Result::UNAVAILABLE);
  if (!mouse_button({name.c_str(), name.size()}, mask)) return mouse_accept_(Result::INVALID_INPUT);
  mouse_accept_(op == Op::PRESS     ? mouse_engine_.press(mask)
                : op == Op::RELEASE ? mouse_engine_.release(mask)
                                    : mouse_engine_.click(mask));
}

void HIDKeyboard::pointer_event(int x, int y, int buttons, int wheel) {
  mouse_sync_();
  mouse_accept_(pointer_enabled_ ? pointer_engine_.event(x, y, buttons, wheel) : Result::UNAVAILABLE);
}

// Returns true when the channel's mailbox is free for a new report.
bool HIDKeyboard::channel_ready_(Channel &channel, bool &inflight, uint32_t &sent_at, uint32_t now, bool &failed) {
  failed = false;
  if (inflight) {
    const auto state = channel.mailbox.load();
    if (state == COMPLETE || state == RETRY || state == FAILED) {
      inflight = false;
      channel.mailbox.store(FREE);
      failed = state == FAILED;
    } else if (static_cast<uint32_t>(now - sent_at) >= 2000) {
      failed = true;  // Keep waiting for the endpoint; never overlap transfers.
    }
  }
  return !inflight && channel.mailbox.load() == FREE;
}

void HIDKeyboard::channel_send_(Channel &channel, bool &inflight, uint32_t &sent_at, uint32_t now, uint32_t word0,
                                uint32_t word1) {
  channel.word0.store(word0);
  channel.word1.store(word1);
  channel.request_epoch.store(pointer_epoch_.load());
  channel.mailbox.store(REQUESTED);
  inflight = true;
  sent_at = now;
}

void HIDKeyboard::mouse_loop_() {
  const uint32_t now = millis();
  mouse_sync_();
  const auto epoch = pointer_epoch_.load();
  if (epoch != pointer_observed_epoch_) {
    pointer_observed_epoch_ = epoch;
    mouse_engine_.reset();
    pointer_engine_.reset();
  }
  if (!is_connected()) return;
  bool failed = false;
  if (mouse_enabled_ && channel_ready_(rel_ch_, mouse_inflight_, mouse_sent_at_, now, failed) && !failed) {
    MouseReport report;
    if (mouse_engine_.next_report(now, report))
      channel_send_(rel_ch_, mouse_inflight_, mouse_sent_at_, now,
                    static_cast<uint32_t>(report.buttons) | (static_cast<uint32_t>(static_cast<uint8_t>(report.dx)) << 8) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(report.dy)) << 16) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(report.wheel)) << 24),
                    0);
  }
  if (failed) mouse_engine_.cancel();
  failed = false;
  if (pointer_enabled_ && channel_ready_(abs_ch_, pointer_inflight_, pointer_sent_at_, now, failed) && !failed) {
    PointerReport report;
    if (pointer_engine_.next_report(report))
      channel_send_(abs_ch_, pointer_inflight_, pointer_sent_at_, now,
                    static_cast<uint32_t>(report.buttons) | (static_cast<uint32_t>(static_cast<uint8_t>(report.wheel)) << 8),
                    static_cast<uint32_t>(report.x) | (static_cast<uint32_t>(report.y) << 16));
  }
  if (failed) pointer_engine_.cancel();
}

// USB task: send a requested report on a channel (instance index from enumeration).
void HIDKeyboard::channel_frame_(Channel &channel, bool pointer) {
  const uint8_t instance = channel.instance.load();
  if (instance == 0xff || !usb_available_.load()) return;
  const auto state = channel.mailbox.load();
  if (state != REQUESTED && state != IN_FLIGHT) return;
  if (channel.request_epoch.load() != pointer_epoch_.load()) {
    channel.mailbox.store(RETRY);
    return;
  }
  if (state != REQUESTED || !tud_mounted() || tud_suspended() || !tud_hid_n_ready(instance)) return;
  const auto w0 = channel.word0.load(), w1 = channel.word1.load();
  bool sent;
  if (pointer) {
    // buttons, X (LE16), Y (LE16), wheel
    const uint8_t report[6]{static_cast<uint8_t>(w0),      static_cast<uint8_t>(w1),       static_cast<uint8_t>(w1 >> 8),
                            static_cast<uint8_t>(w1 >> 16), static_cast<uint8_t>(w1 >> 24), static_cast<uint8_t>(w0 >> 8)};
    sent = tud_hid_n_report(instance, 0, report, sizeof(report));
  } else {
    sent = tud_hid_n_mouse_report(instance, 0, w0 & 0xff, static_cast<int8_t>(w0 >> 8), static_cast<int8_t>(w0 >> 16),
                                  static_cast<int8_t>(w0 >> 24), 0);
  }
  if (sent) channel.mailbox.store(IN_FLIGHT);
}

void HIDKeyboard::mouse_frame_() {
  channel_frame_(rel_ch_, false);
  channel_frame_(abs_ch_, true);
}

void HIDKeyboard::channel_complete_(uint8_t instance, bool failed) {
  Channel &channel = instance == rel_ch_.instance.load() ? rel_ch_ : abs_ch_;
  if (channel.mailbox.load() != IN_FLIGHT) return;
  if (failed)
    channel.mailbox.store(FAILED);
  else
    channel.mailbox.store(channel.request_epoch.load() == pointer_epoch_.load() ? COMPLETE : RETRY);
}

void HIDKeyboard::loop() {
  if (!initialized_) return;
  if (mouse_enabled_ || pointer_enabled_) mouse_loop_();
  sync_lifecycle_();
  const uint32_t now = millis();
  if (!is_connected()) return;
  if (inflight_) {
    const auto state = mailbox_.load();
    if (state == COMPLETE || state == RETRY || state == FAILED) {
      inflight_ = false;
      mailbox_.store(FREE);
      if (state == COMPLETE) {
        if (engine_transfer_) engine_.report_completed(now);
      } else if (engine_transfer_) {
        // A stale mailbox from a cancelled/reset session must not cancel a
        // fresh request accepted after that session ended.
        engine_.reset();
        if (state == FAILED) engine_.cancel(Result::USB_TIMEOUT);
      }
    } else if (static_cast<uint32_t>(now - sent_at_) >= 2000) {
      // Keep waiting for the endpoint; never enqueue a release over a transfer
      // which may still complete. Cancellation makes release the next report.
      protocol_changed();
      sync_lifecycle_();
      engine_.cancel(Result::USB_TIMEOUT);
    }
    if (inflight_) return;
  }
  Report report;
  if (engine_.next_report(now, report)) send_(report);
}

void HIDKeyboard::dump_config() {
  ESP_LOGCONFIG(TAG, "USB Boot Keyboard: layout %s; ID-free boot interface", layout_name());
  ESP_LOGCONFIG(TAG, "  Relative mouse: %s", mouse_enabled_ ? "boot mouse interface" : "disabled");
  ESP_LOGCONFIG(TAG, "  Absolute pointer: %s", pointer_enabled_ ? "enabled (0..32767)" : "disabled");
  if (vbus_pin_ < 0)
    ESP_LOGCONFIG(TAG, "  Bus-powered mode (no VBUS sense)");
  else
    ESP_LOGCONFIG(TAG, "  Self-powered mode; VBUS sense GPIO%d", vbus_pin_);
}

void HIDKeyboard::bus_event(uint32_t event) {
  if (event == DCD_EVENT_BUS_RESET || event == DCD_EVENT_UNPLUGGED || event == DCD_EVENT_SUSPEND) {
    usb_available_.store(false);
    generation_.fetch_add(1, std::memory_order_relaxed);
    pointer_epoch_.fetch_add(1, std::memory_order_relaxed);
    if (event == DCD_EVENT_BUS_RESET) {
      idle_rate_.store(0);
      leds_.store(0);
    }
  }
}
void HIDKeyboard::protocol_changed() {
  generation_.fetch_add(1);
  pointer_epoch_.fetch_add(1);
}
void HIDKeyboard::transfer_complete() {
  usb_last_report_at_ = millis();
  if (mailbox_.load() == IN_FLIGHT) mailbox_.store(request_generation_.load() == generation_.load() ? COMPLETE : RETRY);
}
void HIDKeyboard::transfer_failed() {
  if (mailbox_.load() == IN_FLIGHT) mailbox_.store(FAILED);
}
void HIDKeyboard::usb_frame() {
  mouse_frame_();
  // TinyUSB calls this from its task, not the SOF ISR. Serializing sends with
  // reset processing prevents a stale main-loop report entering a new session.
  if (!usb_available_.load()) return;
  const auto state = mailbox_.load();
  if (state != REQUESTED && state != IN_FLIGHT) {
    const uint32_t idle_ms = idle_rate_.load() * 4U;
    // Idle repeats use SOF time rather than the slower ESPHome loop so a host
    // requesting a four-millisecond idle period receives it at that rate.
    if (idle_ms && static_cast<uint32_t>(millis() - usb_last_report_at_) >= idle_ms && tud_mounted() &&
        !tud_suspended() && tud_hid_ready()) {
      const auto report = snapshot();
      if (tud_hid_keyboard_report(0, report.modifiers, report.keys.data())) usb_last_report_at_ = millis();
    }
    return;
  }
  if (request_generation_.load() != generation_.load()) {
    mailbox_.store(RETRY);
    return;
  }
  if (state != REQUESTED || !tud_mounted() || tud_suspended() || !tud_hid_ready()) return;
  const auto packed = request_report_.load(), packed2 = request_report2_.load();
  const uint8_t keys[6]{static_cast<uint8_t>(packed >> 8),  static_cast<uint8_t>(packed >> 16),
                        static_cast<uint8_t>(packed >> 24), static_cast<uint8_t>(packed2),
                        static_cast<uint8_t>(packed2 >> 8), static_cast<uint8_t>(packed2 >> 16)};
  if (tud_hid_keyboard_report(0, packed & 0xff, keys)) {
    snapshot_.store(packed);
    snapshot2_.store(packed2);
    snapshot_generation_.store(request_generation_.load());
    usb_last_report_at_ = millis();
    mailbox_.store(IN_FLIGHT);
  }
}
void HIDKeyboard::set_idle(uint8_t rate) {
  idle_rate_.store(rate);
  usb_last_report_at_ = millis();
}
void HIDKeyboard::set_leds(uint8_t leds) {
  leds_.store(leds);
}
Report HIDKeyboard::snapshot() {
  Report report;
  if (!usb_available_.load() || snapshot_generation_.load() != generation_.load()) return report;
  const uint32_t packed = snapshot_.load();
  report.modifiers = packed & 0xff;
  const uint32_t packed2 = snapshot2_.load();
  report.keys = {static_cast<uint8_t>(packed >> 8),  static_cast<uint8_t>(packed >> 16),
                 static_cast<uint8_t>(packed >> 24), static_cast<uint8_t>(packed2),
                 static_cast<uint8_t>(packed2 >> 8), static_cast<uint8_t>(packed2 >> 16)};
  return report;
}
}  // namespace esphome::hid_keyboard

extern "C" {
// TinyUSB exposes reset processing through its class-driver extension API.
// This observer claims no interface and does not replace the built-in HID
// driver. The ISR hook below fences input as soon as reset is queued; this
// second fence runs when reset is actually processed, even if an older mount
// callback was still ahead of it in the USB task queue.
const usbd_class_driver_t* usbd_app_driver_get_cb(uint8_t* count) {
  static const auto observer = [] {
    usbd_class_driver_t driver{};
    driver.name = "keyboard_lifecycle";
    driver.init = [] {};
    driver.deinit = [] { return true; };
    driver.reset = [](uint8_t) { esphome::hid_keyboard::HIDKeyboard::bus_event(DCD_EVENT_BUS_RESET); };
    driver.open = [](uint8_t, const tusb_desc_interface_t*, uint16_t) -> uint16_t { return 0; };
    return driver;
  }();
  *count = 1;
  return &observer;
}
void tud_event_hook_cb(uint8_t, uint32_t event, bool) {
  esphome::hid_keyboard::HIDKeyboard::bus_event(event);
}
void tud_sof_cb(uint32_t) {
  esphome::hid_keyboard::HIDKeyboard::usb_frame();
}
const uint8_t* tud_hid_descriptor_report_cb(uint8_t instance) {
  using esphome::hid_keyboard::HIDKeyboard;
  if (instance != 0 && instance == HIDKeyboard::pointer_instance()) return esphome::hid_keyboard::POINTER_REPORT_DESCRIPTOR;
  if (instance != 0 && instance == HIDKeyboard::mouse_instance()) return esphome::hid_keyboard::MOUSE_REPORT_DESCRIPTOR;
  return esphome::hid_keyboard::REPORT_DESCRIPTOR;
}
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t* buffer,
                               uint16_t length) {
  if (instance != 0) {  // Mouse / pointer: report the idle (all zero) state.
    if (type != HID_REPORT_TYPE_INPUT) return 0;
    const auto size = std::min<uint16_t>(length, esphome::hid_keyboard::HIDKeyboard::pointer_instance() == instance ? 6 : 5);
    std::memset(buffer, 0, size);
    return size;
  }
  if (report_id != 0) return 0;
  if (type == HID_REPORT_TYPE_OUTPUT && length > 0) {
    buffer[0] = esphome::hid_keyboard::HIDKeyboard::leds();
    return 1;
  }
  if (type != HID_REPORT_TYPE_INPUT) return 0;
  const auto report = esphome::hid_keyboard::HIDKeyboard::snapshot();
  const auto size = std::min<size_t>(length, sizeof(report));
  std::memcpy(buffer, &report, size);
  return size;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, const uint8_t* buffer, uint16_t length) {
  if (instance == 0 && report_id == 0 && type == HID_REPORT_TYPE_OUTPUT && length == 1)
    esphome::hid_keyboard::HIDKeyboard::set_leds(buffer[0]);
}
void tud_hid_set_protocol_cb(uint8_t instance, uint8_t) {
  // The mouse interface switching protocol must not disturb the keyboard.
  if (instance != 0)
    esphome::hid_keyboard::HIDKeyboard::mouse_protocol_changed();
  else
    esphome::hid_keyboard::HIDKeyboard::protocol_changed();
}
bool tud_hid_set_idle_cb(uint8_t instance, uint8_t rate) {
  if (instance == 0) esphome::hid_keyboard::HIDKeyboard::set_idle(rate);
  return true;
}
void tud_hid_report_complete_cb(uint8_t instance, const uint8_t*, uint16_t) {
  if (instance != 0)
    esphome::hid_keyboard::HIDKeyboard::channel_complete_(instance, false);
  else
    esphome::hid_keyboard::HIDKeyboard::transfer_complete();
}
void tud_hid_report_failed_cb(uint8_t instance, hid_report_type_t, const uint8_t*, uint16_t) {
  if (instance != 0)
    esphome::hid_keyboard::HIDKeyboard::channel_complete_(instance, true);
  else
    esphome::hid_keyboard::HIDKeyboard::transfer_failed();
}
}
