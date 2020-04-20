#pragma once

template <typename T> constexpr T align(T unaligned, T alignment)
{
    return (unaligned + (alignment - 1)) & (~(alignment - 1));
}
