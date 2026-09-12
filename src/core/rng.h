/* The original does not call random() at roll time. At the start of every
 * battle it fills KRS[0..99] with random(100) and then walks that table
 * cyclically (KRRR/KRSC), so a battle's rolls repeat with period 100. That
 * quirk is part of how the game feels, so it is reproduced here rather than
 * replaced with a fresh draw per roll.
 *
 * The table is filled from a seeded generator so a battle can be replayed
 * exactly from its seed.
 */
#ifndef SONNY_RNG_H
#define SONNY_RNG_H

#include <stdint.h>

#define SONNY_KRS_SIZE 100

typedef struct {
    uint64_t state;
    int32_t  KRS[SONNY_KRS_SIZE];
    int32_t  KRSC;
} Rng;

/* Seed the underlying generator. Does not fill the table. */
void rng_seed(Rng *r, uint64_t seed);
uint32_t rng_next(Rng *r);
uint32_t rng_below(Rng *r, uint32_t n);

/* Refill KRS with 100 fresh values in [0,100) and reset the cursor. Call at
   the start of each battle, as the original does. */
void rng_refill_krs(Rng *r);
/* KRRR(): next table value, wrapping at 100. */
int32_t rng_krrr(Rng *r);

#endif
