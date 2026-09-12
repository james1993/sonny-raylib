/* Deterministic RNG. Combat must be reproducible from (seed, action log) so a
   recorded fight from the original can be replayed and diffed against ours. */
#ifndef SONNY_RNG_H
#define SONNY_RNG_H

#include <stdint.h>

typedef struct {
    uint64_t s;
} Rng;

void rng_seed(Rng *r, uint64_t seed);
uint32_t rng_next(Rng *r);
/* Uniform in [0, n). */
uint32_t rng_below(Rng *r, uint32_t n);
/* Uniform in [lo, hi] inclusive. */
int32_t rng_range(Rng *r, int32_t lo, int32_t hi);
/* True with probability percent/100. */
int rng_chance(Rng *r, int32_t percent);

#endif
