#ifndef FZERO_GAME_CLOCK_H
#define FZERO_GAME_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/* Approximately 60.098812 Hz. Integer rounding is below 1 ns per frame. */
#define FZERO_GAME_PERIOD_NS UINT64_C(16639264)
#define FZERO_GAME_MAX_STEPS 3u

typedef struct FZeroGameClock {
  uint64_t last_ns;
  uint64_t credit_ns;
  uint32_t pending_input;
  bool paused;
} FZeroGameClock;

typedef struct FZeroGameSteps {
  unsigned count;
  uint32_t first_input;
} FZeroGameSteps;

static inline void FZeroGameClockInit(FZeroGameClock *clock, uint64_t now) {
  *clock = (FZeroGameClock){now, FZERO_GAME_PERIOD_NS, 0, false};
}

/* Poll once per host iteration. Keep short button presses until a game tick.
 * A menu pause discards both pending input and elapsed time. Large host stalls
 * restart the deadline instead of running seconds of input in a burst. */
static inline FZeroGameSteps FZeroGameClockPoll(FZeroGameClock *clock,
    uint64_t now, bool paused, uint32_t input) {
  FZeroGameSteps result = {0, 0};
  uint64_t elapsed = now >= clock->last_ns ? now - clock->last_ns : 0;
  clock->last_ns = now;
  if (paused) {
    clock->paused = true;
    clock->credit_ns = FZERO_GAME_PERIOD_NS;
    clock->pending_input = 0;
    return result;
  }
  if (clock->paused || elapsed > FZERO_GAME_PERIOD_NS * FZERO_GAME_MAX_STEPS) {
    clock->credit_ns = FZERO_GAME_PERIOD_NS;
    clock->pending_input = 0;
    elapsed = 0;
  }
  clock->paused = false;
  clock->credit_ns += elapsed;
  clock->pending_input |= input;
  result.count = (unsigned)(clock->credit_ns / FZERO_GAME_PERIOD_NS);
  clock->credit_ns %= FZERO_GAME_PERIOD_NS;
  if (result.count > FZERO_GAME_MAX_STEPS) result.count = FZERO_GAME_MAX_STEPS;
  result.first_input = clock->pending_input;
  if (result.count) clock->pending_input = 0;
  return result;
}

/* Sleep only for time not already spent in game work and presentation. */
static inline uint64_t FZeroGameClockWait(const FZeroGameClock *clock,
                                         uint64_t now) {
  uint64_t elapsed = now >= clock->last_ns ? now - clock->last_ns : 0;
  uint64_t remaining = clock->paused ? FZERO_GAME_PERIOD_NS :
      FZERO_GAME_PERIOD_NS - clock->credit_ns;
  return elapsed < remaining ? remaining - elapsed : 0;
}

#endif
