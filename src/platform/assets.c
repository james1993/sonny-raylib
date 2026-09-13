#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "rlgl.h"

#define ASSET_CACHE_MAX 512

typedef struct {
    const char *name;
    int32_t     frame;
    Texture2D   texture;
    int32_t     ok;
} CachedTexture;

static CachedTexture cache[ASSET_CACHE_MAX];
static int32_t cache_count;
static char asset_root[512] = ".";

void assets_set_root(const char *root)
{
    snprintf(asset_root, sizeof(asset_root), "%s", root ? root : ".");
}

int32_t assets_loaded_count(void)
{
    int32_t n = 0;
    for (int32_t i = 0; i < cache_count; i++)
        if (cache[i].ok)
            n++;
    return n;
}

int32_t asset_frame_count(const char *name)
{
    const AssetEntry *entry = asset_find(name);
    return entry ? entry->frame_count : 0;
}

const Texture2D *asset_texture(const char *name, int32_t frame)
{
    const AssetEntry *entry = asset_find(name);
    if (!entry || entry->frame_count == 0)
        return NULL;

    if (frame < 1)
        frame = 1;
    if (frame > entry->frame_count)
        frame = ((frame - 1) % entry->frame_count) + 1;

    for (int32_t i = 0; i < cache_count; i++) {
        if (cache[i].name == entry->name && cache[i].frame == frame)
            return cache[i].ok ? &cache[i].texture : NULL;
    }
    if (cache_count >= ASSET_CACHE_MAX)
        return NULL;

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", asset_root, entry->frames[frame - 1]);

    CachedTexture *slot = &cache[cache_count++];
    slot->name = entry->name;
    slot->frame = frame;
    slot->ok = 0;
    if (FileExists(path)) {
        slot->texture = LoadTexture(path);
        if (slot->texture.id != 0) {
            SetTextureFilter(slot->texture, TEXTURE_FILTER_BILINEAR);
            slot->ok = 1;
        }
    }
    return slot->ok ? &slot->texture : NULL;
}

/* Where a frame's own origin sits inside its exported image. */
Vector2 asset_frame_offset(const char *name, int32_t frame)
{
    const AssetEntry *entry = asset_find(name);
    if (!entry || !entry->offsets || entry->frame_count == 0)
        return (Vector2){0, 0};
    if (frame < 1)
        frame = 1;
    if (frame > entry->frame_count)
        frame = ((frame - 1) % entry->frame_count) + 1;
    return (Vector2){entry->offsets[frame - 1].x, entry->offsets[frame - 1].y};
}

static int draw_scaled(const char *name, int32_t frame, Rectangle area,
                       Color tint, int cover)
{
    const Texture2D *tex = asset_texture(name, frame);
    if (!tex)
        return 0;

    float sx = area.width / tex->width;
    float sy = area.height / tex->height;
    /* Fit stays inside the box; cover fills it and crops. */
    float scale = cover ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
    if (!cover && scale > 1.0f)
        scale = 1.0f;

    float w = tex->width * scale;
    float h = tex->height * scale;
    Rectangle dest = {area.x + (area.width - w) / 2,
                      area.y + (area.height - h) / 2, w, h};
    Rectangle src = {0, 0, (float)tex->width, (float)tex->height};

    if (cover) {
        /* Crop the source instead of spilling outside the box. */
        float visible_w = area.width / scale;
        float visible_h = area.height / scale;
        src.x = (tex->width - visible_w) / 2;
        src.y = (tex->height - visible_h) / 2;
        src.width = visible_w;
        src.height = visible_h;
        dest = area;
    }
    DrawTexturePro(*tex, src, dest, (Vector2){0, 0}, 0.0f, tint);
    return 1;
}

int asset_draw_fit(const char *name, int32_t frame, Rectangle area, Color tint)
{
    return draw_scaled(name, frame, area, tint, 0);
}

int asset_draw_cover(const char *name, int32_t frame, Rectangle area,
                     Color tint)
{
    return draw_scaled(name, frame, area, tint, 1);
}

int asset_draw_placed(const char *name, int32_t frame, Vector2 parent,
                      float scale, Color tint)
{
    const Texture2D *tex = asset_texture(name, frame);
    if (!tex)
        return 0;
    /* The frame's recorded offset says where its own origin sits inside the
       image, so this puts that origin exactly on `parent`. */
    Vector2 offset = asset_frame_offset(name, frame);

    float w = tex->width * scale;
    float h = tex->height * scale;
    Rectangle src = {0, 0, (float)tex->width, (float)tex->height};
    Rectangle dest = {parent.x - offset.x * scale,
                      parent.y - offset.y * scale, w, h};
    DrawTexturePro(*tex, src, dest, (Vector2){0, 0}, 0.0f, tint);
    return 1;
}

void assets_unload_all(void)
{
    for (int32_t i = 0; i < cache_count; i++)
        if (cache[i].ok)
            UnloadTexture(cache[i].texture);
    cache_count = 0;
}

/* ----------------------------------------------------------------- doll */

int32_t doll_animation_frame(const char *animation, int32_t tick, int loop)
{
    const AssetAnimation *a = asset_animation(animation);
    if (!a || a->length <= 0)
        return 1;
    if (loop)
        return a->start + (tick % a->length);
    int32_t offset = tick < a->length ? tick : a->length - 1;
    return a->start + offset;
}

/* Draw one texture under a SWF matrix, so skew and rotation survive rather
   than being approximated by a rotation angle. */
static void draw_with_matrix(const Texture2D *tex, const float m[6],
                             Vector2 origin, float scale, int flip,
                             Vector2 offset, Color tint)
{
    float sign = flip ? -1.0f : 1.0f;

    rlPushMatrix();
    rlTranslatef(origin.x, origin.y, 0.0f);
    rlScalef(scale * sign, scale, 1.0f);

    /* SWF: x' = a*x + c*y + tx, y' = b*x + d*y + ty. Column-major 4x4. */
    float mat[16] = {
        m[0], m[1], 0.0f, 0.0f,
        m[2], m[3], 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        m[4], m[5], 0.0f, 1.0f,
    };
    rlMultMatrixf(mat);

    /* The exported image is trimmed and padded, and its SVG sibling records
       where the art's own origin sits inside it. Putting the top-left at
       -offset lines the two coordinate systems back up exactly. */
    Rectangle src = {0, 0, (float)tex->width, (float)tex->height};
    Rectangle dest = {-offset.x, -offset.y,
                      (float)tex->width, (float)tex->height};
    DrawTexturePro(*tex, src, dest, (Vector2){0, 0}, 0.0f, tint);
    rlPopMatrix();
}

static int draw_layer(const char *name, const float m[6], Vector2 origin,
                      float scale, int flip, Color tint)
{
    const Texture2D *tex = asset_texture(name, 1);
    if (!tex)
        return 0;
    draw_with_matrix(tex, m, origin, scale, flip, asset_frame_offset(name, 1), tint);
    return 1;
}

int doll_draw(const DollSpec *spec, int32_t frame, Vector2 origin, float scale,
              int flip, Color tint)
{
    if (!spec || SONNY_DOLL_FRAME_COUNT == 0)
        return 0;

    if (frame < 1)
        frame = 1;
    if (frame > SONNY_DOLL_FRAME_COUNT)
        frame = SONNY_DOLL_FRAME_COUNT;

    const DollFrame *f = &SONNY_DOLL_FRAMES[frame - 1];
    const char *gender = (spec->gender && spec->gender[0]) ? spec->gender : "M";
    int drawn = 0;
    char name[128];

    for (int32_t i = 0; i < f->count; i++) {
        const DollPlacement *p = &f->parts[i];
        const DollPart *part = doll_part(p->part);
        if (!part)
            continue;   /* "shadower" and anything else not a dressed part */

        const float m[6] = {p->a, p->b, p->c, p->d, p->tx, p->ty};

        /* Skin underneath. */
        if (spec->skin && spec->skin[0]) {
            snprintf(name, sizeof(name), "%s_S%s_%s", gender, part->art,
                     spec->skin);
            drawn += draw_layer(name, m, origin, scale, flip, tint);
        }

        /* Then whatever is equipped in the slot this part belongs to. */
        const char *look = (part->core >= 0 && part->core < 7)
                         ? spec->looks[part->core] : NULL;
        if (look && look[0]) {
            /* Weapons are attached with the M_ prefix whatever the gender. */
            int weapon = strcmp(part->art, "WEAPON") == 0;
            snprintf(name, sizeof(name), "%s_%s_%s", weapon ? "M" : gender,
                     part->art, look);
            drawn += draw_layer(name, m, origin, scale, flip, tint);
        }

        /* Hair sits on the head when the head slot is empty, or for a female
           model, as the battle screen does. */
        if (strcmp(p->part, "head") == 0 && spec->hair && spec->hair[0]
            && (!spec->looks[0] || !spec->looks[0][0]
                || strcmp(gender, "F") == 0)) {
            snprintf(name, sizeof(name), "HAIR_%s", spec->hair);
            drawn += draw_layer(name, m, origin, scale, flip, tint);
        }
    }
    return drawn;
}
