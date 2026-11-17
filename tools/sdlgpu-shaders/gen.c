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
/// Writes spirv.c (Vulkan), msl.c (Metal) and dxil.c (Direct3D 12) in
/// src/hardware/r_sdlgpu/precompiled, which are kept in the repository so that
/// building the game doesn't need any shader tools. The Direct3D 12 file
/// needs Microsoft's DXC, so it is made separately (see the Makefile).
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
#include <unistd.h> // mkdtemp on macOS

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

static blob_t blobs[4096];
static size_t numblobs;

#define NOBLOB ((size_t)-1)

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
	size_t blob[GPU_NUMSHADERFORMATS][2]; // by format and stage, NOBLOB if there is none
	char comment[96];
} program_t;

static program_t programs[1024];
static size_t numprograms;

// what DXC to use to make DXIL, if any
static const char *dxc = NULL;
// whether MSL is made
static int msl = 0;
static const char *workdir = NULL;

static void *ReadFile(const char *path, size_t *size)
{
	FILE *in = fopen(path, "rb");
	char *data;
	long len;

	if (!in)
		return NULL;
	fseek(in, 0, SEEK_END);
	len = ftell(in);
	fseek(in, 0, SEEK_SET);
	data = calloc(len + 1, 1);
	if (data && fread(data, 1, len, in) != (size_t)len)
	{
		free(data);
		data = NULL;
	}
	fclose(in);
	*size = (size_t)len;
	return data;
}

// HLSL to DXIL, with DXC
static void *CompileHLSL(const char *hlsl, gpu_stage_t stage, const char *name, size_t *size)
{
	char source[1024], output[1024], command[4096];
	FILE *out;
	void *dxil;

	snprintf(source, sizeof (source), "%s/shader.hlsl", workdir);
	snprintf(output, sizeof (output), "%s/shader.dxil", workdir);

	out = fopen(source, "w");
	if (!out || fputs(hlsl, out) < 0)
	{
		perror(source);
		return NULL;
	}
	fclose(out);
	remove(output);

	snprintf(command, sizeof (command), "\"%s\" -nologo -T %s -E main -Fo \"%s\" \"%s\"",
		dxc, (stage == GPU_STAGE_VERTEX) ? "vs_6_0" : "ps_6_0", output, source);
	if (system(command) != 0)
	{
		fprintf(stderr, "%s: DXC failed on this HLSL:\n%s\n", name, hlsl);
		return NULL;
	}

	dxil = ReadFile(output, size);
	if (!dxil || *size < 20)
	{
		fprintf(stderr, "%s: DXC wrote nothing\n", name);
		return NULL;
	}

	// Direct3D 12 only takes DXIL signed by the validator, which leaves a hash after "DXBC".
	{
		const unsigned char *digest = (const unsigned char *)dxil + 4;
		int i, signed_ = 0;
		for (i = 0; i < 16; i++)
			if (digest[i])
				signed_ = 1;
		if (!signed_)
		{
			fprintf(stderr, "%s: DXC did not sign the DXIL. Is the DXIL validator (dxil.dll / libdxil.so) next to DXC?\n", name);
			free(dxil);
			return NULL;
		}
	}

	return dxil;
}

// Makes a shader in another format out of SPIR-V
static size_t AddTranslation(const char *name, gpu_stage_t stage, size_t spirvblob, gpu_shaderformat_t format)
{
	char error[512];
	char *source = GPU_TranslateSPIRV(blobs[spirvblob].data, blobs[spirvblob].size, format, error, sizeof (error));

	if (!source)
	{
		fprintf(stderr, "%s: can't translate the %s shader: %s\n", name, stage == GPU_STAGE_VERTEX ? "vertex" : "fragment", error);
		return NOBLOB;
	}

	if (format == GPU_SHADERFORMAT_MSL)
		return AddBlob(source, strlen(source) + 1); // Metal takes text; the terminator is kept, but not counted
	else
	{
		size_t size;
		void *dxil = CompileHLSL(source, stage, name, &size);
		free(source);
		if (!dxil)
			return NOBLOB;
		return AddBlob(dxil, size);
	}
}

static int AddProgram(const char *name, unsigned mask, const char *vertex, const char *fragment)
{
	unsigned long long hash = GPU_HashShaderSources(vertex, fragment);
	const char *sources[2];
	char *translated[2], *log;
	char error[256];
	size_t i;
	int stage;
	program_t *program;

	for (i = 0; i < numprograms; i++)
		if (programs[i].hash == hash)
			return 1; // Sprite is the same as WallTexture

	if (!GPU_TranslateGLSL(vertex, fragment, &translated[0], &translated[1], error, sizeof (error)))
	{
		fprintf(stderr, "%s (%x): %s\n", name, mask, error);
		return 0;
	}
	sources[0] = vertex;
	sources[1] = fragment;

	program = &programs[numprograms];
	memset(program->blob, 0xff, sizeof (program->blob)); // NOBLOB
	program->hash = hash;
	snprintf(program->comment, sizeof (program->comment), "%s%s%s%s%s", name,
		(mask & 1) ? ", model lighting" : "", (mask & 2) ? ", palette rendering" : "",
		(mask & 4) ? ", curve" : "", (mask & 8) ? ", light dither" : "");

	for (stage = 0; stage < 2; stage++)
	{
		size_t size;
		void *spirv = GPU_CompileGLSL(translated[stage], stage ? GPU_STAGE_FRAGMENT : GPU_STAGE_VERTEX, name, &size, &log);
		if (!spirv)
		{
			fprintf(stderr, "%s (%x), %s shader:\n%s\n", name, mask, stage ? "fragment" : "vertex", log ? log : "no compiler");
			return 0;
		}
		free(log);
		free(translated[stage]);
		(void)sources;

		program->blob[GPU_SHADERFORMAT_SPIRV][stage] = AddBlob(spirv, size);

		if (dxc)
		{
			program->blob[GPU_SHADERFORMAT_DXIL][stage] = AddTranslation(name, stage, program->blob[GPU_SHADERFORMAT_SPIRV][stage], GPU_SHADERFORMAT_DXIL);
			if (program->blob[GPU_SHADERFORMAT_DXIL][stage] == NOBLOB)
				return 0;
		}
		if (msl)
		{
			program->blob[GPU_SHADERFORMAT_MSL][stage] = AddTranslation(name, stage, program->blob[GPU_SHADERFORMAT_SPIRV][stage], GPU_SHADERFORMAT_MSL);
			if (program->blob[GPU_SHADERFORMAT_MSL][stage] == NOBLOB)
				return 0;
		}
	}

	numprograms++;
	return 1;
}

static const char *const format_names[GPU_NUMSHADERFORMATS] =
{
	"GPU_SHADERFORMAT_SPIRV", "GPU_SHADERFORMAT_MSL", "GPU_SHADERFORMAT_METALLIB", "GPU_SHADERFORMAT_DXIL", "GPU_SHADERFORMAT_DXBC",
};

// Writes the shaders of some formats to a C file
static int WriteFile(const char *path, const char *array, const gpu_shaderformat_t *formats, size_t numformats, const char *what)
{
	char *used = calloc(numblobs ? numblobs : 1, 1);
	size_t i, j, f, total = 0, count = 0;
	FILE *out = fopen(path, "w");

	if (!out || !used)
	{
		perror(path);
		return 0;
	}

	for (f = 0; f < numformats; f++)
		for (i = 0; i < numprograms; i++)
			for (j = 0; j < 2; j++)
				if (programs[i].blob[formats[f]][j] != NOBLOB)
					used[programs[i].blob[formats[f]][j]] = 1;

	fprintf(out,
		"// SRB2 LEGACY\n"
		"//-----------------------------------------------------------------------------\n"
		"// Copyright (C) 2026 by SRB2 Preservation.\n"
		"//\n"
		"// This program is free software distributed under the\n"
		"// terms of the GNU General Public License, version 2.\n"
		"// See the 'LICENSE' file for more details.\n"
		"//-----------------------------------------------------------------------------\n"
		"/// \\file  %s\n"
		"/// \\brief The game's shaders, compiled for the SDL GPU renderer: %s\n"
		"///\n"
		"/// Written by tools/sdlgpu-shaders. Don't edit this, run the tool again.\n"
		"\n"
		"#include \"../gpu_glsl.h\"\n"
		"\n", strrchr(path, '/') ? strrchr(path, '/') + 1 : path, what);

	for (i = 0; i < numblobs; i++)
	{
		const unsigned char *data = blobs[i].data;
		if (!used[i])
			continue;
		fprintf(out, "static const unsigned char shader_%u[%u] =\n{", (unsigned)i, (unsigned)blobs[i].size);
		for (j = 0; j < blobs[i].size; j++)
			fprintf(out, "%s%u,", (j % 32) ? "" : "\n\t", data[j]);
		fprintf(out, "\n};\n\n");
		total += blobs[i].size;
	}

	fprintf(out, "const gpu_precompiled_t %s[] =\n{\n", array);
	for (f = 0; f < numformats; f++)
		for (i = 0; i < numprograms; i++)
		{
			const size_t vs = programs[i].blob[formats[f]][0], fs = programs[i].blob[formats[f]][1];
			// Metal's text is given without its terminator
			const char *size = (formats[f] == GPU_SHADERFORMAT_MSL) ? " - 1" : "";
			if (vs == NOBLOB || fs == NOBLOB)
				continue;
			fprintf(out, "\t{0x%016llxULL, %s, shader_%u, sizeof (shader_%u)%s, shader_%u, sizeof (shader_%u)%s}, // %s\n",
				programs[i].hash, format_names[formats[f]], (unsigned)vs, (unsigned)vs, size, (unsigned)fs, (unsigned)fs, size, programs[i].comment);
			count++;
		}
	if (!count) // C has no empty arrays
		fprintf(out, "\t{0, GPU_SHADERFORMAT_SPIRV, NULL, 0, NULL, 0}, // none\n");
	fprintf(out, "};\n\nconst size_t %s_count = %u;\n", array, (unsigned)count);
	fclose(out);
	free(used);

	printf("%s: %u shader programs, %u bytes of shaders\n", path, (unsigned)count, (unsigned)total);
	return 1;
}

static char *ReadSource(const char *path)
{
	size_t size;
	char *data = ReadFile(path, &size);
	if (!data)
		perror(path);
	return data;
}

static void Usage(const char *self)
{
	fprintf(stderr,
		"usage:\n"
		"  %s [--spirv <spirv.c>] [--msl <msl.c>] [--dxil <dxc> <dxil.c>]\n"
		"      compiles the game's shaders to SPIR-V (Vulkan), MSL (Metal) and DXIL (Direct3D 12),\n"
		"      the last with DXC; \"--dxil none\" writes an empty file\n"
		"  %s --translate <vertex shader> <fragment shader>\n"
		"      prints a GLSL shader pair as it is given to the compiler\n"
		"  %s --cross <msl|hlsl> <vertex shader> <fragment shader>\n"
		"      prints a GLSL shader pair translated for Metal or Direct3D 12\n"
		"  %s --compile <vertex shader> <fragment shader> <vertex SPIR-V> <fragment SPIR-V>\n"
		"      compiles a GLSL shader pair to SPIR-V, for the SPIRV sections of an addon's SHADERS lump\n",
		self, self, self, self);
}

int main(int argc, char **argv)
{
	const char *outputs[GPU_NUMSHADERFORMATS] = {NULL};
	size_t i;
	int arg;
	unsigned mask;

	if ((argc == 4 && !strcmp(argv[1], "--translate"))
		|| (argc == 5 && !strcmp(argv[1], "--cross"))
		|| (argc == 6 && !strcmp(argv[1], "--compile")))
	{
		const int cross = !strcmp(argv[1], "--cross");
		const int first = cross ? 3 : 2;
		char *sources[2], *translated[2], error[512];
		for (i = 0; i < 2; i++)
			if (!(sources[i] = ReadSource(argv[first + i])))
				return 1;
		if (!GPU_TranslateGLSL(sources[0], sources[1], &translated[0], &translated[1], error, sizeof (error)))
		{
			fprintf(stderr, "%s\n", error);
			return 1;
		}
		if (!strcmp(argv[1], "--translate"))
		{
			printf("%s\n// ---------- fragment ----------\n%s", translated[0], translated[1]);
			return 0;
		}
		for (i = 0; i < 2; i++)
		{
			char *log;
			size_t size;
			void *spirv = GPU_CompileGLSL(translated[i], i ? GPU_STAGE_FRAGMENT : GPU_STAGE_VERTEX, argv[first + i], &size, &log);
			if (!spirv)
			{
				fprintf(stderr, "%s", log ? log : "no compiler\n");
				return 1;
			}
			if (cross)
			{
				char *source = GPU_TranslateSPIRV(spirv, size, strcmp(argv[2], "hlsl") ? GPU_SHADERFORMAT_MSL : GPU_SHADERFORMAT_DXIL, error, sizeof (error));
				if (!source)
				{
					fprintf(stderr, "%s\n", error);
					return 1;
				}
				printf("%s%s", i ? "\n// ---------- fragment ----------\n" : "", source);
			}
			else
			{
				FILE *out = fopen(argv[4 + i], "wb");
				if (!out || fwrite(spirv, 1, size, out) != size) { perror(argv[4 + i]); return 1; }
				fclose(out);
			}
		}
		return 0;
	}

	for (arg = 1; arg < argc; arg++)
	{
		if (!strcmp(argv[arg], "--spirv") && arg + 1 < argc)
			outputs[GPU_SHADERFORMAT_SPIRV] = argv[++arg];
		else if (!strcmp(argv[arg], "--msl") && arg + 1 < argc)
			outputs[GPU_SHADERFORMAT_MSL] = argv[++arg];
		else if (!strcmp(argv[arg], "--dxil") && arg + 2 < argc)
		{
			if (strcmp(argv[++arg], "none"))
				dxc = argv[arg];
			outputs[GPU_SHADERFORMAT_DXIL] = argv[++arg];
		}
		else
		{
			Usage(argv[0]);
			return 1;
		}
	}
	if (argc < 2)
	{
		Usage(argv[0]);
		return 1;
	}
	msl = outputs[GPU_SHADERFORMAT_MSL] != NULL;

	if (dxc)
	{
		static char dir[] = "/tmp/sdlgpu-shaders-XXXXXX";
		workdir = getenv("SDLGPU_SHADERS_TMP");
		if (!workdir)
			workdir = mkdtemp(dir);
		if (!workdir)
		{
			perror("mkdtemp");
			return 1;
		}
	}

	if (!GPU_CanCompileGLSL() || !GPU_CanTranslateSPIRV())
	{
		fprintf(stderr, "this tool was built without shaderc or SPIRV-Cross\n");
		return 1;
	}

	// "--dxil none" alone compiles nothing
	if (outputs[GPU_SHADERFORMAT_SPIRV] || msl || dxc)
	{
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
	}

	for (i = 0; i < GPU_NUMSHADERFORMATS; i++)
	{
		static const struct
		{
			const char *array, *what;
		} files[GPU_NUMSHADERFORMATS] =
		{
			{"gpu_precompiled_spirv", "SPIR-V, for Vulkan"},
			{"gpu_precompiled_msl", "MSL, for Metal"},
			{NULL, NULL}, // METALLIB
			{"gpu_precompiled_dxil", "DXIL, for Direct3D 12"},
			{NULL, NULL}, // DXBC
		};
		const gpu_shaderformat_t format = (gpu_shaderformat_t)i;
		if (outputs[i] && !WriteFile(outputs[i], files[i].array, &format, 1, files[i].what))
			return 1;
	}
	return 0;
}
