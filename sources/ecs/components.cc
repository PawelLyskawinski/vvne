#include "components.hh"

void BaseEntityList::init(FreeListAllocator& allocator, uint32_t new_capacity)
{
  SDL_assert(nullptr == entities);
  capacity = new_capacity;
  size     = 0;
  entities = allocator.allocate<Entity>(capacity);
  SDL_memset(entities, 0, sizeof(uint32_t) * new_capacity);
}

uint32_t BaseEntityList::insert(Entity entity)
{
  Entity* begin = entities;
  Entity* end   = &entities[size];

  *end = entity;

  Entity* lb = std::lower_bound(begin, end, entity);
  std::rotate(lb, end, end + 1);
  size += 1;

  return std::distance(begin, lb);
}

uint32_t BaseEntityList::remove(Entity entity)
{
  Entity* begin = entities;
  Entity* end   = &entities[size];
  Entity* it    = std::lower_bound(begin, end, entity);

  SDL_assert(end != it);

  std::rotate(it, it + 1, end);
  size -= 1;

  return std::distance(begin, it);
}

uint32_t BaseEntityList::at(Entity entity) const
{
  Entity* begin = entities;
  Entity* end   = &entities[size];
  return std::distance(begin, std::lower_bound(begin, end, entity));
}
