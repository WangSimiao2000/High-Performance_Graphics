Third Party Software
====================

This code uses the following third party software/source code.

## Premake

- Where: https://premake.github.io/download
- What: Build configuration and generator
- License: BSD 3-Clause

Premake generates platform/toolchain specific build files, such as Visual
Studio project files or standard Makefiles. This project includes prebuilt
binaries for Linux and Windows.

## Volk

- Where: https://github.com/zeux/volk
- What: "Meta-loader for Vulkan"
- License: MIT License

Volk allows applications to dynamically load the Vulkan API without linking
against vulkan-1.dll or libvulkan.so. Furthermore, it enables the loading of
device-specific Vulkan functions, the use of which skips the overhead of
dispatching per-device.

Note: Only the source code (volk.{h,c}) and the license and readme files are
included here. The volk.c source is modified to include the volk.h header
correctly for this project.

## Vulkan Headers

- Where: Vulkan SDK (see https://vulkan.lunarg.com/sdk/home)
- What: Vulkan API header files
- License: Apache 2.0

This is a copy of the necessary Vulkan header files, extracted from the Vulkan
SDK (version 1.3.236). Only the LICENSE.txt, README.txt and a reduced amount of
header files are included.

## STB libraries

- Where: https://github.com/nothings/stb
- What: single-header libraries
- License: Public Domain / MIT License

Collection of single-header libraries by Sean Barrett. Included are:
- stb_image.h         (image loading)
- stb_image_write.h   (image writing)

Note: the files in src/ are custom.

## Shaderc / glslc

- Where: https://github.com/google/shaderc
- What: GLSL to SpirV compiler
- License: Apache v2.0

Note: only pre-built glslc[.exe] compiler binaries for 64-bit Windows,
64-bit Linux and 64-bit MacOS are included here.

The glslc compiler is an offline compiler toolt that accepts (among others)
GLSL sources and compiles these to SpirV code that can be passed to Vulkan.
