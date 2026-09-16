/* The battle screen: the fight itself, drawn over the zone's backdrop with the
 * characters standing where the original stands them.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assets.h"
#include "audio.h"
#include "game.h"

static int player_turn(const Game *g)
{
    const Battle *b = &g->battle;
    return b->phase == PHASE_DECLARE
        && b->units[PLAYER_SLOT].active
        && b->units[PLAYER_SLOT].teamSide == b->TeamMoveNow;
}

/* ------------------------------------------------------------------ layout */

/* The health and focus bar, where the original's root timeline places it
   (p1BAR..p6BAR): the two teams in columns near the top, ordered slot 5, 1, 3
   down the left and 6, 2, 4 down the right. The bar art is 201 x 28.75 at a
   scale of about 1.085, and the right-hand team's is mirrored, so its origin
   is its right edge. */
/* Where a unit's model stands: the original's own stage layout, so position
   follows the slot -- not the speed order, which only decides who acts when. */
static Vector2 unit_slot_pos(int32_t slot)
{
    const StageSlot *s = stage_slot(slot);
    if (s)
        return (Vector2){s->x, s->y};
    return (Vector2){STAGE_W / 2.0f, STAGE_H / 2.0f};
}

/* Where it is actually standing this frame, which is somewhere else while it
   is walking up to whatever it is about to hit. */
static Vector2 unit_stage_pos(const Game *g, int32_t slot)
{
    Vector2 at = unit_slot_pos(slot);
    if (g->melee_slot == slot) {
        at.x += g->melee_x;
        at.y += g->melee_y;
    }
    return at;
}

/* Where BATTLESCREEN's own origin sits on the stage, and where the zoom puts
   the point it is aiming at. */
#define BATTLE_ORIGIN_X 400.0f
#define BATTLE_ORIGIN_Y 294.5f

/* Where the reticle round a unit goes. It is drawn outside the battlefield's
   own transform -- the original keeps the reticles on the root, not in
   BATTLESCREEN -- so the camera has to be applied by hand to keep it on the
   unit it belongs to while the battlefield leans in. */
static Vector2 unit_marker_pos(const Game *g, int32_t slot)
{
    Vector2 at = unit_stage_pos(g, slot);
    float scale = g->camera_scale > 0 ? g->camera_scale : 1.0f;
    at.x = BATTLE_ORIGIN_X + g->camera_x + (at.x - BATTLE_ORIGIN_X) * scale;
    at.y = BATTLE_ORIGIN_Y + g->camera_y + g->shake_y
         + (at.y - BATTLE_ORIGIN_Y) * scale;
    return at;
}

/* ------------------------------------------------------------------ camera */

/* GridZoomer. A move makes the battlefield lean in on whatever it is aimed
   at, hold there while it lands, and come back out: ten steps each way, the
   step proportional to how many are left, so it starts fast and eases in.
   BATTLESCREEN is moved and scaled, which is why the bars, the reticles and
   the panels round it stay where they are.

   The numbers are the clip's own: ten steps of a ratio of ten make a factor
   of 55, the scale gains thirteen per cent over the leg, and the point it
   settles on is the target's middle taken back by the ratio the delivery
   asks for. */
#define ZOOM_TIME      10
#define ZOOM_FACTOR    55.0f          /* time * (step + ratio) / 2 */
#define ZOOM_SCALE     0.13f
#define ZOOM_MELEE     0.4f           /* zoomRatioNEW */
#define ZOOM_SHOCK     0.3f
#define ZOOM_AIM_X      400.0f
#define ZOOM_AIM_Y      300.0f
/* The aim is the target container's _x + _width / 2 and _y + _height / 2.
   Flash measures both from the clip's origin rather than from the middle of
   what the clip holds, so the point lands half a model past where the unit
   stands -- down by half its height, and along by half its width in whatever
   direction the container is turned, which for the right-hand team is back
   towards the middle. The model is 61 by 100; the numbers a zoomed frame of
   the original fits are within a pixel of that. */
#define UNIT_HALF_W     30.5f
#define UNIT_HALF_H     50.0f

/* Start a lean-in on `slot`. `hold` is how many frames to stay at the top. */
static void camera_aim(Game *g, int32_t slot, float ratio, int32_t hold)
{
    const StageSlot *s = stage_slot(slot);
    Vector2 at = unit_slot_pos(slot);
    float aim_x = at.x - BATTLE_ORIGIN_X
                + ((s && s->flip) ? -UNIT_HALF_W : UNIT_HALF_W);
    float aim_y = at.y - BATTLE_ORIGIN_Y + UNIT_HALF_H;
    float to_x = ZOOM_AIM_X - ratio * aim_x - BATTLE_ORIGIN_X;
    float to_y = ZOOM_AIM_Y - ratio * aim_y - BATTLE_ORIGIN_Y;
    g->zoom_step_x = (to_x - g->camera_x) / ZOOM_FACTOR;
    g->zoom_step_y = (to_y - g->camera_y) / ZOOM_FACTOR;
    g->zoom_step_scale = ZOOM_SCALE / ZOOM_FACTOR;
    g->zoom_point = ZOOM_TIME;
    g->zoom_way = 1;
    g->zoom_hold = hold;
    g->zoom_held = 0;
}

static void camera_tick(Game *g)
{
    if (g->zoom_point <= 0) {
        if (g->zoom_way >= 0)
            return;
        /* Holding at the top, counting down to the way back. */
        if (++g->zoom_held < g->zoom_hold)
            return;
        g->zoom_point = ZOOM_TIME;
        return;
    }
    g->camera_x += g->zoom_step_x * g->zoom_point * g->zoom_way;
    g->camera_y += g->zoom_step_y * g->zoom_point * g->zoom_way;
    g->camera_scale += g->zoom_step_scale * g->zoom_point * g->zoom_way;
    if (--g->zoom_point > 0)
        return;
    if (g->zoom_way == 1) {
        g->zoom_way = -1;
        g->zoom_held = 0;
        return;
    }
    /* Back where it started, exactly rather than nearly. */
    g->camera_x = g->camera_y = 0.0f;
    g->camera_scale = 1.0f;
    g->zoom_way = 0;
}

static Camera2D battle_camera(const Game *g)
{
    Camera2D camera = {0};
    camera.target = (Vector2){BATTLE_ORIGIN_X, BATTLE_ORIGIN_Y};
    camera.offset = (Vector2){BATTLE_ORIGIN_X + g->camera_x,
                              BATTLE_ORIGIN_Y + g->camera_y + g->shake_y};
    camera.zoom = g->camera_scale > 0 ? g->camera_scale : 1.0f;
    return camera;
}

/* ------------------------------------------------------------------- melee */

/* krinMelee. A melee attacker walks up to whoever it is hitting, stopping
   where the two models meet, swings, waits, and walks back. The pace is the
   original's: a step of a sixtieth of the way there times ten, eased by how
   far along it is -- flat out for the first third, then slowing into the
   target, and the other way round on the way home. */
#define MELEE_BOLT_TIME   60.0f
#define MELEE_BODY_MOVE   10.0f
#define MELEE_SWING       15      /* krinMeleeAttackCD */
#define MELEE_FOLLOW      15      /* krinMeleeAttackEndCD */
#define MELEE_GIVE_UP     120
/* Where the two models come to rest against each other. */
#define MELEE_REACH       (UNIT_HALF_W * 2.0f)

static void melee_start(Game *g, int32_t caster, int32_t target)
{
    Vector2 from = unit_slot_pos(caster);
    Vector2 to = unit_slot_pos(target);
    /* A move aimed at the caster's own square has nowhere to walk to, and
       the pace is a fraction of the distance, so there would be no step to
       take and nothing to end the walk. */
    if (caster == target || from.x == to.x) {
        g->melee_slot = 0;
        g->melee_dir = 0;
        g->melee_state = 0;
        g->melee_x = g->melee_y = 0.0f;
        return;
    }
    float end = from.x < to.x ? to.x - MELEE_REACH : to.x + MELEE_REACH;
    g->melee_slot = caster;
    g->melee_dir = 1;
    g->melee_state = 0;
    g->melee_counter = 0;
    g->melee_frame = 0;
    g->melee_x = g->melee_y = 0.0f;
    g->melee_rel = 0.0f;
    g->melee_span = end - from.x;
    g->melee_facing = from.x < to.x ? 1.0f : -1.0f;
    g->melee_step_x = (to.x - from.x) / MELEE_BOLT_TIME;
    g->melee_step_y = (to.y - from.y) / MELEE_BOLT_TIME;
}

/* True while the swing has still to land, which is what holds the move. */
static int melee_busy(const Game *g)
{
    return g->melee_slot > 0 && (g->melee_dir == 1 || g->melee_state == 1);
}

static int melee_tick(Game *g)
{
    if (g->melee_slot <= 0)
        return 0;
    g->melee_frame++;
    if (g->melee_state == 1) {
        if (++g->melee_counter >= MELEE_SWING) {
            g->melee_state = 2;
            g->melee_counter = 0;
            /* attack1 and attack2 are the two halves of one swing and sit
               back to back on the model's timeline with nothing between
               them, so the original's playhead simply runs on from 92 to 93.
               Restarting the count here is how that reads from this side. */
            g->melee_frame = 0;
        }
    } else if (g->melee_state == 2) {
        if (++g->melee_counter >= MELEE_FOLLOW) {
            g->melee_state = 0;
            g->melee_dir = -1;
            g->melee_frame = 0;
        }
    }

    float along = g->melee_span != 0 ? g->melee_rel / g->melee_span : 1.0f;
    float pace = g->melee_dir == 1 ? 1.5f - 1.45f * along : 1.45f * along;
    if (pace > 1.0f)
        pace = 1.0f;
    float step = pace * MELEE_BODY_MOVE * g->melee_dir;
    g->melee_x += step * g->melee_step_x;
    g->melee_rel += step * g->melee_step_x;
    g->melee_y += step * g->melee_step_y;

    if (g->melee_dir == -1 && g->melee_rel * g->melee_facing <= 0.000001f) {
        g->melee_x = g->melee_y = 0.0f;
        g->melee_slot = 0;
        g->melee_dir = 0;
        return 0;
    }
    /* Arrival is judged on where the step just left it, not where it was.
       The pace never quite reaches zero, but a walk that has not arrived in
       four times as long as the longest one takes is swung anyway rather
       than left to hold up the fight. */
    along = g->melee_span != 0 ? g->melee_rel / g->melee_span : 1.0f;
    if (g->melee_dir == 1 && (along >= 1.0f || g->melee_frame > MELEE_GIVE_UP)) {
        g->melee_dir = 0;
        g->melee_state = 1;
        g->melee_counter = 0;
        g->melee_frame = 0;
    }
    return g->melee_dir == 1 || g->melee_state == 1;
}

/* ------------------------------------------------------------------- shake */

/* GridShaker. A blow that pierces -- and every shock -- bounces the
   battlefield: the clip throws it by half the current value, then down by the
   whole of it, then up again, taking a tenth off the throw each cycle until
   it has nothing left. Only BATTLESCREEN moves, as with the lean. */
#define SHAKE_THROW  8.0f
#define SHAKE_DAMP   (SHAKE_THROW / 10.0f)

static void shake_start(Game *g)
{
    g->shake_value = SHAKE_THROW;
    g->shake_phase = 1;
    g->shake_y = 0.0f;
}

static void shake_tick(Game *g)
{
    if (g->shake_phase <= 0)
        return;
    switch (g->shake_phase) {
    case 1:                                   /* the clip's frame two */
        g->shake_y += g->shake_value / 2.0f;
        g->shake_phase = 2;
        break;
    case 2:                                   /* frame three does nothing */
        g->shake_phase = 3;
        break;
    case 3:                                   /* frame four */
        g->shake_y -= g->shake_value;
        g->shake_value -= SHAKE_DAMP;
        g->shake_phase = 4;
        break;
    default:                                  /* frame five */
        g->shake_y += g->shake_value;
        g->shake_value -= SHAKE_DAMP;
        if (g->shake_value > 0) {
            g->shake_phase = 1;
        } else {
            g->shake_phase = 0;
            g->shake_y = 0.0f;
        }
        break;
    }
}

/* -------------------------------------------------------------------- bolt */

/* krinBoltMake. A missile throws a projectile from the caster to the target:
   it fades in, is turned to face the way it is going, and accelerates by a
   sixth every frame until it reaches whoever it was aimed at -- which is when
   the move lands and the impact graphic goes up. Melee has the attacker walk
   over instead; this is the same idea for the moves that stay put. */
#define BOLT_TIME      60.0f     /* krinBoltTime */
#define BOLT_SPEED     1.0f      /* krinBoltSpeed */
#define BOLT_INCREASE  1.15f     /* krinBoltIncrease */
#define BOLT_RISE      15.0f     /* it leaves from the caster's chest */
#define BOLT_FADE      10        /* alpha a frame, out of a hundred */
#define BOLT_GIVE_UP   240

/* KrinTrail. The streak is one small shape, ten units long, whose art runs
   from clear at the tail to solid at the head; the clip is dropped where the
   bolt has got to on its first frame, turned the way the bolt is going, and
   from then on stretched along its own x so the solid head keeps pace while
   the clear tail stays at the throw. Its own clip runs thirty-three frames
   whatever the bolt does -- in from nothing over the first nine, out again
   over the last nine -- and takes itself off the screen at the end, so a
   trail is still fading after its bolt has landed. */
#define TRAIL_ART    "#144"
#define TRAIL_GROW   8.3f      /* per cent of its length per unit travelled */
#define TRAIL_FRAMES 33
#define TRAIL_IN     9         /* solid by here */
#define TRAIL_OUT    24        /* and fading from here */
#define TRAIL_PLACED_X 5.0f    /* where the clip puts the shape */

/* The colour is already on the game from bolt_start, which is where the
   move that threw this is still to hand. */
static void trail_start(Game *g)
{
    g->trail_tick = 1;
    g->trail_x = g->bolt_x;
    g->trail_y = g->bolt_y;
    g->trail_angle = g->bolt_angle;
    g->trail_scale = 100.0f;
}

/* The original grows the streak by the distance the bolt covered this frame,
   which is the step it takes times the speed it has wound up to. */
static void trail_grow(Game *g)
{
    if (g->trail_tick <= 0)
        return;
    float step = sqrtf(g->bolt_step_x * g->bolt_step_x
                       + g->bolt_step_y * g->bolt_step_y);
    g->trail_scale += TRAIL_GROW * step * g->bolt_speed;
}

static void trail_tick_on(Game *g)
{
    if (g->trail_tick <= 0)
        return;
    if (++g->trail_tick > TRAIL_FRAMES)
        g->trail_tick = 0;          /* frame 33 unloads it */
}

static void draw_trail(const Game *g)
{
    if (g->trail_tick <= 0)
        return;
    /* The art is already white, carrying only the fade along its length,
       so the move's own colour goes straight on as a tint -- which is what
       Color.setRGB on the clip amounts to. */
    const Texture2D *tex = asset_texture(TRAIL_ART, 1);
    if (!tex)
        return;
    /* The clip's cxform: nothing on its first frame, solid from its ninth,
       and back down to nothing by its thirty-second. */
    float a = 1.0f;
    if (g->trail_tick < TRAIL_IN)
        a = (float)(g->trail_tick - 1) / (float)(TRAIL_IN - 1);
    else if (g->trail_tick >= TRAIL_OUT)
        a = (float)(TRAIL_FRAMES - 1 - g->trail_tick)
            / (float)(TRAIL_FRAMES - 1 - TRAIL_OUT + 1);
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    Color tint = {g->trail_colour.r, g->trail_colour.g, g->trail_colour.b,
                  (unsigned char)(a * 255.0f)};
    /* The shape is placed five units along inside the clip and its own
       origin is its middle, so its left edge -- the clear end -- sits exactly
       on the clip's origin. Stretching along x therefore leaves the tail at
       the throw and carries the solid head away from it, which is the whole
       point of the effect; the pivot is that same origin, since the clip is
       turned about it. Only _xscale moves, so the height is left alone. */
    Vector2 off = asset_frame_offset(TRAIL_ART, 1);
    float s = g->trail_scale / 100.0f;
    Rectangle src = {0, 0, (float)tex->width, (float)tex->height};
    Rectangle dst = {g->trail_x, g->trail_y, (float)tex->width * s,
                     (float)tex->height};
    Vector2 pivot = {(off.x - TRAIL_PLACED_X) * s, off.y};
    DrawTexturePro(*tex, src, dst, pivot, g->trail_angle, tint);
}

static void bolt_start(Game *g, const AbilityDef *a, int32_t caster,
                       int32_t target)
{
    g->bolt = NULL;
    g->trail_tick = 0;
    if (!a || !a->projectile || !a->projectile[0] || asset_frame_count(a->projectile) <= 0)
        return;
    Vector2 from = unit_slot_pos(caster);
    Vector2 to = unit_slot_pos(target);
    if (caster == target)
        return;
    g->bolt = a->projectile;
    g->bolt_target = target;
    g->bolt_x = from.x;
    g->bolt_y = from.y - BOLT_RISE;
    g->bolt_step_x = (to.x - g->bolt_x) / BOLT_TIME;
    g->bolt_step_y = (to.y - g->bolt_y) / BOLT_TIME;
    g->bolt_speed = BOLT_SPEED;
    g->bolt_facing = from.x < to.x ? 1.0f : -1.0f;
    g->bolt_tick = 0;
    /* Turned to face the target once, as it sets out. */
    float angle = atan2f(to.y - g->bolt_y, to.x - g->bolt_x);
    g->bolt_angle = angle * (180.0f / 3.14159265358979f);
    g->trail_colour = (Color){(unsigned char)((a->colour >> 16) & 0xFF),
                              (unsigned char)((a->colour >> 8) & 0xFF),
                              (unsigned char)(a->colour & 0xFF), 255};
}

static int bolt_busy(const Game *g)
{
    return g->bolt != NULL;
}

/* -> 1 while the bolt is still crossing. */
static int bolt_tick(Game *g)
{
    /* The streak runs its own thirty-three frames, so it carries on fading
       after the bolt it came from has landed and gone. */
    trail_tick_on(g);
    if (!g->bolt)
        return 0;
    g->bolt_tick++;
    Vector2 to = unit_stage_pos(g, g->bolt_target);
    g->bolt_x += g->bolt_step_x * g->bolt_speed;
    g->bolt_y += g->bolt_step_y * g->bolt_speed;
    g->bolt_speed *= BOLT_INCREASE;
    /* The original lays the streak down on the bolt's first frame, once it
       has taken that first step. */
    if (g->bolt_tick == 1)
        trail_start(g);
    /* Arrival is being level with the target, whichever way it set off --
       and a bolt that somehow never gets there gives up rather than holding
       the fight open. */
    if ((to.x - g->bolt_x) * g->bolt_facing <= 0
        || g->bolt_tick > BOLT_GIVE_UP) {
        g->bolt = NULL;
        return 0;
    }
    trail_grow(g);
    return 1;
}

static void draw_bolt(const Game *g)
{
    if (!g->bolt)
        return;
    int32_t frames = asset_frame_count(g->bolt);
    if (frames <= 0)
        return;
    int32_t frame = (g->bolt_tick % frames) + 1;
    const Texture2D *tex = asset_texture(g->bolt, frame);
    if (!tex)
        return;
    Vector2 off = asset_frame_offset(g->bolt, frame);
    /* It fades in over its first ten frames. */
    int32_t alpha = g->bolt_tick * BOLT_FADE;
    if (alpha > 100)
        alpha = 100;
    Color tint = {255, 255, 255, (unsigned char)(alpha * 255 / 100)};
    Rectangle src = {0, 0, (float)tex->width, (float)tex->height};
    Rectangle dst = {g->bolt_x, g->bolt_y, (float)tex->width,
                     (float)tex->height};
    DrawTexturePro(*tex, src, dst, off, g->bolt_angle, tint);
}

/* Which animation a unit is playing, from its state and what is resolving. */
static const char *unit_animation(const Game *g, int32_t slot, int *loop)
{
    const Battle *b = &g->battle;
    const Unit *u = &b->units[slot];

    *loop = 1;
    if (!u->active)
        return (*loop = 0, "dead");
    if (u->STUN > 0)
        return "stun";

    /* A melee attacker is on its feet: out to the target, the swing, then
       home again. */
    if (g->melee_slot == slot) {
        if (g->melee_dir == 1)
            return "run";
        if (g->melee_dir == -1)
            return "runback";
        /* The second half of the swing, where the model shows the sweep in
           the move's own colour over itself. */
        return (*loop = 0, g->melee_state == 2 ? "attack2" : "attack1");
    }

    if (b->phase == PHASE_RESOLVE && g->has_last) {
        if (g->last.caster == slot && g->last.moveID != 0) {
            *loop = 0;
            const AbilityDef *a = ability_by_id(g->last.moveID);
            if (a && a->delivery == DELIVER_MELEE)
                return "attack1";
            return "cast";
        }
        /* The recoil belongs to the moment the blow lands, not to the
           moment the numbers were worked out: a melee attacker is still
           crossing the floor until then, and flinching early made the whole
           exchange look like it had happened backwards. */
        if (!g->move_pending && g->last.target == slot && g->last.amount > 0
            && g->last.kind == KIND_FULL_DAMAGE) {
            *loop = 0;
            return "hit";
        }
    }
    return "stand";
}

/* The impact graphic an ability names, played at the unit it landed on. */
static void draw_effect(const Game *g)
{
    if (!g->effect || g->effect_slot <= 0)
        return;
    int32_t frames = asset_frame_count(g->effect);
    if (frames <= 0 || g->effect_tick >= frames)
        return;

    Vector2 pos = unit_stage_pos(g, g->effect_slot);
    asset_draw_placed(g->effect, g->effect_tick + 1, pos, 1.0f, WHITE);
}

static void draw_doll(const Game *g, int32_t slot)
{
    const Unit *u = &g->battle.units[slot];
    if (u->LIFEU == 0)
        return;

    DollSpec spec;
    memset(&spec, 0, sizeof(spec));
    spec.gender = u->model_gender;
    spec.skin = u->model_skin;
    spec.hair = u->model_hair;
    for (int i = 0; i < 7; i++)
        spec.looks[i] = u->looks[i];
    int32_t colour = g->cast_colour[slot];
    spec.cast = (Color){(unsigned char)((colour >> 16) & 0xFF),
                        (unsigned char)((colour >> 8) & 0xFF),
                        (unsigned char)(colour & 0xFF),
                        (unsigned char)(colour ? 255 : 0)};

    int loop = 1;
    const char *animation = unit_animation(g, slot, &loop);
    /* An animation that runs once is clocked from the move it belongs to; the
       standing loop just keeps going. */
    /* A walking attacker is played off its own counter, which restarts at
       each leg -- out, swing, home -- rather than off the free-running one. */
    int32_t tick = g->melee_slot == slot ? g->melee_frame
                 : loop ? g->anim_tick / 2 : g->move_tick;
    int32_t frame = doll_animation_frame(animation, tick, loop);

    /* The right-hand team's containers are mirrored in the original. */
    const StageSlot *s = stage_slot(slot);
    int flip = s ? s->flip : (u->teamSide == 2);
    Color tint = u->active ? WHITE : (Color){255, 255, 255, 150};
    doll_draw(&spec, frame, unit_stage_pos(g, slot), 1.0f, flip, tint);
}

/* The bar sits where the original's does: its pieces (the backing, the
   selected-move icon, the clock) are all placed at (400, 508) on the root
   timeline, so the eight slots are laid out symmetrically about that. */
#define BAR_CENTER_X 400.0f
#define BAR_CENTER_Y 508.0f
#define BAR_SLOT     46.0f

/* The root frame whose display list is the battle screen. */
#define BATTLE_SCREEN_NAME "KRINBATTLESCENE"

/* The gutter Flash leaves inside every text field before the text starts,
   and how much taller than its size a line box is. */
#define TEXT_GUTTER 2.0f
#define TEXT_LINE_FACTOR 1.15f

/* The turn indicator's clickable middle, which ends the turn. */
/* krinToMove2, the button in the middle of the turn indicator. */
#define PASS_BUTTON 1597

/* Where the speech box goes when it is the left team talking. */
#define SPEECH_LEFT_X 21.8f

/* The piece of the box the game points at whoever is talking. */
#define SPEECH_PORTRAIT "inner2"
/* The standing object the original keeps at slot zero: always active, on no
   side, and named for the portrait frame it shows. */
#define NARRATOR_SLOT 0
#define NARRATOR_NAME "Information"
/* And the prompt beside it, which is a clip of its own. */
#define SPEECH_SKIP_PART "@8"
#define SPEECH_SKIP      "#1639"

/* The balloon a unit pops while it is talking. Every unit's container carries
   one, placed at this offset inside it and mirrored, and the speech driver
   plays it through once -- 57 frames at the stage's own rate. */
#define SPEECH_BALLOON      "#974"
#define SPEECH_BALLOON_X   (-10.65f)
#define SPEECH_BALLOON_Y   (-64.3f)

/* The reticle clip whose art and text every unit's gets: the original parks
   one on each, and they are all the same widget. */
#define RETICLE_INSTANCE "KrinSelector1"

/* How long the reticle takes to come up, and to go back down. */
#define RETICLE_FADE_FRAMES 6.0f

/* ------------------------------------------------------------------ chrome */

/* The battle screen's furniture is not laid out here: it is the display list
   the original places on its KRINBATTLESCENE frame, drawn in the same depth
   order, each piece at the coordinate and scale the SWF gives it. The pieces
   named below are the ones the game drives at runtime and leaves hidden or
   parked off stage on a quiet turn, so drawing them from the display list
   would show furniture the original does not. */
static int chrome_is_runtime(const char *name)
{
    static const char *const hidden[] = {
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
    for (size_t i = 0; i < sizeof(hidden) / sizeof(hidden[0]); i++)
        if (strcmp(name, hidden[i]) == 0)
            return 1;
    return strncmp(name, "p", 1) == 0 && strstr(name, "BAR") != NULL;
}

static void draw_chrome_art(const StageChrome *c, int32_t tick)
{
    /* A piece that ships more than one frame is one the original leaves
       playing -- the exclamation mark under the turn indicator pulses
       through forty-eight of them -- so it runs off the free clock. */
    const char *name = TextFormat("#%d", c->character);
    int32_t frames = asset_frame_count(name);
    int32_t frame = frames > 1 ? (int32_t)(tick % frames) + 1 : 1;
    const Texture2D *tex = asset_texture(name, frame);
    if (!tex)
        return;
    Vector2 off = frames > 1 ? asset_frame_offset(name, frame)
                             : (Vector2){c->origin_x, c->origin_y};
    draw_texture_placed(tex, c->x, c->y, c->scale_x, c->scale_y,
                        off.x, off.y, WHITE);
}

/* Draw every piece of the battle screen whose depth falls in [from, to). */
static void draw_chrome(int32_t from, int32_t to, int32_t tick)
{
    for (int i = 0; i < SONNY_STAGE_CHROME_COUNT; i++) {
        const StageChrome *c = &SONNY_STAGE_CHROME[i];
        if (strcmp(c->screen, BATTLE_SCREEN_NAME) != 0)
            continue;
        if (c->depth < from || c->depth >= to || c->width <= 0)
            continue;
        if (chrome_is_runtime(c->name))
            continue;
        draw_chrome_art(c, tick);
    }
}

/* The screen's own buttons.

   A button is not a sprite and has no art to export: what it shows is a list
   of state records, so nothing draws it unless the screen goes and asks. The
   two here sit above the rest of the furniture -- the pass button in the
   middle of the turn indicator, and the quit button in the corner, whose
   dark glass plate over the red cross beneath it is what leaving this out
   turned into a flat bright square. The eight move orbs are buttons too, but
   they belong to the ring the game moves onto a unit rather than to the
   screen, and draw_ring puts those up. */
static void draw_chrome_buttons(Vector2 mouse)
{
    for (int i = 0; i < SONNY_BUTTON_COUNT; i++) {
        const StageButton *b = &SONNY_BUTTONS[i];
        if (strcmp(b->screen, BATTLE_SCREEN_NAME) != 0)
            continue;
        if (chrome_is_runtime(b->owner))
            continue;
        Rectangle box = {b->x, b->y, b->width, b->height};
        draw_button_state(b, CheckCollisionPointRec(mouse, box), WHITE);
    }
}

/* The battlefield viewport: the original masks the backdrop and the units to
   this box, which is why its art stops at the frame instead of running to the
   edges of the stage. */
static const StageChrome *battlefield_mask(void)
{
    for (int i = 0; i < SONNY_STAGE_CHROME_COUNT; i++)
        if (SONNY_STAGE_CHROME[i].clip_depth > 0
            && strcmp(SONNY_STAGE_CHROME[i].screen, BATTLE_SCREEN_NAME) == 0)
            return &SONNY_STAGE_CHROME[i];
    return NULL;
}

/* -------------------------------------------------------------------- bars */

/* The health colour the original would be showing: it points a hundred-frame
   flat-colour clip at round(percent * 100), red at the bottom through yellow
   to green at the top. */
static Color life_colour(int32_t now, int32_t max)
{
    if (SONNY_LIFE_COLOUR_COUNT <= 0)
        return (Color){116, 179, 53, 255};
    int index = (max > 0) ? (int)floorf((float)now / (float)max * 100.0f + 0.5f)
                          : 0;
    if (index < 0)
        index = 0;
    if (index >= SONNY_LIFE_COLOUR_COUNT)
        index = SONNY_LIFE_COLOUR_COUNT - 1;
    const unsigned char *c = SONNY_LIFE_COLOURS[index];
    return (Color){c[0], c[1], c[2], 255};
}

/* One bar part, drawn in the bar's own coordinates. `fill` cuts the part down
   to a fraction of its width, which is how the two fills show a value --
   the original sets _width on the same clips. `mirror` is the right-hand
   team, whose bars run the other way because the game gives their graphics a
   negative horizontal scale. `recolour`, when given, replaces the part's
   colour rather than tinting it: the life fill is one flat colour and the
   original swaps that colour outright as health drops. */
static void draw_bar_part(const BarPart *part, const StageBar *bar, int mirror,
                          float fill, const Color *recolour)
{
    const Texture2D *tex = asset_texture(TextFormat("#%d", part->character), 1);
    if (!tex)
        return;
    float left = part->x - part->origin_x * part->scale_x;
    float w = part->width * part->scale_x * fill;
    float h = part->height * part->scale_y;
    float top = part->y - part->origin_y * part->scale_y;

    float x = mirror ? bar->x - (left + w) * bar->scale
                     : bar->x + left * bar->scale;
    Rectangle dst = {x, bar->y + top * bar->scale, w * bar->scale, h * bar->scale};
    if (recolour) {
        DrawRectangleRec(dst, *recolour);
        return;
    }
    Rectangle src = {0, 0, (float)tex->width * fill, (float)tex->height};
    if (mirror)
        src.width = -src.width;
    DrawTexturePro(*tex, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
}

/* One of the bar's text fields, laid out the way the SWF lays it out: its box
   in the bar's own coordinates, its own alignment, its own colour. The
   right-hand team needs no mirroring here: the original does not flip the
   text with the widget -- it lays the fields out a second time, already
   mirrored, which is the "right" layout, so flipping it again would undo it. */
static void draw_bar_field(const BarField *f, const StageBar *bar,
                           const char *text)
{
    if (!f || !text || !text[0])
        return;
    float x0 = bar->x + f->x * bar->scale;
    float w = f->width * bar->scale;
    float size = f->size * bar->scale;
    float tw = f->device ? ui_sans_text_width(text, size)
                         : ui_text_width(text, size);
    float x = (f->align == 1) ? x0 + w - tw
            : (f->align == 2) ? x0 + (w - tw) / 2.0f
                              : x0;
    /* The field's box is taller than the line; the original's text sits at
       its top with the usual couple of pixels of leading. */
    /* The widget is placed with a different vertical scale to its horizontal
       one, so the row is found with that and only the glyph size follows the
       horizontal. Inside the field the player leaves its two-pixel gutter and
       then the field's own leading before the line starts. */
    float y = bar->y + (f->y + TEXT_GUTTER + f->leading) * bar->scale_y;
    Color c = {f->r, f->g, f->b, 255};
    if (f->device)
        ui_sans_text(text, x, y, size, c);
    else
        ui_text(text, x, y, size, c);
}

/* A unit's bar, assembled from the widget the original uses for all six:
   the black panel, the life and focus fills, the gloss over them, then the
   name and the four numbers. */
static void draw_unit_bar(const Game *g, int32_t slot)
{
    const Unit *u = &g->battle.units[slot];
    const StageBar *bar = stage_bar(slot);
    if (!bar || u->LIFEU == 0)
        return;

    /* Slots 2, 4 and 6 are the right-hand team, and the original mirrors
       their bars rather than using a second widget. */
    int mirror = (slot % 2) == 0;
    const char *side = mirror ? "right" : "left";

    /* The fight works a move out in one go, but the original does not apply
       it until the blow lands -- so while an attacker is still crossing the
       floor the bar has to read what it read before. */
    int32_t life_now = u->LIFEN;
    int32_t focus_now = u->FOCUSN;
    if (g->move_pending && g->has_last && g->last.target == slot) {
        if (g->last.kind == KIND_FULL_DAMAGE)
            life_now += g->last.amount;
        else if (g->last.kind == KIND_HEAL)
            life_now -= g->last.amount;
        else if (g->last.kind == KIND_FOCUS)
            focus_now -= g->last.amount;
    }

    float life = (u->LIFEU > 0) ? (float)life_now / (float)u->LIFEU : 0.0f;
    float focus = (u->FOCUSU > 0) ? (float)focus_now / (float)u->FOCUSU : 0.0f;
    if (life < 0) life = 0;
    if (life > 1) life = 1;
    if (focus < 0) focus = 0;
    if (focus > 1) focus = 1;

    for (int i = 0; i < SONNY_BAR_PART_COUNT; i++) {
        const BarPart *part = &SONNY_BAR_PARTS[i];
        /* The "2" parts are the lagging ghost bars the original slides down
           after a hit, and the "3" parts are the one-pixel smoother it
           stretches along the way; neither shows on a settled bar. A part
           with a clip depth is a mask, which stops the fill overflowing into
           the maximum's box rather than drawing anything. */
        if (part->clip_depth > 0
            || part->name[strlen(part->name) - 1] == '2'
            || part->name[strlen(part->name) - 1] == '3')
            continue;
        if (strcmp(part->name, "lB") == 0) {
            Color c = life_colour(life_now, u->LIFEU);
            draw_bar_part(part, bar, mirror, life, &c);
        } else if (strcmp(part->name, "fB") == 0) {
            draw_bar_part(part, bar, mirror, focus, NULL);
        } else {
            draw_bar_part(part, bar, mirror, 1.0f, NULL);
        }
    }

    draw_bar_field(bar_field(side, "name"), bar, u->name);
    draw_bar_field(bar_field(side, "lifeNow"), bar,
                   TextFormat("%d", life_now));
    draw_bar_field(bar_field(side, "lifeMax"), bar,
                   TextFormat("%d", u->LIFEU));
    draw_bar_field(bar_field(side, "focusNow"), bar,
                   TextFormat("%d", focus_now));
    draw_bar_field(bar_field(side, "focusMax"), bar,
                   TextFormat("%d", u->FOCUSU));
}

/* --------------------------------------------------------------- turn dial */

/* The lit ring at the middle of the bottom panel. Inside the turn indicator
   it is a clock: one arc held twice, the second copy turned through 180
   degrees, each masked to its own side of the dial and rotated by
   BattleTimeNow so the ring fills as the turn runs down. Turn-based play --
   which is how the game is played -- pins the timer at its limit whether the
   player is choosing or the other side is, so the ring is always whole and
   only its colour carries anything: blue while the player's team moves,
   orange while the other team does.

   Its glow is a filter on the placement rather than anything in the clip, so
   the decompiler hands back an unlit ring; tools/extract_glows.py renders the
   two lit ones instead. */
#define TURN_DIAL_PLAIN  "#1591"
#define TURN_DIAL_FRIEND "TurnRingFriend"
#define TURN_DIAL_ENEMY  "TurnRingEnemy"
/* thingerClock's own offset inside battleClocker. */
#define TURN_DIAL_X 0.4f
#define TURN_DIAL_Y (-0.1f)

/* moveSelectBoomer: a white ring that closes over the turn indicator once
   the player has chosen what to do. The clip sits stopped on an empty first
   frame and is played through once, so this runs its remaining frames and
   then leaves nothing behind. */
#define BOOMER_CLIP "#1610"

static void draw_move_boomer(const Game *g)
{
    if (g->boomer_tick < 0)
        return;
    int32_t frames = asset_frame_count(BOOMER_CLIP);
    int32_t frame = g->boomer_tick + 2;      /* frame one is the empty rest */
    if (frames <= 0 || frame > frames)
        return;
    const StageChrome *at = stage_chrome(BATTLE_SCREEN_NAME,
                                         "moveSelectBoomer");
    if (!at)
        return;
    const Texture2D *tex = asset_texture(BOOMER_CLIP, frame);
    if (!tex)
        return;
    Vector2 off = asset_frame_offset(BOOMER_CLIP, frame);
    draw_texture_placed(tex, at->x, at->y, at->scale_x, at->scale_y,
                        off.x, off.y, WHITE);
}

static void draw_turn_dial(const Game *g)
{
    const StageChrome *clock = stage_chrome(BATTLE_SCREEN_NAME,
                                            "battleClocker");
    if (!clock)
        return;
    const Texture2D *plain = asset_texture(TURN_DIAL_PLAIN, 1);
    if (plain) {
        Vector2 off = asset_frame_offset(TURN_DIAL_PLAIN, 1);
        draw_texture_placed(plain, clock->x, clock->y, clock->scale_x,
                            clock->scale_y, off.x, off.y, WHITE);
    }
    /* The clock over it only fills once the turn is actually the player's to
       spend: the loop holds BattleTimeNow at zero while a line of speech is
       running, so through the opening of a fight the dial is dark. */
    if (g->speech)
        return;
    const Battle *b = &g->battle;
    const char *name = b->units[PLAYER_SLOT].teamSide == b->TeamMoveNow
                     ? TURN_DIAL_FRIEND : TURN_DIAL_ENEMY;
    const Texture2D *tex = asset_texture(name, 1);
    if (!tex)
        return;
    /* The arc is drawn about the ring's centre, which is the origin the
       glow's own manifest entry carries. */
    Vector2 off = asset_frame_offset(name, 1);
    draw_texture_placed(tex, clock->x + TURN_DIAL_X, clock->y + TURN_DIAL_Y,
                        clock->scale_x, clock->scale_y, off.x, off.y, WHITE);
}

/* -------------------------------------------------------------------- ring */

/* The ability ring. The original does not put the player's moves on a bar
   along the bottom: hovering a unit parks a ring of eight orbs around it, one
   per slot of the loadout, and clicking an orb uses that ability on that unit.
   The ring's own scale and its eight offsets come straight out of the SWF. */
static Vector2 ring_slot_pos(const RingSlot *slot, Vector2 centre)
{
    return (Vector2){centre.x + slot->x * SONNY_RING_SCALE,
                     centre.y + slot->y * SONNY_RING_SCALE};
}

/* An orb's circle, for hit-testing. */
static float orb_radius(void)
{
    const OrbPart *ball = orb_part("ball");
    return ball ? ball->width * SONNY_RING_SCALE / 2.0f : 12.0f;
}

/* How far the ring reaches from the unit it is up around: the furthest orb,
   plus that orb's own radius. */
static float ring_radius(void)
{
    float far = 0.0f;
    for (int i = 0; i < SONNY_RING_SLOT_COUNT; i++) {
        const RingSlot *s = &SONNY_RING_SLOTS[i];
        float d = sqrtf(s->x * s->x + s->y * s->y) * SONNY_RING_SCALE;
        if (d > far)
            far = d;
    }
    return far + orb_radius();
}

/* Whether a move can be aimed at this kind of target at all: its own
   self/enemy/ally flags, which is what the original checks. */
static int move_targets(const Game *g, const AbilityDef *a, int32_t target)
{
    const Battle *b = &g->battle;
    const Unit *self = &b->units[PLAYER_SLOT];
    const Unit *t = &b->units[target];
    if (!a || a->id == 0 || !t->active)
        return 0;
    if (t->teamSide != self->teamSide)
        return a->target_enemy != 0;
    return (target == PLAYER_SLOT) ? a->target_self != 0 : a->target_ally != 0;
}

/* What the ring shows as unavailable. The original dims an orb for two
   reasons only -- the move cannot be aimed at that target, or the slot is
   still cooling down -- and leaves the cost checks to the click, where it
   says which cost is short. */
static int move_offered(const Game *g, int slot, int32_t target)
{
    const AbilityDef *a = ability_by_id(g->ability_ids[slot]);
    return move_targets(g, a, target) && g->ability_cooldown[slot] == 0;
}

/* Why a move cannot be used, in the original's own words, or NULL when it
   can. These are the checks addMoveForPlayer makes before it will take one. */
static const char *move_refusal(const Game *g, int slot, int32_t target)
{
    const AbilityDef *a = ability_by_id(g->ability_ids[slot]);
    const Unit *self = &g->battle.units[PLAYER_SLOT];
    if (!a || a->id == 0)
        return "";
    if (self->FOCUSN < a->focus_cost)
        return "You cannot use this move because you don't have enough Focus.";
    if (g->ability_cooldown[slot] != 0)
        return "This move is not ready yet.";
    if (self->LIFEN <= a->health_cost
                       + (int32_t)floorf(self->LIFEU * a->health_cost_pct + 0.5f))
        return "You cannot use this move because you don't have enough Health.";
    if (!move_targets(g, a, target))
        return "You cannot use this move on that target.";
    return NULL;
}

/* One piece of the orb, in the ring's coordinates around `centre`. */
static void draw_ring(Game *g, Vector2 mouse)
{
    if (g->ring_unit <= 0 || !player_turn(g) || g->queued)
        return;
    Vector2 centre = unit_stage_pos(g, g->ring_unit);
    for (int i = 0; i < SONNY_RING_SLOT_COUNT; i++) {
        const RingSlot *slot = &SONNY_RING_SLOTS[i];
        const AbilityDef *a = ability_by_id(g->ability_ids[slot->slot]);
        /* A slot the player has left empty shows nothing at all: the original
           hides the orb's button and marks the slot zero. */
        if (!a || a->id == 0)
            continue;
        Vector2 at = ring_slot_pos(slot, centre);
        draw_orb(a->icon, at, SONNY_RING_SCALE,
                 move_offered(g, slot->slot, g->ring_unit) ? 0 : ORB_DIM_RING,
                 g->ability_cooldown[slot->slot]);
        /* An ability says what it does under the pointer, the same way one
           on the ability screen does. */
        if (CheckCollisionPointCircle(mouse, at, orb_radius()))
            game_tooltip(g, a->name, a->tooltip);
    }
}

/* What colour a unit's reticle is. The original parks six of them, one per
   unit, and the colour is a transform on each placement: the player's has
   none at all, so it keeps the art's own white; an ally's adds
   (-102, 0, -189), which comes out green; an enemy's adds (0, -138, -159),
   which comes out red. The art is white, so adding to it and tinting it land
   on the same place. */
static Color reticle_colour(const Game *g, int32_t slot)
{
    const Unit *u = &g->battle.units[slot];
    if (slot == PLAYER_SLOT)
        return (Color){255, 255, 255, 255};
    if (u->teamSide == g->battle.units[PLAYER_SLOT].teamSide)
        return (Color){153, 255, 66, 255};
    return (Color){255, 117, 96, 255};
}

/* The reticle the original parks on every unit and shows under the pointer:
   a ring in the target's own colour, with the level above it and the name
   below, both from the clip's own two text fields. */
static void draw_reticle(const Game *g)
{
    int32_t on = g->ring_unit > 0 ? g->ring_unit : g->ring_fade_unit;
    if (on <= 0 || g->ring_fade <= 0.0f)
        return;
    const Unit *u = &g->battle.units[on];
    const StageChrome *art = stage_chrome(BATTLE_SCREEN_NAME, RETICLE_INSTANCE);
    if (!art)
        return;
    Vector2 at = unit_marker_pos(g, on);
    Color tint = reticle_colour(g, on);
    tint.a = (unsigned char)(tint.a * g->ring_fade);

    /* The clip's fields are recorded where it is parked off stage, so
       everything in it follows by the same amount it moved. */
    Vector2 moved = {at.x - art->x, at.y - art->y};
    draw_clip_parts(BATTLE_SCREEN_NAME, RETICLE_INSTANCE, moved, NULL, NULL, tint);

    /* The name is the lower of the two fields, the level the upper. */
    const TextField *name = text_field(BATTLE_SCREEN_NAME, RETICLE_INSTANCE, 0);
    const TextField *level = text_field(BATTLE_SCREEN_NAME, RETICLE_INSTANCE, 1);
    if (name && level && name->y < level->y) {
        const TextField *swap = name;
        name = level;
        level = swap;
    }
    /* The original builds this from its own text table, not a literal. */
    draw_field_tinted(level, moved,
                      TextFormat("%s%d", lang_text("MENU", 0), u->plevel),
                      tint);
    draw_field_tinted(name, moved, u->name, tint);
}

/* The frame-rate readout the original keeps in the corner of the
   battlefield: a label and a number, two fields of its own. */
static void draw_frame_rate(void)
{
    draw_field(text_field(BATTLE_SCREEN_NAME, "@535", 0), (Vector2){0, 0}, "FPS:");
    draw_field(text_field(BATTLE_SCREEN_NAME, "@536", 0), (Vector2){0, 0},
               TextFormat("%d", GetFPS()));
}

static Color color_from_rgb(uint32_t rgb)
{
    return (Color){(unsigned char)(rgb >> 16), (unsigned char)(rgb >> 8),
                   (unsigned char)rgb, 255};
}

/* What is on a unit, shown along its bar. The original attaches a
   KrinBuffShower for each -- longest first -- to the unit's own bar clip, at
   110 out from its middle and 17 apart, going the way that team faces. Each
   is the buff's own icon on a backing tinted with its element, with the turns
   it has left over the corner. */
#define BUFF_SHOWN      10
#define BUFF_OFFSET     110.0f
#define BUFF_SPACING    17.0f
#define BUFF_BACKING    "#780"
#define BUFF_FRAME      "#781"
/* The counter's field, from the clip that carries it. */
#define BUFF_COUNT_X    (-4.55f)
#define BUFF_COUNT_Y    (-0.8f)
#define BUFF_COUNT_W    13.05f
#define BUFF_COUNT_SIZE 10.0f

static void draw_buff_widget(Vector2 at, const char *key, int32_t turns,
                             Color element, float sx, float sy)
{
    /* The disc takes the buff's element outright, the way Color.setRGB does,
       rather than being multiplied by it. */
    const Texture2D *back = asset_texture_recolored(BUFF_BACKING, 1);
    if (back) {
        Vector2 off = asset_frame_offset(BUFF_BACKING, 1);
        draw_texture_placed(back, at.x - 0.1f * sx, at.y, sx, sy, off.x, off.y,
                            element);
    }
    /* Then the plate, and the icon over it -- the widget stacks them at
       depths 1, 3 and 4, so the icon is the last of the three. It is a frame
       of the shower's own clip named for the buff; a passive talent has no
       frame there, in the original either. */
    const Texture2D *frame = asset_texture(BUFF_FRAME, 1);
    if (frame) {
        Vector2 off = asset_frame_offset(BUFF_FRAME, 1);
        draw_texture_placed(frame, at.x, at.y, sx, sy, off.x, off.y, WHITE);
    }
    const Texture2D *icon = asset_texture(key, 1);
    if (icon) {
        Vector2 off = asset_frame_offset(key, 1);
        draw_texture_placed(icon, at.x, at.y, sx, sy, off.x, off.y, WHITE);
    }
    const char *text = TextFormat("%d", turns);
    float size = BUFF_COUNT_SIZE * sx;
    float width = ui_sans_text_width(text, size);
    ui_sans_text(text,
                 at.x + (BUFF_COUNT_X - 2.0f) * sx + (BUFF_COUNT_W * sx - width) / 2,
                 at.y + (BUFF_COUNT_Y - 2.0f) * sy, size, RAYWHITE);
}

static void draw_unit_buffs(const Game *g, int32_t slot)
{
    const Unit *u = &g->battle.units[slot];
    const StageBar *bar = stage_bar(slot);
    if (!bar)
        return;
    /* Longest first, which is how the original sorts the list before it
       hangs them on the bar. */
    int32_t order[SONNY_MAX_BUFFS];
    int32_t count = 0;
    for (int32_t i = 0; i < SONNY_MAX_BUFFS; i++)
        if (u->BUFFARRAYK[i].CD != 0)
            order[count++] = i;
    for (int32_t i = 1; i < count; i++)
        for (int32_t j = i; j > 0
             && u->BUFFARRAYK[order[j]].CD > u->BUFFARRAYK[order[j - 1]].CD;
             j--) {
            int32_t swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }

    float way = u->teamSide == 1 ? 1.0f : -1.0f;
    for (int32_t h = 0; h < count && h < BUFF_SHOWN; h++) {
        const BuffDef *def = buff_find(SONNY_BUFFS, SONNY_BUFF_COUNT,
                                       u->BUFFARRAYK[order[h]].buffId);
        Color element = def ? color_from_rgb(element_color((Element)def->element))
                            : WHITE;
        /* The widget is attached inside the bar's own clip, so the offset
           and the art alike come out at the bar's scale, which is neither 1
           nor square. */
        Vector2 at = {bar->x + (BUFF_OFFSET + BUFF_SPACING * h) * way
                             * bar->scale,
                      bar->y};
        draw_buff_widget(at, u->BUFFARRAYK[order[h]].buffId,
                         u->BUFFARRAYK[order[h]].CD, element, bar->scale,
                         bar->scale_y);
    }
}

static void draw_unit(const Game *g, int32_t slot)
{
    const Battle *b = &g->battle;
    const Unit *u = &b->units[slot];
    if (u->LIFEU == 0)
        return;

    draw_unit_bar(g, slot);
    draw_unit_buffs(g, slot);
}

/* The battle backdrop. The battlefield has two layers and each is a container
   the game points at the zone's own art on load -- the sky with
   gotoAndStop(Krin.SkyBG) and the ground with gotoAndStop(Krin.ZoneBG) -- so
   each is drawn where the original places that container. */
static void draw_backdrop(const Game *g)
{
    const StageLayer *sky = stage_layer("sky");
    const StageLayer *zone = stage_layer("zone");

    if (g->def && g->def->sky_bg[0] && sky)
        asset_draw_placed(g->def->sky_bg, 1, (Vector2){sky->x, sky->y}, 1.0f,
                          WHITE);
    if (g->def && g->def->zone_bg[0] && zone)
        asset_draw_placed(g->def->zone_bg, 1, (Vector2){zone->x, zone->y},
                          1.0f, WHITE);
}

/* ---------------------------------------------------------------- numbers */

#define NUMBER_LIFE 34


/* KrinNumberShow: a number rises from the unit it applied to, coloured by the
   element that caused it -- green for healing -- and shown larger when the
   hit pierced. "miss" and "shield" come through the same way. */
static void number_show(Game *g, int32_t slot, const char *text,
                        uint32_t rgb, int crit)
{
    if (slot <= 0 || slot >= SONNY_SLOTS)
        return;
    if (g->number_count >= (int32_t)(sizeof(g->numbers) / sizeof(g->numbers[0]))) {
        /* Drop the oldest rather than the newest. */
        memmove(&g->numbers[0], &g->numbers[1],
                sizeof(g->numbers[0]) * (g->number_count - 1));
        g->number_count--;
    }
    Vector2 at = unit_marker_pos(g, slot);
    int32_t i = g->number_count++;
    snprintf(g->numbers[i].text, sizeof(g->numbers[i].text), "%s", text);
    /* A little scatter so several numbers on one unit stay readable. */
    g->numbers[i].x = at.x + (float)GetRandomValue(-12, 12);
    g->numbers[i].y = at.y - 30;
    g->numbers[i].life = NUMBER_LIFE;
    g->numbers[i].crit = crit;
    g->numbers[i].color = color_from_rgb(rgb);
}

static void numbers_update(Game *g)
{
    for (int32_t i = 0; i < g->number_count; ) {
        g->numbers[i].y -= 0.9f;
        if (--g->numbers[i].life > 0) {
            i++;
            continue;
        }
        g->numbers[i] = g->numbers[g->number_count - 1];
        g->number_count--;
    }
}

static void draw_numbers(const Game *g)
{
    for (int32_t i = 0; i < g->number_count; i++) {
        float size = g->numbers[i].crit ? 22.0f : 16.0f;
        Color c = g->numbers[i].color;
        /* Fade out over the last third of its life. */
        if (g->numbers[i].life < NUMBER_LIFE / 3)
            c.a = (unsigned char)(255 * g->numbers[i].life
                                  / (NUMBER_LIFE / 3));
        float w = ui_text_width(g->numbers[i].text, size);
        /* A dark pass behind it keeps it readable over the backdrop. */
        ui_text(g->numbers[i].text, g->numbers[i].x - w / 2 + 1,
                g->numbers[i].y + 1, size, (Color){0, 0, 0, c.a});
        ui_text(g->numbers[i].text, g->numbers[i].x - w / 2, g->numbers[i].y,
                size, c);
    }
}

/* --------------------------------------------------------------- dialogue */

/* Show the next line whose turn and sequence have come up, if its speaker is
   still alive. Returns 1 while a line is on screen, which holds the fight. */
static int speech_update(Game *g)
{
    if (g->speech) {
        /* The space bar cuts a line short, as the original does: it sets
           nextSpeechKKK, which ends the line there and stops whatever voice
           over it was playing. The prompt under the battlefield says so. */
        if (IsKeyPressed(KEY_SPACE)) {
            g->speech_timer = 0;
            audio_stop_effects();
        }
        if (--g->speech_timer > 0)
            return 1;
        g->speech = NULL;
    }
    if (!g->def || g->speech_index >= g->def->speech_count)
        return 0;

    const Speech *next = &g->def->speeches[g->speech_index];
    if (next->turn != g->turn_counter || next->sequence != g->speech_seq)
        return 0;

    g->speech_index++;
    g->speech_seq++;
    /* A line from a unit that is already dead is skipped, not shown. Slot
       zero is the exception: the original makes a standing object there
       called "Information", always active and on no side, which is who the
       tutorial speaks as. Half of the first fight's lines are its. */
    if (next->speaker < 0 || next->speaker >= SONNY_SLOTS)
        return 0;
    if (next->speaker != NARRATOR_SLOT
        && !g->battle.units[next->speaker].active)
        return 0;

    g->speech = next;
    g->speech_timer = (int32_t)(next->seconds * STAGE_FPS);
    /* The balloon starts with the line and runs once, however long the line
       lasts. */
    g->balloon_tick = 0;
    if (next->voice_over && next->voice_over[0])
        audio_play(next->voice_over);
    return 1;
}

/* The balloon over whoever is talking. It is part of the unit's own
   container -- mirrored inside it, so it faces the way the unit does -- and
   the speech driver plays it through once. */
static void draw_balloon(const Game *g)
{
    /* Nobody on the battlefield is talking when it is the narrator. */
    if (!g->speech || g->speech->speaker == NARRATOR_SLOT)
        return;
    int32_t frames = asset_frame_count(SPEECH_BALLOON);
    if (frames <= 0 || g->balloon_tick >= frames)
        return;
    const Unit *speaker = &g->battle.units[g->speech->speaker];
    const StageSlot *s = stage_slot(g->speech->speaker);
    int flip = s ? s->flip : (speaker->teamSide == 2);
    Vector2 at = unit_stage_pos(g, g->speech->speaker);
    /* Its own placement is mirrored inside the container, and the container
       is mirrored again for the right-hand team, so the two cancel. */
    at.x += flip ? -SPEECH_BALLOON_X : SPEECH_BALLOON_X;
    at.y += SPEECH_BALLOON_Y;

    const Texture2D *tex = asset_texture(SPEECH_BALLOON, g->balloon_tick + 1);
    if (!tex)
        return;
    Vector2 offset = asset_frame_offset(SPEECH_BALLOON, g->balloon_tick + 1);
    /* Mirrored: the source rectangle is read backwards. */
    Rectangle src = {0, 0, flip ? (float)tex->width : -(float)tex->width,
                     (float)tex->height};
    Rectangle dst = {at.x - (flip ? tex->width - offset.x : offset.x), at.y - offset.y,
                     (float)tex->width, (float)tex->height};
    DrawTexturePro(*tex, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
}

/* What a character is saying. The original places one box and slides it to
   whichever side the speaker is on, so the art, the "<name> says:" line and
   the line itself all come from that clip. */
static void draw_speech(const Game *g)
{
    if (!g->speech)
        return;
    int narrator = g->speech->speaker == NARRATOR_SLOT;
    const Unit *speaker = &g->battle.units[g->speech->speaker];
    const char *who = narrator ? NARRATOR_NAME : speaker->name;
    const StageChrome *box = stage_chrome(BATTLE_SCREEN_NAME, "combatScript");
    if (!box)
        return;

    /* Placed on the right; the game moves it left when the left team speaks.
       The narrator is on no side, so it goes left as well. */
    Vector2 at = {box->x, box->y};
    if (narrator || speaker->teamSide != 2)
        at.x = SPEECH_LEFT_X;

    Vector2 shifted = {at.x - box->x, at.y - box->y};
    /* The portrait is the piece the box points at the speaker; the prompt
       beside it is not. */
    draw_clip_parts(BATTLE_SCREEN_NAME, "combatScript", shifted,
                    SPEECH_PORTRAIT, who, WHITE);

    /* The prompt beside it runs through its own frames while the line is up.
       It is a piece of the box, so it moves with it. */
    const ClipPart *prompt = NULL;
    for (int i = 0; i < SONNY_CLIP_PART_COUNT; i++) {
        const ClipPart *part = &SONNY_CLIP_PARTS[i];
        if (strcmp(part->screen, BATTLE_SCREEN_NAME) == 0
            && strcmp(part->owner, "combatScript") == 0
            && strcmp(part->name, SPEECH_SKIP_PART) == 0)
            prompt = part;
    }
    if (prompt) {
        int32_t frames = asset_frame_count(SPEECH_SKIP);
        const Texture2D *tex = asset_texture(SPEECH_SKIP,
                                             frames > 0
                                             ? g->balloon_tick % frames + 1
                                             : 1);
        if (tex) {
            Vector2 off = asset_frame_offset(SPEECH_SKIP,
                                             frames > 0
                                             ? g->balloon_tick % frames + 1
                                             : 1);
            draw_texture_placed(tex, shifted.x + prompt->x,
                                shifted.y + prompt->y, prompt->scale_x,
                                prompt->scale_y, off.x, off.y, WHITE);
        }
    }

    /* The fields are recorded where the clip is placed, so they follow it by
       however far it moved. */
    draw_field_wrapped(text_field(BATTLE_SCREEN_NAME, "combatScript", 0),
                       shifted, g->speech->say);
    draw_field(text_field(BATTLE_SCREEN_NAME, "combatScript", 1), shifted,
               who);
}

static void draw_battle(Game *g)
{
    const StageChrome *mask = battlefield_mask();

    ClearBackground(BLACK);
    /* The furniture below the battlefield mask: the stats panel across the
       top, the black the battlefield sits on, the panels along the bottom. */
    draw_chrome(0, mask ? mask->depth : SONNY_STAGE_CHROME_COUNT,
                g->anim_tick);

    /* Everything the mask clips -- the two backdrop layers and the six units
       over them -- inside the battlefield frame, which is what stops the
       zone's art at the frame instead of running to the edge of the stage. */
    if (mask) {
        Rectangle box = placed_rect(mask->x, mask->y, mask->scale_x,
                                    mask->scale_y, mask->width, mask->height,
                                    mask->origin_x, mask->origin_y);
        BeginScissorMode((int)box.x, (int)box.y, (int)box.width,
                         (int)box.height);
    }
    /* Everything inside the frame moves with the camera; the bars, the
       reticles and the panels round it do not, because the original moves
       BATTLESCREEN and leaves the root alone. */
    Camera2D camera = battle_camera(g);
    BeginMode2D(camera);
    draw_backdrop(g);
    for (int32_t slot = 1; slot < SONNY_SLOTS; slot++)
        draw_doll(g, slot);
    draw_effect(g);
    /* The original stacks these by depth: the impact at 400, the streak at
       500, the projectile itself at 600. */
    draw_trail(g);
    draw_bolt(g);
    draw_balloon(g);
    EndMode2D();
    if (mask)
        EndScissorMode();

    /* The furniture above it: the bars, the turn indicator, the ring. The
       dial goes in between, where the clip puts it: over the indicator's
       plate and under the move orb that sits in the middle of it. */
    const StageChrome *clock = stage_chrome(BATTLE_SCREEN_NAME,
                                            "battleClocker");
    int32_t above = mask ? mask->clip_depth + 1 : 0;
    draw_chrome(above, clock ? clock->depth + 1 : INT32_MAX,
                g->anim_tick);
    draw_turn_dial(g);
    if (clock)
        draw_chrome(clock->depth + 1, INT32_MAX, g->anim_tick);
    draw_chrome_buttons(g->pointer);
    draw_move_boomer(g);
    for (int32_t slot = 1; slot < SONNY_SLOTS; slot++)
        draw_unit(g, slot);

    draw_frame_rate();
    draw_reticle(g);
    draw_ring(g, g->pointer);
    draw_numbers(g);
    draw_speech(g);
}

/* ------------------------------------------------------------------- input */

/* Which unit the pointer is over. The original's hit area is the reticle
   parked on the unit itself, not its bar, so the test is that ring. */
static int32_t unit_at(const Game *g, Vector2 p)
{
    const StageChrome *art = stage_chrome(BATTLE_SCREEN_NAME, "KrinSelector1");
    float radius = art ? art->height * art->scale_y / 3.0f : 30.0f;
    for (int32_t slot = 1; slot < SONNY_SLOTS; slot++) {
        if (!g->battle.units[slot].active)
            continue;
        if (CheckCollisionPointCircle(p, unit_stage_pos(g, slot),
                                      radius))
            return slot;
    }
    return -1;
}

/* What a resolved move sounds and looks like: the ability's own effect sound
   and impact graphic, then the target's hit grunt or death cry. */
/* Setting a move going: what the caster does, and how hard the battlefield
   leans in on whatever the move is aimed at. A melee attacker walks over and
   the camera holds through the swing, a bolt gets a short hold, and a shock
   is a shallower lean with barely any hold at all. */
static void present_move(Game *g, const MoveEvent *e)
{
    const AbilityDef *a = ability_by_id(e->moveID);

    g->effect = NULL;
    g->effect_slot = 0;
    g->move_tick = 0;
    if (!a || e->moveID == 0)
        return;

    /* The original sets colortobe on the caster's model before it tells it to
       play, for the melee swing as much as for a cast, so the effect the model
       shows over itself comes up in the move's own colour. */
    if (e->caster > 0 && e->caster < SONNY_SLOTS)
        g->cast_colour[e->caster] = a->colour;

    switch (a->delivery) {
    case DELIVER_MELEE:
        melee_start(g, e->caster, e->target);
        camera_aim(g, e->target, ZOOM_MELEE, MELEE_SWING * 2);
        break;
    case DELIVER_MISSILE:
        bolt_start(g, a, e->caster, e->target);
        camera_aim(g, e->target, ZOOM_MELEE, 5);
        break;
    case DELIVER_SHOCK:
        camera_aim(g, e->target, ZOOM_SHOCK, 1);
        shake_start(g);
        break;
    default:
        break;
    }
}

/* The moment it lands: the sound, the impact graphic, the number over the
   target and whatever it has to say about being hit. For a melee move this
   is a walk and a wind-up after present_move, which is where the original
   puts it too -- krinMelee attaches the boom when its counter reaches
   krinMeleeAttackCD, not when the move is chosen. */
static void present(Game *g, const MoveEvent *e)
{
    const Battle *b = &g->battle;
    const AbilityDef *a = ability_by_id(e->moveID);

    if (!a || e->moveID == 0 || e->missed)
        return;

    if (a->sound && a->sound[0])
        audio_play(a->sound);
    /* A blow that got through the target's defence bounces the battlefield,
       which is the original's tell for a piercing hit. */
    if (e->pierced)
        shake_start(g);
    if (a->model && a->model[0] && asset_frame_count(a->model) > 0) {
        g->effect = a->model;
        g->effect_slot = e->target;
        g->effect_tick = 0;
    }

    /* The number for what just happened, in the ability's own element. */
    uint32_t rgb = element_color((Element)a->coefs.element);
    if (e->missed) {
        number_show(g, e->target, "miss", rgb, 0);
    } else if (e->kind == KIND_HEAL && e->amount > 0) {
        number_show(g, e->target, TextFormat("%d", e->amount), 0x66FF00,
                    e->pierced);
    } else if (e->kind == KIND_FOCUS && e->amount != 0) {
        number_show(g, e->target, TextFormat("%d", e->amount), 0x66CCFF, 0);
    } else if (e->kind == KIND_FULL_DAMAGE) {
        if (e->absorbed > 0 && e->amount == 0)
            number_show(g, e->target, "shield", rgb, 0);
        else if (e->amount > 0)
            number_show(g, e->target, TextFormat("%d", e->amount), rgb,
                        e->pierced);
    }

    const Unit *target = &b->units[e->target];
    if (e->target_died) {
        if (target->voice_die[0])
            audio_play(target->voice_die);
    } else if (e->kind == KIND_FULL_DAMAGE && e->amount > 0) {
        /* The original picks one of three at random. */
        int32_t pick = GetRandomValue(0, 2);
        if (target->voice_hit[pick][0])
            audio_play(target->voice_hit[pick]);
    }
}

static void describe(Game *g, const MoveEvent *e)
{
    const Battle *b = &g->battle;
    const AbilityDef *a = ability_by_id(e->moveID);
    const char *name = (a && a->name && a->name[0]) ? a->name
                     : (a && a->icon[0]) ? a->icon : "pass";

    /* Empty slots queue a pass every phase; they are not participants. */
    if (e->caster <= 0 || b->units[e->caster].LIFEU == 0)
        return;
    if (e->moveID == 0) {
        game_log(g, "%s waits.", b->units[e->caster].name);
        return;
    }
    if (e->missed) {
        game_log(g, "%s cannot use %s.", b->units[e->caster].name, name);
        return;
    }
    switch (e->kind) {
    case KIND_HEAL:
        game_log(g, "%s heals %s for %d.%s", b->units[e->caster].name,
                  b->units[e->target].name, e->amount,
                  e->pierced ? " (critical)" : "");
        break;
    case KIND_FOCUS:
        game_log(g, "%s restores %d focus to %s.", b->units[e->caster].name,
                  e->amount, b->units[e->target].name);
        break;
    default:
        game_log(g, "%s hits %s with %s for %d.%s", b->units[e->caster].name,
                  b->units[e->target].name, name, e->amount,
                  e->pierced ? " (pierced)" : "");
        break;
    }
    if (e->target_died)
        game_log(g, "%s falls.", b->units[e->target].name);
}

static void handle_input(Game *g)
{
    Battle *b = &g->battle;
    /* Where the pointer is in stage coordinates. The caller has already put
       the window's letterboxing behind it -- and a headless capture parks it
       by hand -- so taking it from the screen again here would put this out
       of step with everything else on the frame. */
    Vector2 stage = g->pointer;

    g->hovered_unit = unit_at(g, stage);
    /* SONNY_HOVER pins a slot for a headless capture, so the ring can be
       photographed without a pointer to move. */
    if (getenv("SONNY_HOVER"))
        g->hovered_unit = atoi(getenv("SONNY_HOVER"));

    /* The ring follows the pointer onto a unit, and stays up while the
       pointer is anywhere within it -- which is what lets it be moved off the
       unit and onto one of the orbs. The original pins it on a click and
       drops it when the pointer leaves. */
    if (g->hovered_unit > 0) {
        g->ring_unit = g->hovered_unit;
    } else if (g->ring_unit > 0
               && !CheckCollisionPointCircle(
                      stage, unit_stage_pos(g, g->ring_unit), ring_radius())) {
        g->ring_unit = -1;
    }

    /* The fade. It follows whoever the ring is on, and keeps the last one
       while it goes back down so the reticle is not simply cut off. */
    if (g->ring_unit > 0)
        g->ring_fade_unit = g->ring_unit;
    g->ring_fade += (g->ring_unit > 0 ? 1.0f : -1.0f) / RETICLE_FADE_FRAMES;
    if (g->ring_fade > 1.0f)
        g->ring_fade = 1.0f;
    if (g->ring_fade < 0.0f)
        g->ring_fade = 0.0f;

    /* What the turn indicator says about itself. The original keeps the two
       lines on the orb -- thinger1 and thinger2 -- and swaps in the chosen
       ability's name and target once there is one. They are written into the
       clip rather than the language table, so they are literals here too. */
    const StageButton *mark = stage_button(BATTLE_SCREEN_NAME, PASS_BUTTON, 0);
    if (mark && hit((Rectangle){mark->x, mark->y, mark->width, mark->height},
                    stage)) {
        const QueuedMove *mine = NULL;
        if (g->queued)
            for (int32_t i = 0; i < SONNY_QUEUE; i++)
                if (b->queue[i].caster == PLAYER_SLOT
                    && b->queue[i].moveID != 0)
                    mine = &b->queue[i];
        const AbilityDef *chosen = mine ? ability_by_id(mine->moveID) : NULL;
        if (chosen && chosen->id != 0)
            game_tooltip(g, chosen->name,
                         TextFormat("You will use this ability on %s. "
                                    "Click to cancel this ability.",
                                    b->units[mine->target].name));
        else
            game_tooltip(g, "No ability selected!",
                         "You may click here to skip your turn.");
    }

    if (!player_turn(g) || g->queued)
        return;

    if (ui_clicked() && g->ring_unit > 0) {
        Vector2 centre = unit_stage_pos(g, g->ring_unit);
        float radius = orb_radius();
        for (int i = 0; i < SONNY_RING_SLOT_COUNT; i++) {
            const RingSlot *slot = &SONNY_RING_SLOTS[i];
            const AbilityDef *a = ability_by_id(g->ability_ids[slot->slot]);
            if (!a || a->id == 0)
                continue;
            if (!CheckCollisionPointCircle(stage, ring_slot_pos(slot, centre),
                                           radius))
                continue;
            const char *refusal = move_refusal(g, slot->slot, g->ring_unit);
            if (refusal) {
                /* The original says which check failed, rather than doing
                   nothing. */
                game_notice(g, refusal);
                return;
            }
            battle_queue(b, PLAYER_SLOT, g->ring_unit, a->id, 0);
            g->queued = 1;
            g->cooldown_slot = slot->slot;
            g->boomer_tick = 0;
            return;
        }
    }

    /* The turn indicator in the middle of the bottom panel: clicking it ends
       the turn with the null move, which is how the original passes. The
       thing that answers the click is krinToMove2, a button -- it has no
       furniture row of its own, and asking for one gave nothing, so this
       never fired. */
    const StageButton *pass = stage_button(BATTLE_SCREEN_NAME, PASS_BUTTON, 0);
    if (pass && ui_clicked()
        && hit((Rectangle){pass->x, pass->y, pass->width, pass->height},
               stage)) {
        battle_queue(b, PLAYER_SLOT, PLAYER_SLOT, 0, 0);
        g->queued = 1;
        g->boomer_tick = 0;
        return;
    }

}

/* Krin.abilityCoolDown, kept the way the original keeps it: as one of the
   player's own moves resolves every slot counts down, and then the slot that
   move came from is set to its ability's cooldown. Order matters -- a move
   with a cooldown of three sits out the player's next three. */
static void tick_cooldowns(Game *g, const MoveEvent *e)
{
    if (e->caster != PLAYER_SLOT)
        return;
    for (int i = 0; i < ABILITY_SLOTS; i++)
        if (g->ability_cooldown[i] > 0)
            g->ability_cooldown[i]--;

    const AbilityDef *a = ability_by_id(e->moveID);
    if (a && e->moveID != 0 && g->cooldown_slot >= 0
        && g->cooldown_slot < ABILITY_SLOTS)
        g->ability_cooldown[g->cooldown_slot] = a->cooldown;
    g->cooldown_slot = -1;
}

static void advance(Game *g)
{
    Battle *b = &g->battle;

    if (b->phase == PHASE_OVER)
        return;

    if (b->phase == PHASE_DECLARE) {
        if (player_turn(g) && !g->queued)
            return;                 /* waiting on the human */
        battle_declare_phase(b);
        g->queued = 0;
        g->resolve_timer = 0;
        return;
    }

    if (b->phase == PHASE_RESOLVE) {
        /* An attacker on its way over holds the fight, the way the original
           waits on krinMelee to reach its target before the hit lands -- and
           a bolt in the air holds it just the same. */
        if (melee_busy(g) || bolt_busy(g))
            return;
        if (g->resolve_timer > 0) {
            g->resolve_timer--;
            return;
        }
        /* The swing has landed: show what it did. */
        if (g->move_pending) {
            g->move_pending = 0;
            present(g, &g->last);
            g->resolve_timer = RESOLVE_FRAMES;
            return;
        }
        MoveEvent e;
        if (battle_resolve_step(b, &e)) {
            g->last = e;
            g->has_last = 1;
            g->move_pending = 1;
            tick_cooldowns(g, &e);
            describe(g, &e);
            present_move(g, &e);
        } else if (b->phase != PHASE_OVER) {
            battle_end_phase(b);
            /* A completed phase advances the dialogue's turn counter and
               resets its within-turn sequence. */
            g->turn_counter++;
            g->speech_seq = 0;
        }
    }
}

/* ----------------------------------------------------------------- entry */

/* The four tracks the game walks, in its own order (soundPlayArray). */
static const char *const MUSIC_TRACKS[4] = {
    "menumusic", "BattleMusic2loopable", "Gamemusic002",
    "BattleMusic1loopable",
};

/* addSound("Music", 1): the roaming track, which the hub asks for. */
void game_music_roaming(Game *g)
{
    if (!audio_ready() || g->music_mode == 1)
        return;
    g->music_mode = 1;
    audio_music(MUSIC_TRACKS[g->music_next % 4]);
    g->music_next = (g->music_next + 1) % 4;
}

/* addSound("Music", 2): a fight. It takes the next track off the same
   counter the hub walks -- so which track a fight gets depends on how many
   times the hub has been through since -- and the boss theme stands in for a
   boss marker or for the three fights the game names outright. The counter
   moves either way. */
void game_music_battle(Game *g)
{
    if (!audio_ready() || g->music_mode == 2)
        return;
    g->music_mode = 2;
    int32_t at = g->campaign.progress_battle;
    int boss = g->boss_fight || at == 24 || at == 30 || at == 36;
    audio_music(boss ? "BossBattleloopable"
                     : MUSIC_TRACKS[g->music_next % 4]);
    g->music_next = (g->music_next + 1) % 4;
}

void battle_screen_start(Game *g, int32_t battle_id)
{
    const BattleDef *def = battle_def_by_id(battle_id);
    if (!def) {
        game_notice(g, "No such battle.");
        return;
    }

    /* The bar holds what the character actually knows. */
    int32_t known[SONNY_TALENT_MAX + 2];
    int32_t n = character_known_abilities(&g->campaign.player, known,
                                          SONNY_TALENT_MAX + 2);
    for (int i = 0; i < ABILITY_SLOTS; i++) {
        int32_t equipped = (i < SONNY_MOVE_SLOTS)
                         ? g->campaign.player.move_matrix[i] : 0;
        g->ability_ids[i] = equipped ? equipped : (i < n ? known[i] : 0);
    }

    g->def = def;
    /* A fight starts with every slot ready, as the original resets
       Krin.abilityCoolDown when it builds the player. */
    memset(g->ability_cooldown, 0, sizeof(g->ability_cooldown));
    g->cooldown_slot = -1;
    g->seed = rng_next(&g->rng);
    campaign_setup_battle(&g->campaign, def, &g->battle, g->seed);

    /* Drops are rolled at the start of the battle, as the original does. */
    g->dropped_count = campaign_roll_drops(def, &g->rng, g->dropped,
                                           SONNY_MAX_DROPPED);
    memset(g->taken, 0, sizeof(g->taken));

    g->selected = -1;
    g->hovered_unit = -1;
    g->queued = 0;
    g->resolve_timer = 0;
    g->has_last = 0;
    g->effect = NULL;
    g->log_count = 0;
    g->screen = SCREEN_BATTLE;
    g->turn_counter = 0;
    g->speech_seq = 0;
    g->speech_index = 0;
    g->speech_timer = 0;
    g->speech = NULL;
    g->number_count = 0;
    g->camera_x = g->camera_y = 0.0f;
    g->camera_scale = 1.0f;
    g->zoom_point = g->zoom_way = 0;
    g->melee_slot = 0;
    g->melee_dir = 0;
    g->melee_state = 0;
    g->melee_x = g->melee_y = 0.0f;
    g->boomer_tick = -1;
    g->bolt = NULL;
    g->shake_phase = 0;
    g->shake_y = 0.0f;

    game_log(g, "%s%d. Team %d is faster and acts first.",
             lang_text("SYSTEM", 10), g->campaign.progress_battle - 1,
             g->battle.TeamMove);
    game_music_battle(g);
}

void battle_screen_update(Game *g, Vector2 mouse, int headless)
{
    g->pointer = mouse;
    g->hovered_unit = unit_at(g, mouse);
    handle_input(g);

    /* A headless capture passes the turn so the fight keeps moving, unless
       SONNY_HOVER asked for a unit to be held under the pointer -- which is
       how the ability ring gets photographed. */
    if (headless && !getenv("SONNY_HOVER") && player_turn(g) && !g->queued) {
        battle_queue(&g->battle, PLAYER_SLOT, PLAYER_SLOT, 0, 0);
        g->queued = 1;
        g->boomer_tick = 0;
    }

    g->anim_tick++;
    if (g->speech)
        g->balloon_tick++;
    g->move_tick++;
    g->effect_tick++;
    camera_tick(g);
    shake_tick(g);
    melee_tick(g);
    bolt_tick(g);
    if (g->boomer_tick >= 0)
        g->boomer_tick++;
    numbers_update(g);

    /* Dialogue holds the fight, as speechDone does in the original. */
    if (speech_update(g))
        return;
    advance(g);

    /* When the fight ends, pay out and move on. */
    if (g->battle.phase == PHASE_OVER) {
        if (g->battle.winCondition == 1) {
            g->rewards = campaign_award(&g->campaign, &g->battle, &g->rng, 0);
            /* The experience is the victory screen's to grant: it fills the
               bar towards it over thirty frames, adding a thirtieth a frame,
               and levels up if the fill reaches the end. */
            g->win_fill = WIN_FILL_FRAMES;
            g->win_xp = (float)g->campaign.player.xp;
            g->win_step = (float)g->rewards.xp_percent / WIN_FILL_FRAMES;
            g->win_leveled = 0;
            /* Winning re-arms the story's note, as frame 213 clears
               Krin.tutSpeecher. */
            g->hub_note_done = 0;
            /* Only a fight the story marker started carries progress -- a
               practice fight and a replayed boss both leave it where it is,
               as Krin.progressFight does. */
            /* A boss fight won on the story marker is what finishes a
               zone, which frame 213 records before it clears the flags. */
            g->boss_beaten = (g->boss_fight && g->progress_fight);
            if (g->boss_beaten)
                g->campaign.stats.zones_cleared++;
            if (g->progress_fight)
                campaign_advance(&g->campaign);
            g->progress_fight = 0;
            g->boss_fight = 0;
            g->screen = SCREEN_VICTORY;
        } else {
            g->progress_fight = 0;
            g->boss_fight = 0;
            g->lost_timer = 0;
            g->battle_drawn = (g->battle.winCondition == 2);
            g->screen = SCREEN_LOST;
        }
        audio_music(NULL);
        g->music_mode = 0;
    }
}

void battle_screen_draw(Game *g)
{
    draw_battle(g);
}
