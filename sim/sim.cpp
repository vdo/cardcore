// Renders the README screenshots by running the real src/main.cpp on a PC: M5GFX's native renderer
// draws, the hardware is stubbed below, and a second node ("alice") is simulated with proto.h.
// Build + run: sh sim/run.sh
#include <M5GFX.h>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// ---- Arduino ----
static uint32_t now_ms, rng = 1;
uint32_t millis() { return now_ms; }
void delay(uint32_t ms) { now_ms += ms; }
uint32_t esp_random() { return rng = rng * 1664525 + 1013904223; }  // deterministic -> stable screenshots
long random(long n) { return esp_random() % n; }
void esp_fill_random(void *b, size_t n) {
  for (size_t i = 0; i < n; i++) ((uint8_t *)b)[i] = esp_random() >> 24;
}
void setCpuFrequencyMhz(int) {}
void bootloader_random_enable() {}
void bootloader_random_disable() {}
#define IRAM_ATTR
#define SERIAL_8N1 0
#define OUTPUT 1
#define HIGH 1
#define FILE_WRITE "w"
#define SET_LOOP_TASK_STACK_SIZE(n)
void pinMode(int, int) {}
void digitalWrite(int, int) {}

struct String : std::string {
  String() {}
  String(const char *s) : std::string(s) {}
  String(const std::string &s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  String(unsigned v) : std::string(std::to_string(v)) {}
  String(double v, int dec) : std::string(std::to_string(v).substr(0, std::to_string(v).find('.') + 1 + dec)) {}
  operator const char *() const { return c_str(); }  // what Arduino's Print::print(String) amounts to
  bool isEmpty() const { return empty(); }
  friend String operator+(const String &a, const char *b) { return std::string(a) + b; }
  friend String operator+(const char *a, const String &b) { return a + std::string(b); }
  friend String operator+(const String &a, const String &b) { return std::string(a) + std::string(b); }
  String substring(size_t a, size_t b = npos) const { return a >= size() ? String() : String(substr(a, b - a)); }
  void remove(size_t i) { erase(i); }
  int indexOf(char c) const { return find(c) == npos ? -1 : (int)find(c); }
  long toInt() const { return atol(c_str()); }
  void trim() {
    erase(0, find_first_not_of(' '));
    erase(find_last_not_of(' ') + 1);
  }
};

struct {
  void begin(int, int, int, int) {}
  int available() { return 0; }
  int read() { return -1; }
} Serial1;
struct {
  void begin(int, int, int, int) {}
} SPI;

// ---- M5Cardputer / M5Unified ----
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
    bool isPressed() { return false; }
    bool wasPressed() { return false; }
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

// ---- RadioLib: air is two queues ----
#define RADIOLIB_ERR_NONE 0
struct Module {
  Module(int, int, int, int) {}
};
struct SX1262 {
  std::vector<std::vector<uint8_t>> rx, tx;
  SX1262(Module *) {}
  int begin(float, float, int, int, int, int, int, float, bool) { return 0; }
  void setCurrentLimit(float) {}
  void setPacketReceivedAction(void (*)()) {}
  void startReceive() {}
  int transmit(const uint8_t *b, size_t n) {
    tx.emplace_back(b, b + n);
    now_ms += 1100;
    return 0;
  }
  size_t getPacketLength() { return rx.front().size(); }
  int readData(uint8_t *b, size_t) {
    memcpy(b, rx.front().data(), rx.front().size());
    rx.erase(rx.begin());
    return 0;
  }
  float getRSSI() { return -97; }
};

// ---- codec2 + SD: frames are opaque bytes here ----
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
  bool begin(int, decltype(SPI) &, int) { return true; }
  void mkdir(const char *) {}
  File open(const char *, const char * = "r") { return {true}; }
} SD;

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
  void encode(int) {}
};

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
};

#include "../src/main.cpp"

// ---- scenario ----
static const char *dir;
static uint8_t aSec[32], aPub[32], aNet[32], aKey[32];

void run(int n = 20) {
  while (n--) loop();
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
  run();
}
void fromAlice(uint8_t type, uint32_t dst, const uint8_t *body, size_t len, const uint8_t *key) {
  uint8_t nonce[24], pkt[MAXPKT];
  esp_fill_random(nonce, 24);
  radio.rx.emplace_back(pkt, pkt + seal(pkt, 3, nonce, aNet, type, nodeId(aPub), dst, body, len, key));
  rxFlag = true;
  run();
}
void aliceSays(const char *text) {
  uint8_t b[MAXPKT];
  put32(b, esp_random());
  memcpy(b + 4, text, strlen(text));
  fromAlice(T_MSG, me, b, 4 + strlen(text), aKey);
}
void aliceVoiceChunk(uint32_t id, uint32_t i, uint32_t total, size_t n, bool last) {
  uint8_t b[6 + VCHUNK] = {};
  put32(b, id);
  b[4] = i | (last ? 0x80 : 0);
  b[5] = total;
  fromAlice(T_VOICE, me, b, 6 + n, aKey);
}
// Sends a voice message the way the real sender cuts it, losing chunk `drop` on the first round.
// Doubles as the check for selective resend: bob must ack exactly what he got.
int aliceVoice(float secs, uint32_t drop) {
  uint32_t id = esp_random(), bytes = secs / 0.04 * VBYTES, total = (bytes + VCHUNK - 1) / VCHUNK;
  auto len = [&](uint32_t i) { return std::min<size_t>(VCHUNK, bytes - i * VCHUNK); };
  radio.tx.clear();
  for (uint32_t i = 0; i < total; i++)
    if (i != drop) aliceVoiceChunk(id, i, total, len(i), i == total - 1);
  uint32_t got = 0;
  for (auto &p : radio.tx) {
    Inner in;
    if (openNet(in, p.data(), p.size(), aNet) && in.type() == T_ACK && openE2E(in, aKey) && in.len() == 8)
      got = get32(in.body() + 4);
  }
  if (got != ((1u << total) - 1 & ~(1u << drop))) return printf("FAIL: bob acked %x\n", got), 1;
  aliceVoiceChunk(id, drop, total, len(drop), true);
  return lines.back().text.find("voice") == 0 ? 0 : (printf("FAIL: voice not completed\n"), 1);
}
void aliceAcks(size_t skipLast) {  // ACK what bob sent to alice, except the last `skipLast` messages
  std::vector<uint32_t> ids;
  for (auto &p : radio.tx) {
    Inner in;
    if (openNet(in, p.data(), p.size(), aNet) && in.type() == T_MSG && openE2E(in, aKey))
      ids.push_back(get32(in.body()));
  }
  radio.tx.clear();
  for (size_t i = 0; i + skipLast < ids.size(); i++) {
    uint8_t b[4];
    put32(b, ids[i]);
    fromAlice(T_ACK, me, b, 4, aKey);
  }
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
  type("bob");
  type("tide pool 47 lantern", false);
  shot("setup.png");
  type("");

  memset(aSec, 0xA1, 32);
  crypto_x25519_public_key(aPub, aSec);
  netKeyFrom(aNet, "tide pool 47 lantern");
  pairKey(aKey, aSec, aPub, pub);
  uint8_t beacon[37];
  memcpy(beacon, aPub, 32);
  memcpy(beacon + 32, "alice", 5);
  now_ms += 40000;
  fromAlice(T_BEACON, BCAST, beacon, sizeof beacon, nullptr);
  shot("request.png");

  type("/ok");
  aliceSays("are you ok? we're at the school gym");
  type("yes, 3 of us. need water, no injuries");
  aliceAcks(0);
  aliceSays("ok. road north is blocked, use the river path");
  if (aliceVoice(4.4, 1)) return 1;
  type("/loc");
  aliceAcks(0);
  type("leaving in 10 min");
  now_ms += 12000;
  run();
  shot("chat.png");
}
