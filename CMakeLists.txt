cmake_minimum_required(VERSION 3.9)
project(vvne)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fno-exceptions -fno-rtti -Wall -Wextra -Werror")

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
        gltf_model.cc
        gltf_renderablemodel.cc
        gltf.hh
        thirdparty/imconfig.h
        thirdparty/imgui.cpp
        thirdparty/imgui.h
        thirdparty/imgui_draw.cpp
        thirdparty/imgui_internal.h
        thirdparty/linmath.h
        thirdparty/cJSON.c
        thirdparty/cJSON.h
        thirdparty/cJSON_Utils.c
        thirdparty/cJSON_Utils.h
        assets/triangle_push.vert
        assets/triangle_push.frag
        assets/imgui.vert
        assets/imgui.frag
        assets/skybox.vert
        assets/skybox.frag
		)

if(WIN32)
target_link_libraries(${PROJECT_NAME}
        ${CMAKE_CURRENT_LIST_DIR}/bin/SDL2.dll
        C:/Windows/System32/vulkan-1.dll
        )
else()
target_link_libraries(${PROJECT_NAME}
        ${CMAKE_CURRENT_LIST_DIR}/bin/libSDL2-2.0.so.0
        /usr/lib/x86_64-linux-gnu/libvulkan.so.1
        )
endif()