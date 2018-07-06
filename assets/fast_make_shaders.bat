@echo off

call:compile green_gui_sdf.frag
call:compile green_gui_sdf.vert
goto:eof

:compile
C:/VulkanSDK/1.0.65.0/Bin/glslangValidator.exe -V %~1 -o ../bin/%~1.spv
goto:eof