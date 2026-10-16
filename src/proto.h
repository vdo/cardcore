// Packet format + crypto. No Arduino deps so test/test_proto.cpp can run it on a PC.
//
// On air:  ttl | nonce[24] | XChaCha20-Poly1305_net( type | src | dst | body ) | mac[16]
// MSG/ACK bodies are also sealed end-to-end with the X25519 pair key (same nonce, other key).
// ttl stays outside the MAC so relays can decrement it without re-encrypting.
#pragma once
#include <stdint.h>
#include <string.h>
#include "monocypher.h"

enum : uint8_t { T_BEACON = 1, T_MSG = 2, T_ACK = 3, T_VOICE = 4 };
constexpr uint32_t BCAST = 0xFFFFFFFF;
constexpr size_t MAXPKT = 255, OUTER = 1 + 24 + 16, HDR = 9, E2E = 16;
constexpr size_t MAXTEXT = MAXPKT - OUTER - HDR - E2E - 4;  // 4 = msgId

inline void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
inline uint32_t get32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}

// ponytail: plain hash, a weak passphrase can be brute-forced from sniffed packets
// (exposes names/metadata only, DMs stay E2E). Swap in crypto_argon2 if that matters.
inline void netKeyFrom(uint8_t key[32], const char *pass) {
  crypto_blake2b_keyed(key, 32, (const uint8_t *)"cardxat-net", 11, (const uint8_t *)pass, strlen(pass));
}

inline void fingerprint(uint8_t fp[8], const uint8_t pub[32]) { crypto_blake2b(fp, 8, pub, 32); }
inline uint32_t nodeId(const uint8_t pub[32]) {
  uint8_t fp[8];
  fingerprint(fp, pub);
  return get32(fp);
}

inline void pairKey(uint8_t key[32], const uint8_t mySec[32], const uint8_t myPub[32], const uint8_t theirPub[32]) {
  uint8_t s[96];
  crypto_x25519(s, mySec, theirPub);
  bool meFirst = memcmp(myPub, theirPub, 32) < 0;
  memcpy(s + 32, meFirst ? myPub : theirPub, 32);
  memcpy(s + 64, meFirst ? theirPub : myPub, 32);
  crypto_blake2b(key, 32, s, 96);
  crypto_wipe(s, sizeof s);
}

// Returns packet length, 0 if it doesn't fit. e2eKey == nullptr: body only net-sealed.
inline size_t seal(uint8_t *out, uint8_t ttl, const uint8_t nonce[24], const uint8_t netKey[32], uint8_t type,
                   uint32_t src, uint32_t dst, const uint8_t *body, size_t len, const uint8_t *e2eKey) {
  size_t n = HDR + len + (e2eKey ? E2E : 0);
  if (OUTER + n > MAXPKT) return 0;
  uint8_t in[MAXPKT];
  in[0] = type;
  put32(in + 1, src);
  put32(in + 5, dst);
  if (e2eKey) crypto_aead_lock(in + HDR, in + HDR + len, e2eKey, nonce, in, HDR, body, len);
  else memcpy(in + HDR, body, len);
  out[0] = ttl;
  memcpy(out + 1, nonce, 24);
  crypto_aead_lock(out + 25, out + 25 + n, netKey, nonce, nullptr, 0, in, n);
  return OUTER + n;
}

struct Inner {
  uint8_t b[MAXPKT];
  size_t n;
  const uint8_t *nonce;
  uint8_t type() const { return b[0]; }
  uint32_t src() const { return get32(b + 1); }
  uint32_t dst() const { return get32(b + 5); }
  uint8_t *body() { return b + HDR; }
  size_t len() const { return n - HDR; }
};

// false = not our network (or corrupted). pkt must outlive `in`.
inline bool openNet(Inner &in, const uint8_t *pkt, size_t len, const uint8_t netKey[32]) {
  if (len < OUTER + HDR || len > MAXPKT) return false;
  in.n = len - OUTER;
  in.nonce = pkt + 1;
  return crypto_aead_unlock(in.b, pkt + len - 16, netKey, in.nonce, nullptr, 0, pkt + 25, in.n) == 0;
}

// Decrypts body in place. false = not from the holder of `key`.
inline bool openE2E(Inner &in, const uint8_t key[32]) {
  if (in.len() < E2E) return false;
  size_t m = in.len() - E2E;
  if (crypto_aead_unlock(in.body(), in.body() + m, key, in.nonce, in.b, HDR, in.body(), m)) return false;
  in.n -= E2E;
  return true;
}
