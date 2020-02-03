#pragma once

//
// https://www.happyhues.co/palettes/
//

struct Palette
{
  struct RGB
  {
    RGB() = default;
    constexpr RGB(int r, int g, int b)
        : r(r)
        , g(g)
        , b(b)
    {
    }

    int r, g, b;
  };

  RGB background;
  RGB headline;
  RGB paragraph;
  RGB button;
  RGB button_text;
  RGB stroke;
  RGB main;
  RGB highlight;
  RGB secondary;
  RGB tertiary;

  static Palette generate_happyhue_3();
  static Palette generate_happyhue_13();
};
