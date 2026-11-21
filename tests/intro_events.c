/* Headless intro driver for the Linux engine and cartoon fixture. */
#define _GNU_SOURCE
#include <SDL.h>
#ifdef LOTR_TEST_MIXER
#include <SDL_mixer.h>
#endif
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *intro;
static const char *clip;
static int frames, finished, outside_frames, menu_seen, menu_key, sent_skip, sent_exit;
static int rendered_change;
static unsigned polls_without_render;
static Uint32 frame_hash;
static const char *test_mode;
static int game_window;

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
    SDL_Window *(*create_window)(const char *, int, int, int, int, Uint32) =
        dlsym(RTLD_NEXT, "SDL_CreateWindow");
    SDL_Window *window = create_window(title, x, y, w, h, flags);
    if (window && !strcmp(title, "Lord of the Rings"))
        game_window = 1;
    return window;
}

int SDL_WaitEvent(SDL_Event *event)
{
    int (*wait_event)(SDL_Event *) = dlsym(RTLD_NEXT, "SDL_WaitEvent");
    static int selected;
    if (!event || !getenv("LOTR_INTRO_SELECTOR"))
        return wait_event(event);
    memset(event, 0, sizeof(*event));
    event->type = SDL_KEYDOWN;
    event->key.keysym.sym = selected ? SDLK_ESCAPE : SDLK_RETURN;
    if (!selected++)
        fprintf(stderr, "intro selector pressed Play\n");
    return 1;
}

FILE *fopen(const char *path, const char *mode)
{
    FILE *(*open_file)(const char *, const char *) = dlsym(RTLD_NEXT, "fopen");
    FILE *file = open_file(path, mode);
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (file && game_window && !strcasecmp(name, "ongame.dat"))
        menu_seen = 1;
    if (file && game_window && (!strcasecmp(name, "opening.dat") || !strcasecmp(name, "democrt0.dat") ||
                 !strcasecmp(name, "democrt1.dat") || !strcasecmp(name, "democrt2.dat") ||
                 !strcasecmp(name, "sacf.av") ||
                 !strcasecmp(name, "saqf.av"))) {
        clip = !strcasecmp(name, "sacf.av") ? "sacf" :
            !strcasecmp(name, "saqf.av") ? "saqf" :
            !strcasecmp(name, "democrt0.dat") ? "democrt0" :
            !strcasecmp(name, "democrt1.dat") ? "democrt1" :
            !strcasecmp(name, "democrt2.dat") ? "democrt2" : "opening";
        test_mode = getenv("LOTR_INTRO_MODE");
        intro = file;
        frames = sent_skip = outside_frames = rendered_change = 0;
        polls_without_render = 0;
        fprintf(stderr, "intro opened %s\n", clip);
    }
    return file;
}

int fclose(FILE *file)
{
    int (*close_file)(FILE *) = dlsym(RTLD_NEXT, "fclose");
    if (file == intro) {
        fprintf(stderr, "intro clip=%s frames=%d skip=%d changed=%d\n",
                clip, frames, sent_skip, rendered_change);
        intro = NULL;
        finished++;
    }
    return close_file(file);
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    void (*present)(SDL_Renderer *) = dlsym(RTLD_NEXT, "SDL_RenderPresent");
    if (intro) {
        Uint32 pixels[160];
        SDL_Rect row = {80, 120, SDL_arraysize(pixels), 1};
        if (!rendered_change &&
            SDL_RenderReadPixels(renderer, &row, SDL_PIXELFORMAT_ARGB8888,
                                 pixels, sizeof(pixels)) == 0) {
            Uint32 hash = 0;
            for (size_t i = 0; i < SDL_arraysize(pixels); ++i)
                hash = hash * 33 + pixels[i];
            if (frames > 0 && frame_hash != hash)
                rendered_change = 1;
            frame_hash = hash;
        }
        frames++;
        polls_without_render = 0;
    } else if (finished) {
        outside_frames++;
    }
    present(renderer);
}

int SDL_PollEvent(SDL_Event *event)
{
    int (*poll_event)(SDL_Event *) = dlsym(RTLD_NEXT, "SDL_PollEvent");
    SDL_Keycode key = SDLK_UNKNOWN;
    if (event && menu_seen && !intro && !finished && menu_key < 2) {
        if (menu_key == 1 && outside_frames == 0) {
            outside_frames = 1;
            return 0;
        }
        key = menu_key++ == 0 ? SDLK_n : SDLK_x;
    } else if (intro) {
        int cartoon = !strcmp(clip, "opening") || !strncmp(clip, "democrt", 7);
        ++polls_without_render;
        if (!sent_skip && !strcmp(test_mode, "skip") &&
            (frames >= (cartoon ? 5 : 2) ||
             polls_without_render == 512)) {
            sent_skip = 1;
            key = SDLK_ESCAPE;
        } else if (cartoon && polls_without_render % 512 == 0) {
            key = ((polls_without_render / 512) % 2) ? SDLK_RETURN : SDLK_SPACE;
        }
    } else if (getenv("LOTR_DEMO_TRACE_WALKTHROUGH") && finished > 0 && finished < 3) {
        static int poll;
        if (++poll % 2)
            key = SDLK_RETURN;
    } else if (!intro && finished == atoi(getenv("LOTR_INTRO_CLIPS")) && outside_frames &&
               !sent_exit && !getenv("LOTR_CARTOON_FIXTURE")) {
        sent_exit = 1;
        fprintf(stderr, "intro returned to gameplay\n");
        memset(event, 0, sizeof(*event));
        event->type = SDL_QUIT;
        return 1;
    }
    if (key == SDLK_UNKNOWN)
        return poll_event(event);
    memset(event, 0, sizeof(*event));
    event->type = SDL_KEYDOWN;
    event->key.keysym.sym = key;
    return 1;
}

void SDL_Delay(Uint32 duration)
{
    void (*delay)(Uint32) = dlsym(RTLD_NEXT, "SDL_Delay");
    /* Accelerate long visual waits and give the dummy mixer CPU time. */
    if (!intro)
        delay(duration < 2 ? duration : 2);
}

#ifdef LOTR_TEST_MIXER
static SDL_atomic_t midi_samples;

static void SDLCALL count_midi_samples(void *unused, Uint8 *stream, int length)
{
    (void)unused;
    int audible = 0;
    for (int i = 0; i < length; ++i)
        if (stream[i] < 126 || stream[i] > 130)
            ++audible;
    SDL_AtomicAdd(&midi_samples, audible);
}

int Mix_PlayMusic(Mix_Music *music, int loops)
{
    int (*play)(Mix_Music *, int) = dlsym(RTLD_NEXT, "Mix_PlayMusic");
    static int verified;
    const char *verify = getenv("LOTR_VERIFY_DEMO_MIDI");
    if (verified || !verify || strcmp(verify, "1"))
        return play(music, loops);

    int rate, channels;
    Uint16 format;
    if (Mix_GetMusicType(music) != MUS_MID ||
        !Mix_QuerySpec(&rate, &format, &channels) || format != AUDIO_U8 || channels != 1) {
        fprintf(stderr, "Expected the Fellowship MIDI track and unsigned mono mixer\n");
        exit(1);
    }
    SDL_AtomicSet(&midi_samples, 0);
    Mix_SetPostMix(count_midi_samples, NULL);
    int result = play(music, loops);
    void (*delay)(Uint32) = dlsym(RTLD_NEXT, "SDL_Delay");
    for (int i = 0; result == 0 && i < 100 && SDL_AtomicGet(&midi_samples) < 128; ++i)
        delay(50);
    Mix_SetPostMix(NULL, NULL);
    int samples = SDL_AtomicGet(&midi_samples);
    if (result != 0 || samples < 128 || !Mix_PlayingMusic()) {
        fprintf(stderr, "Fellowship MIDI failed to produce audio: %s\n", Mix_GetError());
        exit(1);
    }
    fprintf(stderr, "Fellowship MIDI produced %d signal samples at %d Hz\n", samples, rate);
    verified = 1;
    return result;
}

Mix_Chunk *Mix_QuickLoad_RAW(Uint8 *data, Uint32 size)
{
    Mix_Chunk *(*load_raw)(Uint8 *, Uint32) = dlsym(RTLD_NEXT, "Mix_QuickLoad_RAW");
    if (intro && strcmp(clip, "opening")) {
        int rate, channels;
        Uint16 format;
        Uint32 bytes_per_source_byte = 1;
        if (Mix_QuerySpec(&rate, &format, &channels))
            bytes_per_source_byte = (rate / 22050) * (SDL_AUDIO_BITSIZE(format) / 8) * channels;
        fprintf(stderr, "intro audio clip=%s bytes=%u\n", clip, size / bytes_per_source_byte);
        /* Use an excerpt because the video clock is accelerated. */
        if (size > 8192 * bytes_per_source_byte)
            size = 8192 * bytes_per_source_byte;
    }
    return load_raw(data, size);
}

int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks)
{
    int (*play)(int, Mix_Chunk *, int, int) = dlsym(RTLD_NEXT, "Mix_PlayChannelTimed");
    int result = play(channel, chunk, loops, ticks);
    if (intro && strcmp(clip, "opening"))
        fprintf(stderr, "intro mixer clip=%s channel=%d\n", clip, result);
    return result;
}

#ifndef Mix_PlayChannel
int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops)
{
    int (*play)(int, Mix_Chunk *, int) = dlsym(RTLD_NEXT, "Mix_PlayChannel");
    int result = play(channel, chunk, loops);
    if (intro && strcmp(clip, "opening"))
        fprintf(stderr, "intro mixer clip=%s channel=%d\n", clip, result);
    return result;
}
#endif
#endif
