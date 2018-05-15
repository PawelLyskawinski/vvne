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
compile colored_geometry.frag
compile colored_geometry.vert
compile colored_geometry_skinned.frag
compile colored_geometry_skinned.vert
compile equirectangular_to_cubemap.frag
compile equirectangular_to_cubemap.vert
compile cubemap_to_irradiance.frag
compile cubemap_to_irradiance.vert
compile cubemap_prefiltering.frag
compile cubemap_prefiltering.vert
compile brdf_compute.frag
compile brdf_compute.vert
