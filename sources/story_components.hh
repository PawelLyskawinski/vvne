#pragma once

#include "engine/math.hh"

namespace story {

enum class Node
{
  Start,
  Any,
  All,
  GoTo
};

enum class State
{
  Upcoming,
  Active,
  Finished
};

struct TargetPosition
{
  uint32_t entity;
  Vec3     position;
  float    radius;

  [[nodiscard]] bool operator==(uint32_t rhs) const { return entity == rhs; }
};

struct Connection
{
  uint32_t src_node_idx   = 0;
  uint32_t src_output_idx = 0;
  uint32_t dst_input_idx  = 0;
  uint32_t dst_node_idx   = 0;

  bool operator==(const Connection& rhs) const
  {
    return (src_node_idx == rhs.src_node_idx) and (src_output_idx == rhs.src_output_idx) and
           (dst_input_idx == rhs.dst_input_idx) and (dst_node_idx == rhs.dst_node_idx);
  }
};

} // namespace story