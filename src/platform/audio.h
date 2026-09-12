/* Sound, played by the names the engine uses.
 *
 * The original mixes effects through three channels it cycles between
 * (my_sound1..3), so a new sound never cuts off the one before it but a burst
 * of four does drop the oldest. That behaviour is reproduced rather than
 * replaced with unlimited voices, because it is audible: rapid multi-hit
 * abilities lose their earliest sounds in the original too.
 */
#ifndef SONNY_AUDIO_H
#define SONNY_AUDIO_H

#include <stdint.h>

void audio_init(void);
void audio_shutdown(void);
int  audio_ready(void);

/* Play an effect by export name, through the next channel in the rotation.
   Unknown names are ignored, as attachSound on a missing name is in Flash. */
void audio_play(const char *name);

/* Start a looping music track by name, crossfading from whatever is playing.
   Passing NULL stops the music. */
void audio_music(const char *name);
void audio_update(void);

int32_t audio_loaded_count(void);

#endif
