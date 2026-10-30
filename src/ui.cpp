// CARDCORE UI: pages, chat, contacts, metrics, settings on the Cardputer's 240x135 screen.
// Looks and keys follow FRESNELDRONE: phosphor themes, scanlines, header pill, two-line
// footer (status + key hints); ;.,/ are the arrows, ` is back, i is help.
// test/host.cpp compiles this file against a stub canvas to render the screens.
#ifndef CARDCORE_HOST
#include <M5Cardputer.h>
#include "target.h"
#endif
#include "app.h"
#include "i18n.h"
#include "presets.h"
#include "map_world.h"

#ifndef CARDCORE_VERSION
#define CARDCORE_VERSION "dev"
#endif

constexpr int W = 240, H = 135, ROW0 = 15, RH = 11, NROWS = 9;
#ifndef CARDCORE_HOST
static M5Canvas cv(&M5Cardputer.Display);
#endif

// ── phosphor palette (FRESNELDRONE's) ────────────────────────────────────────
static constexpr uint16_t rgb(int r, int g, int b) { return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }
constexpr uint16_t C_BG = 0;
static uint16_t C_SCAN, C_BAR, C_DIM, C_MID, C_HI, C_HOT, C_WARN;
struct Theme { const char* name; uint8_t r, g, b; uint16_t warn; };
static const Theme THEMES[] = {{"GREEN", 0, 255, 90, rgb(255, 176, 0)}, {"AMBER", 255, 200, 0, rgb(255, 70, 40)},
                               {"RED", 255, 40, 30, rgb(255, 200, 0)},  {"BLUE", 40, 140, 255, rgb(255, 176, 0)},
                               {"B&W", 255, 255, 255, rgb(255, 176, 0)}};
constexpr int NTHEME = sizeof THEMES / sizeof *THEMES;
const char* ui_theme_name(int t) { return THEMES[t % NTHEME].name; }
int ui_theme_count() { return NTHEME; }
// Fixed tag colours (no amber: that means "warning").
static const uint16_t PAGE_COLOR[] = {rgb(0, 220, 255), rgb(120, 255, 60), rgb(180, 110, 255), rgb(255, 120, 30),
                                      rgb(255, 70, 170), rgb(255, 225, 40), rgb(120, 230, 200), rgb(60, 140, 255)};
static const uint16_t KIND_COLOR[] = {rgb(120, 255, 60), rgb(0, 220, 255), rgb(255, 70, 170), rgb(255, 225, 40), rgb(180, 110, 255)};
static const uint16_t TYPE_COLOR[] = {0, rgb(120, 255, 60), rgb(0, 220, 255), rgb(180, 110, 255), rgb(255, 225, 40)};

static void applyTheme() {
  const Theme& t = THEMES[app_ui().theme % NTHEME];
  auto c = [&](float k, float w) {
    auto ch = [&](int v) { float x = v * k; return (int)(x + (255 - x) * w); };
    return rgb(ch(t.r), ch(t.g), ch(t.b));
  };
  C_SCAN = c(0.063f, 0), C_BAR = c(0.14f, 0), C_DIM = c(0.31f, 0), C_MID = c(0.65f, 0);
  C_HI = c(1, 0.27f), C_HOT = c(1, 0.82f), C_WARN = t.warn;
}

// ── state ────────────────────────────────────────────────────────────────────
enum Page { P_HOME, P_CHATS, P_CONTACTS, P_HEARD, P_METRICS, P_GPS, P_MAP, P_SETTINGS, NPAGE };
static const uint16_t PAGE_NAME[NPAGE] = {S_HOME, S_CHATS, S_CONTACTS, S_HEARD, S_METRICS, S_GPS, S_MAP, S_SETTINGS};
enum Screen { SC_PAGE, SC_CHAT, SC_CONTACT, SC_CHANNELS, SC_WIZARD };
enum Overlay { OV_NONE, OV_HELP, OV_INPUT, OV_CONFIRM, OV_REPLY };
enum Input { IN_SETTING, IN_LOGIN, IN_HASHTAG, IN_PRIV_NAME, IN_PRIV_KEY, IN_WIZ_NAME, IN_WIZ_LOC };
enum Confirm { CO_SETTING, CO_DEL_CONTACT, CO_DEL_CHANNEL };
enum Key { K_UP = 0x80, K_DOWN, K_LEFT, K_RIGHT, K_ESC = 0x1B };
enum WizStep { W_LANG, W_NAME, W_PRESET, W_LOC, W_BLE, W_DONE, NWIZ };
static int wizStep = 0, wizSel = 0, wizTop = 0;
// map: the cursor is the centre of the view
static const uint32_t MAP_MPP[] = {25, 100, 2500, 25000, 200000};  // m/px: street, local, Catalonia, Europe, world
constexpr int NZOOM = sizeof MAP_MPP / sizeof *MAP_MPP, MCX = 120, MCY = 63;  // map area: y 13..112
static int mapZoom = 2, mapNode = -1;
static double mapLat = 41.70, mapLon = 1.75;  // opens on Catalonia; 200 km/px shows the world
static bool mapInit = false;
static void drawMapStatus();

static uint8_t page = P_HOME, screen = SC_PAGE, overlay = OV_NONE;
static int sel[NPAGE], top[NPAGE];
static int chatFilter = CF_ALL, contactFilter = 0, metricsView = 0, chanSel = 0, chanTop = 0, chanSlot = -1;
static const uint8_t CONTACT_FILTERS[] = {0, CT_CHAT, CT_REPEATER, CT_ROOM, CT_SENSOR};

constexpr int NMSG = 40;
static ChatId chatId;
static Msg msgs[NMSG];
static int nmsg = 0, chatScroll = 0, chatMetaIdx = -1;  // chatMetaIdx: newest message on screen
static char compose[170];
static int composeLen = 0;

static Contact cur;
static int actSel = 0;
static uint8_t acts[9];
static int nacts = 0;

static uint8_t inputKind;
static int inputCtx;
static char inputTitle[40], inputBuf[64], pendingName[32];
static int inputLen = 0, inputMax = 63;
static uint8_t confirmKind;
static int confirmCtx;
static char confirmText[48];

static char toast[64];
static uint32_t toastUntil = 0;
static bool toastWarn = false;
static uint32_t lastKeyAt = 0, ledUntil = 0, discoverUntil = 0;
static bool dark = false, dirty = true;

struct Recent { ChatId chat; char line[64]; uint32_t at; };
static Recent recent[4];
static int nrecent = 0;

static void say(const char* s, bool warn = false) {
  snprintf(toast, sizeof toast, "%s", s);
  toastUntil = millis() + 3500;
  toastWarn = warn;
  dirty = true;
}

// ── hardware bits ────────────────────────────────────────────────────────────
static const uint8_t BRIGHT[] = {20, 50, 90, 150, 255};
static const uint16_t TIMEOUT_S[] = {15, 30, 60, 300, 0};
static void hwBright(int level) {
#ifndef CARDCORE_HOST
  M5Cardputer.Display.setBrightness(level < 0 ? 0 : BRIGHT[level % 5]);
#endif
}
static void hwBeep(bool msg) {
#ifndef CARDCORE_HOST
  if (!app_ui().sound) return;
  M5Cardputer.Speaker.setVolume(40 + app_ui().vol * 50);
  if (msg) {
    M5Cardputer.Speaker.tone(2637, 60);
    delay(70);
    M5Cardputer.Speaker.tone(3520, 80);
  } else {
    M5Cardputer.Speaker.tone(1760, 40);
  }
#endif
}
static void hwLed(uint8_t r, uint8_t g, uint8_t b) {
#ifndef CARDCORE_HOST
  M5.Led.setAllColor(r, g, b);  // M5Unified owns the G21 LED (RMT)
  M5.Led.display();
#endif
}

// ── drawing primitives ───────────────────────────────────────────────────────
static int tlen(const char* s) {  // characters on screen
  char b[192];
  return utf8ToCp437(s, b, sizeof b);
}
static void txt(const char* s, int x, int y, uint16_t c, int align = 0) {  // align: 0 left, 1 right, 2 centre
  char b[192];
  utf8ToCp437(s, b, sizeof b);
  cv.setTextColor(c);
  if (align == 1) cv.drawRightString(b, x, y);
  else if (align == 2) cv.drawCenterString(b, x, y);
  else cv.drawString(b, x, y);
}
static void txtFit(const char* s, int x, int y, uint16_t c, int cols) {  // clip to cols, '~' marks the cut
  char b[192];
  int n = utf8ToCp437(s, b, sizeof b);
  if (n > cols && cols > 0) b[cols - 1] = '~', b[cols] = 0;
  cv.setTextColor(c);
  cv.drawString(b, x, y);
}
static void pill(const char* s, int x, int y, uint16_t bg) {
  int w = tlen(s) * 6 + 6;
  cv.fillRoundRect(x, y, w, 10, 2, bg);
  txt(s, x + 3, y + 1, C_BG);
}
static int pillW(const char* s) { return tlen(s) * 6 + 6; }
static void backdrop() {
  cv.fillSprite(C_BG);
  for (int y = 0; y < H; y += 2) cv.drawFastHLine(0, y, W, C_SCAN);
}
static void selBar(int y) { cv.fillRect(0, y - 2, W, RH, C_BAR); }
static void box(int x, int y, int w, int h) {
  cv.fillRect(x, y, w, h, C_BG);
  cv.drawRect(x, y, w, h, C_MID);
  cv.drawFastHLine(x + 1, y + 11, w - 2, C_DIM);
}
static void meterBar(int x, int y, int w, float v, uint16_t c) {
  cv.drawRect(x, y, w, 6, C_DIM);
  int f = (int)(v * (w - 2));
  if (f > 0) cv.fillRect(x + 1, y + 1, f > w - 2 ? w - 2 : f, 4, c);
}
static void tabs(const uint16_t* ids, int n, int cur, int y) {
  int x = 3;
  for (int i = 0; i < n; i++) {
    const char* s = tr(ids[i]);
    if (i == cur) pill(s, x, y, C_MID);
    else txt(s, x + 3, y + 1, C_DIM);
    x += pillW(s) + 4;
  }
}
static void envelope(int x, int y, uint16_t c) {
  cv.drawRect(x, y, 9, 7, c);
  cv.drawLine(x, y, x + 4, y + 3, c);
  cv.drawLine(x + 8, y, x + 4, y + 3, c);
}
static const char* typeTag(int t) {
  static const char* const T[] = {"---", "CHT", "RPT", "ROM", "SNS"};
  return T[t >= 0 && t <= 4 ? t : 0];
}
static const char* kindTag(int k) {
  static const char* const T[] = {"PUB", " # ", "KEY", " DM", "ROM"};
  return T[k <= CK_ROOM ? k : 0];
}
// Keeps the selection inside a scrolled window of `rows`.
static void follow(int s, int& t, int rows) {
  if (s < t) t = s;
  if (s >= t + rows) t = s - rows + 1;
  if (t < 0) t = 0;
}

// ── header & footer ──────────────────────────────────────────────────────────
static void contextText(char* s, int cap) {
  *s = 0;
  if (screen == SC_CHAT) snprintf(s, cap, "%s", app_chat_name(chatId));
  else if (screen == SC_CONTACT) snprintf(s, cap, "%s", cur.name);
  else if (screen == SC_CHANNELS) snprintf(s, cap, "%s", tr(S_CHANNELS));
  else if (page == P_HOME) snprintf(s, cap, "%s", app_node_name());
  else if (page == P_HEARD && millis() < discoverUntil) snprintf(s, cap, "%s", tr(S_DISCOVERING));
  else if (page == P_MAP) snprintf(s, cap, "%d/%d", mapZoom + 1, NZOOM);
}

static void drawHeader(const Status& st) {
  cv.fillRect(0, 0, W, 12, C_BAR);
  cv.drawFastHLine(0, 12, W, C_MID);
  txt("CARDCORE", 3, 2, C_HI);
  int x = 3 + 8 * 6 + 4;
  int pg = screen == SC_CHAT ? P_CHATS : screen == SC_CONTACT ? P_CONTACTS : screen == SC_CHANNELS ? P_SETTINGS
         : screen == SC_WIZARD ? P_HOME : page;
  const char* pn = screen == SC_CHAT ? tr(S_CHAT) : screen == SC_CONTACT ? tr(S_NODE) : screen == SC_WIZARD ? tr(S_SETUP)
                 : tr(PAGE_NAME[pg]);
  pill(pn, x, 1, PAGE_COLOR[pg]);
  x += pillW(pn) + 4;
  // right side, right to left: battery, GPS, BLE, unread
  int r = W - 3;
  uint16_t bc = st.battPct >= 0 && st.battPct < 20 ? C_WARN : C_MID;
  cv.drawRect(r - 14, 3, 12, 7, bc);
  cv.fillRect(r - 2, 5, 2, 3, bc);
  if (st.battPct > 0) cv.fillRect(r - 12, 5, st.battPct * 8 / 100 < 1 ? 1 : st.battPct * 8 / 100, 3, bc);
  if (st.charging) cv.drawLine(r - 9, 3, r - 6, 9, C_HOT);
  r -= 19;
  if (st.gpsOn) { txt("G", r, 2, st.gpsFix ? C_HI : C_DIM, 1); r -= 8; }
  if (st.bleOn) { txt("B", r, 2, st.bleConn ? C_HI : C_DIM, 1); r -= 8; }
  if (st.usbConn) { txt("U", r, 2, C_HI, 1); r -= 8; }
  if (st.txNow) { cv.fillCircle(r - 3, 6, 2, C_WARN); r -= 8; }
  int unread = app_total_unread();
  if (unread) {
    char u[8];
    snprintf(u, sizeof u, "%d", unread > 99 ? 99 : unread);
    txt(u, r, 2, C_WARN, 1);
    r -= tlen(u) * 6 + 11;
    envelope(r, 3, C_WARN);
    r -= 4;
  }
  char ctx[40];
  contextText(ctx, sizeof ctx);
  int cols = (r - x) / 6;
  if (*ctx && cols > 2) txtFit(ctx, x, 2, C_HI, cols);
}

static const char* hintFor() {
  if (overlay == OV_HELP) return tr(S_H_ANYKEY);
  if (overlay == OV_INPUT) return tr(S_H_INPUT);
  if (overlay == OV_CONFIRM) return tr(S_H_CONFIRM);
  if (overlay == OV_REPLY) return tr(S_H_ANYKEY);
  switch (screen) {
    case SC_CHAT: return tr(S_H_CHAT);
    case SC_CONTACT: return tr(S_H_CONTACT);
    case SC_CHANNELS: return tr(S_H_CHANNELS);
    case SC_WIZARD: return tr(wizStep == W_DONE ? S_H_WIZDONE : S_H_WIZARD);
  }
  static const uint16_t H[NPAGE] = {S_H_HOME, S_H_CHATS, S_H_CONTACTS, S_H_HEARD, S_H_METRICS, S_H_GPS, S_H_MAP, S_H_SETTINGS};
  return tr(H[page]);
}

static void drawFooter(const Status& st, const Metrics& m) {
  cv.drawFastHLine(0, 113, W, C_MID);
  if (screen == SC_CHAT && overlay == OV_NONE) {  // the compose line takes the status row
    char b[192];
    int n = utf8ToCp437(compose, b, sizeof b), cols = 37;
    const char* tail = n > cols ? b + n - cols : b;
    cv.setTextColor(C_HI);
    cv.drawString(">", 3, 116);
    cv.setTextColor(C_HOT);
    cv.drawString(tail, 12, 116);
    if (millis() / 400 % 2) cv.fillRect(12 + (int)strlen(tail) * 6, 116, 5, 8, C_HI);
  } else if (screen == SC_PAGE && page == P_MAP && overlay == OV_NONE) {
    drawMapStatus();
  } else {
    txt("RX", 3, 116, C_DIM);
    for (int i = 0; i < 40; i++) {  // packets per 10 s, older is dimmer
      int v = m.rxHist[HIST - 40 + i], h = v > 8 ? 8 : v;
      cv.drawFastVLine(18 + i * 2, 124 - h, h ? h : 1, h ? (i < 20 ? C_DIM : C_MID) : C_SCAN);
    }
    char s[40];
    snprintf(s, sizeof s, "%.3f SF%d %ddBm", st.freq, st.sf, st.txPower);
    txt(s, W - 3, 116, st.txNow ? C_WARN : C_DIM, 1);
  }
  char hint[64];
  if (screen == SC_CHAT && overlay == OV_NONE && chatScroll > 0 && millis() >= toastUntil) {
    // scrolled back: details of the newest visible message stand in for the hints
    int idx = chatMetaIdx >= 0 && chatMetaIdx < nmsg ? chatMetaIdx : nmsg - 1;
    const Msg& mm = msgs[idx];
    char age[8];
    fmtAge(st.now > mm.ts ? st.now - mm.ts : 0, age, sizeof age);
    if (mm.out) snprintf(hint, sizeof hint, "%s  %s", age, tr(mm.status == MS_ACKED ? S_DELIVERED : mm.status == MS_FAILED ? S_FAILED : mm.status == MS_PENDING ? S_SENDING : S_SENT));
    else if (mm.hops < 0) snprintf(hint, sizeof hint, "%s  %s  %+.1fdB", age, tr(S_DIRECT), mm.snr4 / 4.f);
    else snprintf(hint, sizeof hint, "%s  %d %s  %+.1fdB", age, mm.hops, tr(S_HOPS), mm.snr4 / 4.f);
    txt(hint, 3, 126, C_MID);
    return;
  }
  if (screen == SC_CHAT && overlay == OV_NONE && millis() >= toastUntil) {
    snprintf(hint, sizeof hint, "%s  %d/%d", tr(S_H_CHAT), tlen(compose), app_text_limit(chatId));
    txtFit(hint, 3, 126, C_DIM, 39);
    return;
  }
  bool t = millis() < toastUntil;
  txtFit(t ? toast : hintFor(), 3, 126, t ? (toastWarn ? C_WARN : C_HI) : C_DIM, 39);
}

// ── pages ────────────────────────────────────────────────────────────────────
static void drawHome(const Status& st) {
  char s[48];
  cv.setTextSize(3);
  if (st.timeOk) {
    uint32_t t = st.now + st.tz * 3600;
    snprintf(s, sizeof s, "%02u:%02u", (unsigned)(t / 3600 % 24), (unsigned)(t / 60 % 60));
  } else {
    snprintf(s, sizeof s, "--:--");
  }
  txt(s, 6, 18, C_HOT);
  cv.setTextSize(1);
  if (st.timeOk) {
    static const char* const WD[3][7] = {{"THU", "FRI", "SAT", "SUN", "MON", "TUE", "WED"},  // 1970-01-01: a Thursday
                                         {"DJ.", "DV.", "DS.", "DG.", "DL.", "DT.", "DC."},
                                         {"JUE", "VIE", "SÁB", "DOM", "LUN", "MAR", "MIÉ"}};
    uint32_t days = (st.now + st.tz * 3600) / 86400;
    int y, mo, d;
    civilFromDays(days, y, mo, d);
    snprintf(s, sizeof s, "%s %d/%02d/%d", WD[app_ui().lang % 3][days % 7], d, mo, y);
    txt(s, 7, 46, C_MID);
  } else {
    txt(tr(S_NO_TIME), 7, 46, C_WARN);
  }
  int x = 104;
  txtFit(app_node_name(), x, 16, C_HI, 22);
  txtFit(st.preset, x, 27, C_MID, 22);
  snprintf(s, sizeof s, "%.3f %g/%d/%d", st.freq, st.bw, st.sf, st.cr);
  txt(s, x, 38, C_DIM);
  if (st.bleOn) snprintf(s, sizeof s, "BLE %s  PIN %06lu", st.bleConn ? "ON" : "--", (unsigned long)st.pin);
  else snprintf(s, sizeof s, "BLE OFF");
  txt(s, x, 49, st.bleConn ? C_HI : C_DIM);
  cv.drawFastHLine(3, 60, W - 6, C_DIM);
  if (!nrecent) {
    txt(tr(S_NO_RECENT), W / 2, 80, C_DIM, 2);
    return;
  }
  for (int i = 0; i < nrecent; i++) {
    int y = 64 + i * 12;
    char age[8];
    fmtAge((millis() - recent[i].at) / 1000, age, sizeof age);
    txtFit(recent[i].line, 3, y, i == 0 ? C_HI : C_MID, 34);
    txt(age, W - 3, y, C_DIM, 1);
  }
}

static Chat chatRows[48];
static int nChatRows = 0;
static void drawChats(const Status& st) {
  static const uint16_t T[] = {S_ALL, S_CHANNELS, S_DIRECT};
  tabs(T, NCF, chatFilter, 15);
  nChatRows = app_chats(chatRows, 48, chatFilter);
  if (!nChatRows) {
    txt(tr(S_NO_CHATS), W / 2, 60, C_DIM, 2);
    return;
  }
  int& s = sel[P_CHATS];
  if (s >= nChatRows) s = nChatRows - 1;
  follow(s, top[P_CHATS], 8);
  for (int i = 0; i < 8 && top[P_CHATS] + i < nChatRows; i++) {
    const Chat& c = chatRows[top[P_CHATS] + i];
    int y = 28 + i * 11;
    bool on = top[P_CHATS] + i == s;
    if (on) cv.fillRect(0, y - 2, W, 11, C_BAR);
    txt(kindTag(c.id.kind), 3, y, KIND_COLOR[c.id.kind]);
    txtFit(c.name, 27, y, on ? C_HOT : C_MID, 24);
    char r[16] = "";
    if (c.last && st.timeOk) fmtAge(st.now > c.last ? st.now - c.last : 0, r, sizeof r);
    txt(r, W - 3, y, C_DIM, 1);
    if (c.unread) {
      char u[8];
      snprintf(u, sizeof u, "%d", c.unread);
      pill(u, W - 30 - pillW(u), y - 1, C_WARN);
    }
  }
}

static void drawContacts(const Status& st) {
  static const uint16_t T[] = {S_ALL, S_T_CHAT, S_T_RPT, S_T_ROOM, S_T_SENS};
  tabs(T, 5, contactFilter, 15);
  int tf = CONTACT_FILTERS[contactFilter], n = app_contact_count(tf);
  if (!n) {
    txt(tr(S_NO_CONTACTS), W / 2, 60, C_DIM, 2);
    return;
  }
  int& s = sel[P_CONTACTS];
  if (s >= n) s = n - 1;
  follow(s, top[P_CONTACTS], 8);
  for (int i = 0; i < 8 && top[P_CONTACTS] + i < n; i++) {
    Contact c;
    if (!app_contact(top[P_CONTACTS] + i, tf, c)) break;
    int y = 28 + i * 11;
    bool on = top[P_CONTACTS] + i == s;
    if (on) cv.fillRect(0, y - 2, W, 11, C_BAR);
    txt(typeTag(c.type), 3, y, TYPE_COLOR[c.type <= 4 ? c.type : 0]);
    if (c.fav) txt("*", 22, y, C_WARN);
    txtFit(c.name, 28, y, on ? C_HOT : C_MID, 23);
    char p[8];
    if (c.hops < 0) snprintf(p, sizeof p, "FL");
    else if (c.hops == 0) snprintf(p, sizeof p, "D");
    else snprintf(p, sizeof p, "%d\xc2\xbb", c.hops);  // "2»": hops
    txt(p, 186, y, c.hops < 0 ? C_DIM : C_MID, 1);
    char a[8] = "";
    if (c.lastHeard && st.timeOk) fmtAge(st.now > c.lastHeard ? st.now - c.lastHeard : 0, a, sizeof a);
    txt(a, W - 3, y, C_DIM, 1);
  }
}

static Heard heardRows[24];
static int nHeard = 0;
static void drawHeard() {
  nHeard = app_heard(heardRows, 24);
  txt(tr(S_COL_NODE), 3, 15, C_DIM);
  txt(tr(S_COL_HOPS), 168, 15, C_DIM, 1);
  txt("SNR", 204, 15, C_DIM, 1);
  cv.drawFastHLine(0, 24, W, C_SCAN);
  if (!nHeard) {
    txt(tr(S_NO_HEARD), W / 2, 60, C_DIM, 2);
    return;
  }
  int& s = sel[P_HEARD];
  if (s >= nHeard) s = nHeard - 1;
  follow(s, top[P_HEARD], 8);
  for (int i = 0; i < 8 && top[P_HEARD] + i < nHeard; i++) {
    const Heard& h = heardRows[top[P_HEARD] + i];
    int y = 28 + i * 11;
    bool on = top[P_HEARD] + i == s;
    if (on) cv.fillRect(0, y - 2, W, 11, C_BAR);
    txt(typeTag(h.type), 3, y, TYPE_COLOR[h.type <= 4 ? h.type : 0]);
    char nm[24];
    if (h.name[0]) snprintf(nm, sizeof nm, "%s", h.name);
    else toHex(nm, h.key, 4);
    txtFit(nm, 27, y, on ? C_HOT : h.known ? C_MID : C_DIM, 21);
    char v[12];
    if (h.hops <= 0) snprintf(v, sizeof v, "D");
    else snprintf(v, sizeof v, "%d", h.hops);
    txt(v, 168, y, C_MID, 1);
    snprintf(v, sizeof v, "%+.1f", h.snr4 / 4.f);
    txt(v, 204, y, h.snr4 < -20 ? C_WARN : C_MID, 1);
    fmtAge((millis() - h.at) / 1000, v, sizeof v);
    txt(v, W - 3, y, C_DIM, 1);
  }
}

static void kv(int col, int row, const char* k, const char* v, uint16_t vc = 0) {
  int x = col ? 122 : 3, y = ROW0 + row * RH;
  txt(k, x, y, C_MID);
  txt(v, x + 114, y, vc ? vc : C_HI, 1);
}
static void graph(int x, int y, int w, int h, const char* label, const int* v, int n, int lo, int hi, bool bars) {
  cv.drawRect(x, y, w, h, C_DIM);
  txt(label, x + 2, y + 2, C_DIM);
  char s[12];
  snprintf(s, sizeof s, "%d", v[n - 1]);
  txt(s, x + w - 2, y + 2, C_HI, 1);
  if (hi <= lo) hi = lo + 1;
  int px = -1, py = 0;
  for (int i = 0; i < n; i++) {
    int xx = x + 1 + i * (w - 2) / n, vv = v[i] < lo ? lo : v[i] > hi ? hi : v[i];
    int hh = (vv - lo) * (h - 12) / (hi - lo), yy = y + h - 2 - hh;
    if (bars) {
      if (hh) cv.drawFastVLine(xx, yy, hh, i > n * 3 / 4 ? C_HI : C_MID);
    } else {
      if (px >= 0) cv.drawLine(px, py, xx, yy, C_HI);
      px = xx, py = yy;
    }
  }
}

static void drawMetrics(const Metrics& m, const Status& st) {
  static const uint16_t T[] = {S_RADIO, S_NODE, S_RXLOG};
  tabs(T, 3, metricsView, 15);
  char a[24], b[24];
  auto row = [&](int r) { return 28 + r * 10; };
  if (metricsView == 0) {
    struct { uint16_t k; char v[16]; uint16_t c; } L[8];
    snprintf(L[0].v, 16, "%lu", (unsigned long)m.rx), L[0].k = S_M_RX;
    snprintf(L[1].v, 16, "%lu", (unsigned long)m.tx), L[1].k = S_M_TX;
    snprintf(L[2].v, 16, "%lu/%lu", (unsigned long)m.floodTx, (unsigned long)m.floodRx), L[2].k = S_M_FLOOD;
    snprintf(L[3].v, 16, "%lu/%lu", (unsigned long)m.directTx, (unsigned long)m.directRx), L[3].k = S_M_DIRECT;
    snprintf(L[4].v, 16, "%lu", (unsigned long)m.rxErr), L[4].k = S_M_ERR;
    snprintf(L[5].v, 16, "%.1fs", m.airTxMs / 1000.f), L[5].k = S_M_AIRTX;
    snprintf(L[6].v, 16, "%.1f", m.lastRssi), L[6].k = S_M_RSSI;
    snprintf(L[7].v, 16, "%+.1f", m.lastSnr), L[7].k = S_M_SNR;
    for (int i = 0; i < 8; i++) {
      txt(tr(L[i].k), 3, row(i), C_MID);
      txt(L[i].v, 122, row(i), C_HI, 1);
    }
    int rx[HIST], nf[HIST], lo = 0, hi = 4, nlo = -130, nhi = -60;
    for (int i = 0; i < HIST; i++) {
      rx[i] = m.rxHist[i], nf[i] = m.noiseHist[i];
      if (rx[i] > hi) hi = rx[i];
    }
    graph(126, 27, 111, 40, "RX/10s", rx, HIST, lo, hi, true);
    graph(126, 70, 111, 40, tr(S_M_NOISE), nf, HIST, nlo, nhi, false);
    return;
  }
  if (metricsView == 1) {
    uint32_t u = m.uptimeS;
    snprintf(a, sizeof a, "%lud %02lu:%02lu", (unsigned long)(u / 86400), (unsigned long)(u / 3600 % 24), (unsigned long)(u / 60 % 60));
    txt(tr(S_M_UPTIME), 3, row(0), C_MID), txt(a, 116, row(0), C_HI, 1);
    snprintf(a, sizeof a, "%.2fV %d%%", st.battMv / 1000.f, st.battPct);
    txt(tr(S_M_BATT), 3, row(1), C_MID), txt(a, 116, row(1), st.battPct >= 0 && st.battPct < 20 ? C_WARN : C_HI, 1);
    snprintf(a, sizeof a, "%luK", (unsigned long)(m.heap / 1024));
    snprintf(b, sizeof b, "%luK", (unsigned long)(m.heapMin / 1024));
    txt(tr(S_M_HEAP), 3, row(2), C_MID), txt(a, 116, row(2), m.heap < 20000 ? C_WARN : C_HI, 1);
    txt(tr(S_M_HEAPMIN), 3, row(3), C_MID), txt(b, 116, row(3), C_HI, 1);
    snprintf(a, sizeof a, "%d", m.contacts);
    txt(tr(S_CONTACTS), 3, row(4), C_MID), txt(a, 116, row(4), C_HI, 1);
    snprintf(a, sizeof a, "%d", m.channels);
    txt(tr(S_CHANNELS), 3, row(5), C_MID), txt(a, 116, row(5), C_HI, 1);
    snprintf(a, sizeof a, "%d", m.queue);
    txt(tr(S_M_QUEUE), 3, row(6), C_MID), txt(a, 116, row(6), C_HI, 1);
    txt("SD", 3, row(7), C_MID), txt(st.sd ? "OK" : tr(S_SD_NONE), 116, row(7), st.sd ? C_HI : C_DIM, 1);
    // duty cycle: the EU 869.4-869.65 MHz sub-band allows 10 %
    txt(tr(S_M_DUTY), 124, row(0), C_MID);
    snprintf(a, sizeof a, "%.2f%%", m.duty1h);
    txt(a, W - 3, row(0), m.duty1h > 8 ? C_WARN : C_HI, 1);
    meterBar(124, row(1), 113, m.duty1h / 10.f, m.duty1h > 8 ? C_WARN : C_HI);
    txt(tr(S_M_LIMIT), 124, row(2), C_DIM);
    snprintf(a, sizeof a, "%.1fs", m.airRxMs / 1000.f);
    txt(tr(S_M_AIRRX), 124, row(4), C_MID), txt(a, W - 3, row(4), C_HI, 1);
    snprintf(a, sizeof a, "%d dBm", m.noise);
    txt(tr(S_M_NOISE), 124, row(5), C_MID), txt(a, W - 3, row(5), C_HI, 1);
    snprintf(a, sizeof a, "%d dBm", st.txPower);
    txt("TX", 124, row(6), C_MID), txt(a, W - 3, row(6), C_HI, 1);
    return;
  }
  RxLog log[8];
  int n = app_rx_log(log, 8);
  if (!n) {
    txt(tr(S_NO_HEARD), W / 2, 60, C_DIM, 2);
    return;
  }
  for (int i = 0; i < n; i++) {
    int y = row(i);
    fmtAge((millis() - log[i].at) / 1000, a, sizeof a);
    txt(a, 3, y, C_DIM);
    snprintf(a, sizeof a, "%d dBm", log[i].rssi);
    txt(a, 90, y, C_HI, 1);
    snprintf(a, sizeof a, "%+.1f dB", log[i].snr4 / 4.f);
    txt(a, 150, y, log[i].snr4 < 0 ? C_WARN : C_MID, 1);
    meterBar(158, y + 1, 79, (log[i].rssi + 130) / 90.f, C_MID);
  }
}

static void drawGps(const Status& st) {
  Gps g;
  app_gps(g);
  char s[40];
  snprintf(s, sizeof s, "%s  %s  %d %s", g.on ? "ON" : "OFF", g.fix ? "FIX" : tr(S_NO_FIX), g.sats, tr(S_SATS));
  txt("GPS", 3, ROW0, C_MID);
  txt(s, 40, ROW0, g.fix ? C_HI : C_WARN);
  if (g.fix) {
    snprintf(s, sizeof s, "%.5f  %.5f  %dm", g.lat, g.lon, g.altM);
    txt(s, 40, ROW0 + RH, C_HOT);
  }
  txt(tr(S_NODE), 3, ROW0 + 2 * RH, C_MID);
  if (g.nodeLocSet) snprintf(s, sizeof s, "%.5f  %.5f", g.nodeLat, g.nodeLon);
  else snprintf(s, sizeof s, "%s", tr(S_UNSET));
  txt(s, 40, ROW0 + 2 * RH, g.nodeLocSet ? C_HI : C_DIM);
  cv.drawFastHLine(3, ROW0 + 3 * RH - 1, W - 6, C_DIM);
  double la = g.fix ? g.lat : g.nodeLat, lo = g.fix ? g.lon : g.nodeLon;
  if (!g.fix && !g.nodeLocSet) {
    txt(tr(S_NEED_POS), W / 2, 80, C_DIM, 2);
    return;
  }
  struct Near { char name[24]; float d, b; } nr[16];
  int nn = 0, total = app_contact_count(0);
  for (int i = 0; i < total; i++) {
    Contact c;
    if (!app_contact(i, 0, c) || (!c.lat && !c.lon)) continue;
    float b, d = geoDistKm(la, lo, c.lat / 1e6, c.lon / 1e6, &b);
    int k = nn < 16 ? nn++ : 15;  // insertion sort, nearest 16 kept
    if (k == 15 && nn == 16 && d >= nr[15].d) continue;
    while (k > 0 && nr[k - 1].d > d) nr[k] = nr[k - 1], k--;
    snprintf(nr[k].name, sizeof nr[k].name, "%s", c.name);
    nr[k].d = d, nr[k].b = b;
  }
  if (!nn) {
    txt(tr(S_NO_LOCATED), W / 2, 80, C_DIM, 2);
    return;
  }
  int& sl = sel[P_GPS];
  if (sl >= nn) sl = nn - 1;
  follow(sl, top[P_GPS], 6);
  for (int i = 0; i < 6 && top[P_GPS] + i < nn; i++) {
    const Near& e = nr[top[P_GPS] + i];
    int y = ROW0 + (3 + i) * RH;
    txtFit(e.name, 3, y, top[P_GPS] + i == sl ? C_HOT : C_MID, 22);
    if (e.d < 10) snprintf(s, sizeof s, "%.2fkm", e.d);
    else snprintf(s, sizeof s, "%.0fkm", e.d);
    txt(s, 192, y, C_HI, 1);
    snprintf(s, sizeof s, "%-2s%4.0f", compass(e.b), e.b);
    txt(s, W - 3, y, C_DIM, 1);
  }
}

// ── map ──────────────────────────────────────────────────────────────────────
// Equirectangular around the view centre: plenty for a low-res map. Returns the longitude
// offset (-180..180] so callers can spot a line wrapping round the back of the world.
static double mapProject(double lat, double lon, int& x, int& y) {
  double mpp = MAP_MPP[mapZoom], dl = lon - mapLon;
  if (dl > 180) dl -= 360;
  if (dl <= -180) dl += 360;
  double dx = dl * 111320.0 * cos(mapLat * M_PI / 180) / mpp, dy = (mapLat - lat) * 110540.0 / mpp;
  x = MCX + (int)fmax(-4000, fmin(4000, dx)), y = MCY + (int)fmax(-4000, fmin(4000, dy));
  return dl;
}
// Draws one layer's on-screen chunks. dot > 0: one pixel in `dot`, for provinces and rivers.
static void drawLayer(const MapChunk* cs, int n, uint16_t col, int dot = 0) {
  float mpp = MAP_MPP[mapZoom], kx = 111320.f * cosf(mapLat * M_PI / 180) / mpp, ky = 110540.f / mpp;
  float hw = (MCX + 4) / kx, hh = 54 / ky;  // half the view, degrees
  const uint16_t* pts = (const uint16_t*)MAP_DATA;
  int phase = 0;
  for (int i = 0; i < n; i++) {
    const MapChunk& c = cs[i];
    if (c.lat0 / 100.0 > mapLat + hh || (c.lat0 + c.dlat) / 100.0 < mapLat - hh) continue;
    double bl = c.lon0 / 100.0 - mapLon;  // chunk's west edge from the view centre
    if (hw < 180) {  // longitude overlap, also one world to either side
      int s = -360;
      while (s <= 360 && !(bl + s <= hw && bl + s + c.dlon / 100.0 >= -hw)) s += 360;
      if (s > 360) continue;
      bl += s;
    } else {  // world view: each chunk once, from its west edge
      bl = fmod(bl + 540, 360) - 180;
    }
    float by = c.lat0 / 100.0 - mapLat, sc = (1 << c.shift) / 10000.f, pl = 0;
    const uint16_t* p = pts + 2 * c.start;
    int px = 0, py = 0;
    for (int j = 0; j < c.n; j++, p += 2) {
      float dl = bl + p[1] * sc, dy = (by + p[0] * sc) * ky, dx = dl * kx;
      int x = MCX + (int)fmaxf(-4000, fminf(4000, dx)), y = MCY - (int)fmaxf(-4000, fminf(4000, dy));
      if (j && fabsf(dl - pl) < 180) {  // not a line wrapping round the back of the world
        if (!dot) cv.drawLine(px, py, x, y, col);
        else {
          int m = abs(x - px) > abs(y - py) ? abs(x - px) : abs(y - py);
          for (int k = 0; k < m; k++)
            if (phase++ % dot == 0) cv.drawPixel(px + (x - px) * k / m, py + (y - py) * k / m, col);
        }
      }
      px = x, py = y, pl = dl;
    }
  }
}
static void mapPan(int px, int py) {  // move the view by pixels
  double mpp = MAP_MPP[mapZoom];
  mapLat = fmax(-85, fmin(85, mapLat - py * mpp / 110540.0));
  mapLon += px * mpp / (111320.0 * cos(mapLat * M_PI / 180));
  if (mapLon > 180) mapLon -= 360;
  if (mapLon < -180) mapLon += 360;
}
static bool mapMe(double& lat, double& lon) {
  Gps g;
  app_gps(g);
  lat = g.fix ? g.lat : g.nodeLat, lon = g.fix ? g.lon : g.nodeLon;
  return g.fix || g.nodeLocSet;
}
// Index (in app_contact order) of the located contact nearest the cursor, within 8 px; -1 if none.
static int mapNearest(Contact* out) {
  int best = -1, bd = 65, n = app_contact_count(0);
  for (int i = 0; i < n; i++) {
    Contact c;
    if (!app_contact(i, 0, c) || (!c.lat && !c.lon)) continue;
    int x, y;
    mapProject(c.lat / 1e6, c.lon / 1e6, x, y);
    int d = (x - MCX) * (x - MCX) + (y - MCY) * (y - MCY);
    if (d < bd) bd = d, best = i, *out = c;
  }
  return best;
}

static void drawMap() {
  if (!mapInit) {  // first visit: around us, or all of Catalonia
    mapInit = true;
    mapMe(mapLat, mapLon);
  }
  uint32_t mpp = MAP_MPP[mapZoom];
  cv.setClipRect(0, 13, W, 100);
  if (mpp <= 2500) {  // 1:10m Natural Earth, Catalonia's outline
    drawLayer(MAP_PROVINCE, NMAP_PROVINCE, C_DIM, 3);
    drawLayer(MAP_RIVER, NMAP_RIVER, C_DIM, 2);
    drawLayer(MAP_BORDER, NMAP_BORDER, C_DIM);
    if (mpp > 100) drawLayer(MAP_CATALONIA, NMAP_CATALONIA, C_DIM);  // local: it would double the coast
    drawLayer(MAP_COAST, NMAP_COAST, C_MID);
  } else {  // 1:110m
    drawLayer(MAP_BORDER_LO, NMAP_BORDER_LO, C_DIM);
    drawLayer(MAP_COAST_LO, NMAP_COAST_LO, C_MID);
  }
  double hh = 60.0 * mpp / 110540;  // half the view, degrees, with room for a label
  for (int i = 0; i < NMAP_TOWNS && mpp <= 2500; i++) {  // big towns on the Catalonia view, all local
    const MapTown& t = MAP_TOWNS[i];
    if ((t.rank && mpp > 100) || fabs(t.lat / 1e4 - mapLat) > hh) continue;
    int x, y;
    mapProject(t.lat / 1e4, t.lon / 1e4, x, y);
    if (x < -60 || x > W || y < 10 || y > 115) continue;
    cv.fillRect(x - 1, y - 1, 2, 2, C_DIM);
    txt(t.name, x + 3, y - 3, C_DIM);
  }
  // contacts with a position
  Contact near;
  int ni = mapNearest(&near), n = app_contact_count(0);
  for (int i = 0; i < n; i++) {
    Contact c;
    if (!app_contact(i, 0, c) || (!c.lat && !c.lon)) continue;
    int x, y;
    mapProject(c.lat / 1e6, c.lon / 1e6, x, y);
    if (x < -2 || x > W + 2 || y < 11 || y > 114) continue;
    uint16_t col = i == ni ? C_HOT : TYPE_COLOR[c.type <= 4 ? c.type : 0];
    if (c.type == CT_REPEATER) cv.drawRect(x - 2, y - 2, 5, 5, col), cv.drawPixel(x, y, col);
    else if (c.type == CT_ROOM) cv.drawLine(x, y - 3, x + 3, y, col), cv.drawLine(x + 3, y, x, y + 3, col),
                                cv.drawLine(x, y + 3, x - 3, y, col), cv.drawLine(x - 3, y, x, y - 3, col);
    else cv.fillRect(x - 1, y - 1, 3, 3, col);
    if (mpp <= 100) {  // wider views: the top line names it
      int w = tlen(c.name);
      cv.fillRect(x + 4, y - 4, (w < 10 ? w : 10) * 6 + 2, 10, C_BG);  // over town names and lines
      txtFit(c.name, x + 5, y - 3, i == ni ? C_HOT : C_MID, 10);
    }
  }
  // us
  double la, lo;
  if (mapMe(la, lo)) {
    int x, y;
    mapProject(la, lo, x, y);
    cv.drawRect(x - 3, y - 3, 7, 7, C_HOT);
    cv.drawPixel(x, y, C_HOT);
  }
  // cursor
  cv.drawFastHLine(MCX - 7, MCY, 5, C_HI), cv.drawFastHLine(MCX + 3, MCY, 5, C_HI);
  cv.drawFastVLine(MCX, MCY - 7, 5, C_HI), cv.drawFastVLine(MCX, MCY + 3, 5, C_HI);
  cv.clearClipRect();
  if (ni >= 0) {  // what's under the cursor, from us
    char s[48];
    int k = snprintf(s, sizeof s, "%s", near.name);
    if (mapMe(la, lo)) {
      float b, d = geoDistKm(la, lo, near.lat / 1e6, near.lon / 1e6, &b);
      snprintf(s + k, sizeof s - k, d < 10 ? "  %.2fkm %s" : "  %.0fkm %s", d, compass(b));
    }
    int w = tlen(s) * 6 + 4;
    cv.fillRect(1, 14, w > W - 2 ? W - 2 : w, 10, C_BG);
    txtFit(s, 3, 15, C_HOT, 39);
  }
}

static void drawMapStatus() {  // cursor position and a scale bar, in place of the RX line
  char s[32];
  snprintf(s, sizeof s, "%.5f %.5f", mapLat, mapLon);
  txt(s, 3, 116, C_MID);
  static const uint32_t NICE[] = {100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000,
                                  500000, 1000000, 2000000, 5000000};
  uint32_t mpp = MAP_MPP[mapZoom];
  uint32_t len = NICE[0];
  for (uint32_t v : NICE)
    if (v / mpp <= 56) len = v;
  int px = len / mpp;
  if (len >= 1000) snprintf(s, sizeof s, "%lukm", (unsigned long)(len / 1000));
  else snprintf(s, sizeof s, "%lum", (unsigned long)len);
  int x1 = W - 3, x0 = x1 - px;
  cv.drawFastHLine(x0, 123, px, C_MID);
  cv.drawFastVLine(x0, 119, 5, C_MID), cv.drawFastVLine(x1, 119, 5, C_MID);
  txt(s, x0 - 4, 116, C_MID, 1);
}

static void drawSettings() {
  int n = app_setting_count();
  int& s = sel[P_SETTINGS];
  while (s < n - 1 && app_setting_kind(s) == SK_SECTION) s++;
  follow(s, top[P_SETTINGS], NROWS);
  if (top[P_SETTINGS] > 0 && app_setting_kind(top[P_SETTINGS] - 1) == SK_SECTION && s - top[P_SETTINGS] < NROWS - 1)
    top[P_SETTINGS]--;  // show a section's title above its first row
  for (int i = 0; i < NROWS && top[P_SETTINGS] + i < n; i++) {
    int k = top[P_SETTINGS] + i, y = ROW0 + i * RH, kind = app_setting_kind(k);
    if (kind == SK_SECTION) {
      const char* t = tr(app_setting_label(k));
      int w = tlen(t) * 6;
      cv.drawFastHLine(3, y + 3, (W - w) / 2 - 8, C_DIM);
      cv.drawFastHLine((W + w) / 2 + 5, y + 3, (W - w) / 2 - 8, C_DIM);
      txt(t, W / 2, y, C_DIM, 2);
      continue;
    }
    bool on = k == s;
    if (on) selBar(y);
    txt(tr(app_setting_label(k)), 6, y, on ? C_HOT : kind == SK_DANGER ? C_WARN : C_MID);
    char v[48];
    app_setting_value(k, v, sizeof v);
    txtFit(v, 104, y, on ? C_HI : C_DIM, 22);
  }
  if (n > NROWS) {  // scroll bar
    int h = NROWS * 97 / n, y = 14 + top[P_SETTINGS] * 97 / n;
    cv.fillRect(W - 2, y, 2, h, C_DIM);
  }
}

// ── chat ─────────────────────────────────────────────────────────────────────
struct Line { int8_t msg; uint8_t start, len; bool first, last; };
static Line lines[160];
static char lineText[NMSG][200];  // each message as drawn ("from: text" / "> text"), CP437
static int nlines = 0, prefixLen[NMSG];

static void layoutChat() {
  nlines = 0;
  for (int i = 0; i < nmsg; i++) {
    const Msg& m = msgs[i];
    char u[200];
    if (m.out) snprintf(u, sizeof u, "> %s", m.text);
    else if (m.from[0]) snprintf(u, sizeof u, "%s: %s", m.from, m.text);
    else snprintf(u, sizeof u, "%s", m.text);
    utf8ToCp437(u, lineText[i], sizeof lineText[i]);
    char f[32];
    prefixLen[i] = m.out ? 2 : m.from[0] ? utf8ToCp437(m.from, f, sizeof f) + 1 : 0;
    int st[12], ln[12], n = wrapLines(lineText[i], 39, st, ln, 12);
    for (int k = 0; k < n && nlines < 160; k++)
      lines[nlines++] = {(int8_t)i, (uint8_t)st[k], (uint8_t)ln[k], k == 0, k == n - 1};
  }
}

static uint16_t nameColor(const char* s) {  // a stable colour per sender
  uint32_t h = 5381;
  for (; *s; s++) h = h * 33 + (uint8_t)*s;
  return KIND_COLOR[h % 5];
}

static void drawChat() {
  if (!nmsg) {
    txt(tr(S_EMPTY_CHAT), W / 2, 50, C_DIM, 2);
    return;
  }
  const int rows = 9;
  int maxScroll = nlines > rows ? nlines - rows : 0;
  if (chatScroll > maxScroll) chatScroll = maxScroll;
  int first = nlines - rows - chatScroll;
  if (first < 0) first = 0;
  chatMetaIdx = -1;
  for (int r = 0; r < rows && first + r < nlines; r++) {
    const Line& L = lines[first + r];
    const Msg& m = msgs[L.msg];
    int y = 15 + r * 11;
    chatMetaIdx = L.msg;
    char seg[48];
    int pl = L.first ? prefixLen[L.msg] : 0;
    if (pl > L.len) pl = L.len;
    if (pl) {
      memcpy(seg, lineText[L.msg] + L.start, pl);
      seg[pl] = 0;
      cv.setTextColor(m.out ? C_DIM : nameColor(m.from));
      cv.drawString(seg, 3, y);
    }
    memcpy(seg, lineText[L.msg] + L.start + pl, L.len - pl);
    seg[L.len - pl] = 0;
    cv.setTextColor(m.out ? C_HI : C_MID);
    cv.drawString(seg, 3 + pl * 6, y);
    if (L.last && m.out) {  // delivery mark after the text
      int x = 3 + L.len * 6 + 3;
      if (m.status == MS_ACKED) {
        cv.drawLine(x, y + 4, x + 2, y + 6, C_HOT), cv.drawLine(x + 2, y + 6, x + 6, y + 1, C_HOT);
      } else if (m.status == MS_FAILED) {
        txt("!", x, y, C_WARN);
      } else if (m.status == MS_PENDING) {
        for (int d = 0; d < 3; d++) cv.drawPixel(x + d * 2, y + 6, (millis() / 300) % 3 == d ? C_HI : C_DIM);
      } else {
        cv.drawLine(x, y + 4, x + 2, y + 6, C_DIM), cv.drawLine(x + 2, y + 6, x + 6, y + 1, C_DIM);
      }
    }
  }
  if (maxScroll) {
    int h = rows * 97 / nlines, y = 14 + first * 97 / nlines;
    cv.fillRect(W - 2, y, 2, h < 3 ? 3 : h, C_DIM);
  }
}

static void openChat(const ChatId& id) {
  chatId = id;
  screen = SC_CHAT;
  composeLen = 0;
  compose[0] = 0;
  chatScroll = 0;
  app_open_chat(&chatId);
  nmsg = app_load(chatId, msgs, NMSG);
  layoutChat();
}
static void reloadChat() {
  nmsg = app_load(chatId, msgs, NMSG);
  layoutChat();
}

// ── contact detail ───────────────────────────────────────────────────────────
static const uint16_t ACT_NAME[] = {S_A_MESSAGE, S_A_LOGIN, S_A_STATUS, S_A_TELEMETRY, S_A_TRACE,
                                    S_A_RESET, S_A_SHARE, S_A_FAV, S_A_DELETE};
static void openContact(const Contact& c) {
  cur = c;
  screen = SC_CONTACT;
  actSel = 0;
  nacts = 0;
  if (c.type == CT_CHAT || c.type == CT_ROOM) acts[nacts++] = CA_MESSAGE;
  if (c.type == CT_REPEATER || c.type == CT_ROOM || c.type == CT_SENSOR) acts[nacts++] = CA_LOGIN;
  if (c.type != CT_CHAT) acts[nacts++] = CA_STATUS;
  acts[nacts++] = CA_TELEMETRY;
  if (c.type == CT_REPEATER || c.type == CT_ROOM) acts[nacts++] = CA_TRACE;
  acts[nacts++] = CA_RESET_PATH;
  acts[nacts++] = CA_SHARE;
  acts[nacts++] = CA_FAV;
  acts[nacts++] = CA_DELETE;
}

static void drawContact(const Status& st) {
  Contact c;
  if (app_contact_by_key(cur.key, c)) cur = c;  // refresh path, last heard
  char s[64], k[20];
  txt(typeTag(cur.type), 3, ROW0, TYPE_COLOR[cur.type <= 4 ? cur.type : 0]);
  txtFit(cur.name, 27, ROW0, C_HOT, 30);
  if (cur.fav) txt("*", W - 3, ROW0, C_WARN, 1);
  toHex(k, cur.key, 8);
  txt(k, 3, ROW0 + RH, C_DIM);
  char age[8] = "?";
  if (cur.lastHeard && st.timeOk) fmtAge(st.now > cur.lastHeard ? st.now - cur.lastHeard : 0, age, sizeof age);
  snprintf(s, sizeof s, "%s %s", tr(S_HEARD_AGO), age);
  txt(s, W - 3, ROW0 + RH, C_DIM, 1);
  // path: hop hashes in hex
  if (cur.hops < 0) snprintf(s, sizeof s, "%s: %s", tr(S_PATH), tr(S_FLOOD));
  else if (cur.hops == 0) snprintf(s, sizeof s, "%s: %s", tr(S_PATH), tr(S_DIRECT));
  else {
    int n = snprintf(s, sizeof s, "%s:", tr(S_PATH));
    for (int i = 0; i < cur.hops && n < (int)sizeof s - 8; i++) {
      s[n++] = ' ';
      toHex(s + n, cur.path + i * cur.hashSize, cur.hashSize);
      n += cur.hashSize * 2;
    }
  }
  txtFit(s, 3, ROW0 + 2 * RH, C_MID, 39);
  Gps g;
  app_gps(g);
  if (cur.lat || cur.lon) {
    int n = snprintf(s, sizeof s, "%.4f %.4f", cur.lat / 1e6, cur.lon / 1e6);
    if (g.fix || g.nodeLocSet) {
      float b, d = geoDistKm(g.fix ? g.lat : g.nodeLat, g.fix ? g.lon : g.nodeLon, cur.lat / 1e6, cur.lon / 1e6, &b);
      snprintf(s + n, sizeof s - n, "  %.1fkm %s", d, compass(b));
    }
    txt(s, 3, ROW0 + 3 * RH, C_MID);
  }
  cv.drawFastHLine(3, ROW0 + 4 * RH - 1, W - 6, C_DIM);
  for (int i = 0; i < nacts; i++) {  // two columns
    int col = i / 4, row = i % 4, x = col ? 122 : 3, y = ROW0 + (4 + row) * RH + 1;
    bool on = i == actSel;
    if (on) cv.fillRect(x - 2, y - 2, 117, RH, C_BAR);
    uint16_t c = acts[i] == CA_DELETE ? C_WARN : on ? C_HOT : C_MID;
    const char* name = acts[i] == CA_FAV && cur.fav ? tr(S_A_UNFAV) : tr(ACT_NAME[acts[i]]);
    txt(name, x + 6, y, c);
    if (on) txt(">", x, y, C_HI);
  }
}

// ── channels manager ─────────────────────────────────────────────────────────
static void drawChannels() {
  int n = app_channel_count(), shown = 0;
  int idx[32];
  for (int i = 0; i < n && shown < 32; i++) {
    Chat c;
    if (app_channel(i, c)) idx[shown++] = i;
  }
  if (chanSel >= shown) chanSel = shown - 1;
  if (chanSel < 0) chanSel = 0;
  follow(chanSel, chanTop, NROWS);
  for (int r = 0; r < NROWS && chanTop + r < shown; r++) {
    Chat c;
    app_channel(idx[chanTop + r], c);
    int y = ROW0 + r * RH;
    bool on = chanTop + r == chanSel;
    if (on) selBar(y);
    txt(kindTag(c.id.kind), 3, y, KIND_COLOR[c.id.kind]);
    txtFit(c.name, 27, y, on ? C_HOT : C_MID, 22);
    char k[33];
    app_channel_key(idx[chanTop + r], k);
    k[8] = 0;
    txt(k, W - 3, y, C_DIM, 1);
  }
  chanSlot = shown ? idx[chanSel] : -1;  // the slot under the cursor
}

// ── overlays ─────────────────────────────────────────────────────────────────
static void drawHelp() {
  struct KH { const char* k; uint16_t d; };
  static const KH G[] = {{"TAB", S_K_PAGE}, {"1-8", S_K_JUMP}, {";.", S_K_UPDOWN}, {",/", S_K_LEFTRIGHT}, {"ENT", S_K_OPEN},
                         {"`", S_K_BACK}, {"a A", S_K_ADVERT}, {"d", S_K_DISCOVER}, {"n", S_K_NEWHASH}, {"c", S_K_CHANNELS},
                         {"g", S_K_GPS}, {"l", S_K_SETLOC}, {"\\", S_K_BLACKOUT}, {"i", S_K_HELP}, {"Fn;.", S_K_SCROLL},
                         {"DEL", S_K_DEL}, {"- =", S_K_ZOOM}};
  constexpr int N = sizeof G / sizeof *G;
  for (int i = 0; i < N; i++) {
    int x = 3 + (i / 9) * 120, y = ROW0 + (i % 9) * 11;
    txt(G[i].k, x, y, C_HI);
    txt(tr(G[i].d), x + 30, y, C_MID);
  }
}

static void drawInput() {
  box(10, 34, 220, 52);
  txtFit(inputTitle, 15, 36, C_HI, 35);
  char b[96];
  int n = utf8ToCp437(inputBuf, b, sizeof b), cols = 33;
  const char* tail = n > cols ? b + n - cols : b;
  cv.drawRect(15, 52, 210, 13, C_DIM);
  cv.setTextColor(C_HOT);
  cv.drawString(tail, 18, 55);
  if (millis() / 400 % 2) cv.fillRect(18 + (int)strlen(tail) * 6, 55, 5, 8, C_HI);
  char c[16];
  snprintf(c, sizeof c, "%d/%d", inputLen, inputMax);
  txt(c, 224, 72, C_DIM, 1);
}

static void drawConfirm() {
  box(20, 40, 200, 40);
  txt(tr(S_CONFIRM), W / 2, 42, C_WARN, 2);
  txtFit(confirmText, 26, 58, C_HI, 32);
}

static void drawReply() {
  const Reply& r = app_reply();
  box(4, 15, 232, 96);
  txtFit(r.title, 9, 17, C_HI, 30);
  if (r.waiting) {
    static const char* const SPIN[] = {"-", "\\", "|", "/"};
    txt(SPIN[millis() / 150 % 4], 230, 17, C_HOT, 1);
    txt(tr(S_WAITING), W / 2, 55, C_MID, 2);
    return;
  }
  if (!r.ok) txt(tr(S_FAILED), 230, 17, C_WARN, 1);
  for (int i = 0; i < r.n; i++) txtFit(r.lines[i], 9, 29 + i * 9, r.ok ? C_MID : C_WARN, 37);
}

// ── first-boot setup wizard ──────────────────────────────────────────────────
static const uint16_t WIZ_TITLE[NWIZ] = {S_LANGUAGE, S_NAME, S_TEMPLATE, S_LOCATION, S_BLUETOOTH, S_W_READY};

static void wizOption(int i, const char* s, const char* right = nullptr) {
  int y = 29 + (i - wizTop) * RH;
  bool on = i == wizSel;
  if (on) selBar(y);
  if (on) txt(">", 3, y, C_HI);
  txtFit(s, 12, y, on ? C_HOT : C_MID, right ? 23 : 37);
  if (right) txt(right, W - 3, y, on ? C_HI : C_DIM, 1);
}

static void drawWizard(const Status& st) {
  char s[48];
  txt(tr(WIZ_TITLE[wizStep]), 3, ROW0, C_HI);
  snprintf(s, sizeof s, "%d/%d", wizStep + 1, NWIZ);
  txt(s, W - 3, ROW0, C_DIM, 1);
  cv.drawFastHLine(3, 25, W - 6, C_DIM);
  switch (wizStep) {
    case W_LANG: {
      static const char* const L[] = {"English", "Catal\xc3\xa0", "Espa\xc3\xb1ol"};
      for (int i = 0; i < 3; i++) wizOption(i, L[i]);
      txt(tr(S_W_WELCOME), W / 2, 84, C_HOT, 2);
      txt("MeshCore \xc2\xb7 Cardputer ADV", W / 2, 98, C_DIM, 2);
      break;
    }
    case W_NAME: txt(app_node_name(), W / 2, 96, C_DIM, 2); break;  // under the input box
    case W_PRESET:
      follow(wizSel, wizTop, 7);
      for (int i = wizTop; i < wizTop + 7 && i < NPRESET; i++) {
        snprintf(s, sizeof s, "%.3f SF%d", PRESETS[i].freq, PRESETS[i].sf);
        wizOption(i, PRESETS[i].name, s);
      }
      break;
    case W_LOC: {
      Gps g;
      app_gps(g);
      wizOption(0, tr(S_W_LOC_GPS), g.fix ? "FIX" : g.on ? tr(S_NO_FIX) : "GPS OFF");
      wizOption(1, tr(S_W_LOC_TYPE));
      wizOption(2, tr(S_W_SKIP));
      if (g.nodeLocSet) {
        snprintf(s, sizeof s, "%.5f, %.5f", g.nodeLat, g.nodeLon);
        txt(s, W / 2, 84, C_HI, 2);
      }
      break;
    }
    case W_BLE:
      wizOption(0, tr(S_W_BLE_ON));
      wizOption(1, tr(S_W_BLE_OFF));
      break;
    case W_DONE:
      txt(app_node_name(), W / 2, 32, C_HOT, 2);
      txt(st.preset, W / 2, 46, C_HI, 2);
      snprintf(s, sizeof s, "%.3f MHz  BW%g  SF%d  CR%d", st.freq, st.bw, st.sf, st.cr);
      txt(s, W / 2, 58, C_MID, 2);
      if (st.bleOn) snprintf(s, sizeof s, "BLE PIN %06lu", (unsigned long)st.pin);
      else snprintf(s, sizeof s, "BLE OFF");
      txt(s, W / 2, 74, C_MID, 2);
      txt(tr(S_W_ADVERT), W / 2, 94, C_DIM, 2);
      break;
  }
}

// ── frame ────────────────────────────────────────────────────────────────────
static Status st;
static Metrics mt;
static void draw() {
  app_status(st);
  app_metrics(mt);
  backdrop();
  drawHeader(st);
  if (screen == SC_CHAT) drawChat();
  else if (screen == SC_CONTACT) drawContact(st);
  else if (screen == SC_CHANNELS) drawChannels();
  else if (screen == SC_WIZARD) drawWizard(st);
  else switch (page) {
      case P_HOME: drawHome(st); break;
      case P_CHATS: drawChats(st); break;
      case P_CONTACTS: drawContacts(st); break;
      case P_HEARD: drawHeard(); break;
      case P_METRICS: drawMetrics(mt, st); break;
      case P_GPS: drawGps(st); break;
      case P_MAP: drawMap(); break;
      case P_SETTINGS: drawSettings(); break;
    }
  if (overlay == OV_HELP) { backdrop(), drawHeader(st), drawHelp(); }
  else if (overlay == OV_INPUT) drawInput();
  else if (overlay == OV_CONFIRM) drawConfirm();
  else if (overlay == OV_REPLY) drawReply();
  drawFooter(st, mt);
  cv.pushSprite(0, 0);
}

// ── input ────────────────────────────────────────────────────────────────────
static void startInput(uint8_t kind, int ctx, const char* title, const char* init, int maxLen) {
  overlay = OV_INPUT;
  inputKind = kind, inputCtx = ctx, inputMax = maxLen;
  snprintf(inputTitle, sizeof inputTitle, "%s", title);
  snprintf(inputBuf, sizeof inputBuf, "%s", init);
  inputLen = strlen(inputBuf);
}
static void startConfirm(uint8_t kind, int ctx, const char* text) {
  overlay = OV_CONFIRM;
  confirmKind = kind, confirmCtx = ctx;
  snprintf(confirmText, sizeof confirmText, "%s", text);
}
static void showReply() { overlay = OV_REPLY; }

static void startInput(uint8_t kind, int ctx, const char* title, const char* init, int maxLen);
static void wizGo(int step) {
  Status s;
  app_status(s);
  wizStep = step, wizTop = 0;
  wizSel = step == W_LANG ? app_ui().lang % 3 : step == W_BLE ? !s.bleOn : 0;  // Catalunya is preset 0
  if (step == W_NAME) startInput(IN_WIZ_NAME, 0, tr(S_W_NAME), app_node_name(), 31);
}
static void wizFinish(bool advert) {
  app_ui().setup = 1;
  app_ui_save();
  if (advert) app_advert(true), say(tr(S_ADVERT_FLOOD));
  screen = SC_PAGE, page = P_HOME;
}
static void wizKey(int c) {
  static const int N[NWIZ] = {3, 0, NPRESET, 3, 2, 0};
  if (c == K_UP && wizSel > 0) wizSel--;
  if (c == K_DOWN && wizSel < N[wizStep] - 1) wizSel++;
  if (c == K_ESC) return wizStep == W_DONE ? wizFinish(false) : wizGo(wizStep + 1);  // skip this step
  if (c != '\n') return;
  switch (wizStep) {
    case W_LANG: app_ui().lang = wizSel, app_ui_save(); break;
    case W_NAME: return startInput(IN_WIZ_NAME, 0, tr(S_W_NAME), app_node_name(), 31);
    case W_PRESET: app_apply_preset(wizSel); break;
    case W_LOC:
      if (wizSel == 1) return startInput(IN_WIZ_LOC, 0, tr(S_W_LOC_INPUT), "", 31);
      if (wizSel == 0 && !app_gps_to_node()) {  // no fix yet: switch the GPS on and stay here
        Gps g;
        app_gps(g);
        if (!g.on) app_gps_toggle();
        return say(tr(S_W_WAIT_FIX), true);
      }
      break;
    case W_BLE: {
      Status s;
      app_status(s);
      if (s.bleOn != (wizSel == 0)) app_setting_adjust(app_setting_find(S_BLUETOOTH), 0);  // toggles
      break;
    }
    case W_DONE: return wizFinish(true);
  }
  wizGo(wizStep + 1);
}

static void inputDone() {
  overlay = OV_NONE;
  switch (inputKind) {
    case IN_SETTING:
      if (!app_setting_set_text(inputCtx, inputBuf)) say(tr(S_INVALID), true);
      break;
    case IN_LOGIN:
      app_contact_action(cur.key, CA_LOGIN, inputBuf);
      showReply();
      break;
    case IN_HASHTAG: {
      bool ok = app_add_hashtag(inputBuf);
      say(tr(ok ? S_ADDED : S_INVALID), !ok);
      break;
    }
    case IN_PRIV_NAME:
      snprintf(pendingName, sizeof pendingName, "%s", inputBuf);
      startInput(IN_PRIV_KEY, 0, tr(S_IN_KEY), "", 32);
      break;
    case IN_PRIV_KEY:
      if (app_add_private(pendingName, inputBuf)) say(tr(S_ADDED));
      else say(tr(S_INVALID), true);
      break;
    case IN_WIZ_NAME:  // blank keeps the default name
      if (*inputBuf) app_setting_set_text(app_setting_find(S_NAME), inputBuf);
      wizGo(W_PRESET);
      break;
    case IN_WIZ_LOC:
      if (app_setting_set_text(app_setting_find(S_LOCATION), inputBuf)) wizGo(W_BLE);
      else say(tr(S_INVALID), true);
      break;
  }
}

static void confirmDone(bool yes) {
  overlay = OV_NONE;
  if (!yes) return;
  switch (confirmKind) {
    case CO_SETTING: {
      int r = app_setting_action(confirmCtx);
      if (r == AR_FAIL) say(tr(S_FAILED), true);
      break;
    }
    case CO_DEL_CONTACT:
      app_contact_action(cur.key, CA_DELETE, nullptr);
      screen = SC_PAGE;
      say(tr(S_DELETED));
      break;
    case CO_DEL_CHANNEL:
      say(app_remove_channel(confirmCtx) ? tr(S_DELETED) : tr(S_FAILED));
      break;
  }
}

static bool typing() { return overlay == OV_INPUT || (screen == SC_CHAT && overlay == OV_NONE); }

static void runAction(int a) {
  switch (a) {
    case CA_MESSAGE: {
      ChatId id = {(uint8_t)(cur.type == CT_ROOM ? CK_ROOM : CK_DIRECT), {0}};
      memcpy(id.key, cur.key, 6);
      openChat(id);
      return;
    }
    case CA_LOGIN: startInput(IN_LOGIN, 0, tr(S_IN_PASSWORD), "", 15); return;
    case CA_DELETE: startConfirm(CO_DEL_CONTACT, 0, cur.name); return;
    case CA_STATUS: case CA_TELEMETRY: case CA_TRACE:
      app_contact_action(cur.key, a, nullptr);
      showReply();
      return;
    default:
      say(app_contact_action(cur.key, a, nullptr) ? tr(S_DONE) : tr(S_FAILED));
      return;
  }
}

static void pageKey(int c) {
  int& s = sel[page];
  switch (page) {
    case P_HOME:
      if (c == '\n') {
        page = P_CHATS;
        if (nrecent) openChat(recent[0].chat);
      }
      break;
    case P_CHATS:
      if (c == K_UP && s > 0) s--;
      if (c == K_DOWN && s < nChatRows - 1) s++;
      if (c == K_LEFT || c == K_RIGHT) chatFilter = (chatFilter + (c == K_RIGHT ? 1 : NCF - 1)) % NCF, s = 0;
      if (c == '\n' && nChatRows) openChat(chatRows[s].id);
      if (c == 'n') startInput(IN_HASHTAG, 0, tr(S_IN_HASHTAG), "#", 30);
      if (c == 'c') screen = SC_CHANNELS;
      break;
    case P_CONTACTS: {
      int n = app_contact_count(CONTACT_FILTERS[contactFilter]);
      if (c == K_UP && s > 0) s--;
      if (c == K_DOWN && s < n - 1) s++;
      if (c == K_LEFT || c == K_RIGHT) contactFilter = (contactFilter + (c == K_RIGHT ? 1 : 4)) % 5, s = 0;
      Contact ct;
      if (c == '\n' && app_contact(s, CONTACT_FILTERS[contactFilter], ct)) openContact(ct);
      break;
    }
    case P_HEARD:
      if (c == K_UP && s > 0) s--;
      if (c == K_DOWN && s < nHeard - 1) s++;
      if (c == 'd') { app_discover(); discoverUntil = millis() + 6000; say(tr(S_DISCOVERING)); }
      if (c == '\n' && nHeard) {
        Contact ct;
        if (app_contact_by_key(heardRows[s].key, ct)) openContact(ct);
        else say(tr(S_NOT_CONTACT), true);
      }
      break;
    case P_METRICS:
      if (c == K_LEFT || c == K_RIGHT) metricsView = (metricsView + (c == K_RIGHT ? 1 : 2)) % 3;
      break;
    case P_GPS:
      if (c == K_UP && s > 0) s--;
      if (c == K_DOWN) s++;
      if (c == 'g') app_gps_toggle();
      if (c == 'l') say(app_gps_to_node() ? tr(S_LOC_SET) : tr(S_NO_FIX), true);
      break;
    case P_MAP: {
      if (c == K_UP) mapPan(0, -12);
      if (c == K_DOWN) mapPan(0, 12);
      if (c == K_LEFT) mapPan(-12, 0);
      if (c == K_RIGHT) mapPan(12, 0);
      if ((c == '=' || c == '+') && mapZoom > 0) mapZoom--;
      if (c == '-' && mapZoom < NZOOM - 1) mapZoom++;
      if (c == 'c' && !mapMe(mapLat, mapLon)) say(tr(S_NEED_POS), true);
      if (c == 'n' || c == 'N') {  // jump between located contacts
        int n = app_contact_count(0);
        for (int k = 0; k < n; k++) {
          mapNode = (mapNode + (c == 'n' ? 1 : n - 1) + n) % n;
          Contact ct;
          if (app_contact(mapNode, 0, ct) && (ct.lat || ct.lon)) {
            mapLat = ct.lat / 1e6, mapLon = ct.lon / 1e6;
            break;
          }
        }
      }
      if (c == 'l') {
        char s[32];
        snprintf(s, sizeof s, "%.6f,%.6f", mapLat, mapLon);
        bool ok = app_setting_set_text(app_setting_find(S_LOCATION), s);
        say(tr(ok ? S_LOC_SET : S_FAILED), !ok);
      }
      Contact ct;
      if (c == '\n' && mapNearest(&ct) >= 0) openContact(ct);
      break;
    }
    case P_SETTINGS: {
      int n = app_setting_count();
      auto step = [&](int d) {
        int t = s;
        do t += d; while (t >= 0 && t < n && app_setting_kind(t) == SK_SECTION);
        if (t >= 0 && t < n) s = t;
      };
      if (app_setting_kind(s) == SK_SECTION) step(1);
      if (c == K_UP) step(-1);
      if (c == K_DOWN) step(1);
      int kind = app_setting_kind(s);
      if ((c == K_LEFT || c == K_RIGHT) && kind == SK_ENUM) app_setting_adjust(s, c == K_RIGHT ? 1 : -1);
      if (c == '\n') {
        if (kind == SK_ENUM) app_setting_adjust(s, 0);
        if (kind == SK_TEXT) {
          char b[64];
          app_setting_text(s, b, sizeof b);
          startInput(IN_SETTING, s, tr(app_setting_label(s)), b, 31);
        }
        if (kind == SK_DANGER) startConfirm(CO_SETTING, s, tr(app_setting_label(s)));
        if (kind == SK_ACTION) {
          int r = app_setting_action(s);
          if (r == AR_CHANNELS) screen = SC_CHANNELS;
          if (r == AR_REPLY) showReply();
          if (r == AR_DONE) say(tr(S_DONE));
          if (r == AR_FAIL) say(tr(S_FAILED), true);
        }
      }
      applyTheme();
      hwBright(app_ui().bright);
      break;
    }
  }
}

static void onKey(int c) {
  dirty = true;
  if (overlay == OV_HELP || overlay == OV_REPLY) { overlay = OV_NONE; return; }
  if (overlay == OV_CONFIRM) { confirmDone(c == 'y' || c == 'Y' || c == '\n'); return; }
  if (overlay == OV_INPUT) {
    if (c == K_ESC) {
      overlay = OV_NONE;
      if (inputKind == IN_WIZ_NAME) wizGo(W_PRESET);  // skip the name step
    } else if (c == '\n') inputDone();
    else if (c == '\b') { if (inputLen) inputBuf[--inputLen] = 0; }
    else if (c >= 0x20 && c < 0x7F && inputLen < inputMax) inputBuf[inputLen++] = c, inputBuf[inputLen] = 0;
    return;
  }
  if (screen == SC_WIZARD) { wizKey(c); return; }
  if (screen == SC_CHAT) {
    if (c == K_ESC) { screen = SC_PAGE; app_open_chat(nullptr); return; }
    if (c == K_UP) { chatScroll++; return; }
    if (c == K_DOWN) { if (chatScroll) chatScroll--; return; }
    if (c == '\b') { if (composeLen) compose[--composeLen] = 0; return; }
    if (c == '\n') {
      if (!composeLen) return;
      if (app_send(chatId, compose)) {
        composeLen = 0, compose[0] = 0, chatScroll = 0;
        reloadChat();
      } else {
        say(tr(S_SEND_FAILED), true);
      }
      return;
    }
    if (c >= 0x20 && c < 0x7F && composeLen < app_text_limit(chatId) && composeLen < (int)sizeof compose - 1)
      compose[composeLen++] = c, compose[composeLen] = 0;
    return;
  }
  // navigation keys shared by every non-typing screen
  if (c == 'i') { overlay = OV_HELP; return; }
  if (c == '\\') { dark = true; hwBright(-1); return; }
  if (c == '\t') { screen = SC_PAGE; page = (page + 1) % NPAGE; return; }
  if (c >= '1' && c <= '8') { screen = SC_PAGE; page = c - '1'; return; }
  if (c == 'a' || c == 'A') { app_advert(c == 'A'); say(tr(c == 'A' ? S_ADVERT_FLOOD : S_ADVERT_SENT)); return; }
  if (screen == SC_CONTACT) {
    if (c == K_ESC) screen = SC_PAGE;
    if (c == K_UP && actSel > 0) actSel--;
    if (c == K_DOWN && actSel < nacts - 1) actSel++;
    if (c == K_RIGHT && actSel + 4 < nacts) actSel += 4;
    if (c == K_LEFT && actSel >= 4) actSel -= 4;
    if (c == '\n') runAction(acts[actSel]);
    return;
  }
  if (screen == SC_CHANNELS) {
    if (c == K_ESC) screen = SC_PAGE;
    if (c == K_UP && chanSel > 0) chanSel--;
    if (c == K_DOWN) chanSel++;
    if (c == 'n') startInput(IN_HASHTAG, 0, tr(S_IN_HASHTAG), "#", 30);
    if (c == 'p') startInput(IN_PRIV_NAME, 0, tr(S_IN_CHNAME), "", 30);
    if (c == '\b' && chanSlot > 0) {  // slot 0 (Public) stays
      Chat ch;
      app_channel(chanSlot, ch);
      startConfirm(CO_DEL_CHANNEL, chanSlot, ch.name);
    }
    if (c == '\n' && chanSlot >= 0) {
      Chat ch;
      app_channel(chanSlot, ch);
      char k[33];
      app_channel_key(chanSlot, k);
      const char* L[] = {tr(S_KEY_HEX), k};
      app_reply_show(ch.name, 2, L);
      showReply();
    }
    return;
  }
  if (c == K_ESC) { if (page != P_HOME) page = P_HOME; return; }
  pageKey(c);
}

#ifndef CARDCORE_HOST
static int readKey() {
  auto ks = M5Cardputer.Keyboard.keysState();
  char w = ks.word.empty() ? 0 : ks.word.back();
  bool text = typing();
  if (ks.fn || !text) {  // arrows: Fn+;.,/ everywhere, plain ;.,/ outside text fields
    switch (w) {
      case ';': return K_UP;
      case '.': return K_DOWN;
      case ',': return K_LEFT;
      case '/': return K_RIGHT;
    }
  }
  if (w == '`') return K_ESC;
  if (ks.enter) return '\n';
  if (ks.del) return '\b';
  if (ks.tab) return '\t';
  return (uint8_t)w;
}

static void serviceKeys() {
  static int held = 0;
  static uint32_t repeatAt = 0;
  M5Cardputer.update();
  auto& kb = M5Cardputer.Keyboard;
  uint32_t now = millis();
  if (kb.isChange()) {
    held = kb.isPressed() ? readKey() : 0;
    repeatAt = now + 350;
    if (!held) return;
    lastKeyAt = now;
    if (dark) {  // this key only wakes the screen
      dark = false, held = 0, dirty = true;
      hwBright(app_ui().bright);
      return;
    }
    onKey(held);
  } else if (held && (held >= K_UP || held == '\b') && now >= repeatAt) {
    repeatAt = now + 60;
    lastKeyAt = now;
    onKey(held);
  }
}
#endif

// ── events from the node ─────────────────────────────────────────────────────
static void serviceEvents() {
  Event e;
  while (app_event(e)) {
    dirty = true;
    switch (e.kind) {
      case EV_MSG: {
        bool here = screen == SC_CHAT && e.chat == chatId;
        if (here) reloadChat();
        memmove(recent + 1, recent, sizeof recent - sizeof *recent);
        recent[0].chat = e.chat;
        snprintf(recent[0].line, sizeof recent[0].line, "%s", e.text);
        recent[0].at = millis();
        if (nrecent < 4) nrecent++;
        if (!here) say(e.text);
        hwBeep(true);
        if (app_ui().led) hwLed(0, 40, 12), ledUntil = millis() + 400;
        if (dark) { dark = false; hwBright(app_ui().bright); }
        lastKeyAt = millis();  // keep the screen on to read it
        break;
      }
      case EV_ACK:
      case EV_FAIL:
        if (screen == SC_CHAT && e.chat == chatId) reloadChat();
        if (e.kind == EV_FAIL) say(e.text, true);
        break;
      case EV_REPLY:
        if (overlay != OV_REPLY) say(e.text);
        hwBeep(false);
        break;
      case EV_CONTACT:
      case EV_TOAST:
        say(e.text);
        break;
    }
  }
}

// ── boot ─────────────────────────────────────────────────────────────────────
static void bootScreen() {
  const int cy = H / 2;
  for (uint32_t t0 = millis(), t = 0; t < 900; t = millis() - t0) {
    cv.fillSprite(C_BG);
    if (t < 150) {
      int w = W * t / 150;
      cv.fillRect((W - w) / 2, cy - 1, w, 2, C_HOT);
    } else if (t < 300) {
      int h = 2 + (H - 2) * (t - 150) / 150;
      for (int y = cy - h / 2; y < cy + h / 2; y += 2) cv.drawFastHLine(0, y, W, C_SCAN);
      cv.drawFastHLine(0, cy - h / 2, W, C_HI);
      cv.drawFastHLine(0, cy + h / 2, W, C_HI);
    } else {
      backdrop();
      cv.setTextSize(3);
      txt("CARDCORE", W / 2, 34, t < 600 ? C_MID : C_HOT, 2);
      cv.setTextSize(1);
      txt("MeshCore \xc2\xb7 Cardputer ADV", W / 2, 66, C_MID, 2);
      txt(CARDCORE_VERSION, W - 3, 3, C_DIM, 1);
      txt(tr(S_STARTING), W / 2, 100, C_DIM, 2);
    }
    cv.pushSprite(0, 0);
#ifndef CARDCORE_HOST
    delay(16);
#endif
  }
}

// Called from board.begin(): the canvas is allocated before BLE takes its share of RAM.
void ui_early() {
#ifndef CARDCORE_HOST
  cv.setColorDepth(16);
  if (!cv.createSprite(W, H)) {  // not enough RAM for 16-bit: 8-bit colour
    cv.setColorDepth(8);
    cv.createSprite(W, H);
  }
  cv.setAttribute(lgfx::cp437_switch, true);
  cv.setAttribute(lgfx::utf8_switch, false);
#endif
  applyTheme();
  bootScreen();
}

void ui_begin() {
  applyTheme();
  hwBright(app_ui().bright);
#ifndef CARDCORE_HOST
  if (app_ui().sound) {
    M5Cardputer.Speaker.begin();
  }
#endif
  lastKeyAt = millis();
  dirty = true;
  if (!app_ui().setup) {  // first boot, or after a factory reset
    screen = SC_WIZARD;
    wizGo(W_LANG);
  }
}

void ui_loop() {
  static uint32_t tDraw = 0;
#ifndef CARDCORE_HOST
  serviceKeys();
#endif
  serviceEvents();
  uint32_t now = millis();
  if (ledUntil && now >= ledUntil) { hwLed(0, 0, 0); ledUntil = 0; }
#ifndef CARDCORE_HOST
  if (app_ui().led && tx_led_until > now && !ledUntil) { hwLed(30, 0, 0); ledUntil = tx_led_until; }
#endif
  uint16_t to = TIMEOUT_S[app_ui().timeout % 5];
  if (!dark && to && now - lastKeyAt > to * 1000UL) { dark = true; hwBright(-1); }
  if (dark) return;
  if (dirty || now - tDraw >= (typing() || overlay == OV_REPLY ? 120 : 400)) {
    dirty = false;
    tDraw = now;
    draw();
  }
}

#ifdef CARDCORE_HOST  // test/host.cpp drives screens directly
void ui_host_key(int c) { onKey(c); }
void ui_host_draw() { draw(); }
void ui_host_open_chat(const ChatId& id) { openChat(id); }
void ui_host_wizard(int step, int sel) { screen = SC_WIZARD, overlay = OV_NONE, wizGo(step), wizSel = sel; }
void ui_host_recent(const ChatId& id, const char* line, uint32_t at) {
  recent[nrecent].chat = id;
  snprintf(recent[nrecent].line, sizeof recent[nrecent].line, "%s", line);
  recent[nrecent++].at = at;
}
#endif
