#include "keyboard_engine.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace esphome::hid_keyboard;

struct Host {
  KeyboardEngine engine;
  uint32_t now{0};
  bool transfer{false};
  uint32_t due{0};
  std::vector<Report> reports;
  Host() { engine.set_available(true); }
  void tick(uint32_t latency = 1) {
    if (transfer && static_cast<int32_t>(now - due) >= 0) {
      transfer = false;
      engine.report_completed(now);
    }
    Report report;
    if (engine.next_report(now, report)) {
      reports.push_back(report);
      engine.report_submitted(now);
      transfer = true;
      due = now + latency;
    }
    now++;
  }
  void run(int n = 20) {
    for (int i = 0; i < n; i++) tick();
  }
};

int main() {
  Host h;
  h.run();  // Initial neutral report.
  h.reports.clear();

  // Modifier + key chord with several keys held at once.
  assert(h.engine.key_event(0xe0, true) == Result::ACCEPTED);   // Left Ctrl
  assert(h.engine.key_event(0xe2, true) == Result::ACCEPTED);   // Left Alt
  h.run();
  assert(h.engine.key_event(0x4c, true) == Result::ACCEPTED);   // Delete
  assert(h.engine.key_event(0x04, true) == Result::ACCEPTED);   // A
  h.run();
  const Report &held = h.reports.back();
  assert(held.modifiers == 0x05 && held.keys[0] == 0x4c && held.keys[1] == 0x04);

  // Release in any order.
  assert(h.engine.key_event(0x4c, false) == Result::ACCEPTED);
  h.run();
  assert(h.reports.back().keys[0] == 0 && h.reports.back().keys[1] == 0x04);
  assert(h.engine.key_event(0x04, false) == Result::ACCEPTED);
  assert(h.engine.key_event(0xe0, false) == Result::ACCEPTED);
  assert(h.engine.key_event(0xe2, false) == Result::ACCEPTED);
  h.run();
  assert(h.reports.back() == Report{});

  // A press and release queued while a transfer is in flight are both delivered.
  h.reports.clear();
  assert(h.engine.key_event(0x05, true) == Result::ACCEPTED);
  h.tick(10);  // Slow transfer: press report in flight.
  assert(h.engine.key_event(0x05, false) == Result::ACCEPTED);
  h.run(40);
  assert(h.reports.size() == 2 && h.reports[0].keys[0] == 0x05 && h.reports[1] == Report{});

  // Seven simultaneous keys exceeds six-key rollover and is rejected.
  for (uint8_t k = 4; k < 10; k++) assert(h.engine.key_event(k, true) == Result::ACCEPTED);
  assert(h.engine.key_event(10, true) == Result::QUEUE_FULL);
  h.run();
  assert(h.reports.back().keys[5] == 9);

  // Invalid usages.
  assert(h.engine.key_event(0x01, true) == Result::INVALID_INPUT);
  assert(h.engine.key_event(0xe8, true) == Result::INVALID_INPUT);

  // A queued tap releases live keys (baseline neutral) and blocks live events while running.
  assert(h.engine.submit_key(Operation::TAP, "ENTER", 0) == Result::ACCEPTED);
  assert(h.engine.key_event(0x04, true) == Result::QUEUE_FULL);
  h.reports.clear();
  h.run(200);
  assert(h.reports.front() == Report{});
  assert(h.engine.key_event(0x04, true) == Result::ACCEPTED);  // Idle again.

  // cancel() (release_all) drops live state and sends neutral.
  h.engine.cancel();
  h.reports.clear();
  h.run();
  assert(h.reports.back() == Report{});
  std::cout << "live keys ok\n";
}
