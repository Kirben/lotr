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
#include "sound.h"
#include "utils.h"
#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "sounds_cd.h"
#define ARCHIVE_SOUND_COUNT 25

Uint8 *music_data;
int music_size;
static int music_muted;

enum { SOUND_CAPACITY = 36 };
static Uint8 *sound_data[SOUND_CAPACITY];
static int sound_sizes[SOUND_CAPACITY];

#define MUSIC_MAIN_INDEX 8


/* Both mixers accept effects on a 22,050 Hz mono PCM grid. */
static void
resample_effect(int index, unsigned rate_numerator, unsigned rate_denominator)
{
    int size = sound_sizes[index];
    if (size <= 0)
        return;

    Uint64 scale = 22050u * rate_denominator;
    Uint64 frames = ((Uint64)size * scale + rate_numerator / 2) / rate_numerator;
    if (frames > INT_MAX) {
        fprintf(stderr, "lotr: sampled effect %d is too large\n", index);
        exit(1);
    }

    Uint8 *source = sound_data[index];
    Uint8 *pcm = lotr_malloc((int)frames);
    /* Hold each original sample until the next source sample begins. */
    for (Uint64 frame = 0; frame < frames; ++frame)
        pcm[frame] = source[frame * rate_numerator / scale];

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

    for (i = 0; i < ARCHIVE_SOUND_COUNT; ++i) {
        sound_data[i] = archive_read(archive, i);
        sound_sizes[i] = archive_data_size(archive, i);

        if (i < MUSIC_MAIN_INDEX && lotr_current_edition()->data_format == LOTR_DATA_STANDARD)
            resample_effect(i, 16000, 1);

        /* Fix tempo in midi files */
        if (sound_sizes[i] > 13
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

    music_data = sound_data[MUSIC_MAIN_INDEX];
    music_size = sound_sizes[MUSIC_MAIN_INDEX];

    archive_close(archive);
}

/* Some CD effects use a Creative Voice container instead of raw PCM. */
static int
unwrap_cd_voice(Uint8 *data, int size)
{
    static const char signature[] = "Creative Voice File\x1a";
    if (size < 20 || memcmp(data, signature, 20) != 0)
        return size;
    if (size < 26)
        return 0;

    int pos = data[20] | (data[21] << 8);
    if (pos < 26)
        return 0;
    while (pos <= size - 4 && data[pos]) {
        int type = data[pos];
        int length = data[pos + 1] | (data[pos + 2] << 8) | (data[pos + 3] << 16);
        pos += 4;
        if (length > size - pos)
            return 0;
        if (type == 8 && (length != 4 || data[pos + 2] || data[pos + 3]))
            return 0;
        if (type == 1) {
            if (length <= 2 || data[pos + 1])
                return 0;
            memmove(data, data + pos + 2, length - 2);
            return length - 2;
        }
        pos += length;
    }
    return 0;
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
        sound_sizes[i] = unwrap_cd_voice(sound_data[i], sound_sizes[i]);
        /* Sound Blaster time constant 0xd2 gives 1 MHz / (256 - 0xd2). */
        resample_effect(i, 1000000, 256 - 0xd2);
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
    TOWERS_MAGIC_SUCCESS = 34, TOWERS_MAGIC_FAILURE = 35
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
    if (size >= 32 && !memcmp(data, "Creative Voice File\x1a", 20)) {
        /* Retail VOC cues are a single uncompressed mono Type 1 block. */
        unsigned offset = data[20] | data[21] << 8;
        if (offset > (unsigned)size - 6 || data[offset] != 1)
            return 0;
        unsigned divisor = 256 - data[offset + 4];
        int payload = unwrap_cd_voice(data, size);
        if (!payload)
            return 0;
        sound_sizes[index] = payload;
        sound_data[index] = data;
        resample_effect(index, 1000000, divisor);
    } else if (size >= 8 && !memcmp(data, "\xff\xff\xff\xff", 4)) {
        /* The speaker assets store 6-bit PCM and an 8253 timer divisor. */
        unsigned length = data[4] | data[5] << 8;
        unsigned divisor = data[6] | data[7] << 8;
        if (!length || length > (unsigned)size - 8 || !divisor || divisor > 256)
            return 0;
        for (unsigned i = 0; i < length; ++i)
            data[i] = (data[i + 8] & 63) * 4;
        sound_sizes[index] = length;
        sound_data[index] = data;
        resample_effect(index, 1193182, divisor);
    } else {
        return 0;
    }
    towers_sampled[index] = 1;
    return 1;
}

static void
load_towers_sounds(void)
{
    for (int i = 0; i < SDL_arraysize(towers_sound_names); ++i) {
        towers_sampled[i] = 0;
        const char *name = towers_sound_names[i];
        /* The FM intro contains setup events only. Prefer the Roland title score. */
        const char *extensions[] = {i == 0 ? "rol" : "adl", "pcm", i == 0 ? "adl" : "rol"};
        for (size_t j = 0; j < SDL_arraysize(extensions) && !sound_data[i]; ++j) {
            int size;
            Uint8 *data = read_towers_sound(name, extensions[j], &size);
            if (!data)
                continue;
            if (load_towers_sample(i, data, size))
                break;
            if (size >= 4 && !memcmp(data, "FORM", 4))
                sound_data[i] = convert_xmidi(data, size, &sound_sizes[i]);
            free(data);
        }
    }
}

static void
play_towers_sound(int index, int loop)
{
    if (index < 0 || index >= SDL_arraysize(towers_sound_names) || !sound_data[index])
        return;
    if (towers_sampled[index])
        play_sample(sound_data[index], sound_sizes[index], LOTR_PCM_U8);
    else if (!music_muted)
        play_midi(sound_data[index], sound_sizes[index], loop);
}


void
sound_init(void)
{
    LotrAudio audio = lotr_current_edition()->audio;
    music_data = NULL;
    music_size = 0;
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

    stop_midi();
    for (i = 0; i < SOUND_CAPACITY; ++i) {
        if (sound_data[i])
            stop_sample(sound_data[i]);
        free(sound_data[i]);
        sound_data[i] = NULL;
        sound_sizes[i] = 0;
    }
}


/*
  sound play
*/

void
sound_play(int index)
{

    LotrAudio audio = lotr_current_edition()->audio;
    if (audio == LOTR_AUDIO_ARCHIVE) {
        if (index > 8)
            play_sample(sound_data[index - 8], sound_sizes[index - 8], LOTR_PCM_U8);
        else
            play_midi(sound_data[index + 8], sound_sizes[index + 8], 0);
    } else if (audio == LOTR_AUDIO_CD && index >= 0 && index <= 0x0a) {
        /* Play scripted CD cues once, using the ranges extracted from the disc. */
        if (!midi_disabled && !music_muted) {
            char name[sizeof("cd-cue-00.wav")];
            snprintf(name, sizeof(name), "cd-cue-%02X.wav", (unsigned)index);
            FILE *file = lotr_try_open_asset(name, 0);
            if (file)
                play_music_file(file, 0);
        }
    } else if (audio == LOTR_AUDIO_CD && index >= 11 &&
               index - 11 < CD_SOUND_COUNT) {
        play_sample(sound_data[index - 11], sound_sizes[index - 11], LOTR_PCM_U8);
    } else if (audio == LOTR_AUDIO_TOWERS) {
        play_towers_sound(index, 0);
    }

}



/*
  play the main music track
*/

void
play_music(void)
{
    if (lotr_current_edition()->audio == LOTR_AUDIO_CD) {
        if (midi_disabled || music_muted)
            return;
        const char *tracks[] = {"cd-track-02.wav", "cd-track-03.wav", "cd-track-04.wav"};
        static int selected = -1;
        int first = selected < 0 ? SDL_GetTicks() % 3 : selected;
        for (int i = 0; i < 3; ++i) {
            int index = (first + i) % 3;
            FILE *file = lotr_try_open_asset(tracks[index], 0);
            if (file) {
                selected = index;
                play_music_file(file, 1);
                return;
            }
        }
        return;
    }
    if (lotr_current_edition()->audio == LOTR_AUDIO_TOWERS)
        play_towers_sound(TOWERS_WALKING, TRUE);
    else
        play_midi(music_data, music_size, TRUE);
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


/*
  play music if spell was performed
*/

void
music_spell_performed(void)
{
    LotrAudio audio = lotr_current_edition()->audio;
    if (audio == LOTR_AUDIO_ARCHIVE)
        play_midi(sound_data[15], sound_sizes[15], 0);
    else if (audio == LOTR_AUDIO_CD)
        play_sample(sound_data[0], sound_sizes[0], LOTR_PCM_U8);
    else if (audio == LOTR_AUDIO_TOWERS)
        play_towers_sound(TOWERS_MAGIC_SUCCESS, 0);
}


/*
  play music if spell failed
*/

void
music_spell_failed(void)
{
    LotrAudio audio = lotr_current_edition()->audio;
    if (audio == LOTR_AUDIO_ARCHIVE)
        play_midi(sound_data[16], sound_sizes[16], 0);
    else if (audio == LOTR_AUDIO_CD)
        play_sample(sound_data[1], sound_sizes[1], LOTR_PCM_U8);
    else if (audio == LOTR_AUDIO_TOWERS)
        play_towers_sound(TOWERS_MAGIC_FAILURE, 0);
}



/*
  play music if combat started
*/

void
music_combat_started(void)
{
    if (lotr_current_edition()->audio == LOTR_AUDIO_ARCHIVE)
        play_midi(sound_data[21], sound_sizes[21], 0);
    else if (lotr_current_edition()->audio == LOTR_AUDIO_TOWERS)
        play_towers_sound(TOWERS_BATTLE, 0);
}


/*
  play music if combat was won
 */

void
music_combat_won(void)
{
    if (lotr_current_edition()->audio == LOTR_AUDIO_ARCHIVE)
        play_midi(sound_data[22], sound_sizes[22], 0);
    else if (lotr_current_edition()->audio == LOTR_AUDIO_TOWERS)
        play_towers_sound(TOWERS_SUCCESS, 0);
}
