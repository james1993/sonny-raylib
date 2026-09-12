#include "rng.h"

void rng_seed(Rng *r, uint64_t seed)
{
    r->state = seed ? seed : 0x9E3779B97F4A7C15ull;
    r->KRSC = 0;
    for (int i = 0; i < SONNY_KRS_SIZE; i++)
        r->KRS[i] = 0;
}

uint32_t rng_next(Rng *r)
{
    /* splitmix64 */
    uint64_t z = (r->state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z = z ^ (z >> 31);
    return (uint32_t)(z >> 32);
}

uint32_t rng_below(Rng *r, uint32_t n)
{
    if (n == 0)
        return 0;
    uint32_t limit = 0xFFFFFFFFu - (0xFFFFFFFFu % n);
    uint32_t x;
    do {
        x = rng_next(r);
    } while (x >= limit);
    return x % n;
}

void rng_refill_krs(Rng *r)
{
    for (int i = 0; i < SONNY_KRS_SIZE; i++)
        r->KRS[i] = (int32_t)rng_below(r, 100);
    r->KRSC = 0;
}

int32_t rng_krrr(Rng *r)
{
    int32_t value = r->KRS[r->KRSC];
    r->KRSC++;
    if (r->KRSC == SONNY_KRS_SIZE)
        r->KRSC = 0;
    return value;
}
