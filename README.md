# vvne

There are a lot of publicly available engines / demos on github which use vulkan and gltf2.0, but as far as I can tell none of them keeps the implementation *stl-free*. I'm paranoid about compile times and sadly only way to archive that is by skipping what state of the art c++ has to offer.

Project aims to recreate a [very inspiring muv luv gif](https://thumbs.gfycat.com/HelplessRealAlbacoretuna-size_restricted.gif) as best as possible.

Current preview (still a veeeery long road ahead)
![status preview image](assets/current_status.jpg)

### Features
- Handmade gltf2.0 glb model importer (nodes, animations, skinning)
- Integrated imgui
- Support for both windows (mingw) and linux
- Custom memory allocators (host and device)
- PBR with IBL (huge thanks to Sascha Willems vulkan examples and https://learnopengl.com/PBR/Theory)
- Signed distance field font rendering
- Multithreaded rendering / job system
- Cascaded shadow mapping

### TODO - engine
short distance goals:
- Parallelized pipeline creation
- Shader preloading and reuse
- Textured tesselated level
- Particle system (engine boost effect)

long distance goals:
- Editor for level design
- Global illumination (got to understand spherical harmonics and light probes first)
- Occluders (AABB trees)

### TODO - game
short distance goals:
- Small AI horde patrol throughout level
- Maintenence create drop call
- Shooting and melee
- Height calculation rework

long distance goals:
- AI combat
- better model and animations (while only using keyframes from glb)

### Used C++ subset
- no stl
- no rtti
- no exceptions
- minimal use of templates
- designated initializers (available in C++ with GCC 8.1 c++2a flag)

### Dependencies
- SDL2 (any version with SDL_vulkan header)
- lunarg vulkan sdk

### How to build?
Modification inside CMakeLists.txt are needed. Depending on which platform is used different paths for vulkan and SDL dynamic libraries may be required.

Shaders can be build using assets/make_shaders scripts, but path to glslangvalidator need to be substituted.
Since shaders are renamed using hashes make sure that 'hashlib' package is available.

GCC 8.1 is nessesary since a brand new standard is used.

Binary should be run inside "bin" folder.
