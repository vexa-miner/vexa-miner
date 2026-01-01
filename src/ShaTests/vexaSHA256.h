#ifndef vexaSHA256_H_
#define vexaSHA256_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VEXA_DIGEST_SIZE 32
#define VEXA_BLOCK_SIZE  64
#define VEXA_PAD_SIZE    56

struct vexa_sha256 {
    uint32_t  digest[VEXA_DIGEST_SIZE / sizeof(uint32_t)];
    uint32_t  buffer[VEXA_BLOCK_SIZE  / sizeof(uint32_t)];
    uint32_t  buffLen;   /* in bytes          */
    uint32_t  loLen;     /* length in bytes   */
    uint32_t  hiLen;     /* length in bytes   */
    void*   heap;
};

/* Calculate midstate */
IRAM_ATTR int vexa_midstate(vexa_sha256* sha256, uint8_t* data, uint32_t len);

//IRAM_ATTR int vexa_double_sha(vexa_sha256* midstate, uint8_t* data, uint8_t* doubleHash);

IRAM_ATTR int vexa_double_sha2(vexa_sha256* midstate, uint8_t* dataIn, uint8_t* doubleHash);

#endif /* vexaSHA256_H_ */