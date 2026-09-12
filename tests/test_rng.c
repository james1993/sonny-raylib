/* The KRS ring buffer: 100 values in [0,100), walked cyclically, refilled at
   the start of each battle. */
#include <assert.h>
#include <stdio.h>
#include "../src/core/rng.h"

int main(void)
{
    Rng a, b;
    rng_seed(&a, 99);
    rng_seed(&b, 99);
    rng_refill_krs(&a);
    rng_refill_krs(&b);

    /* Same seed, same table. */
    for (int i = 0; i < SONNY_KRS_SIZE; i++)
        assert(a.KRS[i] == b.KRS[i]);

    /* Values are in range, and the cursor wraps with period 100. */
    int32_t first[SONNY_KRS_SIZE];
    for (int i = 0; i < SONNY_KRS_SIZE; i++) {
        first[i] = rng_krrr(&a);
        assert(first[i] >= 0 && first[i] < 100);
    }
    assert(a.KRSC == 0);
    for (int i = 0; i < SONNY_KRS_SIZE; i++)
        assert(rng_krrr(&a) == first[i]);

    /* A refill gives a different table (and resets the cursor). */
    rng_krrr(&a);
    rng_refill_krs(&a);
    assert(a.KRSC == 0);
    int differs = 0;
    for (int i = 0; i < SONNY_KRS_SIZE; i++)
        if (a.KRS[i] != first[i])
            differs = 1;
    assert(differs);

    printf("rng: KRS ring buffer behaves as the original's\n");
    return 0;
}
