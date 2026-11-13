#pragma once
// Pure helpers (no Arduino): text for the 6x8 CP437 font, word wrap, ages,
// hex keys, the chat log line format, CayenneLPP telemetry. test/host.cpp checks them.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// UTF-8 -> CP437 bytes for the built-in glcd font (Catalan/Spanish accents survive,
// uppercase accents the font lacks lose them, anything else becomes '?').
static int utf8ToCp437(const char* in, char* out, int cap) {
  static const uint16_t MAP[][2] = {
      {0xE1, 0xA0}, {0xE9, 0x82}, {0xED, 0xA1}, {0xF3, 0xA2}, {0xFA, 0xA3}, {0xE0, 0x85}, {0xE8, 0x8A},
      {0xF2, 0x95}, {0xEF, 0x8B}, {0xFC, 0x81}, {0xE7, 0x87}, {0xF1, 0xA4}, {0xD1, 0xA5}, {0xC7, 0x80},
      {0xC9, 0x90}, {0xB7, 0xFA}, {0xBF, 0xA8}, {0xA1, 0xAD}, {0xBA, 0xA7}, {0xAA, 0xA6}, {0xB0, 0xF8},
      {0xE2, 0x83}, {0xEA, 0x88}, {0xF4, 0x93}, {0xE4, 0x84}, {0xF6, 0x94}, {0xC4, 0x8E}, {0xD6, 0x99},
      {0xDC, 0x9A}, {0xC5, 0x8F}, {0xE5, 0x86}, {0xC6, 0x92}, {0xE6, 0x91}, {0xA3, 0x9C}, {0xDF, 0xE1},
      {0xC0, 'A'},  {0xC1, 'A'},  {0xC8, 'E'},  {0xCD, 'I'},  {0xCF, 'I'},  {0xD2, 'O'},  {0xD3, 'O'},
      {0xDA, 'U'},  {0x2019, '\''}, {0x2018, '\''}, {0x201C, '"'}, {0x201D, '"'}, {0x2026, '.'}, {0x2013, '-'},
      {0x2014, '-'}, {0x20AC, 'E'}, {0xBB, 0xAF}, {0xAB, 0xAE}};
  int n = 0;
  const unsigned char* s = (const unsigned char*)in;
  while (*s && n < cap - 1) {
    uint32_t cp;
    int len = *s < 0x80 ? 1 : (*s >> 5) == 6 ? 2 : (*s >> 4) == 14 ? 3 : (*s >> 3) == 30 ? 4 : 1;
    if (len == 1) cp = *s;
    else {
      cp = *s & (0x3F >> (len - 1));
      for (int i = 1; i < len; i++) {
        if ((s[i] & 0xC0) != 0x80) { len = i; cp = '?'; break; }  // truncated sequence
        cp = cp << 6 | (s[i] & 0x3F);
      }
    }
    s += len;
    char c = '?';
    if (cp >= 0x20 && cp < 0x7F) c = (char)cp;
    else if (cp == '\n' || cp == '\t') c = ' ';
    else
      for (auto& m : MAP)
        if (m[0] == cp) { c = (char)m[1]; break; }
    if (cp >= 0xFE00 && cp <= 0xFE0F) continue;  // emoji variation selectors: nothing to draw
    out[n++] = c;
  }
  out[n] = 0;
  return n;
}

// Word wrap a single-byte string into lines of at most `cols`; returns the line count.
static int wrapLines(const char* s, int cols, int start[], int len[], int maxLines) {
  int n = 0, i = 0, total = strlen(s);
  while (i < total && n < maxLines) {
    while (s[i] == ' ' && i < total) i++;  // no leading spaces on wrapped lines
    if (i >= total) break;
    int end = i + cols;
    if (end >= total) end = total;
    else {
      int sp = end;
      while (sp > i && s[sp] != ' ') sp--;
      if (sp > i) end = sp;  // break at the last space; a long word is cut hard
    }
    start[n] = i;
    len[n] = end - i;
    n++;
    i = end;
  }
  if (n == 0 && maxLines > 0) { start[0] = 0; len[0] = 0; n = 1; }
  return n;
}

static void fmtAge(uint32_t secs, char* out, int cap) {
  if (secs < 10) snprintf(out, cap, "now");
  else if (secs < 60) snprintf(out, cap, "%us", (unsigned)secs);
  else if (secs < 3600) snprintf(out, cap, "%um", (unsigned)(secs / 60));
  else if (secs < 86400) snprintf(out, cap, "%uh", (unsigned)(secs / 3600));
  else snprintf(out, cap, "%ud", (unsigned)(secs / 86400));
}

static void toHex(char* out, const uint8_t* b, int n) {
  for (int i = 0; i < n; i++) snprintf(out + i * 2, 3, "%02x", b[i]);
  out[n * 2] = 0;
}

// 32 hex digits -> 16 bytes (spaces ignored). False if it isn't exactly that.
static bool parseHexKey(const char* s, uint8_t out[16]) {
  int n = 0;
  for (; *s; s++) {
    if (*s == ' ' || *s == ':') continue;
    char c = *s | 0x20;
    int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    if (v < 0 || n >= 32) return false;
    if (n % 2 == 0) out[n / 2] = v << 4;
    else out[n / 2] |= v;
    n++;
  }
  return n == 32;
}

// ── chat log: one line per record, tab-separated ────────────────────────────
//   M <ts> <out> <status> <hops> <snr4> <from> <text>   a message
//   K <ts>                                               the sent message <ts> was acknowledged
//   F <ts>                                               ... failed
enum MsgStatus : uint8_t { MS_NONE, MS_PENDING, MS_SENT, MS_ACKED, MS_FAILED };
struct Msg {
  uint32_t ts;
  uint8_t out, status;
  int8_t hops, snr4;  // hops: -1 direct/unknown; snr4: dB x 4
  char from[24];
  char text[168];
};

static int formatMsgLine(const Msg& m, char* out, int cap) {
  char clean[sizeof m.text];
  int j = 0;
  for (const char* p = m.text; *p && j < (int)sizeof clean - 1; p++) clean[j++] = (*p == '\t' || *p == '\n') ? ' ' : *p;
  clean[j] = 0;
  return snprintf(out, cap, "M\t%lu\t%u\t%u\t%d\t%d\t%s\t%s\n", (unsigned long)m.ts, m.out, m.status, m.hops, m.snr4,
                  m.from, clean);
}

// Parses one line (without its '\n'). Returns 'M', 'K', 'F', or 0 if malformed.
static char parseMsgLine(char* line, Msg& m, uint32_t& ts) {
  char* f[8];
  int n = 0;
  for (char* p = line; n < 8;) {
    f[n++] = p;
    char* t = strchr(p, '\t');
    if (!t) break;
    *t = 0;
    p = t + 1;
  }
  if (n < 2 || f[0][1]) return 0;
  ts = strtoul(f[1], nullptr, 10);
  if (f[0][0] == 'K' || f[0][0] == 'F') return f[0][0];
  if (f[0][0] != 'M' || n < 8) return 0;
  m.ts = ts;
  m.out = atoi(f[2]);
  m.status = atoi(f[3]);
  m.hops = atoi(f[4]);
  m.snr4 = atoi(f[5]);
  snprintf(m.from, sizeof m.from, "%s", f[6]);
  snprintf(m.text, sizeof m.text, "%s", f[7]);
  return 'M';
}

// ── CayenneLPP telemetry -> "label value" lines ─────────────────────────────
static int lppFormat(const uint8_t* d, int len, char lines[][40], int maxLines) {
  int n = 0, i = 0;
  auto s16 = [&](int o) { return (int16_t)(d[o] << 8 | d[o + 1]); };
  auto u16 = [&](int o) { return (uint16_t)(d[o] << 8 | d[o + 1]); };
  auto s24 = [&](int o) { int32_t v = d[o] << 16 | d[o + 1] << 8 | d[o + 2]; return v & 0x800000 ? v - 0x1000000 : v; };
  while (i + 2 <= len && n < maxLines) {
    int ch = d[i], type = d[i + 1], o = i + 2, size;
    char* L = lines[n];
    switch (type) {
      case 0: case 1: case 102: case 142: size = 1; snprintf(L, 40, "ch%d digital %d", ch, d[o]); break;
      case 2: case 3: size = 2; snprintf(L, 40, "ch%d analog %.2f", ch, s16(o) / 100.f); break;
      case 101: size = 2; snprintf(L, 40, "ch%d light %u lux", ch, u16(o)); break;
      case 103: size = 2; snprintf(L, 40, "ch%d temp %.1f C", ch, s16(o) / 10.f); break;
      case 104: size = 1; snprintf(L, 40, "ch%d humidity %.1f%%", ch, d[o] / 2.f); break;
      case 115: size = 2; snprintf(L, 40, "ch%d pressure %.1f hPa", ch, u16(o) / 10.f); break;
      case 116: size = 2; snprintf(L, 40, "ch%d voltage %.2f V", ch, u16(o) / 100.f); break;
      case 117: size = 2; snprintf(L, 40, "ch%d current %.3f A", ch, u16(o) / 1000.f); break;
      case 120: size = 1; snprintf(L, 40, "ch%d %u%%", ch, d[o]); break;
      case 121: size = 2; snprintf(L, 40, "ch%d altitude %d m", ch, s16(o)); break;
      case 128: size = 2; snprintf(L, 40, "ch%d power %u W", ch, u16(o)); break;
      case 132: size = 2; snprintf(L, 40, "ch%d direction %u", ch, u16(o)); break;
      case 136:
        size = 9;
        snprintf(L, 40, "ch%d %.4f,%.4f %dm", ch, s24(o) / 10000.f, s24(o + 3) / 10000.f, s24(o + 6) / 100);
        break;
      default: return n;  // unknown type: its size is unknown, stop here
    }
    if (o + size > len) return n;
    i = o + size;
    n++;
  }
  return n;
}

// ── calendar (proleptic Gregorian, UTC; H. Hinnant's algorithms) ────────────
static int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
static void civilFromDays(int32_t z, int& y, int& m, int& d) {
  z += 719468;
  int era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  int mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9, y = yoe + era * 400 + (m <= 2);
}
// EU summer time: from 01:00 UTC on March's last Sunday to 01:00 UTC on October's.
static uint32_t lastSunday0100(int y, int m) {  // m: a 31-day month
  int32_t days = daysFromCivil(y, m, 31);
  return (uint32_t)(days - (days + 4) % 7) * 86400u + 3600;  // (days + 4) % 7: 0 on Sundays
}
static bool euSummer(uint32_t utc) {
  int y, m, d;
  civilFromDays(utc / 86400, y, m, d);
  return utc >= lastSunday0100(y, 3) && utc < lastSunday0100(y, 10);
}

// External power, guessed from battery voltage samples taken every 2 s: the Cardputer ADV wires no
// charge-status or VBUS pin, and on power its battery pin reads the charger's output. Plugging in
// lifts the reading at once, unplugging drops it; a full battery on power sits at 4.2 V.
// Tune the thresholds on hardware if it guesses wrong.
struct ChargeGuess {
  static constexpr int N = 16, JUMP = 50, FALL = 30, FULL = 4180;  // samples (~30 s), mV
  int16_t h[N] = {};
  int n = 0;
  bool on = false;
  bool add(int mv) {
    h[n++ % N] = mv;
    if (n < N) return on = mv >= FULL;
    auto avg = [&](int i) { return (h[i % N] + h[(i + 1) % N] + h[(i + 2) % N]) / 3; };
    int now = avg(n - 3), d = now - avg(n);  // last three samples against the oldest three
    if (now >= FULL || d >= JUMP) on = true;
    else if (d <= -FALL) on = false;
    return on;
  }
};

// Great-circle distance (km) and initial bearing (deg) between two points in degrees.
#include <math.h>
static float geoDistKm(double la1, double lo1, double la2, double lo2, float* bearing) {
  const double R = 6371.0, k = M_PI / 180;
  double p1 = la1 * k, p2 = la2 * k, dp = (la2 - la1) * k, dl = (lo2 - lo1) * k;
  double a = sin(dp / 2) * sin(dp / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  if (bearing) {
    double b = atan2(sin(dl) * cos(p2), cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl)) / k;
    *bearing = (float)fmod(b + 360, 360);
  }
  return (float)(R * 2 * atan2(sqrt(a), sqrt(1 - a)));
}
static const char* compass(float deg) {
  static const char* const P[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return P[(int)((deg + 22.5f) / 45) % 8];
}

// "SOS! 41.41387,2.16082": the alert DM, readable by any MeshCore app; CardCore sounds an alarm.
static bool isSos(const char* t) { return !strncmp(t, "SOS!", 4); }
