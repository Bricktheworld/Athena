#pragma once

enum ColorSpaceName : u32
{
  kRec709,
  kRec2020,

  kCount,
};

struct ColorSpace
{
  f32 red_x   = 0.0f;
  f32 red_y   = 0.0f;
  f32 green_x = 0.0f;
  f32 green_y = 0.0f;
  f32 blue_x  = 0.0f;
  f32 blue_y  = 0.0f;
  f32 white_x = 0.0f;
  f32 white_y = 0.0f;
};

static const ColorSpace kColorSpaces[ColorSpaceName::kCount] =
{
  { 0.6400f, 0.3300f, 0.3000f, 0.6000f, 0.1500f, 0.0600f, 0.3127f, 0.3290f }, // https://en.wikipedia.org/wiki/Rec._709
  { 0.7080f, 0.2920f, 0.1700f, 0.7970f, 0.1310f, 0.0460f, 0.3127f, 0.3290f }, // https://en.wikipedia.org/wiki/Rec._2020
};
