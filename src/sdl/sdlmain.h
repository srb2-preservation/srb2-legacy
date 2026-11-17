// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 2006-2018 by Sonic Team Jr.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//-----------------------------------------------------------------------------
/// \file
/// \brief System specific interface stuff.

#ifndef __sdlmain__
#define __sdlmain__

#include <signal.h>

extern bool consolevent;
extern bool framebuffer;
extern volatile sig_atomic_t quitsignal;

#include "../m_fixed.h"
#include "../i_gamepad.h"

// SDL stub macro
#define SDLSTUB() CONS_Printf("SDL: stubbed: %s:%d\n", __func__, __LINE__)

// SDL info about all controllers
typedef struct
{
	boolean started; // started
	int lastindex; // last gamepad ID

	SDL_Gamepad *dev;
	SDL_Joystick *joydev;

	gamepad_t *info; // pointer to gamepad info

	struct {
		Uint16 large_magnitude;
		Uint16 small_magnitude;
		Uint64 expiration, time_left;
	} rumble;
} ControllerInfo;

#define GAMEPAD_INIT_FLAGS (SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)

void I_UpdateControllers(void);

void I_ControllerDeviceAdded(SDL_JoystickID which);
void I_ControllerDeviceRemoved(void);

void I_HandleControllerButtonEvent(SDL_GamepadButtonEvent evt, Uint32 type);
void I_HandleControllerAxisEvent(SDL_GamepadAxisEvent evt);

INT32 I_GetControllerIndex(SDL_Gamepad *dev);

void I_ToggleControllerRumble(boolean unpause);

void I_GetConsoleEvents(void);

void SDLforceUngrabMouse(void);

// Needed for some WIN32 functions
extern SDL_Window *window;

#endif
