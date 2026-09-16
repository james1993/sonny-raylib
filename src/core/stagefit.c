#include "stagefit.h"

StageFit stage_fit(int window_w, int window_h)
{
    StageFit f;
    f.scale = (float)window_w / SONNY_STAGE_W;
    float by_height = (float)window_h / SONNY_STAGE_H;
    if (by_height < f.scale)
        f.scale = by_height;
    /* Whole pixels: the stage is drawn into a texture of exactly this size
       and laid down one texel to one pixel, and a blit at half a pixel
       samples two texels for every one of them -- which is the whole of what
       this is for. */
    f.w = (int)(SONNY_STAGE_W * f.scale + 0.5f);
    f.h = (int)(SONNY_STAGE_H * f.scale + 0.5f);
    if (f.w < 1)
        f.w = 1;
    if (f.h < 1)
        f.h = 1;
    f.x = (float)((window_w - f.w) / 2);
    f.y = (float)((window_h - f.h) / 2);
    return f;
}
