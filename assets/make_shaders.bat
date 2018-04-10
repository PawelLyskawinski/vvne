@echo off

SET compiler=C:/VulkanSDK/1.0.65.0/Bin/glslangValidator.exe

%compiler% -V imgui.frag -o ../bin/imgui.frag.spv
%compiler% -V imgui.vert -o ../bin/imgui.vert.spv

%compiler% -V triangle_push.frag -o ../bin/triangle_push.frag.spv
%compiler% -V triangle_push.vert -o ../bin/triangle_push.vert.spv

%compiler% -V skybox.frag -o ../bin/skybox.frag.spv
%compiler% -V skybox.vert -o ../bin/skybox.vert.spv

%compiler% -V colored_geometry.frag -o ../bin/colored_geometry.frag.spv
%compiler% -V colored_geometry.vert -o ../bin/colored_geometry.vert.spv

pause