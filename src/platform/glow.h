/* The glow filters the battle screen hangs on a unit's model.
 *
 * When a blow or a heal lands, the original puts a stack of three
 * flash.filters.GlowFilter on the target's model for two frames at a time,
 * four times over fifteen:
 *
 *   KFHit1  white    blur 100, strength 10, inner
 *   KFHit2  0xFFCC00 blur  10, strength  1, outer
 *   KFHit3  0xFFCC00 blur  10, strength  1, inner
 *
 * The first of those is what makes the figure read as a white cut-out. An
 * inner glow's alpha at a point is the blurred *inverse* of the shape's alpha
 * there, times the strength, clamped -- and a hundred-pixel blur over a figure
 * sixty wide is mostly outside it, so even before the strength of ten the
 * value is past one everywhere inside. It saturates, so nothing of the model's
 * own colour survives and the silhouette can simply be filled.
 *
 * The other two are ordinary five-pixel edges at strength one: a rim just
 * inside the outline and a halo just outside it. Those do need the blurred
 * silhouette, which is what the render targets and the two blur passes here
 * are for. Heal is the same with 0x99FF00 in place of the orange.
 *
 * The work is done before the frame's own drawing starts, because ending a
 * render target in raylib returns to the window rather than to whatever target
 * was current -- so this cannot run inside the pass that draws the stage.
 */
#ifndef SONNY_GLOW_H
#define SONNY_GLOW_H

#include "raylib.h"

/* Give the silhouette to be glowed: draw into it between these two, in stage
   coordinates, exactly where it belongs on screen. Returns 0 when the
   hardware would not give us the targets, in which case nothing is drawn and
   the caller should fall back to drawing the model plainly. */
int glow_capture_begin(void);
void glow_capture_end(void);

/* Blur the captured silhouette, across and then down. `width` is the filter's
   blurX in stage pixels -- 10 in the original, scaled by whatever the
   battlefield is scaled to, because Flash scales a filter with the clip it is
   on. It is a width, not a reach: the kernel runs half of it either side.
   This opens render targets of its own, so it belongs with the capture,
   before the pass that draws the stage. */
void glow_blur(float width);

/* Lay it down, in the order the filters stack: the halo outside, the white
   fill, then the rim inside. This draws into whatever target is current, so
   it belongs in the frame's own drawing. */
void glow_compose(Color rim);

void glow_unload(void);

#endif
