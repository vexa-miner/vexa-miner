/************************************************************************************
*   written by: Bitmaker
*   based on: Blockstream Jade shaLib
*   thanks to @LarryBitcoin

*   Description:

*   VexaSha256plus is a custom C implementation of sha256d based on Blockstream Jade 
    code https://github.com/Blockstream/Jade

    The folowing file can be used on any ESP32 implementation using both cores

*************************************************************************************/
#ifndef vexaSHA256plus_H_
#define vexaSHA256plus_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


struct vexaSHA256_context {
    uint8_t buffer[64];
    uint32_t digest[8];
};

/* Calculate midstate */
IRAM_ATTR void vexa_mids(uint32_t* digest, const uint8_t* dataIn);

IRAM_ATTR bool vexa_sha256d(vexaSHA256_context* midstate, const uint8_t* dataIn, uint8_t* doubleHash);

IRAM_ATTR void vexa_sha256_bake(const uint32_t* digest, const uint8_t* dataIn, uint32_t* bake);  //15 words
IRAM_ATTR bool vexa_sha256d_baked(const uint32_t* digest, const uint8_t* dataIn, const uint32_t* bake, uint8_t* doubleHash);

void ByteReverseWords(uint32_t* out, const uint32_t* in, uint32_t byteCount);

#endif /* vexaSHA256plus_H_ */