//
// Created by vasquez on 03.02.2020.
//

#include "color_palette.hh"

namespace {

constexpr int to_number(const char *v)
{
  auto char_to_number = [](char c) {
    if (('0' <= c) && ('9' >= c))
    {
      return c - '0';
    }
    else
    {
      return 10 + c - 'a';
    }
  };
  return (char_to_number(v[0])) * 16 + char_to_number(v[1]);
}

constexpr Palette::RGB to_RGB(const char* str)
{
  return {to_number(&str[1]), to_number(&str[3]), to_number(&str[5])};
}

} // namespace

Palette Palette::generate_happyhue_3()
{
  return {
      .background  = to_RGB("#fffffe"),
      .headline    = to_RGB("#094067"),
      .paragraph   = to_RGB("#5f6c7b"),
      .button      = to_RGB("#3da9fc"),
      .button_text = to_RGB("#fffffe"),
      .stroke      = to_RGB("#094067"),
      .main        = to_RGB("#fffffe"),
      .highlight   = to_RGB("#3da9fc"),
      .secondary   = to_RGB("#90b4ce"),
      .tertiary    = to_RGB("#ef4565"),
  };
}

Palette Palette::generate_happyhue_13()
{
  return {
      .background  = to_RGB("#0f0e17"),
      .headline    = to_RGB("#fffffe"),
      .paragraph   = to_RGB("#a7a9be"),
      .button      = to_RGB("#ff8906"),
      .button_text = to_RGB("#fffffe"),
      .stroke      = to_RGB("#000000"),
      .main        = to_RGB("#fffffe"),
      .highlight   = to_RGB("#ff8906"),
      .secondary   = to_RGB("#f25f4c"),
      .tertiary    = to_RGB("#e53170"),
  };
}
