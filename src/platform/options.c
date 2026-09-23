#include <stdlib.h>
#include <string.h>
#include "options.h"

Options OPTIONS;

static const char *env(const char *name)
{
    const char *value = getenv(name);
    return (value && value[0]) ? value : NULL;
}

static int env_int(const char *name, int fallback)
{
    const char *value = env(name);
    return value ? atoi(value) : fallback;
}

void options_load(int argc, char **argv)
{
    memset(&OPTIONS, 0, sizeof(OPTIONS));
    const char *seed = argc > 1 ? argv[1] : env("SONNY_SEED");
    if (seed) {
        OPTIONS.seed = strtoull(seed, NULL, 10);
        OPTIONS.seeded = 1;
    }
    const char *window = env("SONNY_WINDOW");
    if (window && strchr(window, 'x')) {
        OPTIONS.window_w = atoi(window);
        OPTIONS.window_h = atoi(strchr(window, 'x') + 1);
    }
    OPTIONS.assets = env("SONNY_ASSETS") ? env("SONNY_ASSETS") : ".";
    OPTIONS.saves = env("SONNY_SAVES") ? env("SONNY_SAVES") : OPTIONS.assets;
    OPTIONS.silent = env("SONNY_SILENT") != NULL;
    OPTIONS.info = env("SONNY_INFO") != NULL;
    OPTIONS.trace = env("SONNY_TRACE") != NULL;
    OPTIONS.frame_log = env("SONNY_FRAMES");

    OPTIONS.shot = env("SONNY_SHOT");
    OPTIONS.steps = env_int("SONNY_STEPS", 0);
    OPTIONS.every = env_int("SONNY_SHOT_EVERY", 0);
    OPTIONS.screen = env("SONNY_SCREEN");
    OPTIONS.battle = env_int("SONNY_BATTLE", -1);
    OPTIONS.progress = env_int("SONNY_PROGRESS", -1);
    OPTIONS.clicks = env("SONNY_CLICKS");
    OPTIONS.hover = env_int("SONNY_HOVER", -1);
    const char *parked = env("SONNY_MOUSE");
    if (parked && strchr(parked, ':')) {
        OPTIONS.parked = 1;
        OPTIONS.mouse = (Vector2){(float)atof(parked),
                                  (float)atof(strchr(parked, ':') + 1)};
    }
}

int options_click(int frame, Vector2 *at)
{
    /* "frame:x:y" triples, comma separated; each fires on its own frame. */
    for (const char *p = OPTIONS.clicks; p && *p;) {
        int when = atoi(p);
        const char *x = strchr(p, ':');
        const char *y = x ? strchr(x + 1, ':') : NULL;
        if (!x || !y)
            break;
        if (when == frame) {
            *at = (Vector2){(float)atof(x + 1), (float)atof(y + 1)};
            return 1;
        }
        const char *next = strchr(y + 1, ',');
        p = next ? next + 1 : NULL;
    }
    return 0;
}
