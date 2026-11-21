/* Headless Linux integration driver. The game executable stays unmodified. */
#define _GNU_SOURCE
#include <SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int selector_window;
static int game_window;
static int step;
static int clipboard_slot;

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags)
{
    SDL_Window *(*create_window)(const char *, int, int, int, int, Uint32) =
        dlsym(RTLD_NEXT, "SDL_CreateWindow");
    selector_window = strstr(title, "Choose an edition") != NULL;
    game_window = !selector_window;
    fprintf(stderr, "selector-test window: %s\n", selector_window ? "selector" : "game");
    return create_window(title, x, y, w, h, flags);
}

FILE *fopen(const char *path, const char *mode)
{
    FILE *(*open_file)(const char *, const char *) = dlsym(RTLD_NEXT, "fopen");
    FILE *file = open_file(path, mode);
    const char *scenario = getenv("LOTR_SELECTOR_TEST");
    const char *name = strrchr(path, '/');
    if (game_window && name && scenario) {
        if (strcasecmp(name, "/arts.dat") == 0)
            fprintf(stderr, "selector-test game data %s\n", path);
        if (file && (strcasecmp(name, "/sacf.av") == 0 ||
                     ((strcmp(scenario, "towers-cli") == 0 || strcmp(scenario, "towers-selector") == 0) &&
                      strcasecmp(name, "/ongame.dat") == 0) ||
                     (strcmp(scenario, "demo-selector") == 0 && strcasecmp(name, "/democrt0.dat") == 0))) {
            fprintf(stderr, "selector-test startup asset %s\n", path);
            _Exit(0);
        }
    }
    return file;
}

char *SDL_GetClipboardText(void)
{
    static const char *const keys[] = {
        "LOTR_TEST_STANDARD", "LOTR_TEST_CD", "LOTR_TEST_MOVIES", "LOTR_TEST_TOWERS",
        "LOTR_TEST_MOVED_PARENT", "LOTR_TEST_MOVED_MOVIES"
    };
    const char *value = getenv(keys[clipboard_slot]);
    return SDL_strdup(value ? value : "");
}

static void key(SDL_Event *event, SDL_Keycode sym, Uint16 modifiers)
{
    event->type = SDL_KEYDOWN;
    event->key.keysym.sym = sym;
    event->key.keysym.mod = modifiers;
}

static void click(SDL_Event *event, int x, int y)
{
    event->type = SDL_MOUSEBUTTONDOWN;
    event->button.button = SDL_BUTTON_LEFT;
    event->button.x = x;
    event->button.y = y;
}

int SDL_WaitEvent(SDL_Event *event)
{
    int (*wait_event)(SDL_Event *) = dlsym(RTLD_NEXT, "SDL_WaitEvent");
    const char *scenario = getenv("LOTR_SELECTOR_TEST");
    if (!selector_window || !scenario || !event)
        return wait_event(event);
    memset(event, 0, sizeof(*event));
    if (strcmp(scenario, "demo-selector") == 0) {
        switch (step++) {
            case 0: click(event, 810, 268); break;
            case 1: click(event, 800, 585); break;
            default: key(event, SDLK_ESCAPE, 0); break;
        }
        return 1;
    }
    if (strcmp(scenario, "invalid") == 0 || strcmp(scenario, "resume") == 0 ||
        strcmp(scenario, "towers-selector") == 0) {
        if (step++ == 0)
            click(event, 800, 585);
        else
            key(event, SDLK_ESCAPE, 0);
        return 1;
    }
    if (strcmp(scenario, "moved-repair") == 0) {
        switch (step++) {
            case 0: click(event, 60, 402); break;
            case 1: key(event, SDLK_a, KMOD_CTRL); break;
            case 2: clipboard_slot = 4; key(event, SDLK_v, KMOD_CTRL); break;
            case 3: click(event, 820, 402); break;
            case 4: key(event, SDLK_RETURN, 0); break;
            case 5: key(event, SDLK_u, 0); break;
            case 6: click(event, 60, 490); break;
            case 7: key(event, SDLK_a, KMOD_CTRL); break;
            case 8: clipboard_slot = 5; key(event, SDLK_v, KMOD_CTRL); break;
            case 9: click(event, 800, 585); break;
            default: key(event, SDLK_ESCAPE, 0); break;
        }
        return 1;
    }
    switch (step++) {
        case 0: key(event, SDLK_TAB, 0); break;
        case 1: key(event, SDLK_a, KMOD_CTRL); break;
        case 2: clipboard_slot = 0; key(event, SDLK_v, KMOD_CTRL); break;
        case 3: click(event, 400, 268); break;
        case 4: click(event, 60, 402); break;
        case 5: key(event, SDLK_a, KMOD_CTRL); break;
        case 6: clipboard_slot = 1; key(event, SDLK_v, KMOD_CTRL); break;
        case 7: click(event, 60, 490); break;
        case 8: key(event, SDLK_a, KMOD_CTRL); break;
        case 9: clipboard_slot = 2; key(event, SDLK_v, KMOD_CTRL); break;
        case 10: click(event, 820, 494); break;
        case 11: key(event, SDLK_ESCAPE, 0); break;
        case 12: click(event, 670, 268); break;
        case 13: click(event, 60, 402); break;
        case 14: key(event, SDLK_a, KMOD_CTRL); break;
        case 15: clipboard_slot = 3; key(event, SDLK_v, KMOD_CTRL); break;
        case 16: click(event, 820, 402); break;
        case 17: click(event, 800, 571); break;
        case 18: click(event, 400, 268); break;
        case 19: click(event, 800, 585); break;
        default: key(event, SDLK_ESCAPE, 0); break;
    }
    return 1;
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    void (*present)(SDL_Renderer *) = dlsym(RTLD_NEXT, "SDL_RenderPresent");
    const char *capture = getenv("LOTR_SELECTOR_CAPTURE");
    const char *scenario = getenv("LOTR_SELECTOR_TEST");
    if (selector_window && capture && scenario && strcmp(scenario, "configure") == 0 && step == 19) {
        int width, height;
        if (SDL_GetRendererOutputSize(renderer, &width, &height) == 0) {
            SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
            if (image) {
                if (SDL_RenderReadPixels(renderer, NULL, image->format->format, image->pixels, image->pitch) == 0)
                    SDL_SaveBMP(image, capture);
                SDL_FreeSurface(image);
            }
        }
    }
    present(renderer);
}
