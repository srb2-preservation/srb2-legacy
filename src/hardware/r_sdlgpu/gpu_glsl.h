// SRB2 LEGACY
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by SRB2 Preservation.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  gpu_glsl.h
/// \brief Translates the game's OpenGL shaders for the SDL GPU renderer
///
/// This file and gpu_glsl.c don't depend on the rest of the game, because
/// the tool that precompiles the built-in shaders uses them too
/// (see tools/sdlgpu-shaders).

#ifndef __GPU_GLSL_H__
#define __GPU_GLSL_H__

#include <stddef.h>

// ==========================================================================
//                                           WHAT EVERY SHADER IS COMPILED FOR
// ==========================================================================
//
// Shaders written in another format than GLSL (SPIR-V, MSL, DXIL...) are
// given to SDL as they are, so they have to follow all of this themselves.
//
// Vertex inputs, by location:
//   0  vec3  position
//   1  vec2  texture coordinates
//   2  vec4  colour
//   3  vec3  normal
//   4  vec3  position in the next frame of a model (same as 0 otherwise)
//   5  vec3  normal in the next frame of a model (same as 3 otherwise)
//
// Passed from the vertex shader to the fragment shader, by location:
//   0  vec4  colour
//   1  vec4  texture coordinates
//   (varyings declared by a GLSL shader come after, in an unspecified order)
//
// Fragment output: location 0, vec4.
//
// Textures of the fragment shader (SDL GPU's resource set for sampled
// textures), by binding. The vertex shader has none.
//   0  2D  "tex"                 the texture of the surface
//   1  3D  "palette_lookup_tex"  RGB to palette index (palette rendering)
//   2  2D  "lighttable_tex"      the light table of the surface, 256x32
//   3  2D  "palette_tex"         the screen palette, 256x1
//
// Uniforms: one buffer, at binding 0 of each stage's uniform buffer set,
// laid out like gpu_uniforms_t below (std140).
//
// Clip space is OpenGL's: the vertex shader of a GLSL shader gets its depth
// remapped from -w..w to 0..w after it runs. Other formats output 0..w.
//
// For Direct3D 12, the outputs of the vertex shader and the inputs of the
// fragment shader must be the same, in the same order. GLSL shaders are made
// so; the other formats have to do it themselves.
//
// GLSL shaders are compiled to SPIR-V, which Metal gets translated to MSL
// (when the game is built with SPIRV-Cross). Direct3D 12 can't compile shaders
// while the game runs, so only DXIL or DXBC given as is works there.

typedef struct
{
	float projection[16];    // "gl_ProjectionMatrix"
	float modelview[16];     // "gl_ModelViewMatrix"
	float poly_color[4];
	float tint_color[4];
	float fade_color[4];
	float current_color[4];  // multiplied with the vertex colour, gives "gl_Color"
	float lighting;
	float fade_start;
	float fade_end;
	float leveltime;
	float scr_resolution[2];
	float alpha_threshold;   // fragments with an alpha up to this are discarded
	float lerp;              // how far a model is between its frame and the next
	float target_size[2];    // size of what is being drawn to, in pixels
	float padding[2];
} gpu_uniforms_t;

#define GPU_NUM_FRAGMENT_SAMPLERS 4

typedef enum
{
	GPU_STAGE_VERTEX,
	GPU_STAGE_FRAGMENT,
} gpu_stage_t;

/**	\brief Translates a shader program written for the OpenGL renderer
	(GLSL 1.10 to 1.50, compatibility profile) to GLSL for Vulkan.

	Both stages are needed at once, to agree on where varyings go.

	\param	vertex	the vertex shader
	\param	fragment	the fragment shader
	\param	vertex_out	where to store the translated vertex shader. free() it.
	\param	fragment_out	same for the fragment shader
	\param	error	where to write what went wrong
	\param	errorsize	the size of that

	\return	false if the shaders can't be translated
*/
int GPU_TranslateGLSL(const char *vertex, const char *fragment,
	char **vertex_out, char **fragment_out, char *error, size_t errorsize);

/**	\brief Whether GLSL can be compiled, see GPU_CompileGLSL
*/
int GPU_CanCompileGLSL(void);

/**	\brief Compiles a shader translated by GPU_TranslateGLSL to SPIR-V

	\param	source	the shader
	\param	stage	which stage it is for
	\param	name	a name for the shader, for error messages
	\param	size	where to store the size of the SPIR-V, in bytes
	\param	log	where to store the compiler's messages, if any. free() it.

	\return	the SPIR-V, to free(), or NULL
*/
void *GPU_CompileGLSL(const char *source, gpu_stage_t stage, const char *name, size_t *size, char **log);

/**	\brief The hash precompiled shader programs are found by
*/
unsigned long long GPU_HashShaderSources(const char *vertex, const char *fragment);

/**	\brief Whether SPIR-V can be translated for Metal and Direct3D 12, see GPU_TranslateSPIRV
*/
int GPU_CanTranslateSPIRV(void);

/**	\brief Translates a SPIR-V shader that follows the rules above to MSL or HLSL

	MSL is for Metal; its entry point is called "main0".
	HLSL (shader model 6.0) is for Direct3D 12 once compiled to DXIL; its entry point is "main".

	\param	spirv	the SPIR-V
	\param	size	its size, in bytes
	\param	format	GPU_SHADERFORMAT_MSL, or GPU_SHADERFORMAT_DXIL for HLSL
	\param	error	where to write what went wrong
	\param	errorsize	the size of that

	\return	the shader's source, to free(), or NULL
*/
char *GPU_TranslateSPIRV(const void *spirv, size_t size, int format, char *error, size_t errorsize);

// ==========================================================================
//                                                        PRECOMPILED SHADERS
// ==========================================================================

// The formats a shader can be in, other than GLSL
typedef enum
{
	GPU_SHADERFORMAT_SPIRV,
	GPU_SHADERFORMAT_MSL,
	GPU_SHADERFORMAT_METALLIB,
	GPU_SHADERFORMAT_DXIL,
	GPU_SHADERFORMAT_DXBC,
	GPU_NUMSHADERFORMATS
} gpu_shaderformat_t;

// A shader program of the game, compiled ahead of time.
// Found by the hash of the GLSL it was made from, so that the game doesn't
// have to know which shaders are precompiled: it always asks for GLSL.
typedef struct
{
	unsigned long long hash; // GPU_HashShaderSources
	gpu_shaderformat_t format;
	const unsigned char *vertex;
	size_t vertex_size;
	const unsigned char *fragment;
	size_t fragment_size;
} gpu_precompiled_t;

// Written by tools/sdlgpu-shaders.
// gpu_precompiled.c: SPIR-V for Vulkan, MSL for Metal
extern const gpu_precompiled_t gpu_precompiled[];
extern const size_t gpu_precompiled_count;
// gpu_precompiled_d3d12.c: DXIL for Direct3D 12
extern const gpu_precompiled_t gpu_precompiled_d3d12[];
extern const size_t gpu_precompiled_d3d12_count;

// ==========================================================================
//                                                 THE RENDERER'S OWN SHADERS
// ==========================================================================

// These do what the OpenGL renderer does without shaders.
// They are written like the game's shaders, and translated the same way.

#define GPU_GLSL_PLAIN_VERTEX_SHADER \
	"void main()\n" \
	"{\n" \
		"gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex;\n" \
		"gl_FrontColor = gl_Color;\n" \
		"gl_TexCoord[0].xy = gl_MultiTexCoord0.xy;\n" \
	"}\n"

// Texture modulated by the current colour. Used for everything drawn with no shader set (the HUD, the menus...).
#define GPU_GLSL_PLAIN_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"void main(void) {\n" \
		"gl_FragColor = texture2D(tex, gl_TexCoord[0].st) * gl_Color;\n" \
	"}\n"

// Used in place of a shader that failed to compile.
#define GPU_GLSL_FALLBACK_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"uniform vec4 poly_color;\n" \
	"void main(void) {\n" \
		"gl_FragColor = texture2D(tex, gl_TexCoord[0].st) * poly_color;\n" \
	"}\n"

// Screen wipes: the end screen, as opaque as the fade mask says. The fade mask takes the place of the light table.
#define GPU_GLSL_WIPE_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"uniform sampler2D lighttable_tex;\n" \
	"void main(void) {\n" \
		"gl_FragColor = vec4(texture2D(tex, gl_TexCoord[0].st).rgb, texture2D(lighttable_tex, gl_TexCoord[0].st).a);\n" \
	"}\n"

#endif
