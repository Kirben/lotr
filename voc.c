#include "voc.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static unsigned
read_le16(const uint8_t *data)
{
    return data[0] | (unsigned)data[1] << 8;
}

static unsigned
read_le24(const uint8_t *data)
{
    return read_le16(data) | (unsigned)data[2] << 16;
}

int
voc_has_signature(const uint8_t *data, size_t size)
{
    static const char signature[] = "Creative Voice File";
    return data && size >= sizeof(signature) - 1 &&
        memcmp(data, signature, sizeof(signature) - 1) == 0;
}

uint8_t *
voc_decode(const uint8_t *data, size_t size, SampleInfo *sample)
{
    *sample = (SampleInfo){0};
    if (!voc_has_signature(data, size) || size < 26 || data[19] != 0x1a)
        return NULL;

    unsigned offset = read_le16(data + 20);
    unsigned version = read_le16(data + 22);
    unsigned checksum = read_le16(data + 24);
    if (offset < 26 || offset >= size || (version >> 8) != 1 ||
        checksum != ((~version + 0x1234u) & 0xffff))
        return NULL;

    data += offset;
    size -= offset;
    unsigned numerator = 1000000;
    unsigned denominator = 0;
    if (data[0] == 8) {
        /* Extended mono PCM attributes apply only to the following sound block. */
        if (size < 8 || read_le24(data + 1) != 4 || data[6] || data[7])
            return NULL;
        numerator = 256000000;
        denominator = 65536u - read_le16(data + 4);
        data += 8;
        size -= 8;
    }

    if (size < 7 || data[0] != 1)
        return NULL;
    unsigned length = read_le24(data + 1);
    if (length <= 2 || length >= size - 4 || length - 2 > INT_MAX)
        return NULL;
    if (!denominator) {
        if (data[5] != 0)
            return NULL;
        denominator = 256u - data[4];
    }
    /* A terminator is required. Accept zero padding after it. */
    for (size_t i = (size_t)length + 4; i < size; ++i)
        if (data[i] != 0)
            return NULL;

    uint8_t *pcm = malloc(length - 2);
    if (!pcm)
        return NULL;
    memcpy(pcm, data + 6, length - 2);
    *sample = (SampleInfo){(int)(length - 2), numerator, denominator};
    return pcm;
}
