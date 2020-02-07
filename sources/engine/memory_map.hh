#pragma once

#include <vulkan/vulkan.h>

class MemoryMap
{
public:
    inline MemoryMap(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size)
            : device(device)
            , memory(memory)
            , ptr(nullptr)
    {
        vkMapMemory(device, memory, offset, size, 0, &ptr);
    }

    inline ~MemoryMap() { vkUnmapMemory(device, memory); }
    inline void* operator*() { return ptr; }

private:
    VkDevice       device;
    VkDeviceMemory memory;
    void*          ptr;
};

