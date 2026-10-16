// cc -c lib/monocypher/monocypher.c -o /tmp/m.o && c++ -std=c++17 -Isrc -Ilib/monocypher test/test_proto.cpp /tmp/m.o
// -o /tmp/t && /tmp/t
#include <cstdio>
#include "proto.h"

// not assert(): must still run under -DNDEBUG
#define CHECK(x) \
  if (!(x)) return printf("FAIL line %d: %s\n", __LINE__, #x), 1

int main() {
  uint8_t as[32], bs[32], cs[32], ap[32], bp[32], cp[32];
  memset(as, 0x11, 32), memset(bs, 0x22, 32), memset(cs, 0x33, 32);  // low bits get clamped, so not {1},{2},{3}
  crypto_x25519_public_key(ap, as);
  crypto_x25519_public_key(bp, bs);
  crypto_x25519_public_key(cp, cs);

  uint8_t kab[32], kba[32], kcb[32];
  pairKey(kab, as, ap, bp);
  pairKey(kba, bs, bp, ap);
  pairKey(kcb, cs, cp, bp);
  CHECK(!memcmp(kab, kba, 32));  // both ends agree
  CHECK(memcmp(kab, kcb, 32));

  uint8_t net[32], other[32];
  netKeyFrom(net, "secret");
  netKeyFrom(other, "nope");

  uint8_t nonce[24] = {9}, pkt[MAXPKT], body[] = "\x01\x02\x03\x04hello";
  size_t n = seal(pkt, 3, nonce, net, T_MSG, nodeId(ap), nodeId(bp), body, 9, kab);
  CHECK(n == OUTER + HDR + 9 + E2E);

  Inner in;
  CHECK(!openNet(in, pkt, n, other));  // other network: invisible
  pkt[0] = 1;                          // relay decrements ttl, MAC still holds
  CHECK(openNet(in, pkt, n, net));
  CHECK(in.type() == T_MSG && in.src() == nodeId(ap) && in.dst() == nodeId(bp));

  Inner spy = in;
  CHECK(!openE2E(spy, kcb));  // other net member can't read the DM
  CHECK(openE2E(in, kba) && in.len() == 9 && !memcmp(in.body(), body, 9));

  pkt[30] ^= 1;
  CHECK(!openNet(in, pkt, n, net));  // tampered

  uint8_t big[MAXPKT] = {};
  CHECK(seal(pkt, 3, nonce, net, T_MSG, 1, 2, big, 4 + MAXTEXT, kab) == MAXPKT);
  CHECK(seal(pkt, 3, nonce, net, T_MSG, 1, 2, big, 5 + MAXTEXT, kab) == 0);

  // beacon: net-sealed only
  n = seal(pkt, 3, nonce, net, T_BEACON, nodeId(ap), BCAST, ap, 32, nullptr);
  CHECK(openNet(in, pkt, n, net) && in.len() == 32 && nodeId(in.body()) == in.src());

  puts("ok");
}
