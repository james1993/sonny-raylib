/* Small shared pieces every screen uses: the stage-space mouse, panels,
   buttons, and the two message lines. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

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
