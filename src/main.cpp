// cardxat: E2E-encrypted LoRa chat for Cardputer ADV + Cap LoRa-1262
#include <M5Cardputer.h>
#include <utility/PI4IOE5V6408_Class.hpp>
#include <RadioLib.h>
#include <Preferences.h>
#include <TinyGPSPlus.h>
#include <bootloader_random.h>
#include <vector>
#include "proto.h"

// EU868 sub-band h1.7 (869.4-869.65 MHz): the only one with 500 mW ERP / 10% duty.
// Sits in its quiet lower edge: below LoRaWAN RX2 (869.525/125k, 27 dBm gateways) and Meshtastic's
// centre, far from MeshCore (869.618/62.5k). SF9/62.5k = same link budget as SF10/125k, ~1.1 s per 100 B.
#define FREQ 869.435
#define BW 62.5
#define SF 9  // SF10/SF11: +2.5/+5 dB range but 2.2x/4x airtime -> raise ACK_MS to ~30000/~60000
#define CR 5
#define SYNC 0x12
#define TXPWR 22
#define TCXO_V 3.0

// Cap LoRa-1262 on Cardputer ADV
#define P_NSS 5
#define P_IRQ 4
#define P_RST 3
#define P_BUSY 6
#define P_SCK 40
#define P_MISO 39
#define P_MOSI 14
#define GPS_RX 15
#define GPS_TX 13
#define GPS_BAUD 115200

constexpr uint8_t TTL = 3;
constexpr uint32_t BEACON_MS = 5 * 60 * 1000, ACK_MS = 15000, SCREEN_MS = 30000;
constexpr int TRIES = 3, ROWS = 11, COLS = 38, MAXCONTACTS = 32;

struct Contact {
  char name[17];
  uint8_t pub[32], key[32];
  uint32_t id, seen;
};
// peer 0 = system; state '<' in, '?' sending, '+' acked, '!' failed
struct Line {
  uint32_t peer, msgId;
  char state;
  String text;
};
struct Out {
  uint32_t msgId, peer;
  String text;
  int tries;
  uint32_t at;
};
struct Tx {
  uint8_t b[MAXPKT];
  size_t n;
  uint32_t at;
};

SX1262 radio = new Module(P_NSS, P_IRQ, P_RST, P_BUSY);
TinyGPSPlus gps;
Preferences prefs;
M5Canvas cv(&M5Cardputer.Display);

uint8_t sec[32], pub[32], netKey[32], bootSeed[16];
uint64_t ctr;
uint32_t me, nextBeacon, lastDraw, lastActive;
String myName, netPass, input;
std::vector<Contact> contacts, pending;
std::vector<uint32_t> ignored, seen;
std::vector<Line> lines;
std::vector<Out> outbox;
std::vector<Tx> txq;
size_t cur;
int lastRssi;
volatile bool rxFlag;
bool dirty = true, asleep;

void IRAM_ATTR onRx() { rxFlag = true; }

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
  if (lines.size() > 150) lines.erase(lines.begin());
  wake();  // incoming msg / request lights the screen
  dirty = true;
}
void sys(const String &s) { addLine(0, 0, '*', s); }

String fpStr(const uint8_t p[32]) {
  uint8_t fp[8];
  fingerprint(fp, p);
  char s[20];
  snprintf(s, sizeof s, "%02x%02x %02x%02x %02x%02x %02x%02x", fp[0], fp[1], fp[2], fp[3], fp[4], fp[5], fp[6], fp[7]);
  return s;
}

String clean(const uint8_t *b, size_t n) {  // network data -> printable ASCII
  String s;
  for (size_t i = 0; i < n; i++) s += (b[i] >= 32 && b[i] < 127) ? (char)b[i] : '?';
  return s;
}

Contact *find(std::vector<Contact> &v, uint32_t id) {
  for (auto &c : v)
    if (c.id == id) return &c;
  return nullptr;
}

void save() { prefs.putBytes("contacts", contacts.data(), contacts.size() * sizeof(Contact)); }

bool seenBefore(uint32_t h) {
  for (auto s : seen)
    if (s == h) return true;
  seen.push_back(h);
  if (seen.size() > 64) seen.erase(seen.begin());
  return false;
}

// Unique per key: boot-random prefix + counter, so a weak/repeating RNG can't reuse a nonce
void makeNonce(uint8_t n[24]) {
  memcpy(n, bootSeed, 16);
  ctr++;
  memcpy(n + 16, &ctr, 8);
}

void queue(const uint8_t *b, size_t n, uint32_t delay) {
  if (txq.size() >= 8) return;  // ponytail: drop when flooded; no duty-cycle accounting, fine for chat volumes
  Tx t;
  memcpy(t.b, b, n);
  t.n = n;
  t.at = millis() + delay;
  txq.push_back(t);
}

void send(uint8_t type, uint32_t dst, const uint8_t *body, size_t len, const uint8_t *key, uint32_t delay = 0) {
  if (netPass.isEmpty()) return;
  uint8_t nonce[24], b[MAXPKT];
  makeNonce(nonce);
  size_t n = seal(b, TTL, nonce, netKey, type, me, dst, body, len, key);
  if (!n) return sys("too long");
  seenBefore(get32(b + n - 16));  // don't relay our own echo
  queue(b, n, delay);
}

void beacon() {
  uint8_t b[48];
  memcpy(b, pub, 32);
  memcpy(b + 32, myName.c_str(), myName.length());
  send(T_BEACON, BCAST, b, 32 + myName.length(), nullptr);
  nextBeacon = millis() + BEACON_MS + random(30000);
}

void sendMsg(Out &o) {
  Contact *c = find(contacts, o.peer);
  if (!c) {  // contact deleted
    o.tries = TRIES;
    return;
  }
  uint8_t b[4 + MAXTEXT];
  put32(b, o.msgId);
  memcpy(b + 4, o.text.c_str(), o.text.length());
  send(T_MSG, o.peer, b, 4 + o.text.length(), c->key);
  o.tries++;
  o.at = millis() + ACK_MS;
}

void setState(uint32_t peer, uint32_t msgId, char state) {
  for (auto &l : lines)
    if (l.peer == peer && l.msgId == msgId && l.state != '<') l.state = state;
  dirty = true;
}

void sendText(const String &s) {
  if (contacts.empty()) return sys("no contacts yet");
  Out o{esp_random(), contacts[cur].id, s.substring(0, MAXTEXT), 0, millis()};
  addLine(o.peer, o.msgId, '?', o.text);
  outbox.push_back(o);
}

void onBeacon(Inner &in, Contact *c) {
  if (in.len() < 32 || in.len() > 48 || nodeId(in.body()) != in.src()) return;
  String name = clean(in.body() + 32, in.len() - 32);
  if (c) {  // known: just follow renames
    if (name != c->name) {
      strlcpy(c->name, name.c_str(), sizeof c->name);
      save();
    }
    return;
  }
  for (auto id : ignored)
    if (id == in.src()) return;
  if (find(pending, in.src()) || pending.size() >= 8) return;
  Contact n{};
  strlcpy(n.name, name.c_str(), sizeof n.name);
  memcpy(n.pub, in.body(), 32);
  n.id = in.src();
  pairKey(n.key, sec, pub, n.pub);
  pending.push_back(n);
  sys("REQUEST " + name + " " + fpStr(n.pub) + "  /ok /no");
  M5Cardputer.Speaker.tone(2000, 80);
}

void handleRx() {
  uint8_t pkt[MAXPKT];
  size_t len = radio.getPacketLength();
  int st = radio.readData(pkt, sizeof pkt);
  radio.startReceive();
  if (st != RADIOLIB_ERR_NONE) return;

  Inner in;
  if (!openNet(in, pkt, len, netKey)) return;  // not our network
  if (seenBefore(get32(pkt + len - 16))) return;
  uint32_t src = in.src(), dst = in.dst();
  if (src == me) return;
  lastRssi = radio.getRSSI();
  dirty = true;

  if (dst != me && pkt[0] > 0) {  // flood relay, same bytes with ttl-1
    pkt[0] = std::min(pkt[0], TTL) - 1;
    queue(pkt, len, 200 + random(3000));  // jitter > airtime so relays rarely collide
  }
  if (dst != me && dst != BCAST) return;

  Contact *c = find(contacts, src);
  if (c) c->seen = millis();
  if (in.type() == T_BEACON) return onBeacon(in, c);
  if (!c || dst != me || !openE2E(in, c->key) || in.len() < 4) return;  // only contacts can DM us
  uint32_t msgId = get32(in.body());

  if (in.type() == T_MSG) {
    uint8_t a[4];
    put32(a, msgId);
    send(T_ACK, src, a, 4, c->key, 100 + random(400));  // always ack, our last ack may have been lost
    for (auto &l : lines)
      if (l.peer == src && l.msgId == msgId && l.state == '<') return;  // retry dup
    addLine(src, msgId, '<', clean(in.body() + 4, in.len() - 4));
    if (contacts[cur].id != src) sys(String("msg from ") + c->name + " (Tab)");
    M5Cardputer.Speaker.tone(3000, 60);
  } else if (in.type() == T_ACK) {
    for (size_t i = 0; i < outbox.size(); i++)
      if (outbox[i].msgId == msgId && outbox[i].peer == src) {
        outbox.erase(outbox.begin() + i);
        break;
      }
    setState(src, msgId, '+');
  }
}

void command(String s) {
  int sp = s.indexOf(' ');
  String cmd = sp < 0 ? s : s.substring(0, sp), arg = sp < 0 ? "" : s.substring(sp + 1);
  arg.trim();
  if (cmd == "/name" && arg.length()) {
    myName = arg.substring(0, 16);
    prefs.putString("name", myName);
    sys("name: " + myName);
    if (netPass.isEmpty()) sys("type network passphrase + Enter");
    beacon();
  } else if (cmd == "/net" && arg.length()) {
    netPass = arg;
    prefs.putString("net", netPass);
    netKeyFrom(netKey, netPass.c_str());
    pending.clear();
    sys("network set. others with the same passphrase will show up as requests");
    beacon();
  } else if (cmd == "/ok") {
    if (pending.empty()) return sys("no requests");
    if (contacts.size() >= MAXCONTACTS) return sys("contact list full");
    contacts.push_back(pending[0]);
    pending.erase(pending.begin());
    save();
    cur = contacts.size() - 1;
    sys(String("added ") + contacts[cur].name);
    beacon();  // so they get our request too
  } else if (cmd == "/no") {
    if (pending.empty()) return sys("no requests");
    ignored.push_back(pending[0].id);
    pending.erase(pending.begin());
    sys("ignored until reboot");
  } else if (cmd == "/del") {
    if (contacts.empty()) return;
    sys(String("deleted ") + contacts[cur].name);
    contacts.erase(contacts.begin() + cur);
    cur = 0;
    save();
  } else if (cmd == "/loc") {
    if (!gps.location.isValid() || gps.location.age() > 10000)
      return sys("no GPS fix, sats " + String(gps.satellites.value()));
    sendText("loc " + String(gps.location.lat(), 6) + "," + String(gps.location.lng(), 6));
  } else if (cmd == "/b") {
    beacon();
    sys("beacon sent");
  } else if (cmd == "/fp") {
    sys("me " + fpStr(pub));
    if (!contacts.empty()) sys(String(contacts[cur].name) + " " + fpStr(contacts[cur].pub));
  } else {
    sys("/name /net /ok /no /del /loc /b(eacon) /fp  Tab=next");
  }
}

void enter(String s) {
  s.trim();
  if (s.isEmpty()) return;
  if (myName.isEmpty()) s = "/name " + s;  // first-boot setup
  else if (netPass.isEmpty()) s = "/net " + s;
  if (s[0] == '/') command(s);
  else sendText(s);
}

void draw() {
  cv.fillSprite(BLACK);
  cv.fillRect(0, 0, 240, 10, NAVY);
  cv.setTextColor(WHITE);
  cv.setCursor(2, 1);
  if (contacts.empty()) cv.print("no contacts");
  else {
    auto &c = contacts[cur];
    cv.printf("%.10s %d/%d", c.name, (int)cur + 1, (int)contacts.size());
    if (c.seen) {
      uint32_t s = (millis() - c.seen) / 1000;
      s < 120 ? cv.printf(" %lus", (unsigned long)s) : cv.printf(" %lum", (unsigned long)s / 60);
    }
  }
  cv.setCursor(150, 1);
  cv.printf("%s%4d s%lu", pending.empty() ? "   " : "REQ", lastRssi, (unsigned long)gps.satellites.value());

  // newest rows first, then drawn bottom-up
  struct Row {
    uint16_t col;
    String s;
  };
  std::vector<Row> rows;
  uint32_t peer = contacts.empty() ? 0 : contacts[cur].id;
  for (int i = (int)lines.size() - 1; i >= 0 && (int)rows.size() < ROWS; i--) {
    auto &l = lines[i];
    if (l.peer && l.peer != peer) continue;
    uint16_t col = !l.peer ? DARKGREY : l.state == '<' ? CYAN : l.state == '+' ? GREEN : l.state == '!' ? RED : WHITE;
    String pre = !l.peer ? "* " : l.state == '<' ? "< " : "> ";
    std::vector<String> chunks;
    for (size_t p = 0; p == 0 || p < l.text.length(); p += COLS)
      chunks.push_back((p ? "  " : pre) + l.text.substring(p, p + COLS));
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

void setup() {
  setCpuFrequencyMhz(80);  // plenty for chat, ~half the CPU current of 240
  // Hardware TRNG needs an entropy source; the ADC one must be off again before M5 uses the ADC
  bootloader_random_enable();
  esp_fill_random(bootSeed, sizeof bootSeed);
  prefs.begin("cardxat");
  if (prefs.getBytes("sec", sec, 32) != 32) {
    esp_fill_random(sec, 32);
    prefs.putBytes("sec", sec, 32);
  }
  bootloader_random_disable();
  // ponytail: keys sit in plain NVS, physical access = key theft. Enable flash encryption if that matters.
  crypto_x25519_public_key(pub, sec);
  me = nodeId(pub);
  myName = prefs.getString("name", "");
  netPass = prefs.getString("net", "");
  if (netPass.length()) netKeyFrom(netKey, netPass.c_str());
  contacts.resize(prefs.getBytesLength("contacts") / sizeof(Contact));
  prefs.getBytes("contacts", contacts.data(), contacts.size() * sizeof(Contact));
  for (auto &c : contacts) c.seen = 0;

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

  SPI.begin(P_SCK, P_MISO, P_MOSI, P_NSS);
  int st = radio.begin(FREQ, BW, SF, CR, SYNC, TXPWR, 8, TCXO_V, true);
  if (st != RADIOLIB_ERR_NONE) sys("radio init failed " + String(st));
  radio.setCurrentLimit(140);
  radio.setPacketReceivedAction(onRx);
  radio.startReceive();

  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX, GPS_TX);

  sys("me " + fpStr(pub));
  if (myName.isEmpty()) sys("type your name + Enter");
  else if (netPass.isEmpty()) sys("type network passphrase + Enter");
  else sys("/help for commands");
  nextBeacon = millis() + random(3000);
}

void loop() {
  M5Cardputer.update();
  while (Serial1.available()) gps.encode(Serial1.read());

  if (rxFlag) {
    rxFlag = false;
    handleRx();
  }

  if (myName.length() && (int32_t)(millis() - nextBeacon) >= 0) beacon();

  for (size_t i = 0; i < outbox.size();) {
    Out &o = outbox[i];
    if ((int32_t)(millis() - o.at) < 0) {
      i++;
      continue;
    }
    if (o.tries >= TRIES) {
      setState(o.peer, o.msgId, '!');
      outbox.erase(outbox.begin() + i);
      continue;
    }
    sendMsg(o);
    i++;
  }

  for (size_t i = 0; i < txq.size(); i++) {  // one packet per loop
    if ((int32_t)(millis() - txq[i].at) < 0) continue;
    int st = radio.transmit(txq[i].b, txq[i].n);
    if (st != RADIOLIB_ERR_NONE) sys("tx err " + String(st));
    txq.erase(txq.begin() + i);
    rxFlag = false;  // DIO1 also fires on tx done
    radio.startReceive();
    break;
  }

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    bool wasAsleep = asleep;
    wake();
    if (wasAsleep) return;  // first key only wakes the screen
    auto &k = M5Cardputer.Keyboard.keysState();
    if (k.tab && !contacts.empty()) cur = (cur + 1) % contacts.size();
    for (char ch : k.word)
      if (input.length() < MAXTEXT) input += ch;
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
