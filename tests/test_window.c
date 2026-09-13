/* The stage has to survive being fitted into any window the player gives it:
 * what is drawn and where the pointer thinks it is go through the same fit,
 * so a click has to land where it looks like it landed at every size. */
#include <math.h>
#include <stdio.h>
#include "../src/core/stagefit.h"

/* The inverse of the fit, which is what the pointer goes through. */
static void unmap(StageFit f, float mx, float my, float *sx, float *sy)
{
    *sx = (mx - f.x) / f.scale;
    *sy = (my - f.y) / f.scale;
}

int main(void)
{
    static const int WINDOWS[][2] = {
        {800, 575},        /* the stage itself */
        {1600, 1150},      /* two up */
        {2400, 1725},      /* three up */
        {1920, 1080},      /* wider than the stage: black down the sides */
        {1000, 900},       /* taller: black above and below */
        {1280, 600}, {640, 460}, {3840, 2160}, {801, 576},
    };
    int checked = 0, bad = 0;
    for (unsigned w = 0; w < sizeof(WINDOWS) / sizeof(WINDOWS[0]); w++) {
        int ww = WINDOWS[w][0], wh = WINDOWS[w][1];
        StageFit f = stage_fit(ww, wh);

        /* The whole stage has to be inside the window, and touching an edge. */
        float used_w = SONNY_STAGE_W * f.scale, used_h = SONNY_STAGE_H * f.scale;
        if (used_w > ww + 0.01f || used_h > wh + 0.01f) {
            printf("%dx%d: the stage does not fit\n", ww, wh);
            bad++;
        }
        if (f.x > 0.01f && f.y > 0.01f) {
            printf("%dx%d: letterboxed on both axes at once\n", ww, wh);
            bad++;
        }
        /* The shape is kept: one scale, both axes. */
        for (int sy = 0; sy <= SONNY_STAGE_H; sy += 23) {
            for (int sx = 0; sx <= SONNY_STAGE_W; sx += 29) {
                float back_x, back_y;
                unmap(f, f.x + sx * f.scale, f.y + sy * f.scale,
                      &back_x, &back_y);
                checked++;
                if (fabsf(back_x - sx) > 0.01f || fabsf(back_y - sy) > 0.01f) {
                    if (bad < 5)
                        printf("%dx%d: (%d,%d) came back as (%.2f,%.2f)\n",
                               ww, wh, sx, sy, back_x, back_y);
                    bad++;
                }
            }
        }
    }
    printf("window: %d points over %d sizes return to where they started, "
           "%d failures\n", checked,
           (int)(sizeof(WINDOWS) / sizeof(WINDOWS[0])), bad);
    return bad != 0;
}
