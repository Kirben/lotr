/****************************************************************************

    game_save.c
    XML save files and legacy save import


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


#include "game.h"
#include "game_state.h"
#include "combat.h"
#include "map.h"
#include "spot.h"
#include "utils.h"
#include <libxml/parser.h>
#include <stdlib.h>
#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

/*
  save game
*/

static int
write_save(xmlDocPtr doc, LotrDataFormat format, int slot, int replace)
{
    char temporary[LOTR_PATH_CAPACITY];
    const char *path = lotr_save_filename(format, slot);
#ifdef WIN32
    CreateDirectoryA("saves", NULL);
    if (!GetTempFileNameA("saves", "lot", 0, temporary))
        return 0;
#else
    int length = snprintf(temporary, sizeof(temporary), "%s.tmp.XXXXXX", path);
    if (length < 0 || (size_t)length >= sizeof(temporary))
        return 0;
    int fd = mkstemp(temporary);
    if (fd < 0)
        return 0;
    close(fd);
#endif
    int ok = xmlSaveFormatFileEnc(temporary, doc, "ascii", 1) >= 0;
    if (ok) {
#ifdef WIN32
        ok = MoveFileExA(temporary, path, replace ? MOVEFILE_REPLACE_EXISTING : 0) != 0;
#else
        /* Publish an import only if its destination is still free. */
        ok = (replace ? rename(temporary, path) : link(temporary, path)) == 0;
#endif
    }
    remove(temporary);
    return ok;
}

int
game_save(int n)
{

    const LotrEdition *edition = lotr_current_edition();
    int i;


    Uint8 buf[sizeof(game_party) / sizeof(game_party[0])];

    xmlDocPtr doc;
    xmlNodePtr root;
    xmlNodePtr node;
    xmlNodePtr subnode;

    if (n < 0 || n > 9)
        return 0;
    doc = xmlNewDoc((const xmlChar *)"1.0");
    if (!doc)
        return 0;
    xmlSetDocCompressMode(doc, 6);


    root = xmlNewNode(NULL, (const xmlChar *)"lotr_savegame");
    xmlDocSetRootElement(doc, root);

    lotr_save_prop_int(root, "vol", edition->volume);
    xmlNewProp(root, BAD_CAST "edition", BAD_CAST edition->key);


    node = xmlNewNode(NULL, (const xmlChar *)"characters");
    xmlAddChild(root, node);

    characters_save(node);


    node = xmlNewNode(NULL, (const xmlChar *)"map_saves");
    xmlAddChild(root, node);

    map_save(game_map_saves[game_map_id / 2]);
    game_map_saved[game_map_id / 2] = 1;

    for (i = 0; i < GAME_MAP_BANK_COUNT; ++i)
        if (game_map_saved[i]) {
            subnode = xmlNewNode(NULL, (const xmlChar *)"map");
            lotr_save_prop_int(subnode, "map_num", i);
            lotr_save_prop_field(subnode, "map_data", game_map_saves[i],
                                 GAME_MAP_STATE_SIZE);
            xmlAddChild(node, subnode);
        }


    lotr_save_prop_int(root, "game_map_id", game_map_id);


    node = xmlNewNode(NULL, (const xmlChar *)"map");
    xmlAddChild(root, node);

    map_save_mode(node);


    lotr_save_prop_field(root, "game_registers", game_registers, GAME_REGISTER_COUNT);


    node = xmlNewNode(NULL, (const xmlChar *)"party");
    xmlAddChild(root, node);

    lotr_save_prop_int(node, "silver", silver_pennies);
    lotr_save_prop_int(node, "moving", game_moving);
    lotr_save_prop_int(node, "follow", game_follow);

    lotr_save_prop_int(node, "follow", game_follow);

    for (i = 0; i < game_party_size; ++i)
        buf[i] = game_party[i]->id;

    lotr_save_prop_field(node, "party", buf, game_party_size);

    lotr_save_prop_int(node, "leader", leader->id);

    node = xmlNewNode(NULL, (const xmlChar *)"saved_position");
    xmlAddChild(root, node);

    lotr_save_prop_int(node, "x", saved_position_x);
    lotr_save_prop_int(node, "y", saved_position_y);
    lotr_save_prop_int(node, "dir", saved_position_dir);
    lotr_save_prop_int(node, "map", saved_position_map);


    int saved = write_save(doc, edition->data_format, n, 1);

    xmlFreeDoc(doc);

    return saved;
}


/*
  load game
*/

static const char *
save_path(LotrDataFormat format, int slot, int legacy)
{
    return legacy ? lotr_legacy_save_filename(lotr_edition_at(format)->volume, slot) :
        lotr_save_filename(format, slot);
}

int
game_save_available(LotrDataFormat format, int slot, int legacy)
{
    if (!lotr_edition_at(format) || slot < 0 || slot > 9)
        return 0;
    FILE *file = fopen(save_path(format, slot, legacy), "rb");
    if (!file)
        return 0;
    fclose(file);
    return 1;
}

static xmlDocPtr
game_read_save(LotrDataFormat format, int slot, int legacy)
{
    int options = XML_PARSE_NONET;
#if LIBXML_VERSION >= 21500
    /* libxml2 2.15 and later require explicit opt-in for gzip input. */
    options |= XML_PARSE_UNZIP;
#endif
    if (!lotr_edition_at(format) || slot < 0 || slot > 9)
        return NULL;
    xmlDocPtr doc = xmlReadFile(save_path(format, slot, legacy), NULL, options);
    if (!doc)
        return NULL;
    xmlNodePtr root = xmlDocGetRootElement(doc);
    const LotrEdition *edition = lotr_edition_at(format);
    xmlChar *key = root ? xmlGetProp(root, BAD_CAST "edition") : NULL;
    int matches = root && xmlStrEqual(root->name, BAD_CAST "lotr_savegame") &&
        lotr_load_prop_int_default(root, "vol", 1) == edition->volume &&
        (key ? xmlStrEqual(key, BAD_CAST edition->key) : legacy);
    xmlFree(key);
    if (!matches) {
        xmlFreeDoc(doc);
        return NULL;
    }
    return doc;
}

static int
has_save_sections(xmlNodePtr root)
{
    xmlNodePtr party = lotr_get_subnode(root, BAD_CAST "party", 0);
    return party && lotr_get_subnode(party, BAD_CAST "party", 0) &&
        lotr_get_subnode(root, BAD_CAST "characters", 0) &&
        lotr_get_subnode(root, BAD_CAST "map_saves", 0) &&
        lotr_get_subnode(root, BAD_CAST "map", 0);
}

int
game_import_save(int slot)
{
    LotrDataFormat format = lotr_current_edition()->data_format;
    if (game_save_available(format, slot, 0))
        return 0;
    xmlDocPtr doc = game_read_save(format, slot, 1);
    if (!doc)
        return 0;
    xmlNodePtr root = xmlDocGetRootElement(doc);
    if (!has_save_sections(root)) {
        xmlFreeDoc(doc);
        return 0;
    }
    xmlSetProp(root, BAD_CAST "edition", BAD_CAST lotr_current_edition()->key);
    xmlSetDocCompressMode(doc, 6);
    int imported = write_save(doc, format, slot, 0);
    xmlFreeDoc(doc);
    return imported;
}

int
game_load(int n)
{

    int i;

    Uint8 buf[sizeof(game_party) / sizeof(game_party[0])];

    xmlDocPtr doc;
    xmlNodePtr root;
    xmlNodePtr node;
    xmlNodePtr subnode;

    doc = game_read_save(lotr_current_edition()->data_format, n, 0);
    if (doc == NULL)
        return 0;

    root = xmlDocGetRootElement(doc);
    if (!has_save_sections(root)) {
        xmlFreeDoc(doc);
        return 0;
    }

    int map = lotr_load_prop_int(root, "game_map_id");
    if (!game_map_available(map)) {
        xmlFreeDoc(doc);
        return 0;
    }

    combat_set_confined(0);
    spot_reset_selection();
    node = lotr_get_subnode(root, (const xmlChar *)"characters", 1);
    characters_load(node);

    /* Older Two Towers builds could copy past Aragorn's full skill list,
       replacing his runtime ID (0xa4) with 0x10. Recover the old party
       references when the saved records show Aragorn active and Damrod idle. */
    int repair_aragorn = lotr_current_edition()->data_format == LOTR_DATA_TWO_TOWERS &&
        character_exists(0xa4) && character_exists(0x10) &&
        character_get(0xa4)->actived && !character_get(0x10)->actived;

    node = lotr_get_subnode(root, (const xmlChar *)"map_saves", 1);

    for (i = 0; i < GAME_MAP_BANK_COUNT; ++i)
        game_map_saved[i] = 0;

    subnode = node->xmlChildrenNode;

    while (subnode != NULL) {
        if (xmlNodeIsText(subnode)) {
            subnode = subnode->next;
            continue;
        }
        i = lotr_load_prop_int_default(subnode, "map_num", -1);
        if (i >= 0 && i < sizeof(game_map_saved) / sizeof(game_map_saved[0]) &&
            lotr_get_subnode(subnode, BAD_CAST "map_data", 0)) {
            game_map_saved[i] = 1;
            lotr_load_prop_field(subnode, "map_data", game_map_saves[i], GAME_MAP_STATE_SIZE);
        }

        subnode = subnode->next;
    }


    game_reset_map_cache();

    game_load_map(map);


    node = lotr_get_subnode(root, (const xmlChar *)"map", 1);
    map_load_mode(node);


    if (lotr_get_subnode(root, (const xmlChar *)"game_registers", 0))
        lotr_load_prop_field(root, "game_registers", game_registers, GAME_REGISTER_COUNT);
    else
        for (i = 0; i < GAME_REGISTER_COUNT; ++i)
            game_registers[i] = 0;


    node = lotr_get_subnode(root, (const xmlChar *)"party", 1);

    silver_pennies = lotr_load_prop_int(node, "silver");
    game_moving = lotr_load_prop_int(node, "moving");
    game_follow = lotr_load_prop_int(node, "follow");

    game_party_size = lotr_load_prop_field(node, "party", buf, sizeof(buf));

    for (i = 0; i < game_party_size; ++i)
        game_party[i] = character_get(repair_aragorn && buf[i] == 0x10 ? 0xa4 : buf[i]);


    int leader_id = lotr_load_prop_int(node, "leader");
    leader = character_get(repair_aragorn && leader_id == 0x10 ? 0xa4 : leader_id);

    node = lotr_get_subnode(root, (const xmlChar *)"saved_position", FALSE);

    if (node) {
        saved_position_x = lotr_load_prop_int(node, "x");
        saved_position_y = lotr_load_prop_int(node, "y");
        saved_position_dir = lotr_load_prop_int(node, "dir");
        saved_position_map = lotr_load_prop_int(node, "map");
    } else {
        saved_position_x = -1;
        saved_position_y = -1;
        saved_position_dir = -1;
        saved_position_map = -1;
    }


    xmlFreeDoc(doc);


    return 1;
}


/*
  convert vol. I characters to vol. II
*/

int
game_convert(LotrDataFormat source, int slot, int legacy)
{
    if (lotr_current_edition()->volume != 2 ||
        (source != LOTR_DATA_STANDARD && source != LOTR_DATA_ENHANCED_CD))
        return 0;
    xmlDocPtr doc = game_read_save(source, slot, legacy);
    if (!doc && legacy)
        doc = game_read_save(source == LOTR_DATA_STANDARD ? LOTR_DATA_ENHANCED_CD :
                            LOTR_DATA_STANDARD, slot, 1);
    if (!doc)
        return 0;

    xmlNodePtr root = xmlDocGetRootElement(doc);
    xmlNodePtr party = lotr_get_subnode(root, BAD_CAST "party", 0);
    xmlNodePtr characters = lotr_get_subnode(root, BAD_CAST "characters", 0);
    if (!party || !characters || !lotr_get_subnode(party, BAD_CAST "party", 0)) {
        xmlFreeDoc(doc);
        return 0;
    }

    character_get(0xa4)->silver = lotr_load_prop_int_default(party, "silver", 0);
    Uint8 members[sizeof(game_party) / sizeof(game_party[0])];
    int count = lotr_load_prop_field(party, "party", members, sizeof(members));
    character_convert(characters, 0xa0);
    for (int i = 0; i < count; ++i)
        if (members[i] >= 0xa1 && members[i] <= 0xa7 && members[i] != 0xa5)
            character_convert(characters, members[i]);

    xmlFreeDoc(doc);
    return 1;
}
