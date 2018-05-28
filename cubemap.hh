#pragma once

struct Engine;
struct Game;

int generate_cubemap(Engine* engine, Game* game, const char* equirectangular_filepath, int desired_size[2]);
int generate_irradiance_cubemap(Engine* engine, Game* game, int environment_cubemap_idx, int desired_size[2]);
int generate_prefiltered_cubemap(Engine* engine, Game* game, int environment_cubemap_idx, int desired_size[2]);
int generate_brdf_lookup(Engine* engine, int size);
