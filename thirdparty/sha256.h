/*********************************************************************
 * Filename:   sha256.h
 * Author:     Brad Conte (brad AT bradconte.com)
 * Copyright:
 * Disclaimer: This code is presented "as is" without any guarantees.
 * Details:    Defines the API for the corresponding SHA1 implementation.
 *
 * 1.08.2018 lyskawin: adjusted to c++ codebase
 *********************************************************************/
#pragma once

#include <SDL2/SDL_stdinc.h>

#define SHA256_BLOCK_SIZE 32

struct SHA256_CTX
{
  uint8_t            data[64];
  uint32_t           datalen;
  unsigned long long bitlen;
  uint32_t           state[8];
};

void sha256_init(SHA256_CTX* ctx);
void sha256_update(SHA256_CTX* ctx, const uint8_t data[], size_t len);
void sha256_final(SHA256_CTX* ctx, uint8_t hash[]);
