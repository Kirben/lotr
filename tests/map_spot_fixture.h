#ifndef LOTR_TEST_MAP_SPOT_FIXTURE_H
#define LOTR_TEST_MAP_SPOT_FIXTURE_H

#include "map.h"
#include "spot.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

enum { TEST_SPOT_HEADER = 12 };

static inline int test_spot_offset(void)
{
    return lotr_current_edition()->data_format == LOTR_DATA_STANDARD ? 0x100 : 0x300;
}

/* Replace a spot in the current map so the game loop can activate it. */
static inline CommandSpot *install_test_spot(int x, int y, const Uint8 *script, size_t size)
{
    const int offset = test_spot_offset();
    Uint8 resource[0x300 + 0x80] = {1};
    Uint8 *record = resource + offset;
    if (size > 0x80 - TEST_SPOT_HEADER ||
        map_get_num_spots() == 0)
        return NULL;
    record[0] = TEST_SPOT_HEADER + size;
    record[2] = TEST_SPOT_HEADER;
    record[4] = map_get_id();
    record[5] = x & 0xff;
    record[6] = x >> 8;
    record[7] = y & 0xff;
    record[8] = y >> 8;
    record[9] = record[10] = 1;
    memcpy(record + TEST_SPOT_HEADER, script, size);

    CommandSpot *parsed[1];
    int count;
    spot_parse(resource, offset + record[0], parsed, &count);
    if (count != 1)
        return NULL;
    if (parsed[0]->not_parsed) {
        spot_free(parsed[0]);
        return NULL;
    }
    for (int i = 0; i < map_get_num_spots(); ++i)
        map_get_spot_number(i)->flag = 0;
    CommandSpot *spot = map_get_spot_number(0);
    free(spot->data);
    *spot = *parsed[0];
    free(parsed[0]);
    return spot;
}

#endif
