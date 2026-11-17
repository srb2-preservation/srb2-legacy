# Precompiles the game's shaders for the SDL GPU renderer.
#
# Needs shaderc and SPIRV-Cross (its C API, spirv-cross-c-shared or spirv-cross-c).
#
#   make        rewrites precompiled/spirv.c (Vulkan) and precompiled/msl.c (Metal)
#   make dxil DXC=/path/to/dxc
#               rewrites precompiled/dxil.c (Direct3D 12), with
#               Microsoft's DXC, whose DXIL validator (libdxil.so or dxil.dll)
#               must be next to it. The GitHub Actions workflow
#               sdlgpu-shaders.yml does this and uploads the result.
#
# Run it after changing the shaders in src/hardware/hw_shaders.h, the
# renderer's own shaders in gpu_glsl.h, or how they are translated (gpu_glsl.c).

GPUDIR=../../src/hardware/r_sdlgpu

PKG_CONFIG?=pkg-config
# SPIRV-Cross's shared library, or else the static one (like Homebrew's),
# whose .pc leaves out the other SPIRV-Cross libraries and the C++ runtime
SPIRV_CROSS:=$(shell $(PKG_CONFIG) --exists spirv-cross-c-shared && echo spirv-cross-c-shared || echo spirv-cross-c)
SHADER_CFLAGS:=$(shell $(PKG_CONFIG) --cflags shaderc $(SPIRV_CROSS))
SHADER_LIBS:=$(shell $(PKG_CONFIG) --libs shaderc $(SPIRV_CROSS))
ifeq ($(SPIRV_CROSS),spirv-cross-c)
SHADER_LIBS+=-lspirv-cross-msl -lspirv-cross-hlsl -lspirv-cross-cpp -lspirv-cross-reflect \
	-lspirv-cross-glsl -lspirv-cross-core $(if $(filter Darwin,$(shell uname -s)),-lc++,-lstdc++)
endif

all: gen
	./gen --spirv $(GPUDIR)/precompiled/spirv.c --msl $(GPUDIR)/precompiled/msl.c

dxil: gen
	@test -n "$(DXC)" || { echo "give the path to DXC: make dxil DXC=..."; exit 1; }
	./gen --dxil "$(DXC)" $(GPUDIR)/precompiled/dxil.c

gen: gen.c $(GPUDIR)/gpu_glsl.c $(GPUDIR)/gpu_glsl.h ../../src/hardware/hw_shaders.h
	$(CC) -O1 -Wall -DHAVE_SHADERC -DHAVE_SPIRV_CROSS $(SHADER_CFLAGS) -o $@ gen.c $(GPUDIR)/gpu_glsl.c $(SHADER_LIBS)

clean:
	rm -f gen

.PHONY: all dxil clean
