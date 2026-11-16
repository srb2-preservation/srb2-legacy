// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  hwbackend_sdl.h
/// \brief SDL side of the hardware renderer backends

#ifndef __HWBACKEND_SDL_H__
#define __HWBACKEND_SDL_H__

#include <SDL3/SDL.h>

#include "../doomtype.h"
#include "../i_video.h"

#ifdef HWRENDER
#include "../hardware/hw_drv.h"

/**	\brief A hardware renderer backend.

	The game talks to a backend through two interfaces:
	 - the hardware driver (struct hwdriver_s, "HWD"), which draws things,
	 - this table, which owns everything that has to do with SDL: the window
	   flags, the graphics context, the surface setup and presenting a frame.

	To add a backend:
	 - give it a render mode in rendermode_t (see RENDERMODE_IS_HARDWARE),
	 - write its driver, and a function that fills a struct hwdriver_s with it,
	 - add an entry for it in hwbackend_sdl.c,
	 - add its value to cv_renderer in screen.c.
	Nothing else in the game should need to know which backend is running.
*/
typedef struct sdlhwbackend_s
{
	/// Name shown to the player.
	const char *name;

	/// The render mode that uses this backend.
	rendermode_t mode;

	/// VID_GL_LIBRARY_NOTLOADED, VID_GL_LIBRARY_LOADED or VID_GL_LIBRARY_ERROR.
	/// Owned by the generic code, set by VID_StartupHardware.
	INT32 state;

	/// Fills the driver with this backend's functions.
	/// The driver's pfnInit is called right after; the backend only becomes usable when it succeeds.
	void (*FillDriver)(struct hwdriver_s *driver);

	/// Called before the window is created: add the window flags this backend needs
	/// and set any attributes that must be set before creation.
	void (*WindowFlags)(SDL_WindowFlags *flags);

	/// Called once the window exists: creates (or reuses) the graphics context.
	/// Return false on failure.
	boolean (*CreateContext)(SDL_Window *window);

	/// Lets go of the window, right before it is destroyed. May be NULL.
	void (*ReleaseWindow)(SDL_Window *window);

	/// Releases the graphics context, at shutdown.
	void (*DestroyContext)(void);

	/// Called whenever the video mode is set, once the window has its final size.
	void (*SetMode)(INT32 width, INT32 height);

	/// Presents the frame.
	void (*FinishUpdate)(boolean vidwait);

	/// Whether the window must be destroyed and made again when switching
	/// to or from this backend. Some contexts can't be moved between windows.
	boolean needsnewwindow;
} sdlhwbackend_t;

/**	\brief Gets the backend for a render mode.

	\return NULL if the render mode has no backend, or it was not compiled in.
*/
sdlhwbackend_t *SDLHW_GetBackend(rendermode_t mode);

/**	\brief Lets every backend go of a window that is about to be destroyed.
*/
void SDLHW_ReleaseWindow(SDL_Window *window);

/**	\brief Releases every backend's graphics context.
*/
void SDLHW_DestroyContexts(void);

/**	\brief Gets the backend of the current render mode.
*/
#define SDLHW_Current() SDLHW_GetBackend(rendermode)

#endif // HWRENDER

#endif
