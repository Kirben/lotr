/****************************************************************************

    archive.c
    provides access to idx'ed and ndx'ed data files


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
#include "archive.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>



/* buffer used for decompression */
#define BUFSIZE 200000
Uint8 buffer[BUFSIZE];

int
archive_index_matches(FILE *index, long data_size, int length_index)
{
    const int width = length_index ? 2 : 4;
    const long size = lotr_filelen(index);
    Uint32 end = 0;
    Uint8 entry[4];

    if (data_size <= 0 || (Uint64)data_size > SDL_MAX_SINT32 ||
        size < (length_index ? width : 2 * width) || size % width != 0)
        return 0;
    for (long i = 0; i < size; i += width) {
        if (fread(entry, 1, width, index) != (size_t)width)
            return 0;
        Uint32 value = entry[0] | (Uint32)entry[1] << 8;
        if (length_index)
            value += end;
        else
            value |= (Uint32)entry[2] << 16 | (Uint32)entry[3] << 24;
        if ((!length_index && i == 0 && value != 0) ||
            value < end || value > (Uint32)data_size)
            return 0;
        end = value;
    }
    return !length_index || end == (Uint32)data_size;
}

enum {
    STANDARD = 1 << LOTR_DATA_STANDARD,
    CD = 1 << LOTR_DATA_ENHANCED_CD,
    TOWERS = 1 << LOTR_DATA_TWO_TOWERS,
    DEMO_DATA = 1 << LOTR_DATA_DEMO,
    FULL_GAMES = STANDARD | CD | TOWERS,
    ALL_EDITIONS = FULL_GAMES | DEMO_DATA
};

typedef struct {
    const char *name;
    const char *index_name;
    unsigned editions;
    int min_entries;
    int exact_entries;
    int length_index;
    int map_bank;
} ArchiveRequirement;

static const ArchiveRequirement archive_requirements[] = {
    {"arts",     "arts.ndx",     FULL_GAMES},
    {"arts",     "arts.ndx",     DEMO_DATA, 16, 1},
    {"map0",     "map0.ndx",     FULL_GAMES, 16, 0, 0, 1},
    {"map1",     "map1.ndx",     STANDARD | CD, 96, 0, 0, 2},
    {"map1",     "map1.ndx",     TOWERS, 112, 0, 0, 2},
    {"demo",     "demo.ndx",     DEMO_DATA, 8, 1, 0, 1},
    {"shapes",   "shapes.ndx",   ALL_EDITIONS, 700, 1},
    {"portrait", "portrait.ndx", STANDARD | CD | DEMO_DATA, 46, 1},
    {"portrait", "portrait.ndx", TOWERS, 67, 1},
    {"nnpcs",    "npcs.ndx",     FULL_GAMES, 240, 1},
    {"npcs",     "npcs.ndx",     DEMO_DATA, 240, 1},
    {"snd",      "snd.ndx",      STANDARD},
    {"snd",      "snd.ndx",      DEMO_DATA, 27, 1},
    {"opening",  "opening.idx", STANDARD | TOWERS, 0, 0, 1},
    {"democrt0", "democrt0.idx", DEMO_DATA, 0, 0, 1},
    {"democrt1", "democrt1.idx", DEMO_DATA, 0, 0, 1},
    {"democrt2", "democrt2.idx", DEMO_DATA, 0, 0, 1},
    {"building", "building.ndx", TOWERS},
    {"buildpal", "buildpal.ndx", TOWERS},
    {"icon",     "icon.ndx",     TOWERS}
};

static int
validate_requirement(const ArchiveRequirement *rule, char *message, size_t capacity)
{
    char data[32];
    snprintf(data, sizeof(data), "%s.dat", rule->name);
    long data_size = lotr_asset_size(data, 0);
    FILE *index = lotr_try_open_asset(rule->index_name, 0);
    if (data_size <= 0 || !index) {
        snprintf(message, capacity, "Game data folder needs %s.", data_size <= 0 ? data : rule->index_name);
        if (index)
            fclose(index);
        return 0;
    }
    int width = rule->length_index ? 2 : 4;
    long entries = lotr_filelen(index) / width;
    int valid = entries >= rule->min_entries &&
        (!rule->exact_entries || entries == rule->min_entries) &&
        archive_index_matches(index, data_size, rule->length_index);
    fclose(index);
    if (!valid)
        snprintf(message, capacity, "Invalid archive pair for this edition: %s and %s.", data, rule->index_name);
    return valid;
}

int
archive_validate_edition(char *message, size_t capacity)
{
    unsigned edition = 1u << lotr_current_edition()->data_format;
    for (size_t i = 0; i < sizeof(archive_requirements) / sizeof(archive_requirements[0]); ++i) {
        const ArchiveRequirement *rule = &archive_requirements[i];
        if ((rule->editions & edition) && !validate_requirement(rule, message, capacity))
            return 0;
    }
    return 1;
}

Archive *
archive_open_map(int resource)
{
    unsigned edition = 1u << lotr_current_edition()->data_format;
    int bank = (resource >> 8) + 1;
    for (size_t i = 0; i < sizeof(archive_requirements) / sizeof(archive_requirements[0]); ++i) {
        const ArchiveRequirement *rule = &archive_requirements[i];
        if ((rule->editions & edition) && rule->map_bank == bank) {
            char message[128];
            if (validate_requirement(rule, message, sizeof(message)))
                return archive_ndx_open(rule->name);
            fprintf(stderr, "lotr: %s\n", message);
            exit(1);
        }
    }
    fprintf(stderr, "lotr: no map archive for resource %x.\n", resource);
    exit(1);
}

/*
  opens idx'ed archive file
*/

Archive *
archive_idx_open(const char *name)
{
    FILE *datafile;
    FILE *idxfile;
    char *fullname;             /* name + suffix */
    Archive *result;
    int i, k, n;
    Uint8 tmpbytes[2];

    fullname = lotr_add_suffix(name, "dat");
    datafile = lotr_fopen(fullname, "rb");
    free(fullname);

    fullname = lotr_add_suffix(name, "idx");
    idxfile = lotr_fopen(fullname, "rb");
    free(fullname);

    result = (Archive *)lotr_malloc(sizeof(Archive));

    n = lotr_filelen(idxfile);

    result->datafile = datafile;

    n = (n + 1) / 2;
    result->size = n;

    result->index = lotr_malloc((n + 1) * sizeof(int));

    result->index[0] = 0;

    for (i = 0; i < n; ++i) {
        tmpbytes[0] = fgetc(idxfile);
        tmpbytes[1] = fgetc(idxfile);

        k = tmpbytes[1] * 0x100 + tmpbytes[0];

        result->index[i + 1] = result->index[i] + k;
    }

    fclose(idxfile);

    if (result->index[n] != lotr_filelen(datafile)) {
        fprintf(stderr,
                "lotr: %s seems not to be a valid idx'ed data file\n", name);
        exit(1);
    }

    return result;

}





/*
  opens ndx'ed archive file
*/

Archive *
archive_ndx_open(const char *name)
{
    FILE *datafile;
    FILE *ndxfile;
    char *fullname;             /* name + suffix */
    Archive *result;
    int i, k, n;
    Uint8 tmpbytes[4];

    fullname = lotr_add_suffix(name, "dat");
    datafile = lotr_fopen(fullname, "rb");
    free(fullname);

    /* ndx file from nnpcs is npcs.ndx */
    if (strcmp(name, "nnpcs") == 0)
        name++;

    fullname = lotr_add_suffix(name, "ndx");
    ndxfile = lotr_fopen(fullname, "rb");
    free(fullname);

    result = (Archive *)lotr_malloc(sizeof(Archive));

    n = lotr_filelen(ndxfile);

    result->datafile = datafile;

    n = (n + 3) / 4;
    result->size = n;

    result->index = lotr_malloc((n + 1) * sizeof(int));

    result->index[0] = 0;

    if (n < 2 || fgetc(ndxfile) != 0
        || fgetc(ndxfile) != 0
        || fgetc(ndxfile) != 0 || fgetc(ndxfile) != 0) {
        fprintf(stderr,
                "lotr: %s: expecting four zeros at the beginning of an ndx file\n",
                name);
        exit(1);
    }

    for (i = 1; i < n; ++i) {
        tmpbytes[0] = fgetc(ndxfile);
        tmpbytes[1] = fgetc(ndxfile);
        tmpbytes[2] = fgetc(ndxfile);
        tmpbytes[3] = fgetc(ndxfile);

        k = ((tmpbytes[3] * 0x100 + tmpbytes[2]) * 0x100 +
             tmpbytes[1]) * 0x100 + tmpbytes[0];


        result->index[i] = k;
        if (result->index[i - 1] > k) {
            fprintf(stderr,
                    "lotr: %s seems not to be a valid ndx'ed data file\n",
                    name);
            exit(1);
        }

    }

    fclose(ndxfile);


    result->index[n] = lotr_filelen(datafile);

    if (result->index[n - 1] > result->index[n]) {
        fprintf(stderr,
                "lotr: %s seems not to be a valid ndx'ed data file\n", name);
        exit(1);
    }



    return result;

}





/*
  close archive file
*/

void
archive_close(Archive *archive)
{
    fclose(archive->datafile);
    free(archive->index);
    free(archive);
}




/*
  reads data from the archive
*/

Uint8 *
archive_read(Archive *archive, int index)
{
    Uint8 *result;
    int size;

    size = archive_data_size(archive, index);

    if (size == 0)
        return NULL;

    result = lotr_malloc(size);

    fseek(archive->datafile, archive->index[index], SEEK_SET);

    if (!fread(result, size, 1, archive->datafile)) {
        fprintf(stderr, "lotr: can not read datafile\n");
        exit(1);
    }

    return result;
}


/*
  return archive data size
*/
int
archive_data_size(Archive *archive, int index)
{
    if (index < 0 || index >= archive->size)
        return 0;
    return archive->index[index + 1] - archive->index[index];
}



/*
  reads n-th bit from data
 */

INLINE int
readbit(Uint8 *data, int pos)
{
    Uint8 c = data[pos / 8];

    switch (pos % 8) {
        case 0:
            return (c >> 7) & 1;
        case 1:
            return (c >> 6) & 1;
        case 2:
            return (c >> 5) & 1;
        case 3:
            return (c >> 4) & 1;
        case 4:
            return (c >> 3) & 1;
        case 5:
            return (c >> 2) & 1;
        case 6:
            return (c >> 1) & 1;
        case 7:
            return (c >> 0) & 1;
    }

    return 0;                   /* should not happen, only to avoid warnings */
}


/*
  reads n bits from data
 */

INLINE int
readnbits(Uint8 *data, int n, int pos)
{
    Uint8 result = 0;
    int i;

    for (i = 0; i < n; ++i)
        result = result * 2 + readbit(data, pos + i);

    return result;
}





/*
  decompress idx data file compression
*/

Uint8 *
decompress_idx(Uint8 *data, int size, int *resultsize)
{

    Uint8 *index;
    int indexsize;
    int i;
    int datapos, bufpos;
    int codesize;
    int runlen;
    Uint8 code;
    Uint8 *result;

    if (data == NULL)
        return NULL;

    indexsize = data[0];
    index = data + 1;
    data += 1 + indexsize;

    /* now size will be the size of data in bits */
    size -= 1 + indexsize;
    size *= 8;


    /* compute the size of a single code */
    /* number af bits needed but at least 2 */

    i = indexsize;
    codesize = 0;
    while (i) {
        ++codesize;
        i = i / 2;
    }
    if (codesize < 2)
        codesize = 2;

    datapos = 0;
    bufpos = 0;


    /* idx file is coded in RLE (or some variant) */

    while (datapos + 8 <= size) {
        runlen = readnbits(data, 8, datapos);
        datapos += 8;

        /* 0 terminates the data stream */
        if (runlen == 0)
            break;

        if (runlen & 0x80) {    /* there is a repeating code */

            runlen &= 0x7f;
            code = readnbits(data, codesize, datapos);
            datapos += codesize;

            for (i = 0; i < runlen; ++i)
                buffer[bufpos++] = index[code];

        } else {                /* if( runlen&0x80 ) */

            /* copy runlen codes */
            for (i = 0; i < runlen; ++i) {
                code = readnbits(data, codesize, datapos);
                datapos += codesize;
                buffer[bufpos++] = index[code];
            }
        }
    }                           /* while(datapos+8<=n*8) */

    *resultsize = bufpos;
    result = lotr_malloc(*resultsize);
    memcpy(result, buffer, *resultsize);

    return result;

}


/*
  reads decompressed data from the archive
*/

Uint8 *
decompress_idxarchive(Archive *archive, int index, int *size)
{
    Uint8 *result;
    Uint8 *data;

    result = NULL;
    data = archive_read(archive, index);

    if (data != NULL) {
        result =
            decompress_idx(data, archive_data_size(archive, index), size);
        free(data);
    }

    if (result == NULL) {
        fprintf(stderr, "lotr: archive decompression failed\n");
        exit(1);
    }

    return result;

}






/*
  decompress ndx data file compression
*/

Uint8 *
decompress_ndx(Uint8 *data, int size, int *resultsize)
{
    Uint8 *result;
    int bufpos;
    int datapos;
    int n;
    int pattern = 0;
    int i, j;
    int start, end, len;

    bufpos = datapos = 0;

    n = 0;
    while (datapos < size) {

        if (n % 9 == 0) {
            /* every 9-th byte is a pattern for next 8 values */
            pattern = data[datapos++];
        } else {
            if (pattern & 1) {
                /* if the pattern bit == 1 just copy the next byte */
                buffer[bufpos++] = data[datapos++];
            } else {
                /* if the pattern bit == 0 we copy some substring of decoded data */

                i = (Uint8)(data[datapos++]);
                if (!(datapos < size))
                    break;
                j = (Uint8)(data[datapos++]);


                /* next 12 bits are starting position */
                start = (Uint8)(i) + 16 * (Uint8)(j & 0xf0);
                /* we add 0x12 (it is part of ndx format) */
                start = (start + 0x12) & 0xfff;

                /* next 4 bits are length-3 */
                len = (Uint8)(j & 0x0f) + 3;


                if (start > bufpos) {
                    /* it is unclear what to do in this case
                       fprintf( stderr, "lotr: wrongly decoded ndx archive %03x, %03x, %x\n", start, bufpos, len ); */

                    /* this works */

                    start -= 0x1000;
                    while (start < 0 && len > 0) {
                        buffer[bufpos++] = 0x20;
                        ++start;
                        --len;
                    }
                }

                /* 12 bits can not encode too much:
                   the real starting position
                   is in the last segment of size 0x1000 */

                if (start < (bufpos % 0x1000))
                    start += (bufpos / 0x1000) * 0x1000;
                else if (bufpos > 0xfff)
                    start += ((bufpos / 0x1000) - 1) * 0x1000;

                end = start + len;

                for (i = start; i < end; ++i)
                    buffer[bufpos++] = buffer[i];

            }
            pattern = pattern >> 1;

        }                       /* if( datapos%9==0 ) */

        ++n;

    }


    *resultsize = bufpos;
    result = lotr_malloc(*resultsize);
    memcpy(result, buffer, *resultsize);

    return result;

}


/*
  reads decompressed data from the archive
*/

Uint8 *
decompress_ndxarchive(Archive *archive, int index, int *size)
{
    Uint8 *result;
    Uint8 *data;

    result = NULL;
    data = archive_read(archive, index);

    if (data != NULL) {
        result =
            decompress_ndx(data, archive_data_size(archive, index), size);
        free(data);
    }

    if (result == NULL) {
        fprintf(stderr, "lotr: archive decompression failed index=%d\n",
                index);
        exit(1);
    }

    return result;

}
