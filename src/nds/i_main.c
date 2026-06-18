// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
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
/// \brief Main program, simply calls D_SRB2Main and D_SRB2Loop, the high level loop.

#include "../doomdef.h"
#include "../d_main.h"
#include "../m_argv.h"

#include <nds.h>
#include <filesystem.h>

int main(int argc, char **argv)
{
	// wait for a few frames so we can get NDS firmware data
	swiWaitForVBlank();
    swiWaitForVBlank();

	myargc = argc;
	myargv = argv; /// \todo pull out path to exe from this string

	defaultExceptionHandler(); // debug
	
	TIMER0_DATA=0;	// Set up the timer
	TIMER1_DATA=0;//
	TIMER0_CR=TIMER_DIV_1024 | TIMER_ENABLE;
	TIMER1_CR=TIMER_CASCADE | TIMER_ENABLE;

    consoleDemoInit(); // init console
	keyboardDemoInit(); // init keyboard

	// start NitroFS
	if (!nitroFSInit(NULL))
		I_Error("Failed to initialize NitroFS!\n");

	chdir("nitro:/");

	// startup SRB2
	CONS_Printf("Setting up SRB2...\n");
	D_SRB2Main();
	CONS_Printf("Entering main game loop...\n");
	// never return
	D_SRB2Loop();

	// return to OS
#ifndef __GNUC__
	return 0;
#endif
}
