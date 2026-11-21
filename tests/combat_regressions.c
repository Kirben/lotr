#include "lotr.h"
#include "combat.h"
#include "game.h"
#include "gui.h"
#include "lotr_sdl.h"
#include "map.h"
#include "object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Existing combat internals, used to arrange repeatable position fixtures. */
enum { GRID_WIDTH = 38, GRID_HEIGHT = 16, MOVE_COST = 15, ATTACK_COST = 30 };
extern int combat_x, combat_y;
extern int combat_area[GRID_WIDTH][GRID_HEIGHT];
extern void combat_next_turn(void);
extern void combat_player_move(void);
extern void combat_move_to_area(Character *character);

static int check(int condition, const char *label)
{
    if (!condition)
        fprintf(stderr, "combat regression failed: %s\n", label);
    return condition;
}

static void place(Character *character, int x, int y)
{
    character->x = x;
    character->y = y;
    character->action = CHARACTER_STAY;
    map_character_update(character);
}

static Character *spawn_enemy(Character *model, int x, int y, int dex)
{
    int id = combat_enemy(model, x, y, CHARACTER_DOWN, model->map_id);
    Character *enemy = combat_get_enemy(id);
    enemy->items_num = 0;
    enemy->skills_num = 0;
    enemy->dex = dex;
    enemy->life = 100;
    map_add_character(enemy);
    return enemy;
}

static int turn_order(Character *party[11], int count, Character *fast, Character *slow)
{
    Character *leader = party[0];
    Character *ally = party[1];
    if (!check(gui_choosed() == leader && leader->ap == 70 && ally->ap == 50 &&
               fast->ap == 60 && slow->ap == 40, "round opens in dexterity order"))
        return 0;
    leader->ap = 0;
    combat_character_finished();
    if (!check(gui_choosed() == fast, "enemy with most AP acts next"))
        return 0;
    fast->ap = 50;
    combat_character_finished();
    if (!check(gui_choosed() == ally, "party member wins an AP tie"))
        return 0;
    ally->ap = fast->ap = 0;
    combat_character_finished();
    if (!check(gui_choosed() == slow, "remaining enemy gets its turn"))
        return 0;
    slow->ap = 0;
    combat_character_finished();
    for (int i = 2; i < count; ++i) {
        if (!check(gui_choosed() == party[i], "extra party members act before the round refills"))
            return 0;
        party[i]->ap = 0;
        combat_character_finished();
    }
    return check(gui_choosed() == leader && leader->ap == 70 && fast->ap == 60,
                 "round refills AP and restarts in dexterity order");
}

static int targeting(Character *leader, Character *ally, Character *melee_target, Character *bow_target)
{
    int codes[10];
    place(ally, leader->x, leader->y + 4);
    place(melee_target, leader->x + MAP_NEAR_DISTANCE - 1, leader->y);
    place(bow_target, leader->x + MAP_NEAR_DISTANCE, leader->y);
    leader->items_num = 1;
    leader->items[0] = 0x65; /* Magic Bow */
    leader->item_used[0] = 0;
    if (!check(combat_in_bow_range(leader, codes) == 0, "unequipped bow cannot target") ||
        !check(map_npc_stay_near(leader, codes, 0) == 1 && codes[0] == melee_target->id,
               "melee excludes allies and targets at the distance limit"))
        return 0;
    leader->item_used[0] = 1;
    return check(combat_in_bow_range(leader, codes) == 2 &&
                 codes[0] == melee_target->id && codes[1] == bow_target->id,
                 "equipped bow targets both enemies");
}

static int damage_for_seed(Character attacker, Character target, unsigned seed)
{
    attacker.action = CHARACTER_ATTACK; /* Only measure damage, with no map animation. */
    attacker.ap = 60;
    target.life = 1000;
    srand(seed);
    combat_attack(&attacker, &target);
    return attacker.ap == 60 - ATTACK_COST ? 1000 - target.life : -1;
}

static int armour(Character attacker, Character target)
{
    attacker.items_num = attacker.skills_num = target.items_num = target.skills_num = 0;
    attacker.dex = attacker.str = 100;
    target.dex = 1;
    int protected_hits = 0;
    for (unsigned seed = 1; seed <= 16; ++seed) {
        target.items_num = 0;
        int bare = damage_for_seed(attacker, target, seed);
        target.items_num = 1;
        target.items[0] = 0x84; /* No to-hit modifier. */
        target.item_used[0] = 1;
        int reduction = object_armour_reduced(target.items[0]);
        int protected = damage_for_seed(attacker, target, seed);
        if (!check(bare >= 0 && protected == (bare > reduction ? bare - reduction : 0),
                   "defender's equipped armour reduces seeded damage"))
            return 0;
        protected_hits += protected < bare;
        target.item_used[0] = 0;
        if (!check(damage_for_seed(attacker, target, seed) == bare,
                   "unequipped armour does not reduce damage"))
            return 0;
    }
    if (!check(protected_hits > 0, "armour test included successful hits"))
        return 0;

    attacker.dex = target.dex = 21;
    target.items_num = 0;
    int impaired_hits = 0;
    for (unsigned trial = 1; trial <= 32; ++trial) {
        unsigned seed = trial * 7919u;
        attacker.items_num = 0;
        int bare = damage_for_seed(attacker, target, seed);
        attacker.items_num = 1;
        attacker.items[0] = 0x82;
        attacker.item_used[0] = 1;
        int encumbered = damage_for_seed(attacker, target, seed);
        if (!check(bare >= 0 && encumbered >= 0 && encumbered <= bare,
                   "attacker's equipped armour impairs accuracy"))
            return 0;
        impaired_hits += encumbered < bare;
    }
    return check(impaired_hits > 0, "armour penalty changed a seeded hit");
}

static int knockout(Character *leader, Character *ally, Character *enemy)
{
    ally->dex = 1;
    ally->items_num = ally->skills_num = 0;
    enemy->str = 100;
    unsigned seed = 1;
    int damage = 0;
    for (; seed <= 16 && damage <= 0; ++seed)
        damage = damage_for_seed(*enemy, *ally, seed);
    if (!check(damage > 0, "knockout fixture found a damaging attack"))
        return 0;

    ally->life = 5 + damage;
    enemy->ap = 60;
    srand(seed - 1);
    combat_attack(enemy, ally);
    if (!check(ally->life == 5 && game_in_party(ally) && combat_get_mode(),
               "nonlethal damage knocks out the party member"))
        return 0;
    ally->ap = 999;
    combat_character_finished();
    if (!check(gui_choosed() == leader, "knocked-out member is skipped despite available AP"))
        return 0;
    combat_next_turn();
    return check(ally->life == 4 && gui_choosed() == leader,
                 "knocked-out member loses life at the next round");
}

static int press_direction(SDL_Keycode key)
{
    SDL_Event event = {0};
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = key;
    if (SDL_PushEvent(&event) != 1)
        return 0;
    lotr_poll_events();
    combat_player_move();
    event.type = SDL_KEYUP;
    if (SDL_PushEvent(&event) != 1)
        return 0;
    lotr_poll_events();
    return 1;
}

static int grid_edges(Character *leader)
{
    static const struct { int dx, dy; SDL_Keycode key; } edges[] = {
        {-1, 0, SDLK_LEFT}, {1, 0, SDLK_RIGHT}, {0, -1, SDLK_UP}, {0, 1, SDLK_DOWN}
    };
    int center_x = leader->x / 4;
    int center_y = leader->y / 4;
    for (size_t i = 0; i < SDL_arraysize(edges); ++i) {
        int dx = edges[i].dx, dy = edges[i].dy;
        int x = center_x, y = center_y;
        int found = 0;
        for (int tx = center_x - 12; tx <= center_x + 12 && !found; ++tx)
            for (int ty = center_y - 12; ty <= center_y + 12 && !found; ++ty)
                if (map_can_move_to(leader, tx, ty) &&
                    map_can_move_to(leader, tx + dx, ty + dy)) {
                    x = tx;
                    y = ty;
                    found = 1;
                }
        if (!check(found, "walkable tiles on both sides of boundary"))
            return 0;
        place(leader, x * 4, y * 4);
        combat_x = x - (dx < 0 ? 0 : dx > 0 ? GRID_WIDTH - 1 : GRID_WIDTH / 2);
        combat_y = y - (dy < 0 ? 0 : dy > 0 ? GRID_HEIGHT - 1 : GRID_HEIGHT / 2);
        leader->ap = 60;
        if (!press_direction(edges[i].key) ||
            !check(leader->ap == 60 && leader->action == CHARACTER_STAY,
                   "outside move is rejected without charging AP"))
            return 0;
        combat_x += dx;
        combat_y += dy;
        if (!press_direction(edges[i].key) ||
            !check(leader->ap == 60 - MOVE_COST && leader->action != CHARACTER_STAY,
                   "same move inside the grid spends AP"))
            return 0;
        character_frame(leader);
        character_frame(leader);
        if (!check(leader->x == (x + dx) * 4 && leader->y == (y + dy) * 4,
                   "inside move reached the requested tile"))
            return 0;
    }
    return 1;
}

static int lower_edge_placement(Character *leader)
{
    int center_x = leader->x / 4;
    int center_y = leader->y / 4;
    for (int x = center_x - 12; x <= center_x + 12; ++x)
        for (int y = center_y - 12; y <= center_y + 12; ++y)
            if (map_can_move_to(leader, x, y) && map_can_move_to(leader, x - 1, y)) {
                combat_x = x - GRID_HEIGHT;
                combat_y = y - (GRID_HEIGHT - 1);
                memset(combat_area, 0, sizeof(combat_area));
                combat_area[GRID_HEIGHT - 1][GRID_HEIGHT - 1] = 1;
                combat_area[GRID_HEIGHT][GRID_HEIGHT - 1] = 1;
                place(leader, x * 4, (y + 1) * 4);
                combat_move_to_area(leader);
                return check(leader->x == x * 4 && leader->y == y * 4,
                             "lower-edge placement keeps the character's column");
            }
    return check(0, "two walkable tiles for lower-edge placement");
}

int combat_regressions(void)
{
    Character *leader = game_get_leader();
    Character *party[11];
    int party_size = game_get_party_characters(party);
    if (party_size < 2) {
        Character *companion = character_get(0xa1);
        companion->map = map_get_id();
        companion->x = leader->x;
        companion->y = leader->y + 4;
        map_unique_add_character(companion);
        if (!check(game_recruit(companion, 1), "test companion recruited"))
            return 0;
        party_size = game_get_party_characters(party);
    }
    Character *ally = party[1];
    leader->dex = 70;
    for (int i = 1; i < party_size; ++i) {
        party[i]->dex = i == 1 ? 50 : 10;
        party[i]->life = 25;
    }
    game_draw_map();
    Character *fast = spawn_enemy(leader, leader->x + 8, leader->y, 60);
    Character *slow = spawn_enemy(leader, leader->x - 8, leader->y, 40);
    combat_start();
    if (!turn_order(party, party_size, fast, slow) ||
        !targeting(leader, ally, fast, slow) ||
        !armour(*leader, *fast) ||
        !knockout(leader, ally, fast) ||
        !grid_edges(leader) ||
        !lower_edge_placement(leader))
        return 0;
    combat_genocide();
    puts("combat regressions passed: turn order, melee, bow, armour, knockout, four grid edges");
    return 1;
}
