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
   than the box the SWF declares: the art fills w by h of it at one pixel to
   the unit, and the last row and column are padding. So the image has to be
   drawn at its own pixel size and the padding allowed to fall outside the
   box. Squeezing 61 pixels into 60 is less than a pixel, but it is a pixel
   taken off the corner of every icon on the screen, and it shows. */
Rectangle placed_texture(const Texture2D *tex, float x, float y,
                         float scale_x, float scale_y, float ox, float oy)
{
    return placed_rect(x, y, scale_x, scale_y, (float)tex->width,
                       (float)tex->height, ox, oy);
}

void draw_texture_placed(const Texture2D *tex, float x, float y,
                         float scale_x, float scale_y, float ox, float oy,
                         Color tint)
{
    DrawTexturePro(*tex, (Rectangle){0, 0, (float)tex->width,
                                     (float)tex->height},
                   placed_texture(tex, x, y, scale_x, scale_y, ox, oy),
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
        draw_texture_placed(tex, moved.x + part->x, moved.y + part->y,
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
    /* Back out of the window and into the stage, undoing the same fit the
       frame is drawn with -- whichever side the letterbox falls on. */
    Vector2 m = GetMousePosition();
    StageFit fit = stage_fit(GetScreenWidth(), GetScreenHeight());
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
    return over && ui_clicked();
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
    const Texture2D *tex = asset_texture(TextFormat("#%d", part->character), 1);
    if (!tex)
        return;
    float sx = part->scale_x * scale;
    float sy = part->scale_y * scale;
    Rectangle dst = {centre.x + (part->x - part->origin_x * part->scale_x)
                     * scale,
                     centre.y + (part->y - part->origin_y * part->scale_y)
                     * scale,
                     part->width * sx, part->height * sy};
    DrawTexturePro(*tex, (Rectangle){0, 0, (float)tex->width,
                                     (float)tex->height},
                   dst, (Vector2){0, 0}, 0.0f, tint);
}

/* The orb clip cuts the icon to the ball with a circular mask, and the icons
   are drawn larger than the ball -- without the cut they show their corners.
   raylib has no masking, so an icon is combined with the mask the first time
   it is drawn and the cut copy is what every orb shows afterwards. */
#define ORB_ICON_CACHE 64

static struct {
    char      name[48];
    Texture2D texture;
    int       ok;
} orb_cut[ORB_ICON_CACHE];
static int32_t orb_cut_count;

static const Texture2D *orb_icon_cut(const char *icon)
{
    for (int32_t i = 0; i < orb_cut_count; i++)
        if (strcmp(orb_cut[i].name, icon) == 0)
            return orb_cut[i].ok ? &orb_cut[i].texture : NULL;
    if (orb_cut_count >= ORB_ICON_CACHE)
        return NULL;

    int32_t slot = orb_cut_count++;
    snprintf(orb_cut[slot].name, sizeof(orb_cut[slot].name), "%s", icon);
    orb_cut[slot].ok = 0;

    const OrbPart *mask = orb_part("mask");
    const char *art = TextFormat("%s%s", ORB_ICON_PREFIX, icon);
    const Texture2D *icon_tex = asset_texture(art, 1);
    const Texture2D *mask_tex = mask
        ? asset_texture(TextFormat("#%d", mask->character), 1) : NULL;
    if (!icon_tex || !mask_tex)
        return NULL;

    Vector2 icon_origin = asset_frame_offset(art, 1);
    Vector2 mask_origin = asset_frame_offset(TextFormat("#%d",
                                                        mask->character), 1);
    Image icon_img = LoadImageFromTexture(*icon_tex);
    Image mask_img = LoadImageFromTexture(*mask_tex);
    Color *icon_px = LoadImageColors(icon_img);
    Color *mask_px = LoadImageColors(mask_img);
    Color *out_px = (Color *)MemAlloc((unsigned)(mask_img.width
                                                 * mask_img.height)
                                      * sizeof(Color));
    /* The icon's own origin goes on the mask's, which is how the orb places
       the two over each other. */
    int shift_x = (int)(mask_origin.x - icon_origin.x + 0.5f);
    int shift_y = (int)(mask_origin.y - icon_origin.y + 0.5f);
    for (int y = 0; y < mask_img.height; y++) {
        for (int x = 0; x < mask_img.width; x++) {
            int sx = x - shift_x, sy = y - shift_y;
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
        const Texture2D *tex = asset_texture(TextFormat("#%d", p->character),
                                             1);
        if (!tex)
            continue;
        draw_texture_placed(tex, centre.x + p->x * sx, centre.y + p->y * sy,
                            p->scale_x * sx, p->scale_y * sy,
                            p->origin_x, p->origin_y, tint);
    }
}

/* --------------------------------------------------------- screen chrome */

/* Pieces of a screen the game drives rather than simply draws: the zone
   scene, the menu that covers it, the tooltip and the fade, and the progress
   bar, whose width says how far through the zone the player is. Each is put
   on the stage by whatever screen owns it, so none of them is furniture. */
static int chrome_is_runtime(const char *name)
{
    static const char *const driven[] = {
        "KrinScreen", "KRINMENU", "KrinToolTipper", "KrinCombatText",
        "krinNavFadeSpeech", "@1242", "krinXbarPro",
    };
    for (size_t i = 0; i < sizeof(driven) / sizeof(driven[0]); i++)
        if (strcmp(name, driven[i]) == 0)
            return 1;
    return 0;
}

void draw_screen_chrome(const char *screen)
{
    for (int i = 0; i < SONNY_STAGE_CHROME_COUNT; i++) {
        const StageChrome *c = &SONNY_STAGE_CHROME[i];
        if (strcmp(c->screen, screen) != 0 || c->width <= 0)
            continue;
        if (chrome_is_runtime(c->name))
            continue;
        const Texture2D *tex = asset_texture(TextFormat("#%d", c->character),
                                             1);
        if (!tex)
            continue;
        draw_texture_placed(tex, c->x, c->y, c->scale_x, c->scale_y,
                            c->origin_x, c->origin_y, WHITE);
    }
}

void draw_screen_buttons(const char *screen, Vector2 mouse)
{
    for (int i = 0; i < SONNY_BUTTON_COUNT; i++) {
        const StageButton *b = &SONNY_BUTTONS[i];
        if (strcmp(b->screen, screen) != 0)
            continue;
        /* A button inside a clip the engine drives belongs to that clip, not
           to the screen: the menu's bag squares are on the hub's frame too,
           and they are only there when the menu is open. */
        if (chrome_is_runtime(b->owner))
            continue;
        /* A button shows one set of pieces at rest and another under the
           pointer, which is the only thing most of them do to say they can
           be pressed. */
        Rectangle box = {b->x, b->y, b->width, b->height};
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
        && CheckCollisionPointRec(mouse, (Rectangle){b->x, b->y, b->width,
                                                     b->height});
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

void draw_static_text(const char *screen, const char *owner)
{
    for (int i = 0; i < SONNY_TEXT_FIELD_COUNT; i++) {
        const TextField *f = &SONNY_TEXT_FIELDS[i];
        if (strcmp(f->screen, screen) != 0)
            continue;
        if (owner && strcmp(f->owner, owner) != 0)
            continue;
        /* A field the frame's script fills is the screen's to draw. */
        if (f->variable[0] || !f->text || !f->text[0])
            continue;
        char plain[512];
        strip_markup(f->text, plain, sizeof(plain));
        if (plain[0])
            draw_field_wrapped(f, (Vector2){0, 0}, plain);
    }
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
#define TOOLTIP_FLIP_X   570.0f   /* past this the box goes left of the pointer */
#define TOOLTIP_NEAR_X   13.0f
#define TOOLTIP_FAR_X    (-183.0f)
#define TOOLTIP_RISE_Y   500.0f
#define TOOLTIP_RISE     (-50.0f)

void game_tooltip(Game *g, const char *title, const char *body)
{
    snprintf(g->tip_title, sizeof(g->tip_title), "%s", title ? title : "");
    snprintf(g->tip_body, sizeof(g->tip_body), "%s", body ? body : "");
}

/* One of the tooltip's two blocks: the text wrapped to the box's width on its
   own backing, which is only as big as the text turned out to be. */
static float tip_block(const char *text, float x, float y, Color backing,
                       Color ink, int bold)
{
    char line[256];
    float height = 0;
    /* Measure first: the backing is stretched to the text, not the other way
       round. */
    for (int pass = 0; pass < 2; pass++) {
        float at = y;
        int start = 0;
        while (text[start]) {
            int take = 0, last_fit = 0;
            for (int i = start; ; i++) {
                if (text[i] == ' ' || text[i] == 0) {
                    int count = i - start;
                    if (count > (int)sizeof(line) - 1)
                        count = (int)sizeof(line) - 1;
                    memcpy(line, text + start, count);
                    line[count] = 0;
                    float measured = bold
                    ? ui_sans_bold_width(line, TOOLTIP_SIZE)
                    : ui_sans_text_width(line, TOOLTIP_SIZE);
                if (measured <= TOOLTIP_WIDTH - TOOLTIP_INDENT * 2
                        || !last_fit)
                        last_fit = i;
                    else
                        break;
                    take = i;
                    if (text[i] == 0)
                        break;
                }
            }
            take = last_fit;
            int count = take - start;
            if (count > (int)sizeof(line) - 1)
                count = (int)sizeof(line) - 1;
            memcpy(line, text + start, count);
            line[count] = 0;
            if (pass == 1) {
                if (bold)
                    ui_sans_bold_text(line, x + TOOLTIP_INDENT, at,
                                      TOOLTIP_SIZE, ink);
                else
                    ui_sans_text(line, x + TOOLTIP_INDENT, at, TOOLTIP_SIZE,
                                 ink);
            }
            at += TOOLTIP_SIZE + 4.0f;
            start = text[take] ? take + 1 : take;
        }
        height = at - y;
        if (pass == 0) {
            float width = TOOLTIP_WIDTH;
            DrawRectangleRec((Rectangle){x, y, width, height}, backing);
            DrawRectangleLinesEx((Rectangle){x, y, width, height}, 1.0f,
                                 (Color){0, 0, 0, 255});
        }
    }
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
    if (g->tip_title[0])
        tip_block(g->tip_title, x, mouse.y + sy + TOOLTIP_TITLE_Y,
                  (Color){219, 219, 219, 217}, (Color){0, 0, 0, 255}, 1);
    if (g->tip_body[0])
        tip_block(g->tip_body, x, mouse.y + sy + TOOLTIP_BODY_Y,
                  (Color){0, 0, 0, 230}, (Color){255, 255, 255, 255}, 0);
}
