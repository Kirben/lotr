/****************************************************************************

    av.c
    play av file


    Lord of the Rings game engine

    Copyright (C) 2004  Michal Benes

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
#include "av.h"
#include "graphics.h"
#include "lotr_sdl.h"
#include "midi.h"
#include "timing.h"
#include "utils.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) && defined(HAVE_SDL_MIXER) && SDL_MAJOR_VERSION == 2
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <avrt.h>
#define AV_WINDOWS_AUDIO 1
#endif


#define AV_WIDTH 28
#define AV_HEIGHT 18
#define AV_SAMPLE_LEN 732
#define AV_SAMPLE_RATE 11025

/* Longest sample in AV file 2208444 (in 11khz) */
#define MAX_SAMPLE_GROWTH (6 * 1024 * 1024)
#define AV_CACHE_LIMIT (64 * 1024 * 1024)

#ifdef AV_WINDOWS_AUDIO
typedef struct {
    HWAVEOUT device;
    HANDLE playback_task;
    WAVEHDR header;
} AVAudio;

static void
av_audio_close(AVAudio *audio)
{
    if (audio->device) {
        waveOutReset(audio->device);
        if (audio->header.dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(audio->device, &audio->header, sizeof(audio->header));
        waveOutClose(audio->device);
        audio->device = NULL;
    }
    free(audio->header.lpData);
    audio->header.lpData = NULL;
    if (audio->playback_task) {
        AvRevertMmThreadCharacteristics(audio->playback_task);
        audio->playback_task = NULL;
    }
}

static int
av_audio_open(AVAudio *audio, const Uint8 *data, int size)
{
    const char *driver = SDL_GetCurrentAudioDriver();
    if (midi_disabled || !driver || strcmp(driver, "wasapi") ||
        lotr_current_edition()->audio != LOTR_AUDIO_CD)
        return 0;

    WAVEFORMATEX format = { WAVE_FORMAT_PCM, 2, 44100, 176400, 4, 16, 0 };
    Sint16 *pcm = malloc((size_t)size * 4 * sizeof(*pcm));
    if (!pcm)
        return 0;
    audio->header.lpData = (char *)pcm;
    audio->header.dwBufferLength = size * 4 * sizeof(*pcm);
    for (int i = 0; i < size; ++i) {
        Sint16 value = (Sint8)data[i] * 256;
        for (int j = 0; j < 4; ++j)
            pcm[(size_t)i * 4 + j] = value;
    }
    if (waveOutOpen(&audio->device, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR &&
        waveOutPrepareHeader(audio->device, &audio->header, sizeof(audio->header)) == MMSYSERR_NOERROR &&
        waveOutWrite(audio->device, &audio->header, sizeof(audio->header)) == MMSYSERR_NOERROR) {
        /* Let Windows schedule video deadlines alongside other playback work. */
        DWORD task_index = 0;
        audio->playback_task = AvSetMmThreadCharacteristicsW(L"Playback", &task_index);
        return 1;
    }
    av_audio_close(audio);
    return 0;
}

/* waveOut reports the playback position in output frames, including stereo. */
static int
av_wait_for_audio(AVAudio *audio, Uint32 target)
{
    Uint32 deadline = SDL_GetTicks() + 1000;
    while (!lotr_key_esc()) {
        MMTIME position = { TIME_SAMPLES, { 0 } };
        if (waveOutGetPosition(audio->device, &position, sizeof(position)) != MMSYSERR_NOERROR ||
            position.wType != TIME_SAMPLES)
            return -1;
        if (position.u.sample >= target)
            return position.u.sample - target < AV_SAMPLE_LEN * 4;
        if (SDL_TICKS_PASSED(SDL_GetTicks(), deadline))
            return -1;
        lotr_sleep(2);
        lotr_poll_events();
    }
    return 0;
}
#endif

static const char *const av_names[] = { "sacf",
    "sadf",
    "sagf",
    "saaf",
    "sahf",
    "saif",
    "sajf",
    "sabf",
    "saef",
    "sakf",
    "salf",
    "samf",
    "sanf",
    "saff",
    "saof",
    "sarf",
    "saqf"
};

const char *
av_name(int index)
{
    return (unsigned int)index < sizeof(av_names) / sizeof(av_names[0]) ?
        av_names[index] : NULL;
}

/*
  play one audio sample
 */
static int
read_av_audio_sample(Uint8 *audiobuf, int *audiobufpos, SDL_RWops *avfile, Uint8 pcm_bias)
{
    Uint8 sample[AV_SAMPLE_LEN];
    Uint8 *buf;
    int i;

    if (SDL_RWread(avfile, sample, AV_SAMPLE_LEN, 1) != 1)
        return 0;

    if (*audiobufpos + 2 * AV_SAMPLE_LEN > MAX_SAMPLE_GROWTH) {
        fprintf(stderr, "lotr: Too long AV video\n");
        exit(1);
    }

    buf = audiobuf + *audiobufpos;

    /* Upsample unsigned 11,025 Hz PCM, then apply the mixer's byte encoding. */
    *(buf++) = sample[0] ^ pcm_bias;
    *(buf++) = sample[0] ^ pcm_bias;
    for (i = 1; i < AV_SAMPLE_LEN; ++i) {
        *(buf++) = ((sample[i - 1] + sample[i]) / 2) ^ pcm_bias;
        *(buf++) = sample[i] ^ pcm_bias;
    }
    *audiobufpos += 2 * AV_SAMPLE_LEN;
    return 1;
}

/* The interleaved audio must be complete before the mixer reads it. */
static int
load_av_audio(Uint8 *audio, SDL_RWops *file)
{
    Uint8 index[2 * AV_WIDTH * AV_HEIGHT];
    int size = 0;
    Uint8 pcm_bias = lotr_current_edition()->audio == LOTR_AUDIO_CD ? 0x80 : 0;
    for (int i = 0; i < 14; ++i)
        if (!read_av_audio_sample(audio, &size, file, pcm_bias))
            return 0;
    Sint64 first_frame = SDL_RWtell(file);
    if (first_frame < 0)
        return 0;

    while (SDL_RWread(file, index, sizeof(index), 1) == 1) {
        long tiles = 0;
        for (size_t i = 0; i < sizeof(index); i += 2)
            if (index[i] == 0xff && index[i + 1] == 0xff)
                tiles++;
        if (SDL_RWseek(file, tiles * 64, RW_SEEK_CUR) < 0 ||
            !read_av_audio_sample(audio, &size, file, pcm_bias))
            break;
    }
    return SDL_RWseek(file, first_frame, RW_SEEK_SET) >= 0 ? size : 0;
}

/*
   play av
*/
void
av_play(const char *name)
{
    SDL_RWops *avfile = NULL;
    Uint8 *movie_data = NULL;
    char *fullname;             /* name + suffix */
    Uint8 index[2 * AV_WIDTH * AV_HEIGHT];
    Palette palette;
    Uint8 *src, *dst;

    Uint8 *audiobuf;

    Pixmap *pixmap;
    Pixmap *av_frame, *av_frame_old, *av_frame_tmp;
    long avlen;
    int i, j;
    int x, y, fr, av_frame_num;
    int k;
    int pos;
    int frame_time_remainder = 0;
#ifdef AV_WINDOWS_AUDIO
    AVAudio device_audio = { 0 };
    int use_audio_clock;
#endif

    fullname = lotr_add_suffix(name, "av");
    FILE *file = lotr_open_movie(fullname);
    free(fullname);


    avlen = lotr_filelen(file);

    if (avlen < 0x300 + 2 * AV_WIDTH * AV_HEIGHT + AV_SAMPLE_LEN * 14) {
        fprintf(stderr, "lotr: corrupted av file %s.av len=%ld\n", name,
                avlen);
        exit(1);
    }

    /* Keep disc and filesystem seeks outside timed playback when possible. */
    if (avlen <= AV_CACHE_LIMIT) {
        movie_data = malloc(avlen);
        if (movie_data && fread(movie_data, avlen, 1, file) == 1)
            avfile = SDL_RWFromConstMem(movie_data, (int)avlen);
    }
    if (!avfile) {
        free(movie_data);
        movie_data = NULL;
        rewind(file);
        avfile = SDL_RWFromFP(file, SDL_FALSE);
        if (!avfile) {
            fprintf(stderr, "lotr: can't open AV stream %s.av\n", name);
            exit(1);
        }
    }

    graphics_set_background("vid");

    if (SDL_RWread(avfile, &palette, 0x300, 1) != 1) {
        fprintf(stderr, "lotr: corrupted av file %s.av: can't read palette\n",
                name);
        exit(1);
    }
    graphics_set_palette(&palette, 9, 0x100 - 9);


    pixmap = pixmap_new(8, 8);
    av_frame = pixmap_new(8 * AV_WIDTH, 8 * AV_HEIGHT);
    av_frame_old = pixmap_new(8 * AV_WIDTH, 8 * AV_HEIGHT);

    audiobuf = lotr_malloc(MAX_SAMPLE_GROWTH);
    int audio_size = load_av_audio(audiobuf, avfile);
    if (!audio_size) {
        fprintf(stderr, "lotr: corrupted av file %s.av: can't read audio data\n", name);
        exit(1);
    }

    if (SDL_RWread(avfile, index, sizeof(index), 1) != 1) {
        fprintf(stderr,
                "lotr: corrupted av file %s.av: can't read first frame\n",
                name);
        exit(1);
    }

    int music_paused = pause_music_for_movie();
    lotr_reset_keyboard();
#ifdef AV_WINDOWS_AUDIO
    use_audio_clock = av_audio_open(&device_audio, audiobuf, audio_size);
    if (!use_audio_clock)
#endif
        play_sample(audiobuf, audio_size, LOTR_PCM_S8);
#ifdef _WIN32
    /* Give buffered CD audio one video interval to reach the output. */
#ifdef AV_WINDOWS_AUDIO
    if (!use_audio_clock && playing_sample())
#else
    if (playing_sample())
#endif
        lotr_sleep(AV_SAMPLE_LEN * 1000 / AV_SAMPLE_RATE);
#endif
    lotr_reset_timer();

    av_frame_num = 0;
    while (!lotr_key_esc()) {

        /* draw new 8x8 squares */
        for (j = 0; j < AV_HEIGHT; ++j)
            for (i = 0; i < AV_WIDTH; ++i) {
                x = index[(j * AV_WIDTH + i) * 2];
                y = index[(j * AV_WIDTH + i) * 2 + 1];

                if (x == 0xff && y == 0xff) {
                    if (SDL_RWread(avfile, pixmap->data, 8 * 8, 1) != 1) {
                        fprintf(stderr,
                                "lotr: corrupted av file %s.av: can't read block data\n",
                                name);
                        exit(1);
                    }

                    src = pixmap->data;
                    dst = av_frame->data + j * AV_WIDTH * 8 * 8 + i * 8;

                    for (k = 0; k < 8; ++k) {
                        memcpy(dst, src, 8);
                        dst += AV_WIDTH * 8;
                        src += 8;
                    }
                }
            }


        /* recycle old 8x8 squares */
        for (j = 0; j < AV_HEIGHT; ++j)
            for (i = 0; i < AV_WIDTH; ++i) {
                x = index[(j * AV_WIDTH + i) * 2];
                y = index[(j * AV_WIDTH + i) * 2 + 1];
                if (x != 0xff || y != 0xff) {
                    pos = 32 * 8 * y + x;
                    x = pos % (AV_WIDTH * 8);
                    y = pos / (AV_WIDTH * 8) / 2;

                    fr = (pos / (AV_WIDTH * 8) + av_frame_num) % 2;

                    if ((y + 7) * AV_WIDTH * 8 + x + 7 <
                        AV_HEIGHT * AV_WIDTH * 8 * 8) {

                        if (fr == 0)
                            src = av_frame->data + y * AV_WIDTH * 8 + x;
                        else
                            src = av_frame_old->data + y * AV_WIDTH * 8 + x;
                        dst = av_frame->data + j * AV_WIDTH * 8 * 8 + i * 8;

                        for (k = 0; k < 8; ++k) {
                            memmove(dst, src, 8);
                            dst += AV_WIDTH * 8;
                            src += AV_WIDTH * 8;
                        }
                    } else {
                        fprintf(stderr,
                                "lotr: corrupted av file (x=%d, y=%d)\n", x,
                                y);
                        exit(1);
                    }
                }
            }


        /* Decode every delta frame, even when presentation falls behind. */
        int present;
#ifdef AV_WINDOWS_AUDIO
        if (use_audio_clock) {
            /* A zero position can also mean the device has not started. */
            Uint32 target = av_frame_num ? (Uint32)av_frame_num * AV_SAMPLE_LEN * 4 : 1;
            present = av_wait_for_audio(&device_audio, target);
            if (present < 0) {
                use_audio_clock = 0;
                lotr_reset_timer();
            }
        }
        if (!use_audio_clock)
#endif
            present = !av_frame_num || !lotr_timer_late(AV_SAMPLE_LEN * 1000 / AV_SAMPLE_RATE);
        if (present) {
            pixmap_draw(av_frame, 53, 24);
            graphics_update_screen();
        }
        /* Each frame carries one 11,025 Hz audio block. Keep fractional ms. */
        frame_time_remainder += AV_SAMPLE_LEN * 1000;
#ifdef AV_WINDOWS_AUDIO
        lotr_timer_steady(use_audio_clock ? 0 : frame_time_remainder / AV_SAMPLE_RATE);
#else
        lotr_timer_steady(frame_time_remainder / AV_SAMPLE_RATE);
#endif
        frame_time_remainder %= AV_SAMPLE_RATE;
        lotr_poll_events();

        if (SDL_RWseek(avfile, AV_SAMPLE_LEN, RW_SEEK_CUR) < 0)
            break;

        if (SDL_RWread(avfile, index, sizeof(index), 1) != 1)
            break;

        av_frame_tmp = av_frame_old;
        av_frame_old = av_frame;
        av_frame = av_frame_tmp;

        av_frame_num++;

    }

    SDL_RWclose(avfile);
    fclose(file);
    free(movie_data);
    while (playing_sample() && !lotr_key_esc()) {
        lotr_timer(10);
        lotr_poll_events();
    }
#ifdef AV_WINDOWS_AUDIO
    if (device_audio.device) {
        if (!lotr_key_esc() && use_audio_clock)
            av_wait_for_audio(&device_audio, (Uint32)audio_size * 2 - 1);
        av_audio_close(&device_audio);
    }
#endif

    stop_sample(audiobuf);
    resume_music_after_movie(music_paused);

    free(audiobuf);

    pixmap_free(pixmap);
    pixmap_free(av_frame);
    pixmap_free(av_frame_old);

    lotr_reset_keyboard();

}




/*
  play av by number
*/

void
av_playnum(int num)
{
    const char *name = av_name(num);
    if (!name) {
        fprintf(stderr, "lotr: non-existent av file number=%d\n", num);
        exit(1);
    }

    /* The DOS script movie handler stops CD playback before the AV. */
    stop_midi();
    av_play(name);

}
