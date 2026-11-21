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

enum { WIDTH = 944, HEIGHT = 640, BROWSER_ROWS = 9 };
enum { EDITION, DATA, BROWSE_DATA, MOVIES, BROWSE_MOVIES, PLAY, FOCUS_COUNT };

static const Uint32 background = 0x10181d, panel = 0x1a262c, field = 0x101d23;
static const Uint32 bright = 0xf1e5cc, muted = 0xa3b0ac, gold = 0xe3b46f;
static const Uint32 border = 0x3c4a4d, danger = 0xffbd94;
static const SDL_Rect art_rect = {24, 24, 896, 176};
static const SDL_Rect data_rect = {24, 379, 748, 48};
static const SDL_Rect movies_rect = {24, 470, 748, 48};
static const SDL_Rect browse_data_rect = {786, 379, 134, 48};
static const SDL_Rect browse_movies_rect = {786, 470, 134, 48};
static const SDL_Rect play_rect = {760, 568, 160, 48};
static const SDL_Rect up_rect = {48, 548, 112, 44};
static const SDL_Rect cancel_rect = {584, 548, 126, 44};
static const SDL_Rect use_rect = {724, 548, 172, 44};

typedef struct {
    SDL_Renderer *renderer;
    SDL_Texture *art;
    int selected;
    int focus;
    int replace_text;
    int ready;
    char status[160];
    DirectoryBrowser browser;
} Selector;

/* The engine keeps these paths after the selector window closes. */
static EditionPaths edition_paths[LOTR_DATA_COUNT];

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
    SDL_Rect rect = {24 + index * 226, 234, 218, 98};
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
    path_at(renderer, browser->path, 48, 264, 68, bright);
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
}

static void
draw_selector(Selector *selector)
{
    static const char *const releases[] = {"Standard DOS", "Enhanced CD", "Standard DOS", "Demo"};
    SDL_Renderer *renderer = selector->renderer;
    EditionPaths *paths = &edition_paths[selector->selected];
    draw_color(renderer, background);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, selector->art, NULL, &art_rect);
    text_at(renderer, "THE LORD OF THE RINGS", 57, 68, 4, bright);
    text_at(renderer, "Wonderland game engine", 59, 123, 2, gold);

    text_at(renderer, "Choose an edition", 24, 213, 2, bright);
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
    text_at(renderer, "Game data folder", 24, 353, 2, bright);
    box(renderer, data_rect, field, selector->focus == DATA ? gold : border);
    path_at(renderer, paths->data, 38, 395, 59, bright);
    button(renderer, browse_data_rect, "Browse", selector->focus == BROWSE_DATA, 0, 1);
    if (enhanced_cd(selector)) {
        text_at(renderer, "Movie folder (optional)", 24, 444, 2, bright);
        box(renderer, movies_rect, field, selector->focus == MOVIES ? gold : border);
        path_at(renderer, paths->movies[0] ? paths->movies : "Same as game data folder",
                38, 486, 59, paths->movies[0] ? bright : muted);
        button(renderer, browse_movies_rect, "Browse", selector->focus == BROWSE_MOVIES, 0, 1);
    }
    text_at(renderer, selector->status, 24, 540, 2, selector->ready ? muted : danger);
    text_at(renderer, "Tab to move focus  |  Enter to select  |  Esc to quit", 24, 588, 1, muted);
    button(renderer, play_rect, "Play", selector->focus == PLAY, 1, selector->ready);
    if (selector->browser.open)
        draw_browser(selector);
    SDL_RenderPresent(renderer);
}

static int
validate_archive(Selector *selector, const char *data, const char *index, int length_index)
{
    FILE *file = lotr_try_open_asset(index, 0);
    long size = lotr_asset_size(data, 0);
    int valid = file && archive_index_matches(file, size, length_index);
    if (file)
        fclose(file);
    if (!valid)
        snprintf(selector->status, sizeof(selector->status),
                 "Invalid archive pair: %s and %s.", data, index);
    return valid;
}

static void
validate_paths(Selector *selector)
{
    static const char *const demo_pairs[][2] = {
        {"arts.dat", "arts.ndx"}, {"demo.dat", "demo.ndx"}, {"shapes.dat", "shapes.ndx"},
        {"portrait.dat", "portrait.ndx"}, {"npcs.dat", "npcs.ndx"}, {"snd.dat", "snd.ndx"},
        {"democrt0.dat", "democrt0.idx"}, {"democrt1.dat", "democrt1.idx"},
        {"democrt2.dat", "democrt2.idx"}
    };
    static const char *const common_pairs[][2] = {
        {"arts.dat", "arts.ndx"}, {"map0.dat", "map0.ndx"},
        {"map1.dat", "map1.ndx"}, {"shapes.dat", "shapes.ndx"},
        {"portrait.dat", "portrait.ndx"}, {"nnpcs.dat", "npcs.ndx"}
    };
    static const char *const towers_pairs[][2] = {
        {"building.dat", "building.ndx"}, {"buildpal.dat", "buildpal.ndx"},
        {"icon.dat", "icon.ndx"}
    };
    static const char *const common[] = {
        "arts.dat", "arts.ndx", "arts.pal", "map0.dat", "map0.ndx", "map1.dat",
        "map1.ndx", "shapes.dat", "shapes.ndx", "shapes.pal", "portrait.dat",
        "portrait.ndx", "nnpcs.dat", "npcs.ndx"
    };
    static const char *const standard[] = {"snd.dat", "snd.ndx", "opening.dat", "opening.idx"};
    static const char *const cd[] = {"magics.adl", "vid.dat", "vid.pal"};
    static const char *const towers[] = {
        "building.dat", "building.ndx", "buildpal.dat", "buildpal.ndx", "icon.dat",
        "icon.ndx", "opening.dat", "opening.idx", "ongame.dat", "ongame.pal"
    };
    const char *const *files = standard;
    size_t count = SDL_arraysize(standard);
    EditionPaths *paths = &edition_paths[selector->selected];
    selector->ready = 0;
    if (!paths->data[0]) {
        SDL_strlcpy(selector->status, "Choose a game data folder.", sizeof(selector->status));
        return;
    }
    const char *movies = enhanced_cd(selector) && paths->movies[0] ? paths->movies : NULL;
    if (!lotr_configure_edition(lotr_edition_at(selector->selected)->key, paths->data, movies))
        return;
    if (selector->selected == LOTR_DATA_DEMO) {
        for (size_t i = 0; i < SDL_arraysize(demo_pairs); ++i)
            if (!validate_archive(selector, demo_pairs[i][0], demo_pairs[i][1],
                                  strstr(demo_pairs[i][1], ".idx") != NULL))
                return;
        if (lotr_asset_size("arts.pal", 0) <= 0 || lotr_asset_size("shapes.pal", 0) <= 0 ||
            lotr_asset_size("arts.ndx", 0) != 64 || lotr_asset_size("demo.ndx", 0) != 32 ||
            lotr_asset_size("snd.ndx", 0) != 108 || lotr_asset_size("npcs.ndx", 0) != 960 ||
            lotr_asset_size("portrait.ndx", 0) != 184 || lotr_asset_size("shapes.ndx", 0) != 2800) {
            SDL_strlcpy(selector->status, "The archives or palettes do not match the Fellowship demo.",
                        sizeof(selector->status));
            return;
        }
        goto ready;
    }
    for (size_t i = 0; i < SDL_arraysize(common); ++i) {
        if (lotr_asset_size(common[i], 0) <= 0) {
            snprintf(selector->status, sizeof(selector->status), "Game data folder needs %s.", common[i]);
            return;
        }
    }
    int is_towers = selector->selected == LOTR_DATA_TWO_TOWERS;
    if (lotr_asset_size("portrait.ndx", 0) != (is_towers ? 268 : 184) ||
        lotr_asset_size("npcs.ndx", 0) != 960 || lotr_asset_size("shapes.ndx", 0) != 2800 ||
        lotr_asset_size("map0.ndx", 0) < 16 * 4 ||
        lotr_asset_size("map1.ndx", 0) < (is_towers ? 112 : 96) * 4) {
        SDL_strlcpy(selector->status, "The archive indexes do not match the selected edition.",
                    sizeof(selector->status));
        return;
    }
    if (enhanced_cd(selector)) {
        files = cd;
        count = SDL_arraysize(cd);
    } else if (is_towers) {
        files = towers;
        count = SDL_arraysize(towers);
    }
    for (size_t i = 0; i < count; ++i) {
        if (lotr_asset_size(files[i], 0) <= 0) {
            snprintf(selector->status, sizeof(selector->status),
                     "%s is required for this edition. Check the game data folder.", files[i]);
            return;
        }
    }
    for (size_t i = 0; i < SDL_arraysize(common_pairs); ++i)
        if (!validate_archive(selector, common_pairs[i][0], common_pairs[i][1], 0))
            return;
    if (!is_towers && !enhanced_cd(selector) && !validate_archive(selector, "snd.dat", "snd.ndx", 0))
        return;
    if (!enhanced_cd(selector) && !validate_archive(selector, "opening.dat", "opening.idx", 1))
        return;
    if (is_towers)
        for (size_t i = 0; i < SDL_arraysize(towers_pairs); ++i)
            if (!validate_archive(selector, towers_pairs[i][0], towers_pairs[i][1], 0))
                return;
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
ready:
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

static void
browser_event(Selector *selector, const SDL_Event *event)
{
    DirectoryBrowser *browser = &selector->browser;
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
        else if (contains(play_rect, x, y) && selector->ready)
            return 1;
    }
    if (event->type != SDL_KEYDOWN)
        return -1;
    SDL_Keycode key = event->key.keysym.sym;
    Uint16 modifiers = event->key.keysym.mod;
    if (key == SDLK_ESCAPE)
        return 0;
    if (key == SDLK_TAB) {
        int step = modifiers & KMOD_SHIFT ? -1 : 1;
        int focus = (selector->focus + step + FOCUS_COUNT) % FOCUS_COUNT;
        if (!enhanced_cd(selector) && (focus == MOVIES || focus == BROWSE_MOVIES))
            focus = step > 0 ? PLAY : BROWSE_DATA;
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
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        if (selector->focus == BROWSE_DATA || selector->focus == BROWSE_MOVIES)
            open_browser(selector, selector->focus == BROWSE_MOVIES);
        else if (selector->ready)
            return 1;
    }
    return -1;
}

int
lotr_select_edition(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        return -1;
    SDL_Window *window = SDL_CreateWindow("Wonderland - Choose an edition", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : NULL;
    SDL_Texture *art = renderer ? selector_title_art(renderer) : NULL;
    if (!art) {
        fprintf(stderr, "lotr: could not open selector: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
    }
    SDL_SetWindowMinimumSize(window, WIDTH / 2, HEIGHT / 2);
    SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT);
    Selector selector = {.renderer = renderer, .art = art, .selected = LOTR_DATA_STANDARD};
    for (int i = 0; i < LOTR_DATA_COUNT; ++i)
        SDL_strlcpy(edition_paths[i].data, lotr_edition_at(i)->default_data_dir,
                    sizeof(edition_paths[i].data));
    char *preferences = SDL_GetPrefPath("Wonderland", "LotR");
    char settings[LOTR_PATH_CAPACITY];
    const char *settings_path = preferences &&
        selector_join(settings, sizeof(settings), preferences, "selector.xml") ? settings : NULL;
    SDL_free(preferences);
    selector_read_settings(settings_path, edition_paths, &selector.selected);
    select_card(&selector, selector.selected);

    int result = -1;
    while (result < 0) {
        SDL_Event event;
        draw_selector(&selector);
        if (!SDL_WaitEvent(&event))
            break;
        result = selector_event(&selector, &event);
    }
    if (result >= 0)
        selector_write_settings(settings_path, edition_paths, selector.selected);
    browser_clear(&selector.browser);
    SDL_StopTextInput();
    SDL_DestroyTexture(art);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
