/* What the game was told from outside: the seed, where its files are, and the
 * hooks a headless run drives it through.
 *
 * Every one of these is an environment variable, read once at start-up. They
 * used to be read wherever they were wanted, some of them every frame; this
 * is the one place they are named, so the full set can be seen at a glance.
 *
 *   SONNY_SEED=n          the dice, instead of the clock (also argv[1])
 *   SONNY_WINDOW=WxH      open at exactly that size
 *   SONNY_ASSETS=dir      where assets/ is, if not the working directory
 *   SONNY_SAVES=dir       where the save slots go, if not beside the assets
 *   SONNY_SILENT=1        open no audio device
 *   SONNY_INFO=1          print what the window and monitor came to
 *   SONNY_TRACE=1         print which effect clip each move plays
 *
 * and for a headless capture:
 *
 *   SONNY_SHOT=path       photograph the last frame there, then quit
 *   SONNY_STEPS=n         how many frames to run first (default 2)
 *   SONNY_SHOT_EVERY=n    photograph every nth frame instead, as path_NNNN.png
 *   SONNY_SCREEN=name     which screen to open on (see screen_by_name)
 *   SONNY_BATTLE=id       which fight SONNY_SCREEN=battle starts
 *   SONNY_PROGRESS=n      start that far into the story
 *   SONNY_CLICKS=f:x:y,.. press at stage (x, y) on frame f
 *   SONNY_MOUSE=x:y       keep the pointer there
 *   SONNY_HOVER=slot      hold the battle's pointer on a unit
 */
#ifndef SONNY_OPTIONS_H
#define SONNY_OPTIONS_H

#include <stdint.h>
#include "raylib.h"

typedef struct {
    uint64_t    seed;
    int         seeded;
    int         window_w, window_h;
    const char *assets;
    const char *saves;
    int         silent;
    int         info;
    int         trace;

    const char *shot;
    int         steps;
    int         every;
    const char *screen;
    int         battle;         /* -1: the one progress has reached */
    int         progress;       /* -1: the start of the story */
    const char *clicks;
    int         parked;
    Vector2     mouse;
    int         hover;          /* -1: wherever the pointer is */
} Options;

extern Options OPTIONS;

void options_load(int argc, char **argv);

/* Whether a synthetic press lands on this frame, and where. */
int options_click(int frame, Vector2 *at);

#endif
