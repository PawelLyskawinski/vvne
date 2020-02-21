#define SDL_MAIN_HANDLED
#include "story_components.hh"
#include <SDL2/SDL.h>
#include <json.h>

const char* test = R"({
  "target_positions": [
    {"position": [0.0, 0.0, 0.0], "radius": 1.0},
    {"position": [1.0, 0.0, 0.0], "radius": 1.0},
    {"position": [-1.0, 0.0, 0.0], "radius": 1.0}
  ],
  "dialogues": [
    {"type": "Short", "color": [1.0, 0.0, 0.0],
      "size": 800.0, "text": "Hello World! Welcome to vvne demo stage\nFind 3 hidden objectives"
    },
    {"type": "Short", "color": [1.0, 0.0, 0.0],
      "size": 800.0, "text": "Checkpoint 1"
    },
    {"type": "Short", "color": [1.0, 0.0, 0.0],
      "size": 800.0, "text": "Checkpoint 2"
    },
    {"type": "Short", "color": [1.0, 0.0, 0.0],
      "size": 800.0, "text": "Checkpoint 3"
    },
    {"type": "Short", "color": [1.0, 0.0, 0.0],
      "size": 800.0, "text": "All checkpoints found!\nThanks for playing"
    }
  ]
})";

int main()
{
    SDL_Log("%s", test);

    json_value_s*  root        = json_parse(test, SDL_strlen(test));
    json_object_s* main_object = reinterpret_cast<json_object_s*>(root->payload);
    SDL_Log("len: %u (should be 2)", static_cast<uint32_t>(main_object->length));

    for (json_object_element_s* subobject = main_object->start; nullptr != subobject; subobject = subobject->next)
    {
        if (0 == SDL_strncmp("target_positions", subobject->name->string, 256))
        {
            json_array_s* as_array = reinterpret_cast<json_array_s*>(subobject->value);
            SDL_Log("[target_positions] array has %u elements", static_cast<uint32_t>(as_array->length));
            for (json_array_element_s* array_it = as_array->start; nullptr != array_it; ++array_it)
            {
                array_it->value
            }
        }
    }

    free(root);

    return 0;
}