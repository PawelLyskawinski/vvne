#pragma once

namespace utility {

template <typename T, int N> void copy(T* dst, const T* src)
{
  for (int i = 0; i < N; ++i)
  {
    dst[i] = src[i];
  }
};

} // namespace utility
