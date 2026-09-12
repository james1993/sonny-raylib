/* A saved campaign must come back exactly as it went in: the same character,
   the same talent ranks and bar, the same progress and purse. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/core/save.h"

int main(void)
{
    Campaign c;
    campaign_new(&c, 1);
    c.player.level = 14;
    c.player.xp = 42.5;
    c.euros = 1234;
    c.progress_battle = 17;
    c.zone = 1;
    c.player.spent[0] = 3;
    c.player.spent[2] = 7;
    c.player.equip[1] = 4;
    c.player.equip[5] = 6;
    c.inventory[0] = 9;
    c.inventory[1] = 12;
    c.inventory_count = 2;

    /* Spend points so there are ranks, a bar and a passive to round-trip. */
    for (int32_t node = 0; node < SONNY_TALENT_COUNT; node++)
        while (character_can_learn(&c.player, node) == TALENT_OK)
            character_learn(&c.player, node);
    int32_t known[SONNY_TALENT_MAX + 2];
    int32_t n = character_known_abilities(&c.player, known,
                                          SONNY_TALENT_MAX + 2);
    for (int32_t i = 0; i < SONNY_MOVE_SLOTS && i < n; i++)
        c.player.move_matrix[i] = known[i];

    DerivedStats before = character_derive(&c.player);

    const char *path = "build/test_save.txt";
    assert(save_write(&c, path) == 0);

    Campaign back;
    assert(save_read(&back, path) == 0);

    assert(back.player.level == c.player.level);
    assert(back.player.xp == c.player.xp);
    assert(back.euros == c.euros);
    assert(back.progress_battle == c.progress_battle);
    assert(back.zone == c.zone);
    assert(back.player.spent_skill_points == c.player.spent_skill_points);
    assert(back.player.class_template == c.player.class_template);
    assert(back.inventory_count == c.inventory_count);
    for (int32_t i = 0; i < c.inventory_count; i++)
        assert(back.inventory[i] == c.inventory[i]);
    for (int32_t i = 0; i < SONNY_STATS; i++)
        assert(back.player.spent[i] == c.player.spent[i]);
    for (int32_t i = 0; i < SONNY_EQUIP_SLOTS; i++)
        assert(back.player.equip[i] == c.player.equip[i]);
    for (int32_t i = 0; i < SONNY_MOVE_SLOTS; i++)
        assert(back.player.move_matrix[i] == c.player.move_matrix[i]);
    for (int32_t i = 0; i < SONNY_TALENT_MAX; i++) {
        assert(back.player.rank[i] == c.player.rank[i]);
        assert(back.player.skill_adder[i] == c.player.skill_adder[i]);
        assert(strcmp(back.player.buff_adder[i],
                      c.player.buff_adder[i]) == 0);
    }

    /* The derived stats -- what actually reaches a battle -- must match. */
    DerivedStats after = character_derive(&back.player);
    assert(before.life == after.life);
    assert(before.strength == after.strength);
    assert(before.magic == after.magic);
    assert(before.speed == after.speed);
    assert(before.focus == after.focus);

    /* A missing or malformed file is refused rather than half-loaded. */
    Campaign junk;
    assert(save_read(&junk, "build/does-not-exist.txt") != 0);

    printf("save: round-trips character, talents, progress and purse\n");
    return 0;
}
