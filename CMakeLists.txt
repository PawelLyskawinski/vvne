cmake_minimum_required(VERSION 3.9)
project(vvne CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a -O2 -fno-exceptions -fno-rtti -Wall -pedantic")# -DENABLE_VK_VALIDATION -DSDL_ASSERT_LEVEL=0")

include_directories(${CMAKE_CURRENT_LIST_DIR}/thirdparty)

if(WIN32)
include_directories(C:/Users/pawel.lyskawinski/Downloads/MinGW/include
                    C:/VulkanSDK/1.0.65.0/Include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")
set(SDL_LIBRARY    ${CMAKE_CURRENT_LIST_DIR}/bin/SDL2.dll)
set(VULKAN_LIBRARY C:/Windows/System32/vulkan-1.dll)
else()
include_directories(/usr/include/
                    ${CMAKE_CURRENT_LIST_DIR}/thirdparty)

set(SDL_LIBRARY    /usr/lib/libSDL2-2.0.so)
set(VULKAN_LIBRARY /usr/lib/libvulkan.so)
endif()

add_executable(tests unit_tests/SeekerTests.cc)
add_executable(level_generator_tests unit_tests/LevelGenerator.cc)
add_executable(sandbox unit_tests/sandbox.cc)
add_executable(jobsytem unit_tests/JobSystem.cc)

add_executable(${PROJECT_NAME}
        main.cc
        engine.cc
        game.cc
        game_generate_gui_lines.cc
        game_generate_sdf_font.cc
        game_recalculate_node_transforms.cc
        game_render_entity.cc
        gltf.cc
        cubemap.cc
        pipelines.cc
        render_jobs.cc
        update_jobs.cc
        thirdparty/imgui.cpp
        thirdparty/imgui_draw.cpp
        thirdparty/sha256.cpp
        )

target_link_libraries(${PROJECT_NAME} ${SDL_LIBRARY} ${VULKAN_LIBRARY})
if(WIN32)
target_link_libraries(${PROJECT_NAME}
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive
        )
endif()
target_link_libraries(${PROJECT_NAME} ${SDL_LIBRARY} ${VULKAN_LIBRARY})
target_link_libraries(tests ${SDL_LIBRARY})
target_link_libraries(level_generator_tests ${SDL_LIBRARY})
target_link_libraries(jobsytem ${SDL_LIBRARY})

