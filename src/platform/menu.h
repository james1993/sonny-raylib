/* What the menu screens share: the frames they are laid out on, the bar and
 * panels of the hub under all of them, and the squares items sit in.
 *
 * Internal to the screens outside battle -- screen_hub.c, screen_map.c,
 * screen_talents.c, screen_inventory.c, screen_shop.c, screen_victory.c and
 * menu.c. Text comes from the game's own language arrays, so the menus read
 * exactly as the original's do.
 */
#ifndef SONNY_MENU_H
#define SONNY_MENU_H

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "assets.h"
#include "audio.h"
#include "game.h"
#include "render.h"

/* The menu clip's frames, by the labels the original gives them: one screen
   each, and the same slot name means different places on different frames. */
#define MENU_SCREEN    "menu"
#define MENU_INVENTORY "inventory"
/* How wide an experience bar's fill is at a hundred per cent, which both the
   inventory and the tally after a fight scale against. The frame's own
   number: `_width = ExpSets[player] / 100 * 95.1`. */
#define XP_BAR_WIDTH   95.1f
/* The fill is a named slot, not one of the frame's own pieces: "bar", the
   clip the original writes _width on. Its first frame is the plain fill and
   its second is the flash on a level. */
#define XP_BAR_SLOT    "bar"
#define XP_BAR_PLAIN   "#1344@1"
#define XP_BAR_LEVEL   "#1344@2"
#define MENU_WIN       "win"

/* Nothing on a menu moves, so its pieces are drawn where they are recorded. */
static const Vector2 NO_OFFSET = {0, 0};

/* How many bag slots a menu frame's grid has, which is the bag itself. */
#define MENU_BAG_SLOTS SONNY_BAG_SLOTS

/* What g->hovered_item holds while a menu is up. A screen can have three
   kinds of square under the pointer at once and they must not share a range:
   the shop had an equipment row and a stock slot both counting from zero, so
   hovering the body row read out whatever was first on the shelf. */
#define HOVER_STOCK 0            /* a store's shelf, or a tree node */
#define HOVER_BAG   100
#define HOVER_EQUIP 200

/* The root frame the hub is laid out from. */
#define HUB_SCREEN "Navigation"

/* The cross the menu screens close with, which the original places once on
   the hub's own frame. */
#define MENU_BUTTON_CLOSE 1364

/* The character screen's own layout: seven equipment slots either side of a
   doll preview, and a six-by-six bag grid to the right, all placed where the
   original's menu places them. The slot art is 34 square, drawn centred on
   its placement point. */
#define MENU_SLOT_SIZE 34.0f

const Character *menu_character(Game *g, Character *scratch);
int item_fits(const Character *c, int32_t item_id, int32_t row);
void swap_carried(Game *g, int32_t *slot);
void swap_equipped(Game *g, int32_t row);
void swap_bag(Game *g, int32_t index);
int32_t bag_free_slot(const Campaign *c);
Rectangle named_slot(const char *menu, const char *prefix, int i);
Rectangle slot_rect(int i);
Rectangle bag_rect(int i);
Rectangle drop_rect(int i);
Rectangle win_bag_rect(int i);
void draw_slot_art(const char *menu, const char *prefix, int i);
void draw_item_icon(const ItemDef *item, Rectangle r, Color tint);
void draw_party_row(Game *g, const char *menu, Vector2 mouse);
void tooltip_equip_row(Game *g, const Character *who, int32_t row);
const TextField *hub_field(const char *name);
int menu_close_pressed(Vector2 mouse);
const TextField *win_field(const char *name);
void draw_hub_panel(Game *g, Vector2 mouse);

#endif
