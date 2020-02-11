#include "lines_renderer.hh"
#include <algorithm>
#include <numeric>

namespace {

bool are_colors_equal(const Vec4& lhs, const Vec4& rhs)
{
  return (lhs.x == rhs.x) and (lhs.y == rhs.y) and (lhs.z == rhs.z) and (lhs.w == rhs.w);
}

bool compare_colors(const Vec4& lhs, const Vec4& rhs)
{
  return (lhs.x != rhs.x) ? lhs.x < rhs.x
                          : (lhs.y != rhs.y) ? lhs.y < rhs.y : (lhs.z != rhs.z) ? lhs.z < rhs.z : lhs.w < rhs.w;
}

template <typename T, typename TFcn> T find_range_end(T begin, T end, TFcn pred)
{
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
  return are_colors_equal(color, rhs.color) ? (width < rhs.width) : compare_colors(color, rhs.color);
}

void LinesRenderer::setup(HierarchicalAllocator& allocator)
{
  lines               = allocator.allocate<Line>(lines_capacity);
  position_cache      = allocator.allocate<Vec2>(position_cache_capacity);
  lines_size          = 0;
  position_cache_size = 0;
}

void LinesRenderer::teardown(HierarchicalAllocator& allocator)
{
  allocator.free(lines, lines_capacity);
  allocator.free(position_cache, position_cache_capacity);
}

void LinesRenderer::cache_lines()
{
  std::sort(lines, lines + lines_size);

  auto acc_fcn = [](Vec2* a, const Line& line) {
    *a++ = line.origin;
    *a++ = line.size;
    return a;
  };

  position_cache_size =
      std::distance(position_cache, std::accumulate(lines, lines + lines_size, position_cache, acc_fcn));
}

void LinesRenderer::render(VkCommandBuffer cmd, VkPipelineLayout layout)
{
  auto color_equal = [](const Line* a, const Line* b) { return are_colors_equal(a->color, b->color); };
  auto width_equal = [](const Line* a, const Line* b) { return a->width == b->width; };

  const Line* end         = lines + lines_size;
  const Line* color_begin = lines;
  const Line* color_end   = find_range_end(color_begin, end, color_equal);

  while (end != color_begin)
  {
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4), color_begin->color.data());
    const Line* width_end = find_range_end(color_begin, end, width_equal);
    while(color_end != width_end)
  }

  // for each same line width range
  vkCmdSetLineWidth(cmd, line.width);

  // for each element
  vkCmdDraw(cmd, 2 * line_width_range.size(), 1, 2 * line_width_range.offset(), 0);

#if 0
  while (lines_begin != lines_end)
  {
    ColorRange color_range(lines_begin, lines_end);
  }

  const Line* color_begin = lines_begin;
  while (lines_end != color_begin)
  {
    auto find_color_end = [color_begin](const Line& line) { return are_colors_equal(line.color, color_begin->color); };
    const Line* color_end = std::find_if(lines_begin, lines_end, find_color_end);

    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Vec4), color_begin->color.data());

    std::for_each(color_begin, color_end, [&](const Line& line) {
      vkCmdSetLineWidth(cmd, line.width);
      vkCmdDraw(cmd, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
    });
  }

  for (int i = 0; i < 4; ++i)
  {
    if (0 == line_counts[i])
      continue;

    vkCmdSetLineWidth(command, line_widths[i]);
    vkCmdDraw(command, 2 * static_cast<uint32_t>(line_counts[i]), 1, 2 * offset, 0);
    offset += line_counts[i];
  }
}
#endif
}
