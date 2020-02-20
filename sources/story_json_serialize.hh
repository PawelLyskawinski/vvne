#pragma once

#include <SDL2/SDL_rwops.h>

namespace story {

struct StoryEditor;

void serialize_to_file(SDL_RWops* handle, const StoryEditor& editor);

} // namespace story

