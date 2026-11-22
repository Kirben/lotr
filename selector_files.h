#ifndef LOTR_SELECTOR_FILES_H
#define LOTR_SELECTOR_FILES_H

#ifdef WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    char data[LOTR_PATH_CAPACITY];
    char movies[LOTR_PATH_CAPACITY];
    int music_off;
    char profile[16];
    char port[32];
} EditionPaths;

typedef struct {
    char path[LOTR_PATH_CAPACITY];
    char **entries;
    int count;
    int selected;
    int first;
    int open;
    int movies;
#ifdef WIN32
    char drives[26];
    int drive_count;
    int drive_selected;
    int drive_first;
    int drive_open;
#endif
} DirectoryBrowser;

static int
selector_join(char *result, size_t capacity, const char *directory, const char *name)
{
    size_t length = strlen(directory);
    const char *separator = length && directory[length - 1] == '/' ? "" : "/";
    int written = snprintf(result, capacity, "%s%s%s", directory, separator, name);
    return written >= 0 && (size_t)written < capacity;
}

static int
selector_read_legacy_settings(const char *path, EditionPaths paths[], int *selected)
{
    if (!path)
        return 0;
    xmlDocPtr doc = xmlReadFile(path, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc)
        return 0;
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (root && xmlStrEqual(root->name, BAD_CAST "wonderland")) {
        xmlChar *last = xmlGetProp(root, BAD_CAST "selected");
        for (int i = 0; i < LOTR_DATA_COUNT; ++i) {
            const char *key = lotr_edition_at(i)->key;
            if (last && xmlStrEqual(last, BAD_CAST key))
                *selected = i;
            for (xmlNodePtr entry = root->children; entry; entry = entry->next) {
                if (entry->type != XML_ELEMENT_NODE || !xmlStrEqual(entry->name, BAD_CAST key))
                    continue;
                xmlChar *data = xmlGetProp(entry, BAD_CAST "data");
                xmlChar *movies = xmlGetProp(entry, BAD_CAST "movies");
                if (data && xmlStrlen(data) < LOTR_PATH_CAPACITY)
                    SDL_strlcpy(paths[i].data, (const char *)data, sizeof(paths[i].data));
                if (movies && xmlStrlen(movies) < LOTR_PATH_CAPACITY)
                    SDL_strlcpy(paths[i].movies, (const char *)movies, sizeof(paths[i].movies));
                xmlFree(data);
                xmlFree(movies);
                break;
            }
        }
        xmlFree(last);
    }
    xmlFreeDoc(doc);
    return 1;
}

#ifdef WIN32
static int
selector_wide_path(const char *path, wchar_t wide[LOTR_PATH_CAPACITY])
{
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                                    wide, LOTR_PATH_CAPACITY);
    for (int i = 0; i < length; ++i)
        if (wide[i] == L'/')
            wide[i] = L'\\';
    return length != 0;
}
#endif

static int
selector_replace_file(const char *temporary, const char *path)
{
#ifdef WIN32
    wchar_t source[LOTR_PATH_CAPACITY], destination[LOTR_PATH_CAPACITY];
    return selector_wide_path(temporary, source) && selector_wide_path(path, destination) &&
        MoveFileExW(source, destination, MOVEFILE_REPLACE_EXISTING);
#else
    return rename(temporary, path) == 0;
#endif
}

static FILE *
selector_open_settings(const char *path, int writing)
{
#ifdef WIN32
    wchar_t wide[LOTR_PATH_CAPACITY];
    return selector_wide_path(path, wide) ? _wfopen(wide, writing ? L"wb" : L"rb") : NULL;
#else
    return fopen(path, writing ? "wb" : "rb");
#endif
}

static int
selector_read_settings(const char *path, EditionPaths paths[], int *selected)
{
    FILE *file = path ? selector_open_settings(path, 0) : NULL;
    if (!file)
        return 0;
    char line[LOTR_PATH_CAPACITY + 32];
    while (fgets(line, sizeof(line), file)) {
        size_t end = strcspn(line, "\r\n");
        if (!line[end] && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != '\n' && c != EOF)
                ;
            continue;
        }
        line[end] = 0;
        char *value = strchr(line, '=');
        if (!value)
            continue;
        *value++ = 0;
        if (!strcmp(line, "selected")) {
            for (int i = 0; i < LOTR_DATA_COUNT; ++i)
                if (!strcmp(value, lotr_edition_at(i)->key))
                    *selected = i;
            continue;
        }
        char *key = strchr(line, '.');
        if (!key)
            continue;
        *key++ = 0;
        for (int i = 0; i < LOTR_DATA_COUNT; ++i) {
            if (strcmp(line, lotr_edition_at(i)->key))
                continue;
            EditionPaths *entry = &paths[i];
            if (!strcmp(key, "data") && strlen(value) < sizeof(entry->data))
                SDL_strlcpy(entry->data, value, sizeof(entry->data));
            else if (!strcmp(key, "movies") && strlen(value) < sizeof(entry->movies))
                SDL_strlcpy(entry->movies, value, sizeof(entry->movies));
            else if (!strcmp(key, "music"))
                entry->music_off = !strcmp(value, "off");
            else if (!strcmp(key, "roland") &&
                     (!strcmp(value, "off") || !strcmp(value, "lapc-i") || !strcmp(value, "mt-32")))
                SDL_strlcpy(entry->profile, value, sizeof(entry->profile));
            else if (!strcmp(key, "port") && strlen(value) < sizeof(entry->port))
                SDL_strlcpy(entry->port, value, sizeof(entry->port));
        }
    }
    fclose(file);
    return 1;
}

static int
selector_write_settings(const char *path, const EditionPaths paths[], int selected)
{
    char temporary[LOTR_PATH_CAPACITY];
    if (!path)
        return 0;
#ifdef WIN32
    unsigned long process = GetCurrentProcessId();
#else
    unsigned long process = (unsigned long)getpid();
#endif
    int length = snprintf(temporary, sizeof(temporary), "%s.%lu.tmp", path, process);
    if (length < 0 || (size_t)length >= sizeof(temporary))
        return 0;
    FILE *file = selector_open_settings(temporary, 1);
    if (!file)
        return 0;
    fprintf(file, "selected=%s\n", lotr_edition_at(selected)->key);
    for (int i = 0; i < LOTR_DATA_COUNT; ++i) {
        const char *key = lotr_edition_at(i)->key;
        fprintf(file, "%s.data=%s\n%s.music=%s\n", key, paths[i].data,
                key, paths[i].music_off ? "off" : "on");
        if (i == LOTR_DATA_ENHANCED_CD)
            fprintf(file, "%s.movies=%s\n", key, paths[i].movies);
        if (i == LOTR_DATA_STANDARD || i == LOTR_DATA_TWO_TOWERS)
            fprintf(file, "%s.roland=%s\n%s.port=%s\n", key, paths[i].profile, key, paths[i].port);
    }
    int ok = !ferror(file);
    if (fclose(file))
        ok = 0;
    if (ok && selector_replace_file(temporary, path))
        return 1;
#ifdef WIN32
    wchar_t wide[LOTR_PATH_CAPACITY];
    if (selector_wide_path(temporary, wide))
        _wremove(wide);
#else
    remove(temporary);
#endif
    return 0;
}

static void
browser_clear(DirectoryBrowser *browser)
{
    for (int i = 0; i < browser->count; ++i)
        SDL_free(browser->entries[i]);
    SDL_free(browser->entries);
    browser->entries = NULL;
    browser->count = browser->first = browser->selected = 0;
}

static void
browser_add(DirectoryBrowser *browser, const char *name)
{
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return;
    char *copy = SDL_strdup(name);
    if (!copy)
        return;
    char **entries = SDL_realloc(browser->entries, (browser->count + 1) * sizeof(*entries));
    if (!entries) {
        SDL_free(copy);
        return;
    }
    browser->entries = entries;
    browser->entries[browser->count++] = copy;
}

static int
browser_compare(const void *left, const void *right)
{
    return SDL_strcasecmp(*(const char *const *)left, *(const char *const *)right);
}

static int
browser_read(DirectoryBrowser *browser, const char *path)
{
    char absolute[LOTR_PATH_CAPACITY];
#ifdef WIN32
    wchar_t input[LOTR_PATH_CAPACITY], full[LOTR_PATH_CAPACITY];
    if (!selector_wide_path(path, input))
        return 0;
    DWORD length = GetFullPathNameW(input, LOTR_PATH_CAPACITY, full, NULL);
    if (!length || length >= LOTR_PATH_CAPACITY ||
        !WideCharToMultiByte(CP_UTF8, 0, full, -1, absolute, sizeof(absolute), NULL, NULL))
        return 0;
    DWORD attributes = GetFileAttributesW(full);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return 0;
    for (char *p = absolute; *p; ++p)
        if (*p == '\\')
            *p = '/';
    char pattern[LOTR_PATH_CAPACITY];
    if (!selector_join(pattern, sizeof(pattern), absolute, "*") ||
        !selector_wide_path(pattern, input))
        return 0;
    browser_clear(browser);
    WIN32_FIND_DATAW record;
    HANDLE directory = FindFirstFileW(input, &record);
    if (directory != INVALID_HANDLE_VALUE) {
        do {
            char name[LOTR_PATH_CAPACITY];
            if ((record.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                WideCharToMultiByte(CP_UTF8, 0, record.cFileName, -1, name, sizeof(name), NULL, NULL))
                browser_add(browser, name);
        } while (FindNextFileW(directory, &record));
        FindClose(directory);
    }
#else
    char *full = realpath(path, NULL);
    if (!full)
        return 0;
    int fits = SDL_strlcpy(absolute, full, sizeof(absolute)) < sizeof(absolute);
    free(full);
    if (!fits)
        return 0;
    DIR *directory = opendir(absolute);
    if (!directory)
        return 0;
    browser_clear(browser);
    for (struct dirent *entry = readdir(directory); entry; entry = readdir(directory)) {
        char child[LOTR_PATH_CAPACITY];
        struct stat info;
        if (selector_join(child, sizeof(child), absolute, entry->d_name) &&
            stat(child, &info) == 0 && S_ISDIR(info.st_mode))
            browser_add(browser, entry->d_name);
    }
    closedir(directory);
#endif
    SDL_strlcpy(browser->path, absolute, sizeof(browser->path));
    if (browser->count > 1)
        qsort(browser->entries, browser->count, sizeof(*browser->entries), browser_compare);
    return 1;
}

#endif
