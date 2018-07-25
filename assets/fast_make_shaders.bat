@echo off

call:compile green_gui_triangle.frag
call:compile green_gui_triangle.vert
goto:eof

:compile
C:/VulkanSDK/1.1.77.0/Bin/glslangValidator.exe -V %~1 -o ../bin/%~1.spv
goto:eof