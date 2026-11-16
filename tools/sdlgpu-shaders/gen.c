// SRB2 LEGACY
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by SRB2 Preservation.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  gen.c
/// \brief Precompiles the game's shaders for the SDL GPU renderer
///
/// Writes src/hardware/r_sdlgpu/gpu_precompiled.c, which is kept in the
/// repository so that building the game doesn't need a shader compiler.
/// Run this again (see the Makefile next to this file) whenever the shaders in
/// src/hardware/hw_shaders.h or the translation in gpu_glsl.c change.
///
/// The game's shaders change with some settings, which the game does by putting
/// #define lines in front of them (see HWR_PreprocessShader in hw_shaders.c).
/// Every combination is compiled here. The game finds them by hashing the
/// source it would have given to OpenGL, so the lines written here must be
/// exactly the ones the game writes.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/hardware/hw_shaders.h"
#include "../../src/hardware/r_sdlgpu/gpu_glsl.h"

static const struct
{
	const char *name;
	const char *vertex;
	const char *fragment;
} game_shaders[] =
{
	// same as gl_shadersources in hw_shaders.c
	{"Flat", GLSL_DEFAULT_VERTEX_SHADER, GLSL_FLOOR_FRAGMENT_SHADER},
	{"WallTexture", GLSL_DEFAULT_VERTEX_SHADER, GLSL_WALL_FRAGMENT_SHADER},
	{"Sprite", GLSL_DEFAULT_VERTEX_SHADER, GLSL_WALL_FRAGMENT_SHADER},
	{"Model", GLSL_MODEL_VERTEX_SHADER, GLSL_MODEL_FRAGMENT_SHADER},
	{"WaterRipple", GLSL_DEFAULT_VERTEX_SHADER, GLSL_WATER_FRAGMENT_SHADER},
	{"Fog", GLSL_DEFAULT_VERTEX_SHADER, GLSL_FOG_FRAGMENT_SHADER},
	{"Sky", GLSL_DEFAULT_VERTEX_SHADER, GLSL_SKY_FRAGMENT_SHADER},
	{"PalettePostprocess", GLSL_DEFAULT_VERTEX_SHADER, GLSL_PALETTE_POSTPROCESS_FRAGMENT_SHADER},
	{"UIColormapFade", GLSL_DEFAULT_VERTEX_SHADER, GLSL_UI_COLORMAP_FADE_FRAGMENT_SHADER},
}, renderer_shaders[] =
{
	// the renderer's own, which the game doesn't preprocess
	{"plain", GPU_GLSL_PLAIN_VERTEX_SHADER, GPU_GLSL_PLAIN_FRAGMENT_SHADER},
	{"fallback", GPU_GLSL_PLAIN_VERTEX_SHADER, GPU_GLSL_FALLBACK_FRAGMENT_SHADER},
	{"wipe", GPU_GLSL_PLAIN_VERTEX_SHADER, GPU_GLSL_WIPE_FRAGMENT_SHADER},
};

// in the order HWR_PreprocessShader writes them
static const char *const defines[] =
{
	"#define SRB2_MODEL_LIGHTING",
	"#define SRB2_PALETTE_RENDERING",
	"#define SRB2_CURVE_SHADER",
	"#define SRB2_LIGHT_DITHER",
};
#define NUMDEFINES (sizeof (defines) / sizeof (*defines))

// What HWR_PreprocessShader makes of a shader, for a combination of settings.
static char *Preprocess(const char *original, unsigned mask)
{
	char *result = malloc(strlen(original) + 512);
	char *write = result;
	int has_version = !strncmp(original, "#version ", 9);
	size_t i;

	if (!mask)
	{
		strcpy(result, original);
		return result;
	}

	if (has_version)
		write += sprintf(write, "#version %.3s\n", original + 9);

	for (i = 0; i < NUMDEFINES; i++)
		if (mask & (1u << i))
			write += sprintf(write, "%s\n", defines[i]);

	write += sprintf(write, (!has_version || original[9] == '1') ? "#line 0\n" : "#line 1\n");

	strcpy(write, original);
	if (has_version)
		memset(write, ' ', 12); // "#version NNN"

	return result;
}

// --------------------------------------------------------------------------
// The compiled shaders. The same one often comes out of several combinations.
// --------------------------------------------------------------------------

typedef struct
{
	void *data;
	size_t size;
} blob_t;

static blob_t blobs[1024];
static size_t numblobs;

static size_t AddBlob(void *data, size_t size)
{
	size_t i;

	for (i = 0; i < numblobs; i++)
		if (blobs[i].size == size && !memcmp(blobs[i].data, data, size))
		{
			free(data);
			return i;
		}

	blobs[numblobs].data = data;
	blobs[numblobs].size = size;
	return numblobs++;
}

typedef struct
{
	unsigned long long hash;
	size_t vertex, fragment;
	char comment[96];
} program_t;

static program_t programs[1024];
static size_t numprograms;

static int AddProgram(const char *name, unsigned mask, const char *vertex, const char *fragment)
{
	unsigned long long hash = GPU_HashShaderSources(vertex, fragment);
	char *vs, *fs, *log;
	char error[256];
	void *spirv;
	size_t size, i;
	program_t *program;

	for (i = 0; i < numprograms; i++)
		if (programs[i].hash == hash)
			return 1; // Sprite is the same as WallTexture

	if (!GPU_TranslateGLSL(vertex, fragment, &vs, &fs, error, sizeof (error)))
	{
		fprintf(stderr, "%s (%x): %s\n", name, mask, error);
		return 0;
	}

	program = &programs[numprograms];
	program->hash = hash;
	snprintf(program->comment, sizeof (program->comment), "%s%s%s%s%s", name,
		(mask & 1) ? ", model lighting" : "", (mask & 2) ? ", palette rendering" : "",
		(mask & 4) ? ", curve" : "", (mask & 8) ? ", light dither" : "");

	spirv = GPU_CompileGLSL(vs, GPU_STAGE_VERTEX, name, &size, &log);
	if (!spirv)
	{
		fprintf(stderr, "%s (%x), vertex shader:\n%s\n", name, mask, log ? log : "no compiler");
		return 0;
	}
	free(log);
	program->vertex = AddBlob(spirv, size);

	spirv = GPU_CompileGLSL(fs, GPU_STAGE_FRAGMENT, name, &size, &log);
	if (!spirv)
	{
		fprintf(stderr, "%s (%x), fragment shader:\n%s\n", name, mask, log ? log : "no compiler");
		return 0;
	}
	free(log);
	program->fragment = AddBlob(spirv, size);

	free(vs);
	free(fs);
	numprograms++;
	return 1;
}

int main(int argc, char **argv)
{
	FILE *out;
	size_t i, j, total = 0;
	unsigned mask;

	if ((argc == 4 && !strcmp(argv[1], "--translate")) || (argc == 6 && !strcmp(argv[1], "--compile")))
	{
		// For addons, and for looking at what the translation does to a shader:
		//   gen --translate shader.vert shader.frag
		//     prints the shaders as they are given to the compiler
		//   gen --compile shader.vert shader.frag vertex.spv fragment.spv
		//     writes them as SPIR-V, which an addon can have in the "SPIRV" sections of its SHADERS lump
		char *sources[2], *translated[2], error[256];
		for (i = 0; i < 2; i++)
		{
			FILE *in = fopen(argv[2 + i], "rb");
			long len;
			if (!in) { perror(argv[2 + i]); return 1; }
			fseek(in, 0, SEEK_END); len = ftell(in); fseek(in, 0, SEEK_SET);
			sources[i] = calloc(len + 1, 1);
			if (fread(sources[i], 1, len, in) != (size_t)len) return 1;
			fclose(in);
		}
		if (!GPU_TranslateGLSL(sources[0], sources[1], &translated[0], &translated[1], error, sizeof (error)))
		{
			fprintf(stderr, "%s\n", error);
			return 1;
		}
		if (argc == 4)
		{
			printf("%s\n// ---------- fragment ----------\n%s", translated[0], translated[1]);
			return 0;
		}
		for (i = 0; i < 2; i++)
		{
			char *log;
			size_t size;
			void *spirv = GPU_CompileGLSL(translated[i], i ? GPU_STAGE_FRAGMENT : GPU_STAGE_VERTEX, argv[2 + i], &size, &log);
			if (!spirv)
			{
				fprintf(stderr, "%s", log ? log : "no compiler\n");
				return 1;
			}
			out = fopen(argv[4 + i], "wb");
			if (!out || fwrite(spirv, 1, size, out) != size) { perror(argv[4 + i]); return 1; }
			fclose(out);
		}
		return 0;
	}

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <gpu_precompiled.c>\n       %s --translate <vertex shader> <fragment shader>\n       %s --compile <vertex shader> <fragment shader> <vertex SPIR-V> <fragment SPIR-V>\n", argv[0], argv[0], argv[0]);
		return 1;
	}

	for (i = 0; i < sizeof (game_shaders) / sizeof (*game_shaders); i++)
		for (mask = 0; mask < (1u << NUMDEFINES); mask++)
		{
			char *vertex = Preprocess(game_shaders[i].vertex, mask);
			char *fragment = Preprocess(game_shaders[i].fragment, mask);
			if (!AddProgram(game_shaders[i].name, mask, vertex, fragment))
				return 1;
			free(vertex);
			free(fragment);
		}

	for (i = 0; i < sizeof (renderer_shaders) / sizeof (*renderer_shaders); i++)
		if (!AddProgram(renderer_shaders[i].name, 0, renderer_shaders[i].vertex, renderer_shaders[i].fragment))
			return 1;

	out = fopen(argv[1], "w");
	if (!out)
	{
		perror(argv[1]);
		return 1;
	}

	fprintf(out,
		"// SRB2 LEGACY\n"
		"//-----------------------------------------------------------------------------\n"
		"// Copyright (C) 2026 by SRB2 Preservation.\n"
		"//\n"
		"// This program is free software distributed under the\n"
		"// terms of the GNU General Public License, version 2.\n"
		"// See the 'LICENSE' file for more details.\n"
		"//-----------------------------------------------------------------------------\n"
		"/// \\file  gpu_precompiled.c\n"
		"/// \\brief The game's shaders, compiled for the SDL GPU renderer\n"
		"///\n"
		"/// Written by tools/sdlgpu-shaders. Don't edit this, run the tool again.\n"
		"\n"
		"#include \"gpu_glsl.h\"\n"
		"\n");

	for (i = 0; i < numblobs; i++)
	{
		const unsigned char *data = blobs[i].data;
		fprintf(out, "static const unsigned char spirv_%u[%u] =\n{", (unsigned)i, (unsigned)blobs[i].size);
		for (j = 0; j < blobs[i].size; j++)
			fprintf(out, "%s%u,", (j % 32) ? "" : "\n\t", data[j]);
		fprintf(out, "\n};\n\n");
		total += blobs[i].size;
	}

	fprintf(out, "const gpu_precompiled_t gpu_precompiled[] =\n{\n");
	for (i = 0; i < numprograms; i++)
		fprintf(out, "\t{0x%016llxULL, GPU_SHADERFORMAT_SPIRV, spirv_%u, sizeof (spirv_%u), spirv_%u, sizeof (spirv_%u)}, // %s\n",
			programs[i].hash, (unsigned)programs[i].vertex, (unsigned)programs[i].vertex,
			(unsigned)programs[i].fragment, (unsigned)programs[i].fragment, programs[i].comment);
	fprintf(out, "};\n\nconst size_t gpu_num_precompiled = sizeof (gpu_precompiled) / sizeof (*gpu_precompiled);\n");
	fclose(out);

	printf("%u shader programs, %u different shaders, %u bytes of SPIR-V\n", (unsigned)numprograms, (unsigned)numblobs, (unsigned)total);
	return 0;
}
