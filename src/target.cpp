#include <Arduino.h>
#include <M5Cardputer.h>
#include <SPI.h>
#include "utility/PI4IOE5V6408_Class.hpp"
#include "target.h"

CardputerBoard board;

// The radio shares the global SPI bus with the microSD card (src/app.cpp mounts it).
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);  // finds an RTC on the Grove port, if any
MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);
CardputerDisplay display;
volatile uint32_t tx_led_until = 0;

// DisplayDriver's colours (each MeshCore display driver defines its own).
ColorVal UIColor::window_bkg = 0x0000, UIColor::title_bkg = 0x0000, UIColor::title_txt = 0xFFFF,
         UIColor::primary_txt = 0xFFFF, UIColor::secondary_txt = 0x8410, UIColor::warning_txt = 0xFD20,
         UIColor::popup_bkg = 0x0000, UIColor::popup_txt = 0xFFFF, UIColor::corp_blue = 0x001F;

void CardputerBoard::begin() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 0;  // companion main.cpp opens Serial itself
  cfg.external_rtc = false;  // the Grove port belongs to MeshCore's Wire
  cfg.external_imu = false;
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(0);

  // Cap LoRa-1262: P0 of its IO expander (0x43, internal I2C) powers the RF switch.
  m5::PI4IOE5V6408_Class ioe(0x43, 400000, &m5::In_I2C);
  if (ioe.begin()) {
    ioe.setDirection(0, true);
    ioe.setHighImpedance(0, false);
    ioe.digitalWrite(0, true);
  } else {
    Serial.println("cap: no IO expander at 0x43 (Cap LoRa-1262 missing?)");
  }
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);  // SD deselected while the radio talks

  ESP32Board::begin();  // Wire on the Grove port (PIN_BOARD_SDA/SCL)
  extern void ui_early();
  ui_early();  // canvas allocated before BLE takes its RAM; boot screen
}

uint16_t CardputerBoard::getBattMilliVolts() {
  int16_t mv = M5.Power.getBatteryVoltage();
  return mv > 0 ? mv : 0;
}

void CardputerBoard::onBeforeTransmit() { tx_led_until = millis() + 80; }
void CardputerBoard::onAfterTransmit() {}

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  return radio.std_init(&SPI);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}

// ── CardputerDisplay: just enough for the boot message ──────────────────────
static uint16_t dispColor = 0xFFFF;
void CardputerDisplay::turnOn() { _on = true; M5Cardputer.Display.wakeup(); }
void CardputerDisplay::turnOff() { _on = false; M5Cardputer.Display.sleep(); }
void CardputerDisplay::clear() { M5Cardputer.Display.fillScreen(0); }
void CardputerDisplay::startFrame(ColorVal bkg) { M5Cardputer.Display.fillScreen(bkg); }
void CardputerDisplay::setTextSize(int sz) { M5Cardputer.Display.setTextSize(sz); }
void CardputerDisplay::setColor(ColorVal c) { dispColor = c ? 0x07EA : 0; }
void CardputerDisplay::setCursor(int x, int y) { M5Cardputer.Display.setCursor(x, y); }
void CardputerDisplay::print(const char* s) { M5Cardputer.Display.setTextColor(dispColor); M5Cardputer.Display.print(s); }
void CardputerDisplay::fillRect(int x, int y, int w, int h) { M5Cardputer.Display.fillRect(x, y, w, h, dispColor); }
void CardputerDisplay::drawRect(int x, int y, int w, int h) { M5Cardputer.Display.drawRect(x, y, w, h, dispColor); }
uint16_t CardputerDisplay::getTextWidth(const char* s) { return M5Cardputer.Display.textWidth(s); }
