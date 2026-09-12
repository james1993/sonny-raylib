#include "rng.h"

void rng_seed(Rng *r, uint64_t seed)
{
    r->s = seed ? seed : 0x9E3779B97F4A7C15ull;
}

uint32_t rng_next(Rng *r)
{
    /* splitmix64, upper bits */
    uint64_t z = (r->s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return (uint32_t)(z >> 32);
}

uint32_t rng_below(Rng *r, uint32_t n)
{
    if (n == 0)
        return 0;
    /* Rejection sampling keeps the distribution exactly uniform. */
    uint32_t limit = 0xFFFFFFFFu - (0xFFFFFFFFu % n);
    uint32_t x;
    do {
        x = rng_next(r);
    } while (x >= limit);
    return x % n;
}

int32_t rng_range(Rng *r, int32_t lo, int32_t hi)
{
    if (hi <= lo)
        return lo;
    return lo + (int32_t)rng_below(r, (uint32_t)(hi - lo + 1));
}

int rng_chance(Rng *r, int32_t percent)
{
    if (percent <= 0)
        return 0;
    if (percent >= 100)
        return 1;
    return (int32_t)rng_below(r, 100) < percent;
}
