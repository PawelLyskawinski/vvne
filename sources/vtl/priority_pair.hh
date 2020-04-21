#pragma once

#include "engine/math.hh"
#include <vulkan/vulkan_core.h>

struct Priority
{
  static constexpr int min = -5;
  static constexpr int max = 5;

  Priority() = default;

  explicit Priority(int level)
      : level(clamp(level, min, max))
  {
  }

  bool operator<(const Priority& rhs) const
  {
    return level < rhs.level;
  }

  int level = 0;
};

template <typename T> struct PriorityPair
{
  PriorityPair() = default;
  explicit PriorityPair(const T& init)
      : data(init)
  {
  }

  explicit PriorityPair(const T& init, int prio)
      : priority(prio)
      , data(init)
  {
  }

  bool operator<(const PriorityPair<T>& rhs) const
  {
    return priority < rhs.priority;
  }

  PriorityPair<T>& operator=(const T&& other)
  {
    data = other;
    return *this;
  }

  Priority priority;
  T        data;
};
