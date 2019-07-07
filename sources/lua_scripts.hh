#pragma once

#include "lua.hpp"
#include <SDL2/SDL_stdinc.h>

struct LuaScripts
{
  void setup(lua_CFunction functions[], const char* names[], uint32_t n);
  void teardown();
  void reload();

  lua_State*  test_script;
  const char* test_script_file_path;
  size_t      test_script_file_size;
};
