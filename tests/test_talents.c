/* The talent tree: rank progression, level gates, prerequisites, how a learned
   rank follows into the ability bar, and how passive ranks name their buff. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/battle.h"

int main(void)
{
    assert(SONNY_TALENT_COUNT == 28);
    assert(SONNY_START_SKILL1 == 69 && SONNY_START_SKILL2 == 70);

    Character c;
    character_new(&c, 1);
    assert(c.level == 1);
    assert(c.move_matrix[0] == 69 && c.move_matrix[1] == 70);
    assert(character_unspent_skill_points(&c) == 0);

    /* Node 0: ability 1, needs level 2 for rank 1, five ranks, no prereq. */
    const TalentDef *n0 = &SONNY_TALENTS[0];
    assert(n0->ability_id == 1 && n0->level_min == 2 && n0->max_rank == 5);
    assert(character_can_learn(&c, 0) == TALENT_NO_POINTS);

    c.level = 2;                      /* one point, level 2 */
    assert(character_unspent_skill_points(&c) == 1);
    assert(character_talent_next_level(&c, 0) == 2);
    assert(character_can_learn(&c, 0) == TALENT_OK);

    /* Node 1 requires node 0 first. */
    assert(character_can_learn(&c, 1) == TALENT_LEVEL_TOO_LOW);
    c.level = 9;
    assert(character_can_learn(&c, 1) == TALENT_MISSING_PREREQ);

    /* Learn node 0 rank 1: it is not on the bar yet, so nothing is upgraded. */
    assert(character_learn(&c, 0) == TALENT_OK);
    assert(c.rank[0] == 1);
    assert(c.skill_adder[0] == 1);
    assert(c.move_matrix[0] == 69);

    /* Put it on the bar, then take rank 2 -- the slot follows the upgrade. */
    c.move_matrix[2] = c.skill_adder[0];
    assert(character_talent_next_level(&c, 0) == 3);
    assert(character_learn(&c, 0) == TALENT_OK);
    assert(c.rank[0] == 2);
    assert(c.skill_adder[0] == 2);
    assert(c.move_matrix[2] == 2);
    assert(c.move_matrix[0] == 69);   /* other slots untouched */

    /* The prerequisite is satisfied now. */
    assert(character_can_learn(&c, 1) == TALENT_OK);

    /* Ranks stop at the node's maximum. */
    c.level = 40;
    while (c.rank[0] < n0->max_rank)
        assert(character_learn(&c, 0) == TALENT_OK);
    assert(c.rank[0] == 5);
    assert(c.skill_adder[0] == 5);       /* ability_id + rank - 1 */
    assert(character_can_learn(&c, 0) == TALENT_MAX_RANK);

    /* Known abilities: the two starters plus each learned node's rank. */
    int32_t known[SONNY_TALENT_MAX + 2];
    int32_t n = character_known_abilities(&c, known, SONNY_TALENT_MAX + 2);
    assert(n == 3);
    assert(known[0] == 69 && known[1] == 70 && known[2] == 5);

    /* A passive node names its buff with the rank appended, and that key must
       exist in the buff table. */
    int32_t passive = -1;
    for (int32_t i = 0; i < SONNY_TALENT_COUNT; i++)
        if (SONNY_TALENTS[i].passive) {
            passive = i;
            break;
        }
    assert(passive >= 0);
    assert(strcmp(SONNY_TALENTS[passive].buff_name, "REGENERATION") == 0);

    Character p;
    character_new(&p, 1);
    p.level = 40;
    /* Satisfy the node's prerequisites first. */
    for (int32_t i = 0; i < SONNY_TALENTS[passive].prereq_count; i++) {
        int32_t pre = SONNY_TALENTS[passive].prereq[i];
        if (pre >= 0)
            while (p.rank[pre] == 0)
                assert(character_learn(&p, pre) == TALENT_OK);
    }
    assert(character_learn(&p, passive) == TALENT_OK);
    assert(strcmp(p.buff_adder[passive], "REGENERATION1") == 0);
    assert(buff_find(SONNY_BUFFS, SONNY_BUFF_COUNT, "REGENERATION1") != NULL);
    assert(character_learn(&p, passive) == TALENT_OK);
    assert(strcmp(p.buff_adder[passive], "REGENERATION2") == 0);
    assert(buff_find(SONNY_BUFFS, SONNY_BUFF_COUNT, "REGENERATION2") != NULL);

    const char *keys[SONNY_TALENT_MAX];
    assert(character_passive_buffs(&p, keys, SONNY_TALENT_MAX) == 1);

    /* Every passive node's every rank must name a real buff, or a talent
       would silently do nothing in battle. */
    int missing = 0;
    for (int32_t i = 0; i < SONNY_TALENT_COUNT; i++) {
        if (!SONNY_TALENTS[i].passive)
            continue;
        for (int32_t rank = 1; rank <= SONNY_TALENTS[i].max_rank; rank++) {
            char key[SONNY_NAME_LEN];
            snprintf(key, sizeof(key), "%s%d", SONNY_TALENTS[i].buff_name, rank);
            if (!buff_find(SONNY_BUFFS, SONNY_BUFF_COUNT, key)) {
                fprintf(stderr, "talent %d rank %d has no buff %s\n", i, rank,
                        key);
                missing++;
            }
        }
    }

    /* Every active node's every rank must name a real ability. */
    for (int32_t i = 0; i < SONNY_TALENT_COUNT; i++) {
        if (SONNY_TALENTS[i].passive)
            continue;
        for (int32_t rank = 1; rank <= SONNY_TALENTS[i].max_rank; rank++) {
            int32_t id = SONNY_TALENTS[i].ability_id + rank - 1;
            if (!ability_by_id(id)) {
                fprintf(stderr, "talent %d rank %d has no ability %d\n", i,
                        rank, id);
                missing++;
            }
        }
    }

    /* Passive buffs reach the battle: a regenerating Sonny must show the buff
       on his unit from the first turn. */
    Battle b;
    battle_init(&b, 5, 1);
    battle_place_character(&b, 1, &p, "Sonny", 0);
    int found = 0;
    for (int32_t i = 0; i < SONNY_MAX_BUFFS; i++)
        if (b.units[1].BUFFARRAYK[i].CD != 0
            && strcmp(b.units[1].BUFFARRAYK[i].buffId, "REGENERATION2") == 0)
            found = 1;
    assert(found);

    printf("talents: %d nodes, rank/level/prereq rules hold, %d dangling "
           "references\n", SONNY_TALENT_COUNT, missing);
    return missing ? 1 : 0;
}
