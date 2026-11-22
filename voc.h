#ifndef LOTR_VOC_H
#define LOTR_VOC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int size;
    unsigned rate_numerator;
    unsigned rate_denominator;
} SampleInfo;

/* Detect probable VOC input, including files with a damaged signature byte. */
int voc_has_signature(const uint8_t *data, size_t size);

/* Decode one mono unsigned 8-bit PCM block, optionally with extended attributes. Caller owns the result. */
uint8_t *voc_decode(const uint8_t *data, size_t size, SampleInfo *sample);

#endif
