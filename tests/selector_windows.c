/* Exercise the real Win32 filesystem helpers and selector event handling. */
#define SDL_MAIN_HANDLED
#include <SDL.h>

static int test_wait_event(SDL_Event *event);
static char *test_preferences(const char *org, const char *app);
#define SDL_WaitEvent test_wait_event
#define SDL_GetPrefPath test_preferences
#include "../selector.c"
#undef SDL_WaitEvent
#undef SDL_GetPrefPath
#include "game.h"
#include "init.h"
#include "map.h"
#include <ctype.h>

static char preferences_path[LOTR_PATH_CAPACITY];

static int test_wait_event(SDL_Event *event)
{
    static int started;
    memset(event, 0, sizeof(*event));
    event->type = SDL_KEYDOWN;
    event->key.keysym.sym = started++ ? SDLK_ESCAPE : SDLK_RETURN;
    return 1;
}

static char *test_preferences(const char *org, const char *app)
{
    return !strcmp(org, "Wonderland") && !strcmp(app, "LotR") ?
        SDL_strdup(preferences_path) : NULL;
}

static int check(int condition, const char *message)
{
    if (!condition)
        fprintf(stderr, "selector path regression failed: %s\n", message);
    return condition;
}

static int environment_path(const wchar_t *key, char output[LOTR_PATH_CAPACITY])
{
    wchar_t path[LOTR_PATH_CAPACITY];
    DWORD length = GetEnvironmentVariableW(key, path, LOTR_PATH_CAPACITY);
    return length && length < LOTR_PATH_CAPACITY &&
        WideCharToMultiByte(CP_UTF8, 0, path, -1, output, LOTR_PATH_CAPACITY, NULL, NULL);
}

static int click(Selector *selector, SDL_Rect button)
{
    SDL_Event event = {0};
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = button.x + 2;
    event.button.y = button.y + 2;
    return selector_event(selector, &event);
}

static void key(Selector *selector, SDL_Keycode symbol, Uint16 modifiers)
{
    SDL_Event event = {0};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = symbol;
    event.key.keysym.mod = modifiers;
    selector_event(selector, &event);
}

static void type_path(Selector *selector, const char *path)
{
    click(selector, data_rect);
    key(selector, SDLK_a, KMOD_CTRL);
    while (*path) {
        SDL_Event event = {0};
        event.type = SDL_TEXTINPUT;
        size_t length = strlen(path);
        if (length >= sizeof(event.text.text)) {
            length = sizeof(event.text.text) - 1;
            while (((unsigned char)path[length] & 0xc0) == 0x80)
                --length;
        }
        memcpy(event.text.text, path, length);
        selector_event(selector, &event);
        path += length;
    }
}

static int drive_paths(const char *parent)
{
    DirectoryBrowser browser = {0};
    char root[] = {parent[0], ':', '\\', '\0'};
    int ok = check(parent[1] == ':' && browser_read(&browser, root) &&
                   SDL_strcasecmp(browser.path, (char[]){parent[0], ':', '/', '\0'}) == 0,
                   "backslash drive root normalizes to drive-letter slash form");
    if (ok) {
        browser_open_entry(&browser, "..");
        ok = check(strlen(browser.path) == 3 && browser.path[1] == ':' &&
                   browser.path[2] == '/' && SDL_tolower(browser.path[0]) == SDL_tolower(parent[0]),
                   "Up at a drive root stays on the same drive");
    }
    root[0] = (char)tolower((unsigned char)root[0]);
    root[2] = '/';
    ok = ok && check(browser_read(&browser, root) && strlen(browser.path) == 3,
                    "lowercase drive letter with forward slash opens");
    browser_clear(&browser);
    return ok;
}

static int moved_paths(const char *parent, const char *settings)
{
    const char *unicode_name = "\xe7\xa7\xbb\xe5\x8b\x95 & \xc3\xa9";
    char old[LOTR_PATH_CAPACITY], moved[LOTR_PATH_CAPACITY];
    char normalized[LOTR_PATH_CAPACITY];
    if (!selector_join(old, sizeof(old), parent, "Old data") ||
        !selector_join(moved, sizeof(moved), parent, unicode_name))
        return 0;
    SDL_strlcpy(normalized, moved, sizeof(normalized));
    for (char *p = normalized; *p; ++p)
        if (*p == '\\') *p = '/';

    Selector selector = {.selected = LOTR_DATA_TWO_TOWERS};
    type_path(&selector, old);
    if (!check(selector.ready && click(&selector, play_rect) == 1,
               "original Windows drive-letter data path plays"))
        return 0;
    selector_write_settings(settings, edition_paths, selector.selected);

    wchar_t wide_old[LOTR_PATH_CAPACITY], wide_moved[LOTR_PATH_CAPACITY];
    if (!check(selector_wide_path(old, wide_old) && selector_wide_path(moved, wide_moved) &&
               MoveFileW(wide_old, wide_moved), "fixture moved to a Unicode folder"))
        return 0;
    memset(edition_paths, 0, sizeof(edition_paths));
    selector.selected = LOTR_DATA_STANDARD;
    selector_read_settings(settings, edition_paths, &selector.selected);
    select_card(&selector, selector.selected);
    if (!check(selector.selected == LOTR_DATA_TWO_TOWERS &&
               strcmp(edition_paths[selector.selected].data, old) == 0 &&
               !selector.ready && click(&selector, play_rect) != 1,
               "stale saved Windows path blocks Play"))
        return 0;

    type_path(&selector, parent);
    click(&selector, browse_data_rect);
    int ok = check(selector.browser.open && selector.browser.count == 1 &&
                   strcmp(selector.browser.entries[0], unicode_name) == 0,
                   "Windows browser enumerates the Unicode folder");
    if (ok) {
        key(&selector, SDLK_RETURN, 0);
        key(&selector, SDLK_u, 0);
        ok = check(selector.ready && click(&selector, play_rect) == 1 &&
                   SDL_strcasecmp(edition_paths[selector.selected].data, normalized) == 0,
                   "browse selection validates and enables Play");
    }
    browser_clear(&selector.browser);
    if (!ok)
        return 0;

    selector_write_settings(settings, edition_paths, selector.selected);
    memset(edition_paths, 0, sizeof(edition_paths));
    selector.selected = LOTR_DATA_STANDARD;
    selector_read_settings(settings, edition_paths, &selector.selected);
    select_card(&selector, selector.selected);
    FILE *asset = lotr_try_open_asset("arts.dat", 0);
    ok = check(selector.selected == LOTR_DATA_TWO_TOWERS && selector.ready && asset &&
               SDL_strcasecmp(edition_paths[selector.selected].data, normalized) == 0 &&
               click(&selector, play_rect) == 1,
               "Unicode XML settings round-trip and native asset open");
    if (asset)
        fclose(asset);
    return ok;
}

static int play_first_frame(const char *edition, const char *settings)
{
    int towers = strcmp(edition, "two-towers") == 0;
    LotrDataFormat profile = towers ? LOTR_DATA_TWO_TOWERS : LOTR_DATA_ENHANCED_CD;
    if (!towers) {
        if (!check(environment_path(L"LOTR_NATIVE_CD", edition_paths[profile].data),
                   "Enhanced CD fixture available"))
            return 0;
        selector_write_settings(settings, edition_paths, profile);
    }
    const char *suffix = "selector.xml";
    size_t length = strlen(settings);
    if (!check(length > strlen(suffix) && !strcmp(settings + length - strlen(suffix), suffix),
               "test preferences filename has the expected suffix"))
        return 0;
    length -= strlen(suffix);
    memcpy(preferences_path, settings, length);
    preferences_path[length] = '\0';
    if (!check(lotr_select_edition() == 1 && lotr_current_edition()->data_format == profile,
               "native selector Play accepted the saved profile"))
        return 0;
    lotr_init();
    game_new();
    game_init_graphics();
    game_next_frame();
    int ok = check(map_get_id() == (towers ? 6 : 0) &&
                   game_get_leader()->id == (towers ? 0xa4 : 0xa0),
                   "native engine rendered a frame on the expected starting map");
    lotr_close();
    if (ok)
        printf("Native selector to first gameplay frame passed for %s\n", edition);
    return ok;
}

int main(void)
{
    char parent[LOTR_PATH_CAPACITY], settings[LOTR_PATH_CAPACITY];
    if (!check(environment_path(L"LOTR_NATIVE_DATA_PARENT", parent) &&
               environment_path(L"LOTR_NATIVE_SETTINGS", settings), "test paths passed in UTF-16"))
        return 1;
    SDL_SetMainReady();
    const char *launch = getenv("LOTR_NATIVE_START_EDITION");
    if (launch)
        return play_first_frame(launch, settings) ? 0 : 1;
    if (!check(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL video initialized"))
        return 1;
    int ok = drive_paths(parent) && moved_paths(parent, settings);
    char second_drive[LOTR_PATH_CAPACITY];
    if (ok && environment_path(L"LOTR_NATIVE_SECOND_DRIVE", second_drive)) {
        ok = drive_paths(second_drive);
        if (ok)
            printf("Additional Windows drive root checked %c:/\n", second_drive[0]);
    }
    SDL_Quit();
    if (ok)
        puts("Native Windows selector tests passed for drive roots, moved folders and Unicode paths");
    return ok ? 0 : 1;
}
