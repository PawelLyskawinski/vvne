#include "level_generator_vr.hh"
#include "stb_image.h"
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_stdinc.h>
#include <linmath.h>

#define STB_HERRINGBONE_WANG_TILE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include "stb_herringbone_wang_tile.h"
#pragma GCC diagnostic pop

namespace {

//
// ids for layer 1
// 0 1 2   0 1 2 3 4
// 7   3   F       5
// 6 5 4   E       6
//         D       7
//         C B A 9 8
//

//
// example results for layer 1
// (-1, 1)  (0, 1)  (1, 1)
// (-1, 0)          (1, 0)
// (-1, -1) (0, -1) (1, -1)
//

void pixel_position_on_squere(int dst[2], int idx, int layer)
{
  const int side        = idx / (2 * layer);
  const int idx_on_side = idx % (2 * layer);

  switch (side)
  {
  default:
  case 0:
    dst[0] = idx_on_side - layer;
    dst[1] = layer;
    break;
  case 1:
    dst[0] = layer;
    dst[1] = layer - idx_on_side;
    break;
  case 2:
    dst[0] = layer - idx_on_side;
    dst[1] = -layer;
    break;
  case 3:
    dst[0] = -layer;
    dst[1] = idx_on_side - layer;
    break;
  }
}

int find_center(const uint8_t line[], const int line_width)
{
  for (int distance = 0; distance < 100; ++distance)
  {
    for (int sign_flip = 0; sign_flip < 2; ++sign_flip)
    {
      const int tile_idx = (line_width / 2) + (sign_flip * -1) * distance;

      if (SDL_TRUE == line[tile_idx])
      {
        return tile_idx;
      }
    }
  }

  return 0;
}

void calculate_normal_at_line_length(float line_pt_a[2], float line_pt_b[2], float length, float distance,
                                     float result[2])
{
  float x_delta         = line_pt_b[0] - line_pt_a[0];
  float y_delta         = line_pt_b[1] - line_pt_a[1];
  float angle           = atanf(y_delta / x_delta);
  float normal_point[2] = {line_pt_a[0] + cosf(angle) * length, line_pt_a[1] + sinf(angle) * length};
  float _90deg_radian   = (float)(M_PI) / 2.0f;
  float new_angle       = angle + (0.0f < distance ? -_90deg_radian : _90deg_radian);

  result[0] = normal_point[0] + (cosf(new_angle) * distance);
  result[1] = normal_point[1] + (sinf(new_angle) * distance);
}

//
// area line: line with the surrounding field around it
// uv coordinates:
//
//  ---------> x
// |
// |
// V y
//
void calculate_n_shaped_area_lines_uv_coordinates(int top_offset, int side_offset, int bottom_offset,
                                                  int max_distance_from_line, int scene_width, int scene_height,
                                                  int line_points[12])
{
  int vertical_line_length   = scene_height - top_offset - bottom_offset;
  int horizontal_line_length = scene_width - 2 * (side_offset + max_distance_from_line);

  // bottom left
  {
    int* pt = &line_points[0];
    pt[0]   = side_offset + max_distance_from_line;
    pt[1]   = top_offset + vertical_line_length;
  }

  // top left
  {
    int* pt = &line_points[2];
    pt[0]   = side_offset + max_distance_from_line;
    pt[1]   = top_offset;
  }

  // center left
  {
    int* pt = &line_points[4];
    pt[0]   = side_offset + (2 * max_distance_from_line);
    pt[1]   = top_offset + max_distance_from_line;
  }

  // center right
  {
    int* pt = &line_points[6];
    pt[0]   = side_offset + (2 * max_distance_from_line) + horizontal_line_length;
    pt[1]   = top_offset + max_distance_from_line;
  }

  // top right
  {
    int* pt = &line_points[8];
    pt[0]   = scene_width - (side_offset + max_distance_from_line);
    pt[1]   = top_offset;
  }

  // bottom right
  {
    int* pt = &line_points[10];
    pt[0]   = scene_width - (side_offset + max_distance_from_line);
    pt[1]   = top_offset + vertical_line_length;
  }
}

void line_lengths(int xy_positions[], int n, int out_lengths[])
{
  auto is_line_vertical = [](int a[2], int b[2]) { return (a[0] == b[0]); };

  for (int i = 0; i < n; ++i)
  {
    int* a         = &xy_positions[4 * i];
    int* b         = &xy_positions[4 * i + 2];
    out_lengths[i] = is_line_vertical(a, b) ? SDL_abs(a[1] - b[1]) : SDL_abs(a[0] - b[0]);
  }
}

struct RgbPixmap
{
  int find_entrence_at_bottom_of_labitynth() const
  {
    return find_center(&tile_used[0], width);
  }

  void generate_goal(vec2 dst) const
  {
    int max_distance_from_line = 20;
    int points[12];
    calculate_n_shaped_area_lines_uv_coordinates(20, 20, 50, max_distance_from_line, width, height, points);

    int lengths[3];
    line_lengths(points, SDL_arraysize(lengths), lengths);

    int random_line               = rand() % SDL_arraysize(lengths);
    int random_point_on_line      = rand() % lengths[random_line];
    int random_distance_from_line = rand() % (2 * max_distance_from_line) - max_distance_from_line;

    float line_pts[4];
    for (int i = 0; i < 4; ++i)
      line_pts[i] = static_cast<float>(points[2 * random_line + i]);

    float goal[2] = {};
    calculate_normal_at_line_length(&line_pts[0], &line_pts[2], random_point_on_line, random_distance_from_line, goal);

    //
    // Ultimately the point should be in a room or corridor, so we'll have to
    // snap to closest point. Floats are
    //
    int tile_approximation[2] = {static_cast<int>(dst[0]), static_cast<int>(dst[1])};

    if (tile_used[(tile_approximation[1] * width) + tile_approximation[0]])
    {
      dst[0] = tile_approximation[0];
      dst[1] = height - tile_approximation[1];
    }
    else
    {
      const int searchbox_diameter = 5;
      for (int layer = 1; layer < searchbox_diameter; ++layer)
      {
        const int layer_length = 4 * (2 * layer);
        for (int idx = 0; idx < layer_length; ++idx)
        {
          int position[2];
          pixel_position_on_squere(position, idx, layer);
          if (SDL_TRUE == tile_used[tile_approximation[0] + position[0], tile_approximation[1] + position[1]])
          {
            dst[0] = tile_approximation[0] + position[0];
            dst[1] = height - (tile_approximation[1] + position[1]);
            return;
          }
        }
      }
    }
  }

  void generate_herringbone_wang(stbhw_tileset* ts)
  {
    stbhw_generate_image(ts, nullptr, pixels, width * 3, width, height);

    for (int i = 0; i < width * height; ++i)
    {
      auto is_pixel_white = [](uint8_t rgb[3]) {
        for (int i = 0; i < 3; ++i)
          if (255 != rgb[i])
            return false;
        return true;
      };

      tile_used[i] = is_pixel_white(&pixels[3 * i]) ? SDL_FALSE : SDL_TRUE;
    }
  }

  int count_tiles() const
  {
    int counter = 0;
    for (int i = 0; i < width * height; ++i)
      if (SDL_TRUE == tile_used[i])
        counter += 1;
    return counter;
  }

  uint8_t* pixels;
  uint8_t* tile_used;
  int      width;
  int      height;
};

} // namespace

VrLevelLoadResult level_generator_vr(Engine* engine)
{
  stbhw_tileset ts = {};

  {
    int      w    = 0;
    int      h    = 0;
    uint8_t* data = stbi_load("../assets/template_horizontal_corridors_v2.png", &w, &h, nullptr, 3);

    stbhw_build_tileset_from_image(&ts, data, w * 3, w, h);
    stbi_image_free(data);
  }

  RgbPixmap pixmap = {};
  pixmap.width     = 300;
  pixmap.height    = 150;
  pixmap.pixels    = engine->double_ended_stack.allocate_back<uint8_t>(3 * pixmap.width * pixmap.height);
  pixmap.tile_used = engine->double_ended_stack.allocate_back<uint8_t>(pixmap.width * pixmap.height);
  pixmap.generate_herringbone_wang(&ts);

  int tile_count = pixmap.count_tiles();

  struct Vertex
  {
    vec3 position;
    vec3 normal;
    vec2 texcoord;
  };

  VkDeviceSize vertex_buffer_size = 4 * tile_count * sizeof(Vertex);
  VkDeviceSize host_vertex_offset = engine->gpu_static_transfer.allocate(vertex_buffer_size);
  VkDeviceSize index_buffer_size  = 6 * tile_count * sizeof(uint16_t);
  VkDeviceSize host_index_offset  = engine->gpu_static_transfer.allocate(index_buffer_size);

  {
    Vertex* dst_vertices = nullptr;
    vkMapMemory(engine->generic_handles.device, engine->gpu_static_transfer.memory, host_vertex_offset,
                vertex_buffer_size, 0, (void**)&dst_vertices);

    int last_tile_idx = 0;
    for (int y = 0; y < pixmap.height; ++y)
    {
      for (int x = 0; x < pixmap.width; ++x)
      {
        if (SDL_TRUE == pixmap.tile_used[y * pixmap.width + x])
        {
          Vertex* stride = &dst_vertices[4 * last_tile_idx];
          last_tile_idx += 1;

          for (int i = 0; i < 4; ++i)
          {
            Vertex* vtx = &stride[i];

            vtx->normal[0]   = 0.0f;
            vtx->normal[1]   = 0.0f;
            vtx->normal[2]   = 0.0f;
            vtx->texcoord[0] = 0.0f;
            vtx->texcoord[1] = 0.0f;
          }

          Vertex* ul = &stride[0];
          Vertex* ur = &stride[1];
          Vertex* br = &stride[2];
          Vertex* bl = &stride[3];

          float x_adjusted = x - (pixmap.width * 0.5f);

          ul->position[0] = 0.1f * x_adjusted;
          ul->position[1] = 0.0f;
          ul->position[2] = 0.1f * y;

          ur->position[0] = 0.1f * (x_adjusted + 1);
          ur->position[1] = 0.0f;
          ur->position[2] = 0.1f * y;

          bl->position[0] = 0.1f * x_adjusted;
          bl->position[1] = 0.0f;
          bl->position[2] = 0.1f * (y + 1);

          br->position[0] = 0.1f * (x_adjusted + 1);
          br->position[1] = 0.0f;
          br->position[2] = 0.1f * (y + 1);
        }
      }
    }

    vkUnmapMemory(engine->generic_handles.device, engine->gpu_static_transfer.memory);
  }

  {
    uint16_t* dst_indices = nullptr;
    vkMapMemory(engine->generic_handles.device, engine->gpu_static_transfer.memory, host_index_offset,
                index_buffer_size, 0, (void**)&dst_indices);

    uint16_t tile_indices[] = {0, 1, 2, 2, 3, 0};

    for (int tile_idx = 0; tile_idx < tile_count; ++tile_idx)
      for (int i = 0; i < 6; ++i)
        dst_indices[6 * tile_idx + i] = tile_indices[i] + (uint16_t)(4 * tile_idx);

    vkUnmapMemory(engine->generic_handles.device, engine->gpu_static_transfer.memory);
  }

  VrLevelLoadResult result{};
  result.level_load_data.index_count          = 6 * tile_count;
  result.level_load_data.index_type           = VK_INDEX_TYPE_UINT16;
  result.level_load_data.vertex_target_offset = engine->gpu_static_geometry.allocate(vertex_buffer_size);
  result.level_load_data.index_target_offset  = engine->gpu_static_geometry.allocate(index_buffer_size);

  result.entrance_point[0] = 0.1f * (pixmap.find_entrence_at_bottom_of_labitynth() - (pixmap.width * 0.5f));
  result.entrance_point[1] = 0.0f;

  pixmap.generate_goal(result.target_goal);

  result.target_goal[0] = 0.1f * (result.target_goal[0] - (pixmap.width * 0.5f));
  result.target_goal[1] = 0.1f * (pixmap.height - result.target_goal[1]);

  VkCommandBuffer cmd = VK_NULL_HANDLE;

  {
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool        = engine->generic_handles.graphics_command_pool;
    allocate.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    vkAllocateCommandBuffers(engine->generic_handles.device, &allocate, &cmd);
  }

  {
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
  }

  {
    VkBufferCopy copies[2] = {};

    copies[0].size      = vertex_buffer_size;
    copies[0].srcOffset = host_vertex_offset;
    copies[0].dstOffset = result.level_load_data.vertex_target_offset;

    copies[1].size      = index_buffer_size;
    copies[1].srcOffset = host_index_offset;
    copies[1].dstOffset = result.level_load_data.index_target_offset;

    vkCmdCopyBuffer(cmd, engine->gpu_static_transfer.buffer, engine->gpu_static_geometry.buffer, SDL_arraysize(copies),
                    copies);
  }

  {
    VkBufferMemoryBarrier barriers[2] = {};

    barriers[0].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[0].srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[0].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].buffer              = engine->gpu_static_geometry.buffer;
    barriers[0].offset              = result.level_load_data.vertex_target_offset;
    barriers[0].size                = vertex_buffer_size;

    barriers[1].sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barriers[1].srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].buffer              = engine->gpu_static_geometry.buffer;
    barriers[1].offset              = result.level_load_data.index_target_offset;
    barriers[1].size                = index_buffer_size;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, nullptr,
                         SDL_arraysize(barriers), barriers, 0, nullptr);
  }

  vkEndCommandBuffer(cmd);

  VkFence data_upload_fence = VK_NULL_HANDLE;

  {
    VkFenceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(engine->generic_handles.device, &ci, nullptr, &data_upload_fence);
  }

  {
    VkSubmitInfo submit{};
    submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers    = &cmd;
    vkQueueSubmit(engine->generic_handles.graphics_queue, 1, &submit, data_upload_fence);
  }

  vkWaitForFences(engine->generic_handles.device, 1, &data_upload_fence, VK_TRUE, UINT64_MAX);
  vkDestroyFence(engine->generic_handles.device, data_upload_fence, nullptr);
  vkFreeCommandBuffers(engine->generic_handles.device, engine->generic_handles.graphics_command_pool, 1, &cmd);
  engine->gpu_static_transfer.used_memory = 0;

  engine->double_ended_stack.reset_back();
  stbhw_free_tileset(&ts);

  return result;
}
