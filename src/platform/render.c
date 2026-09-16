#include "render.h"

#include <math.h>

#include "rlgl.h"
#include "../core/stagefit.h"

static float scale = 1.0f;

float render_scale(void)
{
    return scale;
}

void render_set_scale(float s)
{
    scale = (s > 0.0f) ? s : 1.0f;
}

float render_snap(float v)
{
    return floorf(v * scale + 0.5f) / scale;
}

void render_stage_projection(void)
{
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0.0, SONNY_STAGE_W, SONNY_STAGE_H, 0.0, 0.0, 1.0);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
}

void render_scissor(Rectangle box)
{
    /* Truncated, each on its own, which is what the box came to when the
       stage was its own texture and a stage unit was a pixel. */
    BeginScissorMode((int)(box.x * scale), (int)(box.y * scale),
                     (int)(box.width * scale), (int)(box.height * scale));
}
