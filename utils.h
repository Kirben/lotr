/****************************************************************************

    utils.h
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


#ifndef _UTILS_H
#define _UTILS_H

#include <stdio.h>
#include <libxml/parser.h>

#define LOTR_PATH_CAPACITY 1024

typedef enum {
    LOTR_INTRO_CARTOON,
    LOTR_INTRO_CD,
    LOTR_INTRO_DEMO
} LotrIntro;

typedef enum {
    LOTR_AUDIO_ARCHIVE,
    LOTR_AUDIO_CD,
    LOTR_AUDIO_TOWERS
} LotrAudio;

typedef enum {
    LOTR_DATA_STANDARD,
    LOTR_DATA_ENHANCED_CD,
    LOTR_DATA_TWO_TOWERS,
    LOTR_DATA_DEMO,
    LOTR_DATA_COUNT
} LotrDataFormat;

typedef struct {
    const char *key;
    const char *title;
    const char *default_data_dir;
    LotrIntro intro;
    LotrAudio audio;
    int volume;
    LotrDataFormat data_format;
} LotrEdition;

extern const LotrEdition *lotr_current_edition(void);
extern const LotrEdition *lotr_edition_at(LotrDataFormat format);
/* Startup arguments must remain valid for the lifetime of the game. */
extern int lotr_configure_edition(const char *edition, const char *data_dir,
                                  const char *movie_dir);
extern FILE *lotr_open_movie(const char *path);
extern long lotr_asset_size(const char *path, int movie);
extern FILE *lotr_try_open_asset(const char *path, int movie);

#ifndef min
#  define min(a,b)  ((a) > (b) ? (b) : (a))
#endif
#ifndef max
#  define max(a,b)  ((a) > (b) ? (a) : (b))
#endif

/* allocates memory, exits on error */
extern void *lotr_malloc(int size);

/* opens a file, exits on error */
extern FILE *lotr_fopen(const char *path, const char *mode);

/* check if file exists */
extern int lotr_file_exists(const char *path);

/* return full path to $HOME/.lotr/<name> */
extern char *lotr_homedir_filename(const char *name);
extern char *lotr_save_filename(LotrDataFormat format, int slot);
extern char *lotr_legacy_save_filename(int volume, int slot);

/* adds suffix to a filename */
extern char *lotr_add_suffix(const char *name, const char *suffix);

/* returns file length, -1 if file==NULL */
extern long lotr_filelen(FILE *file);

/* random number */
extern int lotr_rnd(int n);

/* add integer property node to a given xml node */
extern void lotr_save_prop_int(xmlNodePtr node, char *name, int value);

/* add field property node to a given xml node */
extern void lotr_save_prop_field(xmlNodePtr node, char *name, Uint8 *value,
                                 int len);

/* read integer property from node */
extern int lotr_load_prop_int(xmlNodePtr node, char *name);

/* read integer property from node, return default if no such node */
extern int lotr_load_prop_int_default(xmlNodePtr node, char *name,
                                      int default_value);

/* read field property node from a given xml node */
extern int lotr_load_prop_field(xmlNodePtr node, char *name, Uint8 *value,
                                int maxlen);


/* gets node with a given name */
extern xmlNodePtr lotr_get_subnode(xmlNodePtr node, const xmlChar *name,
                                   int force);

/* return data directory */
extern const char *lotr_data_directory(void);


#if defined(AMIGA_OS4) || defined(WIN32)
#  define random() rand()
#endif


#endif /* _UTILS_H */
