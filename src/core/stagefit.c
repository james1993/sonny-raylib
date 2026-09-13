#include "stagefit.h"

StageFit stage_fit(int window_w, int window_h)
{
    StageFit f;
    f.scale = (float)window_w / SONNY_STAGE_W;
    float by_height = (float)window_h / SONNY_STAGE_H;
    if (by_height < f.scale)
        f.scale = by_height;
    f.x = (window_w - SONNY_STAGE_W * f.scale) / 2.0f;
    f.y = (window_h - SONNY_STAGE_H * f.scale) / 2.0f;
    return f;
}
