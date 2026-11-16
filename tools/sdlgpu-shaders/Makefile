# Precompiles the game's shaders for the SDL GPU renderer.
#
# Needs shaderc and SPIRV-Cross (its C API, spirv-cross-c-shared).
#
#   make        rewrites gpu_precompiled.c: SPIR-V (Vulkan) and MSL (Metal)
#   make d3d12 DXC=/path/to/dxc
#               rewrites gpu_precompiled_d3d12.c: DXIL (Direct3D 12), with
#               Microsoft's DXC, whose DXIL validator (libdxil.so or dxil.dll)
#               must be next to it. The GitHub Actions workflow
#               sdlgpu-shaders.yml does this and uploads the result.
#
# Run it after changing the shaders in src/hardware/hw_shaders.h, the
# renderer's own shaders in gpu_glsl.h, or how they are translated (gpu_glsl.c).

GPUDIR=../../src/hardware/r_sdlgpu

PKG_CONFIG?=pkg-config
SHADER_CFLAGS:=$(shell $(PKG_CONFIG) --cflags shaderc spirv-cross-c-shared)
SHADER_LIBS:=$(shell $(PKG_CONFIG) --libs shaderc spirv-cross-c-shared)

all: gen
	./gen $(GPUDIR)/gpu_precompiled.c

d3d12: gen
	@test -n "$(DXC)" || { echo "give the path to DXC: make d3d12 DXC=..."; exit 1; }
	./gen --d3d12 "$(DXC)" $(GPUDIR)/gpu_precompiled_d3d12.c

gen: gen.c $(GPUDIR)/gpu_glsl.c $(GPUDIR)/gpu_glsl.h ../../src/hardware/hw_shaders.h
	$(CC) -O1 -Wall -DHAVE_SHADERC -DHAVE_SPIRV_CROSS $(SHADER_CFLAGS) -o $@ gen.c $(GPUDIR)/gpu_glsl.c $(SHADER_LIBS)

clean:
	rm -f gen

.PHONY: all d3d12 clean
