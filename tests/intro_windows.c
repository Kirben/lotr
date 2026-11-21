#include "lotr.h"
#include "av.h"
#include "cartoon.h"
#include "game.h"
#include "gui.h"
#include "init.h"
#include "sound.h"
#include "timing.h"
#include <SDL.h>
#ifdef HAVE_SDL_MIXER
#include <SDL_mixer.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_SDL_MIXER
#include <windows.h>
#include <mmsystem.h>
#endif

static const char *clip;
static int frames, skipped, visual_change, active_av, completed, menu_step, menu_active;
static unsigned polls;
static Uint32 previous_hash;
static int realtime, capture, timing_only;
static int loopback, stall_check, demo_walkthrough;
static SDL_Keycode demo_key;
static Uint32 first_present, last_present;
static int av_source_frame, presentation_count;
static Uint64 longest_sleep_overshoot, longest_present;
static unsigned delayed_sleeps, delayed_presents;
static struct {
    int frame, mixed_bytes;
    Uint64 tick;
} presentations[8192];

void media_timer_steady(int delay)
{
    lotr_timer_steady(delay);
    ++av_source_frame;
    if (stall_check && av_source_frame == 30 && !strcmp(clip, "sacf"))
        SDL_Delay(380);
}

#ifdef HAVE_SDL_MIXER
static SDL_atomic_t audio_active, audio_bytes, audio_signal, audio_first, audio_last;
static SDL_atomic_t audio_channel, audio_finished;
static void (SDLCALL *original_finished)(int);
static FILE *music_pcm;
static int bytes_per_source_byte = 1;

static void record_music(void *unused, Uint8 *stream, int length)
{
    (void)unused;
    fwrite(stream, 1, length, music_pcm);
}

static void SDLCALL record_channel_finish(int channel)
{
    if (SDL_AtomicGet(&audio_active) && channel == SDL_AtomicGet(&audio_channel))
        SDL_AtomicSet(&audio_finished, SDL_GetTicks());
    original_finished(channel);
}

void media_channel_finished(void (SDLCALL *callback)(int))
{
    original_finished = callback;
    Mix_ChannelFinished(callback ? record_channel_finish : NULL);
}

static void record_audio(void *unused, Uint8 *stream, int length)
{
    (void)unused;
    if (!SDL_AtomicGet(&audio_active))
        return;
    Uint32 now = SDL_GetTicks();
    if (SDL_AtomicAdd(&audio_bytes, length / bytes_per_source_byte) == 0)
        SDL_AtomicSet(&audio_first, now);
    SDL_AtomicSet(&audio_last, now);
    for (int i = 0; i < length; ++i)
        if (stream[i]) {
            SDL_AtomicAdd(&audio_signal, 1);
            break;
        }
}
#endif

void media_init(void)
{
    realtime = getenv("LOTR_NATIVE_REALTIME") != NULL;
    capture = getenv("LOTR_NATIVE_CAPTURE") != NULL;
    timing_only = getenv("LOTR_NATIVE_TIMING_ONLY") != NULL;
    loopback = getenv("LOTR_NATIVE_LOOPBACK") != NULL;
    stall_check = getenv("LOTR_NATIVE_STALL_CHECK") != NULL;
    demo_walkthrough = getenv("LOTR_NATIVE_DEMO_WALKTHROUGH") != NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("native initializing engine");
    lotr_init();
    puts("native engine initialized");
#ifdef HAVE_SDL_MIXER
    {
        int rate, channels;
        Uint16 format;
        if (Mix_QuerySpec(&rate, &format, &channels)) {
            bytes_per_source_byte = (rate / 22050) * (SDL_AUDIO_BITSIZE(format) / 8) * channels;
            if (realtime) {
                printf("audio device driver=%s rate=%d format=%04x channels=%d\n",
                       SDL_GetCurrentAudioDriver(), rate, format, channels);
                Mix_SetPostMix(record_audio, NULL);
            }
        }
    }
#endif
    if (loopback) {
        printf("performance frequency=%llu\n", (unsigned long long)SDL_GetPerformanceFrequency());
        for (int tries = 0; tries < 300; ++tries) {
            FILE *ready = fopen("loopback.ready", "rb");
            if (ready) {
                fclose(ready);
                return;
            }
            SDL_Delay(100);
        }
        fprintf(stderr, "Process loopback capture did not start\n");
        exit(1);
    }
}

SDL_Window *media_create_window(const char *title, int x, int y, int width, int height, Uint32 flags)
{
    if (capture)
        flags = (flags & ~SDL_WINDOW_SHOWN) | SDL_WINDOW_HIDDEN;
    return SDL_CreateWindow(title, x, y, width, height, flags);
}

static void begin_clip(const char *name, int av)
{
#ifdef HAVE_SDL_MIXER
    if (av && getenv("LOTR_NATIVE_CD_MUSIC_CHECK") && Mix_PlayingMusic())
        exit(1);
#endif
    clip = name;
    active_av = av;
    frames = skipped = visual_change = polls = 0;
    av_source_frame = 0;
    presentation_count = 0;
    longest_sleep_overshoot = longest_present = 0;
    delayed_sleeps = delayed_presents = 0;
    first_present = last_present = 0;
    printf("native starting intro %s\n", clip);
}

static void end_clip(void)
{
    if (loopback && active_av) {
        Uint64 clock = SDL_GetPerformanceFrequency();
        printf("timing %s max_sleep_overshoot_ms=%.1f sleeps_over_66ms=%u max_present_ms=%.1f presents_over_66ms=%u\n",
               clip, 1000.0 * longest_sleep_overshoot / clock, delayed_sleeps,
               1000.0 * longest_present / clock, delayed_presents);
        size_t capacity = (size_t)presentation_count * 96 + 1;
        char *trace = malloc(capacity);
        if (!trace)
            exit(1);
        size_t written = 0;
        for (int i = 0; i < presentation_count; ++i)
            written += snprintf(trace + written, capacity - written,
                                "present %s frame=%d qpc=%llu mixed_bytes=%d\n", clip,
                                presentations[i].frame, (unsigned long long)presentations[i].tick,
                                presentations[i].mixed_bytes);
        if (fwrite(trace, 1, written, stdout) != written)
            exit(1);
        free(trace);
        printf("video decoded %s frames=%d dropped=%d\n", clip, av_source_frame,
               av_source_frame - (frames - 1));
    }
#ifdef HAVE_SDL_MIXER
    if (realtime && active_av) {
        SDL_AtomicSet(&audio_active, 0);
        printf("audio trace %s first=%d last=%d finished=%d bytes=%d signal_buffers=%d\n",
               clip, SDL_AtomicGet(&audio_first), SDL_AtomicGet(&audio_last),
               SDL_AtomicGet(&audio_finished), SDL_AtomicGet(&audio_bytes),
               SDL_AtomicGet(&audio_signal));
    }
#endif
    if (capture)
        printf("video trace %s first=%u last=%u done=%u\n",
               clip, first_present, last_present, SDL_GetTicks());
    printf("native intro %s frames=%d skipped=%d changed=%d\n",
           clip, frames, skipped, visual_change);
    clip = NULL;
    completed++;
}

void media_play_av(const char *name)
{
    begin_clip(name, 1);
    av_play(name);
    end_clip();
}

void media_play_cartoon(char *name)
{
    begin_clip(name, 0);
    cartoon_play(name);
    end_clip();
}

int media_towers_menu(void)
{
    menu_active = 1;
    int result = gui_ttt_start_dialog();
    menu_active = 0;
    return result;
}

int media_poll_event(SDL_Event *event)
{
    if (!event)
        return SDL_PollEvent(event);
    SDL_Keycode key = SDLK_UNKNOWN;
    if (menu_active && menu_step < 2)
        key = menu_step++ == 0 ? SDLK_n : SDLK_x;
    else if (clip) {
        const char *mode = getenv("LOTR_NATIVE_INTRO_MODE");
        ++polls;
        int escape = mode && !strcmp(mode, "skip") &&
            (frames >= (active_av ? 2 : 5) || polls == 512);
        if (!skipped && (escape || (stall_check && active_av && av_source_frame >= 60))) {
            skipped = 1;
            key = SDLK_ESCAPE;
        } else if (!active_av && polls % 512 == 0) {
            key = (polls / 512) % 2 ? SDLK_RETURN : SDLK_SPACE;
        }
    } else if (demo_key) {
        key = demo_key;
        demo_key = SDLK_UNKNOWN;
    }
    if (key == SDLK_UNKNOWN)
        return SDL_PollEvent(event);
    memset(event, 0, sizeof(*event));
    event->type = SDL_KEYDOWN;
    event->key.keysym.sym = key;
    return 1;
}

static void capture_frame(SDL_Renderer *renderer, int frame)
{
    int width, height;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) < 0)
        return;
    Uint32 *pixels = malloc((size_t)width * height * sizeof(*pixels));
    if (!pixels)
        exit(1);
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                             pixels, width * sizeof(*pixels)) == 0) {
        Uint32 hash = 2166136261u;
        for (int i = 0; i < width * height; ++i)
            hash = (hash ^ (pixels[i] & 0xffffff)) * 16777619u;
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(renderer, &info) == 0)
            printf("frame %s %d renderer=%s size=%dx%d rgb=%08x\n",
                   clip, frame, info.name, width, height, (unsigned)hash);
    }
    free(pixels);
}

void media_present(SDL_Renderer *renderer)
{
    if (clip) {
        Uint32 pixels[160];
        SDL_Rect row = {80, 120, SDL_arraysize(pixels), 1};
        if (!timing_only && !visual_change &&
            SDL_RenderReadPixels(renderer, &row, SDL_PIXELFORMAT_ARGB8888,
                                 pixels, sizeof(pixels)) == 0) {
            Uint32 hash = 0;
            for (size_t i = 0; i < SDL_arraysize(pixels); ++i)
                hash = hash * 33 + pixels[i];
            if (frames && previous_hash != hash)
                visual_change = 1;
            previous_hash = hash;
        }
        int frame = active_av && frames ? av_source_frame + 2 : frames + 1;
        if (capture && !timing_only && (frame == 1 || frame % 300 == 0))
            capture_frame(renderer, frame);
        ++frames;
        polls = 0;
    }
    Uint64 before_present = loopback && active_av ? SDL_GetPerformanceCounter() : 0;
    SDL_RenderPresent(renderer);
    if (before_present) {
        Uint64 elapsed = SDL_GetPerformanceCounter() - before_present;
        if (elapsed > longest_present)
            longest_present = elapsed;
        if (elapsed * 1000 >= SDL_GetPerformanceFrequency() * 66)
            ++delayed_presents;
    }
    if (clip && capture) {
        if (loopback && active_av && frames > 1 && presentation_count < SDL_arraysize(presentations)) {
            presentations[presentation_count].frame = av_source_frame + 2;
            presentations[presentation_count].tick = SDL_GetPerformanceCounter();
#ifdef HAVE_SDL_MIXER
            presentations[presentation_count].mixed_bytes = SDL_AtomicGet(&audio_bytes);
#endif
            ++presentation_count;
        }
        last_present = SDL_GetTicks();
        if (frames == (active_av ? 2 : 1))
            first_present = last_present;
#ifdef HAVE_SDL_MIXER
        if (realtime && active_av && (frames == 2 || frames % 300 == 0))
            printf("cadence %s frame=%d tick=%u mixed_bytes=%d\n",
                   clip, frames, last_present, SDL_AtomicGet(&audio_bytes));
#endif
    }
}

void media_delay(Uint32 duration)
{
    /* Keep the video clock fast while still yielding to the mixer. */
    if (realtime) {
        Uint64 before = loopback && active_av ? SDL_GetPerformanceCounter() : 0;
        SDL_Delay(duration);
        if (before) {
            Uint64 clock = SDL_GetPerformanceFrequency();
            Uint64 requested = clock * duration / 1000;
            Uint64 elapsed = SDL_GetPerformanceCounter() - before;
            if (elapsed > requested) {
                Uint64 excess = elapsed - requested;
                if (excess > longest_sleep_overshoot)
                    longest_sleep_overshoot = excess;
                if (excess * 1000 >= clock * 66)
                    ++delayed_sleeps;
            }
        }
    } else if (!clip)
        SDL_Delay(duration > 1 ? 1 : duration);
}

void media_game_frame(void)
{
    game_next_frame();
    if (demo_walkthrough) {
        static int walking, ending_message, demo_frames;
        SDL_Event event = {0};
        Character *leader = game_get_leader();
        if (++demo_frames % 1000 == 0)
            printf("Fellowship demo frame=%d position=%d,%d action=%d dialog=%d cartoons=%d\n",
                   demo_frames, leader->x, leader->y, leader->action, gui_mode(), completed);
        if (demo_frames > 12000)
            exit(1);
        if (gui_mode()) {
            if (completed == 3 && leader->x == 0x21c && leader->y == 0xb0)
                ending_message = 1;
            demo_key = SDLK_RETURN;
        } else if (ending_message && !walking) {
            walking = 1;
            demo_key = SDLK_DOWN;
        } else if (walking && leader->y > 0xb0) {
            printf("Fellowship demo completed all three cartoons and reached free movement\n");
            event.type = SDL_QUIT;
        }
        if (event.type && SDL_PushEvent(&event) != 1)
            exit(1);
        return;
    }
#ifdef HAVE_SDL_MIXER
    if (getenv("LOTR_NATIVE_CD_MUSIC_LOOPBACK")) {
        if (!Mix_PlayingMusic() || Mix_GetMusicType(NULL) != MUS_WAV)
            exit(1);
        music_pcm = fopen("music-postmix.pcm", "wb");
        if (!music_pcm)
            exit(1);
        Mix_SetPostMix(record_music, NULL);
        puts("Recording gameplay music for 25 seconds");
        for (Uint32 start = SDL_GetTicks(); SDL_GetTicks() - start < 25000; ) {
            SDL_Delay(20);
            game_next_frame();
        }
        Mix_SetPostMix(NULL, NULL);
        if (fclose(music_pcm) || !Mix_PlayingMusic())
            exit(1);
    }
    if (getenv("LOTR_NATIVE_CD_MUSIC_CHECK")) {
        if (!Mix_PlayingMusic() || Mix_GetMusicType(NULL) != MUS_WAV) {
            fprintf(stderr, "Enhanced CD background WAV is not playing\n");
            exit(1);
        }
        SDL_AtomicSet(&audio_signal, 0);
        SDL_AtomicSet(&audio_active, 1);
        Mix_SetPostMix(record_audio, NULL);
        for (int i = 0; i < 100 && !SDL_AtomicGet(&audio_signal); ++i)
            SDL_Delay(50);
        Mix_SetPostMix(NULL, NULL);
        SDL_AtomicSet(&audio_active, 0);
        if (!SDL_AtomicGet(&audio_signal)) {
            fprintf(stderr, "Enhanced CD music produced no nonzero PCM\n");
            exit(1);
        }
        toggle_music();
        if (Mix_PlayingMusic())
            exit(1);
        toggle_music();
        if (!Mix_PlayingMusic())
            exit(1);
        puts("Enhanced CD gameplay music produced PCM and toggled off and on");
    }
#endif
    printf("native first gameplay frame after %d intro clips\n", completed);
    SDL_Event event = {0};
    event.type = SDL_QUIT;
    if (SDL_PushEvent(&event) != 1)
        exit(1);
}

#ifdef HAVE_SDL_MIXER
static void record_intro_sample(const Uint8 *data, Uint32 size)
{
    if (clip && active_av) {
        printf("native intro audio %s bytes=%lu\n", clip,
               (unsigned long)(size / bytes_per_source_byte));
        if (loopback) {
            char filename[40];
            snprintf(filename, sizeof(filename), "%s-source.pcm", clip);
            FILE *file = fopen(filename, "wb");
            if (!file)
                exit(1);
            for (Uint32 i = 0; i < size; i += bytes_per_source_byte) {
                Uint8 value = data[i];
                if (bytes_per_source_byte == 8)
                    value = (Uint8)(((const Sint16 *)(data + i))[0] / 256);
                if (fputc(value, file) == EOF)
                    exit(1);
            }
            if (fclose(file) != 0)
                exit(1);
        }
    }
}

MMRESULT media_waveout_write(HWAVEOUT device, WAVEHDR *header, UINT size)
{
    record_intro_sample((const Uint8 *)header->lpData, header->dwBufferLength);
    SDL_AtomicSet(&audio_bytes, 0);
    printf("audio queued %s tick=%u backend=waveout\n", clip, SDL_GetTicks());
    return waveOutWrite(device, header, size);
}

MMRESULT media_waveout_position(HWAVEOUT device, MMTIME *position, UINT size)
{
    MMRESULT result = waveOutGetPosition(device, position, size);
    if (result == MMSYSERR_NOERROR && position->wType == TIME_SAMPLES)
        SDL_AtomicSet(&audio_bytes, position->u.sample / 2);
    return result;
}

Mix_Chunk *media_load_sample(Uint8 *data, Uint32 size)
{
    record_intro_sample(data, size);
    /* Play a short excerpt at normal audio speed in the accelerated test. */
    if (clip && active_av && !realtime && size > 8192 * (Uint32)bytes_per_source_byte)
        size = 8192 * (Uint32)bytes_per_source_byte;
    return Mix_QuickLoad_RAW(data, size);
}

int media_play_channel(int channel, Mix_Chunk *chunk, int loops)
{
    if (realtime && clip && active_av) {
        SDL_AtomicSet(&audio_bytes, 0);
        SDL_AtomicSet(&audio_signal, 0);
        SDL_AtomicSet(&audio_first, 0);
        SDL_AtomicSet(&audio_last, 0);
        SDL_AtomicSet(&audio_channel, -1);
        SDL_AtomicSet(&audio_finished, 0);
        SDL_AtomicSet(&audio_active, 1);
        printf("audio queued %s tick=%u\n", clip, SDL_GetTicks());
    }
    int result = Mix_PlayChannel(channel, chunk, loops);
    if (realtime && clip && active_av)
        SDL_AtomicSet(&audio_channel, result);
    if (clip && active_av)
        printf("native intro mixer %s channel=%d\n", clip, result);
    return result;
}
#endif
