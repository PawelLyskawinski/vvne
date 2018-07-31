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
call:compile green_gui.frag
call:compile green_gui.vert
call:compile green_gui_weapon_selector_box_left.frag
call:compile green_gui_weapon_selector_box_left.vert
call:compile green_gui_weapon_selector_box_right.frag
call:compile green_gui_weapon_selector_box_right.vert
call:compile green_gui_lines.frag
call:compile green_gui_lines.vert
call:compile green_gui_sdf.frag
call:compile green_gui_sdf.vert
call:compile green_gui_triangle.frag
call:compile green_gui_triangle.vert
call:compile green_gui_radar_dots.frag
call:compile green_gui_radar_dots.vert
pause
goto:eof

:compile
C:/VulkanSDK/1.1.77.0/Bin/glslangValidator.exe -V %~1 -o ../bin/%~1.spv

goto:eof