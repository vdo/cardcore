#pragma once
// Board target for MeshCore's companion_radio: M5Stack Cardputer ADV + Cap LoRa-1262.

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/ESP32Board.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

class CardputerBoard : public ESP32Board {
public:
  void begin();
  uint16_t getBattMilliVolts() override;
  const char* getManufacturerName() const override { return "M5Stack Cardputer ADV"; }
  void onBeforeTransmit() override;
  void onAfterTransmit() override;
};

// companion main.cpp shows "Loading..." through this; the UI then draws on its own canvas.
class CardputerDisplay : public DisplayDriver {
  bool _on = true;
public:
  CardputerDisplay() : DisplayDriver(240, 135) {}
  bool begin() { return true; }
  bool isOn() override { return _on; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = 0) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int, int, const uint8_t*, int, int) override {}
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override {}
};

extern CardputerBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
extern CardputerDisplay display;
extern volatile uint32_t tx_led_until;  // ms: LED blink on transmit (ui.cpp drives the LED)

bool radio_init();
mesh::LocalIdentity radio_new_identity();
