#include "game.hh"

ArrayView<KeyMapping> generate_sdl_imgui_keymap(Engine::DoubleEndedStack& allocator)
{
  ArrayView<KeyMapping> r{};
  r.count = 21;
  r.data  = allocator.allocate_back<KeyMapping>(r.count);

  r.data[0]  = {ImGuiKey_Tab, SDL_SCANCODE_TAB};
  r.data[1]  = {ImGuiKey_LeftArrow, SDL_SCANCODE_LEFT};
  r.data[2]  = {ImGuiKey_RightArrow, SDL_SCANCODE_RIGHT};
  r.data[3]  = {ImGuiKey_UpArrow, SDL_SCANCODE_UP};
  r.data[4]  = {ImGuiKey_DownArrow, SDL_SCANCODE_DOWN};
  r.data[5]  = {ImGuiKey_PageUp, SDL_SCANCODE_PAGEUP};
  r.data[6]  = {ImGuiKey_PageDown, SDL_SCANCODE_PAGEDOWN};
  r.data[7]  = {ImGuiKey_Home, SDL_SCANCODE_HOME};
  r.data[8]  = {ImGuiKey_End, SDL_SCANCODE_END};
  r.data[9]  = {ImGuiKey_Insert, SDL_SCANCODE_INSERT};
  r.data[10] = {ImGuiKey_Delete, SDL_SCANCODE_DELETE};
  r.data[11] = {ImGuiKey_Backspace, SDL_SCANCODE_BACKSPACE};
  r.data[12] = {ImGuiKey_Space, SDL_SCANCODE_SPACE};
  r.data[13] = {ImGuiKey_Enter, SDL_SCANCODE_RETURN};
  r.data[14] = {ImGuiKey_Escape, SDL_SCANCODE_ESCAPE};
  r.data[15] = {ImGuiKey_A, SDL_SCANCODE_A};
  r.data[16] = {ImGuiKey_C, SDL_SCANCODE_C};
  r.data[17] = {ImGuiKey_V, SDL_SCANCODE_V};
  r.data[18] = {ImGuiKey_X, SDL_SCANCODE_X};
  r.data[19] = {ImGuiKey_Y, SDL_SCANCODE_Y};
  r.data[20] = {ImGuiKey_Z, SDL_SCANCODE_Z};

  return r;
}

ArrayView<CursorMapping> generate_sdl_imgui_cursormap(Engine::DoubleEndedStack& allocator)
{
  ArrayView<CursorMapping> r{};
  r.count = 7;
  r.data  = allocator.allocate_back<CursorMapping>(r.count);

  r.data[0] = {ImGuiMouseCursor_Arrow, SDL_SYSTEM_CURSOR_ARROW};
  r.data[1] = {ImGuiMouseCursor_TextInput, SDL_SYSTEM_CURSOR_IBEAM};
  r.data[2] = {ImGuiMouseCursor_ResizeAll, SDL_SYSTEM_CURSOR_SIZEALL};
  r.data[3] = {ImGuiMouseCursor_ResizeNS, SDL_SYSTEM_CURSOR_SIZENS};
  r.data[4] = {ImGuiMouseCursor_ResizeEW, SDL_SYSTEM_CURSOR_SIZEWE};
  r.data[5] = {ImGuiMouseCursor_ResizeNESW, SDL_SYSTEM_CURSOR_SIZENESW};
  r.data[6] = {ImGuiMouseCursor_ResizeNWSE, SDL_SYSTEM_CURSOR_SIZENWSE};

  return r;
}
