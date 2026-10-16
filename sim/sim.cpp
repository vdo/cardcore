// End-to-end test + README screenshots. Runs the real src/main.cpp ("bob") and the real MeshCore library
// on a PC, talking over an in-memory radio to a second, plain MeshCore chat node ("alice").
// Hardware is stubbed below; M5GFX's native renderer draws the screens. Build + run: sh sim/run.sh
#include <M5GFX.h>
#include <Arduino.h>
#include <RNG.h>  // before MeshCore: its SEED_SIZE macro clashes with RNG.h
#include <Mesh.h>
#include <helpers/BaseChatMesh.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <ctime>
#include <map>
#include <vector>

uint32_t now_ms, rng_state = 1;
uint32_t esp_random() { return rng_state = rng_state * 1664525 + 1013904223; }  // deterministic runs
Stream Serial;
// rweather's Ed25519 links its keygen to this global; MeshCore brings its own RNG
RNGClass::RNGClass() {}
RNGClass::~RNGClass() {}
void RNGClass::rand(uint8_t *d, size_t n) { esp_fill_random(d, n); }
RNGClass RNG;

// ---- hardware stubs for main.cpp ----
#define IRAM_ATTR
#define SERIAL_8N1 0
#define OUTPUT 1
#define HIGH 1
#define FILE_WRITE "w"
#define SET_LOOP_TASK_STACK_SIZE(n)
#define P_LORA_NSS 5
#define P_LORA_DIO_1 4
#define P_LORA_RESET 3
#define P_LORA_BUSY 6
void setCpuFrequencyMhz(int) {}
void bootloader_random_enable() {}
void bootloader_random_disable() {}
void pinMode(int, int) {}
void digitalWrite(int, int) {}

struct {
  void begin(int, int, int, int) {}
  int available() { return 0; }
  int read() { return -1; }
} Serial1;
struct SpiStub {
} SPI;

struct {
  int config() { return 0; }
  void update() {}
} M5;
struct SimDisplay : M5Canvas {
  void setRotation(int) {}  // sprite is already landscape
  void sleep() {}
  void wakeup() {}
};
struct KeysState {
  bool tab, del, enter;
  std::vector<char> word;
};
struct {
  SimDisplay Display;
  struct {
    KeysState k;
    bool changed;
    bool isChange() { return changed; }
    bool isPressed() { return true; }
    KeysState &keysState() { return k; }
  } Keyboard;
  struct {
    void tone(int, int) {}
    void setVolume(int) {}
    void begin() {}
    void end() {}
    void stop(int) {}
    int isPlaying(int = 0) { return 0; }
    void playRaw(const int16_t *, size_t, int, bool, int, int) {}
  } Speaker;
  struct {
    void begin() {}
    void end() {}
    int isRecording() { return 0; }
    void record(int16_t *, size_t, int) {}
  } Mic;
  struct {
    int held;  // frames the button stays down
    bool press;
    bool isPressed() { return held-- > 0; }
    bool wasPressed() { return press ? !(press = false) : false; }
  } BtnA;
  void begin(int, bool) { Display.createSprite(240, 135); }
  void update() {}
} M5Cardputer;
namespace m5 {
struct I2C_Class {
} In_I2C;
struct PI4IOE5V6408_Class {
  PI4IOE5V6408_Class(int, int, I2C_Class *) {}
  bool begin() { return true; }
  void setDirection(int, bool) {}
  void setHighImpedance(int, bool) {}
  void digitalWrite(int, bool) {}
};
}  // namespace m5

struct Preferences {
  std::map<std::string, std::string> m;
  void begin(const char *) {}
  size_t getBytesLength(const char *k) { return m.count(k) ? m[k].size() : 0; }
  size_t getBytes(const char *k, void *b, size_t n) {
    if (!m.count(k)) return 0;
    n = std::min(n, m[k].size());
    memcpy(b, m[k].data(), n);
    return n;
  }
  void putBytes(const char *k, const void *b, size_t n) { m[k].assign((const char *)b, n); }
  String getString(const char *k, const char *d) { return m.count(k) ? String(m[k]) : String(d); }
  void putString(const char *k, const String &v) { m[k] = v; }
  uint32_t getUInt(const char *k, uint32_t d) { return m.count(k) ? atol(m[k].c_str()) : d; }
  void putUInt(const char *k, uint32_t v) { m[k] = std::to_string(v); }
  bool getBool(const char *k, bool d) { return m.count(k) ? m[k] == "1" : d; }
  void putBool(const char *k, bool v) { m[k] = v ? "1" : "0"; }
};

struct TinyGPSPlus {
  struct {
    bool isValid() { return true; }
    uint32_t age() { return 800; }
    double lat() { return 41.387917; }
    double lng() { return 2.169919; }
  } location;
  struct {
    uint32_t value() { return 9; }
  } satellites;
  struct {
    bool isValid() { return false; }  // no GPS time: the sim clock stays put
    int year() { return 0; }
    int month() { return 0; }
    int day() { return 0; }
  } date;
  struct {
    bool isValid() { return false; }
    int hour() { return 0; }
    int minute() { return 0; }
    int second() { return 0; }
  } time;
  void encode(int) {}
};
struct DateTime {
  std::tm t{};
  DateTime(int y, int mo, int d, int h, int mi, int s) { t = {s, mi, h, d, mo - 1, y - 1900}; }
  uint32_t unixtime() { return timegm(&t); }
};

#define CODEC2_MODE_1300 4
struct CODEC2 {};
CODEC2 *codec2_create(int) { return new CODEC2; }
void codec2_encode(CODEC2 *, unsigned char *b, short *) { memset(b, 0, 7); }
void codec2_decode(CODEC2 *, short *pcm, const unsigned char *) { memset(pcm, 0, 640); }
struct File {
  bool ok;
  operator bool() const { return ok; }
  size_t write(const uint8_t *, size_t n) { return n; }
  size_t size() { return 0; }
  size_t read(uint8_t *, size_t) { return 0; }
  void close() {}
  const char *name() { return ""; }
  File openNextFile() { return {false}; }
};
struct {
  bool begin(int, SpiStub &, int) { return true; }
  void mkdir(const char *) {}
  File open(const char *, const char * = "r") { return {true}; }
} SD;

// ---- radio: two nodes share the air; deaf[i] drops what node i would hear ----
struct {
  std::vector<std::vector<uint8_t>> q[2];
  bool deaf[2];
} air;
struct SimRadio : mesh::Radio {
  int me;
  explicit SimRadio(int i) : me(i) {}
  int recvRaw(uint8_t *b, int sz) override {
    auto &q = air.q[me];
    if (q.empty()) return 0;
    int n = std::min<int>(sz, q.front().size());
    memcpy(b, q.front().data(), n);
    q.erase(q.begin());
    return n;
  }
  uint32_t getEstAirtimeFor(int len) override { return 60 + len * 5; }  // roughly SF8 / 62.5 kHz
  float packetScore(float, int) override { return 1; }
  bool startSendRaw(const uint8_t *b, int n) override {
    if (!air.deaf[1 - me]) air.q[1 - me].emplace_back(b, b + n);
    return true;
  }
  bool isSendComplete() override { return true; }
  void onSendFinished() override {}
  bool isInRecvMode() const override { return true; }
  float getLastRSSI() const override { return -97; }
  float getLastSNR() const override { return 8; }
};
struct Module {
  Module(int, int, int, int, SpiStub &) {}
};
struct CustomSX1262 {
  CustomSX1262(Module *) {}
  bool std_init(SpiStub *) { return true; }
};
struct CustomSX1262Wrapper : SimRadio {
  CustomSX1262Wrapper(CustomSX1262 &, mesh::MainBoard &) : SimRadio(0) {}
  void setParams(float, float, uint8_t, uint8_t) {}
};
struct ESP32Board : mesh::MainBoard {
  uint16_t getBattMilliVolts() override { return 4000; }
  const char *getManufacturerName() const override { return "sim"; }
  void reboot() override {}
  uint8_t getStartupReason() const override { return 0; }
};
struct ESP32RTCClock : mesh::RTCClock {
  uint32_t base = 1767225600;  // 2026-01-01
  void begin() {}
  uint32_t getCurrentTime() override { return base + now_ms / 1000; }
  void setCurrentTime(uint32_t t) override { base = t - now_ms / 1000; }
};
struct SensorManager {};

#include "../src/main.cpp"

// ---- alice: a second, plain MeshCore chat node ----
struct SimRng : mesh::RNG {
  void random(uint8_t *d, size_t n) override { esp_fill_random(d, n); }
};
SimRadio aliceRadio(1);
ArduinoMillis aliceMs;
SimRng aliceRng;
ESP32RTCClock aliceClock;
SimpleMeshTables aliceTables;
StaticPoolPacketManager alicePool(16);

struct Alice : BaseChatMesh {
  uint32_t lastGot;                 // chunk bitmap from bob's last voice ack
  std::map<uint64_t, uint32_t> rx;  // chunks of bob's voice messages
  std::vector<std::string> inbox;

  Alice() : BaseChatMesh(aliceRadio, aliceMs, aliceRng, aliceClock, alicePool, aliceTables) {}
  void start() {
    BaseChatMesh::begin();
    self_id = mesh::LocalIdentity(&aliceRng);
    ChannelDetails cd;
    memset(&cd, 0, sizeof cd);
    memcpy(cd.channel.secret, PUBLIC_PSK, 16);
    strcpy(cd.name, "Public");
    setChannel(0, cd);
  }
  const mesh::LocalIdentity &me() const { return self_id; }
  ContactInfo *bob() { return lookupContactByPubKey(node.me().pub_key, PUB_KEY_SIZE); }
  void advert() {
    if (auto p = createSelfAdvert("alice")) sendFlood(p);
  }
  void say(const char *t) {
    uint32_t ack, est;
    sendMessage(*bob(), getRTCClock()->getCurrentTimeUnique(), 0, t, ack, est);
  }
  void post(const char *t) {
    ChannelDetails cd;
    getChannel(0, cd);
    sendGroupMessage(getRTCClock()->getCurrentTimeUnique(), cd.channel, "alice", t, strlen(t));
  }
  // A voice chunk cut the way cardcore cuts it. The key is shared, so bob's side can encrypt it for us.
  void voiceChunk(uint64_t vid, int i, int total, size_t n, bool last) {
    uint8_t b[VHDR + VCHUNK] = {REQ_VOICE};
    memcpy(b + 1, &vid, 8);
    b[9] = i | (last ? 0x80 : 0);
    b[10] = total;
    b[11] = n / VBYTES;
    node.voiceCrypt(*node.lookupContactByPubKey(self_id.pub_key, PUB_KEY_SIZE), vid, i, b + VHDR, n);
    uint32_t tag, est;
    sendRequest(*bob(), b, VHDR + n, tag, est);
  }

 protected:
  void onDiscoveredContact(ContactInfo &, bool, uint8_t, const uint8_t *) override {}
  void onContactPathUpdated(const ContactInfo &) override {}
  ContactInfo *processAck(const uint8_t *) override { return nullptr; }
  void onMessageRecv(const ContactInfo &, mesh::Packet *, uint32_t, const char *t) override { inbox.push_back(t); }
  void onCommandDataRecv(const ContactInfo &, mesh::Packet *, uint32_t, const char *) override {}
  void onCLICommandRecv(const ContactInfo &, mesh::Packet *, uint32_t, const char *, char *) override {}
  void onSignedMessageRecv(const ContactInfo &, mesh::Packet *, uint32_t, const uint8_t *, const char *) override {}
  void onChannelMessageRecv(const mesh::GroupChannel &, mesh::Packet *, uint32_t, const char *) override {}
  uint8_t onContactRequest(const ContactInfo &, uint32_t ts, const uint8_t *d, uint8_t len, uint8_t *reply) override {
    if (len < VHDR || d[0] != REQ_VOICE) return 0;
    uint64_t vid;
    memcpy(&vid, d + 1, 8);
    uint32_t &got = rx[vid];
    got |= 1u << (d[9] & 0x7f);
    if (!(d[9] & 0x80)) return 0;
    memcpy(reply, &ts, 4);
    reply[4] = REQ_VOICE;
    memcpy(reply + 5, &vid, 8);
    memcpy(reply + 13, &got, 4);
    return 17;
  }
  void onContactResponse(const ContactInfo &, const uint8_t *d, uint8_t len) override {
    if (len >= 17) memcpy(&lastGot, d + 13, 4);
  }
  uint32_t calcFloodTimeoutMillisFor(uint32_t t) const override { return 500 + 16 * t; }
  uint32_t calcDirectTimeoutMillisFor(uint32_t t, uint8_t p) const override {
    return 500 + (t * 6 + 250) * ((p & 63) + 1);
  }
  void onSendTimeout() override {}
} alice;

// ---- scenario ----
static const char *dir;
#define CHECK(x) \
  if (!(x)) return printf("FAIL line %d: %s\n", __LINE__, #x), 1

void run(uint32_t ms) {
  for (uint32_t end = now_ms + ms; now_ms < end;) {
    loop();  // bob: advances the clock by its 5 ms idle delay
    alice.loop();
  }
}
void type(const char *s, bool enterKey = true) {
  for (; *s; s++) {
    M5Cardputer.Keyboard.k = {false, false, false, {*s}};
    M5Cardputer.Keyboard.changed = true;
    loop();
  }
  M5Cardputer.Keyboard.k = {false, false, enterKey, {}};
  M5Cardputer.Keyboard.changed = enterKey;
  loop();
  M5Cardputer.Keyboard.changed = false;
}
const Line *findLine(uint32_t peer, const char *prefix) {
  for (auto &l : lines)
    if (l.peer == peer && l.text.rfind(prefix, 0) == 0) return &l;
  return nullptr;
}
void shot(const char *name) {
  draw();
  M5Canvas big;  // 3x nearest-neighbour so it isn't a stamp on GitHub
  big.createSprite(720, 405);
  for (int y = 0; y < 135; y++)
    for (int x = 0; x < 240; x++) big.fillRect(x * 3, y * 3, 3, 3, M5Cardputer.Display.readPixel(x, y));
  size_t n;
  void *png = big.createPng(&n, 0, 0, 720, 405);  // defaults of 0 mean "nothing", not "all"
  std::string path = std::string(dir) + "/" + name;
  FILE *f = fopen(path.c_str(), "wb");
  fwrite(png, 1, n, f);
  fclose(f);
  free(png);
  printf("%s\n", path.c_str());
}

int main(int argc, char **argv) {
  dir = argc > 1 ? argv[1] : ".";
  setup();
  alice.start();
  type("bob", false);
  shot("setup.png");
  type("");  // name set: bob adverts
  run(5000);
  CHECK(alice.bob());  // alice auto-adds from adverts

  alice.advert();
  run(5000);
  shot("request.png");
  CHECK(pending.size() == 1);
  type("/ok");
  ContactInfo *a = node.lookupContactByPubKey(alice.me().pub_key, PUB_KEY_SIZE);
  CHECK(a && cur == idOf(*a));
  uint32_t A = idOf(*a);

  alice.say("are you ok? we're at the school gym");
  run(5000);
  CHECK(findLine(A, "are you ok"));
  type("yes, 3 of us. need water, no injuries");
  run(10000);
  CHECK(findLine(A, "yes, 3 of us")->state == '+');  // MeshCore ack came back
  CHECK(a->out_path_len != OUT_PATH_UNKNOWN);        // and bob learned the route
  CHECK(alice.inbox.size() == 1);
  alice.say("ok. road north is blocked, use the river path");
  run(5000);

  // Voice from alice, chunk 1 lost on the first round: bob must ack exactly what he got, then complete
  uint64_t vid = 0x1122334455667788;
  size_t bytes = 110 * VBYTES, total = (bytes + VCHUNK - 1) / VCHUNK;  // 4.4 s
  auto len = [&](size_t i) { return std::min(VCHUNK, bytes - i * VCHUNK); };
  for (size_t i = 0; i < total; i++) {
    if (i != 1) alice.voiceChunk(vid, i, total, len(i), i == total - 1);
    run(3000);
  }
  CHECK(alice.lastGot == ((1u << total) - 1 & ~2u));
  alice.voiceChunk(vid, 1, total, len(1), true);
  run(5000);
  CHECK(alice.lastGot == (1u << total) - 1);
  CHECK(findLine(A, "voice 4.4s"));

  type("/loc");
  run(10000);
  CHECK(findLine(A, "loc ")->state == '+');

  M5Cardputer.BtnA = {75, true};  // hold G0 for 3 s: bob sends voice to alice
  run(40000);
  CHECK(findLine(A, "voice 3.0s")->state == '+');

  alice.post("anyone near the bridge? need a medic");
  run(5000);
  cur = CHAN;
  type("medic here, 5 min away");
  run(5000);
  CHECK(findLine(CHAN, "alice: anyone near"));
  shot("channel.png");

  cur = A;
  air.deaf[1] = true;  // alice out of reach: this one stays "sending"
  type("leaving in 10 min");
  run(1000);
  shot("chat.png");
  return 0;
}
