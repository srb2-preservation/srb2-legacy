# Precompiles the game's shaders for the SDL GPU renderer.
# Needs libshaderc. Run "make" here after changing the shaders in
# src/hardware/hw_shaders.h, or how they are translated (gpu_glsl.c).

GPUDIR=../../src/hardware/r_sdlgpu

all: gen
	./gen $(GPUDIR)/gpu_precompiled.c

gen: gen.c $(GPUDIR)/gpu_glsl.c $(GPUDIR)/gpu_glsl.h ../../src/hardware/hw_shaders.h
	$(CC) -O1 -Wall -DHAVE_SHADERC -o $@ gen.c $(GPUDIR)/gpu_glsl.c -lshaderc_shared

clean:
	rm -f gen

.PHONY: all clean
