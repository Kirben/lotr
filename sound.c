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
static int cd_music_muted;

static Uint8 *sound_data[CD_SOUND_COUNT];
static int sound_sizes[CD_SOUND_COUNT];

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
}


/*
  sound deinitializations
*/

void
sound_close(void)
{

    int i;

    stop_midi();
    for (i = 0; i < CD_SOUND_COUNT; ++i) {
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
        if (!midi_disabled && !cd_music_muted) {
            char name[sizeof("cd-cue-00.wav")];
            snprintf(name, sizeof(name), "cd-cue-%02X.wav", (unsigned)index);
            FILE *file = lotr_try_open_asset(name, 0);
            if (file)
                play_music_file(file, 0);
        }
    } else if (audio == LOTR_AUDIO_CD && index >= 11 &&
               index - 11 < CD_SOUND_COUNT) {
        play_sample(sound_data[index - 11], sound_sizes[index - 11], LOTR_PCM_U8);
    }

}



/*
  play the main music track
*/

void
play_music(void)
{
    if (lotr_current_edition()->audio == LOTR_AUDIO_CD) {
        if (midi_disabled || cd_music_muted)
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
    play_midi(music_data, music_size, TRUE);
}


/*
  starts stop playing the music track
*/

void
toggle_music(void)
{
    int was_playing = stop_midi();
    if (lotr_current_edition()->audio == LOTR_AUDIO_CD)
        cd_music_muted = was_playing;
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
}



/*
  play music if combat started
*/

void
music_combat_started(void)
{
    if (lotr_current_edition()->audio == LOTR_AUDIO_ARCHIVE)
        play_midi(sound_data[21], sound_sizes[21], 0);
}


/*
  play music if combat was won
 */

void
music_combat_won(void)
{
    if (lotr_current_edition()->audio == LOTR_AUDIO_ARCHIVE)
        play_midi(sound_data[22], sound_sizes[22], 0);
}
