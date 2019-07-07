#include "lua_scripts.hh"
#include <SDL2/SDL_log.h>
#include <SDL2/SDL_rwops.h>

static size_t get_file_size(const char* path)
{
  SDL_RWops* handle = SDL_RWFromFile(path, "r");
  size_t     result = SDL_RWsize(handle);
  SDL_RWclose(handle);
  return result;
}

void LuaScripts::setup(lua_CFunction functions[], const char* names[], uint32_t n)
{
  test_script_file_path = "../scripts/render_robot_gui_lines.lua";
  test_script           = luaL_newstate();
  luaL_openlibs(test_script);

  for (uint32_t i = 0; i < n; ++i)
  {
    lua_pushcfunction(test_script, functions[i]);
    lua_setglobal(test_script, names[i]);
  }

  test_script_file_size = get_file_size(test_script_file_path);
  if (LUA_OK != luaL_loadfile(test_script, test_script_file_path))
  {
    SDL_Log("Lua script NOT loaded correctly! (%s)", test_script_file_path);
  }
}

void LuaScripts::teardown()
{
  //
  lua_close(test_script);
}

void LuaScripts::reload()
{
  const size_t size = get_file_size(test_script_file_path);
  if (test_script_file_size != size)
  {
    if (LUA_OK != luaL_loadfile(test_script, test_script_file_path))
    {
      SDL_Log("Lua script NOT loaded correctly! (%s)", test_script_file_path);
    }
    test_script_file_size = size;
  }
}
