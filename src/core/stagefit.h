/* Fitting the stage into the window.
 *
 * The stage is the original's own 800 by 575 and every coordinate in the game
 * is in it. That is an authored size, not a window size -- Flash scales the
 * stage to whatever the player is given and letterboxes to keep its shape --
 * so the window is free and the stage is fitted into it. The drawing and the
 * pointer both go through this, which is what keeps a click landing where it
 * looks like it landed.
 */
#ifndef SONNY_STAGEFIT_H
#define SONNY_STAGEFIT_H

#define SONNY_STAGE_W 800
#define SONNY_STAGE_H 575

typedef struct {
    float scale;
    float x, y;        /* where the stage's top-left lands in the window */
    int   w, h;        /* and how big it is there, in whole pixels */
} StageFit;

StageFit stage_fit(int window_w, int window_h);

#endif
