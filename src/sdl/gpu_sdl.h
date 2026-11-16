// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  gpu_sdl.h
/// \brief SDL specific part of the SDL GPU renderer

#ifndef __GPU_SDL_H__
#define __GPU_SDL_H__

#include "../hardware/r_sdlgpu/r_sdlgpu.h"

// The SDL GPU hooks for the SDL hardware backend table, see hwbackend_sdl.h
void GPUSdlWindowFlags(SDL_WindowFlags *flags);
boolean GPUSdlCreateContext(SDL_Window *win);
void GPUSdlReleaseWindow(SDL_Window *win);
void GPUSdlDestroyContext(void);
void GPUSdlSetMode(INT32 w, INT32 h);
void GPUSdlFinishUpdate(boolean vidwait);

/// The GPU device, created by the driver's Init through GPUSdlCreateDevice
extern SDL_GPUDevice *sdlgpudevice;

boolean GPUSdlCreateDevice(void);

#endif
