/* Small shared pieces every screen uses: the stage-space mouse, panels,
   buttons, and the two message lines. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "game.h"
#include "render.h"
#include "rlgl.h"

/* The interface's two faces.
 *
 * The game's own text is set in the Tahoma the SWF embeds. Not all of it is,
 * though: the battle bars and other fields are set in "_sans", one of Flash's
 * device fonts, which the player renders with a system face -- Arial on the
 * Windows the game shipped for -- rather than with anything in the file. So
 * the system's nearest equivalent is what those get here.
 *
 * Flash rasterises text at the size it is finally drawn at, hinted. Baking one
 * big atlas and scaling it down does not look like that: at nine points the
 * minification eats the stems and the text goes grey and soft. So each size a
 * screen asks for is baked once and kept, which comes out crisp the way the
 * original does.
 *
 * Spacing needs the opposite. raylib rounds every glyph's advance down to a
 * whole pixel, which at these sizes loses about half a pixel a letter -- a
 * line of text comes out the better part of a fifth too narrow, and wraps in
 * the wrong place. So each size also keeps a much larger bake, used only to
 * measure: its advances scale down to fractions of a pixel, and text is laid
 * out a glyph at a time against those.
 */
#define UI_FONT_PATH "assets/font/1478_Tahoma.ttf"

static const char *const DEVICE_SANS[] = {
    "/usr/share/fonts/truetype/msttcorefonts/Arial.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
};

/* The bold weight of the same device face. The tooltip's title asks for it
   outright -- my_fmt2.bold -- and nothing else in the game does. */
static const char *const DEVICE_SANS_BOLD[] = {
    "/usr/share/fonts/truetype/msttcorefonts/Arial_Bold.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
};

#define UI_FACE_GAME   0
#define UI_FACE_SANS   1
#define UI_FACE_BOLD   2
#define UI_FACES       3
/* More sizes than the game asks for. A bake is pointed at while text is laid
   out, so the table is not grown under it; running out is reported instead. */
#define UI_SIZE_CACHE  64
/* How much bigger the measuring bake is than the one drawn from. */
#define UI_METRIC_SCALE 8

typedef struct {
    const char *path;
    int         ready;
    /* Flash sizes text by the em square; raylib bakes a font so that its
       ascent plus descent comes to the size asked for. Those are not the
       same number, so a nine-point field comes out a fifth too small unless
       the request is scaled by the face's own ratio between them. */
    float       em_to_box;
    struct { int size; Font font; Font metrics; float em; float scale; }
                cache[UI_SIZE_CACHE];
    int         cached;
} UiFace;

/* (ascender - descender) / unitsPerEm, read out of the font file: the head
   table carries the em square and hhea the two metrics. Returns 0 when the
   file is not a font this can read, and the caller then sizes text as raylib
   would on its own. */
static float font_em_to_box(const char *path)
{
    int size = 0;
    unsigned char *data = LoadFileData(path, &size);
    if (!data)
        return 0.0f;

    float ratio = 0.0f;
    float units = 0.0f, ascender = 0.0f, descender = 0.0f;
    if (size >= 12) {
        int tables = (data[4] << 8) | data[5];
        for (int i = 0; i < tables; i++) {
            int rec = 12 + i * 16;
            if (rec + 16 > size)
                break;
            unsigned int off = ((unsigned int)data[rec + 8] << 24)
                             | ((unsigned int)data[rec + 9] << 16)
                             | ((unsigned int)data[rec + 10] << 8)
                             | data[rec + 11];
            if (memcmp(data + rec, "head", 4) == 0 && off + 20 <= (unsigned)size)
                units = (float)((data[off + 18] << 8) | data[off + 19]);
            else if (memcmp(data + rec, "hhea", 4) == 0
                     && off + 8 <= (unsigned)size) {
                ascender = (float)(short)((data[off + 4] << 8) | data[off + 5]);
                descender = (float)(short)((data[off + 6] << 8) | data[off + 7]);
            }
        }
    }
    if (units > 0.0f)
        ratio = (ascender - descender) / units;
    UnloadFileData(data);
    return (ratio > 0.5f && ratio < 3.0f) ? ratio : 0.0f;
}

static UiFace ui_faces[UI_FACES];

/* The orb icons cut to the ball, which ui_font_unload lets go of too. */
typedef struct {
    char      name[48];
    Texture2D texture;
    int       ok;
} OrbCut;

static OrbCut *orb_cut;
static int32_t orb_cut_count, orb_cut_room;

/* The SWF embeds each font as a subset of the glyphs it happens to use, and
   three of its four hold only a handful. Anything that thin would render most
   of the interface as blanks. */
#define UI_MIN_GLYPHS 90

/* raylib bakes printable ASCII by default, which leaves out the euro sign the
   game prints against every price. */
static int UI_CODEPOINTS[96];
#define UI_CODEPOINT_COUNT ((int)(sizeof(UI_CODEPOINTS) / sizeof(UI_CODEPOINTS[0])))

static void codepoints_init(void)
{
    for (int i = 0; i < 95; i++)
        UI_CODEPOINTS[i] = 32 + i;
    UI_CODEPOINTS[95] = 0x20AC;         /* EURO SIGN */
}

/* The pair of bakes for one size: the one text is drawn from, and the large
   one it is measured against. `em` is the em square the measuring bake works
   out to in its own pixels, which is what turns its advances into the
   caller's units.
 *
 * A bake is a picture of the glyphs at a fixed size, so the size drawn from
 * is the size it is sharp at: baked at the stage's nine points and shown in a
 * window twice that, every letter is smoothed across four pixels. So the
 * drawing bake is made at the size the stage is being rasterised to and the
 * glyphs are laid down at a stage unit each, which is the same text at the
 * window's own resolution. The measuring bake is deliberately left alone --
 * it only ever yields advances, in stage units, and keeping it off the render
 * scale is what keeps every line of text laid out identically at every window
 * size. */
static int face_size(int face, float size)
{
    UiFace *f = &ui_faces[face];
    if (!f->ready)
        return -1;

    int px = (int)(size * f->em_to_box + 0.5f);
    if (px < 6)
        px = 6;
    float scale = render_scale();
    int draw_px = (int)(px * scale + 0.5f);
    if (draw_px < 6)
        draw_px = 6;
    for (int i = 0; i < f->cached; i++) {
        if (f->cache[i].size != px)
            continue;
        /* The window has changed size under a bake that is still wanted:
           the letters are re-drawn at the new resolution, and the advances
           they are laid out on stay exactly as they were. */
        if (f->cache[i].scale != scale) {
            Font again = LoadFontEx(f->path, draw_px, UI_CODEPOINTS,
                                    UI_CODEPOINT_COUNT);
            if (again.texture.id != 0) {
                SetTextureFilter(again.texture, TEXTURE_FILTER_BILINEAR);
                if (f->cache[i].metrics.texture.id
                    != f->cache[i].font.texture.id)
                    UnloadFont(f->cache[i].font);
                f->cache[i].font = again;
                f->cache[i].scale = scale;
            }
        }
        return i;
    }
    if (f->cached == UI_SIZE_CACHE) {
        static int warned;
        if (!warned++)
            TraceLog(LOG_WARNING, "UI: more than %d text sizes; %d is drawn "
                     "at the last size baked", UI_SIZE_CACHE, px);
        return f->cached - 1;
    }

    Font font = LoadFontEx(f->path, draw_px, UI_CODEPOINTS,
                           UI_CODEPOINT_COUNT);
    if (font.texture.id == 0)
        return -1;
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);

    int big = px * UI_METRIC_SCALE;
    Font metrics = LoadFontEx(f->path, big, UI_CODEPOINTS,
                              UI_CODEPOINT_COUNT);
    if (metrics.texture.id == 0)
        metrics = font;

    int slot = f->cached++;
    f->cache[slot].size = px;
    f->cache[slot].font = font;
    f->cache[slot].metrics = metrics;
    f->cache[slot].scale = scale;
    f->cache[slot].em = (float)metrics.baseSize / f->em_to_box;
    return slot;
}

static int face_open(int face, const char *path)
{
    Font probe = LoadFontEx(path, 16, UI_CODEPOINTS, UI_CODEPOINT_COUNT);
    int ok = probe.texture.id != 0 && probe.glyphCount >= UI_MIN_GLYPHS;
    if (probe.texture.id != 0)
        UnloadFont(probe);
    if (!ok)
        return 0;
    ui_faces[face].path = path;
    float ratio = font_em_to_box(path);
    ui_faces[face].em_to_box = (ratio > 0.0f) ? ratio : 1.0f;
    ui_faces[face].ready = 1;
    return 1;
}

void ui_font_load(void)
{
    codepoints_init();
    if (ui_faces[UI_FACE_GAME].ready)
        return;
    /* face_open keeps the path, so it has to be one that lasts. */
    static char font_path[1024];
    snprintf(font_path, sizeof(font_path), "%s", assets_path(UI_FONT_PATH));
    if (!FileExists(font_path) || !face_open(UI_FACE_GAME, font_path))
        TraceLog(LOG_WARNING, "UI font %s unusable; using the default",
                 font_path);

    for (size_t i = 0; i < sizeof(DEVICE_SANS_BOLD)
                           / sizeof(DEVICE_SANS_BOLD[0]); i++)
        if (FileExists(DEVICE_SANS_BOLD[i])
            && face_open(UI_FACE_BOLD, DEVICE_SANS_BOLD[i]))
            break;
    for (size_t i = 0; i < sizeof(DEVICE_SANS) / sizeof(DEVICE_SANS[0]); i++)
        if (FileExists(DEVICE_SANS[i])
            && face_open(UI_FACE_SANS, DEVICE_SANS[i]))
            break;
}

void ui_font_unload(void)
{
    for (int face = 0; face < UI_FACES; face++) {
        for (int i = 0; i < ui_faces[face].cached; i++) {
            if (ui_faces[face].cache[i].metrics.texture.id
                != ui_faces[face].cache[i].font.texture.id)
                UnloadFont(ui_faces[face].cache[i].metrics);
            UnloadFont(ui_faces[face].cache[i].font);
        }
        ui_faces[face].cached = 0;
        ui_faces[face].ready = 0;
    }
    /* The orbs' cut icons are made from the same bakes' neighbours and go
       with them. */
    for (int32_t i = 0; i < orb_cut_count; i++)
        if (orb_cut[i].ok)
            UnloadTexture(orb_cut[i].texture);
    MemFree(orb_cut);
    orb_cut = NULL;
    orb_cut_count = orb_cut_room = 0;
}

/* A little negative tracking keeps the game's own face close to the
   original's spacing at small sizes rather than looking loose; a device font
   is drawn at the player's metrics, with none of its own. */
static float face_spacing(int face, float size)
{
    (void)face;
    (void)size;
    /* Flash spaces text by the font's own advances and nothing else. */
    return 0.0f;
}

/* Walk a string, handing each glyph its advance in the caller's units. */
static float face_run(int face, const char *text, float size, float x, float y,
                      Color color, int draw)
{
    int slot = face_size(face, size);
    if (slot < 0)
        return -1.0f;
    UiFace *f = &ui_faces[face];
    const Font *font = &f->cache[slot].font;
    const Font *metrics = &f->cache[slot].metrics;
    /* The measuring bake's advances are in its own pixels; one of its em
       squares is what the caller calls `size`. */
    float scale = size / f->cache[slot].em;
    float pen = 0.0f;

    for (int i = 0; text[i];) {
        int step = 0;
        int codepoint = GetCodepointNext(text + i, &step);
        i += step;
        int index = GetGlyphIndex(*metrics, codepoint);
        int advance = metrics->glyphs[index].advanceX;
        if (advance == 0)
            advance = (int)metrics->recs[index].width
                    + metrics->glyphs[index].offsetX;
        if (draw && codepoint != ' ') {
            int g = GetGlyphIndex(*font, codepoint);
            Rectangle src = font->recs[g];
            /* The bake is in device pixels and everything here is in stage
               units, so a texel of it is one over the render scale. The
               glyph still goes on a whole pixel -- Flash hints its text onto
               one, and drawn at a fraction the same glyphs sample across two
               texels each and go soft -- but the pixel that matters is the
               window's, not the stage's. */
            float texel = 1.0f / f->cache[slot].scale;
            Rectangle dst = {
                render_snap(x + pen + font->glyphs[g].offsetX * texel),
                render_snap(y + font->glyphs[g].offsetY * texel),
                src.width * texel, src.height * texel};
            DrawTexturePro(font->texture, src, dst, (Vector2){0, 0}, 0.0f,
                           color);
        }
        pen += advance * scale + face_spacing(face, size);
    }
    return pen;
}

static void face_draw(int face, const char *text, float x, float y, float size,
                      Color color)
{
    if (!text || !text[0])
        return;
    /* On the pixel grid, the window's rather than the stage's. */
    if (face_run(face, text, size, render_snap(x), render_snap(y), color,
                 1) >= 0.0f)
        return;
    if (face == UI_FACE_BOLD) {
        face_draw(UI_FACE_SANS, text, x, y, size, color);
        return;
    }
    if (face == UI_FACE_SANS) {
        face_draw(UI_FACE_GAME, text, x, y, size, color);
        return;
    }
    DrawText(text, (int)x, (int)y, (int)size, color);
}

static float face_width(int face, const char *text, float size)
{
    if (!text || !text[0])
        return 0;
    float width = face_run(face, text, size, 0.0f, 0.0f, BLANK, 0);
    if (width >= 0.0f)
        return width;
    if (face == UI_FACE_BOLD)
        return face_width(UI_FACE_SANS, text, size);
    if (face == UI_FACE_SANS)
        return face_width(UI_FACE_GAME, text, size);
    return (float)MeasureText(text, (int)size);
}

/* Measuring a line a glyph at a time. face_run adds each glyph's advance
   to a pen that starts at nothing, so adding them up the same way here gives
   exactly the widths it would -- without measuring the whole prefix again
   every time a letter is added, which is what wrapping used to do. A face
   with nothing baked falls back the way face_width does, and past all of
   them to raylib's own font, which is not additive and is measured whole. */
typedef struct {
    int         face;       /* -1: raylib's own font */
    const Font *metrics;
    float       scale;
    float       size;
} TextMeasure;

static TextMeasure measure_begin(int face, float size)
{
    TextMeasure m = {-1, NULL, 0.0f, size};
    for (;;) {
        int slot = face_size(face, size);
        if (slot >= 0) {
            m.face = face;
            m.metrics = &ui_faces[face].cache[slot].metrics;
            m.scale = size / ui_faces[face].cache[slot].em;
            return m;
        }
        if (face == UI_FACE_BOLD)
            face = UI_FACE_SANS;
        else if (face == UI_FACE_SANS)
            face = UI_FACE_GAME;
        else
            return m;
    }
}

/* One codepoint's advance, in the caller's units. */
static float measure_glyph(const TextMeasure *m, int codepoint)
{
    int index = GetGlyphIndex(*m->metrics, codepoint);
    int advance = m->metrics->glyphs[index].advanceX;
    if (advance == 0)
        advance = (int)m->metrics->recs[index].width
                + m->metrics->glyphs[index].offsetX;
    return advance * m->scale + face_spacing(m->face, m->size);
}

/* The width of text[from, to), for the fallback that cannot add up. */
static float measure_span(const TextMeasure *m, const char *text, int from,
                          int to)
{
    char buf[512];
    int n = to - from;
    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;
    memcpy(buf, text + from, (size_t)n);
    buf[n] = 0;
    return (float)MeasureText(buf, (int)m->size);
}

/* Draw text[from, to) -- a line cut out of something longer. */
static void draw_span(int face, const char *text, int from, int to, float x,
                      float y, float size, Color color)
{
    char buf[512];
    int n = to - from;
    if (n <= 0)
        return;
    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;
    memcpy(buf, text + from, (size_t)n);
    buf[n] = 0;
    face_draw(face, buf, x, y, size, color);
}

void ui_text(const char *text, float x, float y, float size, Color color)
{
    face_draw(UI_FACE_GAME, text, x, y, size, color);
}

void ui_sans_text(const char *text, float x, float y, float size, Color color)
{
    face_draw(UI_FACE_SANS, text, x, y, size, color);
}

float ui_text_width(const char *text, float size)
{
    return face_width(UI_FACE_GAME, text, size);
}

float ui_sans_text_width(const char *text, float size)
{
    return face_width(UI_FACE_SANS, text, size);
}

/* The gutter Flash leaves inside every text field before the text starts,
   and how much taller than its size a line box is. */
#define TEXT_GUTTER 2.0f
#define TEXT_LINE_FACTOR 1.15f

/* Where a piece of art goes, given the placement the original recorded: its
   exported canvas carries the piece's own origin inside it, so the top-left
   is the placement point less that origin. */
Rectangle placed_rect(float x, float y, float scale_x, float scale_y,
                             float w, float h, float ox, float oy)
{
    return (Rectangle){x - ox * scale_x, y - oy * scale_y,
                       w * scale_x, h * scale_y};
}

/* Where an exported image goes, as opposed to where its box is.
   The decompiler rasterises a shape into a bitmap a pixel wider and taller
   than the box the SWF declares: the art fills the box and the last row and
   column are padding. So the image has to be drawn at its own size and the
   padding allowed to fall outside the box. Squeezing 61 units into 60 is
   less than a pixel, but it is a pixel taken off the corner of every icon on
   the screen, and it shows. `art.size` is that size in the stage's units,
   which is not the image's pixels: the art is rasterised finer than the
   stage wherever it can be. */
Rectangle placed_art(const Art *art, float x, float y,
                     float scale_x, float scale_y, float ox, float oy)
{
    return placed_rect(x, y, scale_x, scale_y, art->size.x, art->size.y,
                       ox, oy);
}

void draw_art_placed(const Art *art, float x, float y,
                     float scale_x, float scale_y, float ox, float oy,
                     Color tint)
{
    DrawTexturePro(*art->texture, art->source,
                   placed_art(art, x, y, scale_x, scale_y, ox, oy),
                   (Vector2){0, 0}, 0.0f, tint);
}

/* One text field, laid out as the SWF lays it out: its own box, alignment,
   leading, size, colour and face, relative to the clip it belongs to. */
void draw_field_tinted(const TextField *f, Vector2 clip,
                              const char *text, Color colour)
{
    if (!f || !text || !text[0])
        return;
    float width = f->device ? ui_sans_text_width(text, f->size)
                            : ui_text_width(text, f->size);
    float x = clip.x + f->x;
    if (f->align == 1)
        x += f->width - width;
    else if (f->align == 2)
        x += (f->width - width) / 2.0f;
    float y = clip.y + f->y + TEXT_GUTTER + f->leading;
    if (f->device)
        ui_sans_text(text, x, y, f->size, colour);
    else
        ui_text(text, x, y, f->size, colour);
}

/* The same, in the field's own colour. */
void draw_field(const TextField *f, Vector2 clip, const char *text)
{
    if (f)
        draw_field_tinted(f, clip, text, (Color){f->r, f->g, f->b, 255});
}

/* The same, wrapped to the field's width and stacked by its own line box.

   A line takes letters until the next one would carry it past the field's
   width, then goes back to the last space -- or breaks mid-word when there
   was none. It is measured in the face it is drawn in: a Tahoma field
   measured as _sans wrapped in the wrong place. */
void draw_field_wrapped(const TextField *f, Vector2 clip, const char *text)
{
    if (!f || !text || !text[0])
        return;
    int face = f->device ? UI_FACE_SANS : UI_FACE_GAME;
    TextMeasure m = measure_begin(face, f->size);
    /* Flash stacks lines a full line box apart, not a font size apart. */
    float line_height = f->size * TEXT_LINE_FACTOR + f->leading;
    int rows = (int)(f->height / line_height);
    Color ink = {f->r, f->g, f->b, 255};
    int start = 0, row = 0;
    while (text[start] && row < rows) {
        int fit = start, space = -1, hard = 0;
        float pen = 0.0f;
        for (int i = start; text[i];) {
            /* A line break in the text is a line break on the screen: Flash
               fields honour them, and running one through the wrapper as an
               ordinary character drew it as a glyph. */
            if (text[i] == '\n') {
                fit = i;
                hard = 1;
                break;
            }
            if (text[i] == ' ')
                space = i;
            int step = 0;
            int codepoint = GetCodepointNext(text + i, &step);
            float width = m.metrics ? (pen += measure_glyph(&m, codepoint))
                                    : measure_span(&m, text, start, i + step);
            if (width > f->width) {
                fit = (space > start) ? space : i;
                break;
            }
            i += step;
            fit = i;
        }
        /* A field too narrow for even one letter still shows it, rather than
           standing still on it for ever. */
        if (fit == start && !hard && text[start]) {
            int step = 0;
            GetCodepointNext(text + start, &step);
            fit = start + step;
        }

        float width = 0.0f;
        for (int i = start; m.metrics && i < fit;) {
            int step = 0;
            width += measure_glyph(&m, GetCodepointNext(text + i, &step));
            i += step;
        }
        if (!m.metrics)
            width = measure_span(&m, text, start, fit);
        float x = clip.x + f->x;
        if (f->align == 1)
            x += f->width - width;
        else if (f->align == 2)
            x += (f->width - width) / 2.0f;
        float y = clip.y + f->y + row * line_height + TEXT_GUTTER + f->leading;
        draw_span(face, text, start, fit, x, y, f->size, ink);

        start = fit;
        if (hard)
            start++;            /* step over the break itself */
        while (text[start] == ' ')
            start++;
        row++;
    }
}

/* The graphics of a clip that carries text. Such a clip cannot be drawn as
   one picture -- the export bakes its fields' design-time copy in -- so its
   pieces are drawn and the real text goes over them. A piece the game points
   at a frame of by name takes `chosen` as that name. */
void draw_clip_parts(const char *screen, const char *owner,
                            Vector2 moved, const char *framed,
                            const char *chosen, Color tint)
{
    int32_t row0, rows = clip_part_rows(screen, &row0);
    for (int32_t i = row0; i < row0 + rows; i++) {
        const ClipPart *part = &SONNY_CLIP_PARTS[i];
        if (strcmp(part->screen, screen) != 0
            || strcmp(part->owner, owner) != 0 || part->width <= 0)
            continue;
        /* A piece the game points at a frame of by name is only drawn when
           the caller names that piece and says which frame. The speech box
           has two of them -- the portrait, which it points at the speaker,
           and the "press SPACEBAR" prompt, which runs on its own -- so
           pointing every one of them at the speaker put a second little
           portrait over the speaker's name. */
        if (part->frames && (!framed || !chosen
                             || strcmp(part->name, framed) != 0))
            continue;
        const char *name = part->frames ? chosen
                                        : TextFormat("#%d", part->character);
        Art art;
        if (!asset_art(name, 1, &art))
            continue;
        draw_art_placed(&art, moved.x + part->x, moved.y + part->y,
                        part->scale_x, part->scale_y,
                        part->origin_x, part->origin_y, tint);
    }
}

/* Where a message goes. In a fight the original runs it across the top of
   the battlefield, in the KrinCombatText banner; off the battle screen there
   is no such banner, so it goes along the bottom. */
void game_draw_notice(const Game *g)
{
    if (!g->notice[0])
        return;
    const StageChrome *banner = (g->screen == SCREEN_BATTLE)
                              ? stage_chrome("KRINBATTLESCENE", "KrinCombatText") : NULL;
    Color colour = {235, 200, 90, 255};
    if (!banner) {
        ui_text(g->notice, 22, STAGE_H - 20, 10, colour);
        return;
    }
    float size = 12.0f;
    ui_sans_text(g->notice, banner->x - ui_sans_text_width(g->notice, size) / 2,
                 banner->y, size, colour);
}

void game_notice(Game *g, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(g->notice, sizeof(g->notice), fmt, args);
    va_end(args);
    g->notice_timer = STAGE_FPS * 3;
}

/* The mouse in stage coordinates, undoing the letterboxed scale the window
   applies to the fixed 800x575 stage. */
Vector2 stage_mouse(void)
{
    /* Back out of the window and into the stage, undoing the same fit the
       frame is drawn with -- whichever side the letterbox falls on. */
    Vector2 m = GetMousePosition();
    /* Through the same fit the frame is drawn with, which is the
       framebuffer's -- see the note in main.c. */
    StageFit fit = stage_fit(GetRenderWidth(), GetRenderHeight());
    if (fit.scale <= 0)
        return m;
    return (Vector2){(m.x - fit.x) / fit.scale, (m.y - fit.y) / fit.scale};
}

/* A click the harness makes rather than the pointer, so a run through the
   menus can be driven from a script. */
static int synthetic_click;

void ui_set_synthetic_click(int on)
{
    synthetic_click = on;
}

int ui_clicked(void)
{
    return synthetic_click || IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

int hit(Rectangle r, Vector2 p)
{
    return CheckCollisionPointRec(p, r);
}

/* ------------------------------------------------------------------ orbs */

/* The size an orb prints a slot's remaining cooldown at. */
#define ORB_ICON_PREFIX "ORB "
#define ORB_COOLDOWN_SIZE 9.0f
/* The ring's copy of an ability icon is a different shape to the menus', so
   it is kept under its own name. */

static void draw_orb_part(const OrbPart *part, Vector2 centre, float scale,
                          Color tint)
{
    if (!part)
        return;
    Art art;
    if (!asset_art(TextFormat("#%d", part->character), 1, &art))
        return;
    float sx = part->scale_x * scale;
    float sy = part->scale_y * scale;
    Rectangle dst = {centre.x + (part->x - part->origin_x * part->scale_x)
                     * scale,
                     centre.y + (part->y - part->origin_y * part->scale_y)
                     * scale,
                     part->width * sx, part->height * sy};
    DrawTexturePro(*art.texture, art.source, dst, (Vector2){0, 0}, 0.0f,
                   tint);
}

/* The orb clip cuts the icon to the ball with a circular mask, and the icons
   are drawn larger than the ball -- without the cut they show their corners.
   raylib has no masking, so an icon is combined with the mask the first time
   it is drawn and the cut copy is what every orb shows afterwards. */
/* The cut copies are kept in orb_cut, grown as icons are asked for: a fixed
   sixty-four stopped cutting new ones and the orbs past it came up empty. */
static const Texture2D *orb_icon_cut(const char *icon)
{
    for (int32_t i = 0; i < orb_cut_count; i++)
        if (strcmp(orb_cut[i].name, icon) == 0)
            return orb_cut[i].ok ? &orb_cut[i].texture : NULL;
    if (orb_cut_count == orb_cut_room) {
        int32_t room = orb_cut_room ? orb_cut_room * 2 : 32;
        OrbCut *grown = MemRealloc(orb_cut, (unsigned)room * sizeof(*grown));
        if (!grown)
            return NULL;
        orb_cut = grown;
        orb_cut_room = room;
    }

    int32_t slot = orb_cut_count++;
    snprintf(orb_cut[slot].name, sizeof(orb_cut[slot].name), "%s", icon);
    orb_cut[slot].ok = 0;

    const OrbPart *mask = orb_part("mask");
    Art icon_art, mask_art;
    if (!mask
        || !asset_art(TextFormat("%s%s", ORB_ICON_PREFIX, icon), 1, &icon_art)
        || !asset_art(TextFormat("#%d", mask->character), 1, &mask_art))
        return NULL;

    Image icon_img = LoadImageFromTexture(*icon_art.texture);
    Image mask_img = LoadImageFromTexture(*mask_art.texture);
    Color *icon_px = LoadImageColors(icon_img);
    Color *mask_px = LoadImageColors(mask_img);
    Color *out_px = (Color *)MemAlloc((unsigned)(mask_img.width
                                                 * mask_img.height)
                                      * sizeof(Color));
    /* The icon's own origin goes on the mask's, which is how the orb places
       the two over each other. The cut is made in the mask's pixels; the two
       need not be rasterised at the same scale, so the icon is walked at
       whatever ratio there is between them -- one, whenever both rasterised,
       which is when the step is a whole pixel and the sampling exact. */
    float mask_scale = mask_art.source.width / mask_art.size.x;
    float ratio = (icon_art.source.width / icon_art.size.x) / mask_scale;
    int shift_x = (int)((mask_art.offset.x - icon_art.offset.x) * mask_scale
                        + 0.5f);
    int shift_y = (int)((mask_art.offset.y - icon_art.offset.y) * mask_scale
                        + 0.5f);
    for (int y = 0; y < mask_img.height; y++) {
        for (int x = 0; x < mask_img.width; x++) {
            int sx = (int)((x - shift_x) * ratio + 0.5f);
            int sy = (int)((y - shift_y) * ratio + 0.5f);
            Color c = {0, 0, 0, 0};
            if (sx >= 0 && sy >= 0 && sx < icon_img.width
                && sy < icon_img.height)
                c = icon_px[sy * icon_img.width + sx];
            c.a = (unsigned char)(c.a * mask_px[y * mask_img.width + x].a
                                  / 255);
            out_px[y * mask_img.width + x] = c;
        }
    }
    Image out = {out_px, mask_img.width, mask_img.height, 1,
                 PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    orb_cut[slot].texture = LoadTextureFromImage(out);
    orb_cut[slot].ok = 1;

    UnloadImageColors(icon_px);
    UnloadImageColors(mask_px);
    UnloadImage(out);
    UnloadImage(icon_img);
    UnloadImage(mask_img);
    return &orb_cut[slot].texture;
}

void draw_orb(const char *icon, Vector2 centre, float scale, int32_t dim,
              int32_t cooldown)
{
    draw_orb_part(orb_part("ball"), centre, scale, WHITE);
    const OrbPart *mask = orb_part("mask");
    if (icon && icon[0] && mask) {
        const Texture2D *cut = orb_icon_cut(icon);
        if (cut) {
            Rectangle dst = {centre.x - mask->origin_x * scale,
                             centre.y - mask->origin_y * scale,
                             mask->width * scale, mask->height * scale};
            DrawTexturePro(*cut, (Rectangle){0, 0, (float)cut->width,
                                             (float)cut->height},
                           dst, (Vector2){0, 0}, 0.0f, WHITE);
        }
    }
    draw_orb_part(orb_part("glass"), centre, scale, WHITE);
    if (dim > 0)
        draw_orb_part(orb_part("filter"), centre, scale,
                      (Color){255, 255, 255, (unsigned char)dim});
    if (cooldown > 0) {
        const char *left = TextFormat("%d", cooldown);
        float size = ORB_COOLDOWN_SIZE;
        ui_sans_text(left, centre.x - ui_sans_text_width(left, size) / 2,
                     centre.y - size / 2, size, RAYWHITE);
    }
}

/* A button's own resting art. The hit shape a button answers in is drawn
   about the button's own origin, so the middle of the box the screen
   recorded is where its pieces hang from. */
void draw_button_art(const StageButton *b, Color tint)
{
    draw_button_state(b, 0, tint);
}

/* `over` picks the state the pointer being on the button swaps in, which is
   the only thing most of these buttons do to show they can be pressed. */
void draw_button_state(const StageButton *b, int over, Color tint)
{
    if (!b)
        return;
    Vector2 centre = {b->x + b->width / 2, b->y + b->height / 2};
    float sx = b->scale_x != 0 ? b->scale_x : 1.0f;
    float sy = b->scale_y != 0 ? b->scale_y : 1.0f;
    for (int32_t i = 0; ; i++) {
        const ButtonPiece *p = button_piece(b->character, i);
        if (!p)
            break;
        if (!(over ? p->over : p->up))
            continue;
        if (p->width <= 0)
            continue;
        Art art;
        if (!asset_art(TextFormat("#%d", p->character), 1, &art))
            continue;
        draw_art_placed(&art, centre.x + p->x * sx, centre.y + p->y * sy,
                        p->scale_x * sx, p->scale_y * sy,
                        p->origin_x, p->origin_y, tint);
    }
}

/* --------------------------------------------------------- screen chrome */

/* Pieces of a screen the game drives rather than simply draws, so the
   furniture pass leaves them to whatever owns them. A name is only meaningful
   on its own frame -- "@25" is a mask on the battlefield and something else
   entirely elsewhere -- so each frame has its own list.

   Off the battlefield: the zone scene, the menu that covers it, the tooltip
   and the fade, and the progress bar, whose width says how far through the
   zone the player is. Each is put on the stage by whatever screen owns it. */
static const char *const HUB_RUNTIME[] = {
        "KrinScreen", "KRINMENU", "KrinToolTipper", "KrinCombatText",
        "krinNavFadeSpeech", "@1242", "krinXbarPro",
    };

/* On the battlefield. The battle screen's furniture is not laid out here: it is the display list
   the original places on its KRINBATTLESCENE frame, drawn in the same depth
   order, each piece at the coordinate and scale the SWF gives it. The pieces
   named below are the ones the game drives at runtime and leaves hidden or
   parked off stage on a quiet turn, so drawing them from the display list
   would show furniture the original does not. */
static const char *const BATTLE_RUNTIME[] = {
        /* The two backdrop containers; the zone's own art is drawn for them. */
        "BATTLESCREEN", "@26",
        /* A mask and an invisible hit area, neither of which is a picture. */
        "@25", "@262",
        /* The full-screen fade, transparent except between screens. */
        "blacker5",
        /* The tooltip, parked off stage until something is hovered. */
        "KrinToolTipper", "@537",
        /* The target reticles, parked off stage until a target is picked. */
        "KrinSelector1", "KrinSelector2", "KrinSelector3",
        "KrinSelector4", "KrinSelector5", "KrinSelector6",
        /* The spinner shown while the other side is deciding. */
        "selector",
        /* The floating combat text and the speech box, both empty until
           something happens. */
        "KrinCombatText", "combatScript",
        /* The chosen move's orb, which sits in the turn indicator once the
           player has picked something and is hidden until then. */
        "krinToMove",
        /* The ring that closes over the indicator on a choice. It rests on
           an empty frame and is played through once, which draw_move_boomer
           does; left to the furniture pass it loops for ever. */
        "moveSelectBoomer",
        /* The turn indicator, which draw_turn_dial puts up itself: its clip
           holds the clock as well as the ring, and the decompiler renders
           that clock -- masked away at rest -- as a pair of stray slivers. */
        "battleClocker",
    };

int chrome_runtime(const char *screen, const char *name)
{
    int battle = strcmp(screen, "KRINBATTLESCENE") == 0;
    const char *const *list = battle ? BATTLE_RUNTIME : HUB_RUNTIME;
    size_t n = battle ? sizeof(BATTLE_RUNTIME) / sizeof(BATTLE_RUNTIME[0])
                      : sizeof(HUB_RUNTIME) / sizeof(HUB_RUNTIME[0]);
    for (size_t i = 0; i < n; i++)
        if (strcmp(name, list[i]) == 0)
            return 1;
    /* And the six unit bars, p1BAR to p6BAR, which the fight fills itself. */
    return battle && strncmp(name, "p", 1) == 0 && strstr(name, "BAR") != NULL;
}

void draw_screen_chrome(const char *screen)
{
    int32_t row0, rows = stage_chrome_rows(screen, &row0);
    for (int32_t i = row0; i < row0 + rows; i++) {
        const StageChrome *c = &SONNY_STAGE_CHROME[i];
        if (strcmp(c->screen, screen) != 0 || c->width <= 0)
            continue;
        if (chrome_runtime(screen, c->name))
            continue;
        Art art;
        if (!asset_art(TextFormat("#%d", c->character), 1, &art))
            continue;
        draw_art_placed(&art, c->x, c->y, c->scale_x, c->scale_y,
                        c->origin_x, c->origin_y, WHITE);
    }
}

void draw_screen_buttons(const char *screen, Vector2 mouse)
{
    int32_t row0, rows = button_rows(screen, &row0);
    for (int32_t i = row0; i < row0 + rows; i++) {
        const StageButton *b = &SONNY_BUTTONS[i];
        if (strcmp(b->screen, screen) != 0)
            continue;
        /* A button inside a clip the engine drives belongs to that clip, not
           to the screen: the menu's bag squares are on the hub's frame too,
           and they are only there when the menu is open. */
        if (chrome_runtime(screen, b->owner))
            continue;
        /* A button shows one set of pieces at rest and another under the
           pointer, which is the only thing most of them do to say they can
           be pressed. */
        Rectangle box = BOX_OF(b);
        draw_button_state(b, CheckCollisionPointRec(mouse, box), WHITE);
    }
}

const TextField *chrome_field(const char *screen, const char *name)
{
    return text_field_named(screen, name, name, 0);
}

int screen_button_pressed(const char *screen, int32_t character,
                          Vector2 mouse)
{
    const StageButton *b = stage_button(screen, character, 0);
    return b && ui_clicked()
        && CheckCollisionPointRec(mouse, BOX_OF(b));
}

/* The text a frame bakes into a field rather than setting at run time: the
   "Back" on every menu, the slot numbers, the copyright line. The decompiler
   hands these back as the HTML the field was authored with, so the markup is
   stripped and what is left is drawn with the field's own size and colour. */
static const char *strip_markup(const char *html, char *out, size_t max)
{
    size_t n = 0;
    int in_tag = 0;
    for (const char *p = html; *p && n + 1 < max; p++) {
        if (*p == '<') {
            in_tag = 1;
            continue;
        }
        if (*p == '>') {
            in_tag = 0;
            continue;
        }
        if (in_tag)
            continue;
        if (*p == '&') {
            static const struct { const char *name; char ch; } ENTITY[] = {
                {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'},
                {"quot;", '"'}, {"apos;", '\''}, {"nbsp;", ' '},
            };
            size_t i = 0;
            for (; i < sizeof(ENTITY) / sizeof(ENTITY[0]); i++) {
                size_t len = strlen(ENTITY[i].name);
                if (strncmp(p + 1, ENTITY[i].name, len) == 0) {
                    out[n++] = ENTITY[i].ch;
                    p += len;
                    break;
                }
            }
            if (i < sizeof(ENTITY) / sizeof(ENTITY[0]))
                continue;
        }
        out[n++] = *p;
    }
    out[n] = 0;
    return out;
}

void draw_static_text_except(const char *screen, const char *owner,
                             const Rectangle *skip, int skips, Vector2 shift)
{
    int32_t row0, rows = text_field_rows(screen, &row0);
    for (int32_t i = row0; i < row0 + rows; i++) {
        const TextField *f = &SONNY_TEXT_FIELDS[i];
        if (strcmp(f->screen, screen) != 0)
            continue;
        if (owner && strcmp(f->owner, owner) != 0)
            continue;
        /* A field the frame's script fills is the screen's to draw. */
        if (f->variable[0] || !f->text || !f->text[0])
            continue;
        /* And anything a screen has decided not to show, named by where it
           is, because a piece of the clip's own static text has nothing else
           to name it by. */
        int skipped = 0;
        for (int j = 0; j < skips && !skipped; j++)
            skipped = CheckCollisionPointRec((Vector2){f->x, f->y}, skip[j]);
        if (skipped)
            continue;
        char plain[512];
        strip_markup(f->text, plain, sizeof(plain));
        if (plain[0])
            draw_field_wrapped(f, shift, plain);
    }
}

void draw_static_text(const char *screen, const char *owner)
{
    draw_static_text_except(screen, owner, NULL, 0, (Vector2){0, 0});
}

void draw_screen_text(const char *screen)
{
    draw_static_text(screen, NULL);
}

/* ---------------------------------------------------------- the tooltip */

void ui_sans_bold_text(const char *text, float x, float y, float size,
                       Color color)
{
    face_draw(UI_FACE_BOLD, text, x, y, size, color);
}

float ui_sans_bold_width(const char *text, float size)
{
    return face_width(UI_FACE_BOLD, text, size);
}

/* The box the original parks under the pointer wherever something has a name.
 * KrinToolTipper builds it at run time rather than laying it out: two text
 * fields it creates on the spot, each with a backing stretched to fit, in the
 * sizes and colours sprite 1142's "GO" frame sets. The clip follows the
 * pointer with startDrag, and it flips to the other side of it near the right
 * edge and rides up near the bottom.
 */
#define TOOLTIP_WIDTH    170.0f
#define TOOLTIP_SIZE     12.0f
#define TOOLTIP_TITLE_Y  0.0f
#define TOOLTIP_BODY_Y   21.0f
#define TOOLTIP_INDENT   3.0f
/* The padding a Flash text field keeps inside its border. It is what makes
   the title block eighteen pixels tall against its sixteen-pixel line, and
   the original leaves three pixels of screen between the title and the body
   below it -- the gap belongs there, it was just twice the size it should be
   while the block was measured as the bare line. */
#define TOOLTIP_GUTTER   2.0f
#define TOOLTIP_FLIP_X   570.0f   /* past this the box goes left of the pointer */
#define TOOLTIP_NEAR_X   13.0f
#define TOOLTIP_FAR_X    (-183.0f)
#define TOOLTIP_RISE_Y   500.0f
#define TOOLTIP_RISE     (-50.0f)
/* The item form is wider, and flips further left. */
#define TOOLTIP_ITEM_WIDTH 200.0f
#define TOOLTIP_ITEM_FAR_X (-213.0f)
/* The grey the title sits on before the item's rarity is added to it. */
#define TOOLTIP_TITLE_BACKING ((Color){219, 219, 219, 217})
/* The three inks the item form uses: the requirement line, the attributes it
   adds, and what it says about itself. */
#define TOOLTIP_REQ_INK   ((Color){0x33, 0x33, 0x33, 255})
#define TOOLTIP_STAT_INK  ((Color){0xF8, 0xD7, 0x54, 255})
#define TOOLTIP_SAY_INK   ((Color){0xB8, 0xFE, 0x4E, 255})

void game_tooltip(Game *g, const char *title, const char *body)
{
    snprintf(g->tip_title, sizeof(g->tip_title), "%s", title ? title : "");
    snprintf(g->tip_body, sizeof(g->tip_body), "%s", body ? body : "");
    g->tip_req[0] = 0;
    g->tip_line_count = 0;
    g->tip_tint = TOOLTIP_TITLE_BACKING;
}

/* What the title's backing is tinted by: the item's rarity, as an offset
   added to the backing's own grey. */
static Color rarity_tint(const char *rarity)
{
    static const struct { const char *name; int r, g, b; } RARITY[] = {
        {"Common", 0, 0, 0}, {"Uncommon", -62, -51, 0},
        {"Rare", 86, 0, -164}, {"Unique", -67, 0, -159},
    };
    Color c = TOOLTIP_TITLE_BACKING;
    for (size_t i = 0; rarity && i < sizeof(RARITY) / sizeof(RARITY[0]); i++) {
        if (strcmp(rarity, RARITY[i].name) != 0)
            continue;
        int r = c.r + RARITY[i].r, g = c.g + RARITY[i].g;
        int b = c.b + RARITY[i].b;
        c.r = (unsigned char)(r < 0 ? 0 : r > 255 ? 255 : r);
        c.g = (unsigned char)(g < 0 ? 0 : g > 255 ? 255 : g);
        c.b = (unsigned char)(b < 0 ? 0 : b > 255 ? 255 : b);
        break;
    }
    return c;
}

/* Another line under the tooltip's body. The talent tree uses it for what a
   move costs and for what the next tier of it would do. */
void game_tooltip_line(Game *g, const char *text)
{
    if (g->tip_line_count >= TOOLTIP_LINES)
        return;
    snprintf(g->tip_lines[g->tip_line_count], sizeof(g->tip_lines[0]), "%s",
             text ? text : "");
    g->tip_line_count++;
}

static void tip_line(Game *g, const char *text)
{
    if (g->tip_line_count >= TOOLTIP_LINES)
        return;
    snprintf(g->tip_lines[g->tip_line_count], sizeof(g->tip_lines[0]), "%s",
             text);
    g->tip_line_count++;
}

void game_tooltip_item(Game *g, const ItemDef *item, int32_t price)
{
    if (!item) {
        game_tooltip(g, "", "");
        return;
    }
    /* A shop puts what it costs in front of what it is. */
    if (price > 0)
        game_tooltip(g, TextFormat("%s%d - %s", EURO, price, item->name),
                     item->tooltip);
    else
        game_tooltip(g, item->name, item->tooltip);
    g->tip_tint = rarity_tint(item->rarity);
    if (item->slot < 2)
        return;             /* not equipment: the plain two-block tooltip */

    /* "Lvl. N <Class> <what it goes on>". */
    const char *klass = item->class_req > 0
                      ? TextFormat(" %s", lang_text("CLASS",
                                                    item->class_req - 1))
                      : "";
    snprintf(g->tip_req, sizeof(g->tip_req), "%s%d%s %s", lang_text("MENU", 0),
             item->level_req, klass, lang_text("ITEMSS", item->slot - 2));

    /* Every attribute it adds, then piercing, then defense -- and each list
       is walked backwards, because that is the order ActionScript's for..in
       hands an array's indices back. */
    for (int32_t i = 4; i >= 0; i--)
        if (item->stat[i] > 0)
            tip_line(g, TextFormat("%s +%d", lang_text("SYSTEM", i),
                                   (int32_t)item->stat[i]));
    for (int32_t i = SONNY_ELEMENTS - 1; i >= 0; i--)
        if (item->per[i] > 0)
            tip_line(g, TextFormat("%s %s +%d", lang_text("ELEMENTS", i),
                                   lang_text("SYSTEM", 5),
                                   (int32_t)item->per[i]));
    for (int32_t i = SONNY_ELEMENTS - 1; i >= 0; i--)
        if (item->def[i] > 0)
            tip_line(g, TextFormat("%s %s +%d", lang_text("ELEMENTS", i),
                                   lang_text("SYSTEM", 6),
                                   (int32_t)item->def[i]));
}

/* One of the tooltip's two blocks: the text wrapped to the box's width on its
   own backing, which is only as big as the text turned out to be. A line
   takes whole words while they fit, and always takes at least one. */
static float tip_block(const char *text, float x, float y, float width,
                       Color backing, Color ink, int bold)
{
    int face = bold ? UI_FACE_BOLD : UI_FACE_SANS;
    TextMeasure m = measure_begin(face, TOOLTIP_SIZE);
    float room = width - TOOLTIP_INDENT * 2;
    /* Where each line starts and stops, worked out once: the backing is
       stretched to the text, not the other way round, so the text has to be
       laid out before anything is drawn. */
    enum { MAX_LINES = 32 };
    int from[MAX_LINES], to[MAX_LINES];
    int lines = 0;
    int start = 0;
    while (text[start] && lines < MAX_LINES) {
        int last_fit = 0;
        float pen = 0.0f;
        for (int i = start; ; ) {
            if (text[i] == ' ' || text[i] == 0) {
                float measured = m.metrics ? pen
                                           : measure_span(&m, text, start, i);
                if (measured <= room || !last_fit)
                    last_fit = i;
                else
                    break;
                if (text[i] == 0)
                    break;
            }
            int step = 0;
            int codepoint = GetCodepointNext(text + i, &step);
            if (m.metrics)
                pen += measure_glyph(&m, codepoint);
            i += step;
        }
        from[lines] = start;
        to[lines] = last_fit;
        lines++;
        start = text[last_fit] ? last_fit + 1 : last_fit;
    }

    /* A Flash text field keeps a gutter inside its border, which is what
       makes a single line of twelve-point _sans twenty-one pixels tall
       against a sixteen-pixel line -- and twenty-one is exactly where the
       clip puts the body, so the two backings meet. Leaving the gutter off
       left a gap between the title and the body with the screen showing
       through it. */
    float height = lines * (TOOLTIP_SIZE + 4.0f) + TOOLTIP_GUTTER;
    if (backing.a) {
        /* The width is the one the field was created at. autoSize only grows
           a field downwards once wordWrap is on -- it does not pull the sides
           in -- so both blocks stay as wide as each other, which is how the
           original looks. */
        DrawRectangleRec((Rectangle){x, y, width, height}, backing);
        DrawRectangleLinesEx((Rectangle){x, y, width, height}, 1.0f,
                             (Color){0, 0, 0, 255});
    }
    if (ink.a)
        for (int i = 0; i < lines; i++)
            draw_span(face, text, from[i], to[i], x + TOOLTIP_INDENT,
                      y + i * (TOOLTIP_SIZE + 4.0f) + TOOLTIP_GUTTER / 2.0f,
                      TOOLTIP_SIZE, ink);
    return height;
}

void game_draw_tooltip(const Game *g, Vector2 mouse)
{
    /* What the pointer is carrying rides in the tooltip's own two holders:
       an item's picture, or the orb of an ability on its way to the bar. */
    if (g->carried_item != 0) {
        const ItemDef *item = item_by_id(g->carried_item);
        if (item)
            asset_draw_placed(item->name, 1, mouse, 1.0f, WHITE);
    }
    if (g->carrying != 0) {
        const AbilityDef *a = ability_by_id(g->carrying);
        if (a)
            draw_orb(a->icon, mouse, 1.0f, 0, 0);
    }
    if (!g->tip_title[0] && !g->tip_body[0])
        return;
    float sx = (mouse.x < TOOLTIP_FLIP_X) ? TOOLTIP_NEAR_X : TOOLTIP_FAR_X;
    float sy = (mouse.y > TOOLTIP_RISE_Y) ? TOOLTIP_RISE : 0.0f;
    float x = mouse.x + sx;
    /* The title sits on a light backing, the body under it on a dark one --
       the two shapes the clip keeps behind its fields. */
    if (!g->tip_req[0]) {
        if (g->tip_title[0])
            tip_block(g->tip_title, x, mouse.y + sy + TOOLTIP_TITLE_Y,
                      TOOLTIP_WIDTH, g->tip_tint, (Color){0, 0, 0, 255}, 1);
        float at = mouse.y + sy + TOOLTIP_BODY_Y;
        if (g->tip_body[0])
            at += tip_block(g->tip_body, x, at, TOOLTIP_WIDTH,
                            (Color){0, 0, 0, 230},
                            (Color){255, 255, 255, 255}, 0);
        /* Anything else the tooltip was given goes under the body on its own
           backing: on a node of the talent tree, what the next tier of the
           move would do and the level it wants. */
        /* The clip chains each extra field two pixels under the one above
           it: previousOne._y + previousOne._height + 2. */
        for (int32_t i = 0; i < g->tip_line_count; i++)
            at += tip_block(g->tip_lines[i], x, at, TOOLTIP_WIDTH,
                            (Color){0, 0, 0, 230},
                            (Color){255, 255, 255, 255}, 0) + 2.0f;
        return;
    }

    /* An item. A wider box, flipping further left, with the requirement line
       on its own light backing and the attributes and the description on one
       dark one below it. */
    x = mouse.x + ((mouse.x < TOOLTIP_FLIP_X) ? TOOLTIP_NEAR_X
                                              : TOOLTIP_ITEM_FAR_X);
    float top = mouse.y + sy;
    float at = top + TOOLTIP_BODY_Y;
    float req = tip_block(g->tip_req, x, at, TOOLTIP_ITEM_WIDTH,
                          TOOLTIP_TITLE_BACKING, TOOLTIP_REQ_INK, 0);
    at += req + 2.0f;

    /* Measure the block under it, so its backing can be drawn first. */
    float body = 0;
    for (int32_t i = 0; i < g->tip_line_count; i++)
        body += TOOLTIP_SIZE + 4.0f + 2.0f;
    float say = g->tip_body[0]
              ? tip_block(g->tip_body, x, at + body, TOOLTIP_ITEM_WIDTH,
                          BLANK, BLANK, 0) : 0;
    DrawRectangleRec((Rectangle){x, at, TOOLTIP_ITEM_WIDTH, body + say},
                     (Color){0, 0, 0, 230});
    DrawRectangleLinesEx((Rectangle){x, at, TOOLTIP_ITEM_WIDTH, body + say},
                         1.0f, (Color){0, 0, 0, 255});
    for (int32_t i = 0; i < g->tip_line_count; i++) {
        ui_sans_text(g->tip_lines[i], x + TOOLTIP_INDENT, at, TOOLTIP_SIZE,
                     TOOLTIP_STAT_INK);
        at += TOOLTIP_SIZE + 4.0f + 2.0f;
    }
    if (g->tip_body[0])
        tip_block(g->tip_body, x, at, TOOLTIP_ITEM_WIDTH, BLANK,
                  TOOLTIP_SAY_INK, 0);
    /* The name goes on last, over the top of it all. */
    tip_block(g->tip_title, x, top + TOOLTIP_TITLE_Y, TOOLTIP_ITEM_WIDTH,
              g->tip_tint, (Color){0, 0, 0, 255}, 1);
}
