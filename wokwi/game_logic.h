#pragma once
#include <stdint.h>
#include <math.h>

// Urutan pilihan ini sama dengan tombol, training, dan output model.
enum Pose { BATU = 0, GUNTING = 1, KERTAS = 2, TIDAK_VALID = 3 };
enum Result { KALAH = -1, SERI = 0, MENANG = 1, DITOLAK = 2 };
static const char* const POSE_NAMES[] = {"BATU", "GUNTING", "KERTAS", "TIDAK VALID"};
constexpr float MIN_MODEL_SCORE = 0.75f;

inline int selectPose(const float scores[4], float* bestScore) {
  int best = 0;
  for (int i = 0; i < 4; ++i) {
    if (!isfinite(scores[i]) || scores[i] < 0.0f || scores[i] > 1.0f) {
      *bestScore = 0;
      return TIDAK_VALID;
    }
    if (scores[i] > scores[best]) best = i;
  }
  *bestScore = scores[best];
  return *bestScore >= MIN_MODEL_SCORE ? best : TIDAK_VALID;
}

inline Result decideWinner(int player, int cpu) {
  if (player < 0 || player > 2 || cpu < 0 || cpu > 2) return DITOLAK;
  if (player == cpu) return SERI;
  return ((player == BATU && cpu == GUNTING) ||
          (player == GUNTING && cpu == KERTAS) ||
          (player == KERTAS && cpu == BATU)) ? MENANG : KALAH;
}

// Satu event per tekan; ditahan tidak mengulangi ronde. Aman saat millis wrap.
struct DebouncedButton {
  bool previous = false;
  bool stable = false;
  uint32_t changedAt = 0;
  bool update(bool pressed, uint32_t now) {
    if (pressed != previous) { previous = pressed; changedAt = now; }
    if (pressed != stable && uint32_t(now - changedAt) >= 35) {
      stable = pressed;
      return stable;
    }
    return false;
  }
};
