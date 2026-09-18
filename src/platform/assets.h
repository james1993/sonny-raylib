/* Texture cache over the generated asset manifest.
 *
 * Assets are looked up by the name the engine itself uses -- an ability's icon
 * string, a battle's ZoneBG, a buff key, a doll part -- so drawing code stays
 * written in the original's own terms. Textures load on first use and stay
 * cached; a name with no art resolves to NULL and callers draw a placeholder,
 * which is the honest behaviour for the handful of names the original has no
 * art for either.
 */
#ifndef SONNY_ASSETS_H
#define SONNY_ASSETS_H

#include "raylib.h"
#include "../gen/assets_gen.h"

/* Where the asset files live, relative to the working directory. Set once at
   startup; defaults to "." so running from the repository root just works. */
void assets_set_root(const char *root);

/* Shout if the art on the disk is not the art this build was made against.
 *
 * The manifest is compiled in and the pictures are read off the disk, so the
 * two drift apart the moment one is updated without the other -- and a
 * picture does not say what it is: art rasterised at two pixels to the stage
 * unit, drawn by a build that believes in one, comes out at exactly twice its
 * size, everywhere, without a word. Both are written by the same run of
 * tools/gen_asset_manifest.py, so a stamp each is enough to tell. */
void assets_check_stamp(void);

/* The texture for a still, or frame `frame` (1-based) of an animation.
   Returns NULL when the name is unknown or the file will not load. */
const Texture2D *asset_texture(const char *name, int32_t frame);
/* Where a frame's own origin sits inside its exported image, in stage units. */
Vector2 asset_frame_offset(const char *name, int32_t frame);
int32_t asset_frame_count(const char *name);

/* One frame of art, ready to draw.
 *
 * An image is not its size: the art is rasterised above 1:1 so that the game
 * is not limited to the stage's own resolution, and how far above is per
 * asset -- a piece the decompiler wrote a filter into is kept at 1:1 because
 * no other rasteriser reproduces those the way its own renderer does. So
 * `size` is how big the picture is in the stage's units, which is what every
 * coordinate in the game is in, and the texture's pixels are only ever the
 * source rectangle. */
typedef struct {
    const Texture2D *texture;
    Vector2          size;      /* in stage units */
    Vector2          offset;    /* the art's own origin inside it, in units */
    Rectangle        source;    /* the whole image, in its own pixels */
} Art;

/* Returns 0 with everything zeroed when there is nothing to draw. */
int asset_art(const char *name, int32_t frame, Art *out);

/* Draw an asset centred in `area`, scaled down to fit if needed. Returns 0 if
   there was nothing to draw. */
int asset_draw_fit(const char *name, int32_t frame, Rectangle area, Color tint);
/* Draw covering `area`, cropping the overflow. */
int asset_draw_cover(const char *name, int32_t frame, Rectangle area,
                     Color tint);

/* Draw art where the original places it: centred on its recorded bounds,
   offset from `parent` (the stage position of whatever contains it). Assets
   with no bounds fall back to being centred on `parent`. */
/* The same image with its own colour taken out, for a piece the game recolours
   with Color.setRGB: drawn with a tint, it lands on exactly that colour. */
const Texture2D *asset_texture_recolored(const char *name, int32_t frame);

int asset_draw_placed(const char *name, int32_t frame, Vector2 parent,
                      float scale, Color tint);

void assets_unload_all(void);
int32_t assets_loaded_count(void);

/* ------------------------------------------------------------------- doll */

/* Everything needed to dress a character, in the original's own terms:
 * `gender` is the model's M/F, `skin` and `hair` come from its model array,
 * and `looks` holds the appearance string of whatever is in each of the seven
 * equipment slots (empty for an empty slot).
 *
 * Each part draws two layers, as the battle screen attaches them: the skin
 * (<gender>_S<part>_<skin>) underneath, then the equipped item
 * (<gender>_<part>_<look>) over it. Weapons always use the M_ form. Hair goes
 * on the head only when nothing is equipped there, or the model is female.
 */
typedef struct {
    const char *gender;
    const char *skin;
    const char *hair;
    const char *looks[7];
    /* The colour the model was last told to cast in -- `colortobe` in the
       original, set on the model before it is told to play. One layer of each
       effect clip is recoloured to it outright; the other keeps its own
       colour. An alpha of zero means nothing has been cast, and the layer is
       left as it was drawn. */
    Color       cast;
} DollSpec;

/* Draw one frame of the model. `frame` is 1-based into the model's timeline;
   `scale` and `flip` place it, with flip mirroring for the right-hand team.
   Returns the number of part layers actually drawn. */
/* How opaque the model is on this frame, as its own timeline sets it: 1
   everywhere but the last eleven frames of the death, which fade it out. */
float doll_frame_alpha(int32_t frame);
int doll_draw(const DollSpec *spec, int32_t frame, Vector2 origin, float scale,
              int flip, Color tint);

/* Resolve an animation name to a frame in its range, given a tick counter.
   Non-looping states hold on their last frame. */
int32_t doll_animation_frame(const char *animation, int32_t tick, int loop);

#endif
