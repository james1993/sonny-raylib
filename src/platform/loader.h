/* Decoding pictures off the main thread.
 *
 * Reading a PNG is the slow half of putting a picture on the card: a comic's
 * panel or a zone's backdrop is fifteen to thirty milliseconds of inflating,
 * which is a whole frame at thirty a second. The card itself can only be
 * spoken to from the main thread, so the split is: this decodes, and the
 * asset cache uploads whatever has come back.
 *
 * Nothing waits on it. A picture asked for before it is ready is loaded the
 * old way, on the spot, so what is drawn never depends on how quickly this
 * got there -- only how long the frame took.
 */
#ifndef SONNY_LOADER_H
#define SONNY_LOADER_H

#include <stdint.h>
#include "raylib.h"

void loader_start(void);
void loader_stop(void);

/* Ask for `path` to be decoded, under `key`. Asking twice for the same key
   while it is outstanding does nothing. Returns 0 when the queue is full. */
int loader_request(int32_t key, const char *path);

/* Whether `key` has been asked for and not yet collected. */
int loader_pending(int32_t key);

/* Hand back one decoded picture, if any is ready: its key and the image,
   which the caller then owns. Returns 0 when nothing is ready. */
int loader_collect(int32_t *key, Image *image);

/* Collect `key` in particular, waiting for it if the worker has it in hand.
   Returns 0 -- and leaves the image empty -- when it was never asked for or
   could not be read. */
int loader_wait(int32_t key, Image *image);

#endif
