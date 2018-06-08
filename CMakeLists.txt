cmake_minimum_required(VERSION 3.9)
project(vvne CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fno-exceptions -fno-rtti -Wall")# -Wextra -Werror")

include_directories(${CMAKE_CURRENT_LIST_DIR}/thirdparty)

if(WIN32)
include_directories(C:/Users/pawel.lyskawinski/Downloads/MinGW/include
                    C:/VulkanSDK/1.0.65.0/Include)

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

add_executable(${PROJECT_NAME}
        main.cc
        engine.hh
        engine.cc
        game.hh
        game.cc
        gltf.hh
        gltf.cc
        cubemap.cc
        cubemap.hh
        utility.hh
        level_generator_vr.cc
        level_generator_vr.hh
        thirdparty/imconfig.h
        thirdparty/imgui.cpp
        thirdparty/imgui.h
        thirdparty/imgui_draw.cpp
        thirdparty/imgui_internal.h
        thirdparty/linmath.h
		)

target_link_libraries(${PROJECT_NAME} ${SDL_LIBRARY} ${VULKAN_LIBRARY})
target_link_libraries(tests ${SDL_LIBRARY})
target_link_libraries(level_generator_tests ${SDL_LIBRARY})

