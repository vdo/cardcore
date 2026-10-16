// cardcore: standalone MeshCore client for M5Stack Cardputer ADV + Cap LoRa-1262
#include <M5Cardputer.h>
#include <utility/PI4IOE5V6408_Class.hpp>
#include <Mesh.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/ESP32Board.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <target.h>
#include <Preferences.h>
#include <TinyGPSPlus.h>
#include <RTClib.h>
#include <SD.h>
#include <ChaCha.h>
#include <codec2.h>
#include <bootloader_random.h>
#include <algorithm>
#include <map>
#include <vector>

// Radio pins and MeshCore EU defaults (869.618 MHz / 62.5 kHz / SF8) are in platformio.ini
#define P_SD_CS 12  // SD shares the LoRa SPI bus
#define GPS_RX 15
#define GPS_TX 13
#define GPS_BAUD 115200
#define VOLUME 200  // speaker 0-255

constexpr uint32_t SCREEN_MS = 30000, CHAN = 0xFFFFFF00;  // line/conversation ids: CHAN | slot = channel
constexpr int TRIES = 3, ROWS = 11, COLS = 38, MAXINPUT = 150;
// Voice: Codec2 1300 bps, 40 ms of 8 kHz audio -> 7 bytes. Sent as MeshCore requests of our own type,
// 21 frames (0.84 s) each: type(1) id(8) seq|last(1) total(1) nframes(1) frames. nframes is explicit because
// MeshCore pads request payloads to the AES block size.
constexpr uint8_t REQ_VOICE = 0xCC;
constexpr int SPF = 320, VBYTES = 7, VFRAMES = 21, VMAX_S = 20;
constexpr size_t VCHUNK = VFRAMES * VBYTES, VHDR = 12, VMAXCHUNKS = (VMAX_S * 25 + VFRAMES - 1) / VFRAMES;
static_assert(4 + VHDR + VCHUNK + CIPHER_MAC_SIZE + CIPHER_BLOCK_SIZE - 1 <= MAX_PACKET_PAYLOAD,
              "a voice chunk must fit one MeshCore request (see Mesh::createDatagram)");
static const uint8_t PUBLIC_PSK[16] = {139, 51, 135, 233, 197, 205, 234, 106, 201, 229, 237, 186, 161, 21, 205, 114};

SET_LOOP_TASK_STACK_SIZE(32 * 1024);  // codec2 needs far more than the default 8 KB

// peer 0 = system; state '<' in, '?' sending, '+' acked, '!' failed, 's' sent (channels: no acks)
struct Line {
  uint32_t peer, msgId;
  char state;
  String text;
};
struct Out {  // waits for its ack
  uint32_t peer, msgId, ts, ack, at;
  int tries;
  String text;
  std::vector<uint8_t> voice;  // empty = text
  uint64_t vid;
  uint32_t need;  // voice chunks not acked yet
  int next;       // next chunk to send this round; 32 = round sent, waiting
  bool probed;    // receiver answered once (so it speaks cardcore voice)
};
struct RxVoice {
  uint32_t peer, got;
  uint64_t vid;
  uint8_t total;
  size_t len;
  std::vector<uint8_t> data;
};

struct HwRng : mesh::RNG {
  void random(uint8_t *dest, size_t sz) override { esp_fill_random(dest, sz); }
};

ESP32Board board;
CustomSX1262 radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);
CustomSX1262Wrapper radio_driver(radio, board);
ESP32RTCClock rtc_clock;
SensorManager sensors;
HwRng rng;
SimpleMeshTables tables;
TinyGPSPlus gps;
Preferences prefs;
M5Canvas cv(&M5Cardputer.Display);
CODEC2 *c2;

uint32_t cur, lastDraw, lastActive, lastSave, lastAdvert, lastClock;
String myName, input, status;
std::vector<ContactInfo> pending;
std::vector<uint32_t> ignored;
std::vector<Line> lines;
std::vector<Out> outbox;
std::vector<RxVoice> rxv;
std::vector<uint8_t> toPlay;
std::map<uint32_t, uint32_t> heard;  // peer -> millis
bool dirty = true, asleep, sd, repeat, contactsDirty;
void draw();
String voiceLabel(size_t bytes, uint32_t num);
uint32_t saveVoice(uint32_t peer, char dir, const std::vector<uint8_t> &d);

uint32_t idOf(const ContactInfo &c) {
  uint32_t v;
  memcpy(&v, c.id.pub_key, 4);
  return v;
}
bool isChan(uint32_t id) { return (id & CHAN) == CHAN; }
String hops(uint8_t path_len) {
  int n = path_len & 63;
  return n == 0 ? String("direct") : String(n) + (n == 1 ? " hop" : " hops");
}
String hex(const uint8_t *b, int n) {
  String s;
  char h[3];
  for (int i = 0; i < n; i++) {
    snprintf(h, sizeof h, "%02x", b[i]);
    s += h;
  }
  return s;
}

void wake() {
  lastActive = millis();
  if (asleep) {
    M5Cardputer.Display.wakeup();
    asleep = false;
    dirty = true;
  }
}

void addLine(uint32_t peer, uint32_t msgId, char state, const String &text) {
  lines.push_back({peer, msgId, state, text});
  if (lines.size() > 200) lines.erase(lines.begin());
  wake();  // incoming msg / advert lights the screen
  dirty = true;
}
void sys(const String &s) { addLine(0, 0, '*', s); }

void setState(uint32_t peer, uint32_t msgId, char state) {
  for (auto &l : lines)
    if (l.peer == peer && l.msgId == msgId && l.state != '<') l.state = state;
  dirty = true;
}

struct Node : BaseChatMesh {
  Node() : BaseChatMesh(radio_driver, *new ArduinoMillis(), rng, rtc_clock, *new StaticPoolPacketManager(16), tables) {}

  void begin(const mesh::LocalIdentity &id) {
    BaseChatMesh::begin();
    self_id = id;
  }
  const mesh::LocalIdentity &me() const { return self_id; }
  int outbound() const { return _mgr->getOutboundTotal(); }
  void syncClockFromContacts() { bootstrapRTCfromContacts(); }

  ContactInfo *byId(uint32_t id) {
    uint8_t p[4];
    memcpy(p, &id, 4);
    return isChan(id) ? nullptr : lookupContactByPubKey(p, 4);
  }
  ContactInfo *at(int j) {  // j-th real contact (anon slots skipped)
    ContactInfo tmp;
    return getContactByIdx(MAX_ANON_CONTACTS + j, tmp) ? lookupContactByPubKey(tmp.id.pub_key, PUB_KEY_SIZE) : nullptr;
  }

  void advert() {
    auto pkt = createSelfAdvert(myName.c_str());
    if (pkt) sendFlood(pkt, 1500);
    lastAdvert = millis();
  }

  // Encrypts or decrypts voice frames in place. MeshCore's own layer is AES-ECB, which would show
  // repeated Codec2 frames (silence); a ChaCha stream under it hides them at zero byte cost.
  void voiceCrypt(const ContactInfo &c, uint64_t vid, uint8_t seq, uint8_t *b, size_t n) {
    uint8_t key[32], ctr[8] = {(uint8_t)(seq * 4)};  // 4 blocks = 256 B >= one chunk
    mesh::Utils::sha256(key, 32, c.getSharedSecret(self_id), 32, (const uint8_t *)"cardcore-voice", 14);
    ChaCha cha;
    cha.setKey(key, 32);
    cha.setIV(reinterpret_cast<const uint8_t *>(&vid), 8);
    cha.setCounter(ctr, 8);
    cha.encrypt(b, b, n);
    cha.clear();
  }

 protected:
  bool allowPacketForward(const mesh::Packet *) override { return repeat; }
  bool shouldAutoAddContactType(uint8_t) const override { return false; }  // adverts become /ok requests

  void onDiscoveredContact(ContactInfo &c, bool is_new, uint8_t path_len, const uint8_t *) override {
    heard[idOf(c)] = millis();
    if (!is_new) {
      contactsDirty = true;
      return;
    }
    if (c.type != ADV_TYPE_CHAT || std::count(ignored.begin(), ignored.end(), idOf(c))) return;
    for (auto &p : pending)
      if (p.id.matches(c.id)) return;
    if (pending.size() >= 16) pending.erase(pending.begin());
    pending.push_back(c);
    sys("ADVERT " + String(c.name) + " " + hex(c.id.pub_key, 4) + " " + hops(path_len) + "  /ok /no");
  }

  void onContactPathUpdated(const ContactInfo &) override { contactsDirty = true; }

  ContactInfo *processAck(const uint8_t *data) override {
    for (size_t i = 0; i < outbox.size(); i++) {
      Out &o = outbox[i];
      if (o.voice.empty() && memcmp(data, &o.ack, 4) == 0) {
        uint32_t peer = o.peer;
        setState(peer, o.msgId, '+');
        outbox.erase(outbox.begin() + i);
        heard[peer] = millis();
        return byId(peer);
      }
    }
    return nullptr;
  }

  void onMessageRecv(const ContactInfo &c, mesh::Packet *, uint32_t ts, const char *text) override {
    uint32_t peer = idOf(c);
    heard[peer] = millis();
    for (auto &l : lines)
      if (l.peer == peer && l.msgId == ts && l.state == '<') return;  // a retry we already have
    addLine(peer, ts, '<', text);
    if (cur != peer) sys(String("msg from ") + c.name + " (Tab)");
    M5Cardputer.Speaker.tone(3000, 60);
  }
  void onSignedMessageRecv(const ContactInfo &c, mesh::Packet *p, uint32_t ts, const uint8_t *,
                           const char *text) override {
    onMessageRecv(c, p, ts, text);  // room server posts
  }
  void onCommandDataRecv(const ContactInfo &, mesh::Packet *, uint32_t, const char *) override {}
  void onCLICommandRecv(const ContactInfo &, mesh::Packet *, uint32_t, const char *, char *) override {}

  void onChannelMessageRecv(const mesh::GroupChannel &ch, mesh::Packet *, uint32_t ts, const char *text) override {
    int slot = findChannelIdx(ch);
    if (slot >= 0) addLine(CHAN | slot, ts, '<', text);  // text is "name: msg"
  }

  // Voice chunk in, chunk bitmap out (only at the end of a round, to save airtime)
  uint8_t onContactRequest(const ContactInfo &c, uint32_t ts, const uint8_t *d, uint8_t len, uint8_t *reply) override {
    if (len < VHDR + VBYTES || d[0] != REQ_VOICE) return 0;
    uint64_t vid;
    memcpy(&vid, d + 1, 8);
    uint8_t seq = d[9] & 0x7f, total = d[10];
    bool last = d[9] & 0x80;
    size_t n = d[11] * VBYTES;
    if (!total || total > VMAXCHUNKS || seq >= total || !n || n > VCHUNK || VHDR + n > len) return 0;
    uint32_t peer = idOf(c), full = (1u << total) - 1, got = full;
    heard[peer] = millis();
    bool have = false;
    for (auto &l : lines) have |= l.peer == peer && l.msgId == (uint32_t)vid && l.state == '<';
    if (!have) {
      RxVoice *v = nullptr;
      for (auto &r : rxv)
        if (r.peer == peer && r.vid == vid) v = &r;
      if (!v) {
        if (rxv.size() >= 4) rxv.erase(rxv.begin());
        rxv.push_back({peer, 0, vid, total, 0, std::vector<uint8_t>(total * VCHUNK)});
        v = &rxv.back();
      }
      if (total != v->total) return 0;
      uint8_t *dst = v->data.data() + seq * VCHUNK;
      memcpy(dst, d + VHDR, n);
      voiceCrypt(c, vid, seq, dst, n);
      if (seq == total - 1) v->len = seq * VCHUNK + n;
      got = v->got |= 1u << seq;
      if (got == full) {
        std::vector<uint8_t> voice(v->data.begin(), v->data.begin() + v->len);
        rxv.erase(rxv.begin() + (v - rxv.data()));
        addLine(peer, (uint32_t)vid, '<', voiceLabel(voice.size(), saveVoice(peer, 'i', voice)));
        if (cur != peer) sys(String("voice from ") + c.name + " (Tab)");
        toPlay = voice;  // played from loop() once our reply is out
      }
    }
    if (!last && got != full) return 0;
    memcpy(reply, &ts, 4);
    reply[4] = REQ_VOICE;
    memcpy(reply + 5, &vid, 8);
    memcpy(reply + 13, &got, 4);
    return 17;
  }

  void onContactResponse(const ContactInfo &c, const uint8_t *d, uint8_t len) override {
    if (len < 17 || d[4] != REQ_VOICE) return;
    uint64_t vid;
    uint32_t got, peer = idOf(c);
    memcpy(&vid, d + 5, 8);
    memcpy(&got, d + 13, 4);
    heard[peer] = millis();
    for (size_t i = 0; i < outbox.size(); i++) {
      Out &o = outbox[i];
      if (o.voice.empty() || o.vid != vid) continue;
      o.probed = true;
      o.need &= ~got;
      o.next = 0;  // send what's still missing right away
      if (!o.need) {
        setState(peer, o.msgId, '+');
        outbox.erase(outbox.begin() + i);
      }
      return;
    }
  }

  uint32_t calcFloodTimeoutMillisFor(uint32_t airtime) const override { return 500 + 16 * airtime; }
  uint32_t calcDirectTimeoutMillisFor(uint32_t airtime, uint8_t path_len) const override {
    return 500 + (airtime * 6 + 250) * ((path_len & 63) + 1);
  }
  void onSendTimeout() override {}  // our outbox keeps its own timers
};
Node node;

// ---- persistence (NVS) ----
constexpr size_t CREC = 32 + 32 + 3 + MAX_PATH_SIZE + 8;  // pub name type/flags/pathlen path advert_ts lastmod

void saveContacts() {
  std::vector<uint8_t> blob;
  for (int j = 0; ContactInfo *c = node.at(j); j++) {
    blob.resize(blob.size() + CREC);
    uint8_t *p = blob.data() + blob.size() - CREC;
    memcpy(p, c->id.pub_key, 32), p += 32;
    memcpy(p, c->name, 32), p += 32;
    *p++ = c->type, *p++ = c->flags, *p++ = c->out_path_len;
    memcpy(p, c->out_path, MAX_PATH_SIZE), p += MAX_PATH_SIZE;
    memcpy(p, &c->last_advert_timestamp, 4), p += 4;
    memcpy(p, &c->lastmod, 4);
  }
  prefs.putBytes("contacts", blob.data(), blob.size());
  contactsDirty = false;
  lastSave = millis();
}

void loadContacts() {
  std::vector<uint8_t> blob(prefs.getBytesLength("contacts"));
  prefs.getBytes("contacts", blob.data(), blob.size());
  for (size_t off = 0; off + CREC <= blob.size(); off += CREC) {
    const uint8_t *p = blob.data() + off;
    ContactInfo c;
    memset(&c, 0, sizeof c);
    c.id = mesh::Identity(p), p += 32;
    memcpy(c.name, p, 32), p += 32;
    c.type = *p++, c.flags = *p++, c.out_path_len = *p++;
    memcpy(c.out_path, p, MAX_PATH_SIZE), p += MAX_PATH_SIZE;
    memcpy(&c.last_advert_timestamp, p, 4), p += 4;
    memcpy(&c.lastmod, p, 4);
    node.addContact(c);
  }
}

bool setChannel(int slot, const String &name, const uint8_t *secret, int len) {
  ChannelDetails cd;
  memset(&cd, 0, sizeof cd);
  memcpy(cd.channel.secret, secret, len);
  strlcpy(cd.name, name.c_str(), sizeof cd.name);
  return node.setChannel(slot, cd);
}

void saveChannels() {  // "name hex\n" per joined channel, slot 0 = Public is built in
  String s;
  ChannelDetails cd;
  for (int i = 1; i < MAX_GROUP_CHANNELS && node.getChannel(i, cd); i++)
    if (cd.name[0]) s += String(cd.name) + " " + hex(cd.channel.secret, 32) + "\n";
  prefs.putString("chans", s);
}

void loadChannels() {
  setChannel(0, "Public", PUBLIC_PSK, 16);
  String s = prefs.getString("chans", "");
  int slot = 1;
  for (int a = 0, nl; (nl = s.indexOf('\n', a)) > 0 && slot < MAX_GROUP_CHANNELS; a = nl + 1) {
    String row = s.substring(a, nl);
    int sp = row.indexOf(' ');
    uint8_t secret[32];
    if (sp < 0 || !mesh::Utils::fromHex(secret, 32, row.substring(sp + 1).c_str())) continue;
    setChannel(slot++, row.substring(0, sp), secret, 32);
  }
}

// ---- conversations: joined channels, then contacts ----
std::vector<uint32_t> convs() {
  std::vector<uint32_t> v;
  ChannelDetails cd;
  for (int i = 0; i < MAX_GROUP_CHANNELS && node.getChannel(i, cd); i++)
    if (cd.name[0]) v.push_back(CHAN | i);
  for (int j = 0; ContactInfo *c = node.at(j); j++) v.push_back(idOf(*c));
  return v;
}
String convName(uint32_t id) {
  if (isChan(id)) {
    ChannelDetails cd;
    node.getChannel(id & 0xff, cd);
    return String(cd.name[0] == '#' ? "" : "#") + cd.name;
  }
  ContactInfo *c = node.byId(id);
  return c ? c->name : "?";
}

void sendText(const String &s) {
  String t = s.substring(0, MAXINPUT);
  if (isChan(cur)) {
    ChannelDetails cd;
    node.getChannel(cur & 0xff, cd);
    uint32_t ts = rtc_clock.getCurrentTimeUnique();
    if (!node.sendGroupMessage(ts, cd.channel, myName.c_str(), t.c_str(), t.length())) return sys("send failed");
    return addLine(cur, ts, 's', t);
  }
  if (!node.byId(cur)) return sys("no contact selected (Tab)");
  Out o{cur, esp_random(), rtc_clock.getCurrentTimeUnique(), 0, millis(), 0, t};
  addLine(o.peer, o.msgId, '?', t);
  outbox.push_back(o);
}

void pumpVoice(Out &o, ContactInfo &c) {
  if (c.out_path_len == OUT_PATH_UNKNOWN) {  // route lost: fail rather than flood the mesh with voice
    o.next = 32;
    return;
  }
  if (o.next >= 32 || node.outbound()) return;  // round sent, or pace: one request queued at a time
  uint32_t rest = o.need >> o.next << o.next;
  if (!rest) {
    o.next = 32;
    return;
  }
  int i = __builtin_ctz(rest);
  bool last = !o.probed || !(rest >> i >> 1);  // first round is a single probe chunk
  size_t total = (o.voice.size() + VCHUNK - 1) / VCHUNK, n = std::min(VCHUNK, o.voice.size() - i * VCHUNK);
  uint8_t b[VHDR + VCHUNK];
  b[0] = REQ_VOICE;
  memcpy(b + 1, &o.vid, 8);
  b[9] = i | (last ? 0x80 : 0);
  b[10] = total;
  b[11] = n / VBYTES;
  memcpy(b + VHDR, o.voice.data() + i * VCHUNK, n);
  node.voiceCrypt(c, o.vid, i, b + VHDR, n);
  uint32_t tag, est;
  if (node.sendRequest(c, b, VHDR + n, tag, est) == MSG_SEND_FAILED) return;  // pool full, next loop
  o.next = last ? 32 : i + 1;
  if (last) o.at = millis() + est + 3000;
}

void pumpOutbox() {
  for (size_t i = 0; i < outbox.size();) {
    Out &o = outbox[i];
    ContactInfo *c = node.byId(o.peer);
    bool due = (int32_t)(millis() - o.at) >= 0, waiting = o.voice.empty() || o.next >= 32;
    if (!c || (due && waiting && o.tries >= TRIES)) {
      setState(o.peer, o.msgId, '!');
      outbox.erase(outbox.begin() + i);
      continue;
    }
    if (o.voice.size()) {
      if (due && waiting) o.tries++, o.next = 0;  // no ack: resend the round
      pumpVoice(o, *c);
    } else if (due) {
      if (o.tries == TRIES - 1) node.resetPathTo(*c);  // last try floods: the path may be stale
      uint32_t est = 0;
      if (node.sendMessage(*c, o.ts, o.tries, o.text.c_str(), o.ack, est) != MSG_SEND_FAILED) o.tries++;
      o.at = millis() + std::max<uint32_t>(est, 3000);
    }
    i++;
  }
}

// ---- voice ----
String voiceLabel(size_t bytes, uint32_t num) {
  char s[32];
  double secs = bytes / VBYTES * 0.04;
  if (num) snprintf(s, sizeof s, "voice %.1fs #%lu", secs, (unsigned long)num);
  else snprintf(s, sizeof s, "voice %.1fs (no SD)", secs);
  return s;
}

// Standard Codec2 .c2 file: plays on a PC with `c2dec 1300 x.c2 x.raw`
uint32_t saveVoice(uint32_t peer, char dir, const std::vector<uint8_t> &d) {
  if (!sd) return 0;
  uint32_t num = prefs.getUInt("vnum", 0) + 1;
  prefs.putUInt("vnum", num);
  char path[40];
  snprintf(path, sizeof path, "/cardcore/%04lu_%08lx_%c.c2", (unsigned long)num, (unsigned long)peer, dir);
  File f = SD.open(path, FILE_WRITE);
  if (!f) return 0;
  const uint8_t hdr[7] = {0xc0, 0xde, 0xc2, 0, 9, CODEC2_MODE_1300, 0};
  f.write(hdr, sizeof hdr);
  f.write(d.data(), d.size());
  f.close();
  return num;
}

// num 0 = newest one with this peer
String findVoice(uint32_t peer, uint32_t num, bool list = false) {
  String best;
  uint32_t bestNum = 0;
  std::vector<String> all;
  File d = SD.open("/cardcore");
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    unsigned long n, p;
    if (sscanf(f.name(), "%lu_%lx", &n, &p) != 2) continue;
    if (list && p == peer) all.push_back(f.name());
    if (num ? n == num : p == peer && n > bestNum) {
      bestNum = n;
      best = String("/cardcore/") + f.name();
    }
  }
  std::sort(all.begin(), all.end());  // zero-padded, so text order = time order
  for (size_t i = all.size() > 8 ? all.size() - 8 : 0; i < all.size(); i++) sys(all[i]);
  return best;
}

// Blocking, like a walkie: the radio misses packets meanwhile
void playVoice(const uint8_t *d, size_t n) {
  static int16_t pcm[3][SPF];  // speaker holds 2 queued, we fill the 3rd
  if (!c2) return;
  wake();
  status = "PLAY  tap G0 to stop";
  draw();
  setCpuFrequencyMhz(240);
  for (size_t i = 0, k = 0; i + VBYTES <= n; i += VBYTES, k++) {
    codec2_decode(c2, pcm[k % 3], d + i);
    while (M5Cardputer.Speaker.isPlaying(0) >= 2) delay(1);
    M5Cardputer.Speaker.playRaw(pcm[k % 3], SPF, 8000, false, 1, 0);
    M5.update();
    if (M5Cardputer.BtnA.wasPressed()) {
      M5Cardputer.Speaker.stop(0);
      break;
    }
  }
  while (M5Cardputer.Speaker.isPlaying()) delay(1);
  setCpuFrequencyMhz(80);
  status = "";
  dirty = true;
}

void playFile(const String &path) {
  File f = SD.open(path.c_str());
  if (!f || f.size() < 7 || f.size() > 8192) return sys("no such voice message");
  std::vector<uint8_t> d(f.size());
  f.read(d.data(), d.size());
  f.close();
  playVoice(d.data() + 7, d.size() - 7);  // skip .c2 header
}

void playLatest() {
  String path = sd && !isChan(cur) ? findVoice(cur, 0) : String();
  if (path.isEmpty()) return sys("no saved voice messages here");
  playFile(path);
}

// Hold G0 to talk: encodes while recording, frame by frame
void talk() {
  static int16_t pcm[3][SPF];  // mic holds 2 queued, we encode the 3rd
  if (!c2) return sys("codec2 init failed");
  std::vector<uint8_t> enc;
  auto encode = [&](int16_t *p) {
    uint8_t b[VBYTES];
    codec2_encode(c2, b, p);
    enc.insert(enc.end(), b, b + VBYTES);
  };
  wake();
  setCpuFrequencyMhz(240);
  M5Cardputer.Speaker.end();  // mic and speaker share the I2S pins
  M5Cardputer.Mic.begin();
  int k = 0;
  for (; M5Cardputer.BtnA.isPressed() && k < VMAX_S * 25; k++) {
    M5Cardputer.Mic.record(pcm[k % 3], SPF, 8000);  // waits for a free slot, so k-2 is done
    if (k >= 2) encode(pcm[(k - 2) % 3]);
    if (k % 25 == 0) {
      status = "REC " + String(k / 25) + "/" + String(VMAX_S) + "s";
      draw();
    }
    M5.update();
  }
  while (M5Cardputer.Mic.isRecording()) delay(1);
  for (int j = std::max(0, k - 2); j < k; j++) encode(pcm[j % 3]);
  M5Cardputer.Mic.end();
  M5Cardputer.Speaker.begin();
  setCpuFrequencyMhz(80);
  status = "";
  dirty = true;

  if (k < 12) return playLatest();  // under 0.5 s = a tap: replay instead
  ContactInfo *c = node.byId(cur);
  if (!c) return sys("voice goes to a contact, not a channel");
  if (c->out_path_len == OUT_PATH_UNKNOWN) return sys(String("no route to ") + c->name + " yet: send a text first");
  uint32_t chunks = (enc.size() + VCHUNK - 1) / VCHUNK;
  Out o{cur, 0, 0, 0, 0, 0, "", enc, 0, (1u << chunks) - 1, 0, false};
  esp_fill_random(&o.vid, sizeof o.vid);
  o.msgId = (uint32_t)o.vid;
  addLine(o.peer, o.msgId, '?', voiceLabel(enc.size(), saveVoice(o.peer, 'o', enc)));
  outbox.push_back(o);
}

// ---- commands ----
void command(String s) {
  int sp = s.indexOf(' ');
  String cmd = sp < 0 ? s : s.substring(0, sp), arg = sp < 0 ? "" : s.substring(sp + 1);
  arg.trim();
  if (cmd == "/name" && arg.length()) {
    myName = arg.substring(0, 31);
    prefs.putString("name", myName);
    sys("name: " + myName + ". People show up as ADVERT lines, accept with /ok");
    node.advert();
  } else if (cmd == "/ok" || cmd == "/add") {
    auto it = pending.end();
    for (auto p = pending.begin(); p != pending.end(); ++p)
      if (cmd == "/ok" || String(p->name).startsWith(arg)) it = p;  // /ok = newest
    if (it == pending.end()) return sys("no matching advert");
    if (!node.addContact(*it)) return sys("contact list full");
    cur = idOf(*it);
    sys(String("added ") + it->name);
    pending.erase(it);
    saveContacts();
    if (millis() - lastAdvert > 300000) node.advert();  // so they can add us back
  } else if (cmd == "/no") {
    if (pending.empty()) return sys("no adverts");
    ignored.push_back(idOf(pending.back()));
    pending.pop_back();
    sys("ignored until reboot");
  } else if (cmd == "/del") {
    ContactInfo *c = node.byId(cur);
    if (!c) return;
    sys(String("deleted ") + c->name);
    node.removeContact(*c);
    cur = CHAN;
    saveContacts();
  } else if (cmd == "/join" && arg.length()) {
    String name = arg, psk;
    int sp2 = arg.indexOf(' ');
    if (sp2 > 0) name = arg.substring(0, sp2), psk = arg.substring(sp2 + 1);
    uint8_t secret[32] = {};
    if (name[0] == '#') mesh::Utils::sha256(secret, 16, (const uint8_t *)name.c_str(), name.length());
    else if (!mesh::Utils::fromHex(secret, 16, psk.c_str())) return sys("/join #name, or /join name <32 hex key>");
    ChannelDetails cd;
    int slot = 1;
    while (slot < MAX_GROUP_CHANNELS && node.getChannel(slot, cd) && cd.name[0]) slot++;
    if (slot >= MAX_GROUP_CHANNELS) return sys("no free channel slot");
    setChannel(slot, name, secret, 16);
    saveChannels();
    cur = CHAN | slot;
    sys("joined " + name);
  } else if (cmd == "/leave") {
    if (!isChan(cur) || cur == CHAN) return sys("switch to a joined channel first (Public stays)");
    ChannelDetails empty;
    memset(&empty, 0, sizeof empty);
    node.setChannel(cur & 0xff, empty);
    saveChannels();
    cur = CHAN;
  } else if (cmd == "/loc") {
    if (!gps.location.isValid() || gps.location.age() > 10000)
      return sys("no GPS fix, sats " + String(gps.satellites.value()));
    sendText("loc " + String(gps.location.lat(), 6) + "," + String(gps.location.lng(), 6));
  } else if (cmd == "/advert") {
    node.advert();
    sys("advert sent");
  } else if (cmd == "/fp") {
    sys("me " + hex(node.me().pub_key, 8));
    if (ContactInfo *c = node.byId(cur)) sys(String(c->name) + " " + hex(c->id.pub_key, 8));
  } else if (cmd == "/repeat") {
    repeat = arg == "on";
    prefs.putBool("repeat", repeat);
    sys(repeat ? "relaying others' packets (disaster mode)" : "not relaying (leave it to repeaters)");
  } else if (cmd == "/radio") {
    float f, bw;
    int sf, cr;
    if (sscanf(arg.c_str(), "%f %f %d %d", &f, &bw, &sf, &cr) == 4) {
      prefs.putString("radio", arg);
      radio_driver.setParams(f, bw, sf, cr);
    }
    sys("radio " + prefs.getString("radio", "869.618 62.5 8 5") + "  (/radio freq bw sf cr)");
  } else if (cmd == "/play") {
    if (arg.isEmpty()) playLatest();
    else playFile(sd ? findVoice(0, arg.toInt()) : String());
  } else if (cmd == "/voices") {
    if (sd && !isChan(cur)) findVoice(cur, 0, true);
  } else {
    sys("/name /ok /add /no /del /join #x /leave /loc /advert /fp /repeat on|off /radio /play [n] /voices");
    sys("Tab=next chat  hold G0=talk, tap=replay");
  }
}

void enter(String s) {
  s.trim();
  if (s.isEmpty()) return;
  if (myName.isEmpty()) s = "/name " + s;  // first-boot setup
  if (s[0] == '/') command(s);
  else sendText(s);
}

// ---- screen ----
void draw() {
  cv.fillSprite(BLACK);
  cv.fillRect(0, 0, 240, 10, status.length() ? RED : NAVY);
  cv.setTextColor(WHITE);
  cv.setCursor(2, 1);
  if (status.length()) cv.print(status);
  else {
    auto all = convs();
    int idx = std::find(all.begin(), all.end(), cur) - all.begin();
    cv.printf("%.10s %d/%d", convName(cur).c_str(), idx + 1, (int)all.size());
    if (ContactInfo *c = node.byId(cur)) {
      cv.print(c->out_path_len == OUT_PATH_UNKNOWN ? " flood" : (" " + hops(c->out_path_len)).c_str());
      if (heard.count(cur)) {
        uint32_t s = (millis() - heard[cur]) / 1000;
        if (s < 120) cv.printf(" %lus", (unsigned long)s);
        else cv.printf(" %lum", (unsigned long)s / 60);
      }
    }
    cv.setCursor(150, 1);
    cv.printf("%s%4d s%lu", pending.empty() ? "   " : "ADV", (int)radio_driver.getLastRSSI(),
              (unsigned long)gps.satellites.value());
  }

  // newest rows first, then drawn bottom-up
  struct Row {
    uint16_t col;
    String s;
  };
  std::vector<Row> rows;
  for (int i = (int)lines.size() - 1; i >= 0 && (int)rows.size() < ROWS; i--) {
    auto &l = lines[i];
    if (l.peer && l.peer != cur) continue;
    uint16_t col = !l.peer          ? DARKGREY
                   : l.state == '<' ? CYAN
                   : l.state == '+' ? GREEN
                   : l.state == '!' ? RED
                   : l.state == 's' ? LIGHTGREY
                                    : WHITE;
    String pre = !l.peer ? "* " : l.state == '<' ? "< " : "> ";
    std::vector<String> chunks;
    size_t p = 0, len = l.text.length();
    do {  // wrap at a space when there is one
      size_t e = std::min(len, p + COLS);
      int sp = e < len ? l.text.lastIndexOf(' ', e) : -1;
      if (sp > (int)p) e = sp + 1;
      chunks.push_back((p ? "  " : pre) + l.text.substring(p, e));
      p = e;
    } while (p < len);
    if (!rows.empty() && rows.size() + chunks.size() > ROWS) break;  // only whole messages at the top
    for (int j = (int)chunks.size() - 1; j >= 0 && (int)rows.size() < ROWS; j--) rows.push_back({col, chunks[j]});
  }
  for (int i = 0; i < (int)rows.size(); i++) {
    cv.setTextColor(rows[i].col);
    cv.setCursor(0, 12 + (ROWS - 1 - i) * 10);
    cv.print(rows[i].s);
  }

  cv.drawFastHLine(0, 123, 240, DARKGREY);
  cv.setTextColor(YELLOW);
  cv.setCursor(0, 126);
  String shown = input.length() > COLS ? input.substring(input.length() - COLS) : input;
  cv.print("> " + shown + "_");
  cv.pushSprite(0, 0);
  dirty = false;
  lastDraw = millis();
}

void syncClockFromGps() {  // MeshCore timestamps need real time; there's no RTC battery
  if (!gps.date.isValid() || !gps.time.isValid() || gps.date.year() < 2025) return;
  uint32_t t =
      DateTime(gps.date.year(), gps.date.month(), gps.date.day(), gps.time.hour(), gps.time.minute(), gps.time.second())
          .unixtime();
  int32_t diff = t - rtc_clock.getCurrentTime();
  if (abs(diff) > 5) {
    rtc_clock.setCurrentTime(t);
    sys("clock set from GPS");
  }
}

void setup() {
  setCpuFrequencyMhz(80);  // plenty for chat, ~half the CPU current of 240
  prefs.begin("cardcore");
  // Hardware TRNG needs an entropy source; the ADC one must be off again before M5 uses the ADC
  bootloader_random_enable();
  mesh::LocalIdentity id;
  uint8_t idbuf[128];
  size_t idlen = prefs.getBytes("id", idbuf, sizeof idbuf);
  if (idlen) id.readFrom(idbuf, idlen);
  else {
    do id = mesh::LocalIdentity(&rng);
    while (id.pub_key[0] == 0x00 || id.pub_key[0] == 0xff);  // reserved hashes
    prefs.putBytes("id", idbuf, id.writeTo(idbuf, sizeof idbuf));
  }
  bootloader_random_disable();
  // ponytail: keys sit in plain NVS, physical access = key theft. Enable flash encryption if that matters.
  myName = prefs.getString("name", "");
  repeat = prefs.getBool("repeat", false);

  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  cv.createSprite(240, 135);

  // Cap LoRa-1262: P0 of its IO expander enables the antenna switch
  m5::PI4IOE5V6408_Class ioe(0x43, 400000, &m5::In_I2C);
  if (ioe.begin()) {
    ioe.setDirection(0, true);
    ioe.setHighImpedance(0, false);
    ioe.digitalWrite(0, true);
  } else sys("no Cap IO expander (0x43)");

  pinMode(P_SD_CS, OUTPUT);  // keep the SD card off the bus while the radio starts
  digitalWrite(P_SD_CS, HIGH);
  if (!radio.std_init(&SPI)) sys("radio init failed");
  rtc_clock.begin();
  node.begin(id);
  float f, bw;
  int sf, cr;
  if (sscanf(prefs.getString("radio", "").c_str(), "%f %f %d %d", &f, &bw, &sf, &cr) == 4)
    radio_driver.setParams(f, bw, sf, cr);
  loadChannels();
  loadContacts();
  node.syncClockFromContacts();
  cur = CHAN;

  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);
  sd = SD.begin(P_SD_CS, SPI, 20000000);
  if (sd) SD.mkdir("/cardcore");
  else sys("no SD card: voice messages won't be saved");
  c2 = codec2_create(CODEC2_MODE_1300);
  M5Cardputer.Speaker.setVolume(VOLUME);

  sys("me " + hex(node.me().pub_key, 8));
  if (myName.isEmpty()) sys("type your name + Enter");
  else {
    sys("/help for commands");
    node.advert();
  }
}

void loop() {
  M5Cardputer.update();
  while (Serial1.available()) gps.encode(Serial1.read());
  if (millis() - lastClock > 10000) {
    lastClock = millis();
    syncClockFromGps();
  }

  node.loop();
  pumpOutbox();
  if (toPlay.size() && !node.outbound()) {  // our reply has left
    std::vector<uint8_t> v;
    v.swap(toPlay);
    playVoice(v.data(), v.size());
  }
  if (contactsDirty && millis() - lastSave > 30000) saveContacts();

  if (M5Cardputer.BtnA.wasPressed()) talk();

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    bool wasAsleep = asleep;
    wake();
    if (wasAsleep) return;  // first key only wakes the screen
    auto &k = M5Cardputer.Keyboard.keysState();
    if (k.tab) {
      auto all = convs();
      auto it = std::find(all.begin(), all.end(), cur);
      cur = it == all.end() || ++it == all.end() ? all[0] : *it;
    }
    for (char ch : k.word)
      if (input.length() < MAXINPUT) input += ch;
    if (k.del && input.length()) input.remove(input.length() - 1);
    if (k.enter) {
      enter(input);
      input = "";
    }
    dirty = true;
  }

  if (!asleep && millis() - lastActive > SCREEN_MS) {
    M5Cardputer.Display.sleep();
    asleep = true;
  }
  if (!asleep && (dirty || millis() - lastDraw > 1000)) draw();
  delay(5);  // let the idle task run instead of spinning the CPU
}
