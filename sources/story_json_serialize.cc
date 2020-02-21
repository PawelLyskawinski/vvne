#include "story_json_serialize.hh"
#include "story_editor.hh"
#include <SDL2/SDL_log.h>
#include <json.h>

#include <algorithm>
#include <vector>

//
// ------------ WARNING!! ------------
// This is only TEMPORARY implementation because I wanted to get it up to speed VERY FAST.
// Any STL container which is visible here will be either:
// - only local to this function
// - removed after implementation is done
//
// Node: This function will be used only during development AND only ocassionally, so
//       any overhead it generates will be minor in scope. Should not affect anything else.
//

namespace story {

namespace {

const char* type_to_string(const Dialogue::Type& type)
{
  switch (type)
  {
  case Dialogue::Type::Short:
    return "Short";
  case Dialogue::Type::Long:
    return "Long";
  default:
    return "N/A";
  }
}

struct JsonNumber
{
  explicit JsonNumber(uint32_t number)
      : number_as_string{}
      , number_as_json{number_as_string, static_cast<size_t>(SDL_snprintf(number_as_string, 8, "%u", number))}
  {
  }

  char          number_as_string[32] = {};
  json_number_s number_as_json       = {};
};

struct DialogueObject
{
  explicit DialogueObject(const Dialogue& dialogue);

  json_string_s         entity_name_s;
  JsonNumber            entity_n;
  json_value_s          entity_v;
  json_string_s         type_name_s;
  json_string_s         type_s;
  json_value_s          type_v;
  json_string_s         text_name_s;
  json_string_s         text_s;
  json_value_s          text_v;
  json_object_element_s text_element;
  json_object_element_s type_element;
  json_object_element_s entity_element;
  json_object_s         object;
};

DialogueObject::DialogueObject(const Dialogue& dialogue)
    : entity_name_s{"entity", 6}
    , entity_n(dialogue.entity)
    , entity_v{&entity_n.number_as_json, json_type_number}
    , type_name_s{"type", 4}
    , type_s{type_to_string(dialogue.type), SDL_strlen(type_to_string(dialogue.type))}
    , type_v{&type_s, json_type_string}
    , text_name_s{"text", 4}
    , text_s{dialogue.text, SDL_strlen(dialogue.text)}
    , text_v{&text_s, json_type_string}
    , text_element{&text_name_s, &text_v, nullptr}
    , type_element{&type_name_s, &type_v, &text_element}
    , entity_element{&entity_name_s, &entity_v, &type_element}
    , object{&type_element, 2}
{
}

struct DialoguesArray
{
  explicit DialoguesArray(std::vector<DialogueObject>& objects);

  static std::vector<json_value_s>         convert_to_values(std::vector<DialogueObject>& objects);
  static std::vector<json_array_element_s> convert_to_array_elements(std::vector<json_value_s>& values);
  static void                              form_linked_list(std::vector<json_array_element_s>& elements);

  std::vector<json_value_s>         element_values;
  std::vector<json_array_element_s> elements;
  json_array_s                      array;
};

std::vector<json_value_s> DialoguesArray::convert_to_values(std::vector<DialogueObject>& objects)
{
  std::vector<json_value_s> result;
  result.reserve(objects.size());
  auto dialogue_object_to_json_value = [](DialogueObject& it) { return json_value_s{&it.object, json_type_object}; };
  std::transform(objects.begin(), objects.end(), std::back_inserter(result), dialogue_object_to_json_value);
  return result;
}

std::vector<json_array_element_s> DialoguesArray::convert_to_array_elements(std::vector<json_value_s>& values)
{
  std::vector<json_array_element_s> result;
  result.reserve(values.size());
  auto value_to_array_element = [](json_value_s& it) { return json_array_element_s{&it}; };
  std::transform(values.begin(), values.end(), std::back_inserter(result), value_to_array_element);
  form_linked_list(result);
  return result;
}

void DialoguesArray::form_linked_list(std::vector<json_array_element_s>& elements)
{
  if (not elements.empty())
  {
    for (uint32_t i = 0; i < (elements.size() - 1); ++i)
    {
      elements[i].next = &elements[i + 1];
    }
    elements.rbegin()->next = nullptr;
  }
}

DialoguesArray::DialoguesArray(std::vector<DialogueObject>& objects)
    : element_values(convert_to_values(objects))
    , elements(convert_to_array_elements(element_values))
    , array{elements.data(), elements.size()}
{
}

std::vector<DialogueObject> convert_story_dialogues(const Dialogue dialogues[], const uint32_t count)
{
  std::vector<DialogueObject> result;
  result.reserve(count);
  for (uint32_t i = 0; i < count; ++i)
  {
    result.emplace_back(dialogues[i]);
  }
  return result;
}

} // namespace

void serialize_to_file(SDL_RWops* handle, const StoryEditor& editor)
{
  std::vector<DialogueObject> json_dialogue_objects = convert_story_dialogues(editor.dialogues, editor.dialogues_count);
  DialoguesArray              json_dialogues        = DialoguesArray(json_dialogue_objects);
  json_value_s                sub_value             = {&json_dialogues.array, json_type_array};
  json_string_s               sub_string            = {"dialogues", SDL_strlen("dialogues")};
  json_object_element_s       element               = {&sub_string, &sub_value, nullptr};
  json_object_s               object                = {&element, 1};

  json_value_s final = {
      .payload = &object,
      .type    = json_type_object,
  };

  size_t size       = 0;
  char*  serialized = reinterpret_cast<char*>(json_write_pretty(&final, nullptr, nullptr, &size));
  SDL_RWwrite(handle, serialized, size, 1);
  free(serialized);
}

} // namespace story
