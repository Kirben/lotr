#ifndef LOTR_GAME_STATE_H
#define LOTR_GAME_STATE_H

#include "character.h"

enum { GAME_PARTY_CAPACITY = 11, GAME_MAP_BANK_COUNT = 8, GAME_MAP_STATE_SIZE = 256,
       GAME_REGISTER_COUNT = 256 };

/* Shared by gameplay and the XML save codec. */
extern Character *leader;
extern Character *game_party[GAME_PARTY_CAPACITY];
extern int game_party_size;
extern int game_map_id;
extern Uint8 game_map_saves[GAME_MAP_BANK_COUNT][GAME_MAP_STATE_SIZE];
extern int game_map_saved[GAME_MAP_BANK_COUNT];
extern Uint8 game_registers[GAME_REGISTER_COUNT];
extern int silver_pennies;
extern int game_moving;
extern int game_follow;
extern int saved_position_x, saved_position_y, saved_position_dir, saved_position_map;

void game_reset_map_cache(void);

#endif
