#include <stdio.h>
#include <string.h>
#include "../src/core/campaign.h"
int main(void)
{
    Campaign c;
    campaign_new(&c, 1);
    int fails = 0;
    if (!campaign_has_friend(&c, 0)) { puts("Sonny missing at the start"); fails++; }
    if (campaign_has_friend(&c, 1)) { puts("Veradux there too early"); fails++; }
    /* The story hands someone over in the marker's own handler, as the fight
       is started -- not on the way back from the one before it. So getting
       there is not enough. */
    while (c.progress_battle < 15) campaign_advance(&c);
    if (campaign_has_friend(&c, 1)) { puts("Veradux joined a fight early"); fails++; }
    campaign_story_joins(&c);
    if (!campaign_has_friend(&c, 1)) { puts("Veradux never joined at 15"); fails++; }
    while (c.progress_battle < 38) campaign_advance(&c);
    campaign_story_joins(&c);
    for (int i = 0; i < SONNY_PARTY_SIZE; i++)
        if (!campaign_has_friend(&c, i)) { printf("member %d missing at 38\n", i); fails++; }
    /* The fighting line is friendArrayX, and its members turn out in battle. */
    Character ally;
    campaign_ally(&c, c.line[0], &ally);
    if (!ally.class_template || ally.level <= 0) { puts("ally has no class"); fails++; }
    /* Their gear is their own: changing one leaves the others alone. */
    int32_t member = c.line[1];
    c.ally_equip[member][0] = 0;
    Character changed;
    campaign_ally(&c, member, &changed);
    if (changed.equip[0] != 0) { puts("ally gear did not follow"); fails++; }
    campaign_ally(&c, c.line[0], &ally);
    if (ally.equip[0] != SONNY_PARTY[c.line[0]].equip[0]) {
        puts("changing one ally changed another");
        fails++;
    }

    /* Each of them has a level of their own -- LevelStats -- which is what
       they fight at and what the fight's experience is worked out against,
       and it is not the player's. */
    int32_t v = c.line[0];
    if (c.ally_level[v] != SONNY_PARTY[v].level) { puts("ally level not the table's"); fails++; }
    c.ally_level[v] = 20;
    campaign_ally(&c, v, &ally);
    if (ally.level != 20) { puts("ally does not fight at their own level"); fails++; }
    BattleRewards r = {0};
    r.enemy_rating = 20;
    if (campaign_ally_xp_gain(&c, &r, v) != character_xp_gain(20, 20)) {
        puts("ally experience not worked out at their own level");
        fails++;
    }
    /* The three AI modes and the four numbers each writes. */
    campaign_set_ai_mode(&c, v, 1);
    if (c.ally_ai_mode[v] != 1 || c.ally_aggression[v][1] != 95) { puts("defensive mode wrong"); fails++; }
    campaign_set_ai_mode(&c, v, 3);
    if (c.ally_aggression[v][0] != 50 || c.ally_aggression[v][3] != 85) { puts("aggressive mode wrong"); fails++; }
    campaign_set_ai_mode(&c, 0, 1);
    if (c.ally_ai_mode[0] != 2) { puts("the player took an AI mode"); fails++; }
    /* And a fight records who stood where. */
    Battle b;
    campaign_setup_battle(&c, battle_def_by_id(20), &b, 1);
    int placed = 0;
    for (int32_t slot = 2; slot < SONNY_SLOTS; slot++)
        if (b.member[slot] > 0)
            placed++;
    if (b.member[1] != 0) { puts("the player's slot is not member 0"); fails++; }

    printf("party: %d joined by battle %d, line is %s and %s, %d placed in "
           "fight 20, %d failures\n", SONNY_PARTY_SIZE, c.progress_battle,
           SONNY_PARTY[c.line[0]].name, SONNY_PARTY[c.line[1]].name, placed,
           fails);
    return fails != 0;
}
