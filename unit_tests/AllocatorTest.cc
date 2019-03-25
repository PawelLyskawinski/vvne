#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "../sources/engine/free_list_allocator.hh"

void print_as_offset(FreeListAllocator* allocator, uint8_t* ptr)
{
    SDL_Log("%u", static_cast<uint32_t>(ptr - allocator->pool));
}

void print_freelist(FreeListAllocator* allocator)
{
    SDL_Log("ALLOCATOR STATE - BEGIN");
    FreeListAllocator::Node* current = allocator->head.next;
    while(current)
    {
        SDL_Log("offset: %u, size: %u", static_cast<uint32_t>(reinterpret_cast<uint8_t*>(current) - allocator->pool), current->size);
        current = current->next;
    }
    SDL_Log("ALLOCATOR STATE - END");
}

int main()
{
    FreeListAllocator* allocator = reinterpret_cast<FreeListAllocator*>(SDL_calloc(1, sizeof(FreeListAllocator)));

    allocator->init();
    SDL_Log("1");
    print_freelist(allocator);

    SDL_Log("2");
    uint8_t* a = allocator->allocate<uint8_t>(100);
    print_freelist(allocator);

    SDL_Log("3");
    allocator->allocate<uint8_t>(200);
    print_freelist(allocator);

    SDL_Log("4");
    allocator->free(a, 100);
    print_freelist(allocator);

    SDL_Log("5");
    a = allocator->allocate<uint8_t>(300);
    print_freelist(allocator);

    // this triggers assert
    SDL_Log("6");
    allocator->free(a, 300);

    SDL_Log("Hello World!");
    SDL_free(allocator);
    return 0;
}