// Emacs style mode select   -*- C++ -*-
// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
// Portions Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2014-2018 by Sonic Team Junior.
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
/// \brief SRB2 graphics stuff for NDS

#include "../doomdef.h"
#include "../doomstat.h"
#include "../d_netcmd.h"
#include "../command.h"
#include "../screen.h"
#include "../v_video.h"
#include "../i_system.h"
#include "../i_video.h"

#include <nds.h>

rendermode_t rendermode = render_soft;
boolean highcolor = false;
boolean allow_fullscreen = false;
consvar_t cv_vidwait = CVAR_INIT ("vid_wait", "On", "Does nothing on this build", CV_SAVE, CV_OnOff, NULL);

UINT16 ds_palette[256];

void I_StartupGraphics(void)
{
    CV_RegisterVar (&cv_vidwait);
	VID_SetMode(1);
    graphics_started = true;
}

void I_ShutdownGraphics(void){}

void VID_StartupOpenGL(void){}

void I_SetPalette(RGBA_t *palette)
{
    for (int i = 0; i < 256; i++)
    {
        UINT8 r = palette[i].s.red>>3;
        UINT8 g = palette[i].s.green>>3;
        UINT8 b = palette[i].s.blue>>3;

        ds_palette[i] = RGB15(r, g, b);
    }
}

INT32 VID_NumModes(void)
{
	return 1;
}

INT32 VID_GetModeForSize(INT32 w, INT32 h)
{
	(void)w;
	(void)h;
	return 0;
}

void VID_PrepareModeList(void) { allow_fullscreen = true; }

INT32 VID_SetMode(INT32 modenum)
{
	vid.width = 256;
	vid.height = 192;
	vid.bpp = 1;
	vid.rowbytes = vid.width * vid.bpp;
	vid.recalc = true;

	if (vid.buffer) free(vid.buffer);
	vid.buffer = NULL;
	
	vid.modenum = 0; 
    vid.buffer = malloc(vid.width * vid.height);

    if (!vid.buffer)
        I_Error("Couldn't allocate video buffer");

    memset(vid.buffer, 0, vid.width*vid.height);
	
    videoSetMode(MODE_VRAM_A);
    vramSetBankA(VRAM_A_LCD);

	return 1;
}

void VID_CheckRenderer(void) {}
void VID_CheckGLLoaded(rendermode_t oldrender) {}

UINT32 I_GetRefreshRate(void) { return 35; }

const char *VID_GetModeName(INT32 modenum)
{
	(void)modenum;
	return "256x192";
}

void I_UpdateNoBlit(void){}

void I_FinishUpdate(void)
{
    UINT16* framebuffer = (UINT16*)VRAM_A;

	SCR_CalculateFPS();
	SCR_DisplayTicRate();

	if (cv_showping.value && netgame && consoleplayer != serverplayer)
		SCR_DisplayLocalPing();

	if (marathonmode)
		SCR_DisplayMarathonInfo();

    for (int i = 0; i < 256 * 192; i++)
        framebuffer[i] = ds_palette[screens[0][i]];
}

void I_UpdateNoVsync(void) {}

void I_WaitVBL(INT32 count)
{
	(void)count;
}

void I_ReadScreen(UINT8 *scr)
{
	VID_BlitLinearScreen(screens[0], scr, vid.width*vid.bpp, vid.height, vid.rowbytes, vid.rowbytes);
}

void I_BeginRead(void){}

void I_EndRead(void){}

