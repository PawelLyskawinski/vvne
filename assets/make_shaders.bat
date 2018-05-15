@echo off

call:compile imgui.frag
call:compile imgui.vert
call:compile triangle_push.frag
call:compile triangle_push.vert
call:compile skybox.frag
call:compile skybox.vert
call:compile colored_geometry.frag
call:compile colored_geometry.vert
call:compile colored_geometry_skinned.frag
call:compile colored_geometry_skinned.vert
call:compile equirectangular_to_cubemap.frag
call:compile equirectangular_to_cubemap.vert
call:compile cubemap_to_irradiance.frag
call:compile cubemap_to_irradiance.vert
call:compile cubemap_prefiltering.frag
call:compile cubemap_prefiltering.vert
call:compile brdf_compute.frag
call:compile brdf_compute.vert
pause
goto:eof

:compile
C:/VulkanSDK/1.0.65.0/Bin/glslangValidator.exe -V %~1 -o ../bin/%~1.spv
goto:eof