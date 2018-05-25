cmake_minimum_required(VERSION 3.9)
project(vvne)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2 -fno-exceptions -fno-rtti")# -Wall -Wextra -Werror")

if(WIN32)
include_directories(
        C:/Users/pawel.lyskawinski/Downloads/MinGW/include
        C:/VulkanSDK/1.0.65.0/Include
        ${CMAKE_CURRENT_LIST_DIR}/thirdparty
        )
else()
include_directories(
	    ~/Pobrane/VulkanSDK/1.1.70.1/x86_64/include
        ${CMAKE_CURRENT_LIST_DIR}/thirdparty
        )
endif()

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

if(WIN32)
target_link_libraries(${PROJECT_NAME}
        ${CMAKE_CURRENT_LIST_DIR}/bin/SDL2.dll
        C:/Windows/System32/vulkan-1.dll
        )
else()
target_link_libraries(${PROJECT_NAME}
        ${CMAKE_CURRENT_LIST_DIR}/bin/libSDL2-2.0.so.0
        ~/Pobrane/VulkanSDK/1.1.70.1/x86_64/lib/libvulkan.so
        )
endif()

add_executable(tests unit_tests/SeekerTests.cc)
if(WIN32)
target_link_libraries(tests ${CMAKE_CURRENT_LIST_DIR}/bin/SDL2.dll)
else()
target_link_libraries(tests ${CMAKE_CURRENT_LIST_DIR}/bin/libSDL2-2.0.so.0)
endif()

add_executable(level_generator_tests unit_tests/LevelGenerator.cc)
if(WIN32)
target_link_libraries(level_generator_tests ${CMAKE_CURRENT_LIST_DIR}/bin/SDL2.dll)
else()
target_link_libraries(level_generator_tests ${CMAKE_CURRENT_LIST_DIR}/bin/libSDL2-2.0.so.0)
endif()

add_executable(sandbox unit_tests/sandbox.cc)
