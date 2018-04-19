#pragma once

struct Engine;
struct Game;

struct CubemapGenerator
{
  const char* filepath;
  Engine*     engine;
  Game*       game;
  int         desired_size[2];

  int generate();
};

struct IrradianceGenerator
{
  int     environment_cubemap_idx;
  Engine* engine;
  Game*   game;
  int     desired_size[2];

  int generate();
};

struct PrefilteredCubemapGenerator
{
  int     environment_cubemap_idx;
  Engine* engine;
  Game*   game;
  int     desired_size[2];

  int generate();
};

int generateBRDFlookup(Engine *engine, int size);
