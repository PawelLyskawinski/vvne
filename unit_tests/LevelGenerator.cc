#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_HERRINGBONE_WANG_TILE_IMPLEMENTATION
#include "stb_herringbone_wang_tile.h"

namespace {

bool is_pixel_white(uint8_t rgb[3])
{
  for (int i = 0; i < 3; ++i)
    if (255 != rgb[i])
      return false;
  return true;
};

void pixel_position_on_squere(int dst[2], int idx, int layer)
{
  // we'll start from top left, and continue clockwise
  int side        = idx / (2 * layer);
  int idx_on_side = idx % (2 * layer);

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

} // namespace

struct RgbPixmap
{
  int find_entrence_at_bottom_of_labitynth() const
  {
    for (int distance = 0; distance < 100; ++distance)
    {
      int mid_point = width / 2;

      if (not is_pixel_white(get_pixel(mid_point + distance, height - 1)))
        return mid_point + distance;

      if (not is_pixel_white(get_pixel(mid_point - distance, height - 1)))
        return mid_point - distance;
    }

    return 0;
  }

  void generate_goal(int dst[2]) const
  {
    // we'll be interested in any points in distance from the "n" line at top of the level. the line will span with
    // offsets from sides

    const int top_offset             = 80;
    const int side_offset            = 80;
    const int bottom_offset          = 100;
    const int max_distance_from_line = 40;
    const int vertical_line_length   = height - top_offset - bottom_offset;
    const int horizontal_line_length = width - 2 * (side_offset + max_distance_from_line);
    const int total_line_length      = 2 * vertical_line_length + horizontal_line_length;

    const int random_point_on_line      = rand() % total_line_length;
    const int random_distance_from_line = (rand() % (2 * max_distance_from_line)) - max_distance_from_line;

    int unchecked_goal[2] = {};

    // left vertical line
    if (random_point_on_line < vertical_line_length)
    {
      int x = side_offset + random_distance_from_line;
      int y = height - (bottom_offset + random_point_on_line);

      unchecked_goal[0] = x;
      unchecked_goal[1] = y;
    }
    // center horizontal line
    else if (random_point_on_line < (vertical_line_length + horizontal_line_length))
    {
      int adjusted_distance = random_point_on_line - vertical_line_length;
      int x                 = side_offset + max_distance_from_line + adjusted_distance;
      int y                 = top_offset + random_distance_from_line;

      unchecked_goal[0] = x;
      unchecked_goal[1] = y;
    }
    // right vertical line
    else
    {
      int adjusted_distance = random_point_on_line - (vertical_line_length + horizontal_line_length);
      int x                 = width - (side_offset + random_distance_from_line);
      int y                 = height - (top_offset + adjusted_distance);

      unchecked_goal[0] = x;
      unchecked_goal[1] = y;
    }

    int initial_goal[2] = {unchecked_goal[0], unchecked_goal[1]};

    const int searchbox_diameter = 5;
    for (int layer = 1; layer < searchbox_diameter; ++layer)
    {
      int layer_length = 4 * (2 * layer);
      for (int idx = 0; idx < layer_length; ++idx)
      {
        int position[2];
        pixel_position_on_squere(position, idx, layer);

        if (not is_pixel_white(get_pixel(unchecked_goal[0] + position[0], unchecked_goal[1] + position[1])))
        {
          dst[0] = unchecked_goal[0] + position[0];
          dst[1] = unchecked_goal[1] + position[1];
          return;
        }
      }
    }

    dst[0] = unchecked_goal[0];
    dst[1] = unchecked_goal[1];
  }

  void allocate_pixels()
  {
    pixels = reinterpret_cast<uint8_t*>(SDL_malloc(static_cast<size_t>(3 * width * height)));
  }

  void generate_herringbone_wang(stbhw_tileset* ts)
  {
    stbhw_generate_image(ts, nullptr, pixels, width * 3, width, height);
  }

  SDL_Surface* create_surface() const
  {
    return SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, 1, 3 * width, SDL_PIXELFORMAT_RGB24);
  }

  uint8_t* get_pixel(int x, int y) const
  {
    return &pixels[(y * 3 * width) + (3 * x)];
  }

  uint8_t* pixels;
  int      width;
  int      height;
};

int main(int argc, char* argv[])
{
  SDL_Init(SDL_INIT_VIDEO);

  stbhw_tileset ts = {};

  {
    int      w, h;
    uint8_t* data = stbi_load("../assets/template_horizontal_corridors_v2.png", &w, &h, nullptr, 3);

    stbhw_build_tileset_from_image(&ts, data, w * 3, w, h);
    free(data);
  }

  srand(SDL_GetTicks());

  RgbPixmap pixmap = {};
  pixmap.width     = 500;
  pixmap.height    = 300;
  pixmap.allocate_pixels();
  pixmap.generate_herringbone_wang(&ts);

  int           entrance = pixmap.find_entrence_at_bottom_of_labitynth();
  SDL_Surface*  surface  = pixmap.create_surface();
  SDL_Window*   window   = SDL_CreateWindow("Image viewer", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                        2 * pixmap.width, 2 * pixmap.height, 0);
  SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
  SDL_Texture*  texture  = SDL_CreateTextureFromSurface(renderer, surface);

  SDL_RenderCopy(renderer, texture, nullptr, nullptr);
  SDL_RenderSetScale(renderer, 5.0, 5.0);
  SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
  SDL_RenderDrawPoint(renderer, (2 * entrance) / 5.0, (2 * (pixmap.height - 1)) / 5.0);
  SDL_RenderPresent(renderer);

  while (!SDL_QuitRequested())
  {
    bool quit = false;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (SDL_KEYDOWN == event.type)
      {
        switch (event.key.keysym.scancode)
        {
        case SDL_SCANCODE_ESCAPE:
        {

          quit = true;
        }
        break;
        case SDL_SCANCODE_R:
        {
          pixmap.generate_herringbone_wang(&ts);
          entrance = pixmap.find_entrence_at_bottom_of_labitynth();

          SDL_FreeSurface(surface);
          surface = pixmap.create_surface();

          SDL_DestroyTexture(texture);
          texture = SDL_CreateTextureFromSurface(renderer, surface);

          SDL_RenderCopy(renderer, texture, nullptr, nullptr);

          SDL_RenderSetScale(renderer, 5.0, 5.0);
          SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
          SDL_RenderDrawPoint(renderer, (2 * entrance) / 5.0, (2 * (pixmap.height - 1)) / 5.0);

          int goal[2];
          pixmap.generate_goal(goal);

          SDL_RenderSetScale(renderer, 10.0, 10.0);
          SDL_RenderDrawPoint(renderer, (2 * goal[0]) / 10.0, (2 * (goal[1] - 1)) / 10.0);
          SDL_RenderPresent(renderer);
        }
        break;
        }
      }
    }

    if (quit)
    {
      SDL_Event event;
      event.type = SDL_QUIT;
      SDL_PushEvent(&event);
    }

    SDL_Delay(50);
  }

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_FreeSurface(surface);
  SDL_free(pixmap.pixels);
  stbhw_free_tileset(&ts);
  SDL_Quit();

  return 0;
}
