// SRB2 LEGACY
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by SRB2 Preservation.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_sdlgpu.c
/// \brief SDL GPU hardware driver
///
/// Implements the hardware driver (hw_drv.h) with SDL's GPU API, so it draws
/// with Vulkan, Direct3D 12 or Metal. It is given the same calls as the OpenGL
/// driver (r_opengl.c) and is meant to draw the same picture.
///
/// How a frame is made:
///  - The game is drawn to a texture of the game's resolution, with a depth
///    buffer. The window only matters when presenting (SDLGPU_Present).
///  - Draw calls are recorded in a command buffer as they come. Everything
///    that has to be copied to the GPU first (textures, the vertices of the
///    frame) goes in a second command buffer, which is submitted right before
///    the first one. See Flush.
///  - OpenGL's state (blending, depth test...) lives in graphics pipelines
///    here. They are made when first needed, from the polygon flags, and kept.
///  - Everything is drawn with a shader. See gpu_glsl.h for what shaders get.
///
/// Names are prefixed so that they don't clash with the OpenGL driver's,
/// which lives in the same executable.

#include "../../doomdef.h"

#ifdef HWRENDER_SDLGPU

#include <math.h>

#include "r_sdlgpu.h"
#include "gpu_glsl.h"
#include "../hw_main.h"
#include "../hw_shaders.h"
#include "../../console.h"
#include "../../i_system.h"
#include "../../m_argv.h"
#include "../../r_main.h" // rendertimefrac
#include "../../z_zone.h"

// ==========================================================================
//                                                                  CONSTANTS
// ==========================================================================

#define FAR_CLIPPING_PLANE 32768.0f
#define ASPECT_RATIO 1.0f

#define COLOR_FORMAT SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM

// the vertices and indices of a frame are written in chunks of this size, in bytes
#define VERTEX_CHUNK_SIZE (2*1024*1024)
#define INDEX_CHUNK_SIZE (1024*1024)

// polygons of up to this many vertices are drawn with a ready-made index buffer
#define MAX_FAN_VERTICES 1024

// how everything drawn but models is stored. Same as gl_skyvertex_t.
typedef struct
{
	float x, y, z;
	float s, t;
	UINT8 r, g, b, a;
} gpuvertex_t;

// how the vertices of a model's frame are stored
typedef struct
{
	float x, y, z;
	float nx, ny, nz;
	float s, t;
} gpumodelvertex_t;

typedef enum
{
	VERTEXFORMAT_STREAM, // gpuvertex_t
	VERTEXFORMAT_MODEL,  // gpumodelvertex_t, for a frame and the next one
} vertexformat_t;

typedef enum
{
	CULL_NONE,
	CULL_FRONT,
	CULL_BACK,
} cullmode_t;

// ==========================================================================
//                                                                      STATE
// ==========================================================================

static SDL_GPUDevice *device = NULL;
static SDL_GPUShaderFormat device_formats = 0;
static boolean device_anisotropy = true; // whether samplers can use anisotropic filtering
static boolean prefer_lowpower = false; // see SDLGPU_SetPreferLowPower

// what the game is drawn to
static SDL_GPUTexture *color_target = NULL;
static SDL_GPUTexture *depth_target = NULL;
static SDL_GPUTextureFormat depth_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
static INT32 screen_width = 0;
static INT32 screen_height = 0;

// the draw calls being recorded
static SDL_GPUCommandBuffer *render_cmd = NULL;
static SDL_GPURenderPass *render_pass = NULL;
static SDL_GPUGraphicsPipeline *bound_pipeline = NULL;
// the copies to the GPU that those draw calls need
static SDL_GPUCommandBuffer *upload_cmd = NULL;
static SDL_GPUCopyPass *upload_pass = NULL;
// counts the submissions of render_cmd, to know what was used in the current one
static UINT32 segment = 1;

static boolean clear_color_pending = false;
static boolean clear_depth_pending = false;
static SDL_FColor clear_color = {0.0f, 0.0f, 0.0f, 1.0f};

static SDL_GPUViewport viewport;

static FBITFIELD CurrentPolyFlags = 0;
static float NEAR_CLIPPING_PLANE = NZCLIP_PLANE;
static float fov = 90.0f;
static boolean wireframe = false;
static boolean model_lighting = false;

// texture filtering
static boolean filter_min_linear = false;
static boolean filter_mag_linear = false;
static boolean filter_mipmap = false;
static INT32 filter_anisotropy = 1;

// the uniforms, as they would be in OpenGL's state. Some are given per draw call.
static gpu_uniforms_t uniforms;
static float shader_leveltime = 0;

static const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};

// the palette paletted textures are converted with
static RGBA_t texture_palette[256];

// ==========================================================================
//                                                                   MESSAGES
// ==========================================================================

static void GPU_Error(const char *what)
{
	CONS_Alert(CONS_ERROR, "SDL GPU: %s: %s\n", what, SDL_GetError());
}

// ==========================================================================
//                                                                   MATRICES
// ==========================================================================

// Like OpenGL's: column-major, and each operation is multiplied on the right.

static void Mat_Identity(float *m)
{
	memset(m, 0, 16 * sizeof (float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void Mat_Multiply(float *m, const float *b)
{
	float a[16];
	int r, c;

	memcpy(a, m, sizeof (a));
	for (c = 0; c < 4; c++)
		for (r = 0; r < 4; r++)
			m[c*4 + r] = a[0*4 + r] * b[c*4 + 0] + a[1*4 + r] * b[c*4 + 1]
				+ a[2*4 + r] * b[c*4 + 2] + a[3*4 + r] * b[c*4 + 3];
}

static void Mat_Scale(float *m, float x, float y, float z)
{
	float s[16];
	Mat_Identity(s);
	s[0] = x;
	s[5] = y;
	s[10] = z;
	Mat_Multiply(m, s);
}

static void Mat_Translate(float *m, float x, float y, float z)
{
	float t[16];
	Mat_Identity(t);
	t[12] = x;
	t[13] = y;
	t[14] = z;
	Mat_Multiply(m, t);
}

// the axis must be of length 1
static void Mat_Rotate(float *m, float angle, float x, float y, float z)
{
	const float radians = angle * (float)(M_PIl / 180.0);
	const float c = cosf(radians), s = sinf(radians), t = 1.0f - c;
	float r[16];

	Mat_Identity(r);
	r[0] = x*x*t + c;
	r[1] = y*x*t + z*s;
	r[2] = x*z*t - y*s;
	r[4] = x*y*t - z*s;
	r[5] = y*y*t + c;
	r[6] = y*z*t + x*s;
	r[8] = x*z*t + y*s;
	r[9] = y*z*t - x*s;
	r[10] = z*z*t + c;
	Mat_Multiply(m, r);
}

static void Mat_Perspective(float *m, float fovy, float aspect)
{
	const float zNear = NEAR_CLIPPING_PLANE;
	const float zFar = FAR_CLIPPING_PLANE;
	const float radians = (float)(fovy / 2.0f * M_PIl / 180.0f);
	const float sine = sinf(radians);
	const float deltaZ = zFar - zNear;
	float cotangent;
	float p[16];

	if ((fabsf(deltaZ) < 1.0E-36f) || fpclassify(sine) == FP_ZERO || fpclassify(aspect) == FP_ZERO)
		return;
	cotangent = cosf(radians) / sine;

	memset(p, 0, sizeof (p));
	p[0] = cotangent / aspect;
	p[5] = cotangent;
	p[10] = -(zFar + zNear) / deltaZ;
	p[11] = -1.0f;
	p[14] = -2.0f * zNear * zFar / deltaZ;
	Mat_Multiply(m, p);
}

// ==========================================================================
//                                                            COMMAND BUFFERS
// ==========================================================================

static void Flush(void);

static SDL_GPUCopyPass *UploadPass(void)
{
	if (!upload_cmd)
	{
		upload_cmd = SDL_AcquireGPUCommandBuffer(device);
		if (!upload_cmd)
			I_Error("SDL GPU: couldn't get a command buffer: %s", SDL_GetError());
	}
	if (!upload_pass)
		upload_pass = SDL_BeginGPUCopyPass(upload_cmd);
	return upload_pass;
}

static void EndUploadPass(void)
{
	if (upload_pass)
	{
		SDL_EndGPUCopyPass(upload_pass);
		upload_pass = NULL;
	}
}

static SDL_GPUCommandBuffer *RenderCommands(void)
{
	if (!render_cmd)
	{
		render_cmd = SDL_AcquireGPUCommandBuffer(device);
		if (!render_cmd)
			I_Error("SDL GPU: couldn't get a command buffer: %s", SDL_GetError());
	}
	return render_cmd;
}

// Starts drawing to the game's picture, if not already.
// Clearing can only be done there.
static SDL_GPURenderPass *RenderPass(void)
{
	SDL_GPUColorTargetInfo color;
	SDL_GPUDepthStencilTargetInfo depth;

	if (render_pass)
		return render_pass;

	SDL_zero(color);
	color.texture = color_target;
	color.load_op = clear_color_pending ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	color.store_op = SDL_GPU_STOREOP_STORE;
	color.clear_color = clear_color;

	SDL_zero(depth);
	depth.texture = depth_target;
	depth.load_op = clear_depth_pending ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	depth.store_op = SDL_GPU_STOREOP_STORE;
	depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
	depth.clear_depth = 1.0f;

	render_pass = SDL_BeginGPURenderPass(RenderCommands(), &color, 1, &depth);
	if (!render_pass)
		I_Error("SDL GPU: couldn't begin a render pass: %s", SDL_GetError());

	clear_color_pending = clear_depth_pending = false;
	bound_pipeline = NULL;
	SDL_SetGPUViewport(render_pass, &viewport);
	return render_pass;
}

static void EndRenderPass(void)
{
	// a clear that nothing was drawn after still has to happen
	if (clear_color_pending || clear_depth_pending)
		RenderPass();

	if (render_pass)
	{
		SDL_EndGPURenderPass(render_pass);
		render_pass = NULL;
	}
}

// ==========================================================================
//                                                    COPYING DATA TO THE GPU
// ==========================================================================

// Gives memory to write data in, which Staging_End makes available to copy to the GPU.
static void *Staging_Begin(Uint32 size, SDL_GPUTransferBuffer **transfer)
{
	SDL_GPUTransferBufferCreateInfo info;
	void *data;

	SDL_zero(info);
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	info.size = size;

	*transfer = SDL_CreateGPUTransferBuffer(device, &info);
	if (!*transfer)
		I_Error("SDL GPU: couldn't create a transfer buffer of %u bytes: %s", size, SDL_GetError());

	data = SDL_MapGPUTransferBuffer(device, *transfer, false);
	if (!data)
		I_Error("SDL GPU: couldn't map a transfer buffer: %s", SDL_GetError());
	return data;
}

static void Staging_ToBuffer(SDL_GPUTransferBuffer *transfer, SDL_GPUBuffer *buffer, Uint32 size)
{
	SDL_GPUTransferBufferLocation source;
	SDL_GPUBufferRegion destination;

	SDL_UnmapGPUTransferBuffer(device, transfer);

	SDL_zero(source);
	source.transfer_buffer = transfer;
	SDL_zero(destination);
	destination.buffer = buffer;
	destination.size = size;
	SDL_UploadToGPUBuffer(UploadPass(), &source, &destination, false);

	SDL_ReleaseGPUTransferBuffer(device, transfer);
}

static void Staging_ToTexture(SDL_GPUTransferBuffer *transfer, SDL_GPUTexture *texture, Uint32 w, Uint32 h, Uint32 d)
{
	SDL_GPUTextureTransferInfo source;
	SDL_GPUTextureRegion destination;

	SDL_UnmapGPUTransferBuffer(device, transfer);

	SDL_zero(source);
	source.transfer_buffer = transfer;
	source.pixels_per_row = w;
	source.rows_per_layer = h;
	SDL_zero(destination);
	destination.texture = texture;
	destination.w = w;
	destination.h = h;
	destination.d = d;
	SDL_UploadToGPUTexture(UploadPass(), &source, &destination, false);

	SDL_ReleaseGPUTransferBuffer(device, transfer);
}

static SDL_GPUBuffer *CreateBuffer(SDL_GPUBufferUsageFlags usage, Uint32 size, const void *data)
{
	SDL_GPUBufferCreateInfo info;
	SDL_GPUBuffer *buffer;

	SDL_zero(info);
	info.usage = usage;
	info.size = size;

	buffer = SDL_CreateGPUBuffer(device, &info);
	if (!buffer)
		I_Error("SDL GPU: couldn't create a buffer of %u bytes: %s", size, SDL_GetError());

	if (data)
	{
		SDL_GPUTransferBuffer *transfer;
		memcpy(Staging_Begin(size, &transfer), data, size);
		Staging_ToBuffer(transfer, buffer, size);
	}

	return buffer;
}

// --------------------------------------------------------------------------
// The vertices and indices of what is drawn in a frame. They are written
// straight into memory that gets copied to the GPU when the frame is submitted.
// --------------------------------------------------------------------------

typedef struct
{
	SDL_GPUBuffer *buffer;
	SDL_GPUTransferBuffer *transfer;
	UINT8 *mapped;
	Uint32 used, capacity;
} streamchunk_t;

typedef struct
{
	streamchunk_t *chunks;
	size_t numchunks;
	size_t current;
	SDL_GPUBufferUsageFlags usage;
	Uint32 chunksize;
} stream_t;

static stream_t vertex_stream = {NULL, 0, 0, SDL_GPU_BUFFERUSAGE_VERTEX, VERTEX_CHUNK_SIZE};
static stream_t index_stream = {NULL, 0, 0, SDL_GPU_BUFFERUSAGE_INDEX, INDEX_CHUNK_SIZE};

static void Stream_CreateChunk(stream_t *stream, streamchunk_t *chunk, Uint32 capacity)
{
	SDL_GPUTransferBufferCreateInfo info;

	chunk->buffer = CreateBuffer(stream->usage, capacity, NULL);

	SDL_zero(info);
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	info.size = capacity;
	chunk->transfer = SDL_CreateGPUTransferBuffer(device, &info);
	if (!chunk->transfer)
		I_Error("SDL GPU: couldn't create a transfer buffer of %u bytes: %s", capacity, SDL_GetError());

	chunk->mapped = NULL;
	chunk->used = 0;
	chunk->capacity = capacity;
}

// Gives memory for size bytes, a multiple of unit.
// offset is where it is in buffer, in units.
static void *Stream_Alloc(stream_t *stream, Uint32 size, Uint32 unit, SDL_GPUBuffer **buffer, Uint32 *offset)
{
	streamchunk_t *chunk;
	void *memory;

	if (!stream->numchunks)
	{
		stream->chunks = calloc(1, sizeof (*stream->chunks));
		if (!stream->chunks)
			I_Error("SDL GPU: out of memory");
		Stream_CreateChunk(stream, &stream->chunks[0], max(stream->chunksize, size));
		stream->numchunks = 1;
		stream->current = 0;
	}

	chunk = &stream->chunks[stream->current];
	chunk->used += (unit - (chunk->used % unit)) % unit;

	if (chunk->used + size > chunk->capacity)
	{
		// on to the next chunk
		stream->current++;
		if (stream->current == stream->numchunks)
		{
			stream->chunks = realloc(stream->chunks, (stream->numchunks + 1) * sizeof (*stream->chunks));
			if (!stream->chunks)
				I_Error("SDL GPU: out of memory");
			Stream_CreateChunk(stream, &stream->chunks[stream->numchunks], max(stream->chunksize, size));
			stream->numchunks++;
		}

		chunk = &stream->chunks[stream->current];
		if (chunk->capacity < size)
		{
			// nothing uses it yet in this frame
			SDL_ReleaseGPUBuffer(device, chunk->buffer);
			SDL_ReleaseGPUTransferBuffer(device, chunk->transfer);
			Stream_CreateChunk(stream, chunk, size);
		}
	}

	if (!chunk->mapped)
	{
		// the previous contents may still be on their way to the GPU
		chunk->mapped = SDL_MapGPUTransferBuffer(device, chunk->transfer, true);
		if (!chunk->mapped)
			I_Error("SDL GPU: couldn't map a transfer buffer: %s", SDL_GetError());
	}

	memory = chunk->mapped + chunk->used;
	*buffer = chunk->buffer;
	*offset = chunk->used / unit;
	chunk->used += size;
	return memory;
}

// Copies what was written to the GPU, and starts over.
static void Stream_Submit(stream_t *stream)
{
	size_t i;

	for (i = 0; i < stream->numchunks; i++)
	{
		streamchunk_t *chunk = &stream->chunks[i];

		if (!chunk->mapped)
			continue;

		SDL_UnmapGPUTransferBuffer(device, chunk->transfer);
		chunk->mapped = NULL;

		if (chunk->used)
		{
			SDL_GPUTransferBufferLocation source;
			SDL_GPUBufferRegion destination;

			SDL_zero(source);
			source.transfer_buffer = chunk->transfer;
			SDL_zero(destination);
			destination.buffer = chunk->buffer;
			destination.size = chunk->used;
			// Not cycled: the draw calls recorded already refer to this buffer.
			// Copies and draws happen in the order they are submitted, so the
			// previous frame is done with the buffer by the time this is copied.
			SDL_UploadToGPUBuffer(UploadPass(), &source, &destination, false);
		}

		chunk->used = 0;
	}

	stream->current = 0;
}

static void Stream_Free(stream_t *stream)
{
	size_t i;

	for (i = 0; i < stream->numchunks; i++)
	{
		if (stream->chunks[i].mapped)
			SDL_UnmapGPUTransferBuffer(device, stream->chunks[i].transfer);
		SDL_ReleaseGPUBuffer(device, stream->chunks[i].buffer);
		SDL_ReleaseGPUTransferBuffer(device, stream->chunks[i].transfer);
	}

	free(stream->chunks);
	stream->chunks = NULL;
	stream->numchunks = stream->current = 0;
}

// Sends everything recorded so far to the GPU.
static void Flush(void)
{
	EndRenderPass();

	Stream_Submit(&vertex_stream);
	Stream_Submit(&index_stream);
	EndUploadPass();

	// the copies first: the draw calls need them done
	if (upload_cmd)
	{
		if (!SDL_SubmitGPUCommandBuffer(upload_cmd))
			GPU_Error("couldn't submit the uploads");
		upload_cmd = NULL;
	}

	if (render_cmd)
	{
		if (!SDL_SubmitGPUCommandBuffer(render_cmd))
			GPU_Error("couldn't submit the frame");
		render_cmd = NULL;
	}

	segment++;
}

// ==========================================================================
//                                                                   TEXTURES
// ==========================================================================

typedef struct
{
	SDL_GPUTexture *texture;
	Uint32 width, height;
	Uint32 levels;
	UINT32 flags; // TF_*
	UINT32 used_segment; // the last segment a draw call used it in
	boolean nofilter; // never filtered: screen textures, lookup tables
	boolean lighttable;
} gputexture_t;

// The textures of the game. FTextureInfo.downloaded and the ids of light
// tables are indices in this, plus one.
static gputexture_t **gputextures = NULL;
static size_t numgputextures = 0;

// the game's textures that are on the GPU, linked by FTextureInfo.nextmipmap
static FTextureInfo *cache_head = NULL;
static FTextureInfo *cache_tail = NULL;

static gputexture_t white_texture; // what PF_NoTexture draws with
static gputexture_t screen_textures[NUMSCREENTEXTURES];
static gputexture_t palette_lookup_texture; // 3D, RGB to palette index
static gputexture_t screen_palette_texture; // 256x1
static gputexture_t dummy_table_texture; // stands for a light table when there is none
static RGBA_t screen_palette[256];

// what OpenGL would have bound
static gputexture_t *current_texture = &white_texture;
static gputexture_t *current_lighttable = NULL;

static SDL_GPUTexture *CreateTexture(SDL_GPUTextureType type, SDL_GPUTextureFormat format,
	SDL_GPUTextureUsageFlags usage, Uint32 w, Uint32 h, Uint32 d, Uint32 levels)
{
	SDL_GPUTextureCreateInfo info;
	SDL_GPUTexture *texture;

	SDL_zero(info);
	info.type = type;
	info.format = format;
	info.usage = usage;
	info.width = w;
	info.height = h;
	info.layer_count_or_depth = d;
	info.num_levels = levels;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;

	texture = SDL_CreateGPUTexture(device, &info);
	if (!texture)
		I_Error("SDL GPU: couldn't create a %ux%u texture: %s", w, h, SDL_GetError());
	return texture;
}

// A texture can't change between two draw calls of the same submission:
// all copies are done before all draws. So if it was drawn with, submit.
static void Texture_WillChange(gputexture_t *texture)
{
	if (texture->used_segment == segment)
		Flush();
}

static void Texture_Release(gputexture_t *texture)
{
	if (texture->texture)
		SDL_ReleaseGPUTexture(device, texture->texture);
	texture->texture = NULL;

	if (current_texture == texture)
		current_texture = &white_texture;
	if (current_lighttable == texture)
		current_lighttable = NULL;
}

// Makes a texture out of RGBA pixels, or replaces its contents.
static void Texture_SetPixels(gputexture_t *texture, const void *pixels, Uint32 w, Uint32 h, Uint32 levels)
{
	SDL_GPUTransferBuffer *transfer;

	if (texture->texture && (texture->width != w || texture->height != h || texture->levels != levels))
		Texture_Release(texture);

	if (!texture->texture)
	{
		texture->texture = CreateTexture(SDL_GPU_TEXTURETYPE_2D, COLOR_FORMAT,
			// making mipmaps is done by drawing to them
			SDL_GPU_TEXTUREUSAGE_SAMPLER | (levels > 1 ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0),
			w, h, 1, levels);
		texture->width = w;
		texture->height = h;
		texture->levels = levels;
	}
	else
		Texture_WillChange(texture);

	memcpy(Staging_Begin(w * h * 4, &transfer), pixels, w * h * 4);
	Staging_ToTexture(transfer, texture->texture, w, h, 1);

	if (levels > 1)
	{
		EndUploadPass();
		SDL_GenerateMipmapsForGPUTexture(upload_cmd, texture->texture);
	}
}

static UINT32 Texture_NewId(gputexture_t **texture)
{
	size_t i;

	for (i = 0; i < numgputextures; i++)
		if (!gputextures[i])
			break;

	if (i == numgputextures)
	{
		gputextures = realloc(gputextures, (numgputextures + 64) * sizeof (*gputextures));
		if (!gputextures)
			I_Error("SDL GPU: out of memory");
		memset(gputextures + numgputextures, 0, 64 * sizeof (*gputextures));
		numgputextures += 64;
	}

	gputextures[i] = calloc(1, sizeof (**gputextures));
	if (!gputextures[i])
		I_Error("SDL GPU: out of memory");

	*texture = gputextures[i];
	return (UINT32)i + 1;
}

static gputexture_t *Texture_FromId(UINT32 id)
{
	if (!id || id > numgputextures)
		return NULL;
	return gputextures[id - 1];
}

static void Texture_FreeId(UINT32 id)
{
	gputexture_t *texture = Texture_FromId(id);
	if (!texture)
		return;
	Texture_Release(texture);
	free(texture);
	gputextures[id - 1] = NULL;
}

// -----------------+
// FlushTextures    : Forget the game's textures
// -----------------+
static void FlushTextures(void)
{
	while (cache_head)
	{
		Texture_FreeId(cache_head->downloaded);
		cache_head->downloaded = 0;
		cache_head = cache_head->nextmipmap;
	}
	cache_tail = cache_head = NULL;
}

// --------------------------------------------------------------------------
// Samplers: how a texture is filtered and wrapped. Unlike in OpenGL they are
// not part of the texture, so changing the filter doesn't mean reloading
// every texture, and a draw call can wrap a texture its own way.
// --------------------------------------------------------------------------

#define SAMPLER_MIN_LINEAR 1
#define SAMPLER_MAG_LINEAR 2
#define SAMPLER_MIPMAP 4
#define SAMPLER_WRAP_U 8
#define SAMPLER_WRAP_V 16
#define SAMPLER_ANISOTROPY_SHIFT 5 // then the anisotropy, up to 16
#define NUMSAMPLERS (17 << SAMPLER_ANISOTROPY_SHIFT)

static SDL_GPUSampler *samplers[NUMSAMPLERS];

static SDL_GPUSampler *GetSampler(unsigned key)
{
	if (!samplers[key])
	{
		SDL_GPUSamplerCreateInfo info;
		const unsigned anisotropy = key >> SAMPLER_ANISOTROPY_SHIFT;

		SDL_zero(info);
		info.min_filter = (key & SAMPLER_MIN_LINEAR) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
		info.mag_filter = (key & SAMPLER_MAG_LINEAR) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
		info.mipmap_mode = (key & SAMPLER_MIPMAP) ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		info.address_mode_u = (key & SAMPLER_WRAP_U) ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		info.address_mode_v = (key & SAMPLER_WRAP_V) ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		info.max_lod = (key & SAMPLER_MIPMAP) ? 1000.0f : 0.0f;
		if (anisotropy > 1 && device_anisotropy)
		{
			info.enable_anisotropy = true;
			info.max_anisotropy = (float)anisotropy;
		}

		samplers[key] = SDL_CreateGPUSampler(device, &info);
		if (!samplers[key])
			I_Error("SDL GPU: couldn't create a sampler: %s", SDL_GetError());
	}

	return samplers[key];
}

// The sampler for a texture, drawn with some polygon flags.
static SDL_GPUSampler *Texture_Sampler(const gputexture_t *texture, FBITFIELD PolyFlags)
{
	unsigned key = 0;

	if (texture->nofilter)
		return GetSampler(0);

	// no filtering on any texture that has holes so there's no dumb borders or blending issues
	if (!(texture->flags & TF_TRANSPARENT))
	{
		if (filter_min_linear)
			key |= SAMPLER_MIN_LINEAR;
		if (filter_mag_linear)
			key |= SAMPLER_MAG_LINEAR;
		if (filter_mipmap)
			key |= SAMPLER_MIPMAP;
		if (filter_anisotropy > 1)
			key |= (unsigned)filter_anisotropy << SAMPLER_ANISOTROPY_SHIFT;
	}

	if ((texture->flags & TF_WRAPX) || (PolyFlags & PF_ForceWrapX))
		key |= SAMPLER_WRAP_U;

	if (PolyFlags & PF_ForceWrapY)
		key |= SAMPLER_WRAP_V;
	else if ((texture->flags & TF_WRAPY) && !(PolyFlags & PF_RemoveYWrap))
		key |= SAMPLER_WRAP_V;

	return GetSampler(key);
}

// ==========================================================================
//                                                                    SHADERS
// ==========================================================================

typedef struct
{
	// what the game gave: GLSL for OpenGL
	char *vertex_source;
	char *fragment_source;
	// what an addon gave in a format SDL GPU takes as is
	void *binary[GPU_NUMSHADERFORMATS][2];
	size_t binary_size[GPU_NUMSHADERFORMATS][2];

	SDL_GPUShader *vertex;
	SDL_GPUShader *fragment;
	UINT32 id; // changes each time it is compiled. 0 if it isn't usable.
} gpuprogram_t;

static gpuprogram_t programs[HWR_MAXSHADERS];
// the renderer's own, see gpu_glsl.h
static gpuprogram_t plain_program;
static gpuprogram_t fallback_program;
static gpuprogram_t wipe_program;

static gpuprogram_t *current_program = NULL; // NULL when the game has set no shader
static UINT32 next_program_id = 1;

static void Pipelines_ForgetProgram(UINT32 id);

static const struct
{
	SDL_GPUShaderFormat sdl;
	const char *entrypoint;
} shaderformats[GPU_NUMSHADERFORMATS] =
{
	{SDL_GPU_SHADERFORMAT_SPIRV, "main"},
	{SDL_GPU_SHADERFORMAT_MSL, "main0"}, // what SPIRV-Cross calls it
	{SDL_GPU_SHADERFORMAT_METALLIB, "main0"},
	{SDL_GPU_SHADERFORMAT_DXIL, "main"},
	{SDL_GPU_SHADERFORMAT_DXBC, "main"},
};

static SDL_GPUShader *Shader_Create(gpu_shaderformat_t format, gpu_stage_t stage, const void *code, size_t size)
{
	SDL_GPUShaderCreateInfo info;

	SDL_zero(info);
	info.code = code;
	info.code_size = size;
	info.entrypoint = shaderformats[format].entrypoint;
	info.format = shaderformats[format].sdl;
	info.stage = (stage == GPU_STAGE_VERTEX) ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
	info.num_samplers = (stage == GPU_STAGE_FRAGMENT) ? GPU_NUM_FRAGMENT_SAMPLERS : 0;
	info.num_uniform_buffers = 1;

	return SDL_CreateGPUShader(device, &info);
}

static void Program_Unload(gpuprogram_t *program)
{
	if (program->id)
		Pipelines_ForgetProgram(program->id);
	if (program->vertex)
		SDL_ReleaseGPUShader(device, program->vertex);
	if (program->fragment)
		SDL_ReleaseGPUShader(device, program->fragment);
	program->vertex = program->fragment = NULL;
	program->id = 0;
}

static boolean Program_Create(gpuprogram_t *program, gpu_shaderformat_t format,
	const void *vertex, size_t vertex_size, const void *fragment, size_t fragment_size, const char *name)
{
	program->vertex = Shader_Create(format, GPU_STAGE_VERTEX, vertex, vertex_size);
	if (program->vertex)
		program->fragment = Shader_Create(format, GPU_STAGE_FRAGMENT, fragment, fragment_size);

	if (!program->vertex || !program->fragment)
	{
		CONS_Alert(CONS_ERROR, "SDL GPU: couldn't create the %s shader: %s\n", name, SDL_GetError());
		Program_Unload(program);
		return false;
	}

	program->id = next_program_id++;
	return true;
}

// The tables of the game's shaders, compiled ahead of time
static const struct
{
	const gpu_precompiled_t *list;
	const size_t *count;
} precompiled_tables[] =
{
	{gpu_precompiled, &gpu_precompiled_count},             // Vulkan, Metal
	{gpu_precompiled_d3d12, &gpu_precompiled_d3d12_count}, // Direct3D 12
};

// Finds a shader program of the game, in a format the GPU device takes
static const gpu_precompiled_t *FindPrecompiled(unsigned long long hash)
{
	size_t t, i;

	for (t = 0; t < SDL_arraysize(precompiled_tables); t++)
		for (i = 0; i < *precompiled_tables[t].count; i++)
		{
			const gpu_precompiled_t *precompiled = &precompiled_tables[t].list[i];
			if (precompiled->hash == hash && (device_formats & shaderformats[precompiled->format].sdl))
				return precompiled;
		}

	return NULL;
}

// The shader formats there are precompiled shaders in
static SDL_GPUShaderFormat PrecompiledFormats(boolean d3d12)
{
	SDL_GPUShaderFormat formats = 0;
	size_t i;

	if (d3d12)
	{
		for (i = 0; i < gpu_precompiled_d3d12_count; i++)
			formats |= shaderformats[gpu_precompiled_d3d12[i].format].sdl;
	}
	else
	{
		for (i = 0; i < gpu_precompiled_count; i++)
			formats |= shaderformats[gpu_precompiled[i].format].sdl;
	}

	return formats;
}

// gpu_preferlowpower, or -gpulowpower, which also works with -sdlgpu:
// that starts SDL GPU before the configuration is loaded.
static boolean PreferLowPower(void)
{
	return prefer_lowpower || M_CheckParm("-gpulowpower");
}

// Creates the GPU device, for some shader formats
static SDL_GPUDevice *CreateDevice(SDL_GPUShaderFormat formats, boolean anisotropy)
{
	SDL_PropertiesID props = SDL_CreateProperties();
	SDL_GPUDevice *created;

	if (!props)
		return NULL;

	// -gpudebug makes SDL and the graphics API check what the renderer does, and say what it does wrong
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, M_CheckParm("-gpudebug") != 0);
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, PreferLowPower());
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0);
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_DXIL) != 0);
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, (formats & SDL_GPU_SHADERFORMAT_MSL) != 0);

	// Direct3D 12: Resource Binding Tier 1 only falls short with more than 8 storage buffers and textures,
	// and the renderer has none, so Intel Haswell and Broadwell GPUs can be used too.
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_D3D12_ALLOW_FEWER_RESOURCE_SLOTS_BOOLEAN, true);

	// Vulkan: the renderer doesn't need some of the device features SDL asks for by default,
	// and many Android devices don't have them:
	//  - clip distances (no shader writes gl_ClipDistance)
	//  - depth clamping (depth clipping is always on)
	//  - the first instance of indirect draws (nothing is drawn indirectly)
	// Anisotropic filtering is used, but can be done without (see GetSampler).
#ifdef __ANDROID__
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN, false);
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_DEPTH_CLAMPING_BOOLEAN, false);
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_INDIRECT_DRAW_FIRST_INSTANCE_BOOLEAN, false);
#endif
	SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_FEATURE_ANISOTROPY_BOOLEAN, anisotropy);

	created = SDL_CreateGPUDeviceWithProperties(props);
	SDL_DestroyProperties(props);
	return created;
}

// Makes a SPIR-V shader pair usable by a GPU device that doesn't take SPIR-V
static boolean Program_CreateFromSPIRV(gpuprogram_t *program, const void *vertex, size_t vertex_size,
	const void *fragment, size_t fragment_size, const char *name)
{
	char *translated[2] = {NULL, NULL};
	char error[512];
	boolean success = false;
	int stage;

	if (device_formats & SDL_GPU_SHADERFORMAT_SPIRV)
		return Program_Create(program, GPU_SHADERFORMAT_SPIRV, vertex, vertex_size, fragment, fragment_size, name);

	if (!(device_formats & SDL_GPU_SHADERFORMAT_MSL))
	{
		CONS_Alert(CONS_ERROR, "SDL GPU: can't use the %s shader with %s: it can only be translated for Vulkan and Metal\n",
			name, SDL_GetGPUDeviceDriver(device));
		return false;
	}

	if (!GPU_CanTranslateSPIRV())
	{
		CONS_Alert(CONS_ERROR, "SDL GPU: can't use the %s shader with Metal: this build of the game has no SPIRV-Cross\n", name);
		return false;
	}

	for (stage = 0; stage < 2; stage++)
	{
		translated[stage] = GPU_TranslateSPIRV(stage ? fragment : vertex, stage ? fragment_size : vertex_size,
			GPU_SHADERFORMAT_MSL, error, sizeof (error));
		if (!translated[stage])
		{
			CONS_Alert(CONS_ERROR, "SDL GPU: can't translate the %s shader for Metal: %s\n", name, error);
			break;
		}
	}

	if (translated[0] && translated[1])
		success = Program_Create(program, GPU_SHADERFORMAT_MSL, translated[0], strlen(translated[0]),
			translated[1], strlen(translated[1]), name);

	free(translated[0]);
	free(translated[1]);
	return success;
}

// Makes a shader program usable, from whatever it was given in.
static boolean Program_Compile(gpuprogram_t *program, const char *name)
{
	const gpu_precompiled_t *precompiled = NULL;
	int format;

	Program_Unload(program);

	// The game's own shaders are compiled ahead of time. They are found by their GLSL.
	if (program->vertex_source && program->fragment_source)
		precompiled = FindPrecompiled(GPU_HashShaderSources(program->vertex_source, program->fragment_source));

	// 1. a format SDL GPU takes as is, if it's one this GPU device takes.
	// If only one stage was given that way, the other one is the game's.
	for (format = 0; format < GPU_NUMSHADERFORMATS; format++)
	{
		const void *vertex = program->binary[format][GPU_STAGE_VERTEX];
		const void *fragment = program->binary[format][GPU_STAGE_FRAGMENT];
		size_t vertex_size = program->binary_size[format][GPU_STAGE_VERTEX];
		size_t fragment_size = program->binary_size[format][GPU_STAGE_FRAGMENT];

		if (!(device_formats & shaderformats[format].sdl) || (!vertex && !fragment))
			continue;

		if (precompiled && (int)precompiled->format == format)
		{
			if (!vertex)
			{
				vertex = precompiled->vertex;
				vertex_size = precompiled->vertex_size;
			}
			if (!fragment)
			{
				fragment = precompiled->fragment;
				fragment_size = precompiled->fragment_size;
			}
		}

		if (vertex && fragment)
			return Program_Create(program, format, vertex, vertex_size, fragment, fragment_size, name);
	}

	// SPIR-V given for both stages can be translated for Metal
	if (program->binary[GPU_SHADERFORMAT_SPIRV][GPU_STAGE_VERTEX] && program->binary[GPU_SHADERFORMAT_SPIRV][GPU_STAGE_FRAGMENT]
		&& !(device_formats & SDL_GPU_SHADERFORMAT_SPIRV))
		return Program_CreateFromSPIRV(program,
			program->binary[GPU_SHADERFORMAT_SPIRV][GPU_STAGE_VERTEX], program->binary_size[GPU_SHADERFORMAT_SPIRV][GPU_STAGE_VERTEX],
			program->binary[GPU_SHADERFORMAT_SPIRV][GPU_STAGE_FRAGMENT], program->binary_size[GPU_SHADERFORMAT_SPIRV][GPU_STAGE_FRAGMENT], name);

	if (!program->vertex_source || !program->fragment_source)
	{
		CONS_Alert(CONS_ERROR, "SDL GPU: the %s shader is not available in a format this GPU device takes\n", name);
		return false;
	}

	// 2. the game's own shaders
	if (precompiled)
		return Program_Create(program, precompiled->format, precompiled->vertex, precompiled->vertex_size,
			precompiled->fragment, precompiled->fragment_size, name);

	// 3. GLSL written for the OpenGL renderer
	if (!GPU_CanCompileGLSL())
	{
		CONS_Alert(CONS_ERROR, "SDL GPU: can't compile the %s shader: this build of the game has no GLSL compiler\n", name);
		return false;
	}
	else
	{
		char *vertex_glsl, *fragment_glsl;
		void *vertex_spirv, *fragment_spirv = NULL;
		size_t vertex_size, fragment_size;
		char error[256];
		char *log;
		boolean success = false;

		if (!GPU_TranslateGLSL(program->vertex_source, program->fragment_source, &vertex_glsl, &fragment_glsl, error, sizeof (error)))
		{
			CONS_Alert(CONS_ERROR, "SDL GPU: can't use the %s shader: %s\n", name, error);
			return false;
		}

		vertex_spirv = GPU_CompileGLSL(vertex_glsl, GPU_STAGE_VERTEX, name, &vertex_size, &log);
		if (!vertex_spirv)
			CONS_Alert(CONS_ERROR, "SDL GPU: error compiling the vertex shader of the %s shader\n%s", name, log ? log : "");
		free(log);

		if (vertex_spirv)
		{
			fragment_spirv = GPU_CompileGLSL(fragment_glsl, GPU_STAGE_FRAGMENT, name, &fragment_size, &log);
			if (!fragment_spirv)
				CONS_Alert(CONS_ERROR, "SDL GPU: error compiling the fragment shader of the %s shader\n%s", name, log ? log : "");
			free(log);
		}

		// SPIR-V is what Vulkan takes; for Metal it is translated further
		if (vertex_spirv && fragment_spirv)
			success = Program_CreateFromSPIRV(program, vertex_spirv, vertex_size, fragment_spirv, fragment_size, name);

		free(vertex_spirv);
		free(fragment_spirv);
		free(vertex_glsl);
		free(fragment_glsl);
		return success;
	}
}

// The shader to draw with.
static gpuprogram_t *ActiveProgram(void)
{
	if (!current_program)
		return &plain_program; // what OpenGL does without shaders
	if (!current_program->id)
		return &fallback_program; // unusable shader
	return current_program;
}

static boolean SDLGPU_InitShaders(void)
{
	// the renderer's own were compiled by Init
	return true;
}

static void SDLGPU_LoadShader(int slot, char *code, hwdshaderstage_t stage)
{
	gpuprogram_t *program;

	if (slot < 0 || slot >= HWR_MAXSHADERS)
		I_Error("LoadShader: Invalid slot %d", slot);

	program = &programs[slot];

	if (stage == HWD_SHADERSTAGE_VERTEX)
	{
		Z_Free(program->vertex_source);
		program->vertex_source = code;
	}
	else if (stage == HWD_SHADERSTAGE_FRAGMENT)
	{
		Z_Free(program->fragment_source);
		program->fragment_source = code;
	}
	else
		I_Error("LoadShader: invalid shader stage");
}

static void SDLGPU_LoadShaderBinary(int slot, hwdshaderformat_t format, hwdshaderstage_t stage, const void *code, size_t size)
{
	gpuprogram_t *program;
	gpu_shaderformat_t gpuformat;
	gpu_stage_t gpustage = (stage == HWD_SHADERSTAGE_VERTEX) ? GPU_STAGE_VERTEX : GPU_STAGE_FRAGMENT;

	if (slot < 0 || slot >= HWR_MAXSHADERS)
		I_Error("LoadShaderBinary: Invalid slot %d", slot);

	switch (format)
	{
		case HWD_SHADERFORMAT_SPIRV:    gpuformat = GPU_SHADERFORMAT_SPIRV; break;
		case HWD_SHADERFORMAT_MSL:      gpuformat = GPU_SHADERFORMAT_MSL; break;
		case HWD_SHADERFORMAT_METALLIB: gpuformat = GPU_SHADERFORMAT_METALLIB; break;
		case HWD_SHADERFORMAT_DXIL:     gpuformat = GPU_SHADERFORMAT_DXIL; break;
		case HWD_SHADERFORMAT_DXBC:     gpuformat = GPU_SHADERFORMAT_DXBC; break;
		default: return;
	}

	program = &programs[slot];

	free(program->binary[gpuformat][gpustage]);
	program->binary[gpuformat][gpustage] = NULL;
	program->binary_size[gpuformat][gpustage] = 0;

	if (code && size)
	{
		// MSL is text, which SDL wants terminated
		char *copy = malloc(size + 1);
		if (!copy)
			return;
		memcpy(copy, code, size);
		copy[size] = '\0';
		program->binary[gpuformat][gpustage] = copy;
		program->binary_size[gpuformat][gpustage] = size;
	}
}

static boolean SDLGPU_CompileShader(int slot)
{
	if (slot < 0 || slot >= HWR_MAXSHADERS)
		I_Error("CompileShader: Invalid slot %d", slot);

	return Program_Compile(&programs[slot], HWR_GetShaderName(slot % NUMSHADERTARGETS));
}

static void SDLGPU_SetShaderInfo(hwdshaderinfo_t info, INT32 value)
{
	switch (info)
	{
		case HWD_SHADERINFO_LEVELTIME:
			shader_leveltime = (((float)(value-1)) + FIXED_TO_FLOAT(rendertimefrac)) / TICRATE;
			break;
		default:
			break;
	}
}

static void SDLGPU_UnSetShader(void)
{
	current_program = NULL;
}

static void SDLGPU_SetShader(int slot)
{
	if (slot < 0 || slot >= HWR_MAXSHADERS) // SHADER_NONE
		SDLGPU_UnSetShader();
	else
		current_program = &programs[slot];
}

// ==========================================================================
//                                                                  PIPELINES
// ==========================================================================

// Everything OpenGL has as state that SDL GPU wants decided in advance.
typedef struct
{
	UINT32 program; // gpuprogram_t.id
	UINT8 vertexformat;
	UINT8 primitive; // SDL_GPUPrimitiveType
	UINT8 blend;
	UINT8 cull;
	UINT8 flags; // PIPELINE_*
} pipelinekey_t;

#define PIPELINE_NODEPTHTEST 1
#define PIPELINE_DEPTHWRITE 2
#define PIPELINE_DECAL 4
#define PIPELINE_WIREFRAME 8

enum
{
	BLEND_NONE,
	BLEND_TRANSLUCENT,
	BLEND_MASKED,
	BLEND_ADDITIVE,
	BLEND_ENVIRONMENT,
	BLEND_SUBSTRACTIVE,
	BLEND_FOG,
	BLEND_INVISIBLE,
	NUMBLENDS
};

typedef struct
{
	pipelinekey_t key;
	SDL_GPUGraphicsPipeline *pipeline;
} pipelineentry_t;

static pipelineentry_t *pipelines = NULL;
static size_t numpipelines = 0;
static size_t maxpipelines = 0;

static void Pipelines_ForgetProgram(UINT32 id)
{
	size_t i;

	for (i = 0; i < numpipelines;)
	{
		if (pipelines[i].key.program == id)
		{
			if (bound_pipeline == pipelines[i].pipeline)
				bound_pipeline = NULL;
			SDL_ReleaseGPUGraphicsPipeline(device, pipelines[i].pipeline);
			pipelines[i] = pipelines[--numpipelines];
		}
		else
			i++;
	}
}

static SDL_GPUGraphicsPipeline *Pipeline_Create(const pipelinekey_t *key, const gpuprogram_t *program)
{
	static const struct
	{
		SDL_GPUBlendFactor source, destination;
	} blends[NUMBLENDS] =
	{
		{SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ZERO},                        // the same as no blending
		{SDL_GPU_BLENDFACTOR_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA},   // alpha = level of transparency
		{SDL_GPU_BLENDFACTOR_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ZERO},                  // 0 alpha = holes in texture
		{SDL_GPU_BLENDFACTOR_SRC_ALPHA, SDL_GPU_BLENDFACTOR_ONE},                   // src * alpha + dest
		{SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA},
		{SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR},
		{SDL_GPU_BLENDFACTOR_SRC_ALPHA, SDL_GPU_BLENDFACTOR_SRC_COLOR},             // fog
		{SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_ONE},                        // transparent blending
	};

	// gpuvertex_t, plus a buffer with one normal for all vertices
	static const SDL_GPUVertexBufferDescription stream_buffers[] =
	{
		{0, sizeof (gpuvertex_t), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
		{1, 16, SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
	};
	static const SDL_GPUVertexAttribute stream_attributes[] =
	{
		{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(gpuvertex_t, x)},
		{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(gpuvertex_t, s)},
		{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(gpuvertex_t, r)},
		{3, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
		{4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(gpuvertex_t, x)},
		{5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, 0},
	};

	// gpumodelvertex_t for the frame, the same for the next frame, plus a buffer with one colour for all vertices
	static const SDL_GPUVertexBufferDescription model_buffers[] =
	{
		{0, sizeof (gpumodelvertex_t), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
		{1, sizeof (gpumodelvertex_t), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0},
		{2, 16, SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0},
	};
	static const SDL_GPUVertexAttribute model_attributes[] =
	{
		{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(gpumodelvertex_t, x)},
		{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(gpumodelvertex_t, s)},
		{2, 2, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, 12},
		{3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(gpumodelvertex_t, nx)},
		{4, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(gpumodelvertex_t, x)},
		{5, 1, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(gpumodelvertex_t, nx)},
	};

	SDL_GPUGraphicsPipelineCreateInfo info;
	SDL_GPUColorTargetDescription color;
	SDL_GPUGraphicsPipeline *pipeline;

	SDL_zero(info);
	info.vertex_shader = program->vertex;
	info.fragment_shader = program->fragment;

	if (key->vertexformat == VERTEXFORMAT_MODEL)
	{
		info.vertex_input_state.vertex_buffer_descriptions = model_buffers;
		info.vertex_input_state.num_vertex_buffers = SDL_arraysize(model_buffers);
		info.vertex_input_state.vertex_attributes = model_attributes;
		info.vertex_input_state.num_vertex_attributes = SDL_arraysize(model_attributes);
	}
	else
	{
		info.vertex_input_state.vertex_buffer_descriptions = stream_buffers;
		info.vertex_input_state.num_vertex_buffers = SDL_arraysize(stream_buffers);
		info.vertex_input_state.vertex_attributes = stream_attributes;
		info.vertex_input_state.num_vertex_attributes = SDL_arraysize(stream_attributes);
	}

	info.primitive_type = (SDL_GPUPrimitiveType)key->primitive;

	info.rasterizer_state.fill_mode = (key->flags & PIPELINE_WIREFRAME) ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = (key->cull == CULL_FRONT) ? SDL_GPU_CULLMODE_FRONT
		: (key->cull == CULL_BACK) ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE;
	info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	info.rasterizer_state.enable_depth_clip = true;
	if (key->flags & PIPELINE_DECAL)
	{
		// polygon offset
		info.rasterizer_state.enable_depth_bias = true;
		info.rasterizer_state.depth_bias_constant_factor = -1.0f;
		info.rasterizer_state.depth_bias_slope_factor = -1.0f;
	}

	info.depth_stencil_state.enable_depth_test = true;
	info.depth_stencil_state.compare_op = (key->flags & PIPELINE_NODEPTHTEST) ? SDL_GPU_COMPAREOP_ALWAYS : SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	info.depth_stencil_state.enable_depth_write = (key->flags & PIPELINE_DEPTHWRITE) != 0;

	SDL_zero(color);
	color.format = COLOR_FORMAT;
	color.blend_state.enable_blend = true;
	color.blend_state.src_color_blendfactor = color.blend_state.src_alpha_blendfactor = blends[key->blend].source;
	color.blend_state.dst_color_blendfactor = color.blend_state.dst_alpha_blendfactor = blends[key->blend].destination;
	color.blend_state.color_blend_op = color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	// The alpha of the picture is never written: it stays opaque, like OpenGL's
	// window, so that copies of the screen can be drawn like any texture.
	color.blend_state.enable_color_write_mask = true;
	color.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;

	info.target_info.color_target_descriptions = &color;
	info.target_info.num_color_targets = 1;
	info.target_info.has_depth_stencil_target = true;
	info.target_info.depth_stencil_format = depth_format;

	pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
	if (!pipeline)
		I_Error("SDL GPU: couldn't create a pipeline: %s", SDL_GetError());
	return pipeline;
}

static SDL_GPUGraphicsPipeline *GetPipeline(const pipelinekey_t *key, const gpuprogram_t *program)
{
	size_t i;

	for (i = 0; i < numpipelines; i++)
		if (!memcmp(&pipelines[i].key, key, sizeof (*key)))
			return pipelines[i].pipeline;

	if (numpipelines == maxpipelines)
	{
		maxpipelines += 64;
		pipelines = realloc(pipelines, maxpipelines * sizeof (*pipelines));
		if (!pipelines)
			I_Error("SDL GPU: out of memory");
	}

	pipelines[numpipelines].key = *key;
	pipelines[numpipelines].pipeline = Pipeline_Create(key, program);
	return pipelines[numpipelines++].pipeline;
}

// ==========================================================================
//                                                                    DRAWING
// ==========================================================================

static SDL_GPUBuffer *defaults_buffer = NULL; // a normal and a colour, for the vertices that have none
static SDL_GPUBuffer *fan_index_buffer = NULL; // the triangles of a polygon given as a fan

typedef struct
{
	gpuprogram_t *program;
	vertexformat_t vertexformat;
	SDL_GPUPrimitiveType primitive;
	FBITFIELD flags; // polygon flags: blending, depth...
	cullmode_t cull;

	SDL_GPUBuffer *vertices;
	SDL_GPUBuffer *next_vertices; // models: the next frame
	Uint32 first_vertex;
	Uint32 count; // of vertices, or of indices if there are indices

	SDL_GPUBuffer *indices;
	SDL_GPUIndexElementSize index_size;
	Uint32 first_index;

	gputexture_t *texture;
	gputexture_t *lighttable; // NULL for the current one
	const float *color; // what the vertex colours are multiplied by
	const float *modelview; // NULL for the current one
	float lerp;
} drawcall_t;

static unsigned BlendFromFlags(FBITFIELD PolyFlags)
{
	if (PolyFlags & PF_Invisible)
		return BLEND_INVISIBLE;

	switch (PolyFlags & PF_Blending)
	{
		case PF_Translucent & PF_Blending: return BLEND_TRANSLUCENT;
		case PF_Masked & PF_Blending:      return BLEND_MASKED;
		case PF_Additive & PF_Blending:    return BLEND_ADDITIVE;
		case PF_Environment & PF_Blending: return BLEND_ENVIRONMENT;
		case PF_Substractive & PF_Blending:return BLEND_SUBSTRACTIVE;
		case PF_Fog & PF_Fog:              return BLEND_FOG;
		default:                           return BLEND_NONE;
	}
}

// OpenGL's alpha test: fragments with an alpha up to this are discarded
static float AlphaThresholdFromFlags(FBITFIELD PolyFlags)
{
	if (PolyFlags & PF_NoAlphaTest)
		return -1.0f;

	switch (PolyFlags & PF_Blending)
	{
		case PF_Translucent & PF_Blending:
		case PF_Additive & PF_Blending:
		case PF_Environment & PF_Blending:
		case PF_Substractive & PF_Blending:
			return 0.0f; // discard 0 alpha pixels
		case PF_Fog & PF_Fog:
			return -1.0f; // don't discard zero alpha fragments
		default:
			return 0.5f;
	}
}

static void Draw(const drawcall_t *draw)
{
	SDL_GPURenderPass *pass = RenderPass();
	SDL_GPUCommandBuffer *cmd = render_cmd;
	SDL_GPUGraphicsPipeline *pipeline;
	SDL_GPUTextureSamplerBinding textures_bound[GPU_NUM_FRAGMENT_SAMPLERS];
	SDL_GPUBufferBinding buffers[3];
	gputexture_t *lighttable = draw->lighttable ? draw->lighttable : current_lighttable;
	gputexture_t *texture = draw->texture;
	pipelinekey_t key;
	gpu_uniforms_t u;

	if (!draw->count)
		return;

	if (!texture || !texture->texture)
		texture = &white_texture;
	if (!lighttable || !lighttable->texture)
		lighttable = &dummy_table_texture;

	// the pipeline
	memset(&key, 0, sizeof (key)); // it is compared as memory
	key.program = draw->program->id;
	key.vertexformat = (UINT8)draw->vertexformat;
	key.primitive = (UINT8)draw->primitive;
	key.blend = (UINT8)BlendFromFlags(draw->flags);
	key.cull = (UINT8)draw->cull;
	if (draw->flags & PF_NoDepthTest)
		key.flags |= PIPELINE_NODEPTHTEST;
	if (draw->flags & PF_Occlude)
		key.flags |= PIPELINE_DEPTHWRITE;
	if (draw->flags & PF_Decal)
		key.flags |= PIPELINE_DECAL;
	if (wireframe && draw->primitive != SDL_GPU_PRIMITIVETYPE_LINELIST)
		key.flags |= PIPELINE_WIREFRAME;

	pipeline = GetPipeline(&key, draw->program);
	if (pipeline != bound_pipeline)
	{
		SDL_BindGPUGraphicsPipeline(pass, pipeline);
		bound_pipeline = pipeline;
	}

	// the vertices
	SDL_zero(buffers);
	buffers[0].buffer = draw->vertices;
	if (draw->vertexformat == VERTEXFORMAT_MODEL)
	{
		buffers[1].buffer = draw->next_vertices ? draw->next_vertices : draw->vertices;
		buffers[2].buffer = defaults_buffer;
		SDL_BindGPUVertexBuffers(pass, 0, buffers, 3);
	}
	else
	{
		buffers[1].buffer = defaults_buffer;
		SDL_BindGPUVertexBuffers(pass, 0, buffers, 2);
	}

	// the textures
	textures_bound[0].texture = texture->texture;
	textures_bound[0].sampler = Texture_Sampler(texture, draw->flags);
	textures_bound[1].texture = palette_lookup_texture.texture;
	textures_bound[1].sampler = GetSampler(0);
	textures_bound[2].texture = lighttable->texture;
	textures_bound[2].sampler = Texture_Sampler(lighttable, 0);
	textures_bound[3].texture = screen_palette_texture.texture;
	textures_bound[3].sampler = GetSampler(0);
	SDL_BindGPUFragmentSamplers(pass, 0, textures_bound, GPU_NUM_FRAGMENT_SAMPLERS);
	texture->used_segment = lighttable->used_segment = segment;
	palette_lookup_texture.used_segment = screen_palette_texture.used_segment = segment;

	// the uniforms
	u = uniforms;
	if (draw->modelview)
		memcpy(u.modelview, draw->modelview, sizeof (u.modelview));
	memcpy(u.current_color, draw->color ? draw->color : white, sizeof (u.current_color));
	u.leveltime = shader_leveltime;
	u.scr_resolution[0] = (float)vid.width;
	u.scr_resolution[1] = (float)vid.height;
	u.alpha_threshold = AlphaThresholdFromFlags(draw->flags);
	u.lerp = draw->lerp;
	u.target_size[0] = (float)screen_width;
	u.target_size[1] = (float)screen_height;
	SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof (u));
	SDL_PushGPUFragmentUniformData(cmd, 0, &u, sizeof (u));

	if (draw->indices)
	{
		SDL_GPUBufferBinding index_binding;
		SDL_zero(index_binding);
		index_binding.buffer = draw->indices;
		SDL_BindGPUIndexBuffer(pass, &index_binding, draw->index_size);
		SDL_DrawGPUIndexedPrimitives(pass, draw->count, 1, draw->first_index, (Sint32)draw->first_vertex, 0);
	}
	else
		SDL_DrawGPUPrimitives(pass, draw->count, 1, draw->first_vertex, 0);
}

static void DrawCall_Init(drawcall_t *draw, FBITFIELD PolyFlags)
{
	memset(draw, 0, sizeof (*draw));
	draw->program = ActiveProgram();
	draw->vertexformat = VERTEXFORMAT_STREAM;
	draw->primitive = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	draw->flags = PolyFlags;
	draw->cull = CULL_NONE;
	draw->texture = (PolyFlags & PF_NoTexture) ? &white_texture : current_texture;
}

// Gives memory for the vertices of a draw call
static gpuvertex_t *DrawCall_Vertices(drawcall_t *draw, Uint32 count)
{
	return Stream_Alloc(&vertex_stream, count * sizeof (gpuvertex_t), sizeof (gpuvertex_t), &draw->vertices, &draw->first_vertex);
}

static void Vertex_Set(gpuvertex_t *vertex, float x, float y, float z, float s, float t)
{
	vertex->x = x;
	vertex->y = y;
	vertex->z = z;
	vertex->s = s;
	vertex->t = t;
	vertex->r = vertex->g = vertex->b = vertex->a = 255;
}

// Makes a draw call draw count vertices as a triangle fan
static void DrawCall_Fan(drawcall_t *draw, Uint32 count)
{
	if (count < 3)
	{
		draw->count = 0;
		return;
	}

	draw->count = (count - 2) * 3;
	draw->index_size = SDL_GPU_INDEXELEMENTSIZE_16BIT;

	if (count <= MAX_FAN_VERTICES)
	{
		draw->indices = fan_index_buffer;
		draw->first_index = 0;
	}
	else if (count <= 65536)
	{
		UINT16 *indices = Stream_Alloc(&index_stream, draw->count * sizeof (UINT16), sizeof (UINT16), &draw->indices, &draw->first_index);
		Uint32 i;
		for (i = 2; i < count; i++)
		{
			*indices++ = 0;
			*indices++ = (UINT16)(i - 1);
			*indices++ = (UINT16)i;
		}
	}
	else
		draw->count = 0;
}

// Draws the whole screen with a texture
static void DrawScreenQuad(drawcall_t *draw)
{
	gpuvertex_t *v = DrawCall_Vertices(draw, 4);

	// the first row of a texture is the top of the screen
	Vertex_Set(&v[0], -1.0f, -1.0f, 1.0f, 0.0f, 1.0f);
	Vertex_Set(&v[1], -1.0f,  1.0f, 1.0f, 0.0f, 0.0f);
	Vertex_Set(&v[2],  1.0f,  1.0f, 1.0f, 1.0f, 0.0f);
	Vertex_Set(&v[3],  1.0f, -1.0f, 1.0f, 1.0f, 1.0f);

	DrawCall_Fan(draw, 4);
	Draw(draw);
}

// -----------------+
// SetBlend         : Set render mode
// -----------------+
static void SDLGPU_SetBlend(FBITFIELD PolyFlags)
{
	// what the flags mean is worked out when drawing
	CurrentPolyFlags = PolyFlags;
	if (PolyFlags & PF_NoTexture)
		current_texture = &white_texture;
}

// -----------------+
// GClipRect        : Defines the 2D hardware clipping window
// -----------------+
static void SDLGPU_GClipRect(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip)
{
	viewport.x = (float)minx;
	viewport.y = (float)miny;
	viewport.w = (float)(maxx - minx);
	viewport.h = (float)(maxy - miny);
	viewport.min_depth = 0.0f;
	viewport.max_depth = 1.0f;
	if (render_pass)
		SDL_SetGPUViewport(render_pass, &viewport);

	NEAR_CLIPPING_PLANE = nearclip;

	Mat_Identity(uniforms.projection);
	Mat_Perspective(uniforms.projection, fov, ASPECT_RATIO);
}

// -----------------+
// ClearBuffer      : Clear the color/alpha/depth buffer(s)
// -----------------+
static void SDLGPU_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FRGBAFloat *ClearColor)
{
	if (ColorMask || DepthMask)
	{
		// Clearing is done when starting to draw. If some was done, start again.
		if (render_pass)
		{
			SDL_EndGPURenderPass(render_pass);
			render_pass = NULL;
		}

		if (ColorMask)
		{
			if (ClearColor)
			{
				clear_color.r = ClearColor->red;
				clear_color.g = ClearColor->green;
				clear_color.b = ClearColor->blue;
			}
			clear_color_pending = true;
		}
		if (DepthMask)
			clear_depth_pending = true;
	}

	SDLGPU_SetBlend(DepthMask ? PF_Occlude | CurrentPolyFlags : CurrentPolyFlags&~PF_Occlude);
}

// -----------------+
// Draw2DLine       : Render a 2D line
// -----------------+
static void SDLGPU_Draw2DLine(F2DCoord *v1, F2DCoord *v2, RGBA_t Color)
{
	drawcall_t draw;
	gpuvertex_t *v;
	float dx, dy;
	float angle;
	float color[4];

	// creating a polygon
	if (fabsf(v2->x - v1->x) > FLT_EPSILON)
		angle = (float)atan((v2->y-v1->y)/(v2->x-v1->x));
	else
		angle = (float)(M_PIl/2.0f);
	dx = (float)sin(angle) / (float)screen_width;
	dy = (float)cos(angle) / (float)screen_height;

	color[0] = Color.s.red / 255.0f;
	color[1] = Color.s.green / 255.0f;
	color[2] = Color.s.blue / 255.0f;
	color[3] = Color.s.alpha / 255.0f;

	DrawCall_Init(&draw, CurrentPolyFlags);
	draw.program = &plain_program;
	draw.texture = &white_texture;
	draw.color = color;

	v = DrawCall_Vertices(&draw, 4);
	Vertex_Set(&v[0], v1->x - dx, -(v1->y + dy), 1.0f, 0.0f, 0.0f);
	Vertex_Set(&v[1], v2->x - dx, -(v2->y + dy), 1.0f, 0.0f, 0.0f);
	Vertex_Set(&v[2], v2->x + dx, -(v2->y - dy), 1.0f, 0.0f, 0.0f);
	Vertex_Set(&v[3], v1->x + dx, -(v1->y - dy), 1.0f, 0.0f, 0.0f);

	DrawCall_Fan(&draw, 4);
	Draw(&draw);
}

// -----------------+
// UpdateTexture    : Updates the texture data.
// -----------------+
static void SDLGPU_UpdateTexture(FTextureInfo *pTexInfo)
{
	static RGBA_t *converted = NULL;
	static size_t converted_size = 0;
	const RGBA_t *pixels;
	gputexture_t *texture;
	const INT32 w = pTexInfo->width;
	const INT32 h = pTexInfo->height;
	const UINT8 *pImgData = (const UINT8 *)pTexInfo->data;
	Uint32 levels = 1;
	INT32 i;

	if (w <= 0 || h <= 0)
		return;

	if (pTexInfo->format == GL_TEXFMT_RGBA)
		pixels = pTexInfo->data;
	else
	{
		RGBA_t *tex;

		if ((size_t)(w * h) > converted_size)
		{
			converted_size = (size_t)(w * h);
			converted = realloc(converted, converted_size * sizeof (*converted));
			if (!converted)
				I_Error("SDL GPU: out of memory converting a %dx%d texture", w, h);
		}
		pixels = tex = converted;

		if ((pTexInfo->format == GL_TEXFMT_P_8) || (pTexInfo->format == GL_TEXFMT_AP_88))
		{
			for (i = 0; i < w * h; i++)
			{
				if ((*pImgData == HWR_PATCHES_CHROMAKEY_COLORINDEX) && (pTexInfo->flags & TF_CHROMAKEYED))
				{
					tex[i].rgba = 0;
					pTexInfo->flags |= TF_TRANSPARENT; // there is a hole in it
				}
				else
					tex[i] = texture_palette[*pImgData];

				pImgData++;

				if (pTexInfo->format == GL_TEXFMT_AP_88)
				{
					if (!(pTexInfo->flags & TF_CHROMAKEYED))
						tex[i].s.alpha = *pImgData;
					pImgData++;
				}
			}
		}
		else if (pTexInfo->format == GL_TEXFMT_ALPHA_INTENSITY_88)
		{
			for (i = 0; i < w * h; i++)
			{
				tex[i].s.red = tex[i].s.green = tex[i].s.blue = *pImgData++;
				tex[i].s.alpha = *pImgData++;
			}
		}
		else if (pTexInfo->format == GL_TEXFMT_ALPHA_8) // Used for fade masks
		{
			for (i = 0; i < w * h; i++)
			{
				// 255 because the fade mask is modulated with the screen texture, so alpha affects it while the colours don't
				tex[i].s.red = tex[i].s.green = tex[i].s.blue = 255;
				tex[i].s.alpha = *pImgData++;
			}
		}
		else
		{
			CONS_Alert(CONS_WARNING, "SDL GPU: SetTexture(bad format) %d\n", (int)pTexInfo->format);
			return;
		}
	}

	texture = Texture_FromId(pTexInfo->downloaded);
	if (!texture)
		pTexInfo->downloaded = Texture_NewId(&texture);

	// No mipmaps on transparent stuff
	if (filter_mipmap && !(pTexInfo->flags & TF_TRANSPARENT))
	{
		INT32 size = max(w, h);
		while (size > 1 && levels < 6)
		{
			size >>= 1;
			levels++;
		}
	}

	texture->flags = pTexInfo->flags;
	Texture_SetPixels(texture, pixels, (Uint32)w, (Uint32)h, levels);
	current_texture = texture;
}

// -----------------+
// SetTexture       : The mipmap becomes the current texture source
// -----------------+
static void SDLGPU_SetTexture(FTextureInfo *pTexInfo)
{
	if (!pTexInfo)
	{
		current_texture = &white_texture;
		return;
	}
	else if (pTexInfo->downloaded)
	{
		gputexture_t *texture = Texture_FromId(pTexInfo->downloaded);
		current_texture = texture ? texture : &white_texture;
	}
	else
	{
		SDLGPU_UpdateTexture(pTexInfo);
		if (!pTexInfo->downloaded)
			return;

		pTexInfo->nextmipmap = NULL;
		if (cache_tail)
		{ // insertion at the tail
			cache_tail->nextmipmap = pTexInfo;
			cache_tail = pTexInfo;
		}
		else // initialization of the linked list
			cache_tail = cache_head = pTexInfo;
	}
}

static void SDLGPU_SetTexturePalette(RGBA_t *palette)
{
	// on a palette change, you have to reload all of the textures
	if (memcmp(texture_palette, palette, sizeof (texture_palette)))
	{
		memcpy(texture_palette, palette, sizeof (texture_palette));
		FlushTextures();
	}
}

static void SDLGPU_ClearMipMapCache(void)
{
	FlushTextures();
}

static INT32 SDLGPU_GetTextureUsed(void)
{
	FTextureInfo *tmp = cache_head;
	INT32 res = 0;

	while (tmp)
	{
		res += tmp->height*tmp->width*4;
		tmp = tmp->nextmipmap;
	}
	return res;
}

// --------------------------------------------------------------------------
// The uniforms of a surface. Like the OpenGL driver, those a surface doesn't
// give keep the values of the last surface that did.
// --------------------------------------------------------------------------

static void Color_FromRGBA(float *color, RGBA_t rgba)
{
	color[0] = rgba.s.red / 255.0f;
	color[1] = rgba.s.green / 255.0f;
	color[2] = rgba.s.blue / 255.0f;
	color[3] = rgba.s.alpha / 255.0f;
}

static void SetSurfaceUniforms(FSurfaceInfo *Surface, const float *poly, const float *tint, const float *fade)
{
	memcpy(uniforms.poly_color, poly ? poly : white, sizeof (uniforms.poly_color));
	memcpy(uniforms.tint_color, tint ? tint : white, sizeof (uniforms.tint_color));
	memcpy(uniforms.fade_color, fade ? fade : white, sizeof (uniforms.fade_color));

	if (Surface != NULL)
	{
		uniforms.lighting = (float)Surface->LightInfo.light_level;
		uniforms.fade_start = (float)Surface->LightInfo.fade_start;
		uniforms.fade_end = (float)Surface->LightInfo.fade_end;
	}
}

static void SetLightTable(UINT32 id)
{
	gputexture_t *lighttable = id ? Texture_FromId(id) : NULL;
	if (lighttable)
		current_lighttable = lighttable;
}

// code that is common between DrawPolygon and DrawIndexedTriangles
// DrawScreenTexture also uses it
static void PreparePolygon(drawcall_t *draw, FSurfaceInfo *pSurf, FBITFIELD PolyFlags)
{
	static float poly[4] = {0,0,0,0};
	static float tint[4] = {0,0,0,0};
	static float fade[4] = {0,0,0,0};

	// Coronas are tested against the depth buffer like everything else.
	// (The OpenGL driver reads the depth buffer back for them where it can.)
	if (PolyFlags & PF_Corona)
		PolyFlags &= ~(PF_NoDepthTest|PF_Corona);

	SDLGPU_SetBlend(PolyFlags);
	DrawCall_Init(draw, PolyFlags);

	if (pSurf)
	{
		// If the surface is either modulated or colormapped, or both
		if (CurrentPolyFlags & (PF_Modulated | PF_ColorMapped))
			Color_FromRGBA(poly, pSurf->PolyColor);

		// If modulated, mix the surface colour to the texture
		if (CurrentPolyFlags & PF_Modulated)
			draw->color = poly;

		// Only if the surface is colormapped
		if (CurrentPolyFlags & PF_ColorMapped)
		{
			Color_FromRGBA(tint, pSurf->TintColor);
			Color_FromRGBA(fade, pSurf->FadeColor);
			SetLightTable(pSurf->LightTableId);
		}
	}

	SetSurfaceUniforms(pSurf, poly, tint, fade);
}

// -----------------+
// DrawPolygon      : Render a polygon, set the texture, set render mode
// -----------------+
static void SDLGPU_DrawPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags)
{
	drawcall_t draw;
	gpuvertex_t *v;
	FUINT i;

	if (!iNumPts)
		return;

	PreparePolygon(&draw, pSurf, PolyFlags);

	v = DrawCall_Vertices(&draw, (Uint32)iNumPts);
	for (i = 0; i < iNumPts; i++)
		Vertex_Set(&v[i], pOutVerts[i].x, pOutVerts[i].y, pOutVerts[i].z, pOutVerts[i].s, pOutVerts[i].t);

	if (PolyFlags & PF_WireFrame)
	{
		draw.primitive = SDL_GPU_PRIMITIVETYPE_LINELIST;
		draw.count = (Uint32)iNumPts & ~1u;
	}
	else
		DrawCall_Fan(&draw, (Uint32)iNumPts);

	Draw(&draw);
}

static void SDLGPU_DrawIndexedTriangles(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags, unsigned int *IndexArray)
{
	drawcall_t draw;
	gpuvertex_t *v;
	unsigned int maxindex = 0;
	FUINT i;

	if (!iNumPts)
		return;

	PreparePolygon(&draw, pSurf, PolyFlags);

	// how many vertices there are is not said
	for (i = 0; i < iNumPts; i++)
		if (IndexArray[i] > maxindex)
			maxindex = IndexArray[i];

	v = DrawCall_Vertices(&draw, maxindex + 1);
	for (i = 0; i <= maxindex; i++)
		Vertex_Set(&v[i], pOutVerts[i].x, pOutVerts[i].y, pOutVerts[i].z, pOutVerts[i].s, pOutVerts[i].t);

	memcpy(Stream_Alloc(&index_stream, (Uint32)iNumPts * sizeof (Uint32), sizeof (Uint32), &draw.indices, &draw.first_index),
		IndexArray, iNumPts * sizeof (Uint32));
	draw.index_size = SDL_GPU_INDEXELEMENTSIZE_32BIT;
	draw.count = (Uint32)iNumPts;

	Draw(&draw);
}

// ==========================================================================
//                                                    BUFFERS KEPT ON THE GPU
// ==========================================================================

// The frames of models and the sky dome. mdlframe_t.vboID, tinyframe_t.vboID
// and gl_sky_t.vbo are indices in this, plus one.
typedef struct
{
	SDL_GPUBuffer *vertices;
	SDL_GPUBuffer *indices;
	Uint32 count;
} gpumesh_t;

static gpumesh_t *meshes = NULL;
static size_t nummeshes = 0;

static unsigned int Mesh_New(gpumesh_t **mesh)
{
	size_t i;

	for (i = 0; i < nummeshes; i++)
		if (!meshes[i].vertices)
			break;

	if (i == nummeshes)
	{
		meshes = realloc(meshes, (nummeshes + 256) * sizeof (*meshes));
		if (!meshes)
			I_Error("SDL GPU: out of memory");
		memset(meshes + nummeshes, 0, 256 * sizeof (*meshes));
		nummeshes += 256;
	}

	*mesh = &meshes[i];
	return (unsigned int)i + 1;
}

static gpumesh_t *Mesh_FromId(unsigned int id)
{
	if (!id || id > nummeshes || !meshes[id - 1].vertices)
		return NULL;
	return &meshes[id - 1];
}

static void Mesh_Free(unsigned int id)
{
	gpumesh_t *mesh = Mesh_FromId(id);
	if (!mesh)
		return;
	SDL_ReleaseGPUBuffer(device, mesh->vertices);
	if (mesh->indices)
		SDL_ReleaseGPUBuffer(device, mesh->indices);
	memset(mesh, 0, sizeof (*mesh));
}

// --------------------------------------------------------------------------
// The sky
// --------------------------------------------------------------------------

static unsigned int sky_mesh = 0;

static void SDLGPU_RenderSkyDome(gl_sky_t *sky)
{
	gpumesh_t *mesh;
	drawcall_t draw;
	float modelview[16];

	if (!sky->data || !sky->vertex_count)
		return;

	// Build the sky dome! Yes!
	if (sky->rebuild || !Mesh_FromId(sky->vbo))
	{
		Uint32 *indices, *index;
		Uint32 count = 0;
		int i, j, k;

		// the game forgets about the previous one
		Mesh_Free(sky_mesh);
		sky->vbo = sky_mesh = Mesh_New(&mesh);

		// It is given as fans and strips. Fans can't be drawn as such, so make triangles of it all.
		for (i = 0; i < sky->loopcount; i++)
			if (sky->loops[i].vertexcount > 2)
				count += (Uint32)(sky->loops[i].vertexcount - 2) * 3;

		indices = index = malloc(max(count, 3) * sizeof (*indices));
		if (!indices)
			I_Error("SDL GPU: out of memory");

		// the parts without a texture first
		for (j = 0; j < 2; j++)
		{
			for (i = 0; i < sky->loopcount; i++)
			{
				gl_skyloopdef_t *loop = &sky->loops[i];

				if (j == 0 ? loop->use_texture : !loop->use_texture)
					continue;

				for (k = 2; k < loop->vertexcount; k++)
				{
					if (loop->mode == HWD_SKYLOOP_FAN)
					{
						*index++ = (Uint32)loop->vertexindex;
						*index++ = (Uint32)(loop->vertexindex + k - 1);
						*index++ = (Uint32)(loop->vertexindex + k);
					}
					else
					{
						*index++ = (Uint32)(loop->vertexindex + k - 2);
						*index++ = (Uint32)(loop->vertexindex + k - 1);
						*index++ = (Uint32)(loop->vertexindex + k);
					}
				}
			}
		}

		mesh->count = (Uint32)(index - indices);
		mesh->vertices = CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, (Uint32)sky->vertex_count * sizeof (sky->data[0]), sky->data);
		mesh->indices = CreateBuffer(SDL_GPU_BUFFERUSAGE_INDEX, max(mesh->count, 3) * sizeof (*indices), indices);
		free(indices);

		sky->rebuild = false;
	}

	mesh = Mesh_FromId(sky->vbo);

	// set transforms
	memcpy(modelview, uniforms.modelview, sizeof (modelview));
	Mat_Scale(modelview, 1.0f, (float)sky->height / 200.0f, 1.0f);
	Mat_Rotate(modelview, 270.0f, 0.0f, 1.0f, 0.0f);

	SetSurfaceUniforms(NULL, NULL, NULL, NULL);

	DrawCall_Init(&draw, CurrentPolyFlags);
	draw.modelview = modelview;
	draw.vertices = mesh->vertices;
	draw.indices = mesh->indices;
	draw.index_size = SDL_GPU_INDEXELEMENTSIZE_32BIT;
	draw.count = mesh->count;
	Draw(&draw);
}

// --------------------------------------------------------------------------
// Models
// --------------------------------------------------------------------------

static void CreateModelVBO(mesh_t *mesh, mdlframe_t *frame)
{
	const Uint32 count = (Uint32)mesh->numTriangles * 3;
	const Uint32 size = count * sizeof (gpumodelvertex_t);
	SDL_GPUTransferBuffer *transfer;
	gpumodelvertex_t *v;
	gpumesh_t *gpumesh;
	float *vertPtr = frame->vertices;
	float *normPtr = frame->normals;
	float *uvPtr = mesh->uvs;
	Uint32 i;

	if (!count)
		return;

	frame->vboID = Mesh_New(&gpumesh);
	gpumesh->count = count;
	gpumesh->vertices = CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, size, NULL);

	v = Staging_Begin(size, &transfer);
	for (i = 0; i < count; i++, v++)
	{
		v->x = *vertPtr++;
		v->y = *vertPtr++;
		v->z = *vertPtr++;
		v->nx = *normPtr++;
		v->ny = *normPtr++;
		v->nz = *normPtr++;
		v->s = *uvPtr++;
		v->t = *uvPtr++;
	}
	Staging_ToBuffer(transfer, gpumesh->vertices, size);
}

// "Tiny" frames store positions as shorts and normals as bytes, and share
// their vertices between triangles. They are turned into the same thing as
// the other frames, so that there is only one way to draw a model.
static void CreateModelVBOTiny(mesh_t *mesh, tinyframe_t *frame)
{
	const Uint32 count = (Uint32)mesh->numTriangles * 3;
	const Uint32 size = count * sizeof (gpumodelvertex_t);
	SDL_GPUTransferBuffer *transfer;
	gpumodelvertex_t *v;
	gpumesh_t *gpumesh;
	Uint32 i;

	if (!count || !mesh->indices)
		return;

	frame->vboID = Mesh_New(&gpumesh);
	gpumesh->count = count;
	gpumesh->vertices = CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, size, NULL);

	v = Staging_Begin(size, &transfer);
	for (i = 0; i < count; i++, v++)
	{
		const unsigned short index = mesh->indices[i];
		const short *vert = &frame->vertices[index * 3];
		const char *norm = &frame->normals[index * 3];

		v->x = vert[0] / 64.0f;
		v->y = vert[1] / 64.0f;
		v->z = vert[2] / 64.0f;
		v->nx = norm[0] / 127.0f;
		v->ny = norm[1] / 127.0f;
		v->nz = norm[2] / 127.0f;
		v->s = mesh->uvs[index * 2];
		v->t = mesh->uvs[index * 2 + 1];
	}
	Staging_ToBuffer(transfer, gpumesh->vertices, size);
}

static void SDLGPU_CreateModelVBOs(model_t *model)
{
	int i, j;

	for (i = 0; i < model->numMeshes; i++)
	{
		mesh_t *mesh = &model->meshes[i];

		if (mesh->frames)
		{
			for (j = 0; j < mesh->numFrames; j++)
			{
				mdlframe_t *frame = &mesh->frames[j];
				Mesh_Free(frame->vboID);
				frame->vboID = 0;
				CreateModelVBO(mesh, frame);
			}
		}
		else if (mesh->tinyframes)
		{
			for (j = 0; j < mesh->numFrames; j++)
			{
				tinyframe_t *frame = &mesh->tinyframes[j];
				Mesh_Free(frame->vboID);
				frame->vboID = 0;
				CreateModelVBOTiny(mesh, frame);
			}
		}
	}
}

// -----------------+
// DrawModel        : Draw a model.
// -----------------+
static void SDLGPU_DrawModel(model_t *model, INT32 frameIndex, float duration, float tics, INT32 nextFrameIndex, FTransform *pos, float scale, UINT8 flipped, FSurfaceInfo *Surface)
{
	float poly[4], tint[4], fade[4];
	float modelview[16];
	float pol = 0.0f;
	float scalex, scaley, scalez;
	drawcall_t draw;
	int i;

	// Affect input model scaling
	scale *= 0.5f;
	scalex = scale;
	scaley = scale;
	scalez = scale;

	if (duration > 0.0 && tics >= 0.0) // don't interpolate if instantaneous or infinite in length
	{
		float newtime = (duration - tics); // + 1;

		pol = newtime / duration;

		if (pol > 1.0f)
			pol = 1.0f;

		if (pol < 0.0f)
			pol = 0.0f;
	}

	Color_FromRGBA(poly, Surface->PolyColor);
	Color_FromRGBA(tint, Surface->TintColor);
	Color_FromRGBA(fade, Surface->FadeColor);

	SDLGPU_SetBlend((poly[3] < 1 ? PF_Translucent : (PF_Masked|PF_Occlude))|PF_Modulated);
	SetLightTable(Surface->LightTableId);
	SetSurfaceUniforms(Surface, poly, tint, fade);

	DrawCall_Init(&draw, CurrentPolyFlags);
	draw.vertexformat = VERTEXFORMAT_MODEL;
	draw.color = poly;

	// pos->flip is if the screen is flipped too
	// If either are active, but not both, invert the model's culling
	draw.cull = (flipped != pos->flip) ? CULL_FRONT : CULL_BACK;

	memcpy(modelview, uniforms.modelview, sizeof (modelview));
	Mat_Translate(modelview, pos->x, pos->z, pos->y);
	if (flipped)
		scaley = -scaley;
	Mat_Rotate(modelview, pos->anglex, -1.0f, 0.0f, 0.0f);
	Mat_Rotate(modelview, pos->angley, 0.0f, -1.0f, 0.0f);
	Mat_Scale(modelview, scalex, scaley, scalez);
	draw.modelview = modelview;

	for (i = 0; i < model->numMeshes; i++)
	{
		mesh_t *mesh = &model->meshes[i];
		gpumesh_t *frame = NULL, *nextframe = NULL;

		if (mesh->numFrames <= 0)
			continue;

		if (mesh->tinyframes)
		{
			frame = Mesh_FromId(mesh->tinyframes[frameIndex % mesh->numFrames].vboID);
			if (nextFrameIndex != -1)
				nextframe = Mesh_FromId(mesh->tinyframes[nextFrameIndex % mesh->numFrames].vboID);
		}
		else if (mesh->frames)
		{
			frame = Mesh_FromId(mesh->frames[frameIndex % mesh->numFrames].vboID);
			if (nextFrameIndex != -1)
				nextframe = Mesh_FromId(mesh->frames[nextFrameIndex % mesh->numFrames].vboID);
		}

		if (!frame)
			continue;

		// The vertex shader interpolates between the two frames
		draw.vertices = frame->vertices;
		draw.count = frame->count;
		if (nextframe && nextframe->count == frame->count && fpclassify(pol) != FP_ZERO)
		{
			draw.next_vertices = nextframe->vertices;
			draw.lerp = pol;
		}
		else
		{
			draw.next_vertices = NULL;
			draw.lerp = 0.0f;
		}

		Draw(&draw);
	}
}

// -----------------+
// SetTransform     :
// -----------------+
static void SDLGPU_SetTransform(FTransform *stransform)
{
	static boolean special_splitscreen;
	boolean shearing = false;
	float used_fov;

	Mat_Identity(uniforms.modelview);
	if (stransform)
	{
		used_fov = stransform->fovxangle;
		if (stransform->flip)
			Mat_Scale(uniforms.modelview, stransform->scalex, -stransform->scaley, -stransform->scalez);
		else
			Mat_Scale(uniforms.modelview, stransform->scalex, stransform->scaley, -stransform->scalez);

		if (stransform->roll)
			Mat_Rotate(uniforms.modelview, stransform->rollangle, 0.0f, 0.0f, 1.0f);
		Mat_Rotate(uniforms.modelview, stransform->anglex, 1.0f, 0.0f, 0.0f);
		Mat_Rotate(uniforms.modelview, stransform->angley+270.0f, 0.0f, 1.0f, 0.0f);
		Mat_Translate(uniforms.modelview, -stransform->x, -stransform->z, -stransform->y);

		special_splitscreen = stransform->splitscreen;
		shearing = stransform->shearing;
	}
	else
	{
		used_fov = fov;
		Mat_Scale(uniforms.modelview, 1.0f, 1.0f, -1.0f);
	}

	Mat_Identity(uniforms.projection);

	// Simulate Software's y-shearing
	// https://zdoom.org/wiki/Y-shearing
	if (shearing)
	{
		float fdy = stransform->viewaiming * 2;
		if (stransform->flip)
			fdy *= -1.0f;
		Mat_Translate(uniforms.projection, 0.0f, -fdy/BASEVIDHEIGHT, 0.0f);
	}

	if (special_splitscreen)
	{
		used_fov = (float)(atan(tan(used_fov*M_PI/360)*0.8)*360/M_PI);
		Mat_Perspective(uniforms.projection, used_fov, 2*ASPECT_RATIO);
	}
	else
		Mat_Perspective(uniforms.projection, used_fov, ASPECT_RATIO);
}

static void SDLGPU_SetSpecialState(hwdspecialstate_t IdState, INT32 Value)
{
	switch (IdState)
	{
		case HWD_SET_MODEL_LIGHTING:
			model_lighting = Value;
			break;

		case HWD_SET_SHADERS:
			// there is no drawing without shaders here
			break;

		case HWD_SET_TEXTUREFILTERMODE:
		{
			const boolean had_mipmaps = filter_mipmap;

			switch (Value)
			{
				case HWD_SET_TEXTUREFILTER_TRILINEAR:
					filter_min_linear = filter_mag_linear = filter_mipmap = true;
					break;
				case HWD_SET_TEXTUREFILTER_BILINEAR:
					filter_min_linear = filter_mag_linear = true;
					filter_mipmap = false;
					break;
				case HWD_SET_TEXTUREFILTER_POINTSAMPLED:
					filter_min_linear = filter_mag_linear = filter_mipmap = false;
					break;
				case HWD_SET_TEXTUREFILTER_MIXED1:
					filter_min_linear = false;
					filter_mag_linear = true;
					filter_mipmap = false;
					break;
				case HWD_SET_TEXTUREFILTER_MIXED2:
					filter_min_linear = true;
					filter_mag_linear = false;
					filter_mipmap = false;
					break;
				case HWD_SET_TEXTUREFILTER_MIXED3:
					filter_min_linear = true;
					filter_mag_linear = false;
					filter_mipmap = true;
					break;
				default:
					filter_mag_linear = true;
					filter_min_linear = false;
					filter_mipmap = false;
			}

			// Only the textures loaded without mipmaps have to be loaded again
			if (filter_mipmap && !had_mipmaps)
				FlushTextures();
			break;
		}

		case HWD_SET_TEXTUREANISOTROPICMODE:
			filter_anisotropy = min(max(Value, 1), 16);
			break;

		case HWD_SET_WIREFRAME:
			wireframe = (Value != 0);
			break;

		default:
			break;
	}
}

// ==========================================================================
//                                                            SCREEN TEXTURES
// ==========================================================================

static void SDLGPU_FlushScreenTextures(void)
{
	int i;
	for (i = 0; i < NUMSCREENTEXTURES; i++)
		Texture_Release(&screen_textures[i]);
}

// Create a texture from the screen.
static void SDLGPU_MakeScreenTexture(int tex)
{
	gputexture_t *texture = &screen_textures[tex];
	SDL_GPUTextureLocation source, destination;
	SDL_GPUCopyPass *copy;

	if (!color_target)
		return;

	if (!texture->texture)
	{
		texture->texture = CreateTexture(SDL_GPU_TEXTURETYPE_2D, COLOR_FORMAT, SDL_GPU_TEXTUREUSAGE_SAMPLER,
			(Uint32)screen_width, (Uint32)screen_height, 1, 1);
		texture->width = (Uint32)screen_width;
		texture->height = (Uint32)screen_height;
		texture->levels = 1;
		texture->flags = 0;
		texture->nofilter = true;
	}

	// Copying can't be done while drawing, but it is done in order with the draw calls
	EndRenderPass();

	SDL_zero(source);
	source.texture = color_target;
	SDL_zero(destination);
	destination.texture = texture->texture;

	copy = SDL_BeginGPUCopyPass(RenderCommands());
	SDL_CopyGPUTextureToTexture(copy, &source, &destination, (Uint32)screen_width, (Uint32)screen_height, 1, false);
	SDL_EndGPUCopyPass(copy);

	current_texture = texture;
}

static void SDLGPU_DrawScreenTexture(int tex, FSurfaceInfo *surf, FBITFIELD polyflags)
{
	drawcall_t draw;
	FRGBAFloat black = {0.0f, 0.0f, 0.0f, 1.0f};
	const FBITFIELD oldflags = CurrentPolyFlags;

	SDLGPU_ClearBuffer(true, true, &black);
	CurrentPolyFlags = oldflags;

	current_texture = &screen_textures[tex];
	PreparePolygon(&draw, surf, surf ? polyflags : (PF_NoDepthTest));
	DrawScreenQuad(&draw);
}

// Do screen fades!
static void SDLGPU_DoScreenWipe(int wipeStart, int wipeEnd)
{
	gputexture_t *fademask = current_texture; // the fade mask that has been set
	drawcall_t draw;
	FRGBAFloat black = {0.0f, 0.0f, 0.0f, 1.0f};

	SDLGPU_ClearBuffer(true, true, &black);

	// Draw the original screen
	SDLGPU_SetBlend(PF_Modulated|PF_NoDepthTest);
	DrawCall_Init(&draw, CurrentPolyFlags);
	draw.program = &plain_program;
	draw.texture = &screen_textures[wipeStart];
	DrawScreenQuad(&draw);

	// Draw the end screen that fades in
	SDLGPU_SetBlend(PF_Modulated|PF_Translucent|PF_NoDepthTest);
	DrawCall_Init(&draw, CurrentPolyFlags);
	draw.program = &wipe_program;
	draw.texture = &screen_textures[wipeEnd];
	draw.lighttable = fademask;
	DrawScreenQuad(&draw);

	current_texture = &screen_textures[wipeEnd];
}

static void SDLGPU_DrawScreenFinalTexture(int tex, int width, int height)
{
	// Showing the picture in the window is SDLGPU_Present's job.
	(void)tex;
	(void)width;
	(void)height;
}

static void SDLGPU_PostImgRedraw(float points[SCREENVERTS][SCREENVERTS][2])
{
	static const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	const FBITFIELD flags = PF_NoDepthTest|PF_NoAlphaTest;
	gputexture_t *screen = current_texture; // the screen texture that was just made
	drawcall_t draw;
	gpuvertex_t *v;
	UINT16 *indices;
	INT32 x, y;

	// Draw a black square behind the screen texture,
	// so nothing shows through the edges
	DrawCall_Init(&draw, flags);
	draw.program = &plain_program;
	draw.texture = &white_texture;
	draw.color = black;
	v = DrawCall_Vertices(&draw, 4);
	Vertex_Set(&v[0], -16.0f, -16.0f, 6.0f, 0.0f, 0.0f);
	Vertex_Set(&v[1], -16.0f,  16.0f, 6.0f, 0.0f, 0.0f);
	Vertex_Set(&v[2],  16.0f,  16.0f, 6.0f, 0.0f, 0.0f);
	Vertex_Set(&v[3],  16.0f, -16.0f, 6.0f, 0.0f, 0.0f);
	DrawCall_Fan(&draw, 4);
	Draw(&draw);

	// The screen, as a grid of points that were moved around
	DrawCall_Init(&draw, flags);
	draw.program = &plain_program;
	draw.texture = screen;
	v = DrawCall_Vertices(&draw, SCREENVERTS * SCREENVERTS);
	for (x = 0; x < SCREENVERTS; x++)
		for (y = 0; y < SCREENVERTS; y++)
			Vertex_Set(&v[x * SCREENVERTS + y], points[x][y][0], points[x][y][1], 4.4f,
				(float)x / (SCREENVERTS-1), 1.0f - (float)y / (SCREENVERTS-1));

	draw.count = (SCREENVERTS-1) * (SCREENVERTS-1) * 6;
	draw.index_size = SDL_GPU_INDEXELEMENTSIZE_16BIT;
	indices = Stream_Alloc(&index_stream, draw.count * sizeof (UINT16), sizeof (UINT16), &draw.indices, &draw.first_index);
	for (x = 0; x < SCREENVERTS-1; x++)
		for (y = 0; y < SCREENVERTS-1; y++)
		{
			const UINT16 a = (UINT16)(x * SCREENVERTS + y);
			const UINT16 b = (UINT16)(x * SCREENVERTS + y + 1);
			const UINT16 c = (UINT16)((x + 1) * SCREENVERTS + y + 1);
			const UINT16 d = (UINT16)((x + 1) * SCREENVERTS + y);
			*indices++ = a; *indices++ = b; *indices++ = c;
			*indices++ = a; *indices++ = c; *indices++ = d;
		}
	Draw(&draw);
}

// Writes screen texture tex into dst_data.
// Pixel format is 24-bit RGB. Row order is top to bottom.
// Dimensions are screen_width * screen_height.
static void SDLGPU_ReadScreenTexture(int tex, UINT8 *dst_data)
{
	SDL_GPUTexture *texture = screen_textures[tex].texture ? screen_textures[tex].texture : color_target;
	const Uint32 size = (Uint32)(screen_width * screen_height * 4);
	SDL_GPUTransferBufferCreateInfo info;
	SDL_GPUTransferBuffer *transfer;
	SDL_GPUTextureRegion source;
	SDL_GPUTextureTransferInfo destination;
	SDL_GPUCommandBuffer *cmd;
	SDL_GPUCopyPass *copy;
	SDL_GPUFence *fence;
	const UINT8 *pixels;
	INT32 i;

	memset(dst_data, 0, (size_t)(screen_width * screen_height * 3));
	if (!texture)
		return;

	// what it is a copy of has to be drawn first
	Flush();

	SDL_zero(info);
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	info.size = size;
	transfer = SDL_CreateGPUTransferBuffer(device, &info);
	cmd = SDL_AcquireGPUCommandBuffer(device);
	if (!transfer || !cmd)
	{
		GPU_Error("couldn't read the screen");
		return;
	}

	SDL_zero(source);
	source.texture = texture;
	source.w = (Uint32)screen_width;
	source.h = (Uint32)screen_height;
	source.d = 1;
	SDL_zero(destination);
	destination.transfer_buffer = transfer;
	destination.pixels_per_row = (Uint32)screen_width;
	destination.rows_per_layer = (Uint32)screen_height;

	copy = SDL_BeginGPUCopyPass(cmd);
	SDL_DownloadFromGPUTexture(copy, &source, &destination);
	SDL_EndGPUCopyPass(copy);

	// wait for the GPU to be done with it
	fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
	if (fence)
	{
		SDL_WaitForGPUFences(device, true, &fence, 1);
		SDL_ReleaseGPUFence(device, fence);

		pixels = SDL_MapGPUTransferBuffer(device, transfer, false);
		if (pixels)
		{
			for (i = 0; i < screen_width * screen_height; i++)
			{
				dst_data[i*3] = pixels[i*4];
				dst_data[i*3 + 1] = pixels[i*4 + 1];
				dst_data[i*3 + 2] = pixels[i*4 + 2];
			}
			SDL_UnmapGPUTransferBuffer(device, transfer);
		}
	}
	else
		GPU_Error("couldn't read the screen");

	SDL_ReleaseGPUTransferBuffer(device, transfer);
}

// ==========================================================================
//                                                          PALETTE RENDERING
// ==========================================================================

static void SDLGPU_SetPaletteLookup(UINT8 *lut)
{
	const Uint32 size = HWR_PALETTE_LUT_SIZE * HWR_PALETTE_LUT_SIZE * HWR_PALETTE_LUT_SIZE;
	SDL_GPUTransferBuffer *transfer;

	Texture_WillChange(&palette_lookup_texture);
	memcpy(Staging_Begin(size, &transfer), lut, size);
	Staging_ToTexture(transfer, palette_lookup_texture.texture, HWR_PALETTE_LUT_SIZE, HWR_PALETTE_LUT_SIZE, HWR_PALETTE_LUT_SIZE);
}

static UINT32 SDLGPU_CreateLightTable(RGBA_t *hw_lighttable)
{
	gputexture_t *texture;
	UINT32 id = Texture_NewId(&texture);

	texture->nofilter = true;
	texture->lighttable = true;
	Texture_SetPixels(texture, hw_lighttable, 256, 32, 1);
	return id;
}

static void SDLGPU_UpdateLightTable(UINT32 id, RGBA_t *hw_lighttable)
{
	gputexture_t *texture = Texture_FromId(id);
	if (texture && texture->lighttable)
		Texture_SetPixels(texture, hw_lighttable, 256, 32, 1);
}

// Delete light table textures, ids given before become invalid and must not be used.
static void SDLGPU_ClearLightTables(void)
{
	size_t i;

	for (i = 0; i < numgputextures; i++)
		if (gputextures[i] && gputextures[i]->lighttable)
			Texture_FreeId((UINT32)i + 1);
}

// This palette is used for the palette rendering postprocessing step.
static void SDLGPU_SetScreenPalette(RGBA_t *palette)
{
	if (memcmp(screen_palette, palette, sizeof (screen_palette)))
	{
		memcpy(screen_palette, palette, sizeof (screen_palette));
		Texture_SetPixels(&screen_palette_texture, palette, 256, 1, 1);
	}
}

// ==========================================================================
//                                                        STARTUP AND SHUTDOWN
// ==========================================================================

static void SDLGPU_FinishUpdate(INT32 waitvbl)
{
	// see SDLGPU_Present
	(void)waitvbl;
}

static boolean SDLGPU_Init(void)
{
	static const struct
	{
		float normal[3];
		UINT8 color[4];
	} defaults = {{0.0f, 0.0f, 1.0f}, {255, 255, 255, 255}};
	static const UINT8 white_pixel[4] = {255, 255, 255, 255};
	static UINT8 zeroes[256 * 32 * 4];
	SDL_GPUShaderFormat formats = 0;
	SDL_GPUTransferBuffer *transfer;
	UINT16 *fan;
	size_t i;

	if (device)
		return true;

	// SDL chooses between Vulkan, Direct3D 12 and Metal from the shader formats it can be given.
	// The game's own shaders have to be available.
	// Vulkan and Metal are tried first: the shaders of addons can only be made for them.
	// (SDL_GPU_DRIVER=direct3d12 in the environment still picks Direct3D 12.)
	// Each is tried with anisotropic filtering first, then without, for the GPUs that don't have it.
	formats = PrecompiledFormats(false);
	device_anisotropy = true;
	device = CreateDevice(formats, true);
	if (!device && PrecompiledFormats(true))
	{
		formats |= PrecompiledFormats(true);
		device = CreateDevice(formats, true);
	}
	if (!device)
	{
		device = CreateDevice(formats, false);
		if (device)
		{
			device_anisotropy = false;
			CONS_Printf("SDL GPU: this GPU can't do anisotropic filtering\n");
		}
	}
	if (!device)
	{
		CONS_Alert(CONS_ERROR, "Could not create a GPU device: %s\n"
			"This build has shaders for:%s%s%s\n", SDL_GetError(),
			(formats & SDL_GPU_SHADERFORMAT_SPIRV) ? " Vulkan" : "",
			(formats & SDL_GPU_SHADERFORMAT_MSL) ? " Metal" : "",
			(formats & SDL_GPU_SHADERFORMAT_DXIL) ? " Direct3D 12" : "");
		return false;
	}

	device_formats = SDL_GetGPUShaderFormats(device);
	CONS_Printf("SDL GPU driver: %s\n", SDL_GetGPUDeviceDriver(device));
	{
		const char *name = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(device), SDL_PROP_GPU_DEVICE_NAME_STRING, NULL);
		CONS_Printf("SDL GPU device: %s%s\n", name ? name : "unknown", PreferLowPower() ? " (low power preferred)" : "");
	}

	depth_format = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
		SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
		? SDL_GPU_TEXTUREFORMAT_D32_FLOAT : SDL_GPU_TEXTUREFORMAT_D24_UNORM;

	// the renderer's own shaders
	plain_program.vertex_source = Z_StrDup(GPU_GLSL_PLAIN_VERTEX_SHADER);
	plain_program.fragment_source = Z_StrDup(GPU_GLSL_PLAIN_FRAGMENT_SHADER);
	fallback_program.vertex_source = Z_StrDup(GPU_GLSL_PLAIN_VERTEX_SHADER);
	fallback_program.fragment_source = Z_StrDup(GPU_GLSL_FALLBACK_FRAGMENT_SHADER);
	wipe_program.vertex_source = Z_StrDup(GPU_GLSL_PLAIN_VERTEX_SHADER);
	wipe_program.fragment_source = Z_StrDup(GPU_GLSL_WIPE_FRAGMENT_SHADER);

	if (!Program_Compile(&plain_program, "plain")
		|| !Program_Compile(&fallback_program, "fallback")
		|| !Program_Compile(&wipe_program, "wipe"))
	{
		SDLGPU_Shutdown();
		return false;
	}

	// what there always is to draw with
	defaults_buffer = CreateBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, sizeof (defaults), &defaults);

	fan_index_buffer = CreateBuffer(SDL_GPU_BUFFERUSAGE_INDEX, (MAX_FAN_VERTICES - 2) * 3 * sizeof (UINT16), NULL);
	fan = Staging_Begin((MAX_FAN_VERTICES - 2) * 3 * sizeof (UINT16), &transfer);
	for (i = 2; i < MAX_FAN_VERTICES; i++)
	{
		*fan++ = 0;
		*fan++ = (UINT16)(i - 1);
		*fan++ = (UINT16)i;
	}
	Staging_ToBuffer(transfer, fan_index_buffer, (MAX_FAN_VERTICES - 2) * 3 * sizeof (UINT16));

	white_texture.nofilter = true;
	Texture_SetPixels(&white_texture, white_pixel, 1, 1, 1);

	dummy_table_texture.nofilter = true;
	Texture_SetPixels(&dummy_table_texture, zeroes, 256, 32, 1);

	screen_palette_texture.nofilter = true;
	Texture_SetPixels(&screen_palette_texture, zeroes, 256, 1, 1);

	palette_lookup_texture.nofilter = true;
	palette_lookup_texture.texture = CreateTexture(SDL_GPU_TEXTURETYPE_3D, SDL_GPU_TEXTUREFORMAT_R8_UNORM,
		SDL_GPU_TEXTUREUSAGE_SAMPLER, HWR_PALETTE_LUT_SIZE, HWR_PALETTE_LUT_SIZE, HWR_PALETTE_LUT_SIZE, 1);
	{
		const Uint32 size = HWR_PALETTE_LUT_SIZE * HWR_PALETTE_LUT_SIZE * HWR_PALETTE_LUT_SIZE;
		memset(Staging_Begin(size, &transfer), 0, size);
		Staging_ToTexture(transfer, palette_lookup_texture.texture, HWR_PALETTE_LUT_SIZE, HWR_PALETTE_LUT_SIZE, HWR_PALETTE_LUT_SIZE);
	}

	Mat_Identity(uniforms.projection);
	Mat_Identity(uniforms.modelview);
	Mat_Scale(uniforms.modelview, 1.0f, 1.0f, -1.0f);

	return true;
}

SDL_GPUDevice *SDLGPU_GetDevice(void)
{
	return device;
}

void SDLGPU_SetResolution(INT32 width, INT32 height)
{
	if (!device)
		return;

	if (width != screen_width || height != screen_height || !color_target)
	{
		// The screen textures need to be flushed if the width or height change so that they be remade for the correct size
		Flush();
		SDLGPU_FlushScreenTextures();

		if (color_target)
			SDL_ReleaseGPUTexture(device, color_target);
		if (depth_target)
			SDL_ReleaseGPUTexture(device, depth_target);

		screen_width = width;
		screen_height = height;

		color_target = CreateTexture(SDL_GPU_TEXTURETYPE_2D, COLOR_FORMAT,
			SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
			(Uint32)width, (Uint32)height, 1, 1);
		depth_target = CreateTexture(SDL_GPU_TEXTURETYPE_2D, depth_format,
			SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
			(Uint32)width, (Uint32)height, 1, 1);

		// its contents are not defined yet
		clear_color.r = clear_color.g = clear_color.b = 0.0f;
		clear_color.a = 1.0f;
		clear_color_pending = clear_depth_pending = true;
	}

	viewport.x = viewport.y = 0.0f;
	viewport.w = (float)width;
	viewport.h = (float)height;
	viewport.min_depth = 0.0f;
	viewport.max_depth = 1.0f;
	if (render_pass)
		SDL_SetGPUViewport(render_pass, &viewport);

	// the state the OpenGL driver starts a video mode with
	Mat_Identity(uniforms.projection);
	Mat_Identity(uniforms.modelview);
	Mat_Scale(uniforms.modelview, 1.0f, 1.0f, -1.0f);
	CurrentPolyFlags = 0;
	current_texture = &white_texture;
}

// the window whose swapchain was last set up, and how
static SDL_Window *swapchain_window = NULL;
static boolean swapchain_vsync = false;

void SDLGPU_SetPreferLowPower(boolean lowpower)
{
	prefer_lowpower = lowpower;
}

void SDLGPU_WindowClaimed(SDL_Window *window)
{
	(void)window;
	swapchain_window = NULL;
}

// Whether the window has something to be shown in.
// Android takes the window's surface away while the app is in the background.
// SDL 3.4's Vulkan backend can't make a new one then, and if asked for a swapchain
// texture again it frees the old surface twice, which crashes.
static boolean WindowHasSurface(SDL_Window *window)
{
#ifdef __ANDROID__
	return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL) != NULL;
#else
	(void)window;
	return true;
#endif
}

void SDLGPU_Present(SDL_Window *window, boolean vsync)
{
	SDL_GPUTexture *swapchain = NULL;
	Uint32 width = 0, height = 0;

	if (!device || !color_target || !window)
		return;

	// The frame is still submitted, but not shown.
	if (!WindowHasSurface(window))
	{
		EndRenderPass();
		Flush();
		return;
	}

	if (window != swapchain_window || vsync != swapchain_vsync)
	{
		SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;

		if (!vsync)
		{
			if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_IMMEDIATE))
				mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
			else if (SDL_WindowSupportsGPUPresentMode(device, window, SDL_GPU_PRESENTMODE_MAILBOX))
				mode = SDL_GPU_PRESENTMODE_MAILBOX;
		}

		if (!SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
			GPU_Error("couldn't set the swapchain up");

		swapchain_window = window;
		swapchain_vsync = vsync;
	}

	EndRenderPass();

	if (!SDL_WaitAndAcquireGPUSwapchainTexture(RenderCommands(), window, &swapchain, &width, &height))
		GPU_Error("couldn't get the window's texture");

	// there is none when the window is minimized
	if (swapchain && width && height)
	{
		SDL_GPUBlitInfo blit;
		float scale = min((float)width / screen_width, (float)height / screen_height);
		Uint32 w = (Uint32)(screen_width * scale + 0.5f);
		Uint32 h = (Uint32)(screen_height * scale + 0.5f);

		w = min(w, width);
		h = min(h, height);

		SDL_zero(blit);
		blit.source.texture = color_target;
		blit.source.w = (Uint32)screen_width;
		blit.source.h = (Uint32)screen_height;
		blit.destination.texture = swapchain;
		// black bars around the picture if the window isn't of the same shape
		blit.destination.x = (width - w) / 2;
		blit.destination.y = (height - h) / 2;
		blit.destination.w = w;
		blit.destination.h = h;
		blit.load_op = SDL_GPU_LOADOP_CLEAR;
		blit.clear_color.a = 1.0f;
		// only blurry if the pixels can't all be of the same size
		blit.filter = (w % (Uint32)screen_width || h % (Uint32)screen_height) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
		SDL_BlitGPUTexture(render_cmd, &blit);
	}

	Flush();
}

void SDLGPU_Shutdown(void)
{
	size_t i;

	if (!device)
		return;

	Flush();
	SDL_WaitForGPUIdle(device);

	FlushTextures();
	SDLGPU_FlushScreenTextures();
	for (i = 0; i < numgputextures; i++)
		if (gputextures[i])
			Texture_FreeId((UINT32)i + 1);
	Texture_Release(&white_texture);
	Texture_Release(&dummy_table_texture);
	Texture_Release(&screen_palette_texture);
	Texture_Release(&palette_lookup_texture);
	memset(screen_palette, 0, sizeof (screen_palette));

	for (i = 0; i < nummeshes; i++)
		Mesh_Free((unsigned int)i + 1);
	sky_mesh = 0;

	for (i = 0; i < HWR_MAXSHADERS; i++)
		Program_Unload(&programs[i]);
	Program_Unload(&plain_program);
	Program_Unload(&fallback_program);
	Program_Unload(&wipe_program);

	for (i = 0; i < numpipelines; i++)
		SDL_ReleaseGPUGraphicsPipeline(device, pipelines[i].pipeline);
	numpipelines = 0;

	for (i = 0; i < NUMSAMPLERS; i++)
		if (samplers[i])
		{
			SDL_ReleaseGPUSampler(device, samplers[i]);
			samplers[i] = NULL;
		}

	Stream_Free(&vertex_stream);
	Stream_Free(&index_stream);

	if (defaults_buffer)
		SDL_ReleaseGPUBuffer(device, defaults_buffer);
	if (fan_index_buffer)
		SDL_ReleaseGPUBuffer(device, fan_index_buffer);
	if (color_target)
		SDL_ReleaseGPUTexture(device, color_target);
	if (depth_target)
		SDL_ReleaseGPUTexture(device, depth_target);
	defaults_buffer = fan_index_buffer = NULL;
	color_target = depth_target = NULL;
	screen_width = screen_height = 0;

	SDL_DestroyGPUDevice(device);
	device = NULL;
}

void HWD_FillSDLGPU(struct hwdriver_s *driver)
{
	driver->pfnInit                 = SDLGPU_Init;
	driver->pfnSetTexturePalette    = SDLGPU_SetTexturePalette;
	driver->pfnFinishUpdate         = SDLGPU_FinishUpdate;
	driver->pfnDraw2DLine           = SDLGPU_Draw2DLine;
	driver->pfnDrawPolygon          = SDLGPU_DrawPolygon;
	driver->pfnDrawIndexedTriangles = SDLGPU_DrawIndexedTriangles;
	driver->pfnRenderSkyDome        = SDLGPU_RenderSkyDome;
	driver->pfnSetBlend             = SDLGPU_SetBlend;
	driver->pfnClearBuffer          = SDLGPU_ClearBuffer;
	driver->pfnSetTexture           = SDLGPU_SetTexture;
	driver->pfnUpdateTexture        = SDLGPU_UpdateTexture;
	driver->pfnReadScreenTexture    = SDLGPU_ReadScreenTexture;
	driver->pfnGClipRect            = SDLGPU_GClipRect;
	driver->pfnClearMipMapCache     = SDLGPU_ClearMipMapCache;
	driver->pfnSetSpecialState      = SDLGPU_SetSpecialState;
	driver->pfnDrawModel            = SDLGPU_DrawModel;
	driver->pfnCreateModelVBOs      = SDLGPU_CreateModelVBOs;
	driver->pfnSetTransform         = SDLGPU_SetTransform;
	driver->pfnGetTextureUsed       = SDLGPU_GetTextureUsed;
	driver->pfnPostImgRedraw        = SDLGPU_PostImgRedraw;
	driver->pfnFlushScreenTextures  = SDLGPU_FlushScreenTextures;
	driver->pfnDoScreenWipe         = SDLGPU_DoScreenWipe;
	driver->pfnDrawScreenTexture    = SDLGPU_DrawScreenTexture;
	driver->pfnMakeScreenTexture    = SDLGPU_MakeScreenTexture;
	driver->pfnDrawScreenFinalTexture = SDLGPU_DrawScreenFinalTexture;
	driver->pfnInitShaders          = SDLGPU_InitShaders;
	driver->pfnLoadShader           = SDLGPU_LoadShader;
	driver->pfnLoadShaderBinary     = SDLGPU_LoadShaderBinary;
	driver->pfnCompileShader        = SDLGPU_CompileShader;
	driver->pfnSetShader            = SDLGPU_SetShader;
	driver->pfnUnSetShader          = SDLGPU_UnSetShader;
	driver->pfnSetShaderInfo        = SDLGPU_SetShaderInfo;
	driver->pfnSetPaletteLookup     = SDLGPU_SetPaletteLookup;
	driver->pfnCreateLightTable     = SDLGPU_CreateLightTable;
	driver->pfnUpdateLightTable     = SDLGPU_UpdateLightTable;
	driver->pfnClearLightTables     = SDLGPU_ClearLightTables;
	driver->pfnSetScreenPalette     = SDLGPU_SetScreenPalette;
}

#endif // HWRENDER_SDLGPU
