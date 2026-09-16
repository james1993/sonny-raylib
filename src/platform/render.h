/* How finely the stage is being drawn.
 *
 * Every coordinate in the game is in the original's 800 by 575 stage, and the
 * frame is drawn into a texture that is laid down in the window afterwards.
 * That texture used to be 800 by 575 too, which made the stage's pixel the
 * unit of resolution: a window twice the size got each of those pixels
 * smoothed across four of its own, and the original -- which is vector art
 * rasterised at whatever size Flash is given -- looked sharp beside it.
 *
 * So the texture is the size the stage occupies in the window instead, and
 * the projection maps the stage's 800 by 575 onto the whole of it. Drawing
 * code goes on working in stage coordinates and needs to know nothing about
 * it. Two things do: anything rasterised at a size rather than scaled to one
 * (the text bakes), and anything wanting to sit on a whole device pixel.
 */
#ifndef SONNY_RENDER_H
#define SONNY_RENDER_H

#include "raylib.h"

/* Device pixels per stage unit in the target being drawn into. */
float render_scale(void);
void render_set_scale(float scale);

/* `v`, a stage coordinate, put on the device pixel grid. */
float render_snap(float v);

/* Map the stage's own 800 by 575 onto the whole of the render target that has
   just been opened, whatever its pixel size. BeginTextureMode sets up a
   projection in the target's pixels, so this goes immediately after it --
   and only on the projection, leaving the modelview alone for the cameras
   the battle screen leans in with. */
void render_stage_projection(void);

/* A scissor box given in stage coordinates. The scissor is the one thing
   raylib sets in the framebuffer's own pixels rather than through the
   projection, so it is the one thing that has to be scaled by hand. */
void render_scissor(Rectangle box);

#endif
