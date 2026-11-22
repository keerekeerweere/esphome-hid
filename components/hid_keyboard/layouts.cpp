#include "layouts.h"

namespace esphome::hid_keyboard {
namespace {

constexpr uint32_t DEAD = 0x80000000U;
constexpr uint8_t SHIFT = 0x02, ALTGR = 0x40;

// One physical key (USB usage code) and what it types unshifted, shifted and
// with AltGr on the layout. 0 = nothing. DEAD marks a dead key.
struct Row {
  uint8_t code;
  uint32_t normal, shifted, altgr;
};

// Letters not listed fall back to the US position (see layout_keys).
constexpr Row UK[] = {
    {0x04, 'a', 'A', U'á'}, {0x08, 'e', 'E', U'é'}, {0x0c, 'i', 'I', U'í'}, {0x12, 'o', 'O', U'ó'},
    {0x18, 'u', 'U', U'ú'}, {0x1e, '1', '!', 0},    {0x1f, '2', '"', 0},    {0x20, '3', U'£', 0},
    {0x21, '4', '$', U'€'}, {0x22, '5', '%', 0},    {0x23, '6', '^', 0},    {0x24, '7', '&', 0},
    {0x25, '8', '*', 0},    {0x26, '9', '(', 0},    {0x27, '0', ')', 0},    {0x2d, '-', '_', 0},
    {0x2e, '=', '+', 0},    {0x2f, '[', '{', 0},    {0x30, ']', '}', 0},    {0x32, '#', '~', 0},
    {0x33, ';', ':', 0},    {0x34, '\'', '@', 0},   {0x35, '`', U'¬', U'¦'}, {0x36, ',', '<', 0},
    {0x37, '.', '>', 0},    {0x38, '/', '?', 0},    {0x64, '\\', '|', 0},
};

constexpr Row DE[] = {
    {0x08, 'e', 'E', U'€'},  {0x10, 'm', 'M', U'µ'},  {0x14, 'q', 'Q', '@'},
    {0x1c, 'z', 'Z', 0},     {0x1d, 'y', 'Y', 0},     {0x1e, '1', '!', 0},
    {0x1f, '2', '"', U'²'},  {0x20, '3', U'§', U'³'}, {0x21, '4', '$', 0},
    {0x22, '5', '%', 0},     {0x23, '6', '&', 0},     {0x24, '7', '/', '{'},
    {0x25, '8', '(', '['},   {0x26, '9', ')', ']'},   {0x27, '0', '=', '}'},
    {0x2d, U'ß', '?', '\\'}, {0x2e, DEAD | U'´', DEAD | '`', 0}, {0x2f, U'ü', U'Ü', 0},
    {0x30, '+', '*', '~'},   {0x32, '#', '\'', 0},    {0x33, U'ö', U'Ö', 0},
    {0x34, U'ä', U'Ä', 0},   {0x35, DEAD | '^', U'°', 0}, {0x36, ',', ';', 0},
    {0x37, '.', ':', 0},     {0x38, '-', '_', 0},     {0x64, '<', '>', '|'},
};

constexpr Row FR[] = {
    {0x04, 'q', 'Q', 0},    {0x08, 'e', 'E', U'€'}, {0x10, ',', '?', 0},
    {0x14, 'a', 'A', 0},    {0x1a, 'z', 'Z', 0},    {0x1d, 'w', 'W', 0},
    {0x1e, '&', '1', 0},    {0x1f, U'é', '2', '~'}, {0x20, '"', '3', '#'},
    {0x21, '\'', '4', '{'}, {0x22, '(', '5', '['},  {0x23, '-', '6', '|'},
    {0x24, U'è', '7', '`'}, {0x25, '_', '8', '\\'}, {0x26, U'ç', '9', '^'},
    {0x27, U'à', '0', '@'}, {0x2d, ')', U'°', ']'}, {0x2e, '=', '+', '}'},
    {0x2f, DEAD | '^', DEAD | U'¨', 0}, {0x30, '$', U'£', U'¤'}, {0x32, '*', U'µ', 0},
    {0x33, 'm', 'M', 0},    {0x34, U'ù', '%', 0},   {0x35, U'²', 0, 0},
    {0x36, ';', '.', 0},    {0x37, ':', '/', 0},    {0x38, '!', U'§', 0},
    {0x64, '<', '>', 0},
};

constexpr Row BE[] = {
    {0x04, 'q', 'Q', 0},    {0x08, 'e', 'E', U'€'}, {0x10, ',', '?', 0},
    {0x14, 'a', 'A', 0},    {0x1a, 'z', 'Z', 0},    {0x1d, 'w', 'W', 0},
    {0x1e, '&', '1', '|'},  {0x1f, U'é', '2', '@'}, {0x20, '"', '3', '#'},
    {0x21, '\'', '4', 0},   {0x22, '(', '5', 0},    {0x23, U'§', '6', '^'},
    {0x24, U'è', '7', 0},   {0x25, '!', '8', 0},    {0x26, U'ç', '9', '{'},
    {0x27, U'à', '0', '}'}, {0x2d, ')', U'°', 0},   {0x2e, '-', '_', 0},
    {0x2f, DEAD | '^', DEAD | U'¨', '['}, {0x30, '$', '*', ']'}, {0x32, U'µ', U'£', DEAD | '`'},
    {0x33, 'm', 'M', 0},    {0x34, U'ù', '%', DEAD | U'´'}, {0x35, U'²', U'³', 0},
    {0x36, ';', '.', 0},    {0x37, ':', '/', 0},    {0x38, '=', '+', '~'},
    {0x64, '<', '>', '\\'},
};

struct Table {
  const Row *rows;
  size_t size;
};

Table table_for(Layout layout) {
  switch (layout) {
    case Layout::QWERTY_UK: return {UK, sizeof(UK) / sizeof(UK[0])};
    case Layout::QWERTZ_DE: return {DE, sizeof(DE) / sizeof(DE[0])};
    case Layout::AZERTY_FR: return {FR, sizeof(FR) / sizeof(FR[0])};
    case Layout::AZERTY_BE: return {BE, sizeof(BE) / sizeof(BE[0])};
    default: return {nullptr, 0};
  }
}

// Caps Lock inverts Shift for keys that type letters.
bool is_letter(uint32_t c) {
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return true;
  switch (c) {
    case U'é': case U'è': case U'à': case U'ç': case U'ù': case U'ü': case U'ö': case U'ä':
    case U'á': case U'í': case U'ó': case U'ú': case U'Ü': case U'Ö': case U'Ä':
      return true;
    default:
      return false;
  }
}

}  // namespace

const char *layout_name(Layout layout) {
  switch (layout) {
    case Layout::QWERTY_UK: return "QWERTY_UK";
    case Layout::AZERTY_BE: return "AZERTY_BE";
    case Layout::AZERTY_FR: return "AZERTY_FR";
    case Layout::QWERTZ_DE: return "QWERTZ_DE";
    default: return "QWERTY_US";
  }
}

bool layout_from_name(std::string_view name, Layout &layout) {
  for (size_t i = 0; i < LAYOUT_COUNT; i++) {
    if (name == layout_name(static_cast<Layout>(i))) {
      layout = static_cast<Layout>(i);
      return true;
    }
  }
  return false;
}

bool layout_keys(Layout layout, uint32_t cp, Key out[2], size_t &count) {
  count = 0;
  Key key;
  if (cp == ' ' || cp == '\n' || cp == '\t' || layout == Layout::QWERTY_US) {
    if (cp > 0x7f || !ascii_key(static_cast<uint8_t>(cp), key)) return false;
    out[count++] = key;
    return true;
  }
  const Table table = table_for(layout);
  const Row *dead = nullptr;
  uint8_t dead_mods = 0;
  static constexpr uint8_t mods[3] = {0, SHIFT, ALTGR};
  for (size_t i = 0; i < table.size; i++) {
    const Row &row = table.rows[i];
    const uint32_t columns[3] = {row.normal, row.shifted, row.altgr};
    for (int c = 0; c < 3; c++) {
      if (columns[c] == 0 || (columns[c] & ~DEAD) != cp) continue;
      if (columns[c] & DEAD) {
        if (!dead) {  // Prefer a direct key; use the first dead key only as a fallback.
          dead = &row;
          dead_mods = mods[c];
        }
        continue;
      }
      out[count++] = {row.code, mods[c], is_letter(cp)};
      return true;
    }
  }
  if (dead) {
    out[count++] = {dead->code, dead_mods, false};
    out[count++] = {0x2c, 0, false};  // Space commits the accent as a plain character.
    return true;
  }
  // Letters that sit on the same position as on a US keyboard.
  if (((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) && ascii_key(static_cast<uint8_t>(cp), key)) {
    out[count++] = key;
    return true;
  }
  return false;
}

bool decode_utf8(std::string_view text, size_t &pos, uint32_t &cp) {
  const uint8_t lead = static_cast<uint8_t>(text[pos]);
  size_t extra;
  if (lead < 0x80)
    extra = 0;
  else if ((lead >> 5) == 0x6)
    extra = 1;
  else if ((lead >> 4) == 0xe)
    extra = 2;
  else if ((lead >> 3) == 0x1e)
    extra = 3;
  else
    return false;
  if (pos + extra >= text.size()) return false;
  cp = extra == 0 ? lead : lead & (0x3fU >> extra);
  for (size_t i = 1; i <= extra; i++) {
    const uint8_t next = static_cast<uint8_t>(text[pos + i]);
    if ((next & 0xc0) != 0x80) return false;
    cp = (cp << 6) | (next & 0x3f);
  }
  static constexpr uint32_t minimum[] = {0, 0x80, 0x800, 0x10000};
  if (cp < minimum[extra] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
  pos += extra + 1;
  return true;
}

}  // namespace esphome::hid_keyboard
