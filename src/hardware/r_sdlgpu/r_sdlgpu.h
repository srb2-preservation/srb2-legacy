// SRB2 LEGACY
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by SRB2 Preservation.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_sdlgpu.h
/// \brief SDL GPU hardware driver

#ifndef __R_SDLGPU_H__
#define __R_SDLGPU_H__

#include <SDL3/SDL.h>

#include "../hw_drv.h"

/**	\brief Fills a hardware driver with the SDL GPU one
*/
void HWD_FillSDLGPU(struct hwdriver_s *driver);

// What the SDL side of the renderer (src/sdl/gpu_sdl.c) needs from the driver.
// The driver draws the game to a texture of its own; the window only gets
// involved when a frame is presented.

/**	\brief The GPU device, once the driver's Init succeeded
*/
SDL_GPUDevice *SDLGPU_GetDevice(void);

/**	\brief Sets the resolution the game is drawn at
*/
void SDLGPU_SetResolution(INT32 width, INT32 height);

/**	\brief Shows what was drawn in a window claimed for the GPU device
*/
void SDLGPU_Present(SDL_Window *window, boolean vsync);

/**	\brief Tells the driver a window was just claimed for the GPU device

	Its swapchain then has SDL's default settings (with vertical sync),
	so they have to be set again on the next SDLGPU_Present.
*/
void SDLGPU_WindowClaimed(SDL_Window *window);

/**	\brief Releases everything, and the GPU device
*/
void SDLGPU_Shutdown(void);

#endif
