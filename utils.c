/****************************************************************************

    utils.c
    various utilities
    (memory management, file manipulation, etc... )


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
#include "utils.h"
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#ifdef WIN32
#include <windows.h>
#include <wchar.h>
#endif

#define HOME_DIR_STR "$HOME/.lotr/"

char lotr_filename[LOTR_PATH_CAPACITY];

#if defined(WIN32)
#  define EDITION_ROOT PREFIX
#  define STANDARD_DIRECTORY EDITION_ROOT "/lotr"
#  define DEMO_DIRECTORY PREFIX "/demo"
#elif defined(AMIGA_OS4)
#  define EDITION_ROOT PREFIX "/lotr"
#  define STANDARD_DIRECTORY EDITION_ROOT
#  define DEMO_DIRECTORY PREFIX "/"
#else
#  define EDITION_ROOT PREFIX "/share/games/lotr"
#  define STANDARD_DIRECTORY EDITION_ROOT
#  define DEMO_DIRECTORY "./"
#endif

static const LotrEdition editions[] = {
    {"standard", "The Lord of the Rings, Vol. I - Standard DOS",
     STANDARD_DIRECTORY, LOTR_INTRO_CARTOON, LOTR_AUDIO_ARCHIVE, 1, LOTR_DATA_STANDARD},
    {"enhanced-cd", "The Lord of the Rings, Vol. I - Enhanced CD",
     EDITION_ROOT "/cd", LOTR_INTRO_CD, LOTR_AUDIO_CD, 1, LOTR_DATA_ENHANCED_CD},
    {"two-towers", "The Lord of the Rings, Vol. II - The Two Towers",
     EDITION_ROOT "/towers", LOTR_INTRO_CARTOON, LOTR_AUDIO_SILENT, 2, LOTR_DATA_TWO_TOWERS},
    {"demo", "The Lord of the Rings, Vol. I - Demo",
     DEMO_DIRECTORY, LOTR_INTRO_DEMO, LOTR_AUDIO_ARCHIVE, 1, LOTR_DATA_DEMO},
};

#ifdef DEMO
#  define DEFAULT_EDITION LOTR_DATA_DEMO
#else
#  define DEFAULT_EDITION LOTR_DATA_STANDARD
#endif

static const LotrEdition *edition_profile = &editions[DEFAULT_EDITION];
static const char *configured_data_dir;
static const char *configured_movie_dir;

const LotrEdition *
lotr_current_edition(void)
{
    return edition_profile;
}

const LotrEdition *
lotr_edition_at(LotrDataFormat format)
{
    return (unsigned int)format < LOTR_DATA_COUNT ? &editions[format] : NULL;
}

int
lotr_configure_edition(const char *edition, const char *data_dir,
                       const char *movie_dir)
{
    const LotrEdition *profile = &editions[DEFAULT_EDITION];
    if (edition) {
#ifdef DEMO
        int first = LOTR_DATA_DEMO, last = LOTR_DATA_DEMO;
#else
        int first = LOTR_DATA_STANDARD, last = LOTR_DATA_DEMO;
#endif
        profile = NULL;
        for (int i = first; i <= last; ++i)
            if (strcmp(edition, editions[i].key) == 0)
                profile = &editions[i];
        if (!profile) {
            fprintf(stderr, "lotr: unknown edition '%s'\n", edition);
            return 0;
        }
    }
    if ((data_dir && !*data_dir) || (movie_dir && !*movie_dir)) {
        fprintf(stderr, "lotr: asset folder paths cannot be empty\n");
        return 0;
    }
    if (movie_dir && profile->intro != LOTR_INTRO_CD) {
        fprintf(stderr, "lotr: --movie-dir requires the Enhanced CD edition\n");
        return 0;
    }
    edition_profile = profile;
    configured_data_dir = data_dir;
    configured_movie_dir = movie_dir;
    return 1;
}

char *
lotr_save_filename(LotrDataFormat format, int slot)
{
    char name[64];
    snprintf(name, sizeof(name), "savegame.%s.%d", lotr_edition_at(format)->key, slot);
    return lotr_homedir_filename(name);
}

char *
lotr_legacy_save_filename(int volume, int slot)
{
    char name[32];
    snprintf(name, sizeof(name), "%s.%d", volume == 2 ? "savegame2" : "savegame", slot);
    return lotr_homedir_filename(name);
}

static FILE *
open_asset_path(const char *path, const char *mode)
{
#ifdef WIN32
    wchar_t wide_path[LOTR_PATH_CAPACITY];
    wchar_t wide_mode[8];
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                            wide_path, LOTR_PATH_CAPACITY) ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, wide_mode, 8)) {
        errno = EINVAL;
        return NULL;
    }
    return _wfopen(wide_path, wide_mode);
#else
    return fopen(path, mode);
#endif
}

static FILE *
open_from_directory(const char *directory, const char *path, const char *mode)
{
    int length = snprintf(lotr_filename, sizeof(lotr_filename), "%s/%s",
                          directory, path);
    if (length < 0 || (size_t)length >= sizeof(lotr_filename)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    FILE *file = open_asset_path(lotr_filename, mode);
    if (file || errno != ENOENT || strcmp(mode, "rb") != 0)
        return file;

    /* DOS CD images commonly extract with uppercase filenames. */
    for (char *ch = lotr_filename + strlen(directory) + 1; *ch; ++ch)
        *ch = (char)toupper((unsigned char)*ch);
    file = open_asset_path(lotr_filename, mode);
    if (!file)
        snprintf(lotr_filename, sizeof(lotr_filename), "%s/%s", directory, path);
    return file;
}

/*
  allocates memory, exits on error
*/
void *
lotr_malloc(int size)
{
    void *result;
    result = malloc(size);

    if (result == NULL) {
        char message[128];
        snprintf(message, sizeof(message),
                 "lotr: can not allocate %d bytes of memory", size);
        perror(message);
        exit(1);
    }

    return result;

}




/*
  return full path to $HOME/.lotr/<name>
*/

extern char *
lotr_homedir_filename(const char *name)
{
    char cwd[1024];
#if !defined(AMIGA_OS4) && !defined(WIN32)
    char *home = getenv("HOME");

    if (home == NULL) {
        fprintf(stderr,
                "lotr: HOME variable not set (can not find users homedir)\n");
        exit(1);
    }

    snprintf(lotr_filename, sizeof(lotr_filename), "%s/.lotr/", home);
#else
    snprintf(lotr_filename, sizeof(lotr_filename), "saves/");
#endif
    if (getcwd(cwd, 1024) == NULL) {
        perror("cannot get current directory");
        exit(1);
    }
#ifndef WIN32 /* TODO : Do the check */
    if (chdir(lotr_filename)) { /* Somewhat stupid test for dir existence */
        if (mkdir(lotr_filename, S_IRWXU)) {
            perror("cannot create directory " HOME_DIR_STR);
            exit(1);
        }
    }
#endif
    if (chdir(cwd) < 0) {
        perror("cannot change directory");
        exit(1);
    }

#if !defined(AMIGA_OS4) && !defined(WIN32)
    snprintf(lotr_filename, sizeof(lotr_filename), "%s/.lotr/%s", home, name);
#else
    snprintf(lotr_filename, sizeof(lotr_filename), "saves/%s", name);
#endif

    return lotr_filename;

}



/*
  opens a file, exits on error
*/

FILE *
lotr_fopen(const char *path, const char *mode)
{
    FILE *result = NULL;
    if (!configured_data_dir || mode[0] != 'r' || strchr(mode, '+'))
        result = fopen(lotr_homedir_filename(path), mode);

    if (result != NULL)
        return result;

    if (mode[0] == 'w' ||
        (configured_data_dir && (mode[0] != 'r' || strchr(mode, '+')))) {
        perror("can not write to directory " HOME_DIR_STR);
        exit(1);
    }

    result = open_from_directory(lotr_data_directory(), path, mode);

    if (result == NULL) {
        char message[2048];
        snprintf(message, sizeof(message), "lotr: can not open file %s", lotr_filename);
        perror(message);
        exit(1);
    }

    return result;
}

FILE *
lotr_open_movie(const char *path)
{
    if (!configured_movie_dir)
        return lotr_fopen(path, "rb");
    FILE *file = open_from_directory(configured_movie_dir, path, "rb");
    if (!file) {
        perror(lotr_filename);
        exit(1);
    }
    return file;
}


/*
   check if file exists
*/

int
lotr_file_exists(const char *path)
{
    FILE *testfile = configured_data_dir ? NULL :
        fopen(lotr_homedir_filename(path), "rb");

    if (testfile != NULL) {
        fclose(testfile);
        return 1;
    }

    testfile = open_from_directory(lotr_data_directory(), path, "rb");
    if (testfile != NULL) {
        fclose(testfile);
        return 1;
    }

    return 0;
}

FILE *
lotr_try_open_asset(const char *path, int movie)
{
    FILE *file = NULL;
    if (movie && configured_movie_dir) {
        file = open_from_directory(configured_movie_dir, path, "rb");
    } else {
        if (!configured_data_dir)
            file = fopen(lotr_homedir_filename(path), "rb");
        if (!file)
            file = open_from_directory(lotr_data_directory(), path, "rb");
    }
    return file;
}

long
lotr_asset_size(const char *path, int movie)
{
    FILE *file = lotr_try_open_asset(path, movie);
    if (!file)
        return -1;
    long size = lotr_filelen(file);
    fclose(file);
    return size;
}



/*
  adds suffix to a filename
*/

char *
lotr_add_suffix(const char *name, const char *suffix)
{
    int len;
    char *result;

    len = strlen(name) + strlen(suffix) + 2;

    result = lotr_malloc(len);

    snprintf(result, len, "%s.%s", name, suffix);

    return result;

}




/*
  returns file length, -1 if file==NULL
*/

long
lotr_filelen(FILE *file)
{
    long currpos;               /* current position */
    long result;

    if (file == NULL)
        return -1;
    currpos = ftell(file);

    fseek(file, 0, SEEK_END);

    result = ftell(file);

    fseek(file, currpos, SEEK_SET);

    return result;

}


/*
  random number
*/

int
lotr_rnd(int n)
{
    int k;
    k = ((double)n) * rand() / (RAND_MAX + 1.0);
    if (k < 0)
        k = 0;
    ++k;
    if (k > n)
        k = n;
    return k;
}



/*
  add integer property node to a given xml node
*/

void
lotr_save_prop_int(xmlNodePtr node, char *name, int value)
{
    char buf[16];

    snprintf(buf, sizeof(buf), "%d", value);

    xmlNewTextChild(node, NULL, (const xmlChar *)name, (const xmlChar *)buf);
}


/*
  add field property node to a given xml node
*/

void
lotr_save_prop_field(xmlNodePtr node, char *name, Uint8 *value, int len)
{
    int i, d, e;
    char *buf;

    buf = lotr_malloc(2 * len + 1);

    for (i = 0; i < len; ++i) {
        d = value[i];
        e = d / 0x10;
        if (e < 10)
            buf[2 * i] = e + '0';
        else
            buf[2 * i] = e - 10 + 'a';

        e = d % 0x10;
        if (e < 10)
            buf[2 * i + 1] = e + '0';
        else
            buf[2 * i + 1] = e - 10 + 'a';
    }

    buf[2 * len] = 0;

    xmlNewTextChild(node, NULL, (const xmlChar *)name, (const xmlChar *)buf);

    free(buf);

}



/*
  gets node with a given name
 */

xmlNodePtr
lotr_get_subnode(xmlNodePtr node, const xmlChar *name, int force)
{
    xmlNodePtr cur = node->xmlChildrenNode;
    while (cur != NULL) {
        if (!xmlStrcmp(cur->name, name))
            break;
        cur = cur->next;
    }

    if (force && cur == NULL) {
        fprintf(stderr, "lotr: can not get node named %s\n", name);
        exit(1);
    }

    return cur;

}


/*
  read integer property from node
*/

int
lotr_load_prop_int(xmlNodePtr node, char *name)
{
    xmlNodePtr cur;
    cur = lotr_get_subnode(node, (const xmlChar *)name, 1);

    return atoi((char *)xmlNodeGetContent(cur));
}


/*
  read integer property from node, return default if no such node
*/

int
lotr_load_prop_int_default(xmlNodePtr node, char *name, int default_value)
{
    xmlNodePtr cur;
    cur = lotr_get_subnode(node, (const xmlChar *)name, 0);

    if (cur == NULL)
        return default_value;

    return atoi((char *)xmlNodeGetContent(cur));
}


/*
  read field property node from a given xml node
*/

int
lotr_load_prop_field(xmlNodePtr node, char *name, Uint8 *value, int maxlen)
{
    xmlChar *buf;
    int len;
    int i;
    int d, r;

    xmlNodePtr cur;
    cur = lotr_get_subnode(node, (const xmlChar *)name, 1);

    buf = xmlNodeGetContent(cur);
    len = xmlStrlen(buf) / 2;
    if (len > maxlen)
        len = maxlen;

    for (i = 0; i < len; ++i) {
        r = 0;
        d = buf[2 * i];
        if (d >= '0' && d <= '9')
            r = d - '0';
        if (d >= 'a' && d <= 'f')
            r = d - 'a' + 10;

        r = r * 0x10;

        d = buf[2 * i + 1];
        if (d >= '0' && d <= '9')
            r += d - '0';
        if (d >= 'a' && d <= 'f')
            r += d - 'a' + 10;

        value[i] = r;

    }

    return len;
}




/*
  return data directory
*/
const char *
lotr_data_directory(void)
{
    return configured_data_dir ? configured_data_dir : edition_profile->default_data_dir;
}
