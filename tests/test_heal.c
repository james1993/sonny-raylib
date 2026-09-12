/* Differential test for executeMove's Heal and Focus branches against vectors
   from tools/ref_formula.py --heal. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/formula.h"

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

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "tests/vectors_heal.txt";
    FILE *fh = fopen(path, "r");
    if (!fh) {
        fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }

    double v[30];
    int cases = 0, failures = 0, rc;
    while ((rc = read_fields(fh, v, 30)) == 1) {
        Unit caster, target;
        memset(&caster, 0, sizeof(caster));
        memset(&target, 0, sizeof(target));

        caster.plevel = (int32_t)v[0];
        caster.STRENGTHU = v[1];
        caster.MAGICU = v[2];
        caster.SPEEDU = v[3];
        caster.FOCUSN = (int32_t)v[4];
        for (int i = 0; i < SONNY_ELEMENTS; i++) {
            caster.PERU[i] = v[5];
            target.DEFU[i] = 25;
        }
        target.LIFEN = (int32_t)v[6];
        target.LIFEU = (int32_t)v[7];
        target.FOCUSN = (int32_t)v[8];
        target.FOCUSU = (int32_t)v[9];
        target.SSWITCH = (int32_t)v[10];
        target.active = 1;

        AbilityCoefs a;
        memset(&a, 0, sizeof(a));
        a.element = (int32_t)v[11];
        a.strength_add = v[12];
        a.strength_coef = v[13];
        a.magic_add = v[14];
        a.magic_coef = v[15];
        a.speed_add = v[16];
        a.speed_coef = v[17];
        a.hit_add = v[18];
        a.hit_coef = v[19];
        a.flat_damage = v[20];
        a.damage_coef = v[21];
        a.focus_coef = v[22];

        Rng rng;
        rng_seed(&rng, 1);
        rng.KRS[0] = (int32_t)v[23];
        rng.KRSC = 0;

        int want_pierced = (int)v[24];
        int want_amount = (int)v[25];
        int want_delta = (int)v[26];
        int want_life = (int)v[27];
        int want_focus = (int)v[28];
        int want_focus_delta = (int)v[29];

        Unit heal_target = target;
        DamageResult h = formula_heal(&rng, &caster, &heal_target, &a);
        int32_t delta = formula_apply_heal(&heal_target, h.damage);

        Unit focus_target = target;
        int32_t focus_delta = formula_apply_focus(&focus_target, &a);

        cases++;
        if (h.pierced != want_pierced || h.damage != want_amount
            || delta != want_delta || heal_target.LIFEN != want_life
            || focus_target.FOCUSN != want_focus
            || focus_delta != want_focus_delta) {
            failures++;
            if (failures <= 5)
                fprintf(stderr,
                        "case %d mismatch: pierced %d/%d amount %d/%d "
                        "delta %d/%d life %d/%d focus %d/%d fdelta %d/%d\n",
                        cases, h.pierced, want_pierced, h.damage, want_amount,
                        delta, want_delta, heal_target.LIFEN, want_life,
                        focus_target.FOCUSN, want_focus, focus_delta,
                        want_focus_delta);
        }
    }
    fclose(fh);

    if (rc < 0) {
        fprintf(stderr, "malformed vector file\n");
        return 2;
    }
    printf("heal/focus: %d/%d vectors match\n", cases - failures, cases);
    return failures ? 1 : 0;
}
