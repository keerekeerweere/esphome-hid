#pragma once

#include "keyboard_engine.h"
#include "layouts.h"
#include "mouse_engine.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "tinyusb.h"
#include <atomic>

namespace esphome::hid_keyboard {

class HIDKeyboard : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }
  void configure(uint32_t press_ms, uint32_t gap_ms, size_t max_length, size_t queue_size) {
    engine_.configure(press_ms, gap_ms, max_length, queue_size);
  }
  void set_usb_ids(uint16_t vendor, uint16_t product) {
    device_descriptor_.idVendor = vendor;
    device_descriptor_.idProduct = product;
  }
  void set_vbus_monitor_pin(int pin) { vbus_pin_ = pin; }
  // Optional relative boot mouse (second HID interface).
  void set_mouse(bool enabled, uint32_t click_ms, uint32_t gap_ms) {
    mouse_enabled_ = enabled;
    mouse_engine_.configure(click_ms, gap_ms);
    mouse_active_.store(enabled);
  }
  void mouse_move(int dx, int dy, int wheel) {
    mouse_sync_();
    mouse_accept_(mouse_enabled_ ? mouse_engine_.move(dx, dy, wheel) : Result::UNAVAILABLE);
  }
  void mouse_press(StringRef button) { mouse_button_(Op::PRESS, button); }
  void mouse_release(StringRef button) { mouse_button_(Op::RELEASE, button); }
  void mouse_click(StringRef button) { mouse_button_(Op::CLICK, button); }
  bool last_mouse_accepted() const { return mouse_result_ == Result::ACCEPTED; }
  const char *last_mouse_message() const;
  // Host keyboard layout used to translate text and single-character keys.
  void set_layout(Layout layout) { engine_.set_layout(layout); }
  bool set_layout_name(const std::string &name) {
    Layout layout;
    if (!layout_from_name(name, layout)) return false;
    engine_.set_layout(layout);
    return true;
  }
  const char *layout_name() const { return esphome::hid_keyboard::layout_name(engine_.layout()); }
  // StringRef is borrowed only for this call; engine copies text synchronously.
  void press(StringRef key, int modifiers) { submit_key_(Operation::PRESS, key, modifiers); }
  void tap(StringRef key, int modifiers) { submit_key_(Operation::TAP, key, modifiers); }
  void type(StringRef text) {
    sync_lifecycle_();
    accept_(engine_.submit_text({text.c_str(), text.size()}));
  }
  // Live key state for remote control: usage is a USB HID usage code
  // (0x04-0xA4 keys, 0xE0-0xE7 modifiers). Independent of the text layout.
  void key_event(int usage, bool down) {
    sync_lifecycle_();
    if (usage < 0 || usage > 255) {
      engine_.reject_input();
      accept_(Result::INVALID_INPUT);
      return;
    }
    accept_(engine_.key_event(static_cast<uint8_t>(usage), down));
  }
  void release_all() {
    protocol_changed();
    sync_lifecycle_();
    engine_.cancel();
    if (mouse_enabled_) {
      mouse_sync_();
      mouse_observed_epoch_ = mouse_epoch_.load();  // protocol_changed() above must not drop the release report.
      mouse_engine_.cancel();
    }
  }
  bool is_connected() const;
  bool is_busy() const { return engine_.busy(); }
  bool last_request_accepted() const { return engine_.last_result() == Result::ACCEPTED; }
  Result last_result() const { return engine_.last_result(); }
  const char* last_result_message() const;
  static uint8_t leds() { return leds_.load(); }

  // USB task/ISR entry points. No queue or automation access from callbacks.
  static void bus_event(uint32_t event);
  static void protocol_changed();
  static void transfer_complete();
  static void transfer_failed();
  static void set_idle(uint8_t rate);
  static void set_leds(uint8_t leds);
  static void usb_frame();
  static void mouse_transfer_complete();
  static void mouse_transfer_failed();
  static void mouse_protocol_changed() { mouse_epoch_.fetch_add(1); }
  static void mouse_frame_();
  static Report snapshot();

 protected:
  enum class Op : uint8_t { PRESS, RELEASE, CLICK };
  void mouse_sync_();
  void mouse_loop_();
  void mouse_accept_(Result result);
  void mouse_button_(Op op, StringRef button);
  MouseEngine mouse_engine_;
  bool mouse_enabled_{false}, mouse_inflight_{false};
  uint32_t mouse_observed_epoch_{0}, mouse_sent_at_{0};
  Result mouse_result_{Result::ACCEPTED};
  void sync_lifecycle_();
  void submit_key_(Operation operation, StringRef key, int modifiers) {
    sync_lifecycle_();
    if (modifiers < 0 || modifiers > 255) {
      engine_.reject_input();
      accept_(Result::INVALID_INPUT);
      return;
    }
    accept_(engine_.submit_key(operation, {key.c_str(), key.size()}, modifiers));
  }
  void accept_(Result result);
  bool send_(const Report &report);
  KeyboardEngine engine_;
  bool initialized_{false}, inflight_{false}, engine_transfer_{false};
  uint32_t observed_generation_{0}, sent_at_{0};
  int vbus_pin_{-1};
  char serial_[MAC_ADDRESS_BUFFER_SIZE]{};
  char language_[2]{0x09, 0x04};
  const char* strings_[4]{language_, "ESPHome", "ESPHome Boot Keyboard", serial_};
  tusb_desc_device_t device_descriptor_{};
  // A single atomic mailbox keeps all TinyUSB report operations on its own
  // servicing task. The main loop publishes only when the slot is FREE.
  enum Mailbox : uint32_t { FREE, REQUESTED, IN_FLIGHT, COMPLETE, RETRY, FAILED };
  static std::atomic<uint32_t> generation_, mailbox_, request_generation_, request_report_, request_report2_,
      snapshot_, snapshot2_, snapshot_generation_;
  static std::atomic<uint8_t> idle_rate_, leds_;
  static std::atomic<bool> usb_available_, mouse_active_;
  static std::atomic<uint32_t> mouse_mailbox_, mouse_report_, mouse_epoch_, mouse_request_epoch_;
  static uint32_t usb_last_report_at_;  // USB task only
};

}  // namespace esphome::hid_keyboard
