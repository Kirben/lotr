#include "lotr.h"
#include "game.h"
#include "map_spot_fixture.h"
#include <stdio.h>

enum {
    INPUT_OUTER = 0x60,
    INPUT_INNER,
    PATH_BOTH,
    PATH_OUTER_ONLY,
    PATH_NEITHER,
    BRANCH_DONE,
    TIMER_RESULT,
    RECRUIT_RESULT,
    INVENTORY_RESULT,
    EXACT_COUNT,
    MINIMUM_COUNT,
    QUEST_OBJECT = 0xac,
    MAP_STOCK = 0x50
};

static int check(int condition, const char *description)
{
    if (!condition)
        fprintf(stderr, "quest regression failed: %s\n", description);
    return condition;
}

static CommandSpot *place_at_leader(const Uint8 *script, size_t size)
{
    Character *leader = game_get_leader();
    return install_test_spot(leader->x, leader->y, script, size);
}

static int timer(void)
{
    const int handler = test_spot_offset() + TEST_SPOT_HEADER + 11;
    const Uint8 script[] = {
        0x22, 3, 0,                         /* Set a three-tick delay. */
        0x27, 1, SPOT_ACTION_TIMER, 0xff, 0xff, handler & 0xff, handler >> 8,
        0x24,                               /* Timer handler starts after END. */
        0x35, TIMER_RESULT, 0x12, MAP_STOCK, 0x24
    };
    CommandSpot *spot = place_at_leader(script, sizeof(script));
    if (!check(spot != NULL, "timer script parsed"))
        return 0;
    map_get_objects()[MAP_STOCK] = 0;
    game_set_register(TIMER_RESULT, 0);
    game_next_frame();
    if (!check(game_get_actual_spot() == spot, "movement loop entered the timer spot"))
        return 0;
    game_next_frame();
    if (!check(spot_has_action(spot, SPOT_ACTION_TIMER, 0xff, 0xff) &&
               !spot_action(spot, SPOT_ACTION_TALK, 0xff, 0xff),
               "timer action registered and ignored unrelated input"))
        return 0;
    for (int frame = 0; frame < 4; ++frame) {
        game_next_frame();
        if (!check(!game_get_register(TIMER_RESULT) && !map_get_objects()[MAP_STOCK],
                   "timer handler did not run early"))
            return 0;
    }
    game_next_frame();
    if (!check(game_get_register(TIMER_RESULT) && map_get_objects()[MAP_STOCK] == 1,
               "timer expiry scheduled its handler on the next frame"))
        return 0;
    for (int frame = 0; frame < 8; ++frame)
        game_next_frame();
    return check(map_get_objects()[MAP_STOCK] == 1, "timer ran only once");
}

static int branching(void)
{
    const Uint8 script[] = {
        0x33, INPUT_OUTER, 0x14,             /* IF outer THEN */
            0x33, INPUT_INNER, 0x14,         /* IF inner THEN */
                0x35, PATH_BOTH,
            0x0e,                            /* ELSE */
                0x35, PATH_OUTER_ONLY,
            0x0f,                            /* ENDIF */
        0x0e,                                /* ELSE */
            0x35, PATH_NEITHER,
        0x0f,                                /* ENDIF */
        0x35, BRANCH_DONE, 0x24
    };
    CommandSpot *spot = place_at_leader(script, sizeof(script));
    if (!check(spot != NULL, "nested branch script parsed"))
        return 0;
    for (int outer = 0; outer <= 1; ++outer)
        for (int inner = 0; inner <= 1; ++inner) {
            game_set_register(INPUT_OUTER, outer);
            game_set_register(INPUT_INNER, inner);
            for (int flag = PATH_BOTH; flag <= BRANCH_DONE; ++flag)
                game_set_register(flag, 0);
            if (!check(spot_start(spot) == 0 && game_get_register(BRANCH_DONE) &&
                       game_get_register(PATH_BOTH) == (outer && inner) &&
                       game_get_register(PATH_OUTER_ONLY) == (outer && !inner) &&
                       game_get_register(PATH_NEITHER) == !outer,
                       "exactly one nested branch ran and execution rejoined"))
                return 0;
        }
    return 1;
}

static int inventory_gates(void)
{
    const Uint8 script[] = {
        0x06, MAP_STOCK, 0, 2, 0x14, 0x35, EXACT_COUNT, 0x0f,
        0x07, MAP_STOCK, 0, 2, 0x14, 0x35, MINIMUM_COUNT, 0x0f, 0x24
    };
    CommandSpot *spot = place_at_leader(script, sizeof(script));
    if (!check(spot != NULL, "map stock gates parsed"))
        return 0;
    for (int count = 1; count <= 3; ++count) {
        map_get_objects()[MAP_STOCK] = count;
        game_set_register(EXACT_COUNT, 0);
        game_set_register(MINIMUM_COUNT, 0);
        if (!check(spot_start(spot) == 0 &&
                   game_get_register(EXACT_COUNT) == (count == 2) &&
                   game_get_register(MINIMUM_COUNT) == (count >= 2),
                   "exact and minimum inventory thresholds differ above the limit"))
            return 0;
    }
    return 1;
}

static int recruitment(void)
{
    Character *leader = game_get_leader();
    Character *recruit = character_get(0xa1);
    Character *party[11];
    int count = game_get_party_characters(party);
    int two_towers = lotr_current_edition()->data_format == LOTR_DATA_TWO_TOWERS;
    const int original_party_id = recruit->party_id;
    if (!check(!game_in_party(recruit), "quest recruit starts outside party"))
        return 0;

    for (int i = 0; i < count; ++i)
        party[i]->items_num = 0;
    recruit->items_num = 0;
    if (!check(character_add_item(recruit, QUEST_OBJECT), "give quest item to recruit"))
        return 0;
    recruit->silver = 7;
    game_set_silver(0);
    const Uint8 gate[] = {0x05, 0xf0, 2, QUEST_OBJECT, 0, 0x14,
                          0x35, INVENTORY_RESULT, 0x0f, 0x24};
    CommandSpot *spot = place_at_leader(gate, sizeof(gate));
    if (!check(spot && spot_start(spot) == 0 && !game_get_register(INVENTORY_RESULT),
               "item held by an outsider doesn't open a party gate"))
        return 0;

    Uint8 script[3 + sizeof(gate) + 2] = {0x00, 0xa1, leader->party_id};
    size_t offset = two_towers ? 3 : 2;
    script[offset++] = 0x35;
    script[offset++] = RECRUIT_RESULT;
    memcpy(script + offset, gate, sizeof(gate));
    spot = place_at_leader(script, offset + sizeof(gate));
    if (!check(spot && spot_start(spot) == 0 && game_in_party(recruit) &&
               game_get_register(RECRUIT_RESULT) &&
               recruit->party_id == (two_towers ? leader->party_id : original_party_id),
               "recruitment uses the edition's operand length and party ID") ||
        !check(game_get_register(INVENTORY_RESULT),
               "new member's item opens the gate in the same script"))
        return 0;
    game_set_register(INVENTORY_RESULT, 0);
    character_discard_item(recruit, 0);
    if (two_towers)
        script[2] ^= 0x40; /* A duplicate request must not change the member's party. */
    spot = place_at_leader(script, offset + sizeof(gate));
    return check(spot && spot_start(spot) == 0 && !game_get_register(INVENTORY_RESULT) &&
                 recruit->party_id == (two_towers ? leader->party_id : original_party_id) &&
                 game_get_party_characters(party) == count + 1 && game_get_silver() == 7,
                 "repeat recruitment does not duplicate the member or silver and removed items close the gate");
}

static int towers_demo_weregild(void)
{
    enum { SPOT = 0x0b, BRANCH = 0x07ab, AFTER_MESSAGE = 0x07bc,
           END = 0x07e7, COMPLETE = 0x0e };
    const int flags[] = {0x09, 0x0b, 0x0c, 0x0d, 0x13};
    CommandSpot *spot = map_get_spot_number(SPOT);
    if (!check(spot && spot->map == 6 && !spot->not_parsed,
               "Two Towers demo weregild spot parsed completely"))
        return 0;

    int branch = 0;
    while (branch < spot->commands_num &&
           spot->label_start + spot->command_start[branch] < BRANCH)
        ++branch;
    if (!check(branch < spot->commands_num &&
               spot->label_start + spot->command_start[branch] == BRANCH,
               "weregild completion branch found"))
        return 0;

    Character *leader = game_get_leader();
    Character *leofyn = character_get(0x1e);
    int original_dex = leader->dex;
    int original_end = leader->end;
    for (int state = 0; state < 32; ++state) {
        if (state == COMPLETE)
            continue;
        for (int bit = 0; bit < 5; ++bit)
            map_set_register(flags[bit], (state >> bit) & 1);
        spot->pos = branch;
        if (!check(spot_continue(spot) == 0 && spot->pos < spot->commands_num &&
                   spot->label_start + spot->command_start[spot->pos] == END &&
                   map_get_register(0x09) == (state & 1) &&
                   map_get_register(0x13) == ((state >> 4) & 1) &&
                   leader->dex == original_dex && leader->end == original_end,
                   "weregild incomplete or already awarded reaches END without a reward"))
            return 0;
    }

    for (int bit = 0; bit < 5; ++bit)
        map_set_register(flags[bit], (COMPLETE >> bit) & 1);
    leofyn->texts[0] = 0x26;
    spot->pos = branch;
    if (!check(spot_continue(spot) == 1 && spot->pos < spot->commands_num &&
               spot->label_start + spot->command_start[spot->pos] == AFTER_MESSAGE &&
               !map_get_register(0x09) && !map_get_register(0x13),
               "completed weregild waits at the reward message"))
        return 0;
    if (!check(spot_continue(spot) == 0 && map_get_register(0x09) &&
               map_get_register(0x13) && leader->dex == original_dex + 2 &&
               leader->end == original_end + 2 && leofyn->texts[0] == 0x35,
               "weregild reward updates the party and jumps to Leofyn's new greeting"))
        return 0;
    spot->pos = branch;
    if (!check(spot_continue(spot) == 0 && leader->dex == original_dex + 2 &&
               leader->end == original_end + 2, "weregild reward is granted once"))
        return 0;
    puts("Two Towers demo weregild regression passed across 32 register combinations");
    return 1;
}

static int enhanced_cd_ringbearer_handoff(void)
{
    const Uint8 script[] = {0x3f, 0xf7, 0x40, 0x47, 0x01, 0x3c, 0xfe, 0x24};
    Character *leader = game_get_leader();
    Character *ringbearer = character_get(0xa1); /* Recruited by the preceding test. */
    const int previous_leader_ring = leader->ring_mode;
    const int previous_ring = ringbearer->ring_mode;
    const int previous_activity = ringbearer->actived;
    CommandSpot *spot = place_at_leader(script, sizeof(script));
    if (!check(spot && spot->commands_num == 4 &&
               spot->command_start[1] == TEST_SPOT_HEADER + 2 &&
               spot->command_start[2] == TEST_SPOT_HEADER + 5,
               "Glorfindel commands decode separately"))
        return 0;

    leader->ring_mode = 0;
    ringbearer->ring_mode = 1;
    ringbearer->actived = 0;
    int ok = check(spot_start(spot) == 0 && game_get_leader() == ringbearer &&
                   ringbearer->actived, "selected Ringbearer becomes temporary leader");
    if (game_get_leader() == ringbearer)
        game_dismiss_tmp_leader();
    ringbearer->actived = previous_activity;
    ringbearer->ring_mode = previous_ring;
    leader->ring_mode = previous_leader_ring;
    return ok && check(game_get_leader() == leader, "previous leader restored");
}

int quest_regressions(void)
{
    if (!timer() || !branching() || !inventory_gates() || !recruitment())
        return 0;
    if (lotr_current_edition()->data_format == LOTR_DATA_ENHANCED_CD &&
        !enhanced_cd_ringbearer_handoff())
        return 0;
    if (lotr_current_edition()->data_format == LOTR_DATA_TWO_TOWERS &&
        !towers_demo_weregild())
        return 0;
    puts("quest regressions passed: nested branches, one-shot timer, recruitment, inventory gates");
    return 1;
}
