/* Small shared pieces every screen uses: the stage-space mouse, panels,
   buttons, and the two message lines. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "game.h"

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

#define UI_FACE_GAME   0
#define UI_FACE_SANS   1
#define UI_FACES       2
#define UI_SIZE_CACHE  32
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
    struct { int size; Font font; Font metrics; float em; } cache[UI_SIZE_CACHE];
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
   caller's units. */
static int face_size(int face, float size)
{
    UiFace *f = &ui_faces[face];
    if (!f->ready)
        return -1;

    int px = (int)(size * f->em_to_box + 0.5f);
    if (px < 6)
        px = 6;
    for (int i = 0; i < f->cached; i++)
        if (f->cache[i].size == px)
            return i;
    if (f->cached == UI_SIZE_CACHE)
        return f->cached - 1;

    Font font = LoadFontEx(f->path, px, UI_CODEPOINTS, UI_CODEPOINT_COUNT);
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
    if (!FileExists(UI_FONT_PATH) || !face_open(UI_FACE_GAME, UI_FONT_PATH))
        TraceLog(LOG_WARNING, "UI font %s unusable; using the default",
                 UI_FONT_PATH);

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
            Rectangle dst = {
                floorf(x + pen + font->glyphs[g].offsetX + 0.5f),
                floorf(y + font->glyphs[g].offsetY + 0.5f),
                src.width, src.height};
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
    /* On the pixel grid. Flash hints its text onto whole pixels; drawn at a
       fraction the same glyphs sample across two texels each and go soft. */
    if (face_run(face, text, size, floorf(x + 0.5f), floorf(y + 0.5f), color,
                 1) >= 0.0f)
        return;
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
    if (face == UI_FACE_SANS)
        return face_width(UI_FACE_GAME, text, size);
    return (float)MeasureText(text, (int)size);
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

/* The same, wrapped to the field's width and stacked by its own line box. */
void draw_field_wrapped(const TextField *f, Vector2 clip, const char *text)
{
    if (!f || !text || !text[0])
        return;
    /* Flash stacks lines a full line box apart, not a font size apart. */
    float line_height = f->size * TEXT_LINE_FACTOR + f->leading;
    int rows = (int)(f->height / line_height);
    char line[160];
    int start = 0, row = 0;
    while (text[start] && row < rows) {
        int fit = 0, space = -1;
        for (int i = 0; text[start + i]; i++) {
            if (text[start + i] == ' ')
                space = i;
            line[i] = text[start + i];
            line[i + 1] = 0;
            if (ui_sans_text_width(line, f->size) > f->width) {
                fit = (space > 0) ? space : i;
                break;
            }
            fit = i + 1;
        }
        int count = fit;
        if (count > (int)sizeof(line) - 1)
            count = (int)sizeof(line) - 1;
        memcpy(line, text + start, count);
        line[count] = 0;
        TextField row_field = *f;
        row_field.y = f->y + row * line_height;
        draw_field(&row_field, clip, line);
        start += count;
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
                            Vector2 moved, const char *chosen, Color tint)
{
    for (int i = 0; i < SONNY_CLIP_PART_COUNT; i++) {
        const ClipPart *part = &SONNY_CLIP_PARTS[i];
        if (strcmp(part->screen, screen) != 0
            || strcmp(part->owner, owner) != 0 || part->width <= 0)
            continue;
        /* A piece the game points at a frame of by name -- the speech box's
           portrait -- is only drawn when the caller says which name. */
        if (part->frames && !chosen)
            continue;
        const char *name = part->frames ? chosen
                                        : TextFormat("#%d", part->character);
        const Texture2D *tex = asset_texture(name, 1);
        if (!tex)
            continue;
        Rectangle dst = placed_rect(moved.x + part->x, moved.y + part->y,
                                    part->scale_x, part->scale_y,
                                    part->width, part->height,
                                    part->origin_x, part->origin_y);
        DrawTexturePro(*tex, (Rectangle){0, 0, (float)tex->width,
                                         (float)tex->height},
                       dst, (Vector2){0, 0}, 0.0f, tint);
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

void game_log(Game *g, const char *fmt, ...)
{
    va_list args;
    char line[128];
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    if (g->log_count == LOG_LINES) {
        for (int i = 0; i < LOG_LINES - 1; i++)
            memcpy(g->log[i], g->log[i + 1], sizeof(g->log[0]));
        g->log_count--;
    }
    snprintf(g->log[g->log_count++], sizeof(g->log[0]), "%s", line);
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
    Vector2 m = GetMousePosition();
    float scale = (float)GetScreenHeight() / STAGE_H;
    if (scale <= 0)
        return m;
    return (Vector2){(m.x - (GetScreenWidth() - STAGE_W * scale) / 2) / scale,
                     m.y / scale};
}

int hit(Rectangle r, Vector2 p)
{
    return CheckCollisionPointRec(p, r);
}

void draw_panel(Rectangle r, const char *title)
{
    DrawRectangleRec(r, (Color){22, 24, 30, 235});
    DrawRectangleLinesEx(r, 1.0f, (Color){78, 82, 96, 255});
    if (title && title[0]) {
        DrawRectangle((int)r.x, (int)r.y, (int)r.width, 18,
                      (Color){34, 38, 48, 255});
        DrawText(title, (int)r.x + 8, (int)r.y + 4, 10,
                 (Color){225, 200, 120, 255});
    }
}

int draw_button(Rectangle r, const char *label, Vector2 mouse, int enabled)
{
    int over = enabled && hit(r, mouse);
    Color fill = !enabled ? (Color){30, 32, 38, 200}
               : over ? (Color){58, 64, 78, 255}
                      : (Color){40, 44, 54, 255};
    Color edge = over ? (Color){235, 200, 90, 255} : (Color){86, 90, 104, 255};
    Color text = enabled ? RAYWHITE : (Color){110, 112, 120, 255};

    DrawRectangleRec(r, fill);
    DrawRectangleLinesEx(r, over ? 2.0f : 1.0f, edge);
    DrawText(label, (int)r.x + 10, (int)(r.y + r.height / 2 - 5), 10, text);
    return over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}
