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
    while (c.progress_battle < 15) campaign_advance(&c);
    if (!campaign_has_friend(&c, 1)) { puts("Veradux never joined at 15"); fails++; }
    while (c.progress_battle < 38) campaign_advance(&c);
    for (int i = 0; i < SONNY_PARTY_SIZE; i++)
        if (!campaign_has_friend(&c, i)) { printf("member %d missing at 38\n", i); fails++; }
    /* The fighting line is friendArrayX, and its members turn out in battle. */
    Character ally;
    campaign_ally(&c, c.line[0], &ally);
    if (!ally.class_template || ally.level <= 0) { puts("ally has no class"); fails++; }
    printf("party: %d joined by battle %d, line is %s and %s, %d failures\n",
           SONNY_PARTY_SIZE, c.progress_battle,
           SONNY_PARTY[c.line[0]].name, SONNY_PARTY[c.line[1]].name, fails);
    return fails != 0;
}
