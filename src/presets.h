#pragma once
// Radio templates. MeshCore Catalunya first (the default, docs.livemap-meshcorecat.com/configuracio-radio),
// then the community presets from MeshCore's app API (api.meshcore.nz/api/v1/config, Oct 2026),
// minus the 433 MHz ones: the Cap LoRa-1262 is an 868-923 MHz radio.
#include <stdint.h>

struct Preset {
  const char* name;  // <= 22 chars: fits the settings value column
  float freq, bw;
  uint8_t sf, cr;
  uint8_t hash;  // path hash size in bytes (path_hash_mode = hash - 1)
};

static const Preset PRESETS[] = {
    {"Catalunya", 869.618f, 62.5f, 7, 6, 2},  // 2-byte path hashes: the CardCore default for Catalunya
    {"EU/UK Narrow", 869.618f, 62.5f, 8, 8, 1},
    {"EU/UK (deprecated)", 869.525f, 250.f, 11, 5, 1},
    {"Czech Rep. Narrow", 869.432f, 62.5f, 7, 5, 2},
    {"Hungary", 869.618f, 62.5f, 7, 5, 2},
    {"Netherlands", 869.618f, 62.5f, 7, 5, 1},
    {"Netherlands Limburg", 869.618f, 62.5f, 8, 8, 2},
    {"Portugal 868", 869.618f, 62.5f, 7, 6, 1},
    {"Slovakia", 869.618f, 62.5f, 7, 5, 2},
    {"Switzerland", 869.618f, 62.5f, 8, 8, 1},
    {"USA", 910.525f, 62.5f, 7, 5, 1},
    {"USA South. Calif.", 927.875f, 62.5f, 7, 5, 3},
    {"Canada", 910.525f, 62.5f, 7, 5, 3},
    {"Costa Rica", 910.525f, 125.f, 11, 5, 1},
    {"Brazil", 923.125f, 62.5f, 8, 8, 1},
    {"Australia", 915.8f, 250.f, 10, 5, 1},
    {"Australia Narrow", 916.575f, 62.5f, 7, 7, 1},
    {"Australia Mid", 915.075f, 125.f, 9, 5, 1},
    {"Australia SA, WA", 923.125f, 62.5f, 8, 8, 1},
    {"Australia QLD", 923.125f, 62.5f, 8, 5, 1},
    {"New Zealand Narrow", 917.375f, 62.5f, 7, 5, 2},
    {"New Zealand Gisborne", 917.375f, 250.f, 11, 5, 1},
    {"Vietnam Narrow", 920.25f, 62.5f, 8, 5, 1},
    {"Vietnam (deprecated)", 920.25f, 250.f, 11, 5, 1},
};
constexpr int NPRESET = sizeof PRESETS / sizeof *PRESETS;

static const float BANDWIDTHS[] = {7.8f, 10.4f, 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.f, 250.f, 500.f};
constexpr int NBW = sizeof BANDWIDTHS / sizeof *BANDWIDTHS;

// Which template the current settings match (-1: custom).
static int presetMatch(float freq, float bw, int sf, int cr, int hash) {
  for (int i = 0; i < NPRESET; i++) {
    const Preset& p = PRESETS[i];
    float df = freq - p.freq, db = bw - p.bw;
    if (df > -0.0005f && df < 0.0005f && db > -0.05f && db < 0.05f && p.sf == sf && p.cr == cr && p.hash == hash) return i;
  }
  return -1;
}
