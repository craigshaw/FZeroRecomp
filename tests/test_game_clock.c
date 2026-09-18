#include "game_clock.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { \
  fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)

static void cadence(unsigned refresh, unsigned divisor) {
  FZeroGameClock c;
  FZeroGameClockInit(&c, 0);
  unsigned total = 0, doubles = 0, repeats = 0;
  const unsigned seconds = 120;
  for (unsigned i = 0; i <= refresh * seconds / divisor; ++i) {
    uint64_t now = (uint64_t)i * UINT64_C(1000000000) * divisor / refresh;
    FZeroGameSteps s = FZeroGameClockPoll(&c, now, false, 1);
    CHECK(s.count <= FZERO_GAME_MAX_STEPS);
    total += s.count;
    doubles += s.count > 1;
    repeats += s.count == 0;
    CHECK(total == 1 + now / FZERO_GAME_PERIOD_NS);
  }
  if (refresh == 60 && divisor == 1) CHECK(doubles >= 11 && doubles <= 12);
  if (refresh / divisor >= 120) CHECK(repeats > 0);
}

int main(void) {
  cadence(30, 1); cadence(60, 1); cadence(120, 1); cadence(144, 1);
  cadence(60000, 1001); cadence(240, 1);
  FZeroGameClock c;
  const uint64_t p = FZERO_GAME_PERIOD_NS;
  FZeroGameClockInit(&c, 0);
  CHECK(FZeroGameClockPoll(&c, 0, false, 0).count == 1);
  CHECK(FZeroGameClockWait(&c, p / 2) == p - p / 2);
  CHECK(FZeroGameClockWait(&c, p * 2) == 0);
  CHECK(FZeroGameClockPoll(&c, p / 4, false, 8).count == 0);
  FZeroGameSteps s = FZeroGameClockPoll(&c, p, false, 0);
  CHECK(s.count == 1 && s.first_input == 8);
  CHECK(FZeroGameClockPoll(&c, p * 2, false, 0).first_input == 0);
  CHECK(FZeroGameClockPoll(&c, p * 2 + 1, false, 8).count == 0);
  CHECK(FZeroGameClockPoll(&c, p * 3, true, 8).count == 0);
  CHECK(FZeroGameClockPoll(&c, p * 3000, true, 8).count == 0);
  s = FZeroGameClockPoll(&c, p * 4000, false, 0);
  CHECK(s.count == 1 && s.first_input == 0);
  CHECK(FZeroGameClockWait(&c, p * 4000) == p);
  s = FZeroGameClockPoll(&c, p * 8000, false, 1);
  CHECK(s.count == 1 && s.first_input == 1);
  CHECK(FZeroGameClockPoll(&c, p * 8000, false, 0).count == 0);
  CHECK(FZeroGameClockPoll(&c, p * 8003, false, 0).count == 3);
  /* A released tap before a long stall must not replay on recovery. */
  CHECK(FZeroGameClockPoll(&c, p * 8003 + 1, false, 8).count == 0);
  s = FZeroGameClockPoll(&c, p * 9000, false, 0);
  CHECK(s.count == 1 && s.first_input == 0);
  CHECK(FZeroGameClockWait(&c, p * 9000 + p / 2) == p - p / 2);
  /* A retained tap is delivered once; a held button remains on later ticks. */
  CHECK(FZeroGameClockPoll(&c, p * 9000 + p / 4, false, 8).count == 0);
  s = FZeroGameClockPoll(&c, p * 9002, false, 1);
  CHECK(s.count == 2 && s.first_input == 9);
  s = FZeroGameClockPoll(&c, p * 9003, false, 1);
  CHECK(s.count == 1 && s.first_input == 1);
  puts("Game clock: refresh independence, input, pause, waits and stall recovery passed");
  return 0;
}
