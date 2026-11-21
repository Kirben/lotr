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
} EditionPaths;

typedef struct {
    char path[LOTR_PATH_CAPACITY];
    char **entries;
    int count;
    int selected;
    int first;
    int open;
    int movies;
} DirectoryBrowser;

static int
selector_join(char *result, size_t capacity, const char *directory, const char *name)
{
    size_t length = strlen(directory);
    const char *separator = length && directory[length - 1] == '/' ? "" : "/";
    int written = snprintf(result, capacity, "%s%s%s", directory, separator, name);
    return written >= 0 && (size_t)written < capacity;
}

static void
selector_read_settings(const char *path, EditionPaths paths[], int *selected)
{
    if (!path)
        return;
    xmlDocPtr doc = xmlReadFile(path, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc)
        return;
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

static void
selector_write_settings(const char *path, EditionPaths paths[], int selected)
{
    char temporary[LOTR_PATH_CAPACITY];
    if (!path)
        return;
#ifdef WIN32
    unsigned long process = GetCurrentProcessId();
#else
    unsigned long process = (unsigned long)getpid();
#endif
    int length = snprintf(temporary, sizeof(temporary), "%s.%lu.tmp", path, process);
    if (length < 0 || (size_t)length >= sizeof(temporary))
        return;
    xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
    if (!doc)
        return;
    xmlNodePtr root = xmlNewNode(NULL, BAD_CAST "wonderland");
    if (root) {
        xmlDocSetRootElement(doc, root);
        xmlNewProp(root, BAD_CAST "selected", BAD_CAST lotr_edition_at(selected)->key);
        for (int i = 0; i < LOTR_DATA_COUNT; ++i) {
            xmlNodePtr entry = xmlNewChild(root, NULL, BAD_CAST lotr_edition_at(i)->key, NULL);
            if (!entry)
                continue;
            xmlNewProp(entry, BAD_CAST "data", BAD_CAST paths[i].data);
            if (i == LOTR_DATA_ENHANCED_CD)
                xmlNewProp(entry, BAD_CAST "movies", BAD_CAST paths[i].movies);
        }
        if (xmlSaveFormatFileEnc(temporary, doc, "UTF-8", 1) < 0 ||
            !selector_replace_file(temporary, path))
            fprintf(stderr, "lotr: could not save selector settings in %s\n", path);
    }
    xmlFreeDoc(doc);
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
