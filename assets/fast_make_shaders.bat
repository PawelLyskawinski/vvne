@echo off

call:compile pbr_water.frag
call:compile pbr_water.vert
goto:eof

:compile
:: C:/VulkanSDK/1.1.77.0/Bin/glslangValidator.exe -V %~1 -o ../bin/%~1.spv
C:/VulkanSDK/1.1.77.0/Bin/glslangValidator.exe -V %~1
goto:eof