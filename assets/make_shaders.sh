#!/bin/bash

function compile {
  ~/Pobrane/VulkanSDK/1.1.70.1/x86_64/bin/glslangValidator -V $1 -o ../bin/$1.spv
}

compile imgui.frag
compile imgui.vert
compile triangle_push.frag
compile triangle_push.vert
compile skybox.frag
compile skybox.vert
