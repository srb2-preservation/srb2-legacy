// SRB2 LEGACY
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by SRB2 Preservation.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  gpu_sdl.c
/// \brief SDL specific part of the SDL GPU renderer
///
/// The link between the game window and the SDL GPU driver
/// (src/hardware/r_sdlgpu), which owns the GPU device.

#ifdef HAVE_SDL
#include <SDL3/SDL.h>

#include "sdlmain.h"
#include "../doomdef.h"
#include "../console.h"

#ifdef HWRENDER_SDLGPU
#include "../hardware/hw_main.h"
#include "gpu_sdl.h"

// the window the GPU device draws to
static SDL_Window *claimed_window = NULL;

void GPUSdlWindowFlags(SDL_WindowFlags *flags)
{
	// SDL GPU needs no special window flags, but the window can't have a GL context or a renderer.
	(void)flags;
}

boolean GPUSdlCreateContext(SDL_Window *win)
{
	SDL_GPUDevice *device = SDLGPU_GetDevice();

	if (!device)
		return false;

	if (claimed_window == win)
		return true;

	if (claimed_window)
		SDL_ReleaseWindowFromGPUDevice(device, claimed_window);
	claimed_window = NULL;

	if (!SDL_ClaimWindowForGPUDevice(device, win))
		return false;

	claimed_window = win;
	return true;
}

void GPUSdlReleaseWindow(SDL_Window *win)
{
	SDL_GPUDevice *device = SDLGPU_GetDevice();

	if (device && claimed_window == win)
	{
		// the frames on their way to the window have to get there first
		SDL_WaitForGPUIdle(device);
		SDL_ReleaseWindowFromGPUDevice(device, win);
		claimed_window = NULL;
	}
}

void GPUSdlDestroyContext(void)
{
	SDL_GPUDevice *device = SDLGPU_GetDevice();

	if (device && claimed_window)
	{
		SDL_WaitForGPUIdle(device);
		SDL_ReleaseWindowFromGPUDevice(device, claimed_window);
	}
	claimed_window = NULL;

	SDLGPU_Shutdown();
}

void GPUSdlSetMode(INT32 w, INT32 h)
{
	// The game is not drawn to the window, but to a picture of the game's
	// resolution that is then shown in the window, whatever its size.
	SDLGPU_SetResolution(w, h);

	// This makes the game create what it needs from the driver.
	HWR_Startup();
}

void GPUSdlFinishUpdate(boolean vidwait)
{
	// Screenshots and movies read this copy of the screen.
	HWR_MakeScreenFinalTexture();

	SDLGPU_Present(claimed_window, vidwait);

	HWD.pfnGClipRect(0, 0, vid.width, vid.height, NZCLIP_PLANE);

	// With palette rendering the screen was just replaced by its paletted version.
	// Put back what it was before, so that it doesn't get done twice to what the game doesn't draw again.
	if (HWR_ShouldUsePaletteRendering())
		HWD.pfnDrawScreenTexture(HWD_SCREENTEXTURE_GENERIC2, NULL, 0);
}
#endif // HWRENDER_SDLGPU
#endif // HAVE_SDL
