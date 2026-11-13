// The node behind the UI: MeshCore glue (chats, contacts, remote requests), chat history
// on SD (internal flash when there's no card), metrics, and the settings table.
#include <Arduino.h>
#include <SD.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <M5Cardputer.h>
#include <algorithm>
#include <helpers/MultiSerialInterface.h>
#include <helpers/TransportKeyStore.h>
#include <helpers/AdvertDataHelpers.h>
#include "MyMesh.h"
#include "target.h"
#include "app.h"
#include "i18n.h"
#include "presets.h"

#ifndef CARDCORE_VERSION
#define CARDCORE_VERSION "dev"
#endif

extern DataStore store;
extern MultiSerialInterface interface_manager;
const char* ui_theme_name(int t);
int ui_theme_count();

static NodePrefs* P() { return the_mesh.getNodePrefs(); }
static uint32_t now() { return rtc_clock.getCurrentTime(); }
static int tzAt(uint32_t utc);  // hours: time zone + summer time

// ── events ───────────────────────────────────────────────────────────────────
static Event events[8];
static int evHead = 0, evCount = 0;
static void push(uint8_t kind, const ChatId* chat, const char* fmt, ...) {
  Event& e = events[(evHead + evCount) % 8];
  if (evCount < 8) evCount++;
  else evHead = (evHead + 1) % 8;  // full: drop the oldest
  e.kind = kind;
  if (chat) e.chat = *chat;
  else memset(&e.chat, 0, sizeof e.chat);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.text, sizeof e.text, fmt, ap);
  va_end(ap);
}
bool app_event(Event& e) {
  if (!evCount) return false;
  e = events[evHead];
  evHead = (evHead + 1) % 8, evCount--;
  return true;
}

// ── UI prefs (NVS) ───────────────────────────────────────────────────────────
static UiPrefs ui = {0, 3, 2, 1, 1, 2, 1, 1, 0, 1};  // green, bright 3, 1 min, Catala, sound, vol 2, LED, BLE, wizard due, EU DST
UiPrefs& app_ui() { return ui; }
static void saveUi() {
  Preferences p;
  if (p.begin("cardcore", false)) p.putBytes("ui", &ui, sizeof ui), p.end();
}
void app_ui_save() { saveUi(); }

// ── chat history ─────────────────────────────────────────────────────────────
// SD: /cardcore/<c|d><key hex>.log, unlimited. Flash: /cc/..., each trimmed to its last 4 KB past 8 KB.
static fs::FS* logFs = &SPIFFS;
static bool onSd = false;
static char trimBuf[4096];

static bool mountSd() {
  SD.end();
  onSd = SD.begin(PIN_SD_CS, SPI, 20000000);
  if (onSd) SD.mkdir("/cardcore");
  logFs = onSd ? (fs::FS*)&SD : (fs::FS*)&SPIFFS;
  return onSd;
}
static const char* logDir() { return onSd ? "/cardcore" : "/cc"; }
static void chatPath(const ChatId& id, char* out, int cap) {
  char h[13];
  toHex(h, id.key, 6);
  snprintf(out, cap, "%s/%c%s.log", logDir(), id.kind >= CK_DIRECT ? 'd' : 'c', h);
}

static void appendLine(const ChatId& id, const char* line) {
  char path[40];
  chatPath(id, path, sizeof path);
  File f = logFs->open(path, FILE_APPEND);
  if (!f) return;
  f.print(line);
  size_t size = f.size();
  f.close();
  if (onSd || size <= 8192) return;
  f = logFs->open(path, FILE_READ);  // flash: keep the newest 4 KB
  if (!f) return;
  f.seek(size - sizeof trimBuf);
  int n = f.read((uint8_t*)trimBuf, sizeof trimBuf);
  f.close();
  char* start = (char*)memchr(trimBuf, '\n', n);
  if (!start) return;
  start++;
  f = logFs->open(path, FILE_WRITE);
  if (f) f.write((uint8_t*)start, trimBuf + n - start), f.close();
}

// Unread counts and last activity per chat (RAM; last activity rebuilt from the logs at boot).
struct Meta { ChatId id; uint16_t unread; uint32_t last; };
static Meta metas[64];
static int nmeta = 0;
static Meta* meta(const ChatId& id, bool create) {
  for (int i = 0; i < nmeta; i++)
    if (metas[i].id == id) return &metas[i];
  if (!create || nmeta >= 64) return nullptr;
  metas[nmeta] = {id, 0, 0};
  return &metas[nmeta++];
}
static ChatId openChat;
static bool chatOpen = false;

static void scanLogs() {
  File dir = logFs->open(logDir());
  if (!dir) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    const char* nm = strrchr(f.name(), '/') ? strrchr(f.name(), '/') + 1 : f.name();
    if (strlen(nm) != 17 || (nm[0] != 'c' && nm[0] != 'd')) continue;
    ChatId id = {(uint8_t)(nm[0] == 'd' ? CK_DIRECT : CK_PUBLIC), {0}};
    for (int i = 0; i < 6; i++) {
      char b[3] = {nm[1 + i * 2], nm[2 + i * 2], 0};
      id.key[i] = strtoul(b, nullptr, 16);
    }
    size_t size = f.size();
    f.seek(size > 400 ? size - 400 : 0);
    char line[420];
    uint32_t last = 0;
    while (f.available()) {
      int n = f.readBytesUntil('\n', line, sizeof line - 1);
      line[n] = 0;
      Msg m;
      uint32_t ts;
      if (parseMsgLine(line, m, ts) == 'M') last = ts;
    }
    if (Meta* mt = meta(id, true)) mt->last = last;
  }
}

// ── pending DMs: retries until acknowledged ──────────────────────────────────
struct Pending { bool used, phone; uint32_t ack, deadline, ts; uint8_t key[32]; uint8_t attempt; ChatId chat; char text[160]; };
static Pending pend[6];

static bool sendDm(Pending& p) {
  ContactInfo* c = the_mesh.lookupContactByPubKey(p.key, PUB_KEY_SIZE);
  if (!c) return false;
  if (p.attempt == 2) the_mesh.resetPathTo(*c);  // last try: flood, the path may be stale
  uint32_t ack, timeout;
  int r = the_mesh.sendMessage(*c, p.ts, p.attempt, p.text, ack, timeout);
  if (r == MSG_SEND_FAILED) return false;
  p.ack = ack;
  p.deadline = millis() + timeout * 6 / 5 + 1000;
  return true;
}

static void resolvePending(Pending& p, bool ok) {
  char line[32];
  snprintf(line, sizeof line, "%c\t%lu\n", ok ? 'K' : 'F', (unsigned long)p.ts);
  appendLine(p.chat, line);
  if (ok) push(EV_ACK, &p.chat, "%s", tr(S_DELIVERED));
  else push(EV_FAIL, &p.chat, "%s: %s", tr(S_SEND_FAILED), app_chat_name(p.chat));
  p.used = false;
}

// ── chats API ────────────────────────────────────────────────────────────────
static uint8_t channelKind(int idx, const char* name) {
  if (idx == 0 || !strcmp(name, "Public")) return CK_PUBLIC;
  return name[0] == '#' ? CK_HASHTAG : CK_PRIVATE;
}
static int findChannel(const ChatId& id, ChannelDetails& out) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
    if (the_mesh.getChannel(i, out) && out.name[0] && !memcmp(out.channel.secret, id.key, 6)) return i;
  return -1;
}
static void fillChat(Chat& c, const ChatId& id, const char* name) {
  c.id = id;
  snprintf(c.name, sizeof c.name, "%s", name);
  Meta* m = meta(id, false);
  c.unread = m ? m->unread : 0;
  c.last = m ? m->last : 0;
}

int app_chats(Chat* out, int max, int filter) {
  int n = 0;
  if (filter != CF_DIRECT) {
    ChannelDetails ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS && n < max; i++) {
      if (!the_mesh.getChannel(i, ch) || !ch.name[0]) continue;
      ChatId id = {channelKind(i, ch.name), {0}};
      memcpy(id.key, ch.channel.secret, 6);
      fillChat(out[n++], id, ch.name);
    }
  }
  if (filter != CF_CHANNELS) {
    int first = n;
    for (int i = 0; i < nmeta && n < max; i++) {
      if (metas[i].id.kind < CK_DIRECT) continue;
      ContactInfo* c = the_mesh.lookupContactByPubKey(metas[i].id.key, 6);
      if (!c) continue;
      ChatId id = metas[i].id;
      id.kind = c->type == ADV_TYPE_ROOM ? CK_ROOM : CK_DIRECT;
      fillChat(out[n++], id, c->name);
    }
    std::sort(out + first, out + n, [](const Chat& a, const Chat& b) { return a.last > b.last; });
  }
  return n;
}

const char* app_chat_name(const ChatId& id) {
  static char name[32];
  if (id.kind >= CK_DIRECT) {
    ContactInfo* c = the_mesh.lookupContactByPubKey(id.key, 6);
    snprintf(name, sizeof name, "%s", c ? c->name : "?");
  } else {
    ChannelDetails ch;
    snprintf(name, sizeof name, "%s", findChannel(id, ch) >= 0 ? ch.name : "?");
  }
  return name;
}

int app_load(const ChatId& id, Msg* out, int max) {
  char path[40];
  chatPath(id, path, sizeof path);
  File f = logFs->open(path, FILE_READ);
  if (!f) return 0;
  size_t size = f.size();
  if (size > 12000) {  // only the tail matters
    f.seek(size - 12000);
    f.readStringUntil('\n');
  }
  int n = 0, head = 0;  // ring of the newest `max`
  char line[420];
  while (f.available()) {
    int len = f.readBytesUntil('\n', line, sizeof line - 1);
    line[len] = 0;
    Msg m;
    uint32_t ts;
    char k = parseMsgLine(line, m, ts);
    if (k == 'M') {
      out[(head + n) % max] = m;
      if (n < max) n++;
      else head = (head + 1) % max;
    } else if (k == 'K' || k == 'F') {
      for (int i = 0; i < n; i++) {
        Msg& o = out[(head + i) % max];
        if (o.out && o.ts == ts) o.status = k == 'K' ? MS_ACKED : MS_FAILED;
      }
    }
  }
  f.close();
  if (n == max && head) std::rotate(out, out + head, out + max);  // oldest first
  // a pending message no longer in the retry table was cut off by a reboot
  for (int i = 0; i < n; i++) {
    if (out[i].status != MS_PENDING) continue;
    bool live = false;
    for (auto& p : pend) live |= p.used && p.ts == out[i].ts;
    if (!live) out[i].status = MS_SENT;
  }
  return n;
}

int app_text_limit(const ChatId& id) {
  if (id.kind >= CK_DIRECT) return MAX_TEXT_LEN - 10;
  return MAX_TEXT_LEN - 2 - (int)strlen(P()->node_name);
}

static void logSent(const ChatId& id, const Msg& m) {
  char line[300];
  formatMsgLine(m, line, sizeof line);
  appendLine(id, line);
  if (Meta* mt = meta(id, true)) mt->last = m.ts;
}

bool app_send(const ChatId& id, const char* text) {
  Msg m = {};
  m.ts = rtc_clock.getCurrentTimeUnique();
  m.out = 1;
  m.hops = -1;
  snprintf(m.from, sizeof m.from, "%s", P()->node_name);
  snprintf(m.text, sizeof m.text, "%s", text);
  if (id.kind < CK_DIRECT) {
    ChannelDetails ch;
    if (findChannel(id, ch) < 0) return false;
    if (!the_mesh.sendGroupMessage(m.ts, ch.channel, P()->node_name, text, strlen(text))) return false;
    m.status = MS_SENT;
  } else {
    Pending* p = nullptr;
    for (auto& q : pend)
      if (!q.used) { p = &q; break; }
    if (!p) return false;  // six messages already waiting for an ack
    ContactInfo* c = the_mesh.lookupContactByPubKey(id.key, 6);
    if (!c) return false;
    *p = {};
    memcpy(p->key, c->id.pub_key, PUB_KEY_SIZE);
    p->ts = m.ts, p->chat = id;
    snprintf(p->text, sizeof p->text, "%s", text);
    if (!sendDm(*p)) return false;
    p->used = true;
    m.status = MS_PENDING;
  }
  logSent(id, m);
  return true;
}

// ── what the phone app sends, into our chats too ─────────────────────────────
// The companion has no hook for it, so its link to the app (BLE, USB) is wrapped: a send command
// from the app is noted, and logged once the firmware's reply says it went out. A DM's reply carries
// the ACK code, so its ✓ shows here as well; the app's own retries reuse the timestamp.
static void phoneSent(const uint8_t* cmd, size_t clen, const uint8_t* resp, size_t rlen) {
  enum { CMD_SEND_TXT = 2, CMD_SEND_CHANNEL_TXT = 3, RESP_OK = 0, RESP_SENT = 6 };
  Msg m = {};
  m.out = 1, m.hops = -1, m.status = MS_SENT;
  snprintf(m.from, sizeof m.from, "%s", P()->node_name);
  ChatId id;
  if (cmd[0] == CMD_SEND_TXT && clen >= 14 && cmd[1] == TXT_TYPE_PLAIN && rlen >= 6 && resp[0] == RESP_SENT) {
    ContactInfo* c = the_mesh.lookupContactByPubKey(cmd + 7, 6);
    if (!c) return;
    id = {(uint8_t)(c->type == ADV_TYPE_ROOM ? CK_ROOM : CK_DIRECT), {0}};
    memcpy(id.key, c->id.pub_key, 6);
    memcpy(&m.ts, cmd + 3, 4);
    uint32_t ack;
    memcpy(&ack, resp + 2, 4);
    for (auto& p : pend)
      if (p.used && p.phone && p.ts == m.ts && p.chat == id) { p.ack = ack, p.deadline = millis() + 60000; return; }  // a retry
    snprintf(m.text, sizeof m.text, "%.*s", (int)(clen - 13), (const char*)cmd + 13);
    for (auto& p : pend)
      if (!p.used) {  // track the ACK; the app does the retrying
        p = {};
        p.used = p.phone = true, p.ack = ack, p.ts = m.ts, p.chat = id, p.deadline = millis() + 60000;
        m.status = MS_PENDING;
        break;
      }
  } else if (cmd[0] == CMD_SEND_CHANNEL_TXT && clen >= 7 && cmd[1] == TXT_TYPE_PLAIN && rlen >= 1 && resp[0] == RESP_OK) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(cmd[2], ch) || !ch.name[0]) return;
    id = {channelKind(cmd[2], ch.name), {0}};
    memcpy(id.key, ch.channel.secret, 6);
    memcpy(&m.ts, cmd + 3, 4);
    snprintf(m.text, sizeof m.text, "%.*s", (int)(clen - 7), (const char*)cmd + 7);
  } else {
    return;
  }
  logSent(id, m);
  push(EV_SENT, &id, "");
}

struct PhoneTap : BaseSerialInterface {
  BaseSerialInterface* in = nullptr;
  uint8_t cmd[MAX_FRAME_SIZE + 1];
  size_t cmdLen = 0;
  void enable() override { in->enable(); }
  void disable() override { in->disable(); }
  bool isEnabled() const override { return in->isEnabled(); }
  bool isConnected() const override { return in->isConnected(); }
  void loop() override { in->loop(); }
  bool isWriteBusy() const override { return in->isWriteBusy(); }
  size_t checkRecvFrame(uint8_t dest[]) override {
    size_t n = in->checkRecvFrame(dest);
    if (n) memcpy(cmd, dest, n), cmdLen = n;
    return n;
  }
  size_t writeFrame(const uint8_t src[], size_t len) override {
    if (cmdLen) phoneSent(cmd, cmdLen, src, len), cmdLen = 0;  // the first frame after a command is its reply
    return in->writeFrame(src, len);
  }
};
static PhoneTap phoneTap;

void app_open_chat(const ChatId* id) {
  chatOpen = id != nullptr;
  if (!id) return;
  openChat = *id;
  if (Meta* m = meta(*id, false)) m->unread = 0;
}

int app_total_unread() {
  int n = 0;
  for (int i = 0; i < nmeta; i++) n += metas[i].unread;
  return n;
}

static void incoming(const ChatId& id, const Msg& m, const char* chatName) {
  char line[300];
  formatMsgLine(m, line, sizeof line);
  appendLine(id, line);
  Meta* mt = meta(id, true);
  if (mt) {
    mt->last = m.ts;
    if (!(chatOpen && openChat == id)) mt->unread++;
  }
  if (id.kind >= CK_DIRECT) push(EV_MSG, &id, "%s: %s", m.from, m.text);
  else push(EV_MSG, &id, "%s %s: %s", chatName, m.from, m.text);
}

// ── channels ─────────────────────────────────────────────────────────────────
int app_channel_count() { return MAX_GROUP_CHANNELS; }
bool app_channel(int idx, Chat& out) {
  ChannelDetails ch;
  if (!the_mesh.getChannel(idx, ch) || !ch.name[0]) return false;
  ChatId id = {channelKind(idx, ch.name), {0}};
  memcpy(id.key, ch.channel.secret, 6);
  fillChat(out, id, ch.name);
  return true;
}
static bool putChannel(const char* name, const uint8_t* secret16) {
  ChannelDetails ch;
  int slot = -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (!the_mesh.getChannel(i, ch)) break;
    if (ch.name[0] && !memcmp(ch.channel.secret, secret16, 16)) return false;  // already there
    if (!ch.name[0] && slot < 0) slot = i;
  }
  if (slot < 0) return false;
  memset(&ch, 0, sizeof ch);
  snprintf(ch.name, sizeof ch.name, "%s", name);
  memcpy(ch.channel.secret, secret16, 16);
  if (!the_mesh.setChannel(slot, ch)) return false;
  store.saveChannels(&the_mesh);
  return true;
}
bool app_add_hashtag(const char* in) {
  char name[32];
  while (*in == ' ' || *in == '#') in++;
  if (!*in) return false;
  snprintf(name, sizeof name, "#%s", in);
  if (strchr(name, ' ')) return false;  // case is kept: the key hashes the exact name
  uint8_t hash[32];
  mesh::Utils::sha256(hash, sizeof hash, (const uint8_t*)name, strlen(name));  // key = sha256("#name")[0..16]
  return putChannel(name, hash);
}
bool app_add_private(const char* name, const char* hexkey) {
  uint8_t key[16];
  if (!*name || !parseHexKey(hexkey, key)) return false;
  return putChannel(name, key);
}
bool app_remove_channel(int idx) {
  if (idx <= 0 || idx >= MAX_GROUP_CHANNELS) return false;
  ChannelDetails ch;
  memset(&ch, 0, sizeof ch);
  if (!the_mesh.setChannel(idx, ch)) return false;
  store.saveChannels(&the_mesh);
  return true;
}
void app_channel_key(int idx, char hex[33]) {
  ChannelDetails ch;
  if (the_mesh.getChannel(idx, ch)) toHex(hex, ch.channel.secret, 16);
  else hex[0] = 0;
}

// ── contacts ─────────────────────────────────────────────────────────────────
static uint16_t order[MAX_CONTACTS + 8];
static int norder = 0, orderFilter = -1;
static uint32_t orderAt = 0;
static void buildOrder(int typeFilter) {
  if (orderFilter == typeFilter && millis() - orderAt < 2000) return;
  orderFilter = typeFilter, orderAt = millis();
  static uint32_t mod[MAX_CONTACTS + 8];
  norder = 0;
  ContactInfo c;
  for (int i = 0; i < the_mesh.getTotalContactSlots() && norder < MAX_CONTACTS + 8; i++) {
    if (!the_mesh.getContactByIdx(i, c) || c.type == ADV_TYPE_NONE || !c.name[0]) continue;
    if (typeFilter && c.type != typeFilter) continue;
    int k = norder++;  // insertion sort, newest first
    while (k > 0 && mod[k - 1] < c.lastmod) order[k] = order[k - 1], mod[k] = mod[k - 1], k--;
    order[k] = i, mod[k] = c.lastmod;
  }
}
static void toContact(const ContactInfo& c, Contact& o) {
  memcpy(o.key, c.id.pub_key, 32);
  snprintf(o.name, sizeof o.name, "%s", c.name);
  o.type = c.type, o.fav = c.isFav();
  if (c.out_path_len == OUT_PATH_UNKNOWN) o.hops = -1, o.hashSize = 1;
  else {
    o.hops = c.out_path_len & 63, o.hashSize = (c.out_path_len >> 6) + 1;
    int n = o.hops * o.hashSize;
    memcpy(o.path, c.out_path, n > 64 ? 64 : n);
  }
  o.lastHeard = c.lastmod, o.lat = c.gps_lat, o.lon = c.gps_lon;
}
int app_contact_count(int typeFilter) {
  buildOrder(typeFilter);
  return norder;
}
bool app_contact(int i, int typeFilter, Contact& out) {
  buildOrder(typeFilter);
  ContactInfo c;
  if (i < 0 || i >= norder || !the_mesh.getContactByIdx(order[i], c)) return false;
  toContact(c, out);
  return true;
}
bool app_contact_by_key(const uint8_t* key6, Contact& out) {
  ContactInfo* c = the_mesh.lookupContactByPubKey(key6, 6);
  if (!c) return false;
  toContact(*c, out);
  return true;
}

// ── remote requests and their replies ────────────────────────────────────────
struct RepeaterStats {  // simple_repeater's status reply
  uint16_t batt_milli_volts, curr_tx_queue_len;
  int16_t noise_floor, last_rssi;
  uint32_t n_packets_recv, n_packets_sent, total_air_time_secs, total_up_time_secs;
  uint32_t n_sent_flood, n_sent_direct, n_recv_flood, n_recv_direct;
  uint16_t err_events;
  int16_t last_snr;
  uint16_t n_direct_dups, n_flood_dups;
  uint32_t total_rx_air_time_secs, n_recv_errors;
};
static Reply reply;
static struct { uint8_t kind; uint8_t key[6]; uint32_t tag, deadline; } req;
const Reply& app_reply() { return reply; }

static void replyStart(const char* title, const char* name) {
  reply.serial++;
  reply.waiting = true, reply.ok = false, reply.n = 0;
  snprintf(reply.title, sizeof reply.title, "%s %s", title, name);
}
static void replyLine(const char* fmt, ...) {
  if (reply.n >= 9) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(reply.lines[reply.n++], 40, fmt, ap);
  va_end(ap);
}
static void replyDone(bool ok) {
  reply.waiting = false, reply.ok = ok;
  reply.serial++;
  req.kind = 0;
  push(EV_REPLY, nullptr, "%s", reply.title);
}
void app_reply_show(const char* title, int n, const char* const* lines) {
  reply.serial++;
  reply.waiting = false, reply.ok = true, reply.n = 0;
  snprintf(reply.title, sizeof reply.title, "%s", title);
  for (int i = 0; i < n; i++) replyLine("%s", lines[i]);
}

static void saveContacts() { store.saveContacts(&the_mesh); }

bool app_contact_action(const uint8_t* key, int action, const char* arg) {
  ContactInfo* c = the_mesh.lookupContactByPubKey(key, PUB_KEY_SIZE);
  if (!c) return false;
  uint32_t tag = 0, timeout = 0;
  int r;
  switch (action) {
    case CA_RESET_PATH: the_mesh.resetPathTo(*c); saveContacts(); return true;
    case CA_SHARE: return the_mesh.shareContactZeroHop(*c);
    case CA_FAV: c->flags ^= 1; saveContacts(); return true;
    case CA_DELETE: { bool ok = the_mesh.removeContact(*c); saveContacts(); orderAt = 0; return ok; }
    case CA_LOGIN:
      replyStart(tr(S_A_LOGIN), c->name);
      r = the_mesh.sendLogin(*c, arg ? arg : "", timeout);
      break;
    case CA_STATUS:
      replyStart(tr(S_A_STATUS), c->name);
      r = the_mesh.sendRequest(*c, REQ_TYPE_GET_STATUS, tag, timeout);
      break;
    case CA_TELEMETRY:
      replyStart(tr(S_A_TELEMETRY), c->name);
      r = the_mesh.sendRequest(*c, REQ_TYPE_GET_TELEMETRY_DATA, tag, timeout);
      break;
    case CA_TRACE: {
      replyStart(tr(S_A_TRACE), c->name);
      int hops = c->out_path_len == OUT_PATH_UNKNOWN ? -1 : c->out_path_len & 63;
      int hs = c->out_path_len == OUT_PATH_UNKNOWN ? 1 : (c->out_path_len >> 6) + 1;
      if (hops < 0 || hs == 3) {  // needs a known path; 3-byte hashes don't fit the trace format
        replyLine("%s", tr(S_NEED_PATH));
        replyDone(false);
        return false;
      }
      uint8_t path[MAX_PATH_SIZE];  // there and back: hops, the node itself, hops reversed
      int n = 0;
      for (int i = 0; i < hops; i++) memcpy(path + n, c->out_path + i * hs, hs), n += hs;
      memcpy(path + n, c->id.pub_key, hs), n += hs;
      for (int i = hops - 1; i >= 0; i--) memcpy(path + n, c->out_path + i * hs, hs), n += hs;
      the_mesh.getRNG()->random((uint8_t*)&tag, 4);
      mesh::Packet* pkt = the_mesh.createTrace(tag, 0, hs == 2 ? 1 : 0);
      if (!pkt) { replyDone(false); return false; }
      the_mesh.sendDirect(pkt, path, n);
      r = MSG_SEND_SENT_DIRECT;
      timeout = 4000 + 1500 * (2 * hops + 1);
      break;
    }
    default: return false;
  }
  if (r == MSG_SEND_FAILED) {
    replyLine("%s", tr(S_SEND_FAILED));
    replyDone(false);
    return false;
  }
  req.kind = action, req.tag = tag;
  memcpy(req.key, c->id.pub_key, 6);
  req.deadline = millis() + (timeout ? timeout : 10000) + 4000;
  return true;
}

static bool onResponse(const ContactInfo& c, uint32_t tag, const uint8_t* d, uint8_t len) {
  if (!req.kind || memcmp(req.key, c.id.pub_key, 6)) return false;
  if (req.kind == CA_LOGIN) {
    bool ok = (len >= 1 && d[0] == RESP_SERVER_LOGIN_OK) || (len >= 2 && !memcmp(d, "OK", 2));
    replyLine("%s", tr(ok ? S_LOGGED_IN : S_LOGIN_FAILED));
    if (ok && len >= 3 && d[0] == RESP_SERVER_LOGIN_OK) {
      replyLine("%s: %s", tr(S_ADMIN), d[2] & 1 ? tr(S_YES) : tr(S_NO));
      // ponytail: no room keep-alive (BaseChatMesh::startConnection is protected); log in again if a room goes quiet
    }
    replyDone(ok);
    return true;
  }
  if (tag != req.tag) return false;
  if (req.kind == CA_STATUS && len >= 48) {  // 56 since rx airtime/errors were added
    RepeaterStats s;
    memcpy(&s, d, std::min((size_t)len, sizeof s));
    uint32_t u = s.total_up_time_secs;
    replyLine("batt %.2fV  queue %u", s.batt_milli_volts / 1000.f, s.curr_tx_queue_len);
    replyLine("noise %d  rssi %d  snr %+.1f", s.noise_floor, s.last_rssi, s.last_snr / 4.f);
    replyLine("rx %lu  tx %lu  err %lu", (unsigned long)s.n_packets_recv, (unsigned long)s.n_packets_sent,
              (unsigned long)(len >= sizeof s ? s.n_recv_errors : s.err_events));
    replyLine("flood tx/rx %lu/%lu", (unsigned long)s.n_sent_flood, (unsigned long)s.n_recv_flood);
    replyLine("direct tx/rx %lu/%lu", (unsigned long)s.n_sent_direct, (unsigned long)s.n_recv_direct);
    replyLine("dups flood/direct %u/%u", s.n_flood_dups, s.n_direct_dups);
    replyLine("air tx %lum  rx %lum", (unsigned long)(s.total_air_time_secs / 60),
              (unsigned long)(len >= sizeof s ? s.total_rx_air_time_secs / 60 : 0));
    replyLine("up %lud %02lu:%02lu", (unsigned long)(u / 86400), (unsigned long)(u / 3600 % 24), (unsigned long)(u / 60 % 60));
    replyDone(true);
    return true;
  }
  if (req.kind == CA_TELEMETRY) {
    int n = lppFormat(d, len, reply.lines, 9);
    reply.n = n;
    if (!n) replyLine("%s", tr(S_NO_DATA));
    replyDone(n > 0);
    return true;
  }
  return false;
}

static void onTrace(mesh::Packet* pkt, uint32_t tag, uint8_t flags, const uint8_t* snrs, const uint8_t* hashes, uint8_t len) {
  if (req.kind != CA_TRACE || tag != req.tag) return;
  int hs = 1 << (flags & 3), hops = len / hs;
  for (int i = 0; i < hops && i < pkt->path_len && reply.n < 8; i++) {
    char h[9];
    toHex(h, hashes + i * hs, hs);
    replyLine("%2d  %-6s %+.1f dB", i + 1, h, (int8_t)snrs[i] / 4.f);
  }
  replyLine("%s %+.1f dB", tr(S_BACK_HERE), pkt->getSNR());
  replyDone(true);
}

// ── heard ────────────────────────────────────────────────────────────────────
static Heard heard[24];
static int nheard = 0;
static uint32_t discTag = 0;
static Heard& heardSlot(const uint8_t* key6) {
  for (int i = 0; i < nheard; i++)
    if (!memcmp(heard[i].key, key6, 6)) return heard[i];
  int i = nheard < 24 ? nheard++ : 0;
  if (nheard == 24 && i == 0)  // full: replace the oldest
    for (int k = 1; k < 24; k++)
      if (heard[k].at < heard[i].at) i = k;
  memset(&heard[i], 0, sizeof heard[i]);
  memcpy(heard[i].key, key6, 6);
  return heard[i];
}
int app_heard(Heard* out, int max) {
  int n = 0;
  for (int i = 0; i < nheard && n < max; i++) {
    out[n] = heard[i];
    out[n].known = the_mesh.lookupContactByPubKey(heard[i].key, 6) != nullptr;
    n++;
  }
  std::sort(out, out + n, [](const Heard& a, const Heard& b) { return a.at > b.at; });
  return n;
}
void app_advert(bool flood) {
  mesh::Packet* pkt = P()->advert_loc_policy == ADVERT_LOC_NONE
                          ? the_mesh.createSelfAdvert(P()->node_name)
                          : the_mesh.createSelfAdvert(P()->node_name, sensors.node_lat, sensors.node_lon);
  if (!pkt) return;
  if (!flood) return the_mesh.sendZeroHop(pkt);
  TransportKey scope;
  memcpy(scope.key, P()->default_scope_key, sizeof scope.key);
  if (scope.isNull()) the_mesh.sendFlood(pkt, (uint32_t)0, P()->path_hash_mode + 1);
  else {
    uint16_t codes[2] = {scope.calcTransportCode(pkt), 0};
    the_mesh.sendFlood(pkt, codes, 0, P()->path_hash_mode + 1);
  }
}
void app_discover() {
  uint8_t b[6];
  b[0] = CTL_TYPE_NODE_DISCOVER_REQ | 1;  // prefix-only answers
  b[1] = 0xFF;  // any type
  the_mesh.getRNG()->random((uint8_t*)&discTag, 4);
  memcpy(&b[2], &discTag, 4);
  if (mesh::Packet* pkt = the_mesh.createControlData(b, sizeof b)) the_mesh.sendZeroHop(pkt);
}

// ── status, metrics, GPS ─────────────────────────────────────────────────────
static int queueLen = 0;
static uint16_t battMv = 0;
static int8_t battPct = -1;
static bool charging = false;
static uint8_t rxHist[HIST];
static int8_t noiseHist[HIST];
static uint32_t airMin[60], airMinT[60], lastRx = 0;  // TX airtime sampled each minute, for the 1 h duty cycle
static int airMinHead = 0, airMinN = 0;
static RxLog rxlog[8];
static int nrxlog = 0;

void app_status(Status& s) {
  NodePrefs* p = P();
  s.battMv = battMv, s.battPct = battPct, s.charging = charging;
  s.bleOn = interface_manager.isBluetoothEnabled();
  s.bleConn = interface_manager.isConnected();
  s.usbConn = HWCDC::isPlugged();
  LocationProvider* lp = sensors.getLocationProvider();
  s.gpsOn = p->gps_enabled && lp;
  s.gpsFix = s.gpsOn && lp->isValid();
  s.sd = onSd;
  s.now = now();
  s.timeOk = s.now > 1700000000;
  s.tz = tzAt(s.now);
  s.pin = the_mesh.getBLEPin();
  s.freq = p->freq, s.bw = p->bw, s.sf = p->sf, s.cr = p->cr, s.txPower = p->tx_power_dbm;
  int pm = presetMatch(p->freq, p->bw, p->sf, p->cr, p->path_hash_mode + 1);
  snprintf(s.preset, sizeof s.preset, "%s", pm >= 0 ? PRESETS[pm].name : tr(S_CUSTOM));
  s.txNow = tx_led_until > millis();
}
const char* app_node_name() { return P()->node_name; }
void app_pubkey_hex(char out[65]) { toHex(out, the_mesh.self_id.pub_key, 32); }

void app_metrics(Metrics& m) {
  m.rx = radio_driver.getPacketsRecv(), m.tx = radio_driver.getPacketsSent(), m.rxErr = radio_driver.getPacketsRecvErrors();
  m.floodTx = the_mesh.getNumSentFlood(), m.floodRx = the_mesh.getNumRecvFlood();
  m.directTx = the_mesh.getNumSentDirect(), m.directRx = the_mesh.getNumRecvDirect();
  m.airTxMs = the_mesh.getTotalAirTime(), m.airRxMs = the_mesh.getReceiveAirTime();
  m.uptimeS = millis() / 1000;
  int o = (airMinHead + 60 - airMinN) % 60;
  uint32_t span = airMinN ? millis() - airMinT[o] : 0;
  m.duty1h = span > 1000 ? (m.airTxMs - airMin[o]) * 100.f / span : 0;
  m.noise = radio_driver.getNoiseFloor();
  m.lastRssi = radio_driver.getLastRSSI(), m.lastSnr = radio_driver.getLastSNR();
  m.heap = ESP.getFreeHeap(), m.heapMin = ESP.getMinFreeHeap();
  m.queue = queueLen;
  m.contacts = the_mesh.getNumContacts();
  m.channels = 0;
  ChannelDetails ch;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
    if (the_mesh.getChannel(i, ch) && ch.name[0]) m.channels++;
  memcpy(m.rxHist, rxHist, sizeof rxHist);
  memcpy(m.noiseHist, noiseHist, sizeof noiseHist);
}
int app_rx_log(RxLog* out, int max) {
  int n = std::min(nrxlog, max);
  memcpy(out, rxlog, n * sizeof *out);
  return n;
}

void app_gps(Gps& g) {
  LocationProvider* lp = sensors.getLocationProvider();
  g.on = P()->gps_enabled && lp;
  g.fix = g.on && lp->isValid();
  g.sats = g.on ? lp->satellitesCount() : 0;
  g.lat = g.fix ? lp->getLatitude() / 1e6 : 0;
  g.lon = g.fix ? lp->getLongitude() / 1e6 : 0;
  g.altM = g.fix ? lp->getAltitude() / 1000 : 0;
  g.nodeLat = sensors.node_lat, g.nodeLon = sensors.node_lon;
  g.nodeLocSet = g.nodeLat != 0 || g.nodeLon != 0;
}
void app_gps_toggle() {
  P()->gps_enabled ^= 1;
  the_mesh.applyGpsPrefs();
  the_mesh.savePrefs();
}
bool app_gps_to_node() {
  Gps g;
  app_gps(g);
  if (!g.fix) return false;
  sensors.node_lat = g.lat, sensors.node_lon = g.lon;
  the_mesh.savePrefs();
  return true;
}

// ── settings ─────────────────────────────────────────────────────────────────
enum SetId {
  ST_SEC_RADIO, ST_TEMPLATE, ST_FREQ, ST_BW, ST_SF, ST_CR, ST_TX, ST_HASH, ST_RXBOOST, ST_SCOPE, ST_RELAY,
  ST_SEC_NODE, ST_NAME, ST_LOCATION, ST_SHARELOC, ST_ADVERT, ST_FLOODADV, ST_CHANNELS,
  ST_SEC_CONTACTS, ST_AUTOADD, ST_ADD_CHAT, ST_ADD_RPT, ST_ADD_ROOM, ST_ADD_SENS, ST_OVERWRITE, ST_MAXHOPS,
  ST_SEC_PHONE, ST_BLE, ST_PIN,
  ST_SEC_GPS, ST_GPS, ST_GPSINT,
  ST_SEC_TIME, ST_TZ, ST_DST, ST_SETTIME,
  ST_SEC_DISPLAY, ST_THEME, ST_BRIGHT, ST_TIMEOUT, ST_LANG,
  ST_SEC_ALERTS, ST_SOUND, ST_VOL, ST_LED,
  ST_SEC_SYSTEM, ST_SD, ST_ABOUT, ST_REBOOT, ST_RESET, NSET
};
static const struct { uint16_t label; uint8_t kind; } SETTINGS[NSET] = {
    {S_RADIO, SK_SECTION},       {S_TEMPLATE, SK_ENUM},     {S_FREQ, SK_TEXT},        {S_BW, SK_ENUM},
    {S_SF, SK_ENUM},             {S_CR, SK_ENUM},           {S_TXPOWER, SK_ENUM},     {S_PATHHASH, SK_ENUM},
    {S_RXBOOST, SK_ENUM},        {S_SCOPE, SK_TEXT},        {S_RELAY, SK_ENUM},       {S_NODE, SK_SECTION},
    {S_NAME, SK_TEXT},           {S_LOCATION, SK_TEXT},     {S_SHARELOC, SK_ENUM},    {S_ADVERT, SK_ACTION},
    {S_FLOODADV, SK_ACTION},     {S_CHANNELS, SK_ACTION},   {S_CONTACTS, SK_SECTION}, {S_AUTOADD, SK_ENUM},
    {S_ADD_CHAT, SK_ENUM},       {S_ADD_RPT, SK_ENUM},      {S_ADD_ROOM, SK_ENUM},    {S_ADD_SENS, SK_ENUM},
    {S_OVERWRITE, SK_ENUM},      {S_MAXHOPS, SK_ENUM},      {S_PHONE, SK_SECTION},    {S_BLUETOOTH, SK_ENUM},
    {S_PIN, SK_TEXT},            {S_GPS, SK_SECTION},       {S_GPS, SK_ENUM},         {S_GPSINT, SK_ENUM},
    {S_TIME_SEC, SK_SECTION},    {S_TIMEZONE, SK_ENUM},     {S_DST, SK_ENUM},         {S_SETTIME, SK_TEXT},
    {S_DISPLAY, SK_SECTION},   {S_THEME, SK_ENUM},       {S_BRIGHT, SK_ENUM},
    {S_TIMEOUT, SK_ENUM},        {S_LANGUAGE, SK_ENUM},     {S_ALERTS, SK_SECTION},   {S_SOUND, SK_ENUM},
    {S_VOLUME, SK_ENUM},         {S_LED, SK_ENUM},          {S_SYSTEM, SK_SECTION},   {S_SDCARD, SK_ACTION},
    {S_ABOUT, SK_ACTION},        {S_REBOOT, SK_DANGER},     {S_FACTORY, SK_DANGER},
};
int app_setting_count() { return NSET; }
int app_setting_find(uint16_t label) {
  for (int i = 0; i < NSET; i++)
    if (SETTINGS[i].label == label && SETTINGS[i].kind != SK_SECTION) return i;
  return -1;
}
uint16_t app_setting_label(int i) { return SETTINGS[i].label; }
uint8_t app_setting_kind(int i) { return SETTINGS[i].kind; }

static const uint16_t GPS_INT[] = {1, 5, 30, 60, 300, 900};
static const char* const TIMEOUTS[] = {"15s", "30s", "1m", "5m", "--"};
static const char* const LANGS[] = {"ENGLISH", "CATAL\xc3\x80", "ESPA\xc3\x91OL"};
static int tplSel = -1;  // template being browsed (applied on ENTER)

static int tzAt(uint32_t utc) { return P()->tz_offset + (ui.dst && euSummer(utc)); }
static void fmtLocal(char* o, int cap) {  // "YYYY-MM-DD HH:MM", local
  uint32_t t = now() + tzAt(now()) * 3600;
  int y, m, d;
  civilFromDays(t / 86400, y, m, d);
  snprintf(o, cap, "%04d-%02d-%02d %02u:%02u", y, m, d, (unsigned)(t / 3600 % 24), (unsigned)(t / 60 % 60));
}
static bool relayFreqOk(float f) {
  long k = lroundf(f * 1000);
  return k == 433000 || k == 869495 || k == 918000;  // MeshCore's client-repeat frequencies
}
static void applyRadio() {
  NodePrefs* p = P();
  radio_driver.setParams(p->freq, p->bw, p->sf, p->cr);
  radio_driver.setTxPower(p->tx_power_dbm);
  if (p->isRepeatEn() && !relayFreqOk(p->freq)) p->setRepeatEn(false);
  the_mesh.savePrefs();
}
void app_apply_preset(int i) {
  if (i < 0 || i >= NPRESET) return;
  NodePrefs* p = P();
  const Preset& t = PRESETS[i];
  p->freq = t.freq, p->bw = t.bw, p->sf = t.sf, p->cr = t.cr, p->path_hash_mode = t.hash - 1;
  applyRadio();
}
static const char* onOff(bool v) { return v ? tr(S_ON) : tr(S_OFF); }
template <class T> static T wrapv(int v, int lo, int hi) { return (T)(v < lo ? hi : v > hi ? lo : v); }

void app_setting_value(int i, char* o, int cap) {
  NodePrefs* p = P();
  *o = 0;
  switch (i) {
    case ST_TEMPLATE: {
      int cur = presetMatch(p->freq, p->bw, p->sf, p->cr, p->path_hash_mode + 1);
      if (tplSel >= 0 && tplSel != cur) snprintf(o, cap, "> %s", PRESETS[tplSel].name);
      else snprintf(o, cap, "%s", cur >= 0 ? PRESETS[cur].name : tr(S_CUSTOM));
      break;
    }
    case ST_FREQ: snprintf(o, cap, "%.3f MHz", p->freq); break;
    case ST_BW: snprintf(o, cap, "%g kHz", p->bw); break;
    case ST_SF: snprintf(o, cap, "SF%d", p->sf); break;
    case ST_CR: snprintf(o, cap, "4/%d", p->cr); break;
    case ST_TX: snprintf(o, cap, "%d dBm", p->tx_power_dbm); break;
    case ST_HASH: snprintf(o, cap, "%d B", p->path_hash_mode + 1); break;
    case ST_RXBOOST: snprintf(o, cap, "%s", onOff(p->rx_boosted_gain)); break;
    case ST_SCOPE: snprintf(o, cap, "%s", p->default_scope_name[0] ? p->default_scope_name : tr(S_NONE)); break;
    case ST_RELAY: snprintf(o, cap, "%s", relayFreqOk(p->freq) ? onOff(p->isRepeatEn()) : tr(S_RELAY_NA)); break;
    case ST_NAME: snprintf(o, cap, "%s", p->node_name); break;
    case ST_LOCATION:
      if (sensors.node_lat || sensors.node_lon) snprintf(o, cap, "%.4f,%.4f", sensors.node_lat, sensors.node_lon);
      else snprintf(o, cap, "%s", tr(S_UNSET));
      break;
    case ST_SHARELOC: snprintf(o, cap, "%s", onOff(p->advert_loc_policy != ADVERT_LOC_NONE)); break;
    case ST_ADVERT: case ST_FLOODADV: case ST_CHANNELS: case ST_ABOUT: snprintf(o, cap, ">"); break;
    case ST_AUTOADD: snprintf(o, cap, "%s", tr(p->manual_add_contacts & 1 ? S_BYTYPE : S_ALL)); break;
    case ST_ADD_CHAT: case ST_ADD_RPT: case ST_ADD_ROOM: case ST_ADD_SENS:
      snprintf(o, cap, "%s", onOff(p->autoadd_config & (1 << (i - ST_ADD_CHAT + 1))));
      break;
    case ST_OVERWRITE: snprintf(o, cap, "%s", onOff(p->autoadd_config & 1)); break;
    case ST_MAXHOPS:
      if (!p->autoadd_max_hops) snprintf(o, cap, "%s", tr(S_ANY));
      else snprintf(o, cap, "%d", p->autoadd_max_hops - 1);
      break;
    case ST_BLE: snprintf(o, cap, "%s", onOff(ui.ble)); break;
    case ST_PIN: snprintf(o, cap, "%06lu%s", (unsigned long)the_mesh.getBLEPin(), p->ble_pin ? "" : " (rnd)"); break;
    case ST_GPS: snprintf(o, cap, "%s", onOff(p->gps_enabled)); break;
    case ST_GPSINT: snprintf(o, cap, "%lus", (unsigned long)(p->gps_interval ? p->gps_interval : 1)); break;
    case ST_TZ: snprintf(o, cap, "UTC%+d", p->tz_offset); break;
    case ST_DST: snprintf(o, cap, "%s", ui.dst ? tr(S_AUTO_EU) : tr(S_OFF)); break;
    case ST_SETTIME:
      if (now() > 1700000000) fmtLocal(o, cap);
      else snprintf(o, cap, "%s", tr(S_UNSET));
      break;
    case ST_THEME: snprintf(o, cap, "%s", ui_theme_name(ui.theme)); break;
    case ST_BRIGHT: snprintf(o, cap, "%d/5", ui.bright + 1); break;
    case ST_TIMEOUT: snprintf(o, cap, "%s", TIMEOUTS[ui.timeout % 5]); break;
    case ST_LANG: snprintf(o, cap, "%s", LANGS[ui.lang % 3]); break;
    case ST_SOUND: snprintf(o, cap, "%s", onOff(ui.sound)); break;
    case ST_VOL: snprintf(o, cap, "%d/4", ui.vol + 1); break;
    case ST_LED: snprintf(o, cap, "%s", onOff(ui.led)); break;
    case ST_SD:
      if (onSd) snprintf(o, cap, "OK %lluM %s", (SD.totalBytes() - SD.usedBytes()) >> 20, tr(S_FREE));
      else snprintf(o, cap, "%s", tr(S_SD_NONE));
      break;
    case ST_REBOOT: case ST_RESET: snprintf(o, cap, "!"); break;
  }
}

void app_setting_adjust(int i, int d) {
  NodePrefs* p = P();
  int step = d ? d : 1;  // ENTER steps forward
  switch (i) {
    case ST_TEMPLATE: {
      if (!d) {  // ENTER applies the browsed template
        if (tplSel < 0) return;
        app_apply_preset(tplSel);
        push(EV_TOAST, nullptr, "%s: %s", tr(S_APPLIED), PRESETS[tplSel].name);
        tplSel = -1;
        return;
      }
      int cur = tplSel >= 0 ? tplSel : presetMatch(p->freq, p->bw, p->sf, p->cr, p->path_hash_mode + 1);
      tplSel = cur < 0 ? 0 : (cur + d + NPRESET) % NPRESET;
      return;
    }
    case ST_BW: {
      int k = 0;
      for (int j = 0; j < NBW; j++)
        if (fabsf(BANDWIDTHS[j] - p->bw) < fabsf(BANDWIDTHS[k] - p->bw)) k = j;
      p->bw = BANDWIDTHS[(k + step + NBW) % NBW];
      return applyRadio();
    }
    case ST_SF: p->sf = wrapv<uint8_t>(p->sf + step, 5, 12); return applyRadio();
    case ST_CR: p->cr = wrapv<uint8_t>(p->cr + step, 5, 8); return applyRadio();
    case ST_TX: p->tx_power_dbm = wrapv<int8_t>(p->tx_power_dbm + step, -9, MAX_LORA_TX_POWER); return applyRadio();
    case ST_HASH: p->path_hash_mode = wrapv<uint8_t>(p->path_hash_mode + step, 0, 2); return applyRadio();
    case ST_RXBOOST:
      p->rx_boosted_gain ^= 1;
      radio_driver.setRxBoostedGainMode(p->rx_boosted_gain);
      the_mesh.savePrefs();
      return;
    case ST_RELAY:
      if (!relayFreqOk(p->freq)) return push(EV_TOAST, nullptr, "%s", tr(S_RELAY_NA));
      p->setRepeatEn(!p->isRepeatEn());
      the_mesh.savePrefs();
      return;
    case ST_SHARELOC:
      p->advert_loc_policy = p->advert_loc_policy == ADVERT_LOC_NONE ? ADVERT_LOC_SHARE : ADVERT_LOC_NONE;
      the_mesh.savePrefs();
      return;
    case ST_AUTOADD: p->manual_add_contacts ^= 1; the_mesh.savePrefs(); return;
    case ST_ADD_CHAT: case ST_ADD_RPT: case ST_ADD_ROOM: case ST_ADD_SENS:
      p->autoadd_config ^= 1 << (i - ST_ADD_CHAT + 1);
      the_mesh.savePrefs();
      return;
    case ST_OVERWRITE: p->autoadd_config ^= 1; the_mesh.savePrefs(); return;
    case ST_MAXHOPS: p->autoadd_max_hops = wrapv<uint8_t>(p->autoadd_max_hops + step, 0, 9); the_mesh.savePrefs(); return;
    case ST_BLE:
      ui.ble ^= 1;
      if (ui.ble) interface_manager.enableBluetooth();
      else interface_manager.disableBluetooth();
      return saveUi();
    case ST_GPS: return app_gps_toggle();
    case ST_GPSINT: {
      int k = 0;
      for (int j = 0; j < 6; j++)
        if (GPS_INT[j] == p->gps_interval) k = j;
      p->gps_interval = GPS_INT[(k + step + 6) % 6];
      the_mesh.applyGpsPrefs();
      the_mesh.savePrefs();
      return;
    }
    case ST_TZ: p->tz_offset = wrapv<int8_t>(p->tz_offset + step, -12, 14); the_mesh.savePrefs(); return;
    case ST_DST: ui.dst ^= 1; return saveUi();
    case ST_THEME: ui.theme = (ui.theme + step + ui_theme_count()) % ui_theme_count(); return saveUi();
    case ST_BRIGHT: ui.bright = (ui.bright + step + 5) % 5; return saveUi();
    case ST_TIMEOUT: ui.timeout = (ui.timeout + step + 5) % 5; return saveUi();
    case ST_LANG: ui.lang = (ui.lang + step + 3) % 3; return saveUi();
    case ST_SOUND:
      ui.sound ^= 1;
      if (ui.sound) M5Cardputer.Speaker.begin();
      return saveUi();
    case ST_VOL: ui.vol = (ui.vol + step + 4) % 4; return saveUi();
    case ST_LED: ui.led ^= 1; return saveUi();
  }
}

void app_setting_text(int i, char* o, int cap) {
  NodePrefs* p = P();
  *o = 0;
  if (i == ST_FREQ) snprintf(o, cap, "%.3f", p->freq);
  if (i == ST_SCOPE) snprintf(o, cap, "%s", p->default_scope_name);
  if (i == ST_NAME) snprintf(o, cap, "%s", p->node_name);
  if (i == ST_LOCATION && (sensors.node_lat || sensors.node_lon)) snprintf(o, cap, "%.6f,%.6f", sensors.node_lat, sensors.node_lon);
  if (i == ST_PIN && p->ble_pin) snprintf(o, cap, "%06lu", (unsigned long)p->ble_pin);
  if (i == ST_SETTIME) {
    if (now() > 1700000000) fmtLocal(o, cap);
    else snprintf(o, cap, "2026-01-01 12:00");  // a template to edit
  }
}

bool app_setting_set_text(int i, const char* t) {
  NodePrefs* p = P();
  switch (i) {
    case ST_FREQ: {
      float f = atof(t);
      if (f < 850 || f > 930) return false;  // the Cap's SX1262 front end: 868-923 MHz nominal
      p->freq = f;
      applyRadio();
      return true;
    }
    case ST_SCOPE: {  // region name without '#'; empty = unscoped
      while (*t == '#' || *t == ' ') t++;
      if (!*t) {
        memset(p->default_scope_name, 0, sizeof p->default_scope_name);
        memset(p->default_scope_key, 0, sizeof p->default_scope_key);
      } else {
        char name[31], hashed[33];
        snprintf(name, sizeof name, "%s", t);
        for (char* c = name; *c; c++) {
          *c = tolower(*c);
          if (!isalnum(*c) && *c != '-' && *c != '_') return false;
        }
        snprintf(hashed, sizeof hashed, "#%s", name);
        TransportKeyStore tmp;
        TransportKey key;
        tmp.getAutoKeyFor(0, hashed, key);
        memcpy(p->default_scope_key, key.key, sizeof p->default_scope_key);
        snprintf(p->default_scope_name, sizeof p->default_scope_name, "%s", name);
      }
      the_mesh.savePrefs();
      return true;
    }
    case ST_NAME:
      if (!*t) return false;
      snprintf(p->node_name, sizeof p->node_name, "%s", t);
      the_mesh.savePrefs();
      return true;
    case ST_LOCATION: {
      if (!*t) { sensors.node_lat = sensors.node_lon = 0; the_mesh.savePrefs(); return true; }
      const char* comma = strchr(t, ',');
      if (!comma) return false;
      double la = atof(t), lo = atof(comma + 1);
      if (la < -90 || la > 90 || lo < -180 || lo > 180) return false;
      sensors.node_lat = la, sensors.node_lon = lo;
      the_mesh.savePrefs();
      return true;
    }
    case ST_SETTIME: {  // local "YYYY-MM-DD HH:MM[:SS]"
      int y, mo, d, h, mi, sec = 0;
      if (sscanf(t, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 5) return false;
      if (y < 2024 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 59) return false;
      uint32_t local = (uint32_t)daysFromCivil(y, mo, d) * 86400u + h * 3600 + mi * 60 + sec;
      uint32_t utc = local - P()->tz_offset * 3600;
      utc -= (ui.dst && euSummer(utc - 3600)) * 3600;
      if (utc < now()) {  // as MeshCore's own set-time: peers would take older adverts for replays
        push(EV_TOAST, nullptr, "%s", tr(S_TIME_BACK));
        return true;
      }
      rtc_clock.setCurrentTime(utc);
      push(EV_TOAST, nullptr, "%s", tr(S_TIME_SET));
      return true;
    }
    case ST_PIN: {
      if (!*t) { p->ble_pin = 0; the_mesh.savePrefs(); push(EV_TOAST, nullptr, "%s", tr(S_REBOOT_APPLY)); return true; }
      if (strlen(t) != 6) return false;
      for (const char* c = t; *c; c++)
        if (!isdigit(*c)) return false;
      p->ble_pin = atol(t);
      the_mesh.savePrefs();
      push(EV_TOAST, nullptr, "%s", tr(S_REBOOT_APPLY));
      return true;
    }
  }
  return false;
}

int app_setting_action(int i) {
  switch (i) {
    case ST_ADVERT: app_advert(false); return AR_DONE;
    case ST_FLOODADV: app_advert(true); return AR_DONE;
    case ST_CHANNELS: return AR_CHANNELS;
    case ST_SD: return mountSd() ? (scanLogs(), AR_DONE) : AR_FAIL;
    case ST_ABOUT: {
      char key[65], a[40], b[40], c[40], d[40], e[40];
      app_pubkey_hex(key);
      snprintf(a, sizeof a, "CardCore %s", CARDCORE_VERSION);
      snprintf(b, sizeof b, "MeshCore %s (%s)", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
      snprintf(c, sizeof c, "%.32s", key);
      snprintf(d, sizeof d, "%.32s", key + 32);
      snprintf(e, sizeof e, "heap %luK  min %luK", (unsigned long)(ESP.getFreeHeap() / 1024),
               (unsigned long)(ESP.getMinFreeHeap() / 1024));
      const char* L[] = {a, b, tr(S_PUBKEY), c, d, e, "github.com/meshcore-dev/MeshCore"};
      app_reply_show(tr(S_ABOUT), 7, L);
      return AR_REPLY;
    }
    case ST_REBOOT: ESP.restart(); return AR_DONE;
    case ST_RESET: {
      store.formatFileSystem();
      Preferences p;
      if (p.begin("cardcore", false)) p.clear(), p.end();
      ESP.restart();
      return AR_DONE;
    }
  }
  return AR_FAIL;
}

// ── SOS ──────────────────────────────────────────────────────────────────────
int app_sos(bool send) {
  Gps g;
  app_gps(g);
  double la = g.fix ? g.lat : g.nodeLat, lo = g.fix ? g.lon : g.nodeLon;
  char t[48];
  if (la || lo) snprintf(t, sizeof t, "SOS! %.5f,%.5f", la, lo);
  else snprintf(t, sizeof t, "SOS!");
  int n = 0;
  ContactInfo c;
  for (int i = 0; i < the_mesh.getTotalContactSlots(); i++) {
    if (!the_mesh.getContactByIdx(i, c) || c.type != ADV_TYPE_CHAT || !c.isFav()) continue;
    if (!send) { n++; continue; }
    ChatId id = {CK_DIRECT, {0}};
    memcpy(id.key, c.id.pub_key, 6);
    n += app_send(id, t);
  }
  return n;
}

// ── MeshCore callbacks (from UITask) ─────────────────────────────────────────
void app_on_msg(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t, const char* text) {
  ChatId id = {(uint8_t)(from.type == ADV_TYPE_ROOM ? CK_ROOM : CK_DIRECT), {0}};
  memcpy(id.key, from.id.pub_key, 6);
  Msg m = {};
  m.ts = now();
  m.hops = pkt->isRouteFlood() ? pkt->getPathHashCount() : -1;
  m.snr4 = (int8_t)(pkt->getSNR() * 4);
  snprintf(m.from, sizeof m.from, "%s", from.name);
  snprintf(m.text, sizeof m.text, "%s", text);
  incoming(id, m, from.name);
  if (id.kind == CK_DIRECT && isSos(text)) push(EV_SOS, &id, "%s|%s", from.name, text + 4);
}

void app_on_channel_msg(mesh::Packet* pkt, ChannelDetails& ch, const char* text) {
  int idx = the_mesh.findChannelIdx(ch.channel);
  ChatId id = {channelKind(idx, ch.name), {0}};
  memcpy(id.key, ch.channel.secret, 6);
  Msg m = {};
  m.ts = now();
  m.hops = pkt->isRouteFlood() ? pkt->getPathHashCount() : -1;
  m.snr4 = (int8_t)(pkt->getSNR() * 4);
  const char* sep = strstr(text, ": ");  // channel text is "sender: message"
  if (sep && sep - text < (int)sizeof m.from) {
    memcpy(m.from, text, sep - text);
    m.from[sep - text] = 0;
    snprintf(m.text, sizeof m.text, "%s", sep + 2);
  } else {
    snprintf(m.text, sizeof m.text, "%s", text);
  }
  incoming(id, m, ch.name);
}

void app_on_queue(int n) { queueLen = n; }

void app_on_contact(ContactInfo& c, bool isNew, uint8_t pathLen, const uint8_t*) {
  Heard& h = heardSlot(c.id.pub_key);
  snprintf(h.name, sizeof h.name, "%s", c.name);
  h.type = c.type;
  h.hops = pathLen == OUT_PATH_UNKNOWN ? -1 : pathLen & 63;
  h.snr4 = (int8_t)(radio_driver.getLastSNR() * 4);
  h.at = millis();
  orderAt = 0;  // re-sort contacts
  if (isNew) push(EV_CONTACT, nullptr, "%s: %s", tr(S_NEW_CONTACT), c.name);
}

void app_on_control(const mesh::Packet* pkt) {
  if (pkt->payload_len < 14 || (pkt->payload[0] & 0xF0) != CTL_TYPE_NODE_DISCOVER_RESP) return;
  if (memcmp(&pkt->payload[2], &discTag, 4)) return;
  Heard& h = heardSlot(&pkt->payload[6]);
  ContactInfo* c = the_mesh.lookupContactByPubKey(&pkt->payload[6], 6);
  if (c) snprintf(h.name, sizeof h.name, "%s", c->name);
  h.type = pkt->payload[0] & 0x0F;
  h.hops = 0;
  h.snr4 = (int8_t)(pkt->getSNR() * 4);
  h.at = millis();
}

void app_on_ack(uint32_t ack) {
  for (auto& p : pend)
    if (p.used && p.ack == ack) resolvePending(p, true);
}

bool app_on_response(const ContactInfo& c, uint32_t tag, const uint8_t* d, uint8_t len) { return onResponse(c, tag, d, len); }

void app_on_trace(mesh::Packet* pkt, uint32_t tag, uint32_t, uint8_t flags, const uint8_t* snrs, const uint8_t* hashes, uint8_t len) {
  onTrace(pkt, tag, flags, snrs, hashes, len);
}

// ── lifecycle ────────────────────────────────────────────────────────────────
void app_begin() {
  Preferences p;
  if (p.begin("cardcore", true)) {
    // New fields go at the end of UiPrefs: an older, shorter blob loads and the rest keep their defaults.
    size_t n = p.getBytesLength("ui");
    if (n && n <= sizeof ui) p.getBytes("ui", &ui, n);
    p.end();
  }
  if (!ui.setup) {  // first boot / factory reset: Catalunya, 2-byte paths (MeshCore starts at 1), CET
    P()->tz_offset = 1;
    app_apply_preset(0);  // saves the prefs
  }
  mountSd();
  scanLogs();
  phoneTap.in = &interface_manager;
  the_mesh.startInterface(phoneTap);  // re-enables what's already on
  if (!ui.ble) interface_manager.disableBluetooth();
  for (auto& n : noiseHist) n = radio_driver.getNoiseFloor();
}

void app_loop() {
  uint32_t ms = millis();
  static uint32_t t10 = 0, t60 = 0, tBatt = 0, rxAt10 = 0;
  for (auto& p : pend) {  // DM retries
    if (!p.used || (int32_t)(ms - p.deadline) < 0) continue;
    if (p.phone) {  // the app's message: it may still be retrying, so no verdict
      p.used = false;
    } else if (p.attempt < 2) {
      p.attempt++;
      if (!sendDm(p)) resolvePending(p, false);
    } else {
      resolvePending(p, false);
    }
  }
  if (req.kind && (int32_t)(ms - req.deadline) >= 0) {  // remote request timed out
    replyLine("%s", tr(S_NO_RESPONSE));
    replyDone(false);
  }
  uint32_t rx = radio_driver.getPacketsRecv();
  if (rx != lastRx) {  // RX log
    lastRx = rx;
    memmove(rxlog + 1, rxlog, sizeof rxlog - sizeof *rxlog);
    rxlog[0] = {ms, (int16_t)radio_driver.getLastRSSI(), (int8_t)(radio_driver.getLastSNR() * 4)};
    if (nrxlog < 8) nrxlog++;
  }
  if (ms - t10 >= 10000) {
    t10 = ms;
    memmove(rxHist, rxHist + 1, HIST - 1);
    rxHist[HIST - 1] = std::min<uint32_t>(rx - rxAt10, 255);
    rxAt10 = rx;
    memmove(noiseHist, noiseHist + 1, HIST - 1);
    noiseHist[HIST - 1] = radio_driver.getNoiseFloor();
  }
  if (ms - t60 >= 60000 || !airMinN) {
    t60 = ms;
    airMin[airMinHead] = the_mesh.getTotalAirTime();
    airMinT[airMinHead] = ms;
    airMinHead = (airMinHead + 1) % 60;
    if (airMinN < 60) airMinN++;
  }
  if ((ms - tBatt >= 2000 || !tBatt) && ms > tx_led_until + 1500) {  // a transmit sags the battery
    tBatt = ms;
    static ChargeGuess power;
    battMv = board.getBattMilliVolts();
    battPct = M5.Power.getBatteryLevel();
    // The ADV's charger has no status line (M5Unified says "unknown"): a USB host, or the voltage.
    charging = power.add(battMv) | HWCDC::isPlugged() || M5.Power.isCharging() == m5::Power_Class::is_charging;
  }
}
