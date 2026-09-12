/* Differential test for the buff system against vectors from
   tools/ref_buffs.py (an independent transcription of the same decompiled
   source). Each case builds a unit, applies a few buffs, ticks N turns, and
   compares the resulting state. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/buffs.h"

#define MAX_LIB 64

static int close_enough(double a, double b)
{
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return diff <= 1e-6 * (scale > 1 ? scale : 1);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "tests/vectors_buffs.txt";
    FILE *fh = fopen(path, "r");
    if (!fh) {
        fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }

    BuffDef lib[MAX_LIB];
    int32_t lib_count = 0;
    int cases = 0, failures = 0;
    char line[4096];

    while (fgets(line, sizeof(line), fh)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;

        char *tok = strtok(line, " \t\n");
        if (!tok)
            continue;

        if (strcmp(tok, "BUFF") == 0) {
            BuffDef *b = &lib[lib_count++];
            memset(b, 0, sizeof(*b));
            snprintf(b->key, SONNY_NAME_LEN, "%s", strtok(NULL, " \t\n"));
            b->element = atoi(strtok(NULL, " \t\n"));
            for (int i = 0; i < 12; i++)
                b->change[i] = atof(strtok(NULL, " \t\n"));
            b->dot_flat = atof(strtok(NULL, " \t\n"));
            b->focus_drain = atof(strtok(NULL, " \t\n"));
            b->duration = atoi(strtok(NULL, " \t\n"));
            b->stun = atoi(strtok(NULL, " \t\n"));
            b->reflect = atoi(strtok(NULL, " \t\n"));
            b->shield = atoi(strtok(NULL, " \t\n"));
            b->per_flat = atof(strtok(NULL, " \t\n"));
            b->def_flat = atof(strtok(NULL, " \t\n"));
            b->per_pct = atof(strtok(NULL, " \t\n"));
            b->def_pct = atof(strtok(NULL, " \t\n"));
            b->sswitch = atoi(strtok(NULL, " \t\n"));
            b->dot_strength = atof(strtok(NULL, " \t\n"));
            b->dot_magic = atof(strtok(NULL, " \t\n"));
            b->dot_speed = atof(strtok(NULL, " \t\n"));
            b->filter = atoi(strtok(NULL, " \t\n"));
            continue;
        }
        if (strcmp(tok, "CASE") != 0)
            continue;

        int32_t plevel = atoi(strtok(NULL, " \t\n"));
        double life = atof(strtok(NULL, " \t\n"));
        double strength = atof(strtok(NULL, " \t\n"));
        double magic = atof(strtok(NULL, " \t\n"));
        double speed = atof(strtok(NULL, " \t\n"));
        double per = atof(strtok(NULL, " \t\n"));
        double def = atof(strtok(NULL, " \t\n"));
        int32_t focus = atoi(strtok(NULL, " \t\n"));
        int32_t nbuffs = atoi(strtok(NULL, " \t\n"));

        char keys[8][SONNY_NAME_LEN];
        for (int32_t i = 0; i < nbuffs; i++)
            snprintf(keys[i], SONNY_NAME_LEN, "%s", strtok(NULL, " \t\n"));
        int32_t ticks = atoi(strtok(NULL, " \t\n"));
        strtok(NULL, " \t\n");   /* EXPECT */

        Unit u;
        unit_init(&u, 1);
        u.active = 1;
        u.plevel = plevel;
        u.LIFE = life;
        u.STRENGTH = strength;
        u.MAGIC = magic;
        u.SPEED = speed;
        for (int e = 0; e < SONNY_ELEMENTS; e++) {
            u.PER[e] = per;
            u.DEF[e] = def;
            u.PERU[e] = per;
            u.DEFU[e] = def;
        }
        u.LIFEU = u.LIFEN = (int32_t)floor(life + 0.5);
        u.FOCUSU = u.FOCUSN = focus;
        u.STRENGTHU = strength;
        u.MAGICU = magic;
        u.SPEEDU = speed;

        Unit caster;
        unit_init(&caster, 2);
        caster.STRENGTHU = 100;
        caster.MAGICU = 50;
        caster.SPEEDU = 30;

        unit_apply_changes(&u);
        for (int32_t i = 0; i < nbuffs; i++) {
            const BuffDef *b = buff_find(lib, lib_count, keys[i]);
            if (!b) {
                fprintf(stderr, "unknown buff %s\n", keys[i]);
                return 2;
            }
            buff_apply(&u, b, 1, &caster, 0);
        }
        unit_apply_changes(&u);

        TickResult last;
        memset(&last, 0, sizeof(last));
        for (int32_t t = 0; t < ticks; t++)
            last = buff_tick(&u, lib, lib_count);

        int32_t w_lifen = atoi(strtok(NULL, " \t\n"));
        int32_t w_lifeu = atoi(strtok(NULL, " \t\n"));
        int32_t w_focusn = atoi(strtok(NULL, " \t\n"));
        double w_str = atof(strtok(NULL, " \t\n"));
        double w_mag = atof(strtok(NULL, " \t\n"));
        double w_spd = atof(strtok(NULL, " \t\n"));
        double w_peru = atof(strtok(NULL, " \t\n"));
        double w_defu = atof(strtok(NULL, " \t\n"));
        int32_t w_shield = atoi(strtok(NULL, " \t\n"));
        int32_t w_shieldc = atoi(strtok(NULL, " \t\n"));
        int32_t w_stun = atoi(strtok(NULL, " \t\n"));
        int32_t w_sswitch = atoi(strtok(NULL, " \t\n"));
        int32_t w_active = atoi(strtok(NULL, " \t\n"));
        double w_lastdmg = atof(strtok(NULL, " \t\n"));

        cases++;
        int bad = u.LIFEN != w_lifen || u.LIFEU != w_lifeu
               || u.FOCUSN != w_focusn
               || !close_enough(u.STRENGTHU, w_str)
               || !close_enough(u.MAGICU, w_mag)
               || !close_enough(u.SPEEDU, w_spd)
               || !close_enough(u.PERU[0], w_peru)
               || !close_enough(u.DEFU[0], w_defu)
               || u.SHIELD != w_shield || u.SHIELDCOUNTER != w_shieldc
               || u.STUN != w_stun || u.SSWITCH != w_sswitch
               || u.active != w_active
               || !close_enough(last.total_damage, w_lastdmg);
        if (bad) {
            failures++;
            if (failures <= 5)
                fprintf(stderr,
                        "case %d mismatch:\n"
                        "  life %d/%d  max %d/%d  focus %d/%d\n"
                        "  str %g/%g mag %g/%g spd %g/%g\n"
                        "  peru %.10g/%.10g defu %.10g/%.10g\n"
                        "  shield %d/%d counter %d/%d stun %d/%d "
                        "sswitch %d/%d active %d/%d lastdmg %g/%g\n",
                        cases, u.LIFEN, w_lifen, u.LIFEU, w_lifeu,
                        u.FOCUSN, w_focusn, u.STRENGTHU, w_str, u.MAGICU,
                        w_mag, u.SPEEDU, w_spd, u.PERU[0], w_peru,
                        u.DEFU[0], w_defu, u.SHIELD, w_shield,
                        u.SHIELDCOUNTER, w_shieldc, u.STUN, w_stun,
                        u.SSWITCH, w_sswitch, u.active, w_active,
                        last.total_damage, w_lastdmg);
        }
    }
    fclose(fh);

    if (cases == 0) {
        fprintf(stderr, "no cases loaded\n");
        return 2;
    }
    printf("buffs: %d/%d cases match\n", cases - failures, cases);
    return failures ? 1 : 0;
}
