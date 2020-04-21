#pragma once

//
// Rounds up memory size to fit to alignment
//
// @unaligned: count in bytes
// @alignment: as above, for example 8 bytes = 8
//

template <typename T> constexpr T align(T unaligned, T alignment)
{
    return (unaligned + (alignment - 1)) & (~(alignment - 1));
}
