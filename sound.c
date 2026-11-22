/****************************************************************************

    sound.c
    sound support


    Lord of the Rings game engine

    Copyright (C) 2003  Michal Benes

    Lord of the Rings game engine is free software;
    you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This code is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this code; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*****************************************************************************/

#include "lotr.h"
#include "archive.h"
#include "lotr_sdl.h"
#include "midi.h"
#include "roland.h"
#include "sound.h"
#include "utils.h"
#include "voc.h"
#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "sounds_cd.h"
#define ARCHIVE_SOUND_COUNT 25

static int music_muted;

enum { SOUND_CAPACITY = 36 };
static Uint8 *sound_data[SOUND_CAPACITY];
static int sound_sizes[SOUND_CAPACITY];

#define MUSIC_MAIN_INDEX 8


/* Both mixers accept effects on a 22,050 Hz mono PCM grid. */
static void
resample_effect(int index, SampleInfo sample)
{
    if (sample.size <= 0) {
        sound_sizes[index] = 0;
        return;
    }

    Uint64 scale = (Uint64)22050 * sample.rate_denominator;
    Uint64 frames = ((Uint64)sample.size * scale + sample.rate_numerator / 2) / sample.rate_numerator;
    if (frames > INT_MAX) {
        fprintf(stderr, "lotr: sampled effect %d is too large\n", index);
        exit(1);
    }

    Uint8 *source = sound_data[index];
    Uint8 *pcm = lotr_malloc((int)frames);
    /* Hold each original sample until the next source sample begins. */
    for (Uint64 frame = 0; frame < frames; ++frame)
        pcm[frame] = source[frame * sample.rate_numerator / scale];

    free(source);
    sound_data[index] = pcm;
    sound_sizes[index] = (int)frames;
}


/*
  sound initializations
*/

static void
load_archive_sounds(void)
{
    int i;
    Archive *archive = archive_ndx_open("snd");
    int preserve_timing = 0;
#ifdef LOTR_ROLAND_OUTPUT
    preserve_timing = roland_enabled();
#endif

    for (i = 0; i < ARCHIVE_SOUND_COUNT; ++i) {
        sound_data[i] = archive_read(archive, i);
        sound_sizes[i] = archive_data_size(archive, i);

        if (i < MUSIC_MAIN_INDEX && lotr_current_edition()->data_format == LOTR_DATA_STANDARD)
            resample_effect(i, (SampleInfo){sound_sizes[i], 16000, 1});

        /* Keep the historical tempo adjustment for software playback. */
        if (!preserve_timing && sound_sizes[i] > 13
            && sound_data[i][0] == 'M'
            && sound_data[i][1] == 'T'
            && sound_data[i][2] == 'h'
            && sound_data[i][3] == 'd'
            && sound_data[i][12] == 0x00 && sound_data[i][13] == 0x60) {
            int tempo = 240;
            sound_data[i][12] = tempo / 0x100;
            sound_data[i][13] = tempo & 0xff;
        }
    }

    archive_close(archive);
}

static void
load_cd_sounds(void)
{
    int i;
    for (i = 0; i < CD_SOUND_COUNT; ++i) {
        FILE *adlfile = lotr_fopen(cd_sound_names[i], "rb");
        sound_sizes[i] = lotr_filelen(adlfile);
        sound_data[i] = lotr_malloc(sound_sizes[i]);
        if (fread(sound_data[i], 1, sound_sizes[i], adlfile) !=
            sound_sizes[i]) {
            fprintf(stderr, "lotr: can not read file %s\n", cd_sound_names[i]);
            perror("lotr");
            exit(1);
        }
        fclose(adlfile);
        int size = sound_sizes[i];
        if (voc_has_signature(sound_data[i], size)) {
            SampleInfo decoded;
            Uint8 *pcm = voc_decode(sound_data[i], size, &decoded);
            free(sound_data[i]);
            sound_data[i] = pcm;
            size = decoded.size;
        }
        /* The CD driver sets a fixed Sound Blaster time constant for effects. */
        resample_effect(i, (SampleInfo){size, 1000000, 256 - 0xd2});
        if (!sound_sizes[i])
            fprintf(stderr, "lotr: unsupported or empty CD effect %s\n",
                    cd_sound_names[i]);
    }

}


/* Vol. II SOUND operands index this table in the retail DOS executable. */
static const char *const towers_sound_names[] = {
    "intro", "fellow", "gollum", "entsong", "nazgul", "saruman", "gandalf",
    "theoden", "eowyn", "battle", "walkf", "walka", "walkm", "entmarch",
    "orc", "elf", "dread", "happy", "success", "harad", "vampire", "gallop",
    "scream", "moan", "olitrmp", "crow", "horn", "clash", "hit", "miss",
    "howl", "laugh", "beep", "buzz", "magics", "magicf"
};

enum {
    TOWERS_BATTLE = 9, TOWERS_WALKING = 10, TOWERS_SUCCESS = 18,
    TOWERS_FIRST_EFFECT = 21, TOWERS_MAGIC_SUCCESS = 34, TOWERS_MAGIC_FAILURE = 35
};

static int towers_sampled[SDL_arraysize(towers_sound_names)];

static Uint8 *
read_towers_sound(const char *name, const char *extension, int *size)
{
    char filename[20];
    snprintf(filename, sizeof(filename), "%s.%s", name, extension);
    FILE *file = lotr_try_open_asset(filename, 0);
    if (!file)
        return NULL;
    int length = lotr_filelen(file);
    Uint8 *data = length > 0 ? lotr_malloc(length) : NULL;
    if (data && fread(data, 1, length, file) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(file);
    if (data)
        *size = length;
    return data;
}

static int
load_towers_sample(int index, Uint8 *data, int size)
{
    SampleInfo sample;
    if (voc_has_signature(data, size)) {
        Uint8 *pcm = voc_decode(data, size, &sample);
        if (!pcm)
            return 0;
        free(data);
        data = pcm;
    } else if (size >= 8 && !memcmp(data, "\xff\xff\xff\xff", 4)) {
        /* The speaker assets store 6-bit PCM and an 8253 timer divisor. */
        unsigned length = data[4] | data[5] << 8;
        unsigned divisor = data[6] | data[7] << 8;
        if (!length || length > (unsigned)size - 8 || !divisor || divisor > 256)
            return 0;
        for (unsigned i = 0; i < length; ++i)
            data[i] = (data[i + 8] & 63) * 4;
        sample = (SampleInfo){(int)length, 1193182, divisor};
    } else {
        return 0;
    }
    sound_data[index] = data;
    resample_effect(index, sample);
    towers_sampled[index] = 1;
    return 1;
}

static void
load_towers_sounds(void)
{
    int use_roland = 0;
#ifdef LOTR_ROLAND_OUTPUT
    use_roland = roland_enabled();
#endif
    for (int i = 0; i < SDL_arraysize(towers_sound_names); ++i) {
        towers_sampled[i] = 0;
        const char *name = towers_sound_names[i];
        /* The FM intro contains setup events only. Prefer the Roland title score. */
        const char *extensions[] = {i == 0 ? "rol" : "adl", "pcm", i == 0 ? "adl" : "rol"};
        if (use_roland && i < TOWERS_FIRST_EFFECT) {
            extensions[0] = "rol";
            extensions[2] = "adl";
        }
        for (size_t j = 0; j < SDL_arraysize(extensions) && !sound_data[i]; ++j) {
            int size;
            Uint8 *data = read_towers_sound(name, extensions[j], &size);
            if (!data)
                continue;
            if (load_towers_sample(i, data, size))
                break;
            if (use_roland && !strcmp(extensions[j], "rol")) {
                size_t event_size;
                if (midi_find_xmidi_events(data, size, &event_size, 4) && event_size) {
                    sound_data[i] = data;
                    sound_sizes[i] = size;
                    break;
                }
            } else if (!use_roland && size >= 4 && !memcmp(data, "FORM", 4)) {
                sound_data[i] = convert_xmidi(data, size, &sound_sizes[i]);
            }
            free(data);
        }
    }
}

void
sound_init(void)
{
    LotrAudio audio = lotr_current_edition()->audio;
    if (audio == LOTR_AUDIO_ARCHIVE)
        load_archive_sounds();
    else if (audio == LOTR_AUDIO_CD)
        load_cd_sounds();
    else if (audio == LOTR_AUDIO_TOWERS)
        load_towers_sounds();
}


/*
  sound deinitializations
*/

void
sound_close(void)
{

    int i;

#ifdef LOTR_ROLAND_OUTPUT
    if (roland_enabled())
        roland_close();
    else
#endif
        stop_midi();
    for (i = 0; i < SOUND_CAPACITY; ++i) {
        if (sound_data[i])
            stop_sample(sound_data[i]);
        free(sound_data[i]);
        sound_data[i] = NULL;
        sound_sizes[i] = 0;
    }
}


typedef enum { SOUND_PCM, SOUND_MIDI, SOUND_STREAM } SoundSource;

/* Return whether a cue was dispatched, so CD music can try the next file. */
static int
play_cue(SoundSource source, int index, const char *filename, int loop)
{
    if (source == SOUND_STREAM) {
        if (music_muted || midi_disabled)
            return 0;
        FILE *file = lotr_try_open_asset(filename, 0);
        if (!file)
            return 0;
        play_music_file(file, loop);
        return 1;
    }

    if (index < 0 || index >= SOUND_CAPACITY)
        return 0;
    if (source == SOUND_PCM) {
        play_sample(sound_data[index], sound_sizes[index], LOTR_PCM_U8);
    } else {
        if (lotr_current_edition()->audio == LOTR_AUDIO_TOWERS &&
            (music_muted || !sound_data[index]))
            return 0;
        int fade_in = index == 0 && lotr_current_edition()->audio == LOTR_AUDIO_TOWERS;
        play_midi(sound_data[index], sound_sizes[index], loop, fade_in);
    }
    return 1;
}

void
sound_play(int index)
{
    if (index < 0)
        return;
    switch (lotr_current_edition()->audio) {
        case LOTR_AUDIO_ARCHIVE:
            if (index > 8)
                play_cue(SOUND_PCM, index - 8, NULL, 0);
            else
                play_cue(SOUND_MIDI, index + 8, NULL, 0);
            break;
        case LOTR_AUDIO_CD:
            if (index <= 0x0a) {
                char name[sizeof("cd-cue-00.wav")];
                snprintf(name, sizeof(name), "cd-cue-%02X.wav", (unsigned)index);
                play_cue(SOUND_STREAM, 0, name, 0);
            } else if (index - 11 < CD_SOUND_COUNT) {
                play_cue(SOUND_PCM, index - 11, NULL, 0);
            }
            break;
        case LOTR_AUDIO_TOWERS:
            if (index < SDL_arraysize(towers_sampled))
                play_cue(towers_sampled[index] ? SOUND_PCM : SOUND_MIDI, index, NULL, 0);
            break;
    }
}

void
play_music(void)
{
    switch (lotr_current_edition()->audio) {
        case LOTR_AUDIO_ARCHIVE:
            play_cue(SOUND_MIDI, MUSIC_MAIN_INDEX, NULL, 1);
            break;
        case LOTR_AUDIO_TOWERS:
            play_cue(towers_sampled[TOWERS_WALKING] ? SOUND_PCM : SOUND_MIDI,
                     TOWERS_WALKING, NULL, 1);
            break;
        case LOTR_AUDIO_CD: {
            static const char *const tracks[] = {
                "cd-track-02.wav", "cd-track-03.wav", "cd-track-04.wav"
            };
            static int selected = -1;
            int first = selected < 0 ? SDL_GetTicks() % 3 : selected;
            for (int i = 0; i < 3; ++i) {
                int index = (first + i) % 3;
                if (play_cue(SOUND_STREAM, 0, tracks[index], 1)) {
                    selected = index;
                    break;
                }
            }
            break;
        }
    }
}

/*
  starts stop playing the music track
*/

void
toggle_music(void)
{
    int was_playing = stop_midi();
    LotrAudio audio = lotr_current_edition()->audio;
    if (audio == LOTR_AUDIO_CD || audio == LOTR_AUDIO_TOWERS)
        music_muted = was_playing;
    if (!was_playing)
        play_music();
}


void
music_spell_performed(void)
{
    switch (lotr_current_edition()->audio) {
        case LOTR_AUDIO_ARCHIVE: sound_play(7); break;
        case LOTR_AUDIO_CD: sound_play(11); break;
        case LOTR_AUDIO_TOWERS: sound_play(TOWERS_MAGIC_SUCCESS); break;
    }
}

void
music_spell_failed(void)
{
    switch (lotr_current_edition()->audio) {
        case LOTR_AUDIO_ARCHIVE: sound_play(8); break;
        case LOTR_AUDIO_CD: sound_play(12); break;
        case LOTR_AUDIO_TOWERS: sound_play(TOWERS_MAGIC_FAILURE); break;
    }
}

/* DOS v1.3 selects the Roland bank at 8 for music cues 0 through 8. */
static int
fellowship_combat_music(int victory)
{
#ifdef LOTR_ROLAND_OUTPUT
    if (lotr_current_edition()->data_format == LOTR_DATA_STANDARD && roland_enabled())
        return MUSIC_MAIN_INDEX + 4 + victory;
#endif
    return 21 + victory;
}

void
music_combat_started(void)
{
    if (lotr_current_edition()->audio == LOTR_AUDIO_ARCHIVE)
        play_cue(SOUND_MIDI, fellowship_combat_music(0), NULL, 0);
    else if (lotr_current_edition()->audio == LOTR_AUDIO_TOWERS)
        sound_play(TOWERS_BATTLE);
}

void
music_combat_won(void)
{
    if (lotr_current_edition()->audio == LOTR_AUDIO_ARCHIVE)
        play_cue(SOUND_MIDI, fellowship_combat_music(1), NULL, 0);
    else if (lotr_current_edition()->audio == LOTR_AUDIO_TOWERS)
        sound_play(TOWERS_SUCCESS);
}
