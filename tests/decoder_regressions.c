#include "lotr.h"
#include "archive.h"
#include "character.h"
#include "game.h"
#include "map.h"
#include "object.h"
#include "spot.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int map_desc[MAP_WIDTH][MAP_HEIGHT];

enum { NPC_BYTES = 256, MAP_BYTES = MAP_WIDTH * MAP_HEIGHT * 2 };

static int check(int condition, const char *label)
{
    if (!condition)
        fprintf(stderr, "decoder regression failed: %s\n", label);
    return condition;
}

static unsigned little_word(const Uint8 *bytes)
{
    return bytes[0] | bytes[1] << 8;
}

static int script_offset(void)
{
    return lotr_current_edition()->data_format == LOTR_DATA_STANDARD ? 0x100 : 0x300;
}

/* Encode one NDX entry as groups of eight literals. Test the real reader. */
static int write_literals(FILE *file, const Uint8 *data, size_t length)
{
    for (size_t i = 0; i < length; i += 8) {
        size_t count = length - i < 8 ? length - i : 8;
        if (fputc(0xff, file) == EOF || fwrite(data + i, 1, count, file) != count)
            return 0;
    }
    return 1;
}

static int map_words_match(const Uint8 *bytes, int size)
{
    if (size != MAP_BYTES)
        return 0;
    for (int y = 0; y < MAP_HEIGHT; ++y)
        for (int x = 0; x < MAP_WIDTH; ++x)
            if (map_desc[x][y] != little_word(bytes + 2 * (y * MAP_WIDTH + x)))
                return 0;
    return 1;
}

static int ndx_back_reference(void)
{
    /* The fourth token copies the initial three bytes. */
    Uint8 encoded[] = {0x17, 'A', 'B', 'C', 0xee, 0xf0, '!'};
    int size;
    Uint8 *decoded = decompress_ndx(encoded, sizeof(encoded), &size);
    int matches = size == 7 && !memcmp(decoded, "ABCABC!", 7);
    free(decoded);
    return check(matches, "NDX literal and back-reference token order");
}

static int synthetic_maps_and_scripts(void)
{
    int offset = script_offset();
    int towers = lotr_current_edition()->data_format == LOTR_DATA_TWO_TOWERS;
    static const Uint8 volume_one[] = {0x00, 0xa1, 0x3e, 9, 1, 0x35, 17, 0x24};
    static const Uint8 volume_two[] = {0x00, 0xa1, 1, 0x3e, 9, 0x48, 1, 0x35, 17, 0x24};
    const Uint8 *script = towers ? volume_two : volume_one;
    size_t script_size = towers ? sizeof(volume_two) : sizeof(volume_one);
    static const int starts_one[] = {5, 7, 10, 12};
    static const int starts_two[] = {5, 8, 10, 12, 14};
    const int *starts = towers ? starts_two : starts_one;
    size_t commands = towers ? SDL_arraysize(starts_two) : SDL_arraysize(starts_one);

    Uint8 map[MAP_BYTES];
    for (int i = 0; i < MAP_BYTES; i += 2) {
        map[i] = (i / 2) ^ 0x5a;
        map[i + 1] = (i / 128) + 1;
    }
    Uint8 spots[0x340] = {1};
    spots[0x80] = 0x81;
    spots[0x90] = 0x34;
    spots[0xff] = 0x7e;
    /* The other edition's offset contains a terminator, not our record. */
    int other = offset == 0x100 ? 0x300 : 0x100;
    spots[other] = spots[other + 1] = 0xff;
    spots[offset] = 5 + script_size;
    spots[offset + 2] = 5;
    spots[offset + 4] = 3;
    memcpy(spots + offset + 5, script, script_size);
    spots[offset + 5 + script_size] = spots[offset + 6 + script_size] = 0xff;

    FILE *file = tmpfile();
    if (!check(file != NULL, "temporary map archive opened"))
        return 0;
    int index[] = {0, 0, 0};
    int written = write_literals(file, map, sizeof(map));
    index[1] = ftell(file);
    written = written && write_literals(file, spots, sizeof(spots));
    index[2] = ftell(file);
    if (!check(written, "map archive entries written")) {
        fclose(file);
        return 0;
    }
    Archive archive = {file, index, 2};
    map_set_map(&archive, 3, 0);
    map_set_spots(&archive, 1);
    fclose(file);

    if (!check(map_get_id() == 3 && map_words_match(map, sizeof(map)),
               "map dimensions, row/column order and little-endian tile words") ||
        !check(map_get_num_spots() == 1 && map_get_register(0) && map_get_register(7) &&
               !map_get_register(1) && map_get_objects()[0] == 0x34 &&
               map_get_objects()[0x6f] == 0x7e, "script prefix registers and object stock"))
        return 0;
    CommandSpot *spot = map_get_spot_number(0);
    return check(spot->map == 3 && spot->flag == 1 && spot->label_start == offset &&
                 !spot->not_parsed && spot->commands_num == (int)commands &&
                 !memcmp(spot->command_start, starts, commands * sizeof(*starts)),
                 "edition script offset, recruit and silver operand lengths, and following commands");
}

static int write_npc_fixture(void)
{
    FILE *records = fopen(lotr_homedir_filename("nnpcs.dat"), "wb");
    FILE *index = fopen(lotr_homedir_filename("npcs.ndx"), "wb");
    if (!records || !index) {
        if (records) fclose(records);
        if (index) fclose(index);
        return 0;
    }
    Uint8 record[NPC_BYTES] = {0};
    record[6] = 3;
    record[10] = 4;
    record[13] = 13;
    record[14] = 14;
    record[16] = 17;
    record[17] = 19;
    record[18] = 23;
    record[19] = 29;
    record[21] = 31;
    record[22] = 37;
    for (int i = 0; i < 10; ++i) {
        record[24 + i] = 0x60 + i;
        record[34 + i] = 0x40 + i;
        record[44 + i] = 0x10 + i;
    }
    memcpy(record + 54, "SixteenByteName!", 16);
    record[70] = 0x10; /* The archive slot determines the runtime ID. */
    record[80] = 7;
    unsigned offset = 0;
    int ok = 1;
    for (int id = 0; id < CHARACTERS_NUM && ok; ++id) {
        Uint8 address[] = {offset, offset >> 8, offset >> 16, offset >> 24};
        size_t length = id == 0xef ? 0 : id == 0xee ? NPC_BYTES - 1 : NPC_BYTES;
        ok = fwrite(address, 1, sizeof(address), index) == sizeof(address) &&
            fwrite(record, 1, length, records) == length;
        offset += length;
    }
    int index_closed = fclose(index) == 0;
    int records_closed = fclose(records) == 0;
    return ok && index_closed && records_closed;
}

static int synthetic_npcs(void)
{
    LotrDataFormat format = lotr_current_edition()->data_format;
    int towers = format == LOTR_DATA_TWO_TOWERS;
    if (!check(write_npc_fixture(), "NPC archive written"))
        return 0;
    characters_init();
    Character *npc = character_get(0xa4);
    int ok = check(npc->id == 0xa4 && !strcmp(npc->name, "SixteenByteName!") &&
                   npc->shape_id == 3 && npc->horse_shape_id == 14 && npc->portrait == 7 &&
                   npc->dex == 17 && npc->life == 19 && npc->str == 23 &&
                   npc->will == 29 && npc->luck == 31 && npc->end == 37 && npc->silver == 13,
                   "NPC identity, name, appearance and stat field offsets") &&
        check(npc->items_num == 10 && npc->skills_num == 10 && npc->spells_num == 10 &&
              npc->items[9] == 0x69 && npc->skills[9] == 0x49 && npc->spells[9] == 0x19 &&
              !character_exists(0xee) && !character_exists(0xef),
              "full NPC lists stay bounded and short and empty slots are skipped") &&
        check(npc->direction == (format == LOTR_DATA_ENHANCED_CD ? CHARACTER_DOWN : 4) &&
              npc->party_id == (towers ? 1 : 0xff) &&
              npc->x == (towers ? 2022 : -1) && npc->y == (towers ? 1473 : -1),
              "Enhanced CD direction mapping and Two Towers starting party and placement");
    characters_close();
    return ok;
}

int decoder_fixtures(void)
{
    int towers = lotr_current_edition()->data_format == LOTR_DATA_TWO_TOWERS;
    if (!ndx_back_reference() || !synthetic_maps_and_scripts() || !synthetic_npcs() ||
        !check(!strcmp(object_name(0x12), towers ? "!Fingon" : "!Bombadil") &&
               !strcmp(object_name(0x60), "Sting") &&
               object_price(0x60) == (towers ? 255 : 1) &&
               object_weapon_skill(0x72) == (towers ? 0x44 : 0) &&
               object_weapon_skill(0x76) == (towers ? 0 : 0x44) &&
               !strcmp(object_name(0xf6), towers ? "ent draught" : "Horn of Gondor") &&
               object_transferable(0x60) && !object_transferable(0xf6) &&
               !*object_name(-1) && !*object_name(0xf7),
               "edition object catalog, price, weapon skill and transfer boundaries"))
        return 0;
    puts("synthetic decoder regressions passed");
    return 1;
}

int decoder_disc_assets(void)
{
    int towers = lotr_current_edition()->data_format == LOTR_DATA_TWO_TOWERS;
    Archive *map = archive_ndx_open(towers ? "map1" : "map0");
    int size;
    Uint8 *bytes = decompress_ndxarchive(map, towers ? 0x27 : 7, &size);
    int ok = check(map_get_id() == (towers ? 6 : 0) && map_words_match(bytes, size) &&
                   map_get_num_spots() == (towers ? 37 : 127),
                   "startup map archive, tile record and script count");
    free(bytes);
    archive_close(map);

    Archive *npcs = archive_ndx_open("nnpcs");
    int id = towers ? 0xa4 : 0xa0;
    bytes = archive_read(npcs, id);
    Character *leader = game_get_leader();
    ok = ok && check(bytes && archive_data_size(npcs, id) == NPC_BYTES &&
                     leader == character_get(id) && leader->id == id &&
                     !strcmp(leader->name, towers ? "Aragorn" : "Frodo Baggins") &&
                     leader->shape_id == bytes[6] && leader->portrait == bytes[80] &&
                     leader->life == bytes[17] && leader->end == bytes[22],
                     "startup party resolves the expected NPC archive record");
    free(bytes);
    archive_close(npcs);
    if (ok)
        puts("disc decoder regressions passed");
    return ok;
}

int decoder_invalid_script(const char *kind)
{
    Uint8 resource[0x308] = {0};
    int offset = script_offset();
    int length = 5;
    if (!strcmp(kind, "short"))
        length = 1;
    else if (!strcmp(kind, "header")) {
        resource[offset] = 5;
        resource[offset + 2] = 4;
    } else if (strcmp(kind, "zero"))
        return 0;
    CommandSpot *spots[1];
    int count;
    spot_parse(resource, offset + length, spots, &count);
    return 0;
}
