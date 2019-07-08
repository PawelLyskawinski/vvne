cmake_minimum_required(VERSION 3.9)
project(vvne CXX)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -std=c++2a -g -fno-exceptions -fno-rtti -Wall -DSDL_ASSERT_LEVEL=3")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -std=c++2a -O2 -fno-exceptions -fno-rtti -Wall")

include_directories(
        ${CMAKE_CURRENT_LIST_DIR}/thirdparty
        ${CMAKE_CURRENT_LIST_DIR}/../lua-5.3.5/src/
)

if(WIN32)
include_directories(C:/Users/plyskawinski/Downloads/MinGW/include
                    C:/VulkanSDK/1.1.85.0/Include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libstdc++ -static-libgcc")
set(SDL_LIBRARY    ${CMAKE_CURRENT_LIST_DIR}/bin/SDL2.dll)
set(VULKAN_LIBRARY C:/Windows/System32/vulkan-1.dll)
set(LUA_LIBRARY    ${CMAKE_CURRENT_LIST_DIR}/../lua-5.3.5/src/liblua53.a)
else()
include_directories(/usr/include/
                    ${CMAKE_CURRENT_LIST_DIR}/thirdparty)

set(SDL_LIBRARY    /usr/lib/libSDL2-2.0.so)
set(VULKAN_LIBRARY /usr/lib/libvulkan.so)
set(LUA_LIBRARY    ${CMAKE_CURRENT_LIST_DIR}/../lua-5.3.5/src/liblua.a)
endif()

add_executable(tests unit_tests/SeekerTests.cc)
add_executable(level_generator_tests unit_tests/LevelGenerator.cc)
add_executable(sandbox unit_tests/sandbox.cc)
add_executable(jobsytem unit_tests/JobSystem.cc)
add_Executable(allocator_tests unit_tests/AllocatorTest.cc sources/engine/free_list_allocator.cc)

set(SOURCES
        sources/main.cc
        sources/engine/engine.cc
        sources/engine/engine_render_passes.cc
        sources/engine/engine_framebuffers.cc
        sources/engine/engine_descriptor_set_layouts.cc
        sources/engine/engine_pipeline_layouts.cc
        sources/engine/engine_pipelines.cc
        sources/engine/free_list_allocator.cc
        sources/engine/free_list_visualizer.cc
        sources/engine/gpu_memory_allocator.cc
        sources/engine/gpu_memory_visualizer.cc
        sources/engine/job_system.cc
        sources/engine/fft_water.cc
        sources/engine/instance_creation.cc
        sources/engine/debug_callback_creation.cc
        sources/engine/select_physical_device.cc
        sources/engine/select_graphics_family_index.cc
        sources/engine/device_creation.cc
        sources/game.cc
        sources/game_generate_gui_lines.cc
        sources/game_generate_sdf_font.cc
        sources/game_render_entity.cc
        sources/engine/gltf.cc
        sources/engine/cubemap.cc
        sources/render_jobs.cc
        sources/update_jobs.cc
        sources/SimpleEntity.cc
        sources/player.cc
        sources/terrain_as_a_function.cc
        sources/profiler.cc
        sources/profiler_visualizer.cc
        sources/levels/example_level.cc
        sources/materials.cc
        sources/debug_gui.cc
        thirdparty/imgui.cpp
        thirdparty/imgui_draw.cpp
        thirdparty/imgui_widgets.cpp
        thirdparty/sha256.cpp
        sources/lua_scripts.cc sources/lua_scripts.hh)

if(WIN32)
add_executable(${PROJECT_NAME} ${SOURCES} assets/applicationIcon.rc)
else()
add_executable(${PROJECT_NAME} ${SOURCES})
endif()

if(WIN32)
target_link_libraries(${PROJECT_NAME}
        -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive
        )
else()
target_link_libraries(${PROJECT_NAME}
         -ldl
        )
endif()
target_link_libraries(${PROJECT_NAME} ${SDL_LIBRARY} ${VULKAN_LIBRARY} ${LUA_LIBRARY})
target_link_libraries(tests ${SDL_LIBRARY})
target_link_libraries(level_generator_tests ${SDL_LIBRARY})
target_link_libraries(jobsytem ${SDL_LIBRARY})
target_link_libraries(allocator_tests ${SDL_LIBRARY})

