#include "../voc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER(offset) \
    'C','r','e','a','t','i','v','e',' ','V','o','i','c','e',' ','F','i','l','e', \
    0x1a, offset, 0, 0x0a, 0x01, 0x29, 0x11

static void
expect_pcm(const uint8_t *file, size_t length, const uint8_t *expected,
           int count, unsigned numerator, unsigned denominator)
{
    SampleInfo sample;
    uint8_t *pcm = voc_decode(file, length, &sample);
    assert(pcm);
    assert(sample.size == count);
    assert(sample.rate_numerator == numerator);
    assert(sample.rate_denominator == denominator);
    assert(memcmp(pcm, expected, count) == 0);
    free(pcm);
}

static void
expect_invalid(const uint8_t *file, size_t size)
{
    SampleInfo sample = {99, 1, 1};
    assert(voc_decode(file, size, &sample) == NULL);
    assert(sample.size == 0 && !sample.rate_numerator && !sample.rate_denominator);
}

int
main(void)
{
    static const uint8_t standard[] = {
        HEADER(28), 0xaa, 0xbb,
        1, 4,0,0, 0xc0,0, 1,2,
        0
    };
    static const uint8_t extended[] = {
        HEADER(26),
        8, 4,0,0, 0,0xd2,0,0,
        1, 4,0,0, 0x12,3, 1,2,
        0
    };
    static const uint8_t two[] = {1,2};
    expect_pcm(standard, sizeof(standard), two, sizeof(two), 1000000, 64);
    expect_pcm(extended, sizeof(extended), two, sizeof(two), 256000000, 11776);
    for (size_t length = 0; length < sizeof(standard); ++length)
        expect_invalid(standard, length);
    for (size_t length = 0; length < sizeof(extended); ++length)
        expect_invalid(extended, length);

    uint8_t damaged[sizeof(standard)];
    static const struct {
        size_t offset;
        uint8_t value;
    } invalid_fields[] = {
        {0, 'x'}, {19, 0}, {20, 25}, {20, 255}, {23, 2}, {24, 0},
        {29, 2}, {30, 255}, {33, 1}, {36, 1}
    };
    for (size_t i = 0; i < sizeof(invalid_fields) / sizeof(*invalid_fields); ++i) {
        memcpy(damaged, standard, sizeof(damaged));
        damaged[invalid_fields[i].offset] = invalid_fields[i].value;
        expect_invalid(damaged, sizeof(damaged));
    }
    for (unsigned type = 2; type <= 9; ++type) {
        memcpy(damaged, standard, sizeof(damaged));
        damaged[28] = type;
        expect_invalid(damaged, sizeof(damaged));
    }

    uint8_t extended_bad[sizeof(extended)];
    const size_t extended_fields[] = {27, 32, 33, 34};
    for (size_t i = 0; i < sizeof(extended_fields) / sizeof(*extended_fields); ++i) {
        memcpy(extended_bad, extended, sizeof(extended_bad));
        extended_bad[extended_fields[i]] = 2;
        expect_invalid(extended_bad, sizeof(extended_bad));
    }

    uint8_t padded[sizeof(standard) + 1] = {0};
    memcpy(padded, standard, sizeof(standard));
    expect_pcm(padded, sizeof(padded), two, sizeof(two), 1000000, 64);
    padded[sizeof(standard)] = 1;
    expect_invalid(padded, sizeof(padded));
    puts("Creative Voice game layouts and malformed input tests passed");
    return 0;
}
