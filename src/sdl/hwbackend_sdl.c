// SRB2 LEGACY
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by SRB2 Preservation.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  hwbackend_sdl.c
/// \brief The list of hardware renderer backends

#include "hwbackend_sdl.h"

#ifdef HWRENDER

#ifdef HWRENDER_OPENGL
#include "ogl_sdl.h"
#include "hwsym_sdl.h"
#endif

#ifdef HWRENDER_SDLGPU
#include "gpu_sdl.h"
#endif

#ifdef HWRENDER_OPENGL
/**	\brief	Fills a hardware driver with the OpenGL one

	\param	driver	the driver to fill
*/
// The OpenGL driver's functions are looked up by name, see hwSym.
static void HWD_FillOpenGL(struct hwdriver_s *driver)
{
	driver->pfnInit             = hwSym("Init",NULL);
	driver->pfnFinishUpdate     = NULL;
	driver->pfnDraw2DLine       = hwSym("Draw2DLine",NULL);
	driver->pfnDrawPolygon      = hwSym("DrawPolygon",NULL);
	driver->pfnDrawIndexedTriangles = hwSym("DrawIndexedTriangles",NULL);
	driver->pfnRenderSkyDome    = hwSym("RenderSkyDome",NULL);
	driver->pfnSetBlend         = hwSym("SetBlend",NULL);
	driver->pfnClearBuffer      = hwSym("ClearBuffer",NULL);
	driver->pfnSetTexture       = hwSym("SetTexture",NULL);
	driver->pfnUpdateTexture    = hwSym("UpdateTexture",NULL);
	driver->pfnReadScreenTexture= hwSym("ReadScreenTexture",NULL);
	driver->pfnGClipRect        = hwSym("GClipRect",NULL);
	driver->pfnClearMipMapCache = hwSym("ClearMipMapCache",NULL);
	driver->pfnSetSpecialState  = hwSym("SetSpecialState",NULL);
	driver->pfnSetTexturePalette= hwSym("SetTexturePalette",NULL);
	driver->pfnGetTextureUsed   = hwSym("GetTextureUsed",NULL);
	driver->pfnDrawModel        = hwSym("DrawModel",NULL);
	driver->pfnCreateModelVBOs  = hwSym("CreateModelVBOs",NULL);
	driver->pfnSetTransform     = hwSym("SetTransform",NULL);
	driver->pfnPostImgRedraw    = hwSym("PostImgRedraw",NULL);
	driver->pfnFlushScreenTextures=hwSym("FlushScreenTextures",NULL);
	driver->pfnDoScreenWipe     = hwSym("DoScreenWipe",NULL);
	driver->pfnDrawScreenTexture= hwSym("DrawScreenTexture",NULL);
	driver->pfnMakeScreenTexture= hwSym("MakeScreenTexture",NULL);
	driver->pfnDrawScreenFinalTexture=hwSym("DrawScreenFinalTexture",NULL);
	driver->pfnInitShaders      = hwSym("InitShaders",NULL);
	driver->pfnLoadShader       = hwSym("LoadShader",NULL);
	driver->pfnLoadShaderBinary = hwSym("LoadShaderBinary",NULL);
	driver->pfnCompileShader    = hwSym("CompileShader",NULL);
	driver->pfnSetShader        = hwSym("SetShader",NULL);
	driver->pfnUnSetShader      = hwSym("UnSetShader",NULL);
	driver->pfnSetShaderInfo    = hwSym("SetShaderInfo",NULL);
	driver->pfnSetPaletteLookup = hwSym("SetPaletteLookup",NULL);
	driver->pfnCreateLightTable = hwSym("CreateLightTable",NULL);
	driver->pfnUpdateLightTable = hwSym("UpdateLightTable",NULL);
	driver->pfnClearLightTables = hwSym("ClearLightTables",NULL);
	driver->pfnSetScreenPalette = hwSym("SetScreenPalette",NULL);
}
#endif

// Builds without OpenGL (Android and iOS) only have SDL GPU.
static sdlhwbackend_t sdlhw_backends[] =
{
#ifdef HWRENDER_OPENGL
	{
		"OpenGL", render_opengl, VID_GL_LIBRARY_NOTLOADED,
		HWD_FillOpenGL,
		OglSdlWindowFlags,
		OglSdlCreateContext,
		NULL, // the GL context is not tied to the window
		OglSdlDestroyContext,
		OglSdlSetMode,
		OglSdlFinishUpdate,
		false, // the GL context survives window changes
	},
#endif
#ifdef HWRENDER_SDLGPU
	{
		"SDL GPU", render_sdlgpu, VID_GL_LIBRARY_NOTLOADED,
		HWD_FillSDLGPU,
		GPUSdlWindowFlags,
		GPUSdlCreateContext,
		GPUSdlReleaseWindow,
		GPUSdlDestroyContext,
		GPUSdlSetMode,
		GPUSdlFinishUpdate,
		true, // a GPU device claims its window, and can only release it by destroying the window
	},
#endif
};

sdlhwbackend_t *SDLHW_GetBackend(rendermode_t mode)
{
	size_t i;
	for (i = 0; i < sizeof (sdlhw_backends) / sizeof (*sdlhw_backends); i++)
		if (sdlhw_backends[i].mode == mode)
			return &sdlhw_backends[i];
	return NULL;
}

void SDLHW_ReleaseWindow(SDL_Window *window)
{
	size_t i;
	for (i = 0; i < sizeof (sdlhw_backends) / sizeof (*sdlhw_backends); i++)
		if (sdlhw_backends[i].ReleaseWindow)
			sdlhw_backends[i].ReleaseWindow(window);
}

void SDLHW_DestroyContexts(void)
{
	size_t i;
	for (i = 0; i < sizeof (sdlhw_backends) / sizeof (*sdlhw_backends); i++)
		if (sdlhw_backends[i].DestroyContext)
			sdlhw_backends[i].DestroyContext();
}

#endif // HWRENDER
