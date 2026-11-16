// SRB2 LEGACY
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by SRB2 Preservation.
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

#endif
