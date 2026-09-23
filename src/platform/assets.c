#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "loader.h"
#include "rlgl.h"

#define ASSET_CACHE_MAX 512
/* And what they may come to between them. A count on its own stopped being
   enough when the art went to two pixels to the stage unit: a comic's panel
   is a couple of thousand pixels across and eleven megabytes on the card, and
   five hundred of anything like that is more memory than a machine need have.
   A cutscene is what fills this; everything else in the game together is a
   fraction of it. */
#define ASSET_CACHE_BYTES (192 * 1024 * 1024)

typedef struct {
    const char *name;          /* NULL for a slot nothing is in */
    int32_t     frame;
    int32_t     key;           /* which of every asset's frames this is */
    Texture2D   texture;
    int32_t     ok;
    int64_t     used;          /* when this was last asked for */
    int64_t     bytes;         /* what it takes on the card, mipmaps and all */
} CachedTexture;

static CachedTexture cache[ASSET_CACHE_MAX];
static int32_t cache_count;
/* Where each frame of each asset sits in the cache, so asking for a texture
   is an index rather than a walk over five hundred slots: every asset's
   frames numbered end to end (frame_base[asset] + frame - 1), and for each
   the slot it is in plus one, or nothing. */
static int32_t *frame_base;
static int32_t *frame_slot;

static int32_t frame_key(const AssetEntry *entry, int32_t frame)
{
    if (!frame_base) {
        frame_base = MemAlloc((unsigned)(SONNY_ASSET_COUNT + 1)
                              * sizeof(int32_t));
        for (int32_t i = 0; i < SONNY_ASSET_COUNT; i++)
            frame_base[i + 1] = frame_base[i] + SONNY_ASSETS[i].frame_count;
        frame_slot = MemAlloc((unsigned)(frame_base[SONNY_ASSET_COUNT] + 1)
                              * sizeof(int32_t));
    }
    return frame_base[entry - SONNY_ASSETS] + frame - 1;
}
static int64_t cache_clock;
static int64_t cache_bytes;
static char asset_root[512] = ".";
static int trace_loads;

void assets_trace_loads(int on)
{
    trace_loads = on;
}

void assets_set_root(const char *root)
{
    snprintf(asset_root, sizeof(asset_root), "%s", root ? root : ".");
}

const char *assets_path(const char *relative)
{
    static char pool[4][1024];
    static int next;
    char *out = pool[next];
    next = (next + 1) % 4;
    if (!relative || relative[0] == '/')
        snprintf(out, sizeof(pool[0]), "%s", relative ? relative : "");
    else
        snprintf(out, sizeof(pool[0]), "%s/%s", asset_root, relative);
    return out;
}

void assets_check_stamp(void)
{
    const char *path = assets_path("assets/art/stamp.txt");
    char *on_disk = LoadFileText(path);
    if (!on_disk) {
        TraceLog(LOG_WARNING, "ASSETS: no %s -- cannot tell whether the art "
                              "matches this build", path);
        return;
    }
    /* Whitespace and the trailing newline are the file's, not the stamp's. */
    int n = 0;
    while (on_disk[n] && on_disk[n] > ' ')
        n++;
    if (strncmp(on_disk, SONNY_ASSET_STAMP, (size_t)n) != 0
        || SONNY_ASSET_STAMP[n] != '\0')
        TraceLog(LOG_WARNING,
                 "ASSETS: the art in %s was built as %.*s and this game was "
                 "built against %s. They are not the same set, and nothing "
                 "will be the size it should be. Run: make game",
                 asset_root, n, on_disk, SONNY_ASSET_STAMP);
    UnloadFileText(on_disk);
}

int32_t asset_frame_count(const char *name)
{
    const AssetEntry *entry = asset_find(name);
    return entry ? entry->frame_count : 0;
}

static CachedTexture *oldest_slot(void)
{
    CachedTexture *slot = &cache[0];
    for (int32_t i = 1; i < cache_count; i++)
        if (cache[i].name && (!slot->name || cache[i].used < slot->used))
            slot = &cache[i];
    return slot;
}

static void release(CachedTexture *slot)
{
    if (slot->name)
        frame_slot[slot->key] = 0;
    if (slot->ok)
        UnloadTexture(slot->texture);
    cache_bytes -= slot->bytes;
    slot->name = NULL;
    slot->frame = 0;
    slot->ok = 0;
    slot->bytes = 0;
    slot->used = 0;
}

/* Throw out the one that has gone longest without being asked for. Returns 0
   when there is nothing left to throw out. */
static int evict_oldest(void)
{
    CachedTexture *slot = oldest_slot();
    if (!slot->name)
        return 0;
    release(slot);
    return 1;
}

/* Which frame of an asset a caller means: 1-based, and past the end it
   wraps round. */
static int32_t clamp_frame(const AssetEntry *entry, int32_t frame)
{
    if (frame < 1)
        frame = 1;
    if (frame > entry->frame_count)
        frame = ((frame - 1) % entry->frame_count) + 1;
    return frame;
}

/* A slot for another texture. The one that has gone longest without being
   asked for makes way -- never the one just handed out, which is what lets a
   caller hold a texture while it fetches another.

   A comic is a thousand pictures of its own, so a cache that only filled up
   would leave the game with nothing to draw once one had played. */
static CachedTexture *take_slot(const AssetEntry *entry, int32_t frame,
                                int32_t key)
{
    while (cache_bytes > ASSET_CACHE_BYTES)
        if (!evict_oldest())
            break;
    CachedTexture *slot = NULL;
    for (int32_t i = 0; i < cache_count && !slot; i++)
        if (!cache[i].name)
            slot = &cache[i];
    if (!slot && cache_count < ASSET_CACHE_MAX)
        slot = &cache[cache_count++];
    if (!slot) {
        slot = oldest_slot();
        release(slot);
    }
    slot->used = ++cache_clock;
    slot->name = entry->name;
    slot->frame = frame;
    slot->key = key;
    frame_slot[key] = (int32_t)(slot - cache) + 1;
    slot->ok = 0;
    slot->bytes = 0;
    return slot;
}

/* Put a decoded picture on the card, and give it the chain of reduced copies
   every draw below full size samples from. */
static void upload(CachedTexture *slot, Image image)
{
    slot->texture = LoadTextureFromImage(image);
    if (slot->texture.id == 0)
        return;
    /* Some art is drawn a long way below its exported size -- the quit button
       is a 60px square shown at 18 -- and sampling four texels of it picks out
       whichever ones happen to land under the pointer, which turns a dark
       glossy square into a flat bright one. Mipmaps give those draws a
       properly reduced copy to read; at or near full size trilinear still
       reads the full one. */
    GenTextureMipmaps(&slot->texture);
    SetTextureFilter(slot->texture, TEXTURE_FILTER_TRILINEAR);
    slot->ok = 1;
    /* Four bytes a pixel, and a third as much again for the reduced copies. */
    slot->bytes = (int64_t)slot->texture.width * slot->texture.height * 4 * 4
                / 3;
    cache_bytes += slot->bytes;
}

static const Texture2D *entry_texture(const AssetEntry *entry, int32_t frame)
{
    if (!entry || entry->frame_count == 0)
        return NULL;
    frame = clamp_frame(entry, frame);
    int32_t key = frame_key(entry, frame);
    if (frame_slot[key]) {
        CachedTexture *hit = &cache[frame_slot[key] - 1];
        hit->used = ++cache_clock;
        return hit->ok ? &hit->texture : NULL;
    }

    CachedTexture *slot = take_slot(entry, frame, key);
    /* Decoded ahead, or being decoded now: take the worker's copy. Anything
       else is read here and now, which is how everything used to be. */
    Image image = {0};
    if (!loader_pending(key) || !loader_wait(key, &image)) {
        const char *path = assets_path(entry->frames[frame - 1]);
        double began = GetTime();
        if (FileExists(path))
            image = LoadImage(path);
        /* SONNY_TRACE: what had to be read in the middle of a frame, which is
           what a prefetch list is missing. */
        if (trace_loads)
            printf("TRACE read %s frame %d on the spot, %.1f ms\n",
                   entry->name, (int)frame, (GetTime() - began) * 1000.0);
    }
    if (image.data) {
        upload(slot, image);
        UnloadImage(image);
    }
    return slot->ok ? &slot->texture : NULL;
}

/* Which asset and frame a key numbers, the other way round from frame_key. */
static const AssetEntry *key_entry(int32_t key, int32_t *frame)
{
    int32_t lo = 0, hi = SONNY_ASSET_COUNT - 1;
    while (lo < hi) {
        int32_t mid = lo + (hi - lo + 1) / 2;
        if (frame_base[mid] <= key)
            lo = mid;
        else
            hi = mid - 1;
    }
    /* Assets with no frames share a base with the next one; skip to the one
       that actually owns the key. */
    while (lo < SONNY_ASSET_COUNT - 1 && frame_base[lo + 1] <= key)
        lo++;
    *frame = key - frame_base[lo] + 1;
    return &SONNY_ASSETS[lo];
}

void assets_prefetch(const char *name, int32_t frame)
{
    const AssetEntry *entry = asset_find(name);
    if (!entry || entry->frame_count == 0)
        return;
    frame = clamp_frame(entry, frame);
    int32_t key = frame_key(entry, frame);
    if (frame_slot[key])
        return;
    loader_request(key, assets_path(entry->frames[frame - 1]));
}

void assets_prefetch_all(const char *name)
{
    const AssetEntry *entry = asset_find(name);
    for (int32_t f = 1; entry && f <= entry->frame_count; f++)
        assets_prefetch(name, f);
}

void assets_pump(double budget)
{
    double until = GetTime() + budget;
    int32_t key;
    Image image;
    while (GetTime() < until && loader_collect(&key, &image)) {
        if (!frame_base || frame_slot[key] || !image.data) {
            /* Read on the spot while it was on its way, or unreadable. */
            UnloadImage(image);
            continue;
        }
        int32_t frame = 0;
        const AssetEntry *entry = key_entry(key, &frame);
        CachedTexture *slot = take_slot(entry, frame, key);
        upload(slot, image);
        UnloadImage(image);
    }
}

const Texture2D *asset_texture(const char *name, int32_t frame)
{
    return entry_texture(asset_find(name), frame);
}

/* A copy of an exported image with its colour taken out, so drawing it with a
   tint lands on exactly that colour.
 *
 * ActionScript's Color.setRGB replaces a clip's colour outright -- it zeroes
 * the multiplier and puts the value in the offset -- where a raylib tint
 * multiplies. The two agree only when the art is white, and the pieces the
 * game recolours this way are not: the disc behind a buff's icon is authored
 * flat green, and multiplying green by a red element gives black. */
/* As many as the game asks for -- a fixed sixteen ran out and handed back
   the untouched art instead, which lands on the wrong colour -- keyed by the
   frame as well as the name. */
typedef struct {
    const char *name;
    int32_t     frame;
    Texture2D   texture;
    int32_t     ok;
} WhiteTexture;

static WhiteTexture *white_cache;
static int32_t white_count, white_room;

const Texture2D *asset_texture_recolored(const char *name, int32_t frame)
{
    const AssetEntry *entry = asset_find(name);
    if (!entry)
        return NULL;
    for (int32_t i = 0; i < white_count; i++)
        if (white_cache[i].name == entry->name && white_cache[i].frame == frame)
            return white_cache[i].ok ? &white_cache[i].texture : NULL;

    const Texture2D *source = asset_texture(name, frame);
    if (!source)
        return NULL;
    if (white_count == white_room) {
        int32_t room = white_room ? white_room * 2 : 16;
        WhiteTexture *grown = MemRealloc(white_cache,
                                         (unsigned)room * sizeof(*grown));
        if (!grown)
            return NULL;
        white_cache = grown;
        white_room = room;
    }
    Image image = LoadImageFromTexture(*source);
    Color *pixels = LoadImageColors(image);
    for (int i = 0; i < image.width * image.height; i++) {
        pixels[i].r = 255;
        pixels[i].g = 255;
        pixels[i].b = 255;
    }
    Image plain = {pixels, image.width, image.height, 1,
                   PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    WhiteTexture *slot = &white_cache[white_count++];
    slot->name = entry->name;
    slot->frame = frame;
    slot->texture = LoadTextureFromImage(plain);
    slot->ok = slot->texture.id != 0;
    if (slot->ok)
        SetTextureFilter(slot->texture, TEXTURE_FILTER_BILINEAR);
    UnloadImageColors(pixels);
    UnloadImage(image);
    return slot->ok ? &slot->texture : NULL;
}

/* Where a frame's own origin sits inside its exported image. The offsets are
   read out of the vector source and so are in stage units whatever scale the
   art itself was rasterised at. */
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

int asset_art(const char *name, int32_t frame, Art *out)
{
    *out = (Art){0};
    /* One lookup of the name for everything below: this runs for every
       picture on every frame. */
    const AssetEntry *entry = asset_find(name);
    const Texture2D *tex = entry_texture(entry, frame);
    if (!tex)
        return 0;
    float scale = entry->scale > 0.0f ? entry->scale : 1.0f;
    out->texture = tex;
    out->size = (Vector2){tex->width / scale, tex->height / scale};
    if (entry->offsets) {
        int32_t f = clamp_frame(entry, frame);
        out->offset = (Vector2){entry->offsets[f - 1].x,
                                entry->offsets[f - 1].y};
    }
    out->source = (Rectangle){0, 0, (float)tex->width, (float)tex->height};
    return 1;
}

int asset_draw_placed(const char *name, int32_t frame, Vector2 parent,
                      float scale, Color tint)
{
    Art art;
    if (!asset_art(name, frame, &art))
        return 0;
    /* The frame's recorded offset says where its own origin sits inside the
       image, so this puts that origin exactly on `parent`. */
    Rectangle dest = {parent.x - art.offset.x * scale,
                      parent.y - art.offset.y * scale,
                      art.size.x * scale, art.size.y * scale};
    DrawTexturePro(*art.texture, art.source, dest, (Vector2){0, 0}, 0.0f,
                   tint);
    return 1;
}

void assets_unload_all(void)
{
    for (int32_t i = 0; i < cache_count; i++)
        if (cache[i].ok)
            UnloadTexture(cache[i].texture);
    cache_count = 0;
    cache_bytes = 0;
    memset(cache, 0, sizeof(cache));
    MemFree(frame_base);
    MemFree(frame_slot);
    frame_base = frame_slot = NULL;
    for (int32_t i = 0; i < white_count; i++)
        if (white_cache[i].ok)
            UnloadTexture(white_cache[i].texture);
    MemFree(white_cache);
    white_cache = NULL;
    white_count = white_room = 0;
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
static void draw_with_matrix(const Art *art, const float m[6],
                             Vector2 origin, float scale, int flip,
                             Color tint)
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
    Rectangle dest = {-art->offset.x, -art->offset.y,
                      art->size.x, art->size.y};
    DrawTexturePro(*art->texture, art->source, dest, (Vector2){0, 0}, 0.0f,
                   tint);
    rlPopMatrix();
}

static int draw_layer(const char *name, const float m[6], Vector2 origin,
                      float scale, int flip, Color tint)
{
    Art art;
    if (!asset_art(name, 1, &art))
        return 0;
    draw_with_matrix(&art, m, origin, scale, flip, tint);
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

    /* The model's own placement of the slot is what times the glow: it comes
       up over three frames, holds while the bolt is made, and is faded to
       nothing over the rest of the animation. Drawing the clip solid for as
       long as the slot is placed left the glow hanging on well past the point
       the bolt leaves. */
    if (p->alpha <= 0.0f)
        return 0;

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
        Art art;
        if (!asset_art(name, 1, &art))
            continue;
        if (l->tinted && spec->cast.a) {
            const Texture2D *plain = asset_texture_recolored(name, 1);
            if (!plain)
                continue;
            art.texture = plain;
            c.r = spec->cast.r;
            c.g = spec->cast.g;
            c.b = spec->cast.b;
        }
        c.a = (unsigned char)(c.a * l->alpha * p->alpha);
        draw_with_matrix(&art, m, origin, scale, flip, c);
        drawn++;
    }
    return drawn;
}

float doll_frame_alpha(int32_t frame)
{
    if (SONNY_DOLL_FRAME_COUNT == 0)
        return 1.0f;
    if (frame < 1)
        frame = 1;
    if (frame > SONNY_DOLL_FRAME_COUNT)
        frame = SONNY_DOLL_FRAME_COUNT;
    return SONNY_DOLL_FRAMES[frame - 1].alpha;
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
