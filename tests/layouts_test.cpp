#include "layouts.h"
#include <cassert>
#include <iostream>

using namespace esphome::hid_keyboard;

static Key one(Layout l, uint32_t cp) {
  Key k[2];
  size_t n = 0;
  assert(layout_keys(l, cp, k, n) && n == 1);
  return k[0];
}

int main() {
  // Every printable ASCII character must be typable on every layout.
  for (size_t i = 0; i < LAYOUT_COUNT; i++)
    for (uint32_t c = 32; c < 127; c++) {
      Key k[2];
      size_t n = 0;
      if (!layout_keys(static_cast<Layout>(i), c, k, n)) {
        std::cerr << layout_name(static_cast<Layout>(i)) << " missing '" << char(c) << "'\n";
        return 1;
      }
    }
  // US unchanged.
  assert(one(Layout::QWERTY_US, 'a').code == 0x04);
  // AZERTY: a is on the US q position; m on the US ; position; digits need Shift.
  assert(one(Layout::AZERTY_BE, 'a').code == 0x14);
  assert(one(Layout::AZERTY_BE, 'm').code == 0x33);
  assert(one(Layout::AZERTY_BE, '1').code == 0x1e && one(Layout::AZERTY_BE, '1').modifiers == 2);
  assert(one(Layout::AZERTY_BE, '@').code == 0x1f && one(Layout::AZERTY_BE, '@').modifiers == 0x40);
  assert(one(Layout::AZERTY_BE, U'é').code == 0x1f && one(Layout::AZERTY_BE, U'é').modifiers == 0);
  assert(one(Layout::AZERTY_FR, 'z').code == 0x1a);
  assert(one(Layout::AZERTY_FR, '@').code == 0x27);
  // QWERTZ: y/z swapped, @ on AltGr+q.
  assert(one(Layout::QWERTZ_DE, 'z').code == 0x1c && one(Layout::QWERTZ_DE, 'y').code == 0x1d);
  assert(one(Layout::QWERTZ_DE, '@').code == 0x14 && one(Layout::QWERTZ_DE, '@').modifiers == 0x40);
  // UK.
  assert(one(Layout::QWERTY_UK, '"').code == 0x1f && one(Layout::QWERTY_UK, '"').modifiers == 2);
  assert(one(Layout::QWERTY_UK, '@').code == 0x34 && one(Layout::QWERTY_UK, '@').modifiers == 2);
  assert(one(Layout::QWERTY_UK, U'£').code == 0x20);
  // Dead key: typed as dead key + Space; direct AltGr ^ preferred on French.
  Key k[2];
  size_t n = 0;
  assert(layout_keys(Layout::QWERTZ_DE, '^', k, n) && n == 2 && k[0].code == 0x35 && k[1].code == 0x2c);
  assert(layout_keys(Layout::AZERTY_FR, '^', k, n) && n == 1 && k[0].code == 0x26 && k[0].modifiers == 0x40);
  // Caps Lock only inverts letters.
  assert(one(Layout::AZERTY_FR, 'm').letter && !one(Layout::AZERTY_FR, '1').letter);
  // Unsupported characters are rejected.
  assert(!layout_keys(Layout::QWERTY_US, U'é', k, n));
  assert(!layout_keys(Layout::AZERTY_BE, U'日', k, n));
  // UTF-8 decoding.
  size_t pos = 0;
  uint32_t cp = 0;
  assert(decode_utf8("\xc3\xa9", pos, cp) && cp == U'é' && pos == 2);
  pos = 0;
  assert(decode_utf8("\xe2\x82\xac", pos, cp) && cp == U'€' && pos == 3);
  pos = 0;
  assert(!decode_utf8("\xc3", pos, cp));
  assert(!decode_utf8("\xc0\x80", pos, cp));
  assert(!decode_utf8("\xed\xa0\x80", pos, cp));
  assert(!decode_utf8("\xff", pos, cp));
  // Names round-trip.
  for (size_t i = 0; i < LAYOUT_COUNT; i++) {
    Layout l;
    assert(layout_from_name(layout_name(static_cast<Layout>(i)), l) && l == static_cast<Layout>(i));
  }
  Layout l;
  assert(!layout_from_name("DVORAK", l));
  std::cout << "layouts ok\n";
}
