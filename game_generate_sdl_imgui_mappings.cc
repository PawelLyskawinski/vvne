#include "game.hh"

ArrayView<KeyMapping> generate_sdl_imgui_keymap(Engine::DoubleEndedStack& allocator)
{
  ArrayView<KeyMapping> r{};
  r.count = 21;
  r.data  = allocator.allocate_back<KeyMapping>(r.count);

  KeyMapping* mapping = r.data;

  // :: 0
  mapping->imgui = ImGuiKey_Tab;
  mapping->sdl   = SDL_SCANCODE_TAB;
  mapping++;

  // :: 1
  mapping->imgui = ImGuiKey_LeftArrow;
  mapping->sdl   = SDL_SCANCODE_LEFT;
  mapping++;

  // :: 2
  mapping->imgui = ImGuiKey_RightArrow;
  mapping->sdl   = SDL_SCANCODE_RIGHT;
  mapping++;

  // :: 3
  mapping->imgui = ImGuiKey_UpArrow;
  mapping->sdl   = SDL_SCANCODE_UP;
  mapping++;

  // :: 4
  mapping->imgui = ImGuiKey_DownArrow;
  mapping->sdl   = SDL_SCANCODE_DOWN;
  mapping++;

  // :: 5
  mapping->imgui = ImGuiKey_PageUp;
  mapping->sdl   = SDL_SCANCODE_PAGEUP;
  mapping++;

  // :: 6
  mapping->imgui = ImGuiKey_PageDown;
  mapping->sdl   = SDL_SCANCODE_PAGEDOWN;
  mapping++;

  // :: 7
  mapping->imgui = ImGuiKey_Home;
  mapping->sdl   = SDL_SCANCODE_HOME;
  mapping++;

  // :: 8
  mapping->imgui = ImGuiKey_End;
  mapping->sdl   = SDL_SCANCODE_END;
  mapping++;

  // :: 9
  mapping->imgui = ImGuiKey_Insert;
  mapping->sdl   = SDL_SCANCODE_INSERT;
  mapping++;

  // :: 10
  mapping->imgui = ImGuiKey_Delete;
  mapping->sdl   = SDL_SCANCODE_DELETE;
  mapping++;

  // :: 11
  mapping->imgui = ImGuiKey_Backspace;
  mapping->sdl   = SDL_SCANCODE_BACKSPACE;
  mapping++;

  // :: 12
  mapping->imgui = ImGuiKey_Space;
  mapping->sdl   = SDL_SCANCODE_SPACE;
  mapping++;

  // :: 13
  mapping->imgui = ImGuiKey_Enter;
  mapping->sdl   = SDL_SCANCODE_RETURN;
  mapping++;

  // :: 14
  mapping->imgui = ImGuiKey_Escape;
  mapping->sdl   = SDL_SCANCODE_ESCAPE;
  mapping++;

  // :: 15
  mapping->imgui = ImGuiKey_A;
  mapping->sdl   = SDL_SCANCODE_A;
  mapping++;

  // :: 16
  mapping->imgui = ImGuiKey_C;
  mapping->sdl   = SDL_SCANCODE_C;
  mapping++;

  // :: 17
  mapping->imgui = ImGuiKey_V;
  mapping->sdl   = SDL_SCANCODE_V;
  mapping++;

  // :: 18
  mapping->imgui = ImGuiKey_X;
  mapping->sdl   = SDL_SCANCODE_X;
  mapping++;

  // :: 19
  mapping->imgui = ImGuiKey_Y;
  mapping->sdl   = SDL_SCANCODE_Y;
  mapping++;

  // :: 20
  mapping->imgui = ImGuiKey_Z;
  mapping->sdl   = SDL_SCANCODE_Z;

  return r;
}

ArrayView<CursorMapping> generate_sdl_imgui_cursormap(Engine::DoubleEndedStack& allocator)
{
  ArrayView<CursorMapping> r{};
  r.count = 7;
  r.data  = allocator.allocate_back<CursorMapping>(r.count);

  CursorMapping* mapping = r.data;

  mapping->imgui = ImGuiMouseCursor_Arrow;
  mapping->sdl   = SDL_SYSTEM_CURSOR_ARROW;
  mapping++;

  mapping->imgui = ImGuiMouseCursor_TextInput;
  mapping->sdl   = SDL_SYSTEM_CURSOR_IBEAM;
  mapping++;

  mapping->imgui = ImGuiMouseCursor_ResizeAll;
  mapping->sdl   = SDL_SYSTEM_CURSOR_SIZEALL;
  mapping++;

  mapping->imgui = ImGuiMouseCursor_ResizeNS;
  mapping->sdl   = SDL_SYSTEM_CURSOR_SIZENS;
  mapping++;

  mapping->imgui = ImGuiMouseCursor_ResizeEW;
  mapping->sdl   = SDL_SYSTEM_CURSOR_SIZEWE;
  mapping++;

  mapping->imgui = ImGuiMouseCursor_ResizeNESW;
  mapping->sdl   = SDL_SYSTEM_CURSOR_SIZENESW;
  mapping++;

  mapping->imgui = ImGuiMouseCursor_ResizeNWSE;
  mapping->sdl   = SDL_SYSTEM_CURSOR_SIZENWSE;

  return r;
}
