#include "mouse_engine.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace esphome::hid_keyboard;

static std::vector<MouseReport> drain(MouseEngine &m, uint32_t &now) {
  std::vector<MouseReport> out;
  MouseReport r;
  for (int guard = 0; guard < 100000 && m.busy(); guard++, now++)
    if (m.next_report(now, r)) out.push_back(r);
  return out;
}

int main() {
  uint32_t now = 1000;
  MouseEngine m;
  m.configure(30, 8);

  // Unavailable until the host is mounted.
  assert(m.move(10, 0, 0) == Result::UNAVAILABLE);
  m.set_available(true);

  // Big moves split into <=127 steps that sum to the request.
  assert(m.move(300, -250, 0) == Result::ACCEPTED);
  auto reports = drain(m, now);
  int sx = 0, sy = 0;
  for (auto &r : reports) {
    assert(r.dx >= -127 && r.dx <= 127 && r.dy >= -127 && r.dy <= 127);
    sx += r.dx;
    sy += r.dy;
  }
  assert(sx == 300 && sy == -250 && reports.size() == 3);

  // Wheel only.
  assert(m.move(0, 0, -3) == Result::ACCEPTED);
  reports = drain(m, now);
  assert(reports.size() == 1 && reports[0].wheel == -3 && reports[0].dx == 0);

  now += 100;
  // Click: press, hold click_ms, release.
  assert(m.click(MOUSE_LEFT) == Result::ACCEPTED);
  MouseReport r;
  assert(m.next_report(now, r) && r.buttons == MOUSE_LEFT && r.delay_ms == 30);
  assert(!m.next_report(now + 29, r));  // Still holding.
  assert(m.next_report(now + 30, r) && r.buttons == 0);
  assert(m.buttons() == 0);
  now += 100;

  // Press/hold drag/release keeps the button in moves.
  assert(m.press(MOUSE_LEFT) == Result::ACCEPTED);
  assert(m.move(5, 5, 0) == Result::ACCEPTED);
  assert(m.release(MOUSE_LEFT) == Result::ACCEPTED);
  reports = drain(m, now);
  assert(reports.size() == 3 && reports[0].buttons == 1 && reports[1].buttons == 1 && reports[1].dx == 5 &&
         reports[2].buttons == 0);

  // Validation.
  assert(m.move(8001, 0, 0) == Result::INVALID_INPUT);
  assert(m.press(0) == Result::INVALID_INPUT);
  assert(m.press(0x20) == Result::INVALID_INPUT);
  assert(m.move(0, 0, 0) == Result::ACCEPTED && !m.busy());

  // All-or-nothing when the queue cannot hold the whole move.
  assert(m.move(8000, 8000, 0) == Result::ACCEPTED);  // 63 steps.
  assert(m.move(127, 0, 0) == Result::ACCEPTED);       // The 64th fills the queue.
  assert(m.move(1, 0, 0) == Result::QUEUE_FULL);
  assert(m.click(MOUSE_RIGHT) == Result::QUEUE_FULL);
  m.cancel();
  assert(m.busy());  // Only the release-all report remains.
  assert(m.next_report(now + 1000, r) && r.buttons == 0 && r.dx == 0 && !m.busy());

  // Transport loss drops everything, and only releases if a button was held.
  assert(m.press(MOUSE_MIDDLE) == Result::ACCEPTED);
  m.set_available(false);
  assert(!m.busy() && m.buttons() == 0);
  m.set_available(true);
  assert(m.press(MOUSE_MIDDLE) == Result::ACCEPTED);
  m.reset();  // Epoch change while connected: held button must be released.
  assert(m.busy() && m.next_report(now + 5000, r) && r.buttons == 0);

  // Button names.
  uint8_t mask = 0;
  assert(mouse_button("left", mask) && mask == MOUSE_LEFT);
  assert(mouse_button("Forward", mask) && mask == MOUSE_FORWARD);
  assert(!mouse_button("center", mask) && !mouse_button("", mask));
  std::cout << "mouse ok\n";
}
