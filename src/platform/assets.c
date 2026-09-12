#include <stdio.h>
#include <string.h>
#include "assets.h"

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

void assets_unload_all(void)
{
    for (int32_t i = 0; i < cache_count; i++)
        if (cache[i].ok)
            UnloadTexture(cache[i].texture);
    cache_count = 0;
}
