@echo off

call:compile triangle_push.frag
call:compile triangle_push.vert
goto:eof

:compile
for /f "usebackq delims=" %%x in (`hasher.py %~1`) do set arg=%%x
C:/VulkanSDK/1.1.77.0/Bin/glslangValidator.exe -V %~1 -o ../bin/%arg%
goto:eof
