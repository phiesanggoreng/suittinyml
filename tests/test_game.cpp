#include <cassert>
#include <limits>
#include <iostream>
#include "../wokwi/game_logic.h"

int main() {
  // Matriks diurutkan BATU,GUNTING,KERTAS: baris pemain, kolom CPU.
  const Result expected[3][3] = {
    {SERI, MENANG, KALAH}, {KALAH, SERI, MENANG}, {MENANG, KALAH, SERI}
  };
  for (int p = 0; p < 3; ++p)
    for (int c = 0; c < 3; ++c) assert(decideWinner(p, c) == expected[p][c]);
  assert(decideWinner(TIDAK_VALID, BATU) == DITOLAK);
  assert(decideWinner(-1, BATU) == DITOLAK);
  float score = 0;
  float certain[] = {.90f, .03f, .03f, .04f};
  float uncertain[] = {.40f, .30f, .20f, .10f};
  float invalid[] = {.01f, .01f, .01f, .97f};
  float broken[] = {std::numeric_limits<float>::quiet_NaN(), 0, 0, 1};
  assert(selectPose(certain, &score) == BATU);
  assert(selectPose(uncertain, &score) == TIDAK_VALID);
  assert(selectPose(invalid, &score) == TIDAK_VALID);
  assert(selectPose(broken, &score) == TIDAK_VALID);
  DebouncedButton button;
  assert(!button.update(false, 0));
  assert(!button.update(true, 100));
  assert(!button.update(false, 105)); // bounce
  assert(!button.update(true, 110));
  assert(!button.update(true, 144));
  assert(button.update(true, 145));
  assert(!button.update(true, 200));
  assert(!button.update(true, 5000)); // hold tidak menjadi ronde baru
  assert(!button.update(false, 5010));
  assert(!button.update(false, 5050));
  assert(!button.update(true, 5060));
  assert(button.update(true, 5095));
  DebouncedButton wrap;
  assert(!wrap.update(true, UINT32_MAX - 10));
  assert(wrap.update(true, 25));
  std::cout << "PASS: 9 hasil suit, invalid/low score/NaN, debounce, hold, millis wrap\n";
}
