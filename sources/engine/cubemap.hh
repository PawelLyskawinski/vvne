#pragma once

#include "engine.hh"
struct Game;

Texture generate_cubemap(Engine* engine, Game* game, const char* equirectangular_filepath, int desired_size[2]);
Texture generate_irradiance_cubemap(Engine* engine, Game* game, Texture environment_cubemap_idx, int desired_size[2]);
Texture generate_prefiltered_cubemap(Engine* engine, Game* game, Texture environment_cubemap_idx, int desired_size[2]);
Texture generate_brdf_lookup(Engine* engine, int size);
