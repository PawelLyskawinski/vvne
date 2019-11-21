#pragma once

#include "engine/math.hh"
#include <SDL2/SDL_events.h>

namespace story {

constexpr uint32_t NODES_CAPACITY       = 256;
constexpr uint32_t CONNECTIONS_CAPACITY = 5 * NODES_CAPACITY;

struct Node
{
  enum class Type
  {
    Dummy,
    All,
    Any
  };

  Type type = Type::Dummy;
};

struct Connection
{
  uint32_t src_node_idx   = 0;
  uint32_t src_output_idx = 0;
  uint32_t dst_input_idx  = 0;
  uint32_t dst_node_idx   = 0;
};

struct EditorData
{
  float zoom                      = 0.0f;
  Vec2  positions[NODES_CAPACITY] = {};
};

struct Data
{
  Node       nodes[NODES_CAPACITY]             = {};
  Connection connections[CONNECTIONS_CAPACITY] = {};
  EditorData editor_data                       = {};

  uint32_t nodes_count       = 0;
  uint32_t connections_count = 0;
  uint32_t editor_data_count = 0;

  void init();
  void editor_render();
  void editor_update(const SDL_Event& event);
};

} // namespace story
