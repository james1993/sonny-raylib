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
void audio_stop_effects(void);

/* Start a looping music track by name, crossfading from whatever is playing.
   Passing NULL stops the music. */
void audio_music(const char *name);
void audio_update(void);

/* A cutscene's narration. In the original this is a stream sound on the
   animation's own timeline, which means the animation does not have its own
   clock: Flash holds the timeline to the audio. So it is played as a stream
   here too and the comic is stepped from its playhead. */
/* Returns whether the track actually started, which decides whether the
   playhead can be used as the animation's clock. */
int  audio_narration(const char *name);
void audio_narration_stop(void);
int  audio_narration_playing(void);
/* Seconds of narration played, which is the animation's own frame clock. */
float audio_narration_time(void);

/* Silence, or not. The one setting the game still asks about: everything the
   game plays goes through the one master volume, so this is all of it --
   effects, music and narration alike. Remembered across audio_init, so it can
   be set before there is a device to set it on. */
void audio_set_muted(int muted);
int  audio_muted(void);

int32_t audio_loaded_count(void);

#endif
