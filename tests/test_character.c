/* Differential test for the character stat pipeline and reward formulas
   against vectors from tools/ref_character.py. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/core/character.h"

static int close_enough(double a, double b)
{
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    return diff <= 1e-9 * (scale > 1 ? scale : 1);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "tests/vectors_character.txt";
    FILE *fh = fopen(path, "r");
    if (!fh) {
        fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }

    int chars = 0, xps = 0, ratings = 0, failures = 0;
    char line[2048];

    while (fgets(line, sizeof(line), fh)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        char *tok = strtok(line, " \t\n");
        if (!tok)
            continue;

        if (strcmp(tok, "CHAR") == 0) {
            Character c;
            memset(&c, 0, sizeof(c));
            int32_t cls = atoi(strtok(NULL, " \t\n"));
            c.class_template = unit_template_by_id(cls);
            if (!c.class_template) {
                fprintf(stderr, "unknown class id %d\n", cls);
                return 2;
            }
            c.level = atoi(strtok(NULL, " \t\n"));
            for (int i = 0; i < SONNY_STATS; i++)
                c.spent[i] = atof(strtok(NULL, " \t\n"));
            for (int i = 0; i < SONNY_EQUIP_SLOTS; i++)
                c.equip[i] = atoi(strtok(NULL, " \t\n"));
            /* The vectors describe a character wearing this gear, and the
               game only ever counts a piece that has passed through a slot,
               so the running total is worked out as if each had. */
            character_rebuild_sets(&c);
            strtok(NULL, " \t\n");   /* EXPECT */

            double w_life = atof(strtok(NULL, " \t\n"));
            double w_str = atof(strtok(NULL, " \t\n"));
            double w_mag = atof(strtok(NULL, " \t\n"));
            double w_spd = atof(strtok(NULL, " \t\n"));
            double w_foc = atof(strtok(NULL, " \t\n"));
            double w_per = atof(strtok(NULL, " \t\n"));
            double w_def = atof(strtok(NULL, " \t\n"));

            DerivedStats d = character_derive(&c);
            chars++;
            if (!close_enough(d.life, w_life) || !close_enough(d.strength, w_str)
                || !close_enough(d.magic, w_mag) || !close_enough(d.speed, w_spd)
                || !close_enough(d.focus, w_foc)
                || !close_enough(d.per[0], w_per)
                || !close_enough(d.def[0], w_def)) {
                failures++;
                if (failures <= 5)
                    fprintf(stderr, "CHAR class %d lv%d: life %g/%g str %g/%g "
                            "mag %g/%g spd %g/%g foc %g/%g per %g/%g def %g/%g\n",
                            cls, c.level, d.life, w_life, d.strength, w_str,
                            d.magic, w_mag, d.speed, w_spd, d.focus, w_foc,
                            d.per[0], w_per, d.def[0], w_def);
            }
        } else if (strcmp(tok, "XP") == 0) {
            double rating = atof(strtok(NULL, " \t\n"));
            int32_t level = atoi(strtok(NULL, " \t\n"));
            strtok(NULL, " \t\n");
            double want = atof(strtok(NULL, " \t\n"));
            double got = character_xp_gain(rating, level);
            xps++;
            if (!close_enough(got, want)) {
                failures++;
                if (failures <= 5)
                    fprintf(stderr, "XP rating %g level %d: %.10g / %.10g\n",
                            rating, level, got, want);
            }
        } else if (strcmp(tok, "RATING") == 0) {
            int32_t count = atoi(strtok(NULL, " \t\n"));
            int32_t levels[8];
            for (int32_t i = 0; i < count; i++)
                levels[i] = atoi(strtok(NULL, " \t\n"));
            strtok(NULL, " \t\n");
            double want = atof(strtok(NULL, " \t\n"));
            double got = rewards_enemy_rating(levels, count);
            ratings++;
            if (!close_enough(got, want)) {
                failures++;
                if (failures <= 5)
                    fprintf(stderr, "RATING: %.10g / %.10g\n", got, want);
            }
        }
    }
    fclose(fh);

    /* A battle grants at most one level, and the overflow is discarded. */
    Character c;
    memset(&c, 0, sizeof(c));
    c.class_template = unit_template_by_id(1);
    c.level = 1;
    c.xp = 0;
    if (character_award_xp(&c, 40) != 0 || c.level != 1 || c.xp != 40) {
        fprintf(stderr, "partial XP should not level\n");
        failures++;
    }
    if (character_award_xp(&c, 500) != 1 || c.level != 2 || c.xp != 0) {
        fprintf(stderr, "a huge award should grant exactly one level and "
                        "discard the remainder\n");
        failures++;
    }

    /* Points track level, as the respec path sets them. */
    c.level = 12;
    if (character_stat_points(&c) != 11 || character_skill_points(&c) != 11) {
        fprintf(stderr, "point totals should be level - 1\n");
        failures++;
    }

    printf("character: %d stat sets, %d xp curves, %d ratings checked, "
           "%d failures\n", chars, xps, ratings, failures);
    return failures ? 1 : 0;
}
