#include "pointer_engine.h"
#include <cassert>
#include <iostream>
using namespace esphome::hid_keyboard;

int main() {
  PointerEngine p;
  assert(p.event(1, 1, 0, 0) == Result::UNAVAILABLE);
  p.set_available(true);

  // Range checks.
  assert(p.event(-1, 0, 0, 0) == Result::INVALID_INPUT);
  assert(p.event(0, 32768, 0, 0) == Result::INVALID_INPUT);
  assert(p.event(0, 0, 32, 0) == Result::INVALID_INPUT);
  assert(p.event(0, 0, 0, 128) == Result::INVALID_INPUT);

  // Flooded moves coalesce: only the latest position survives.
  for (int i = 0; i < 1000; i++) assert(p.event(i, 2 * i, 0, 0) == Result::ACCEPTED);
  PointerReport r;
  assert(p.next_report(r) && r.x == 999 && r.y == 1998 && !p.busy());

  // A click keeps ordering: move, press, release at the same spot, each delivered.
  assert(p.event(100, 200, 0, 0) == Result::ACCEPTED);
  assert(p.event(100, 200, 1, 0) == Result::ACCEPTED);
  assert(p.event(100, 200, 0, 0) == Result::ACCEPTED);
  assert(p.next_report(r) && r.buttons == 0 && r.x == 100);
  assert(p.next_report(r) && r.buttons == 1);
  assert(p.next_report(r) && r.buttons == 0 && !p.busy());

  // Moves while the button is held (drag) coalesce separately from released moves.
  assert(p.event(10, 10, 1, 0) == Result::ACCEPTED);
  assert(p.event(20, 20, 1, 0) == Result::ACCEPTED);
  assert(p.event(30, 30, 1, 0) == Result::ACCEPTED);
  assert(p.event(30, 30, 0, 0) == Result::ACCEPTED);
  assert(p.next_report(r) && r.buttons == 1 && r.x == 30);
  assert(p.next_report(r) && r.buttons == 0 && r.x == 30 && !p.busy());

  // Wheel events are never merged away.
  assert(p.event(5, 5, 0, 3) == Result::ACCEPTED);
  assert(p.event(5, 5, 0, 3) == Result::ACCEPTED);
  assert(p.event(6, 6, 0, 0) == Result::ACCEPTED);
  assert(p.next_report(r) && r.wheel == 3);
  assert(p.next_report(r) && r.wheel == 3);
  assert(p.next_report(r) && r.wheel == 0 && r.x == 6 && !p.busy());

  // Queue full rejects non-mergeable events but never loses a transition silently.
  for (int i = 0; i < 32; i++) assert(p.event(0, 0, i % 2, 0) == Result::ACCEPTED);
  assert(p.event(0, 0, 2, 0) == Result::QUEUE_FULL);
  p.reset();  // Queue cleared; last state had button released (31 odd? i=31 -> held).
  assert(p.busy());
  assert(p.next_report(r) && r.buttons == 0 && !p.busy());

  // cancel() always sends a release at the last position.
  assert(p.event(7, 8, 1, 0) == Result::ACCEPTED);
  p.cancel();
  assert(p.next_report(r) && r.buttons == 0 && r.x == 7 && r.y == 8 && !p.busy());

  // Loss of transport drops everything without a release.
  assert(p.event(1, 1, 1, 0) == Result::ACCEPTED);
  p.set_available(false);
  assert(!p.busy());
  std::cout << "pointer ok\n";
}
