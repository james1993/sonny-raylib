/* Saving and loading a campaign.
 *
 * The original keeps its save in a Flash shared object (frame 62's
 * krinHandleData writes Krin's fields into k_slotter.data). The fields are the
 * same here; the container is a small text file instead, so a save can be read
 * and diffed without a Flash runtime.
 */
#ifndef SONNY_SAVE_H
#define SONNY_SAVE_H

#include "campaign.h"

/* Returns 0 on success. */
/* The four slots the original keeps, and where each one lives. */
#define SONNY_SAVE_SLOTS 4
const char *save_slot_path(int32_t slot);

int save_write(const Campaign *c, const char *path);
int save_read(Campaign *c, const char *path);

#endif
