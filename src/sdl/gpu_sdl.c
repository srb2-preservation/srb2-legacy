// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  gpu_sdl.c
/// \brief SDL specific part of the SDL GPU renderer
///
/// Owns the SDL_GPUDevice, and the link between it and the game window.
/// What gets drawn is up to the driver, src/hardware/r_sdlgpu.

#ifdef HAVE_SDL
#include <SDL3/SDL.h>

#include "sdlmain.h"
#include "../doomdef.h"
#include "../console.h"

#ifdef HWRENDER_SDLGPU
#include "../hardware/hw_main.h"
#include "gpu_sdl.h"

SDL_GPUDevice *sdlgpudevice = NULL;
static boolean windowclaimed = false;

boolean GPUSdlCreateDevice(void)
{
	if (sdlgpudevice)
		return true;

	// The game's shaders will have to be provided in every format wanted here.
	sdlgpudevice = SDL_CreateGPUDevice(
		SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
		false, NULL);

	if (!sdlgpudevice)
	{
		CONS_Alert(CONS_ERROR, "Could not create a GPU device: %s\n", SDL_GetError());
		return false;
	}

	CONS_Printf("SDL GPU driver: %s\n", SDL_GetGPUDeviceDriver(sdlgpudevice));
	return true;
}

void GPUSdlWindowFlags(SDL_WindowFlags *flags)
{
	// SDL GPU needs no special window flags, but the window can't have a GL context or a renderer.
	(void)flags;
}

boolean GPUSdlCreateContext(SDL_Window *win)
{
	if (!sdlgpudevice)
		return false;

	if (!windowclaimed)
	{
		if (!SDL_ClaimWindowForGPUDevice(sdlgpudevice, win))
			return false;
		windowclaimed = true;
	}
	return true;
}

void GPUSdlReleaseWindow(SDL_Window *win)
{
	if (windowclaimed && sdlgpudevice)
		SDL_ReleaseWindowFromGPUDevice(sdlgpudevice, win);
	windowclaimed = false;
}

void GPUSdlDestroyContext(void)
{
	if (sdlgpudevice)
	{
		SDL_DestroyGPUDevice(sdlgpudevice);
		sdlgpudevice = NULL;
	}
	windowclaimed = false;
}

void GPUSdlSetMode(INT32 w, INT32 h)
{
	(void)w;
	(void)h;

	// Set up the swapchain for the window here (SDL_SetGPUSwapchainParameters),
	// and anything that depends on the size of the window.

	// This makes the game create what it needs from the driver.
	HWR_Startup();
}

void GPUSdlFinishUpdate(boolean vidwait)
{
	(void)vidwait;

	// Present the frame here. See OglSdlFinishUpdate for what the OpenGL
	// backend does around presenting: the final screen texture gets drawn,
	// the clip rect is reset, and the screen is drawn again for effects
	// that want to take the old screen.
}
#endif // HWRENDER_SDLGPU
#endif // HAVE_SDL
