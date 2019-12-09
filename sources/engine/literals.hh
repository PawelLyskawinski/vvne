#pragma once

#include <SDL2/SDL_stdinc.h>

constexpr uint32_t operator"" _KB(unsigned long long in) { return 1024u * static_cast<uint32_t>(in); }
constexpr uint32_t operator"" _MB(unsigned long long in) { return 1024u * 1024u * static_cast<uint32_t>(in); }
