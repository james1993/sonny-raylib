/* Differential test: every vector in vectors_formula.txt was produced by an
 * independent transcription of the original's executeMove()/perScript()
 * (tools/ref_formula.py). Both sides read the same decompiled source, so this
 * catches transcription drift between the reference and this port -- it is not
 * a substitute for diffing against the running original.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/formula.h"
#include "../src/core/unit.h"

static int read_fields(FILE *fh, double *v, int n)
{
    char line[2048];
    while (fgets(line, sizeof(line), fh)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        char *p = line;
        for (int i = 0; i < n; i++) {
            char *end;
            v[i] = strtod(p, &end);
            if (end == p)
                return -1;
            p = end;
        }
        return 1;
    }
    return 0;
}

/* What a blow comes to, which is the number the fight floats over the target
   and what its bars have to be drawn against. The reading is deliberately not
   clamped to the life the target had: a kill for more than is left counts for
   all of it, the way the original's DamageOutputKrinFinal does. */
static int check_landed(void)
{
    int failures = 0;
    Unit u;
    int32_t absorbed, landed;

    /* An overkill: seventeen left, hit for twenty-three. */
    unit_init(&u, 1);
    u.LIFEU = 100;
    u.LIFEN = 17;
    u.active = 1;
    landed = -1;
    if (formula_apply_damage(&u, 23, &absorbed, &landed) != 17
        || landed != 23 || u.LIFEN != 0 || u.active) {
        fprintf(stderr, "overkill: lost/landed/life/active wrong "
                "(landed %d, life %d)\n", landed, u.LIFEN);
        failures++;
    }

    /* A shield that swallows the blow whole: nothing landed at all, which is
       where the original floats the word rather than a number. */
    unit_init(&u, 1);
    u.LIFEU = 100;
    u.LIFEN = 80;
    u.SHIELD = 50;
    u.active = 1;
    landed = 0;
    if (formula_apply_damage(&u, 23, &absorbed, &landed) != 0
        || landed != -1 || absorbed != 23 || u.SHIELD != 27) {
        fprintf(stderr, "full shield: landed %d absorbed %d shield %d\n",
                landed, absorbed, u.SHIELD);
        failures++;
    }

    /* A shield that only takes part of it: what lands is what got through. */
    unit_init(&u, 1);
    u.LIFEU = 100;
    u.LIFEN = 80;
    u.SHIELD = 10;
    u.active = 1;
    landed = -1;
    if (formula_apply_damage(&u, 23, &absorbed, &landed) != 13
        || landed != 13 || absorbed != 10 || u.SHIELD != 0
        || u.LIFEN != 67) {
        fprintf(stderr, "part shield: landed %d absorbed %d life %d\n",
                landed, absorbed, u.LIFEN);
        failures++;
    }

    return failures;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "tests/vectors_formula.txt";
    FILE *fh = fopen(path, "r");
    if (!fh) {
        fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }

    double v[37];
    int cases = 0, failures = 0, rc;
    while ((rc = read_fields(fh, v, 37)) == 1) {
        Unit caster, target;
        memset(&caster, 0, sizeof(caster));
        memset(&target, 0, sizeof(target));

        caster.plevel = (int32_t)v[0];
        caster.STRENGTHU = v[1];
        caster.MAGICU = v[2];
        caster.SPEEDU = v[3];
        caster.FOCUSN = (int32_t)v[4];
        for (int i = 0; i < SONNY_ELEMENTS; i++)
            caster.PERU[i] = v[5];
        caster.DMG = v[6];
        caster.DMG2 = v[7];

        target.plevel = (int32_t)v[8];
        for (int i = 0; i < SONNY_ELEMENTS; i++)
            target.DEFU[i] = v[9];
        target.IDMG = v[10];
        target.IDMG2 = v[11];
        target.LIFEN = (int32_t)v[12];
        target.LIFEU = (int32_t)v[13];
        target.SHIELD = (int32_t)v[14];
        target.SSWITCH = (int32_t)v[15];
        target.FOCUSN = (int32_t)v[16];
        target.active = 1;

        AbilityCoefs a;
        a.element = (int32_t)v[17];
        a.strength_add = v[18];
        a.strength_coef = v[19];
        a.magic_add = v[20];
        a.magic_coef = v[21];
        a.speed_add = v[22];
        a.speed_coef = v[23];
        a.hit_add = v[24];
        a.hit_coef = v[25];
        a.flat_damage = v[26];
        a.damage_coef = v[27];
        a.focus_coef = v[28];

        /* Pin the roll the reference used. */
        Rng rng;
        rng_seed(&rng, 1);
        rng.KRS[0] = (int32_t)v[29];
        rng.KRSC = 0;

        int want_pierced = (int)v[30];
        int want_damage = (int)v[31];
        int want_lost = (int)v[32];
        int want_absorbed = (int)v[33];
        int want_life = (int)v[34];
        int want_shield = (int)v[35];
        int want_focus = (int)v[36];

        DamageResult d = formula_full_damage(&rng, &caster, &target, &a);
        int32_t absorbed = 0;
        int32_t lost = formula_apply_damage(&target, d.damage, &absorbed, NULL);

        cases++;
        if (d.pierced != want_pierced || d.damage != want_damage
            || lost != want_lost || absorbed != want_absorbed
            || target.LIFEN != want_life || target.SHIELD != want_shield
            || target.FOCUSN != want_focus) {
            failures++;
            if (failures <= 5)
                fprintf(stderr,
                        "case %d mismatch:\n"
                        "  pierced %d/%d damage %d/%d lost %d/%d absorbed %d/%d\n"
                        "  life %d/%d shield %d/%d focus %d/%d\n",
                        cases, d.pierced, want_pierced, d.damage, want_damage,
                        lost, want_lost, absorbed, want_absorbed,
                        target.LIFEN, want_life, target.SHIELD, want_shield,
                        target.FOCUSN, want_focus);
        }
    }
    fclose(fh);

    if (rc < 0) {
        fprintf(stderr, "malformed vector file\n");
        return 2;
    }
    if (cases == 0) {
        fprintf(stderr, "no vectors loaded\n");
        return 2;
    }
    failures += check_landed();

    printf("formula: %d/%d vectors match, what a blow comes to holds\n",
           cases - failures, cases);
    return failures ? 1 : 0;
}
