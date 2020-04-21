#include "aligned_push_consts.hh"

void push_constants(AlignedPushConstsContext ctx, Span<AlignedPushElement> elements)
{
  uint64_t offset = 0;
  for (const AlignedPushElement& element : elements)
  {
    vkCmdPushConstants(ctx.command, ctx.layout, element.stage, offset, element.size, element.data);
    offset += element.size;
  }
}
