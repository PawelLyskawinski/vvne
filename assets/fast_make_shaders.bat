@echo off

call:compile green_gui.frag
call:compile green_gui.vert
goto:eof

:compile
C:/VulkanSDK/1.1.77.0/Bin/glslangValidator.exe -V %~1 -o ../bin/%~1.spv
goto:eof