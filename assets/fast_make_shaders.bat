@echo off

call:compile green_gui_weapon_selector_box_left.frag
call:compile green_gui_weapon_selector_box_left.vert
goto:eof

:compile
C:/VulkanSDK/1.1.77.0/Bin/glslangValidator.exe -V %~1 -o ../bin/%~1.spv
goto:eof