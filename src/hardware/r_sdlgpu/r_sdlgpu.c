// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_sdlgpu.c
/// \brief SDL GPU hardware driver
///
/// This is the skeleton of the SDL GPU renderer: every function the game calls
/// through the hardware driver (see hw_drv.h) is here, and does nothing.
/// Init fails, so selecting the renderer falls back to the previous one.
/// Fill the functions in, then make Init succeed.
///
/// The driver doesn't deal with SDL windows: that's src/sdl/gpu_sdl.c.
/// Names are prefixed so that they don't clash with the OpenGL driver's,
/// which lives in the same executable.

#include "../../doomdef.h"

#ifdef HWRENDER_SDLGPU

#include "r_sdlgpu.h"
#include "../../console.h"
#include "../hw_main.h"

// Flip this once the driver can draw.
#define SDLGPU_IMPLEMENTED 0

static boolean SDLGPU_Init(void)
{
#if SDLGPU_IMPLEMENTED
	// Create the GPU device here (GPUSdlCreateDevice in src/sdl/gpu_sdl.c),
	// then create pipelines, buffers and so on.
	return true;
#else
	CONS_Alert(CONS_ERROR, "The SDL GPU renderer is not implemented yet.\n");
	return false;
#endif
}

static void SDLGPU_SetTexturePalette(RGBA_t *palette) { (void)palette; }
static void SDLGPU_FinishUpdate(INT32 waitvbl) { (void)waitvbl; }
static void SDLGPU_Draw2DLine(F2DCoord *v1, F2DCoord *v2, RGBA_t Color) { (void)v1; (void)v2; (void)Color; }
static void SDLGPU_DrawPolygon(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags) { (void)pSurf; (void)pOutVerts; (void)iNumPts; (void)PolyFlags; }
static void SDLGPU_DrawIndexedTriangles(FSurfaceInfo *pSurf, FOutVector *pOutVerts, FUINT iNumPts, FBITFIELD PolyFlags, unsigned int *IndexArray) { (void)pSurf; (void)pOutVerts; (void)iNumPts; (void)PolyFlags; (void)IndexArray; }
static void SDLGPU_RenderSkyDome(gl_sky_t *sky) { (void)sky; }
static void SDLGPU_SetBlend(FBITFIELD PolyFlags) { (void)PolyFlags; }
static void SDLGPU_ClearBuffer(FBOOLEAN ColorMask, FBOOLEAN DepthMask, FRGBAFloat *ClearColor) { (void)ColorMask; (void)DepthMask; (void)ClearColor; }
static void SDLGPU_SetTexture(FTextureInfo *TexInfo) { (void)TexInfo; }
static void SDLGPU_UpdateTexture(FTextureInfo *TexInfo) { (void)TexInfo; }
static void SDLGPU_ReadScreenTexture(int tex, UINT8 *dst_data) { (void)tex; (void)dst_data; }
static void SDLGPU_GClipRect(INT32 minx, INT32 miny, INT32 maxx, INT32 maxy, float nearclip) { (void)minx; (void)miny; (void)maxx; (void)maxy; (void)nearclip; }
static void SDLGPU_ClearMipMapCache(void) {}
static void SDLGPU_SetSpecialState(hwdspecialstate_t IdState, INT32 Value) { (void)IdState; (void)Value; }
static void SDLGPU_DrawModel(model_t *model, INT32 frameIndex, float duration, float tics, INT32 nextFrameIndex, FTransform *pos, float scale, UINT8 flipped, FSurfaceInfo *Surface) { (void)model; (void)frameIndex; (void)duration; (void)tics; (void)nextFrameIndex; (void)pos; (void)scale; (void)flipped; (void)Surface; }
static void SDLGPU_CreateModelVBOs(model_t *model) { (void)model; }
static void SDLGPU_SetTransform(FTransform *ptransform) { (void)ptransform; }
static INT32 SDLGPU_GetTextureUsed(void) { return 0; }
static void SDLGPU_PostImgRedraw(float points[SCREENVERTS][SCREENVERTS][2]) { (void)points; }
static void SDLGPU_FlushScreenTextures(void) {}
static void SDLGPU_DoScreenWipe(int wipeStart, int wipeEnd) { (void)wipeStart; (void)wipeEnd; }
static void SDLGPU_DrawScreenTexture(int tex, FSurfaceInfo *surf, FBITFIELD polyflags) { (void)tex; (void)surf; (void)polyflags; }
static void SDLGPU_MakeScreenTexture(int tex) { (void)tex; }
static void SDLGPU_DrawScreenFinalTexture(int tex, int width, int height) { (void)tex; (void)width; (void)height; }
static boolean SDLGPU_InitShaders(void) { return false; } // no shaders available
static void SDLGPU_LoadShader(int slot, char *code, hwdshaderstage_t stage) { (void)slot; (void)code; (void)stage; }
static boolean SDLGPU_CompileShader(int slot) { (void)slot; return false; }
static void SDLGPU_SetShader(int slot) { (void)slot; }
static void SDLGPU_UnSetShader(void) {}
static void SDLGPU_SetShaderInfo(hwdshaderinfo_t info, INT32 value) { (void)info; (void)value; }
static void SDLGPU_SetPaletteLookup(UINT8 *lut) { (void)lut; }
static UINT32 SDLGPU_CreateLightTable(RGBA_t *hw_lighttable) { (void)hw_lighttable; return 0; }
static void SDLGPU_UpdateLightTable(UINT32 id, RGBA_t *hw_lighttable) { (void)id; (void)hw_lighttable; }
static void SDLGPU_ClearLightTables(void) {}
static void SDLGPU_SetScreenPalette(RGBA_t *palette) { (void)palette; }

void HWD_FillSDLGPU(struct hwdriver_s *driver)
{
	driver->pfnInit                 = SDLGPU_Init;
	driver->pfnSetTexturePalette    = SDLGPU_SetTexturePalette;
	driver->pfnFinishUpdate         = NULL; // unused, see the backend's FinishUpdate
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
