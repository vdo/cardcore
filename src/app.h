#pragma once
// What the UI (ui.cpp) needs from the node. app.cpp implements it on MeshCore;
// test/host.cpp fakes it to render the screens on a laptop.
#include <stdint.h>
#include "util.h"

// ── chats ────────────────────────────────────────────────────────────────────
enum ChatKind : uint8_t { CK_PUBLIC, CK_HASHTAG, CK_PRIVATE, CK_DIRECT, CK_ROOM };
struct ChatId {
  uint8_t kind;    // ChatKind
  uint8_t key[6];  // channel: first bytes of its secret; DM/room: of the contact's public key
  bool operator==(const ChatId& o) const { return memcmp(key, o.key, 6) == 0 && (kind >= CK_DIRECT) == (o.kind >= CK_DIRECT); }
};
struct Chat {
  ChatId id;
  char name[32];
  uint16_t unread;
  uint32_t last;  // unix time of the last message, 0 if none
};
enum ChatFilter { CF_ALL, CF_CHANNELS, CF_DIRECT, NCF };
int app_chats(Chat* out, int max, int filter);  // channels first, then DMs by recent activity
int app_load(const ChatId& id, Msg* out, int max);  // last `max` messages, oldest first
bool app_send(const ChatId& id, const char* text);
int app_text_limit(const ChatId& id);  // characters that fit one packet
void app_open_chat(const ChatId* id);  // marks read; null = no chat open
const char* app_chat_name(const ChatId& id);
int app_total_unread();

// ── channels (for the manager screen) ────────────────────────────────────────
int app_channel_count();  // slots, some empty
bool app_channel(int idx, Chat& out);  // false for an empty slot
bool app_add_hashtag(const char* name);  // "#name" or "name"
bool app_add_private(const char* name, const char* hexkey);  // 32 hex digits
bool app_remove_channel(int idx);
void app_channel_key(int idx, char hex[33]);

// ── contacts ─────────────────────────────────────────────────────────────────
enum { CT_CHAT = 1, CT_REPEATER = 2, CT_ROOM = 3, CT_SENSOR = 4 };
struct Contact {
  uint8_t key[32];
  char name[32];
  uint8_t type, fav;
  int8_t hops;      // -1: no known path (floods)
  uint8_t hashSize;  // path hash bytes
  uint8_t path[64];  // hop hashes, hops * hashSize bytes
  uint32_t lastHeard;  // unix, our clock
  int32_t lat, lon;  // 1e-6 degrees, 0 = unknown
};
int app_contact_count(int typeFilter);  // 0 = all, else CT_*
bool app_contact(int i, int typeFilter, Contact& out);  // most recently heard first
bool app_contact_by_key(const uint8_t* key6, Contact& out);
enum ContactAction { CA_MESSAGE, CA_LOGIN, CA_STATUS, CA_TELEMETRY, CA_TRACE, CA_RESET_PATH, CA_SHARE, CA_FAV, CA_DELETE };
// Runs an action. Remote ones (login/status/telemetry/trace) answer later through app_reply().
bool app_contact_action(const uint8_t* key, int action, const char* arg);

// The answer to the last remote request: lines of text for a result panel.
struct Reply {
  uint32_t serial;  // bumps on every change
  bool waiting, ok;
  char title[32];
  int n;
  char lines[9][40];
};
const Reply& app_reply();
void app_reply_show(const char* title, int n, const char* const* lines);  // local info in the same panel

// ── heard: adverts and discovery answers ─────────────────────────────────────
struct Heard {
  uint8_t key[6];
  char name[24];
  uint8_t type;
  int8_t hops, snr4;
  uint32_t at;  // millis
  bool known;   // in contacts
};
int app_heard(Heard* out, int max);  // newest first
void app_advert(bool flood);
void app_discover();  // zero-hop: nearby repeaters answer

// ── status, metrics, GPS ─────────────────────────────────────────────────────
struct Status {
  uint16_t battMv;
  int8_t battPct;  // -1 unknown
  bool charging, bleOn, bleConn, usbConn, gpsOn, gpsFix, sd, timeOk, txNow;
  uint32_t pin;  // BLE PIN
  uint32_t now;  // unix, UTC
  int tz;        // hours, summer time included
  char preset[24];
  float freq, bw;
  uint8_t sf, cr;
  int8_t txPower;
};
void app_status(Status& s);
const char* app_node_name();
void app_pubkey_hex(char out[65]);

constexpr int HIST = 60;
struct Metrics {
  uint32_t rx, tx, rxErr, floodTx, floodRx, directTx, directRx;
  uint32_t airTxMs, airRxMs, uptimeS;
  float duty1h;  // % of the last hour spent transmitting
  int noise;     // dBm
  float lastRssi, lastSnr;
  uint32_t heap, heapMin;
  int queue, contacts, channels;
  uint8_t rxHist[HIST];  // packets per 10 s, oldest first
  int8_t noiseHist[HIST];  // dBm, oldest first
};
void app_metrics(Metrics& m);
struct RxLog { uint32_t at; int16_t rssi; int8_t snr4; };
int app_rx_log(RxLog* out, int max);  // newest first

struct Gps {
  bool on, fix;
  int sats;
  double lat, lon;  // fix, or the node's set location
  int altM;
  bool nodeLocSet;
  double nodeLat, nodeLon;
};
void app_gps(Gps& g);
void app_gps_toggle();
bool app_gps_to_node();  // node location := GPS fix

// ── settings: a table the settings screen renders ────────────────────────────
enum SettingKind : uint8_t { SK_SECTION, SK_ENUM, SK_TEXT, SK_ACTION, SK_DANGER };
int app_setting_count();
uint16_t app_setting_label(int i);  // StrId
uint8_t app_setting_kind(int i);
void app_setting_value(int i, char* out, int cap);
void app_setting_adjust(int i, int dir);  // ,/ and Enter on enums
void app_setting_text(int i, char* out, int cap);  // text to edit
bool app_setting_set_text(int i, const char* text);  // false: invalid
enum ActResult { AR_DONE, AR_CHANNELS, AR_REPLY, AR_FAIL };
int app_setting_action(int i);  // SK_DANGER only after the UI's confirmation

// UI preferences (persisted by app.cpp)
// Saved as a blob: only ever append fields, never reorder (app_begin loads older, shorter blobs).
struct UiPrefs {
  uint8_t theme, bright, timeout, lang, sound, vol, led, ble;
  uint8_t setup;  // 0: the first-boot wizard hasn't run
  uint8_t dst;    // 1: EU summer time on top of the time zone
};
UiPrefs& app_ui();
void app_ui_save();
int app_setting_find(uint16_t label);  // row index by its label, -1 if none
void app_apply_preset(int i);

// ── events for the UI: toasts, beeps, the LED ────────────────────────────────
enum EventKind { EV_MSG, EV_ACK, EV_FAIL, EV_REPLY, EV_CONTACT, EV_TOAST };
struct Event {
  uint8_t kind;
  ChatId chat;
  char text[64];
};
bool app_event(Event& e);

void app_begin();
void app_loop();
