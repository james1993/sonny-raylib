/* Campaign progression: zones, the battle roster, and what a fight pays out.
 *
 * The original stores each fight as a KBR object and reads it in frame 196
 * while setting the battle up. players[i] fills slot i + 2 -- a positive value
 * is an enemy unit template, a negative one picks a party ally, 0 leaves the
 * slot empty -- and the player is always slot 1. Levels of -1 stand for the
 * original's "X": match the player's level.
 *
 * Progress is a single battle id (Krin.progressLevelOn) that steps forward on
 * a win; zones are id ranges over it.
 */
#ifndef SONNY_CAMPAIGN_H
#define SONNY_CAMPAIGN_H

#include "battle.h"
#include "character.h"

#define SONNY_MAX_ALLIES 2
#define SONNY_MAX_DROPPED 15   /* Krin.dropArray */

typedef struct {
    Character  player;
    /* Krin.friendArray: who has joined, by the place they take in the party
       -- -1 for a place the story has not filled yet. */
    int32_t    friends[SONNY_PARTY_SIZE];
    /* Krin.friendArrayX: which two of them stand in the fighting line. */
    int32_t    line[SONNY_MAX_ALLIES];
    /* Krin.equipArrayN and StatSetsN for everyone but the player, who keeps
       his own. The character screen turns over to any of them and their gear
       can be changed there, so it cannot live in the table. */
    int32_t    ally_equip[SONNY_PARTY_SIZE][SONNY_EQUIP_SLOTS];
    double     ally_stat_sets[SONNY_PARTY_SIZE][SONNY_STATS];
    int32_t    progress_battle;   /* Krin.progressLevelOn */
    int32_t    zone;              /* Krin.sectionIn */
    int32_t    euros;
    int32_t    inventory[64];
    int32_t    inventory_count;
    /* Krin.slotInUse: which of the four saves this run belongs to. */
    int32_t    slot;
    /* The tally the settings screen shows. Each is one of the counters the
       original keeps on Krin and nudges where the thing happens. */
    struct {
        int32_t zones_cleared;      /* Krin.highestZoneDefeated */
        int32_t respec_used;        /* Krin.numberOfRespecUsed */
        int32_t training_used;      /* Krin.numberOfTrainingUsed */
        int32_t top_physical;       /* the biggest hit of each kind */
        int32_t top_elemental;
        int32_t scenery_found;      /* updateBgElementClicked */
    } stats;
} Campaign;

void campaign_new(Campaign *c, int32_t class_id);

/* Whether this party member has joined. */
int campaign_has_friend(const Campaign *c, int32_t member);
/* Build the character one of them fights as, from the party table. */
void campaign_ally(const Campaign *c, int32_t member, Character *out);
/* Apply any point of the story that progress has now reached. The original
   does this in the marker handlers and on the map, so it runs wherever
   progress can have moved. */
void campaign_story_joins(Campaign *c);

/* Build a battle from a roster definition: the player in slot 1, then each
   roster entry in slots 2..6. Returns 0 if the definition is unusable. */
int campaign_setup_battle(const Campaign *c, const BattleDef *def, Battle *out,
                          uint64_t seed);

/* Roll the battle's drops, as frame 196 does at setup time: each entry drops
   when its chance beats random(100), then the rare table is sampled
   rare_dropper times. Writes item ids and returns how many. */
int32_t campaign_roll_drops(const BattleDef *def, Rng *rng, int32_t *out,
                            int32_t max);

/* Rewards for winning `def`: the XP percentage and the euros. */
typedef struct {
    double  enemy_rating;
    double  xp_percent;
    int32_t euros;
    int32_t leveled;
} BattleRewards;

BattleRewards campaign_award(Campaign *c, const Battle *b, Rng *rng);

/* Advance past the battle just won (frame 213 increments progressLevelOn). */
void campaign_advance(Campaign *c);

/* Where the player is in the campaign. */
const ZoneDef *campaign_zone(const Campaign *c);
/* Is the campaign finished? The original's ending triggers past battle 38. */
int campaign_complete(const Campaign *c);

#endif
