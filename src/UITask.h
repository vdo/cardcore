#pragma once
// MeshCore companion_radio's UI hook: forwards its callbacks to app.cpp and runs ui.cpp.
#include "AbstractUITask.h"

void app_begin();
void app_loop();
void ui_begin();
void ui_loop();
void app_on_msg(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t ts, const char* text);
void app_on_channel_msg(mesh::Packet* pkt, ChannelDetails& ch, const char* text);
void app_on_queue(int n);
void app_on_contact(ContactInfo& c, bool is_new, uint8_t path_len, const uint8_t* path);
void app_on_control(const mesh::Packet* pkt);
void app_on_ack(uint32_t ack);
bool app_on_response(const ContactInfo& c, uint32_t tag, const uint8_t* data, uint8_t len);
void app_on_trace(mesh::Packet* pkt, uint32_t tag, uint32_t auth, uint8_t flags, const uint8_t* snrs,
                  const uint8_t* hashes, uint8_t len);

class UITask : public AbstractUITask {
public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial) {}
  void begin(DisplayDriver*, SensorManager*, NodePrefs*) { app_begin(); ui_begin(); }
  void notify(UIEventType) override {}
  void loop() override { app_loop(); ui_loop(); }

  void onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t t, uint32_t ts, const char* text) override {
    app_on_msg(pkt, from, t, ts, text);
  }
  void onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& ch, const char* text) override { app_on_channel_msg(pkt, ch, text); }
  void onQueueSizeChanged(int n) override { app_on_queue(n); }
  void onDiscoveredContact(ContactInfo& c, bool is_new, uint8_t path_len, const uint8_t* path) override {
    app_on_contact(c, is_new, path_len, path);
  }
  void onControlDataRecv(const mesh::Packet* pkt) override { app_on_control(pkt); }
  void onACKRecv(uint32_t ack) override { app_on_ack(ack); }
  bool onUnhandledResponse(const ContactInfo& c, uint32_t tag, const uint8_t* data, uint8_t len) override {
    return app_on_response(c, tag, data, len);
  }
  void onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t auth, uint8_t flags, const uint8_t* snrs,
                   const uint8_t* hashes, uint8_t len) override {
    app_on_trace(pkt, tag, auth, flags, snrs, hashes, len);
  }
};
