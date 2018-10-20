@echo off

call:compile colored_model_wireframe.frag
call:compile colored_model_wireframe.vert
goto:eof

:compile
for /f "usebackq delims=" %%x in (`hasher.py %~1`) do set arg=%%x
C:/VulkanSDK/1.1.77.0/Bin/glslangValidator.exe -V %~1 -o ../bin/%arg%
goto:eof
