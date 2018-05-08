# vvne
There are a lot of publicly available engines / demos on github which use vulkan and gltf2.0, but as far as I can tell none of them keeps the implementation stl-free. I'm paranoid about compile times and sadly only way to archive that is by skipping what state of the art c++ has to offer.
### main goals of the project
- [recreation of very inspiring muv luv gif](https://thumbs.gfycat.com/HelplessRealAlbacoretuna-size_restricted.gif)
- very fast compile times (no stl, no rtti, no exceptions, minimal use of templates, compression oriented programming)
- learning experiance!
### feature wishlist
- gltf2.0 animations (full skeletal with morph targets)
### already implemented
- gltf2.0 binary model import, node animations
- integrated imgui
- support for both windows and linux
- custom vulkan memory allocators
- hdr skybox (although not used yet)
- PBR with IBL
- MSAA
### additional external dependencies
- vulkan SDK (libvulkan/vulkan-1, glslang)
- compiled SDL2 library
- "Old Industrial Hall" pack from http://www.hdrlabs.com/sibl/archive.html (too big to include into repo)
