/*
 * Deterministic encounters on the available original maps. The fixture
 * replaces one map spot so the game loop can enter, suspend and resume
 * the same quest through its normal movement and combat paths.
 */
#include "lotr.h"
#include "game.h"
#include "combat.h"
#include "lotr_sdl.h"
#include "map.h"
#include "spot.h"
#include "map_spot_fixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { QUEST = 0x71, VICTORY = 0x72, REWARD = 11 };

static int check(int condition, const char *message)
{
    if (!condition)
        fprintf(stderr, "campaign checkpoint failed: %s\n", message);
    return condition;
}

static void snapshot(int slot)
{
    Character *party[11];
    Character *leader = game_get_leader();
    int count = game_get_party_characters(party);
    printf("campaign checkpoint=%d map=%d leader=%d position=%d,%d life=%d silver=%d quest=%d,%d victory=%d,%d party=",
           slot, map_get_id(), leader->id, leader->x, leader->y, leader->life,
           game_get_silver(), game_get_register(QUEST), map_get_register(QUEST),
           game_get_register(VICTORY), map_get_register(VICTORY));
    for (int i = 0; i < count; ++i)
        printf("%s%d", i ? "," : "", party[i]->id);
    puts("");
}

static SDL_Keycode next_tile(Character *leader, int *x, int *y)
{
    static const SDL_Keycode keys[] = {SDLK_RIGHT, SDLK_LEFT, SDLK_UP, SDLK_DOWN};
    static const int dx[] = {1, -1, 0, 0};
    static const int dy[] = {0, 0, -1, 1};
    int tx = leader->x / 4;
    int ty = leader->y / 4;
    for (int i = 0; i < 4; ++i) {
        if (map_can_move_to(leader, tx + dx[i], ty + dy[i])) {
            *x = 4 * (tx + dx[i]);
            *y = 4 * (ty + dy[i]);
            return keys[i];
        }
    }
    return SDLK_UNKNOWN;
}

static int walk_to(SDL_Keycode key, int x, int y)
{
    SDL_Event event = {0};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    if (SDL_PushEvent(&event) != 1)
        return 0;
    lotr_poll_events();
    game_next_frame();
    event.type = SDL_KEYUP;
    if (SDL_PushEvent(&event) != 1)
        return 0;
    lotr_poll_events();

    for (int i = 0; i < 8 && !combat_get_mode(); ++i)
        game_next_frame();
    Character *leader = game_get_leader();
    if (!combat_get_mode() && !check(leader->x == x && leader->y == y, "keyboard movement reached the fixture"))
        return 0;
    printf("campaign movement target=%d,%d combat=%d\n", x, y, combat_get_mode());
    return 1;
}

static int quest_started(void)
{
    return game_get_register(QUEST) == 1 && map_get_register(QUEST) == 1;
}

static int quest_finished(void)
{
    return quest_started() && game_get_register(VICTORY) == 1 &&
        map_get_register(VICTORY) == 1 && game_get_silver() == REWARD;
}

int campaign_checkpoint(int stage)
{
    if (!check(map_get_num_spots() > 0, "map available"))
        return 0;
    int slot = stage == 2 ? 3 : 2;
    if (stage != 0) {
        if (!check(game_load(slot), "saved campaign loaded"))
            return 0;
        snapshot(slot);
    }
    Character *leader = game_get_leader();
    int x = leader->x, y = leader->y;
    SDL_Keycode key = stage == 2 ? SDLK_UNKNOWN : next_tile(leader, &x, &y);
    if (stage != 2 && !check(key != SDLK_UNKNOWN, "reachable adjacent tile"))
        return 0;

    if (stage == 0) {
        const Uint8 quest[] = {0x35, QUEST, 0x20, QUEST, 0x24};
        map_set_register(QUEST, 0);
        map_set_register(VICTORY, 0);
        game_set_silver(0);
        CommandSpot *spot = install_test_spot(x, y, quest, sizeof(quest));
        if (!check(spot != NULL, "fixture quest parsed") || !walk_to(key, x, y) ||
            !check(game_get_actual_spot() == spot && quest_started(), "movement activated quest"))
            return 0;
        snapshot(slot);
        return check(game_save(slot), "pre-combat checkpoint saved");
    }

    int enemy_id = 1;
    while (enemy_id < 0xa0 && (!character_exists(enemy_id) ||
           character_get(enemy_id)->map == map_get_id()))
        ++enemy_id;
    if (!check(enemy_id < 0xa0, "available encounter NPC"))
        return 0;

    Uint8 encounter[] = {
        0x33, VICTORY, 0x13,          /* Run only before victory. */
        0x11, 1, 0, 100, 1, enemy_id, 8, 0, 0, 0, /* Spawn one NPC nearby. */
        0x04, enemy_id, 0xff, 4, 0x38, /* Make the NPC hostile and enter combat. */
        0x35, VICTORY, 0x20, VICTORY, 0x3e, REWARD, 0, 0x0f, 0x24
    };
    size_t size = sizeof(encounter);
    if (lotr_current_edition()->data_format == LOTR_DATA_TWO_TOWERS) {
        /* Two Towers adds silver and encodes the amount in one byte. */
        memmove(encounter + size - 3, encounter + size - 2, 2);
        size--;
    }
    CommandSpot *spot = install_test_spot(x, y, encounter, size);
    if (!check(spot != NULL, "fixture encounter parsed"))
        return 0;

    if (stage == 2) {
        for (int i = 0; i < 12; ++i)
            game_next_frame();
        return check(game_get_actual_spot() == spot && !combat_get_mode() &&
                     !combat_get_enemy(0x100) && quest_finished(),
                     "re-entering the saved encounter did not replay it");
    }

    Character *prototype = character_get(enemy_id);
    prototype->end = prototype->dex = 1;
    prototype->items_num = 0;
    leader->dex = leader->str = 100;
    if (!check(quest_started() && !game_get_register(VICTORY), "quest ready after reload") ||
        !walk_to(key, x, y))
        return 0;
    Character *enemy = combat_get_enemy(0x100);
    if (!check(combat_get_mode() && enemy && game_get_actual_spot() == spot &&
               !game_get_register(VICTORY) && !game_get_silver(), "quest suspended at combat"))
        return 0;
    int ap = leader->ap;
    srand(2026);
    combat_attack(leader, enemy);
    if (!check(leader->ap == ap - 30 && !combat_get_enemy(0x100), "attack spent AP and killed enemy") ||
        !check(!combat_get_mode(), "last enemy ended combat"))
        return 0;
    for (int i = 0; i < 12; ++i)
        game_next_frame();
    if (!check(quest_finished() && leader->x == x && leader->y == y,
               "victory resumed quest once and restored position"))
        return 0;
    printf("campaign victory enemy=%d action_points_spent=%d\n", enemy_id, ap - leader->ap);
    snapshot(3);
    return check(game_save(3), "post-combat checkpoint saved");
}
