// Laptop check: asserts on the pure helpers (src/util.h, presets, string lengths), then renders
// every screen of the real UI (src/ui.cpp) from a fake node to PNGs, drawn with M5GFX's own
// 6x8 font. Run after `pio run` (it fetches M5GFX):
//   c++ -std=c++17 -O1 -Isrc -I.pio/libdeps/cardcore/M5GFX/src/lgfx/Fonts test/host.cpp -lz -o /tmp/cc_host && /tmp/cc_host docs
#include <zlib.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#define PROGMEM
#include "glcdfont.h"  // const unsigned char font[]: 5 columns per glyph, LSB on top

static uint32_t nowMs = 100000;
static uint32_t millis() { return nowMs; }

// The canvas: RGB565 framebuffer with the subset of LovyanGFX ui.cpp draws with (CP437 glyphs).
struct M5Canvas {
  uint16_t px[135][240] = {}, fg = 0xFFFF;
  int size = 1;
  void pushSprite(int, int) {}
  void setTextSize(int s) { size = s; }
  void setTextColor(uint16_t c) { fg = c; }
  int cx0 = 0, cy0 = 0, cx1 = 240, cy1 = 135;  // clip rect
  void setClipRect(int x, int y, int w, int h) { cx0 = x, cy0 = y, cx1 = x + w, cy1 = y + h; }
  void clearClipRect() { cx0 = 0, cy0 = 0, cx1 = 240, cy1 = 135; }
  void drawPixel(int x, int y, uint16_t c) { if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) px[y][x] = c; }
  void fillRect(int x, int y, int w, int h, uint16_t c) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) drawPixel(x + i, y + j, c); }
  void fillSprite(uint16_t c) { fillRect(0, 0, 240, 135, c); }
  void drawFastHLine(int x, int y, int w, uint16_t c) { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { fillRect(x, y, 1, h, c); }
  void drawLine(int x0, int y0, int x1, int y1, uint16_t c) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (drawPixel(x0, y0, c); x0 != x1 || y0 != y1; drawPixel(x0, y0, c)) {
      int e2 = 2 * e;
      if (e2 >= dy) e += dy, x0 += sx;
      if (e2 <= dx) e += dx, y0 += sy;
    }
  }
  void drawRect(int x, int y, int w, int h, uint16_t c) { fillRect(x, y, w, 1, c); fillRect(x, y + h - 1, w, 1, c); fillRect(x, y, 1, h, c); fillRect(x + w - 1, y, 1, h, c); }
  void fillRoundRect(int x, int y, int w, int h, int, uint16_t c) { fillRect(x + 1, y, w - 2, h, c); fillRect(x, y + 1, w, h - 2, c); }
  void fillCircle(int x, int y, int r, uint16_t c) { for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) drawPixel(x + i, y + j, c); }
  int textWidth(const char* s) { return (int)strlen(s) * 6 * size; }
  void drawString(const char* s, int x, int y) {
    for (; *s; s++, x += 6 * size)
      for (int col = 0; col < 5; col++)
        for (int row = 0; row < 8; row++)
          if (font[(uint8_t)*s * 5 + col] >> row & 1) fillRect(x + col * size, y + row * size, size, size, fg);
  }
  void drawCenterString(const char* s, int x, int y) { drawString(s, x - textWidth(s) / 2, y); }
  void drawRightString(const char* s, int x, int y) { drawString(s, x - textWidth(s), y); }
};
static M5Canvas cv;

// ── fake node ────────────────────────────────────────────────────────────────
#include "app.h"
static UiPrefs uiPrefs = {0, 3, 2, 0, 1, 2, 1, 1, 1, 1};
UiPrefs& app_ui() { return uiPrefs; }
void app_ui_save() {}
void app_apply_preset(int) {}
#include "i18n.h"
#include "presets.h"
static const uint32_t NOW = 1792332000;  // Sun 2026-10-18 14:00 UTC

static ChatId cid(uint8_t kind, uint8_t k) { ChatId c = {kind, {k, 1, 2, 3, 4, 5}}; return c; }
static Chat CHATS[] = {
    {cid(CK_PUBLIC, 1), "Public", 3, NOW - 120},
    {cid(CK_HASHTAG, 2), "#test", 0, NOW - 3600},
    {cid(CK_HASHTAG, 3), "#meshcore", 12, NOW - 300},
    {cid(CK_PRIVATE, 4), "Família", 0, NOW - 86400 * 2},
    {cid(CK_DIRECT, 5), "Jordi T-Deck", 1, NOW - 60},
    {cid(CK_ROOM, 6), "Sala BCN", 0, NOW - 7200},
};
int app_chats(Chat* out, int max, int f) {
  int n = 0;
  for (auto& c : CHATS)
    if (n < max && (f == CF_ALL || (f == CF_CHANNELS) == (c.id.kind < CK_DIRECT))) out[n++] = c;
  return n;
}
static Msg M(uint32_t ago, bool out, uint8_t st, int hops, int snr4, const char* from, const char* text) {
  Msg m = {};
  m.ts = NOW - ago, m.out = out, m.status = st, m.hops = hops, m.snr4 = snr4;
  snprintf(m.from, sizeof m.from, "%s", from);
  snprintf(m.text, sizeof m.text, "%s", text);
  return m;
}
int app_load(const ChatId& id, Msg* out, int max) {
  Msg all[] = {M(900, 0, 0, 3, 22, "Marta", "Bon dia! Algú m'escolta des del Tibidabo?"),
               M(840, 0, 0, 2, 14, "Pere", "Sí, et rebo per 2 salts. Senyal força bo."),
               M(600, 1, MS_ACKED, -1, 0, "cardcore", "Hola des de la Cardputer ADV!"),
               M(420, 0, 0, 4, -18, "Anna", "Prova de cobertura a Collserola, quin SNR teniu?"),
               M(300, 1, MS_SENT, -1, 0, "cardcore", "+5.2 dB aquí, 3 salts"),
               M(120, 0, 0, 1, 30, "Jordi", "Perfecte. Ens veiem dissabte a la trobada."),
               M(20, 1, MS_PENDING, -1, 0, "cardcore", "Allà serem!")};
  int n = std::min<int>(max, sizeof all / sizeof *all);
  memcpy(out, all, n * sizeof *out);
  return n;
}
bool app_send(const ChatId&, const char*) { return true; }
int app_text_limit(const ChatId& id) { return id.kind >= CK_DIRECT ? 150 : 150; }
void app_open_chat(const ChatId*) {}
const char* app_chat_name(const ChatId& id) {
  for (auto& c : CHATS) if (c.id == id) return c.name;
  return "?";
}
int app_total_unread() { return 16; }
int app_channel_count() { return 4; }
bool app_channel(int i, Chat& o) { if (i > 3) return false; o = CHATS[i]; return true; }
bool app_add_hashtag(const char*) { return true; }
bool app_add_private(const char*, const char*) { return true; }
bool app_remove_channel(int) { return true; }
void app_channel_key(int i, char hex[33]) { snprintf(hex, 33, "%s", i ? "9cd8fcf22a47333b591d96a2b848b73f" : "8b3387e9c5cdea6ac9e5edbaa115cd72"); }

static Contact C(const char* name, uint8_t type, int hops, uint32_t ago, double lat = 0, double lon = 0, bool fav = false) {
  Contact c = {};
  for (int i = 0; i < 32; i++) c.key[i] = (uint8_t)(name[i % strlen(name)] * 7 + i);
  snprintf(c.name, sizeof c.name, "%s", name);
  c.type = type, c.hops = hops, c.hashSize = 1, c.lastHeard = NOW - ago, c.fav = fav;
  for (int i = 0; i < hops; i++) c.path[i] = 0x3a + i * 17;
  c.lat = (int32_t)(lat * 1e6), c.lon = (int32_t)(lon * 1e6);
  return c;
}
static Contact CONTACTS[] = {
    C("Jordi T-Deck", CT_CHAT, 1, 60, 41.4036, 2.1744, true), C("RPT Collserola", CT_REPEATER, 0, 300, 41.4180, 2.1140),
    C("RPT Montjuïc", CT_REPEATER, 2, 900, 41.3640, 2.1580), C("Sala BCN", CT_ROOM, 1, 1800, 41.3870, 2.1700),
    C("Marta", CT_CHAT, 3, 3600), C("Sensor Garraf", CT_SENSOR, 4, 7200, 41.2500, 1.8900),
    C("Anna Heltec V4", CT_CHAT, -1, 86400), C("RPT Tibidabo", CT_REPEATER, 1, 120, 41.4220, 2.1190),
    C("Pere", CT_CHAT, 2, 4000)};
constexpr int NC = sizeof CONTACTS / sizeof *CONTACTS;
int app_contact_count(int f) { int n = 0; for (auto& c : CONTACTS) n += !f || c.type == f; return n; }
bool app_contact(int i, int f, Contact& o) {
  for (auto& c : CONTACTS)
    if ((!f || c.type == f) && i-- == 0) return o = c, true;
  return false;
}
bool app_contact_by_key(const uint8_t* k, Contact& o) {
  for (auto& c : CONTACTS) if (!memcmp(c.key, k, 6)) return o = c, true;
  return false;
}
bool app_contact_action(const uint8_t*, int, const char*) { return true; }
static Reply rep = {1, false, true, "STATUS RPT Collserola", 8,
                    {"batt 4.11V  queue 0", "noise -114  rssi -71  snr +7.5", "rx 18342  tx 9120  err 3",
                     "flood tx/rx 8410/16200", "direct tx/rx 710/2142", "dups flood/direct 5121/88", "air tx 312m  rx 1104m",
                     "up 12d 04:31"}};
const Reply& app_reply() { return rep; }
void app_reply_show(const char*, int, const char* const*) {}
int app_heard(Heard* out, int max) {
  Heard h[] = {{{1}, "RPT Tibidabo", CT_REPEATER, -1, 30, 99000, true}, {{2}, "Jordi T-Deck", CT_CHAT, 1, 12, 95000, true},
               {{3}, "", CT_REPEATER, 3, -34, 80000, false}, {{4}, "RPT Collserola", CT_REPEATER, 0, 22, 60000, true},
               {{5}, "Nou node Gràcia", CT_CHAT, 5, -10, 20000, false}};
  int n = std::min<int>(max, 5);
  memcpy(out, h, n * sizeof *out);
  out[2].key[1] = 0xa7, out[2].key[2] = 0x3f;
  return n;
}
void app_advert(bool) {}
void app_discover() {}
void app_status(Status& s) {
  s = {};
  s.battMv = 3980, s.battPct = 78, s.bleOn = true, s.bleConn = true, s.gpsOn = true, s.gpsFix = true, s.sd = true;
  s.timeOk = true, s.now = NOW, s.tz = 2, s.pin = 482913;
  snprintf(s.preset, sizeof s.preset, "Catalunya");
  s.freq = 869.618f, s.bw = 62.5f, s.sf = 7, s.cr = 6, s.txPower = 22;
}
const char* app_node_name() { return "EA3 Cardputer"; }
void app_pubkey_hex(char out[65]) { snprintf(out, 65, "%064d", 0); }
void app_metrics(Metrics& m) {
  m = {};
  m.rx = 1843, m.tx = 212, m.rxErr = 9, m.floodTx = 180, m.floodRx = 1502, m.directTx = 32, m.directRx = 341;
  m.airTxMs = 41200, m.airRxMs = 388000, m.uptimeS = 3 * 86400 + 5 * 3600 + 17 * 60, m.duty1h = 1.37f;
  m.noise = -112, m.lastRssi = -87, m.lastSnr = 6.25f, m.heap = 71200, m.heapMin = 52300, m.queue = 4, m.contacts = 9, m.channels = 4;
  for (int i = 0; i < HIST; i++) {
    m.rxHist[i] = (uint8_t)(2 + 3 * sinf(i * 0.4f) * sinf(i * 0.13f) + (i % 7 == 0) * 4 + (i > 45) * 3);
    if (m.rxHist[i] > 200) m.rxHist[i] = 0;
    m.noiseHist[i] = (int8_t)(-113 + 3 * sinf(i * 0.3f) + (i % 11 == 0) * 6);
  }
}
int app_rx_log(RxLog* out, int max) {
  int n = std::min(max, 8);
  for (int i = 0; i < n; i++) out[i] = {nowMs - i * 7300 - 2000, (int16_t)(-70 - i * 6), (int8_t)(28 - i * 9)};
  return n;
}
void app_gps(Gps& g) { g = {true, true, 9, 41.39720, 2.16050, 48, true, 41.39720, 2.16050}; }
void app_gps_toggle() {}
bool app_gps_to_node() { return true; }
static const struct { uint16_t l; uint8_t k; const char* v; } SET[] = {
    {S_RADIO, SK_SECTION, ""}, {S_TEMPLATE, SK_ENUM, "Catalunya"}, {S_FREQ, SK_TEXT, "869.618 MHz"},
    {S_BW, SK_ENUM, "62.5 kHz"}, {S_SF, SK_ENUM, "SF7"}, {S_CR, SK_ENUM, "4/6"}, {S_TXPOWER, SK_ENUM, "22 dBm"},
    {S_PATHHASH, SK_ENUM, "1 B"}, {S_RXBOOST, SK_ENUM, "ON"}, {S_SCOPE, SK_TEXT, "none"},
    {S_RELAY, SK_ENUM, "needs 869.495"}, {S_NODE, SK_SECTION, ""}, {S_NAME, SK_TEXT, "EA3 Cardputer"},
    {S_LOCATION, SK_TEXT, "41.3972,2.1605"}, {S_SHARELOC, SK_ENUM, "OFF"}, {S_ADVERT, SK_ACTION, ">"},
    {S_DISPLAY, SK_SECTION, ""}, {S_THEME, SK_ENUM, "GREEN"}, {S_LANGUAGE, SK_ENUM, "ENGLISH"},
    {S_SYSTEM, SK_SECTION, ""}, {S_REBOOT, SK_DANGER, "!"}, {S_FACTORY, SK_DANGER, "!"}};
constexpr int NSET = sizeof SET / sizeof *SET;
int app_setting_count() { return NSET; }
int app_setting_find(uint16_t l) { for (int i = 0; i < NSET; i++) if (SET[i].l == l) return i; return -1; }
uint16_t app_setting_label(int i) { return SET[i].l; }
uint8_t app_setting_kind(int i) { return SET[i].k; }
void app_setting_value(int i, char* o, int cap) { snprintf(o, cap, "%s", SET[i].v); }
void app_setting_adjust(int, int) {}
void app_setting_text(int i, char* o, int cap) { snprintf(o, cap, "%s", SET[i].v); }
bool app_setting_set_text(int, const char*) { return true; }
int app_setting_action(int) { return AR_DONE; }
static Event evq[2];
static int nev = 0;
bool app_event(Event& e) { if (!nev) return false; e = evq[--nev]; return true; }
int app_sos(bool) { return 2; }
void app_begin() {}
void app_loop() {}

#define CARDCORE_HOST
#define CARDCORE_VERSION "v0.1"
#include "../src/ui.cpp"

// ── checks ───────────────────────────────────────────────────────────────────
static int cols(const char* s) { char b[256]; return utf8ToCp437(s, b, sizeof b); }
static void checks() {
  char b[64];
  utf8ToCp437("Cañón àèòç l·l €?", b, sizeof b);
  assert(!strcmp(b, "Ca\xa4\xa2n \x85\x8a\x95\x87 l\xfal E?"));
  utf8ToCp437("hi 👋\xef\xb8\x8f!", b, sizeof b);  // emoji + variation selector: one '?'
  assert(!strcmp(b, "hi ?!"));
  utf8ToCp437("\xc3", b, sizeof b);  // truncated sequence
  assert(!strcmp(b, "?"));

  int st[8], ln[8];
  int n = wrapLines("hello wonderful world of mesh", 12, st, ln, 8);
  assert(n == 4 && st[0] == 0 && ln[0] == 5 && ln[1] == 9 && ln[2] == 8 && ln[3] == 4);  // breaks at spaces
  n = wrapLines("abcdefghijklmnop", 5, st, ln, 8);  // a long word is cut hard
  assert(n == 4 && ln[3] == 1);

  Msg m = {}, r = {};
  m.ts = 1792332000, m.out = 1, m.status = MS_PENDING, m.hops = -1, m.snr4 = -7;
  snprintf(m.from, sizeof m.from, "Jordi");
  snprintf(m.text, sizeof m.text, "tab\there, newline\nthere");
  char line[300];
  formatMsgLine(m, line, sizeof line);
  line[strlen(line) - 1] = 0;
  uint32_t ts;
  assert(parseMsgLine(line, r, ts) == 'M' && r.ts == m.ts && r.out == 1 && r.status == MS_PENDING && r.hops == -1 &&
         r.snr4 == -7 && !strcmp(r.from, "Jordi") && !strcmp(r.text, "tab here, newline there"));
  char k[] = "K\t1792332000";
  assert(parseMsgLine(k, r, ts) == 'K' && ts == 1792332000);
  char bad[] = "garbage";
  assert(parseMsgLine(bad, r, ts) == 0);

  uint8_t key[16];
  assert(parseHexKey("8B3387E9C5CDEA6AC9E5EDBAA115CD72", key) && key[0] == 0x8b && key[15] == 0x72);
  assert(!parseHexKey("8b33", key) && !parseHexKey("zz3387e9c5cdea6ac9e5edbaa115cd72", key));

  // CayenneLPP: voltage 4.12 V, temperature -3.5 C, GPS
  const uint8_t lpp[] = {1, 116, 0x01, 0x9C, 2, 103, 0xFF, 0xDD, 3, 136, 0x06, 0x50, 0xA4, 0x00, 0x54, 0x6A, 0x00, 0x12, 0xC0};
  char L[9][40];
  n = lppFormat(lpp, sizeof lpp, L, 9);
  assert(n == 3 && !strcmp(L[0], "ch1 voltage 4.12 V") && !strcmp(L[1], "ch2 temp -3.5 C"));
  assert(!strcmp(L[2], "ch3 41.3860,2.1610 48m"));
  const uint8_t trunc[] = {1, 116, 0x01};
  assert(lppFormat(trunc, sizeof trunc, L, 9) == 0);

  int y, mo, dd;  // calendar and EU summer time (2026: 29 March - 25 October)
  assert(daysFromCivil(1970, 1, 1) == 0 && daysFromCivil(2026, 10, 18) * 86400u == NOW - 14 * 3600);
  civilFromDays(daysFromCivil(2024, 2, 29), y, mo, dd);
  assert(y == 2024 && mo == 2 && dd == 29);
  uint32_t mar = daysFromCivil(2026, 3, 29) * 86400u + 3600, oct = daysFromCivil(2026, 10, 25) * 86400u + 3600;
  assert(lastSunday0100(2026, 3) == mar && lastSunday0100(2026, 10) == oct);
  assert(!euSummer(mar - 1) && euSummer(mar) && euSummer(oct - 1) && !euSummer(oct) && !euSummer(NOW + 86400 * 60));
  {  // charging guess: plug in at 3.80 V, charge, unplug; then a full battery on power, unplugged
    ChargeGuess g;
    auto feed = [&](int mv, int k) { for (int i = 0; i < k; i++) g.add(mv + (i % 3) * 8 - 8); return g.on; };  // +-8 mV noise
    assert(!feed(3800, 20));
    assert(feed(3900, 3) && feed(3910, 40));  // jump, then stays on while it climbs slowly
    assert(!feed(3830, 20));                  // unplugged
    assert(feed(4200, 20));                   // full on power
    assert(!feed(4140, 20));                  // unplugged: sags
    assert(!feed(4100, 40));
  }

  assert(isSos("SOS! 41.41387,2.16082") && !isSos("SOS"));

  float bear;
  float d = geoDistKm(41.3874, 2.1686, 41.4036, 2.1744, &bear);  // Pl. Catalunya -> Sagrada Familia
  assert(d > 1.7f && d < 2.0f && bear > 10 && bear < 30 && !strcmp(compass(bear), "N"));

  assert(!strcmp(PRESETS[0].name, "Catalunya") && PRESETS[0].freq == 869.618f && PRESETS[0].bw == 62.5f &&
         PRESETS[0].sf == 7 && PRESETS[0].cr == 6);
  for (auto& p : PRESETS) assert(p.freq > 860 && p.freq < 930 && strlen(p.name) <= 22 && p.hash >= 1 && p.hash <= 3);
  assert(PRESETS[0].hash == 2 && presetMatch(869.618f, 62.5f, 7, 6, 2) == 0 && presetMatch(869.6f, 62.5f, 7, 6, 2) == -1);

  // every translation fits its slot
  for (int lang = 0; lang < 3; lang++) {
    uiPrefs.lang = lang;
    for (int id = S_TEMPLATE; id <= S_FACTORY; id++) assert(cols(tr(id)) <= 16 || !fprintf(stderr, "label: %s\n", tr(id)));
    for (int id = S_H_HOME; id <= S_H_CONFIRM; id++) assert(cols(tr(id)) <= (id == S_H_CHAT ? 31 : 39) || !fprintf(stderr, "hint: %s\n", tr(id)));
    for (int id = S_K_PAGE; id <= S_K_SOS; id++) assert(cols(tr(id)) <= 14 || !fprintf(stderr, "help: %s\n", tr(id)));
    for (int id = S_HOME; id <= S_MAP; id++) assert(cols(tr(id)) <= 9 || !fprintf(stderr, "page: %s\n", tr(id)));
    for (int id = S_A_MESSAGE; id <= S_A_DELETE; id++) assert(cols(tr(id)) <= 17 || !fprintf(stderr, "action: %s\n", tr(id)));
    for (int id : {S_W_LOC_GPS, S_W_LOC_TYPE, S_W_SKIP}) assert(cols(tr(id)) <= 23 || !fprintf(stderr, "wizard: %s\n", tr(id)));
    for (int id : {S_W_BLE_ON, S_W_BLE_OFF, S_W_NAME, S_W_LOC_INPUT}) assert(cols(tr(id)) <= 35 || !fprintf(stderr, "wizard: %s\n", tr(id)));
    for (int id : {S_W_WAIT_FIX, S_W_ADVERT, S_W_WELCOME}) assert(cols(tr(id)) <= 39 || !fprintf(stderr, "wizard: %s\n", tr(id)));
    assert(cols(tr(S_SETUP)) <= 9);
    for (int id = S_SOS_ASK; id <= S_SOS_HINT; id++) assert(cols(tr(id)) <= 39 || !fprintf(stderr, "toast: %s\n", tr(id)));
  }
  uiPrefs.lang = 0;
  puts("checks ok");
}

// ── screenshots ──────────────────────────────────────────────────────────────
static void savePng(const char* path) {  // 3x nearest-neighbour, RGB565 -> RGB888
  const int S = 3, w = 240 * S, h = 135 * S;
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    for (int x = 0; x < w; x++) {
      uint16_t c = cv.px[y / S][x / S];
      raw.push_back((c >> 11) * 255 / 31);
      raw.push_back((c >> 5 & 63) * 255 / 63);
      raw.push_back((c & 31) * 255 / 31);
    }
  }
  uLongf n = compressBound(raw.size());
  std::vector<uint8_t> z(n);
  compress2(z.data(), &n, raw.data(), raw.size(), 9);
  FILE* f = fopen(path, "wb");
  auto u32 = [&](uint32_t v) { uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v}; fwrite(b, 1, 4, f); };
  auto chunk = [&](const char* type, const uint8_t* d, uint32_t len) {
    u32(len);
    fwrite(type, 1, 4, f);
    if (len) fwrite(d, 1, len, f);
    u32(crc32(crc32(0, (const Bytef*)type, 4), d, len));
  };
  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
  uint8_t ihdr[13] = {0, 0, (uint8_t)(w >> 8), (uint8_t)w, 0, 0, (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0};
  chunk("IHDR", ihdr, 13);
  chunk("IDAT", z.data(), n);
  chunk("IEND", nullptr, 0);
  fclose(f);
  printf("wrote %s\n", path);
}

int main(int argc, char** argv) {
  checks();
  const char* dir = argc > 1 ? argv[1] : ".";
  char path[256];
  auto shot = [&](const char* name) {
    ui_host_draw();
    snprintf(path, sizeof path, "%s/%s.png", dir, name);
    savePng(path);
  };
  auto key = [](int c) { ui_host_key(c); };
  applyTheme();
  ui_host_recent(cid(CK_DIRECT, 5), "Jordi T-Deck: Perfecte. Ens veiem dissabte", nowMs - 60000);
  ui_host_recent(cid(CK_HASHTAG, 3), "#meshcore Anna: quin SNR teniu?", nowMs - 300000);
  ui_host_recent(cid(CK_PUBLIC, 1), "Public Pere: et rebo per 2 salts", nowMs - 840000);
  shot("home");
  key('2'), shot("chats");
  key(K_DOWN), key(K_DOWN), key('\n');
  for (const char* p = "Fins dissabte!"; *p; p++) key(*p);
  shot("chat");
  key(K_ESC), ui_host_open_chat(cid(CK_DIRECT, 5));
  nowMs += 500, ui_host_sos(cid(CK_DIRECT, 5), "Jordi T-Deck", "41.40360,2.17440"), shot("sos");
  nowMs -= 500, key(K_ESC);
  key(K_ESC), key('3'), shot("contacts");
  key(K_DOWN), key('\n'), shot("contact");
  key(K_DOWN), key(K_DOWN), overlay = OV_REPLY, shot("status");
  overlay = OV_NONE, key(K_ESC), key('4'), shot("heard");
  key('5'), shot("metrics");
  key(K_RIGHT), shot("metrics_node");
  key(K_RIGHT), shot("metrics_rxlog");
  key('6'), shot("gps");
  key('7'), shot("map_catalonia");
  key('='), shot("map_local");
  key('='), shot("map_street");
  key('-'), key('-'), key('-'), shot("map_europe");
  key('-'), shot("map_world");
  key('='), key('=');
  key('8'), key(K_DOWN), shot("settings");
  screen = SC_CHANNELS, shot("channels");
  key('n');
  for (const char* p = "barcelona"; *p; p++) key(*p);
  shot("new_hashtag");
  key(K_ESC), key(K_ESC), key('i'), shot("help");
  key(' ');
  ui_host_wizard(W_LANG, 1), shot("wizard_1_language");
  uiPrefs.lang = 1;
  ui_host_wizard(W_NAME, 0), shot("wizard_2_name");
  key(K_ESC), shot("wizard_3_template");
  ui_host_wizard(W_LOC, 0), shot("wizard_4_location");
  ui_host_wizard(W_BLE, 0), shot("wizard_5_bluetooth");
  ui_host_wizard(W_DONE, 0), shot("wizard_6_ready");
  key(K_ESC);
  uiPrefs.lang = 1, uiPrefs.theme = 1, applyTheme();
  key('1'), shot("home_ca_amber");
  return 0;
}
