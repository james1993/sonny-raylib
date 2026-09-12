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

/* The texture for a still, or frame `frame` (1-based) of an animation.
   Returns NULL when the name is unknown or the file will not load. */
const Texture2D *asset_texture(const char *name, int32_t frame);
int32_t asset_frame_count(const char *name);

/* Draw an asset centred in `area`, scaled down to fit if needed. Returns 0 if
   there was nothing to draw. */
int asset_draw_fit(const char *name, int32_t frame, Rectangle area, Color tint);
/* Draw covering `area`, cropping the overflow -- for backdrops. */
int asset_draw_cover(const char *name, int32_t frame, Rectangle area,
                     Color tint);

void assets_unload_all(void);
int32_t assets_loaded_count(void);

#endif
