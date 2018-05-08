#pragma once

namespace utility {

template <typename T, int N> void copy(T* dst, T* src)
{
  for (int i = 0; i < N; ++i)
  {
    dst[i] = src[i];
  }
};

template <typename T> void generate_incremental(T* collection, T startval, long long unsigned int count)
{
  for (T i = 0; i < count; ++i)
  {
    collection[i] = startval + i;
  }
};

} // namespace utility
