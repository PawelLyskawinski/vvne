#pragma once

#include <vulkan/vulkan_core.h>

constexpr int               SWAPCHAIN_IMAGES_COUNT        = 2;
constexpr int               SHADOWMAP_IMAGE_DIM           = 1024 * 2;
constexpr int               SHADOWMAP_CASCADE_COUNT       = 4;
constexpr int               WORKER_THREADS_COUNT          = 3;
constexpr int               WORKER_MAX_COMMANDS_PER_FRAME = 64;
constexpr int               MAX_JOBS_PER_FRAME            = 128;
constexpr VkClearColorValue DEFAULT_COLOR_CLEAR           = {{0.0f, 0.0f, 0.2f, 1.0f}};