#include "lotr.h"
#include "selector.h"
#include "utils.h"
#include "av.h"
#include "archive.h"
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "selector_art.h"
#include "selector_files.h"
#include "roland.h"
#include "sound.h"

enum { WIDTH = 944, HEIGHT = 640, BROWSER_ROWS = 9 };
enum { EDITION, DATA, BROWSE_DATA, MOVIES, BROWSE_MOVIES, MUSIC, PROFILE, PORT, APPLY, LAUNCH, FOCUS_COUNT };

static const Uint32 background = 0x10181d, panel = 0x1a262c, field = 0x101d23;
static const Uint32 bright = 0xf1e5cc, muted = 0xa3b0ac, gold = 0xe3b46f;
static const Uint32 border = 0x3c4a4d, danger = 0xffbd94;
static const SDL_Rect data_rect = {24, 194, 748, 48};
static const SDL_Rect movies_rect = {24, 285, 748, 48};
static const SDL_Rect browse_data_rect = {786, 194, 134, 48};
static const SDL_Rect browse_movies_rect = {786, 285, 134, 48};
static const SDL_Rect music_rect = {420, 351, 250, 38};
static const SDL_Rect profile_rect = {420, 401, 500, 38};
static const SDL_Rect port_rect = {420, 451, 500, 38};
static const SDL_Rect apply_rect = {546, 568, 208, 48};
static const SDL_Rect launch_rect = {768, 568, 152, 48};
#ifdef WIN32
static const SDL_Rect drive_rect = {48, 256, 128, 32};
static const SDL_Rect drive_menu = {48, 291, 128, BROWSER_ROWS * 26};
#endif
static const SDL_Rect up_rect = {48, 548, 112, 44};
static const SDL_Rect cancel_rect = {584, 548, 126, 44};
static const SDL_Rect use_rect = {724, 548, 172, 44};

typedef struct {
    SDL_Renderer *renderer;
    int selected;
    int focus;
    int replace_text;
    int ready;
    const char *settings_path;
    char status[160];
    DirectoryBrowser browser;
} Selector;

/* The engine keeps these paths after the selector window closes. */
static EditionPaths edition_paths[LOTR_DATA_COUNT];
static int preferences_loaded;
static int last_edition = LOTR_DATA_STANDARD;

static const char *
selector_pref_path(char path[LOTR_PATH_CAPACITY], const char *filename)
{
    char *directory = SDL_GetPrefPath("Wonderland", "LotR");
    int ok = directory && selector_join(path, LOTR_PATH_CAPACITY, directory, filename);
    SDL_free(directory);
    return ok ? path : NULL;
}

static void
load_preferences(void)
{
    if (preferences_loaded)
        return;
    preferences_loaded = 1;
    for (int i = 0; i < LOTR_DATA_COUNT; ++i)
        SDL_strlcpy(edition_paths[i].data, lotr_edition_at(i)->default_data_dir,
                    sizeof(edition_paths[i].data));
    char path[LOTR_PATH_CAPACITY];
    int have_config = selector_read_settings(selector_pref_path(path, "selector.cfg"),
                                             edition_paths, &last_edition);
    int migrated = !have_config &&
        selector_read_legacy_settings(selector_pref_path(path, "selector.xml"), edition_paths, &last_edition);
    for (int i = 0; i < LOTR_DATA_COUNT; ++i) {
        if (edition_paths[i].profile[0])
            continue;
        strcpy(edition_paths[i].profile, "off");
    }
    if (migrated && !selector_write_settings(selector_pref_path(path, "selector.cfg"), edition_paths, last_edition))
        fputs("lotr: could not migrate preferences to selector.cfg.\n", stderr);
}

#ifdef LOTR_ROLAND_OUTPUT
static EditionPaths *
roland_preferences(int edition)
{
    return &edition_paths[edition == LOTR_DATA_TWO_TOWERS ? LOTR_DATA_TWO_TOWERS : LOTR_DATA_STANDARD];
}
#endif

int
lotr_apply_edition_preferences(const char *port, const char *profile)
{
    load_preferences();
    int edition = lotr_current_edition()->data_format;
    sound_set_music_enabled(!edition_paths[edition].music_off);
#ifdef LOTR_ROLAND_OUTPUT
    if (edition != LOTR_DATA_ENHANCED_CD) {
        EditionPaths *audio = roland_preferences(edition);
        const char *mode = profile ? profile :
            port && !strcmp(audio->profile, "off") ? "lapc-i" : audio->profile;
        if (!roland_activate(port ? port : audio->port, mode, edition != LOTR_DATA_TWO_TOWERS,
                             port || profile))
            return 0;
        if (port || profile) {
            if (port)
                SDL_strlcpy(audio->port, port, sizeof(audio->port));
            if (mode != audio->profile)
                SDL_strlcpy(audio->profile, mode, sizeof(audio->profile));
            char path[LOTR_PATH_CAPACITY];
            if (!selector_write_settings(selector_pref_path(path, "selector.cfg"), edition_paths, last_edition)) {
                fputs("lotr: could not save selector.cfg.\n", stderr);
                return 0;
            }
        }
        return 1;
    }
#endif
    if (port || profile) {
        fputs("lotr: Roland MIDI is unavailable for this edition or platform.\n", stderr);
        return 0;
    }
    return 1;
}


static int
enhanced_cd(const Selector *selector)
{
    return selector->selected == LOTR_DATA_ENHANCED_CD;
}

static void
draw_color(SDL_Renderer *renderer, Uint32 rgb)
{
    SDL_SetRenderDrawColor(renderer, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, 255);
}

static void
box(SDL_Renderer *renderer, SDL_Rect rect, Uint32 fill, Uint32 outline)
{
    draw_color(renderer, fill);
    SDL_RenderFillRect(renderer, &rect);
    if (outline != fill) {
        draw_color(renderer, outline);
        SDL_RenderDrawRect(renderer, &rect);
    }
}

static void
text_at(SDL_Renderer *renderer, const char *text, int x, int y, int scale, Uint32 color)
{
    draw_color(renderer, color);
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if ((*p & 0xc0) == 0x80)
            continue;
        unsigned char glyph = *p >= 32 && *p < 127 ? *p : '?';
        for (int column = 0; column < 5; ++column)
            for (int row = 0; row < 7; ++row)
                if (selector_font[glyph][column] & (1u << row)) {
                    SDL_Rect pixel = {x + column * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
        x += 6 * scale;
    }
}

static void
path_at(SDL_Renderer *renderer, const char *path, int x, int y, size_t limit, Uint32 color)
{
    size_t length = strlen(path);
    if (length > limit) {
        const char *tail = path + length - limit + 3;
        while (((unsigned char)*tail & 0xc0) == 0x80)
            ++tail;
        text_at(renderer, "...", x, y, 2, color);
        text_at(renderer, tail, x + 36, y, 2, color);
    } else {
        text_at(renderer, path, x, y, 2, color);
    }
}

static void
button(SDL_Renderer *renderer, SDL_Rect rect, const char *label,
       int focused, int primary, int enabled)
{
    Uint32 fill = primary && enabled ? gold : panel;
    box(renderer, rect, fill, focused ? bright : border);
    text_at(renderer, label, rect.x + (rect.w - (int)strlen(label) * 12) / 2,
            rect.y + (rect.h - 14) / 2, 2, primary && enabled ? background : enabled ? bright : muted);
}

static SDL_Rect
card_rect(int index)
{
    SDL_Rect rect = {24 + index * 226, 49, 218, 98};
    return rect;
}

static void
draw_browser(Selector *selector)
{
    SDL_Renderer *renderer = selector->renderer;
    DirectoryBrowser *browser = &selector->browser;
    box(renderer, (SDL_Rect){24, 214, 896, 402}, panel, border);
    text_at(renderer, browser->movies ? "Choose the movie folder" : "Choose the game data folder",
            48, 234, 2, gold);
#ifdef WIN32
    char label[] = "Drive -:";
    if (browser->path[1] == ':')
        label[6] = SDL_toupper(browser->path[0]);
    int enabled = browser->drive_count > 0;
    box(renderer, drive_rect, field, browser->drive_open ? gold : border);
    text_at(renderer, label, drive_rect.x + 8, drive_rect.y + 9, 2, enabled ? bright : muted);
    draw_color(renderer, enabled ? bright : muted);
    int chevron_x = drive_rect.x + drive_rect.w - 15;
    int chevron_y = drive_rect.y + 14;
    SDL_RenderDrawLine(renderer, chevron_x - 4, chevron_y, chevron_x, chevron_y + 4);
    SDL_RenderDrawLine(renderer, chevron_x, chevron_y + 4, chevron_x + 4, chevron_y);
    path_at(renderer, browser->path, 190, 264, 57, bright);
#else
    path_at(renderer, browser->path, 48, 264, 68, bright);
#endif
    for (int row = 0; row < BROWSER_ROWS && browser->first + row < browser->count; ++row) {
        int index = browser->first + row;
        int y = 296 + row * 26;
        box(renderer, (SDL_Rect){48, y, 848, 24}, index == browser->selected ? border : field, field);
        path_at(renderer, browser->entries[index], 60, y + 5, 66, bright);
    }
    if (browser->count == 0)
        text_at(renderer, "No subfolders", 48, 312, 2, muted);
    button(renderer, up_rect, "Up", 0, 0, 1);
    button(renderer, cancel_rect, "Cancel", 0, 0, 1);
    button(renderer, use_rect, "Use folder", 0, 1, 1);
#ifdef WIN32
    if (browser->drive_open)
        for (int row = 0; row < BROWSER_ROWS && browser->drive_first + row < browser->drive_count; ++row) {
            int i = browser->drive_first + row;
            int y = drive_menu.y + row * 26;
            char name[] = {browser->drives[i], ':', 0};
            box(renderer, (SDL_Rect){drive_menu.x, y, drive_menu.w, 26},
                i == browser->drive_selected ? border : field, border);
            text_at(renderer, name, drive_menu.x + 12, y + 6, 2, bright);
        }
#endif
}

static void
draw_selector(Selector *selector)
{
    static const char *const releases[] = {"Standard DOS", "Enhanced CD", "Standard DOS", "Demo"};
    SDL_Renderer *renderer = selector->renderer;
    EditionPaths *paths = &edition_paths[selector->selected];
    draw_color(renderer, background);
    SDL_RenderClear(renderer);
    text_at(renderer, "Wonderland - Edition settings", 24, 24, 2, gold);
    for (int i = 0; i < LOTR_DATA_COUNT; ++i) {
        SDL_Rect rect = card_rect(i);
        int active = i == selector->selected;
        box(renderer, rect, active ? 0x303239 : panel,
            active ? (selector->focus == EDITION ? bright : gold) : border);
        text_at(renderer, i == LOTR_DATA_TWO_TOWERS ? "VOLUME II" : "VOLUME I",
                rect.x + 14, rect.y + 13, 1, active ? gold : muted);
        text_at(renderer, i == LOTR_DATA_TWO_TOWERS ? "The Two Towers" : "The Fellowship",
                rect.x + 14, rect.y + 29, 2, bright);
        if (i != LOTR_DATA_TWO_TOWERS)
            text_at(renderer, "of the Ring", rect.x + 14, rect.y + 47, 2, bright);
        text_at(renderer, releases[i], rect.x + 14, rect.y + 73, 2, active ? gold : muted);
    }
    text_at(renderer, "Game data folder", 24, 168, 2, bright);
    box(renderer, data_rect, field, selector->focus == DATA ? gold : border);
    path_at(renderer, paths->data, 38, 210, 59, bright);
    button(renderer, browse_data_rect, "Browse", selector->focus == BROWSE_DATA, 0, 1);
    if (enhanced_cd(selector)) {
        text_at(renderer, "Movie folder (optional)", 24, 259, 2, bright);
        box(renderer, movies_rect, field, selector->focus == MOVIES ? gold : border);
        path_at(renderer, paths->movies[0] ? paths->movies : "Same as game data folder",
                38, 301, 59, paths->movies[0] ? bright : muted);
        button(renderer, browse_movies_rect, "Browse", selector->focus == BROWSE_MOVIES, 0, 1);
    }
    text_at(renderer, "Music at launch", 24, 363, 2, bright);
    button(renderer, music_rect, paths->music_off ? "Off" : "On",
           selector->focus == MUSIC, 0, 1);
    if (enhanced_cd(selector)) {
        text_at(renderer, "Enhanced CD uses the CD soundtrack.", 24, 411, 2, muted);
    } else {
#ifdef LOTR_ROLAND_OUTPUT
        EditionPaths *audio = roland_preferences(selector->selected);
        int active = strcmp(audio->profile, "off") != 0;
        const char *profile = !active ? "Built-in playback" :
            !strcmp(audio->profile, "lapc-i") ? "Roland LAPC-I / CM-32L" : "Roland MT-32";
        text_at(renderer, "Roland profile", 24, 413, 2, bright);
        button(renderer, profile_rect, profile, selector->focus == PROFILE, 0, 1);
        text_at(renderer, "Windows MIDI output", 24, 463, 2, bright);
        box(renderer, port_rect, field, selector->focus == PORT ? gold : border);
        path_at(renderer, audio->port[0] ? audio->port : "No port selected",
                432, 463, 39, active ? bright : muted);
        if (selector->selected != LOTR_DATA_TWO_TOWERS)
            text_at(renderer, "Standard DOS and the sampler share Roland settings.", 24, 505, 1, muted);
#else
        text_at(renderer, "Roland MIDI output is available on Windows.", 24, 411, 2, muted);
#endif
    }
    text_at(renderer, "Save slots and legacy imports are managed in-game.", 24, 521, 1, muted);
    text_at(renderer, selector->status, 24, 540, 2, selector->ready ? muted : danger);
    text_at(renderer, "Tab to move focus  |  Left / Right to change a choice", 24, 588, 1, muted);
    button(renderer, apply_rect, "Apply settings", selector->focus == APPLY, 0, 1);
    button(renderer, launch_rect, "Launch", selector->focus == LAUNCH, 1, selector->ready);
    if (selector->browser.open)
        draw_browser(selector);
    SDL_RenderPresent(renderer);
}

static void
validate_paths(Selector *selector)
{
    static const char *const palettes[] = {"arts.pal", "shapes.pal"};
    static const char *const cd_files[] = {"magics.adl", "vid.dat", "vid.pal"};
    static const char *const towers_files[] = {"ongame.dat", "ongame.pal"};
    EditionPaths *paths = &edition_paths[selector->selected];
    selector->ready = 0;
    if (!paths->data[0]) {
        SDL_strlcpy(selector->status, "Choose a game data folder.", sizeof(selector->status));
        return;
    }
    const char *movies = enhanced_cd(selector) && paths->movies[0] ? paths->movies : NULL;
    if (!lotr_configure_edition(lotr_edition_at(selector->selected)->key, paths->data, movies))
        return;
    if (!archive_validate_edition(selector->status, sizeof(selector->status)))
        return;
    const char *const *extra = NULL;
    size_t count = 0;
    if (enhanced_cd(selector)) {
        extra = cd_files;
        count = SDL_arraysize(cd_files);
    } else if (selector->selected == LOTR_DATA_TWO_TOWERS) {
        extra = towers_files;
        count = SDL_arraysize(towers_files);
    }
    for (size_t i = 0; i < SDL_arraysize(palettes) + count; ++i) {
        const char *file = i < SDL_arraysize(palettes) ? palettes[i] : extra[i - SDL_arraysize(palettes)];
        if (lotr_asset_size(file, 0) <= 0) {
            snprintf(selector->status, sizeof(selector->status), "Game data folder needs %s.", file);
            return;
        }
    }
    if (enhanced_cd(selector)) {
        for (int i = 0; av_name(i); ++i) {
            char name[24];
            snprintf(name, sizeof(name), "%s.av", av_name(i));
            if (lotr_asset_size(name, 1) <= 0) {
                snprintf(selector->status, sizeof(selector->status), "Movie folder needs %s.", name);
                return;
            }
        }
    }
    selector->ready = 1;
    SDL_strlcpy(selector->status, "Game data found. Ready to play.", sizeof(selector->status));
}

static void
focus_field(Selector *selector, int focus)
{
    selector->focus = focus;
    selector->replace_text = 0;
    if (!selector->browser.open && (focus == DATA || focus == MOVIES))
        SDL_StartTextInput();
    else
        SDL_StopTextInput();
}

static void
select_card(Selector *selector, int edition)
{
    selector->selected = edition;
    focus_field(selector, EDITION);
    validate_paths(selector);
}

static void
open_browser(Selector *selector, int movies)
{
    DirectoryBrowser *browser = &selector->browser;
    EditionPaths *paths = &edition_paths[selector->selected];
    const char *initial = movies && paths->movies[0] ? paths->movies : paths->data;
    if (!initial[0] || !browser_read(browser, initial)) {
        if (!browser_read(browser, ".")) {
            SDL_strlcpy(selector->status, "Could not open the folder browser. Type or paste the path.",
                        sizeof(selector->status));
            return;
        }
    }
    browser->open = 1;
    browser->movies = movies;
#ifdef WIN32
    DWORD mask = GetLogicalDrives();
    browser->drive_count = 0;
    browser->drive_open = 0;
    for (int i = 0; i < 26; ++i)
        if (mask & (1u << i))
            browser->drives[browser->drive_count++] = 'A' + i;
#endif
    SDL_StopTextInput();
}

static void
close_browser(Selector *selector, int use)
{
    DirectoryBrowser *browser = &selector->browser;
    EditionPaths *paths = &edition_paths[selector->selected];
    if (use) {
        char *path = browser->movies ? paths->movies : paths->data;
        SDL_strlcpy(path, browser->path, LOTR_PATH_CAPACITY);
        validate_paths(selector);
    }
    browser->open = 0;
    browser_clear(browser);
    focus_field(selector, browser->movies ? BROWSE_MOVIES : BROWSE_DATA);
}

static void
browser_move(DirectoryBrowser *browser, int distance)
{
    if (!browser->count)
        return;
    int selected = browser->selected + distance;
    browser->selected = selected < 0 ? 0 : selected >= browser->count ? browser->count - 1 : selected;
    if (browser->selected < browser->first)
        browser->first = browser->selected;
    if (browser->selected >= browser->first + BROWSER_ROWS)
        browser->first = browser->selected - BROWSER_ROWS + 1;
}

static void
browser_open_entry(DirectoryBrowser *browser, const char *name)
{
    char path[LOTR_PATH_CAPACITY];
    if (selector_join(path, sizeof(path), browser->path, name))
        browser_read(browser, path);
}

static int
contains(SDL_Rect rect, int x, int y)
{
    SDL_Point point = {x, y};
    return SDL_PointInRect(&point, &rect);
}

#ifdef WIN32
static void
drive_move(DirectoryBrowser *browser, int step)
{
    int next = browser->drive_selected + step;
    browser->drive_selected = SDL_clamp(next, 0, browser->drive_count - 1);
    if (browser->drive_selected < browser->drive_first)
        browser->drive_first = browser->drive_selected;
    if (browser->drive_selected >= browser->drive_first + BROWSER_ROWS)
        browser->drive_first = browser->drive_selected - BROWSER_ROWS + 1;
}

static void
drive_menu_open(DirectoryBrowser *browser)
{
    if (!browser->drive_count)
        return;
    browser->drive_open = 1;
    browser->drive_first = browser->drive_selected = 0;
    for (int i = 0; browser->path[1] == ':' && i < browser->drive_count; ++i)
        if (browser->drives[i] == SDL_toupper(browser->path[0])) {
            drive_move(browser, i);
            break;
        }
}

static void
drive_open_selected(Selector *selector, int index)
{
    DirectoryBrowser *browser = &selector->browser;
    char root[] = {browser->drives[index], ':', '/', 0};
    browser->drive_open = 0;
    if (!browser_read(browser, root))
        SDL_strlcpy(selector->status, "Could not open that drive.", sizeof(selector->status));
}
#endif

static void
browser_event(Selector *selector, const SDL_Event *event)
{
    DirectoryBrowser *browser = &selector->browser;
#ifdef WIN32
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int x = event->button.x, y = event->button.y;
        if (browser->drive_open) {
            int index = browser->drive_first + (y - drive_menu.y) / 26;
            if (contains(drive_menu, x, y) && index < browser->drive_count)
                drive_open_selected(selector, index);
            else
                browser->drive_open = 0;
            return;
        }
        if (contains(drive_rect, x, y)) {
            drive_menu_open(browser);
            return;
        }
    }
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_d) {
        if (browser->drive_open)
            browser->drive_open = 0;
        else
            drive_menu_open(browser);
        return;
    }
    if (browser->drive_open) {
        if (event->type == SDL_MOUSEWHEEL)
            drive_move(browser, -event->wheel.y);
        else if (event->type == SDL_KEYDOWN)
            switch (event->key.keysym.sym) {
                case SDLK_ESCAPE: browser->drive_open = 0; break;
                case SDLK_UP: drive_move(browser, -1); break;
                case SDLK_DOWN: drive_move(browser, 1); break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER: drive_open_selected(selector, browser->drive_selected); break;
            }
        return;
    }
#endif
    if (event->type == SDL_MOUSEWHEEL) {
        browser_move(browser, -event->wheel.y);
    } else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int x = event->button.x, y = event->button.y;
        if (contains(cancel_rect, x, y))
            close_browser(selector, 0);
        else if (contains(use_rect, x, y))
            close_browser(selector, 1);
        else if (contains(up_rect, x, y))
            browser_open_entry(browser, "..");
        else if (contains((SDL_Rect){48, 296, 848, BROWSER_ROWS * 26}, x, y)) {
            int entry = browser->first + (y - 296) / 26;
            if (entry < browser->count)
                browser_open_entry(browser, browser->entries[entry]);
        }
    } else if (event->type == SDL_KEYDOWN) {
        switch (event->key.keysym.sym) {
            case SDLK_ESCAPE: close_browser(selector, 0); break;
            case SDLK_u: close_browser(selector, 1); break;
            case SDLK_BACKSPACE: browser_open_entry(browser, ".."); break;
            case SDLK_UP: browser_move(browser, -1); break;
            case SDLK_DOWN: browser_move(browser, 1); break;
            case SDLK_PAGEUP: browser_move(browser, -BROWSER_ROWS); break;
            case SDLK_PAGEDOWN: browser_move(browser, BROWSER_ROWS); break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (browser->count)
                    browser_open_entry(browser, browser->entries[browser->selected]);
                break;
        }
    }
}

static void
edit_path(Selector *selector, char *path, const char *input)
{
    size_t length = selector->replace_text ? 0 : strlen(path);
    size_t added = strcspn(input, "\r\n");
    if (length + added >= LOTR_PATH_CAPACITY) {
        SDL_strlcpy(selector->status, "The folder path is too long.", sizeof(selector->status));
        return;
    }
    memcpy(path + length, input, added);
    path[length + added] = '\0';
    selector->replace_text = 0;
    validate_paths(selector);
}

static int
settings_control(const Selector *selector, int control)
{
    if (control == MUSIC)
        return 1;
#ifdef LOTR_ROLAND_OUTPUT
    if (!enhanced_cd(selector))
        return control == PROFILE ||
            (control == PORT && strcmp(roland_preferences(selector->selected)->profile, "off"));
#endif
    return 0;
}

static void
change_audio(Selector *selector, int control, int step)
{
    if (control == MUSIC) {
        edition_paths[selector->selected].music_off ^= 1;
        return;
    }
#ifdef LOTR_ROLAND_OUTPUT
    EditionPaths *audio = roland_preferences(selector->selected);
    if (control == PROFILE) {
        int index = !strcmp(audio->profile, "off") ? 0 : !strcmp(audio->profile, "lapc-i") ? 1 : 2;
        const char *profiles[] = {"off", "lapc-i", "mt-32"};
        strcpy(audio->profile, profiles[(index + step + 3) % 3]);
        if (audio->port[0] || !strcmp(audio->profile, "off"))
            return;
    }
    unsigned count = roland_port_count();
    char name[32];
    unsigned current = count;
    for (unsigned i = 0; i < count; ++i)
        if (roland_port_name(i, name) && !strcmp(name, audio->port)) {
            current = i;
            break;
        }
    if (count && current == count)
        current = step > 0 ? count - 1 : 0;
    for (unsigned i = 0; i < count; ++i) {
        current = step > 0 ? (current + 1) % count : (current + count - 1) % count;
        if (roland_port_name(current, name)) {
            strcpy(audio->port, name);
            return;
        }
    }
    SDL_strlcpy(selector->status, "No Windows MIDI outputs found.", sizeof(selector->status));
#else
    (void)step;
#endif
}

static int
activate_control(Selector *selector, int control, int step)
{
    if (settings_control(selector, control)) {
        change_audio(selector, control, step);
    } else if (control == APPLY) {
        int saved = selector_write_settings(selector->settings_path, edition_paths, selector->selected);
        SDL_strlcpy(selector->status, saved ? "Settings saved." : "Could not save settings.",
                    sizeof(selector->status));
    } else if (control == LAUNCH && selector->ready) {
        return 1;
    }
    return -1;
}

static int
selector_event(Selector *selector, const SDL_Event *event)
{
    if (event->type == SDL_QUIT)
        return 0;
    if (selector->browser.open) {
        browser_event(selector, event);
        return -1;
    }
    EditionPaths *paths = &edition_paths[selector->selected];
    char *path = selector->focus == DATA ? paths->data :
        selector->focus == MOVIES ? paths->movies : NULL;
    if (event->type == SDL_TEXTINPUT && path)
        edit_path(selector, path, event->text.text);
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int x = event->button.x, y = event->button.y;
        for (int i = 0; i < LOTR_DATA_COUNT; ++i)
            if (contains(card_rect(i), x, y))
                select_card(selector, i);
        if (contains(data_rect, x, y))
            focus_field(selector, DATA);
        else if (enhanced_cd(selector) && contains(movies_rect, x, y))
            focus_field(selector, MOVIES);
        else if (contains(browse_data_rect, x, y))
            open_browser(selector, 0);
        else if (enhanced_cd(selector) && contains(browse_movies_rect, x, y))
            open_browser(selector, 1);
        else if (contains(apply_rect, x, y))
            return activate_control(selector, APPLY, 1);
        else if (contains(launch_rect, x, y))
            return activate_control(selector, LAUNCH, 1);
        else {
            const SDL_Rect controls[] = {music_rect, profile_rect, port_rect};
            for (int control = MUSIC; control <= PORT; ++control)
                if (settings_control(selector, control) && contains(controls[control - MUSIC], x, y)) {
                    focus_field(selector, control);
                    return activate_control(selector, control, 1);
                }
        }
    }
    if (event->type != SDL_KEYDOWN)
        return -1;
    SDL_Keycode key = event->key.keysym.sym;
    Uint16 modifiers = event->key.keysym.mod;
    if (key == SDLK_ESCAPE)
        return 0;
    if (key == SDLK_TAB) {
        int step = modifiers & KMOD_SHIFT ? -1 : 1;
        int focus = selector->focus;
        do {
            focus = (focus + step + FOCUS_COUNT) % FOCUS_COUNT;
        } while ((!enhanced_cd(selector) && (focus == MOVIES || focus == BROWSE_MOVIES)) ||
                 (focus >= MUSIC && focus <= PORT && !settings_control(selector, focus)));
        focus_field(selector, focus);
    } else if (path && key == SDLK_BACKSPACE) {
        size_t length = selector->replace_text ? 0 : strlen(path);
        if (length)
            do { --length; } while (length && ((unsigned char)path[length] & 0xc0) == 0x80);
        path[length] = '\0';
        selector->replace_text = 0;
        validate_paths(selector);
    } else if (path && (modifiers & (KMOD_CTRL | KMOD_GUI))) {
        if (key == SDLK_a) {
            selector->replace_text = 1;
        } else if (key == SDLK_c) {
            SDL_SetClipboardText(path);
        } else if (key == SDLK_v) {
            char *clipboard = SDL_GetClipboardText();
            if (clipboard) {
                edit_path(selector, path, clipboard);
                SDL_free(clipboard);
            }
        }
    } else if (selector->focus == EDITION && (key == SDLK_LEFT || key == SDLK_RIGHT ||
                                             key == SDLK_UP || key == SDLK_DOWN)) {
        int step = key == SDLK_LEFT || key == SDLK_UP ? -1 : 1;
        select_card(selector, (selector->selected + step + LOTR_DATA_COUNT) % LOTR_DATA_COUNT);
    } else if (settings_control(selector, selector->focus) &&
               (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_SPACE)) {
        return activate_control(selector, selector->focus, key == SDLK_LEFT ? -1 : 1);
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (selector->focus == BROWSE_DATA || selector->focus == BROWSE_MOVIES)
            open_browser(selector, selector->focus == BROWSE_MOVIES);
        else
            return activate_control(selector,
                selector->focus >= MUSIC ? selector->focus : LAUNCH, 1);
    }
    return -1;
}

int
lotr_select_edition(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        return -1;
    SDL_Window *window = SDL_CreateWindow("Wonderland - Edition settings", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : NULL;
    if (!renderer) {
        fprintf(stderr, "lotr: could not open selector: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    SDL_SetWindowMinimumSize(window, WIDTH / 2, HEIGHT / 2);
    SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);
    char path[LOTR_PATH_CAPACITY];
    load_preferences();
    Selector selector = {.renderer = renderer, .selected = last_edition,
                         .settings_path = selector_pref_path(path, "selector.cfg")};
    select_card(&selector, selector.selected);

    int result = -1;
    while (result < 0) {
        SDL_Event event;
        draw_selector(&selector);
        if (!SDL_WaitEvent(&event))
            break;
        result = selector_event(&selector, &event);
    }
    last_edition = selector.selected;
    if (result >= 0 && !selector_write_settings(selector.settings_path, edition_paths, last_edition))
        fputs("lotr: could not save selector.cfg.\n", stderr);
    browser_clear(&selector.browser);
    SDL_StopTextInput();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
