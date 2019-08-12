#pragma once

#include "engine/free_list_allocator.hh"
#include <algorithm>

using Entity = uint64_t;

class BaseEntityList
{
public:
  void     init(FreeListAllocator& allocator, uint32_t new_capacity);
  uint32_t insert(Entity entity);
  uint32_t remove(Entity entity);

  [[nodiscard]] uint32_t at(Entity entity) const; // returns entity index in lookup array
  [[nodiscard]] uint32_t get_size() const { return size; }

  [[nodiscard]] const Entity* begin() const { return entities; }
  [[nodiscard]] const Entity* end() const { return &entities[size]; }

private:
  uint32_t capacity;
  uint32_t size;
  Entity*  entities;
};

template <typename T> class Components
{
public:
  void init(FreeListAllocator& allocator, uint32_t new_capacity)
  {
    SDL_assert(nullptr == components);
    entities.init(allocator, new_capacity);
    components = allocator.allocate<T>(new_capacity);
    SDL_memset(components, 0, sizeof(T) * new_capacity);
  }

  T& insert(Entity entity)
  {
    const uint32_t position = entities.insert(entity);
    std::rotate(&components[position], &components[entities.get_size() - 1], &components[entities.get_size()]);
    return components[position];
  }

  void insert(Entity entity, const T& value)
  {
    const uint32_t position = entities.insert(entity);
    std::rotate(&components[position], &components[entities.get_size() - 1], &components[entities.get_size()]);
    components[position] = value;
  }

  void remove(Entity entity)
  {
    const uint32_t position = entities.remove(entity);
    std::rotate(&components[position], &components[position + 1], &components[entities.get_size()]);
    return components[position];
  }

  [[nodiscard]] T&       at(Entity entity) { return components[entities.at(entity)]; }
  [[nodiscard]] const T& at(Entity entity) const { return components[entities.at(entity)]; }

  BaseEntityList entities;
  T*             components;
};
