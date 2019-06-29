@echo off

call:compile tesselated_ground.frag
call:compile tesselated_ground.vert
call:compile tesselated_ground.tesc
call:compile tesselated_ground.tese
call:compile debug_billboard_texture_array.frag
call:compile debug_billboard_texture_array.vert
call:compile debug_billboard.frag
call:compile debug_billboard.vert
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
call:compile green_gui.frag
call:compile green_gui.vert
call:compile green_gui_weapon_selector_box.frag
call:compile green_gui_weapon_selector_box.vert
call:compile green_gui_lines.frag
call:compile green_gui_lines.vert
call:compile green_gui_sdf.frag
call:compile green_gui_sdf.vert
call:compile green_gui_triangle.frag
call:compile green_gui_triangle.vert
call:compile green_gui_radar_dots.frag
call:compile green_gui_radar_dots.vert
call:compile pbr_water.frag
call:compile pbr_water.vert
call:compile depth_pass.frag
call:compile depth_pass.vert
call:compile colored_model_wireframe.frag
call:compile colored_model_wireframe.vert
pause
goto:eof

:compile
for /f "usebackq delims=" %%x in (`hasher.py %~1`) do set arg=%%x
C:/VulkanSDK/1.1.106.0/Bin/glslangValidator.exe -V %~1 -o ../bin/%arg%
goto:eof