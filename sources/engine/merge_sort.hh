#pragma once

#include <algorithm>

template <typename T> void merge(T* begin, T* mid, T* end, T* tmp)
{
  const T* tmp_orig = tmp;
  const T* a        = begin;
  const T* b        = mid;

  while ((mid != a) or (end != b))
  {
    if (mid == a)
    {
      *tmp++ = *b++;
    }
    else if (end == b)
    {
      *tmp++ = *a++;
    }
    else if (*a < *b)
    {
      *tmp++ = *a++;
    }
    else
    {
      *tmp++ = *b++;
    }
  }

  std::copy(tmp_orig, tmp_orig + (end - begin), begin);
}

template <typename T> void merge_sort(T* begin, T* end, T* tmp)
{
  if ((begin + 1) != end)
  {
    T* mid = begin + (end - begin) / 2;
    merge_sort(begin, mid, tmp);
    merge_sort(mid, end, tmp);
    merge(begin, mid, end, tmp);
  }
}
