#include "lines_renderer.hh"
#include "engine/allocators.hh"
#include "engine/merge_sort.hh"
#include <numeric>

namespace {

bool are_colors_equal(const Vec4& lhs, const Vec4& rhs)
{
  return (lhs.x == rhs.x) and (lhs.y == rhs.y) and (lhs.z == rhs.z) and (lhs.w == rhs.w);
}

bool compare_colors(const Vec4& lhs, const Vec4& rhs)
{
  return (lhs.x != rhs.x)   ? lhs.x < rhs.x
         : (lhs.y != rhs.y) ? lhs.y < rhs.y
         : (lhs.z != rhs.z) ? lhs.z < rhs.z
                            : lhs.w < rhs.w;
}

template <typename T, typename TFcn> T find_range_end(T begin, T end, TFcn pred)
{
  if (begin == end)
  {
    return begin;
  }

  T mid = (begin + 1);
  for (; end != mid; ++mid)
  {
    if (!pred(begin, mid))
    {
      break;
    }
  }
  return mid;
}

} // namespace

bool Line::operator<(const Line& rhs) const
{
  if (are_colors_equal(color, rhs.color))
  {
    return width < rhs.width;
  }
  else
  {
    return compare_colors(color, rhs.color);
  }
}

void LinesRenderer::setup(MemoryAllocator& allocator, uint32_t capacity)
{
  lines_capacity      = capacity;
  lines               = reinterpret_cast<Line*>(allocator.Allocate(sizeof(Line) * lines_capacity));
  position_cache      = reinterpret_cast<Vec2*>(allocator.Allocate(sizeof(Vec2) * (2 * lines_capacity)));
  lines_size          = 0;
  position_cache_size = 0;
}

void LinesRenderer::teardown(MemoryAllocator& allocator)
{
  allocator.Free(lines, sizeof(Line) * lines_capacity);
  allocator.Free(position_cache, sizeof(Vec2) * (2 * lines_capacity));
}

void LinesRenderer::cache_lines(MemoryAllocator& allocator)
{
  Line* tmp = reinterpret_cast<Line*>(allocator.Allocate(sizeof(Line) * lines_size));

  merge_sort(lines, lines + lines_size, tmp);

  auto acc_fcn = [](Vec2* a, const Line& line) {
    *a++ = line.origin;
    *a++ = line.origin + line.direction;
    return a;
  };

  position_cache_size =
      std::distance(position_cache, std::accumulate(lines, lines + lines_size, position_cache, acc_fcn));

  allocator.Free(tmp, sizeof(Line) * lines_size);
}

void LinesRenderer::render(VkCommandBuffer cmd, VkPipelineLayout layout, uint32_t base_offset) const
{
  auto color_equal = [](const Line* a, const Line* b) { return are_colors_equal(a->color, b->color); };
  auto width_equal = [](const Line* a, const Line* b) { return a->width == b->width; };

  const Line* begin       = lines;
  const Line* end         = lines + lines_size;
  const Line* color_begin = lines;
  const Line* color_end   = find_range_end(color_begin, end, color_equal);

  while (end != color_begin)
  {
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4), color_begin->color.data());

    const Line* width_begin = color_begin;
    const Line* width_end   = find_range_end(width_begin, end, width_equal);

    while ((color_end != width_begin) && (width_begin != width_end))
    {
      const uint32_t count  = 2 * std::distance(width_begin, width_end);
      const uint32_t offset = 2 * std::distance(begin, width_begin);

      vkCmdSetLineWidth(cmd, width_begin->width);
      vkCmdDraw(cmd, count, 1, base_offset + offset, 0);

      width_begin = width_end;
      width_end   = find_range_end(width_begin, end, width_equal);
    }

    color_begin = color_end;
    color_end   = find_range_end(color_begin, end, color_equal);
  }
}

void LinesRenderer::reset()
{
  lines_size          = 0;
  position_cache_size = 0;
}
