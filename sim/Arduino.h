// Just enough Arduino for MeshCore, rweather Crypto and src/main.cpp to build on a PC (see sim/sim.cpp).
// Time and randomness are simulated so runs and screenshots are reproducible.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern uint32_t now_ms;
uint32_t esp_random();
inline uint32_t millis() { return now_ms; }
inline unsigned long micros() { return now_ms * 1000UL; }
inline void delay(uint32_t ms) { now_ms += ms; }
inline long random(long n) { return esp_random() % n; }
inline long random(long a, long b) { return a + (long)(esp_random() % (b - a)); }
inline void randomSeed(long) {}
inline char *ltoa(long v, char *s, int base) {
  char t[34], *p = t;
  unsigned long u = v < 0 && base == 10 ? -v : v;
  do *p++ = "0123456789abcdefghijklmnopqrstuvwxyz"[u % base];
  while (u /= base);
  char *o = s;
  if (v < 0 && base == 10) *o++ = '-';
  while (p > t) *o++ = *--p;
  *o = 0;
  return s;
}
inline void esp_fill_random(void *b, size_t n) {
  for (size_t i = 0; i < n; i++) ((uint8_t *)b)[i] = esp_random() >> 24;
}

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
  int indexOf(char c, size_t from = 0) const { return find(c, from) == npos ? -1 : (int)find(c, from); }
  bool startsWith(const String &p) const { return rfind(p, 0) == 0; }
  int lastIndexOf(char c, size_t from) const { return rfind(c, from) == npos ? -1 : (int)rfind(c, from); }
  long toInt() const { return atol(c_str()); }
  void trim() {
    erase(0, find_first_not_of(' '));
    erase(find_last_not_of(' ') + 1);
  }
};

struct Stream {  // MeshCore only prints/reads it in debug and file helpers we don't use
  virtual ~Stream() {}
  virtual int available() { return 0; }
  virtual int read() { return -1; }
  virtual size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t *, size_t n) { return n; }
  size_t readBytes(uint8_t *, size_t) { return 0; }
  size_t print(const char *) { return 0; }
  size_t print(char) { return 0; }
  size_t println(const char * = "") { return 0; }
  size_t printf(const char *, ...) { return 0; }
};
extern Stream Serial;
