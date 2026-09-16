#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "assets.h"
#include "raylib.h"

#define AUDIO_CACHE_MAX 128
/* my_sound1..my_sound3 in the original. */
#define EFFECT_CHANNELS 3

typedef struct {
    const char *name;
    Sound       sound;
    int32_t     ok;
} CachedSound;

static CachedSound cache[AUDIO_CACHE_MAX];
static int32_t cache_count;
static Sound channels[EFFECT_CHANNELS];
static int32_t channel_used[EFFECT_CHANNELS];
static int32_t channel_next;
static int32_t ready;

static Music music;
static int32_t music_playing;
static char music_name[64];

/* The cutscene narration, a second stream so it does not fight the music. */
static Music narration;
static int32_t narration_playing;

/* Frames per audio buffer. raylib keeps two of these per stream, so this is
   about three quarters of a second of slack at 44.1 kHz. */
#define MUSIC_BUFFER_FRAMES 16384

void audio_init(void)
{
    if (ready)
        return;
    /* The music is refilled from the frame loop, a buffer at a time, so
       however long the loop is away is how long the stream has to live on
       what it already holds. raylib's default buffer is a fifth of a second
       at most, which a backgrounded window can easily overrun; this gives it
       most of a second instead. It has to be set before the device is
       opened, because that is when the size is taken. */
    SetAudioStreamBufferSizeDefault(MUSIC_BUFFER_FRAMES);
    InitAudioDevice();
    ready = IsAudioDeviceReady();
}

int audio_ready(void)
{
    return ready;
}

int32_t audio_loaded_count(void)
{
    int32_t n = 0;
    for (int32_t i = 0; i < cache_count; i++)
        if (cache[i].ok)
            n++;
    return n;
}

static const Sound *load_sound(const char *name)
{
    const AssetEntry *entry = asset_find(name);
    if (!entry || entry->frame_count == 0)
        return NULL;

    for (int32_t i = 0; i < cache_count; i++)
        if (cache[i].name == entry->name)
            return cache[i].ok ? &cache[i].sound : NULL;
    if (cache_count >= AUDIO_CACHE_MAX)
        return NULL;

    CachedSound *slot = &cache[cache_count++];
    slot->name = entry->name;
    slot->ok = 0;

    char path[1024];
    snprintf(path, sizeof(path), "%s", entry->frames[0]);
    if (FileExists(path)) {
        slot->sound = LoadSound(path);
        slot->ok = (slot->sound.frameCount > 0);
    }
    return slot->ok ? &slot->sound : NULL;
}

void audio_play(const char *name)
{
    if (!ready || !name || !name[0])
        return;
    const Sound *sound = load_sound(name);
    if (!sound)
        return;

    /* Take the next channel in the rotation, stopping whatever it held. */
    int32_t channel = channel_next;
    channel_next = (channel_next + 1) % EFFECT_CHANNELS;

    if (channel_used[channel]) {
        StopSound(channels[channel]);
        UnloadSoundAlias(channels[channel]);
    }
    channels[channel] = LoadSoundAlias(*sound);
    channel_used[channel] = 1;
    PlaySound(channels[channel]);
}

/* Everything the three effect channels are playing, cut off. The original
   stops my_sound1..3 when the space bar skips a line of speech, so the voice
   over it was playing goes with it. */
void audio_stop_effects(void)
{
    if (!ready)
        return;
    for (int32_t i = 0; i < EFFECT_CHANNELS; i++)
        if (channel_used[i])
            StopSound(channels[i]);
}

void audio_music(const char *name)
{
    if (!ready)
        return;
    if (!name || !name[0]) {
        if (music_playing) {
            StopMusicStream(music);
            UnloadMusicStream(music);
            music_playing = 0;
            music_name[0] = 0;
        }
        return;
    }
    if (music_playing && strcmp(music_name, name) == 0)
        return;

    const AssetEntry *entry = asset_find(name);
    if (!entry || entry->frame_count == 0)
        return;
    if (!FileExists(entry->frames[0]))
        return;

    if (music_playing) {
        StopMusicStream(music);
        UnloadMusicStream(music);
        music_playing = 0;
    }
    music = LoadMusicStream(entry->frames[0]);
    if (music.frameCount == 0)
        return;
    music.looping = true;
    SetMusicVolume(music, 0.45f);
    PlayMusicStream(music);
    music_playing = 1;
    snprintf(music_name, sizeof(music_name), "%s", name);
}

int audio_narration(const char *name)
{
    audio_narration_stop();
    if (!ready || !name || !name[0])
        return 0;
    const AssetEntry *entry = asset_find(name);
    if (!entry || entry->frame_count == 0 || !FileExists(entry->frames[0]))
        return 0;
    narration = LoadMusicStream(entry->frames[0]);
    if (narration.frameCount == 0)
        return 0;
    narration.looping = false;
    PlayMusicStream(narration);
    narration_playing = 1;
    return 1;
}

void audio_narration_stop(void)
{
    if (!narration_playing)
        return;
    StopMusicStream(narration);
    UnloadMusicStream(narration);
    narration_playing = 0;
}

int audio_narration_playing(void)
{
    return narration_playing && IsMusicStreamPlaying(narration);
}

float audio_narration_time(void)
{
    return narration_playing ? GetMusicTimePlayed(narration) : 0.0f;
}

void audio_update(void)
{
    if (!ready)
        return;
    if (music_playing)
        UpdateMusicStream(music);
    if (narration_playing)
        UpdateMusicStream(narration);
}

void audio_shutdown(void)
{
    if (!ready)
        return;
    for (int32_t i = 0; i < EFFECT_CHANNELS; i++)
        if (channel_used[i]) {
            StopSound(channels[i]);
            UnloadSoundAlias(channels[i]);
            channel_used[i] = 0;
        }
    if (music_playing) {
        StopMusicStream(music);
        UnloadMusicStream(music);
        music_playing = 0;
    }
    audio_narration_stop();
    for (int32_t i = 0; i < cache_count; i++)
        if (cache[i].ok)
            UnloadSound(cache[i].sound);
    cache_count = 0;
    CloseAudioDevice();
    ready = 0;
}
