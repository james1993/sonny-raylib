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
    int64_t     used;          /* when this was last asked for */
} CachedTexture;

static CachedTexture cache[ASSET_CACHE_MAX];
static int32_t cache_count;
static int64_t cache_clock;
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
        if (cache[i].name == entry->name && cache[i].frame == frame) {
            cache[i].used = ++cache_clock;
            return cache[i].ok ? &cache[i].texture : NULL;
        }
    }

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", asset_root, entry->frames[frame - 1]);

    /* A comic is a thousand pictures of its own, so a cache that only fills
       up would leave the game with nothing to draw once one has played. The
       one that has gone longest without being asked for makes way. Asking
       for a texture puts it at the head of that order, so a pointer handed
       back by this function is never what the next call throws out -- which
       is what lets a caller hold one while it fetches another. */
    CachedTexture *slot;
    if (cache_count < ASSET_CACHE_MAX) {
        slot = &cache[cache_count++];
    } else {
        slot = &cache[0];
        for (int32_t i = 1; i < cache_count; i++)
            if (cache[i].used < slot->used)
                slot = &cache[i];
        if (slot->ok)
            UnloadTexture(slot->texture);
    }
    slot->used = ++cache_clock;
    slot->name = entry->name;
    slot->frame = frame;
    slot->ok = 0;
    if (FileExists(path)) {
        slot->texture = LoadTexture(path);
        if (slot->texture.id != 0) {
            /* Some art is drawn a long way below its exported size -- the
               quit button is a 60px square shown at 18 -- and sampling four
               texels of it picks out whichever ones happen to land under the
               pointer, which turns a dark glossy square into a flat bright
               one. Mipmaps give those draws a properly reduced copy to read;
               at or near full size trilinear still reads the full one. */
            GenTextureMipmaps(&slot->texture);
            SetTextureFilter(slot->texture, TEXTURE_FILTER_TRILINEAR);
            slot->ok = 1;
        }
    }
    return slot->ok ? &slot->texture : NULL;
}

/* A copy of an exported image with its colour taken out, so drawing it with a
   tint lands on exactly that colour.
 *
 * ActionScript's Color.setRGB replaces a clip's colour outright -- it zeroes
 * the multiplier and puts the value in the offset -- where a raylib tint
 * multiplies. The two agree only when the art is white, and the pieces the
 * game recolours this way are not: the disc behind a buff's icon is authored
 * flat green, and multiplying green by a red element gives black. */
#define WHITE_CACHE_MAX 16

static struct {
    const char *name;
    Texture2D   texture;
    int32_t     ok;
} white_cache[WHITE_CACHE_MAX];
static int32_t white_count;

const Texture2D *asset_texture_recolored(const char *name, int32_t frame)
{
    const AssetEntry *entry = asset_find(name);
    if (!entry)
        return NULL;
    for (int32_t i = 0; i < white_count; i++)
        if (white_cache[i].name == entry->name)
            return white_cache[i].ok ? &white_cache[i].texture : NULL;
    if (white_count >= WHITE_CACHE_MAX)
        return asset_texture(name, frame);

    const Texture2D *source = asset_texture(name, frame);
    if (!source)
        return NULL;
    Image image = LoadImageFromTexture(*source);
    Color *pixels = LoadImageColors(image);
    for (int i = 0; i < image.width * image.height; i++) {
        pixels[i].r = 255;
        pixels[i].g = 255;
        pixels[i].b = 255;
    }
    Image plain = {pixels, image.width, image.height, 1,
                   PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    int32_t slot = white_count++;
    white_cache[slot].name = entry->name;
    white_cache[slot].texture = LoadTextureFromImage(plain);
    white_cache[slot].ok = white_cache[slot].texture.id != 0;
    if (white_cache[slot].ok)
        SetTextureFilter(white_cache[slot].texture, TEXTURE_FILTER_BILINEAR);
    UnloadImageColors(pixels);
    UnloadImage(image);
    return white_cache[slot].ok ? &white_cache[slot].texture : NULL;
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
    for (int32_t i = 0; i < white_count; i++)
        if (white_cache[i].ok)
            UnloadTexture(white_cache[i].texture);
    white_count = 0;
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

/* Two SWF matrices, outer applied to inner: the model places the effect clip,
   and the clip places each of its own layers inside that. */
static void compose(const float outer[6], const float inner[6], float out[6])
{
    out[0] = outer[0] * inner[0] + outer[2] * inner[1];
    out[1] = outer[1] * inner[0] + outer[3] * inner[1];
    out[2] = outer[0] * inner[2] + outer[2] * inner[3];
    out[3] = outer[1] * inner[2] + outer[3] * inner[3];
    out[4] = outer[0] * inner[4] + outer[2] * inner[5] + outer[4];
    out[5] = outer[1] * inner[4] + outer[3] * inner[5] + outer[5];
}

/* The effect the model plays over itself on this frame, drawn through the
   unnamed slot the model keeps for it. The clip runs its own timeline
   alongside the animation that started it, so its frame is how far into that
   animation the model has got. */
static int draw_cast(const DollSpec *spec, const DollPlacement *p,
                     int32_t frame, Vector2 origin, float scale, int flip,
                     Color tint)
{
    const CastEffect *fx = NULL;
    for (int i = 0; i < SONNY_CAST_EFFECT_COUNT; i++)
        if (SONNY_CAST_EFFECTS[i].character == p->character)
            fx = &SONNY_CAST_EFFECTS[i];
    if (!fx || fx->count <= 0)
        return 0;

    const AssetAnimation *anim = asset_animation(fx->animation);
    int32_t index = anim ? frame - anim->start : 0;
    if (index < 0)
        index = 0;
    if (index >= fx->count)
        index = fx->loops ? index % fx->count : fx->count - 1;

    const CastFrame *cf = &fx->frames[index];
    const float outer[6] = {p->a, p->b, p->c, p->d, p->tx, p->ty};
    int drawn = 0;
    char name[32];
    for (int32_t i = 0; i < cf->count; i++) {
        const CastLayer *l = &cf->layers[i];
        if (l->alpha <= 0.0f)
            continue;
        const float inner[6] = {l->a, l->b, l->c, l->d, l->tx, l->ty};
        float m[6];
        compose(outer, inner, m);
        snprintf(name, sizeof(name), "#%d", l->character);
        /* The recoloured layer is replaced outright, the way Color.setRGB
           replaces rather than tints, so it goes through the whitened copy. */
        Color c = tint;
        const Texture2D *tex;
        if (l->tinted && spec->cast.a) {
            tex = asset_texture_recolored(name, 1);
            c.r = spec->cast.r;
            c.g = spec->cast.g;
            c.b = spec->cast.b;
        } else {
            tex = asset_texture(name, 1);
        }
        if (!tex)
            continue;
        c.a = (unsigned char)(c.a * l->alpha);
        draw_with_matrix(tex, m, origin, scale, flip,
                         asset_frame_offset(name, 1), c);
        drawn++;
    }
    return drawn;
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
        if (!part) {
            /* The model's one unnamed slot, which carries the move's effect
               rather than any part of the body. */
            if (p->character)
                drawn += draw_cast(spec, p, frame, origin, scale, flip, tint);
            continue;   /* "shadower" and anything else not a dressed part */
        }

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
