/****************************************************************************

    midi.h
    midi music support


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
#include "midi.h"
#include "roland.h"
#include "utils.h"
#ifdef HAVE_SDL_MIXER
#include <SDL_mixer.h>
#endif
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

int midi_disabled = 0;

#ifdef HAVE_SDL_MIXER
static Mix_Music *music = 0;
#endif

static int music_running = 0;

/*
  hook for music finished event
 */
void
hook_music_finished()
{
    music_running = 0;
}

/*
  stops music return nonzero if music was playing
 */
int
stop_midi(void)
{
#ifdef LOTR_ROLAND_OUTPUT
    if (roland_enabled())
        return roland_stop();
#endif
    int was_running = music_running;

#ifdef HAVE_SDL_MIXER
    if (!music)
        return 0;

    Mix_HaltMusic();
    Mix_FreeMusic(music);
    music = 0;
#endif

    /* HACK: hook_music_finished should be called on Mix_HaltMusic
       according to documentation but it is not */
    hook_music_finished();

    return was_running;
}


#ifdef HAVE_SDL_MIXER
static void
start_music(SDL_RWops *stream, int loops)
{
    if (!stream) {
        fprintf(stderr, "lotr: cannot open music stream: %s\n", SDL_GetError());
        return;
    }
    Mix_HookMusicFinished(hook_music_finished);
#if SDL_MAJOR_VERSION == 2
    music = Mix_LoadMUS_RW(stream, 1);
#else
    music = Mix_LoadMUS_RW(stream);
#endif
    if (!music) {
        fprintf(stderr, "lotr: cannot load music: %s\n", Mix_GetError());
        return;
    }
    music_running = 1;
    if (Mix_PlayMusic(music, loops) < 0) {
        fprintf(stderr, "lotr: cannot play music: %s\n", Mix_GetError());
        stop_midi();
    }
}
#endif

void
play_midi(Uint8 *data, int size, int loop, int fade_in)
{
    if (midi_disabled)
        return;

    if (data == NULL || size == 0) {
        if (lotr_current_edition()->audio != LOTR_AUDIO_ARCHIVE)
            return;
        fprintf(stderr, "lotr: trying to play an empty midi file\n");
        exit(1);
    }

    stop_midi();
#ifdef LOTR_ROLAND_OUTPUT
    if (roland_enabled()) {
        roland_play(data, size, loop, fade_in);
        return;
    }
#else
    (void)fade_in;
#endif

#ifdef HAVE_SDL_MIXER
    int repeats = lotr_current_edition()->audio == LOTR_AUDIO_TOWERS ? 0 : 1;
    start_music(SDL_RWFromConstMem(data, size), loop ? -1 : repeats);
#else
    hook_music_finished();
#endif

}

void
play_music_file(FILE *file, int loop)
{
#if defined(HAVE_SDL_MIXER) && SDL_MAJOR_VERSION == 2
    if (!midi_disabled) {
        SDL_RWops *stream = SDL_RWFromFP(file, SDL_TRUE);
        if (stream) {
            stop_midi();
            start_music(stream, loop ? -1 : 0);
            return;
        }
        fprintf(stderr, "lotr: cannot open CD music stream: %s\n", SDL_GetError());
    }
#endif
    fclose(file);
}

int
pause_music_for_movie(void)
{
#ifdef HAVE_SDL_MIXER
    if (music && Mix_PlayingMusic() && !Mix_PausedMusic()) {
        Mix_PauseMusic();
        return 1;
    }
#endif
    return 0;
}

void
resume_music_after_movie(int paused)
{
#ifdef HAVE_SDL_MIXER
    if (paused && music)
        Mix_ResumeMusic();
#else
    (void)paused;
#endif
}

/* The shipped XMIDI cues have one EVNT stream on a 120 Hz clock. */
const Uint8 *
midi_find_xmidi_events(const Uint8 *data, size_t size, size_t *length, int depth)
{
    while (size >= 8) {
        Uint32 chunk = (Uint32)data[4] << 24 | (Uint32)data[5] << 16 |
                       (Uint32)data[6] << 8 | data[7];
        if (chunk > size - 8)
            return NULL;
        if (!memcmp(data, "EVNT", 4)) {
            *length = chunk;
            return data + 8;
        }
        if (depth && chunk >= 4 &&
            (!memcmp(data, "FORM", 4) || !memcmp(data, "CAT ", 4))) {
            const Uint8 *events = midi_find_xmidi_events(data + 12, chunk - 4, length, depth - 1);
            if (events)
                return events;
        }
        size_t skip = 8 + (size_t)chunk + (chunk & 1);
        if (skip > size)
            return NULL;
        data += skip;
        size -= skip;
    }
    return NULL;
}

int
midi_read_length(const Uint8 **data, const Uint8 *end, Uint32 *length)
{
    *length = 0;
    for (int i = 0; i < 4 && *data < end; ++i) {
        Uint8 byte = *(*data)++;
        *length = (*length << 7) | (byte & 127);
        if (byte < 128)
            return 1;
    }
    return 0;
}

static Uint8 *
write_midi_length(Uint8 *out, Uint32 length)
{
    Uint8 bytes[4];
    int count = 0;
    bytes[count++] = length & 127;
    while ((length >>= 7))
        bytes[count++] = (length & 127) | 128;
    while (count)
        *out++ = bytes[--count];
    return out;
}

typedef struct {
    Uint32 tick;
    size_t order;
    const Uint8 *data;
    size_t size;
    Uint8 note_off[3];
} XmidiEvent;

static int
compare_xmidi_events(const void *a, const void *b)
{
    const XmidiEvent *left = a, *right = b;
    if (left->tick != right->tick)
        return left->tick < right->tick ? -1 : 1;
    return left->order < right->order ? -1 : left->order != right->order;
}

Uint8 *
convert_xmidi(const Uint8 *data, int size, int *midi_size)
{
    size_t length;
    const Uint8 *stream = midi_find_xmidi_events(data, size, &length, 4);
    *midi_size = 0;
    if (!stream || !length || length > (INT_MAX - 32) / 8 ||
        length > INT_MAX / sizeof(XmidiEvent))
        return NULL;

    const Uint8 *end = stream + length;
    XmidiEvent *events = lotr_malloc((int)(length * sizeof(*events)));
    size_t count = 0;
    Uint32 tick = 0, delay;
    while (stream < end) {
        while (stream < end && *stream < 128) {
            if (tick > 0x0fffffff - *stream)
                goto invalid;
            tick += *stream++;
        }
        if (stream == end)
            goto invalid;

        const Uint8 *start = stream;
        Uint8 status = *stream++, type = 0;
        if (status == 0xff || status == 0xf0 || status == 0xf7) {
            if (status == 0xff) {
                if (stream == end)
                    goto invalid;
                type = *stream++;
            }
            if (!midi_read_length(&stream, end, &delay) || delay > (size_t)(end - stream))
                goto invalid;
            stream += delay;
            if (status == 0xff && type == 0x2f)
                break;
            if (status == 0xff && type == 0x51)
                continue;  /* XMIDI event delays already use the fixed clock. */
        } else if (status < 0xf0) {
            int command = status >> 4;
            int arguments = command == 0xc || command == 0xd ? 1 : 2;
            if (end - stream < arguments || stream[0] >= 128 ||
                (arguments == 2 && stream[1] >= 128))
                goto invalid;
            stream += arguments;
            if (command == 0xb && start[1] >= 110 && start[1] <= 119)
                continue;  /* Miles driver commands have no SMF equivalent. */
        } else {
            goto invalid;
        }

        events[count] = (XmidiEvent){tick, count, start, (size_t)(stream - start), {0}};
        ++count;
        if ((status & 0xf0) == 0x90) {
            if (!midi_read_length(&stream, end, &delay) || delay > 0x0fffffff - tick)
                goto invalid;
            if (start[2]) {
                events[count] = (XmidiEvent){tick + delay, count, NULL, 3,
                    {(Uint8)(0x80 | (status & 15)), start[1], 0}};
                ++count;
            }
        }
    }

    qsort(events, count, sizeof(*events), compare_xmidi_events);
    /* SMF format 0, 60 ticks per quarter at the default 500,000 us tempo. */
    static const Uint8 header[] = {
        'M','T','h','d', 0,0,0,6, 0,0, 0,1, 0,60,
        'M','T','r','k', 0,0,0,0
    };
    Uint8 *midi = lotr_malloc((int)(sizeof(header) + length * 8 + 8));
    memcpy(midi, header, sizeof(header));
    Uint8 *out = midi + sizeof(header);
    Uint32 last = 0;
    for (size_t i = 0; i < count; ++i) {
        const XmidiEvent *event = &events[i];
        out = write_midi_length(out, event->tick - last);
        memcpy(out, event->data ? event->data : event->note_off, event->size);
        out += event->size;
        last = event->tick;
    }
    out = write_midi_length(out, tick > last ? tick - last : 0);
    *out++ = 0xff;
    *out++ = 0x2f;
    *out++ = 0;
    Uint32 track = (Uint32)(out - midi - sizeof(header));
    for (int i = 0; i < 4; ++i)
        midi[21 - i] = (Uint8)(track >> (8 * i));
    *midi_size = (int)(out - midi);
    free(events);
    return midi;

invalid:
    free(events);
    return NULL;
}
