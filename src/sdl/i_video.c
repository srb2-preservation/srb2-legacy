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
/// \brief SRB2 graphics stuff for SDL

#include <stdlib.h>

#include <signal.h>

#ifdef HAVE_SDL
#define _MATH_DEFINES_DEFINED
#include <SDL3/SDL.h>

#ifdef HAVE_TTF
#include "i_ttf.h"
#endif

#ifdef HAVE_IMAGE
#include <SDL3_image/SDL_image.h>
#elif (!defined(__APPLE__))
#define LOAD_XPM //I want XPM!
#include "IMG_xpm.c" //Alam: I don't want to add SDL_Image.dll/so
#define HAVE_IMAGE //I have SDL_Image, sortof
#endif

#ifdef HAVE_IMAGE
#include "SDL_icon.xpm"
#endif

#include "../doomdef.h"

#include "../doomstat.h"
#include "../i_system.h"
#include "../v_video.h"
#include "../m_argv.h"
#include "../m_menu.h"
#include "../d_main.h"
#include "../s_sound.h"
#include "../i_gamepad.h"
#include "../st_stuff.h"
#include "../hu_stuff.h"
#include "../g_game.h"
#include "../i_video.h"
#include "../console.h"
#include "../command.h"
#include "../r_main.h"
#include "../lua_hook.h"
#include "sdlmain.h"
#ifdef HWRENDER
#include "../hardware/hw_main.h"
#include "../hardware/hw_drv.h"
// For dynamic referencing of HW rendering functions
#include "hwsym_sdl.h"
#include "ogl_sdl.h"
#include "hwbackend_sdl.h"
#endif

// maximum number of windowed modes (see windowedModes[][])
#define MAXWINMODES (18)

/**	\brief
*/
static INT32 numVidModes = -1;

/**	\brief
*/
static char vidModeName[33][32]; // allow 33 different modes

rendermode_t rendermode = render_soft;
static rendermode_t chosenrendermode = render_soft; // set by command line arguments

boolean highcolor = false;

static void Impl_SetVsync(void);

// Whether the game wants text input events; see I_SetTextInputMode
static bool textinputmode = false;

// synchronize page flipping with screen refresh
consvar_t cv_vidwait = CVAR_INIT ("vid_wait", "Off", "Synchronize framerate with refresh rate, eliminating screen tearing", CV_SAVE|CV_CALL|CV_NOINIT, CV_OnOff, Impl_SetVsync);
static consvar_t cv_stretch = CVAR_INIT ("stretch", "Off", NULL, CV_SAVE|CV_NOSHOWHELP, CV_OnOff, NULL);
static consvar_t cv_alwaysgrabmouse = CVAR_INIT ("alwaysgrabmouse", "Off", NULL, CV_SAVE, CV_OnOff, NULL);

UINT8 graphics_started = 0; // Is used in console.c and screen.c

// To disable fullscreen at startup; is set in VID_PrepareModeList
boolean allow_fullscreen = false;
static bool disable_fullscreen = false;
#define USE_FULLSCREEN (disable_fullscreen||!allow_fullscreen)? 0: (cv_fullscreen.value == 1)
static bool disable_mouse = false;
#define USE_MOUSEINPUT (!disable_mouse && cv_usemouse.value && havefocus)
#define IGNORE_MOUSE(condition) (!cv_alwaysgrabmouse.value && ((menuactive && !M_MouseNeeded()) || paused || con_destlines || chat_on || condition))
#define IGNORE_MOUSE_BUTTONS IGNORE_MOUSE(0)
#define IGNORE_MOUSE_GRAB IGNORE_MOUSE(gamestate != GS_LEVEL)
#define MOUSE_MENU false //(!disable_mouse && cv_usemouse.value && menuactive && !USE_FULLSCREEN)
#define MOUSEBUTTONS_MAX MOUSEBUTTONS

// first entry in the modelist which is not bigger than MAXVIDWIDTHxMAXVIDHEIGHT
static      INT32          firstEntry = 0;

// Total mouse motion X/Y offsets
static      float        mousemovex = 0.0f, mousemovey = 0.0f;

// SDL vars
static      SDL_Surface *vidSurface = NULL;
static      SDL_Surface *icoSurface = NULL;
static      UINT32       localPalette[256];
Uint16      realwidth = BASEVIDWIDTH;
Uint16      realheight = BASEVIDHEIGHT;
static       bool    mousegrabok = true;
static       bool    wrapmouseok = false;
#define HalfWarpMouse(x,y) if (wrapmouseok) SDL_WarpMouseInWindow(window, (float)(x/2),(float)(y/2))
static       bool    videoblitok = false;
static       bool    exposevideo = false;
static       bool    usesoftrenderer = false;
static       bool    borderlesswindow = false;

// SDL window/renderer vars
SDL_Window   *window;
SDL_Renderer *renderer;
static SDL_Texture  *texture;
static bool      havefocus = true;
static const char *fallback_resolution_name = "Fallback";

// windowed video modes from which to choose from.
static INT32 windowedModes[MAXWINMODES][2] =
{
	{1920,1200}, // 1.60,6.00
	{1920,1080}, // 1.66
	{1680,1050}, // 1.60,5.25
	{1600,1200}, // 1.33
	{1600, 900}, // 1.66
	{1366, 768}, // 1.66
	{1440, 900}, // 1.60,4.50
	{1280,1024}, // 1.33?
	{1280, 960}, // 1.33,4.00
	{1280, 800}, // 1.60,4.00
	{1280, 720}, // 1.66
	{1152, 864}, // 1.33,3.60
	{1024, 768}, // 1.33,3.20
	{ 800, 600}, // 1.33,2.50
	{ 640, 480}, // 1.33,2.00
	{ 640, 400}, // 1.60,2.00
	{ 320, 240}, // 1.33,1.00
	{ 320, 200}, // 1.60,1.00
};


static void Impl_VideoSetupBuffer(void);
static bool Impl_CreateWindow(bool fullscreen);
//static void Impl_SetWindowName(const char *title);
static void Impl_SetWindowIcon(void);

static void SDLSetMode(INT32 width, INT32 height, bool fullscreen, bool reposition)
{
	static bool wasfullscreen = false;
	const SDL_PixelFormat sw_texture_format = SDL_PIXELFORMAT_ABGR8888;

	realwidth = vid.width;
	realheight = vid.height;

	if (window)
	{
		if (fullscreen)
		{
			wasfullscreen = true;
			SDL_SetWindowFullscreen(window, true);
			I_SetBorderlessWindow();
		}
		else // windowed mode
		{
			if (wasfullscreen)
			{
				wasfullscreen = false;
				SDL_SetWindowFullscreen(window, false);
				I_SetBorderlessWindow();
			}
			// Reposition window only in windowed mode
			SDL_SetWindowSize(window, width, height);
			if (reposition)
			{
				SDL_DisplayID display = SDL_GetDisplayForWindow(window);
				SDL_SetWindowPosition(window,
					SDL_WINDOWPOS_CENTERED_DISPLAY(display),
					SDL_WINDOWPOS_CENTERED_DISPLAY(display)
				);
			}
		}
	}
	else
	{
		Impl_CreateWindow(fullscreen);
		Impl_SetWindowIcon();
		wasfullscreen = fullscreen;
		SDL_SetWindowSize(window, width, height);
		if (fullscreen)
		{
			SDL_SetWindowFullscreen(window, true);
			I_SetBorderlessWindow();
		}
	}

	// SDL3 applies window size, position and fullscreen changes asynchronously.
	// Wait for them like SDL2 did, so the window is in its final state from here on.
	if (window)
		SDL_SyncWindow(window);

#ifdef HWRENDER
	if (HWRENDERING)
	{
		sdlhwbackend_t *backend = SDLHW_Current();
		if (backend)
			backend->SetMode(vid.width, vid.height);
	}
#endif

	if (rendermode == render_soft)
	{
		SDL_RenderClear(renderer);
		SDL_SetRenderLogicalPresentation(renderer, width, height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
		// Set up Texture
		realwidth = width;
		realheight = height;
		if (texture != NULL)
		{
			SDL_DestroyTexture(texture);
		}

		texture = SDL_CreateTexture(renderer, sw_texture_format, SDL_TEXTUREACCESS_STREAMING, width, height);
		// SDL3 defaults to linear filtering, SDL2 defaulted to nearest
		if (texture)
			SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

		// Set up SW surface
		if (vidSurface != NULL)
		{
			SDL_DestroySurface(vidSurface);
		}
		if (vid.buffer)
		{
			free(vid.buffer);
			vid.buffer = NULL;
		}
		vidSurface = SDL_CreateSurface(width, height, sw_texture_format);
	}
}

static INT32 Impl_SDL_Scancode_To_Keycode(SDL_Scancode code)
{
	if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z)
	{
		// get lowercase ASCII
		return code - SDL_SCANCODE_A + 'a';
	}
	if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9)
	{
		return code - SDL_SCANCODE_1 + '1';
	}
	else if (code == SDL_SCANCODE_0)
	{
		return '0';
	}
	if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F10)
	{
		return KEY_F1 + (code - SDL_SCANCODE_F1);
	}
	switch (code)
	{
		// F11 and F12 are separated from the rest of the function keys
		case SDL_SCANCODE_F11: return KEY_F11;
		case SDL_SCANCODE_F12: return KEY_F12;

		case SDL_SCANCODE_KP_0: return KEY_KEYPAD0;
		case SDL_SCANCODE_KP_1: return KEY_KEYPAD1;
		case SDL_SCANCODE_KP_2: return KEY_KEYPAD2;
		case SDL_SCANCODE_KP_3: return KEY_KEYPAD3;
		case SDL_SCANCODE_KP_4: return KEY_KEYPAD4;
		case SDL_SCANCODE_KP_5: return KEY_KEYPAD5;
		case SDL_SCANCODE_KP_6: return KEY_KEYPAD6;
		case SDL_SCANCODE_KP_7: return KEY_KEYPAD7;
		case SDL_SCANCODE_KP_8: return KEY_KEYPAD8;
		case SDL_SCANCODE_KP_9: return KEY_KEYPAD9;

		case SDL_SCANCODE_RETURN:         return KEY_ENTER;
		case SDL_SCANCODE_ESCAPE:         return KEY_ESCAPE;
		case SDL_SCANCODE_BACKSPACE:      return KEY_BACKSPACE;
		case SDL_SCANCODE_TAB:            return KEY_TAB;
		case SDL_SCANCODE_SPACE:          return KEY_SPACE;
		case SDL_SCANCODE_MINUS:          return KEY_MINUS;
		case SDL_SCANCODE_EQUALS:         return KEY_EQUALS;
		case SDL_SCANCODE_LEFTBRACKET:    return '[';
		case SDL_SCANCODE_RIGHTBRACKET:   return ']';
		case SDL_SCANCODE_BACKSLASH:      return '\\';
		case SDL_SCANCODE_NONUSHASH:      return '#';
		case SDL_SCANCODE_SEMICOLON:      return ';';
		case SDL_SCANCODE_APOSTROPHE:     return '\'';
		case SDL_SCANCODE_GRAVE:          return '`';
		case SDL_SCANCODE_COMMA:          return ',';
		case SDL_SCANCODE_PERIOD:         return '.';
		case SDL_SCANCODE_SLASH:          return '/';
		case SDL_SCANCODE_CAPSLOCK:       return KEY_CAPSLOCK;
		case SDL_SCANCODE_PRINTSCREEN:    return 0; // undefined?
		case SDL_SCANCODE_SCROLLLOCK:     return KEY_SCROLLLOCK;
		case SDL_SCANCODE_PAUSE:          return KEY_PAUSE;
		case SDL_SCANCODE_INSERT:         return KEY_INS;
		case SDL_SCANCODE_HOME:           return KEY_HOME;
		case SDL_SCANCODE_PAGEUP:         return KEY_PGUP;
		case SDL_SCANCODE_DELETE:         return KEY_DEL;
		case SDL_SCANCODE_END:            return KEY_END;
		case SDL_SCANCODE_PAGEDOWN:       return KEY_PGDN;
		case SDL_SCANCODE_RIGHT:          return KEY_RIGHTARROW;
		case SDL_SCANCODE_LEFT:           return KEY_LEFTARROW;
		case SDL_SCANCODE_DOWN:           return KEY_DOWNARROW;
		case SDL_SCANCODE_UP:             return KEY_UPARROW;
		case SDL_SCANCODE_NUMLOCKCLEAR:   return KEY_NUMLOCK;
		case SDL_SCANCODE_KP_DIVIDE:      return KEY_KPADSLASH;
		case SDL_SCANCODE_KP_MULTIPLY:    return '*'; // undefined?
		case SDL_SCANCODE_KP_MINUS:       return KEY_MINUSPAD;
		case SDL_SCANCODE_KP_PLUS:        return KEY_PLUSPAD;
		case SDL_SCANCODE_KP_ENTER:       return KEY_ENTER;
		case SDL_SCANCODE_KP_PERIOD:      return KEY_KPADDEL;
		case SDL_SCANCODE_NONUSBACKSLASH: return '\\';

		case SDL_SCANCODE_LSHIFT: return KEY_LSHIFT;
		case SDL_SCANCODE_RSHIFT: return KEY_RSHIFT;
		case SDL_SCANCODE_LCTRL:  return KEY_LCTRL;
		case SDL_SCANCODE_RCTRL:  return KEY_RCTRL;
		case SDL_SCANCODE_LALT:   return KEY_LALT;
		case SDL_SCANCODE_RALT:   return KEY_RALT;
		case SDL_SCANCODE_LGUI:   return KEY_LEFTWIN;
		case SDL_SCANCODE_RGUI:   return KEY_RIGHTWIN;
		default:                  break;
	}
	return 0;
}


static void SDLdoGrabMouse(void)
{
	SDL_HideCursor();
	SDL_SetWindowMouseGrab(window, true);
	if (SDL_SetWindowRelativeMouseMode(window, true)) // already warps mouse if successful
		wrapmouseok = true; // TODO: is wrapmouseok or HalfWarpMouse needed anymore?
}

static void SDLdoUngrabMouse(void)
{
	SDL_ShowCursor();
	SDL_SetWindowMouseGrab(window, false);
	wrapmouseok = false;
	SDL_SetWindowRelativeMouseMode(window, false);
}

void SDLforceUngrabMouse(void)
{
	if (SDL_WasInit(SDL_INIT_VIDEO) && window != NULL)
		SDLdoUngrabMouse();
}

void I_UpdateMouseGrab(void)
{
	if (SDL_WasInit(SDL_INIT_VIDEO) && window != NULL
	&& SDL_GetMouseFocus() == window && SDL_GetKeyboardFocus() == window
	&& !IGNORE_MOUSE_GRAB)
		SDLdoGrabMouse();
}

static void VID_Command_NumModes_f (void)
{
	CONS_Printf(M_GetText("%d video mode(s) available(s)\n"), VID_NumModes());
}

// SDL doesn't have SDL_GetVideoSurface or a lot of the SDL_Surface flags that SDL 1.2 had
static void SurfaceInfo(const SDL_Surface *infoSurface, const char *SurfaceText)
{
	INT32 vfBPP;

	if (!infoSurface)
		return;

	if (!SurfaceText)
		SurfaceText = M_GetText("Unknown Surface");

	vfBPP = SDL_BITSPERPIXEL(infoSurface->format);

	CONS_Printf("\x82" "%s\n", SurfaceText);
	CONS_Printf(M_GetText(" %ix%i at %i bit color\n"), infoSurface->w, infoSurface->h, vfBPP);

	if (infoSurface->flags&SDL_SURFACE_PREALLOCATED)
		CONS_Printf("%s", M_GetText(" Uses preallocated memory\n"));
	else
		CONS_Printf("%s", M_GetText(" Stored in system memory\n"));
}

static void VID_Command_Info_f (void)
{
#if 0
	SDLSTUB();
#else
#if 0
	const SDL_VideoInfo *videoInfo;
	videoInfo = SDL_GetVideoInfo(); //Alam: Double-Check
	if (videoInfo)
	{
		CONS_Printf("%s", M_GetText("Video Interface Capabilities:\n"));
		if (videoInfo->hw_available)
			CONS_Printf("%s", M_GetText(" Hardware surfaces\n"));
		if (videoInfo->wm_available)
			CONS_Printf("%s", M_GetText(" Window manager\n"));
		//UnusedBits1  :6
		//UnusedBits2  :1
		if (videoInfo->blit_hw)
			CONS_Printf("%s", M_GetText(" Accelerated blits HW-2-HW\n"));
		if (videoInfo->blit_hw_CC)
			CONS_Printf("%s", M_GetText(" Accelerated blits HW-2-HW with Colorkey\n"));
		if (videoInfo->wm_available)
			CONS_Printf("%s", M_GetText(" Accelerated blits HW-2-HW with Alpha\n"));
		if (videoInfo->blit_sw)
		{
			CONS_Printf("%s", M_GetText(" Accelerated blits SW-2-HW\n"));
			if (!M_CheckParm("-noblit")) videoblitok = true;
		}
		if (videoInfo->blit_sw_CC)
			CONS_Printf("%s", M_GetText(" Accelerated blits SW-2-HW with Colorkey\n"));
		if (videoInfo->blit_sw_A)
			CONS_Printf("%s", M_GetText(" Accelerated blits SW-2-HW with Alpha\n"));
		if (videoInfo->blit_fill)
			CONS_Printf("%s", M_GetText(" Accelerated Color filling\n"));
		//UnusedBits3  :16
		if (videoInfo->video_mem)
			CONS_Printf(M_GetText(" There is %i KB of video memory\n"), videoInfo->video_mem);
		else
			CONS_Printf("%s", M_GetText(" There no video memory for SDL\n"));
		//*vfmt
	}
#else
	if (!M_CheckParm("-noblit")) videoblitok = true;
#endif
	SurfaceInfo(vidSurface, M_GetText("Current Video Mode"));
#endif
}

static void VID_Command_ModeList_f(void)
{
	// List windowed modes
	INT32 i = 0;
	CONS_Printf("NOTE: Under SDL, all modes are supported on all platforms.\n");
	CONS_Printf("Under opengl, fullscreen only supports native desktop resolution.\n");
	CONS_Printf("Under software, the mode is stretched up to desktop resolution.\n");
	for (i = 0; i < MAXWINMODES; i++)
	{
		CONS_Printf("%2d: %dx%d\n", i, windowedModes[i][0], windowedModes[i][1]);
	}

}

static void VID_Command_Mode_f (void)
{
	INT32 modenum;

	if (COM_Argc()!= 2)
	{
		CONS_Printf(M_GetText("vid_mode <modenum> : set video mode, current video mode %i\n"), vid.modenum);
		return;
	}

	modenum = atoi(COM_Argv(1));

	if (modenum >= VID_NumModes())
		CONS_Printf(M_GetText("Video mode not present\n"));
	else
	{
#ifdef NATIVESCREENRES
		CV_StealthSetValue(&cv_nativeres, false);
#endif
		setmodeneeded = modenum+1; // request vid mode change
	}
}

static void Impl_HandleWindowEvent(SDL_WindowEvent evt)
{
	static bool firsttimeonmouse = true;
	static bool mousefocus = true;
	static bool kbfocus = true;

	switch (evt.type)
	{
		case SDL_EVENT_WINDOW_MOUSE_ENTER:
			mousefocus = true;
			break;
		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			mousefocus = false;
			break;
		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			kbfocus = true;
			mousefocus = true;
			break;
		case SDL_EVENT_WINDOW_FOCUS_LOST:
			kbfocus = false;
			mousefocus = false;
			break;
		case SDL_EVENT_WINDOW_MAXIMIZED:
			break;
		case SDL_EVENT_WINDOW_MOVED:
			window_x = evt.data1;
			window_y = evt.data2;
			break;
		default:
			break;
	}

	if (mousefocus && kbfocus)
	{
		// Tell game we got focus back, resume music if necessary
		window_notinfocus = false;
		if (!paused)
			S_ResumeAudio(); //resume it

		I_ToggleControllerRumble(true);
		P_UnpauseRumble(NULL);

		if (!firsttimeonmouse)
		{
			if (cv_usemouse.value) I_StartupMouse();
		}
		//else firsttimeonmouse = false;

		if (USE_MOUSEINPUT && !IGNORE_MOUSE_GRAB)
			SDLdoGrabMouse();

	}
	else if (!mousefocus && !kbfocus)
	{
		// Tell game we lost focus, pause music
		window_notinfocus = true;
		if (! cv_playmusicifunfocused.value)
			S_PauseAudio();
		if (! cv_playsoundsifunfocused.value)
			S_StopSounds();

		if (!disable_mouse)
		{
			SDLforceUngrabMouse();
		}
		memset(gamekeydown, 0, NUMKEYS); // TODO this is a scary memset

		I_ToggleControllerRumble(false);
		if (P_AutoPause())
			P_PauseRumble(NULL);

		if (MOUSE_MENU)
		{
			SDLdoUngrabMouse();
		}
	}

}

static void Impl_HandleKeyboardEvent(SDL_KeyboardEvent evt, Uint32 type)
{
	event_t event;
	if (type == SDL_EVENT_KEY_UP)
	{
		event.type = ev_keyup;
	}
	else if (type == SDL_EVENT_KEY_DOWN)
	{
		event.type = ev_keydown;
	}
	else
	{
		return;
	}
	event.data1 = Impl_SDL_Scancode_To_Keycode(evt.scancode);
	if (event.data1) D_PostEvent(&event);
}

static void Impl_HandleMouseMotionEvent(SDL_MouseMotionEvent evt)
{
	static boolean firstmove = true;

	if (USE_MOUSEINPUT)
	{
		if ((SDL_GetMouseFocus() != window && SDL_GetKeyboardFocus() != window) || (IGNORE_MOUSE_GRAB && !firstmove))
		{
			SDLdoUngrabMouse();
			firstmove = false;
			return;
		}

		// If using relative mouse mode, don't post an event_t just now,
		// add on the offsets so we can make an overall event later.
		if (SDL_GetWindowRelativeMouseMode(window))
		{
			if (SDL_GetMouseFocus() == window && SDL_GetKeyboardFocus() == window)
			{
				mousemovex +=  evt.xrel;
				mousemovey += -evt.yrel;
				SDL_SetWindowMouseGrab(window, true);
			}
			firstmove = false;
			return;
		}

		// If the event is from warping the pointer to middle
		// of the screen then ignore it.
		if (((INT32)evt.x == realwidth/2) && ((INT32)evt.y == realheight/2))
		{
			firstmove = false;
			return;
		}

		// Don't send an event_t if not in relative mouse mode anymore,
		// just grab and set relative mode
		// this fixes the stupid camera jerk on mouse entering bug
		// -- Monster Iestyn
		if (SDL_GetMouseFocus() == window && SDL_GetKeyboardFocus() == window)
		{
			SDLdoGrabMouse();
		}
	}

	firstmove = false;
}

static void Impl_HandleMouseButtonEvent(SDL_MouseButtonEvent evt, Uint32 type)
{
	event_t event;

	SDL_memset(&event, 0, sizeof(event_t));

	// Ignore the event if the mouse is not actually focused on the window.
	// This can happen if you used the mouse to restore keyboard focus;
	// this apparently makes a mouse button down event but not a mouse button up event,
	// resulting in whatever key was pressed down getting "stuck" if we don't ignore it.
	// -- Monster Iestyn (28/05/18)
	if (SDL_GetMouseFocus() != window || IGNORE_MOUSE_BUTTONS)
		return;

	/// \todo inputEvent.button.which
	if (USE_MOUSEINPUT)
	{
		if (type == SDL_EVENT_MOUSE_BUTTON_UP)
		{
			event.type = ev_keyup;
		}
		else if (type == SDL_EVENT_MOUSE_BUTTON_DOWN)
		{
			event.type = ev_keydown;
		}
		else return;
		if (evt.button == SDL_BUTTON_MIDDLE)
			event.data1 = KEY_MOUSE1+2;
		else if (evt.button == SDL_BUTTON_RIGHT)
			event.data1 = KEY_MOUSE1+1;
		else if (evt.button == SDL_BUTTON_LEFT)
			event.data1 = KEY_MOUSE1;
		else if (evt.button == SDL_BUTTON_X1)
			event.data1 = KEY_MOUSE1+3;
		else if (evt.button == SDL_BUTTON_X2)
			event.data1 = KEY_MOUSE1+4;
		if (event.type == ev_keyup || event.type == ev_keydown)
		{
			D_PostEvent(&event);
		}
	}
}

static void Impl_HandleMouseWheelEvent(SDL_MouseWheelEvent evt)
{
	event_t event;

	SDL_memset(&event, 0, sizeof(event_t));

	if (evt.y > 0.0f)
	{
		event.data1 = KEY_MOUSEWHEELUP;
		event.type = ev_keydown;
	}
	else if (evt.y < 0.0f)
	{
		event.data1 = KEY_MOUSEWHEELDOWN;
		event.type = ev_keydown;
	}
	else
	{
		event.data1 = 0;
		event.type = ev_keyup;
	}
	if (event.type == ev_keyup || event.type == ev_keydown)
	{
		D_PostEvent(&event);
	}
}

static void Impl_HandleTextEvent(SDL_TextInputEvent evt)
{
	event_t event;
	event.type = ev_text;
	if (!evt.text || evt.text[0] == '\0' || evt.text[1] != '\0')
	{
		// limit ourselves to ASCII for now, we can add UTF-8 support later
		return;
	}
	event.data1 = evt.text[0];
	D_PostEvent(&event);
}

#if defined (HWRENDER) && defined (__ANDROID__)
// Android gives the window a new surface when the app comes back from the background.
// A backend that holds on to the window (SDL GPU) can go on drawing to the old one,
// which shows nothing, so it lets go of the window and claims it again,
// as soon as the new surface is there.
static boolean reclaim_window = false;

static void Impl_ReclaimWindow(void)
{
	sdlhwbackend_t *backend = SDLHW_Current();

	if (!reclaim_window)
		return;

	if (!window || !HWRENDERING || !backend || !backend->ReleaseWindow)
	{
		reclaim_window = false;
		return;
	}

	// the new surface isn't there yet
	if (!SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, NULL))
		return;

	reclaim_window = false;
	backend->ReleaseWindow(window);
	if (!backend->CreateContext(window))
		CONS_Alert(CONS_ERROR, "Couldn't claim the window again for %s: %s\n", backend->name, SDL_GetError());
}
#endif

void I_GetEvent(void)
{
	SDL_Event evt;
	// We only want the first motion event,
	// otherwise we'll end up catching the warp back to center.
	//int mouseMotionOnce = 0;

	if (!graphics_started)
	{
		return;
	}

	mousemovex = mousemovey = 0.0f;

	while (SDL_PollEvent(&evt))
	{
		switch (evt.type)
		{
			case SDL_EVENT_WINDOW_MOUSE_ENTER:
			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
			case SDL_EVENT_WINDOW_FOCUS_LOST:
			case SDL_EVENT_WINDOW_MAXIMIZED:
			case SDL_EVENT_WINDOW_MOVED:
				Impl_HandleWindowEvent(evt.window);
				break;
#if defined (HWRENDER) && defined (__ANDROID__)
			case SDL_EVENT_DID_ENTER_FOREGROUND:
				reclaim_window = true;
				break;
#endif
			case SDL_EVENT_KEY_UP:
			case SDL_EVENT_KEY_DOWN:
				Impl_HandleKeyboardEvent(evt.key, evt.type);
				break;
			case SDL_EVENT_TEXT_INPUT:
				Impl_HandleTextEvent(evt.text);
				break;
			case SDL_EVENT_MOUSE_MOTION:
				//if (!mouseMotionOnce)
				Impl_HandleMouseMotionEvent(evt.motion);
				//mouseMotionOnce = 1;
				break;
			case SDL_EVENT_MOUSE_BUTTON_UP:
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				Impl_HandleMouseButtonEvent(evt.button, evt.type);
				break;
			case SDL_EVENT_MOUSE_WHEEL:
				Impl_HandleMouseWheelEvent(evt.wheel);
				break;
			case SDL_EVENT_GAMEPAD_AXIS_MOTION:
				I_HandleControllerAxisEvent(evt.gaxis);
				break;
			case SDL_EVENT_GAMEPAD_BUTTON_UP:
			case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
				I_HandleControllerButtonEvent(evt.gbutton, evt.type);
				break;
			case SDL_EVENT_GAMEPAD_ADDED:
				I_ControllerDeviceAdded(evt.gdevice.which);
				break;
			case SDL_EVENT_GAMEPAD_REMOVED:
				I_ControllerDeviceRemoved();
				break;
			case SDL_EVENT_QUIT:
				if (Playing())
					LUAh_GameQuit();
				I_Quit();
				break;
		}
	}

	// Send all relative mouse movement as one single mouse event.
	if (fpclassify(mousemovex) != FP_ZERO || fpclassify(mousemovey) != FP_ZERO)
	{
		event_t event;
		float xscale = 1.0f, yscale = 1.0f;
		//SDL_memset(&event, 0, sizeof(event_t));

		// SDL2 scaled relative motion down by the renderer's logical size, which the
		// software renderer undid here; SDL3 always reports motion in window coordinates.
		// OpenGL has no SDL renderer, so there the scale was (and still is) applied as is.
		if (rendermode != render_soft)
		{
			int wwidth, wheight;
			SDL_GetWindowSize(window, &wwidth, &wheight);
			xscale = (float)wwidth / (float)realwidth;
			yscale = (float)wheight / (float)realheight;
		}

		event.type = ev_mouse;
		event.data1 = 0;
		event.data2 = (INT32)lround(mousemovex * xscale);
		event.data3 = (INT32)lround(mousemovey * yscale);
		D_PostEvent(&event);
	}

	// In order to make wheels act like buttons, we have to set their state to Up.
	// This is because wheel messages don't have an up/down state.
	gamekeydown[KEY_MOUSEWHEELDOWN] = gamekeydown[KEY_MOUSEWHEELUP] = 0;

#if defined (HWRENDER) && defined (__ANDROID__)
	Impl_ReclaimWindow();
#endif
}

void I_StartupMouse(void)
{
	static bool firsttimeonmouse = true;

	if (disable_mouse)
		return;

	if (!firsttimeonmouse)
	{
		HalfWarpMouse(realwidth, realheight); // warp to center
	}
	else
		firsttimeonmouse = false;
	if (cv_usemouse.value && !IGNORE_MOUSE_GRAB)
		SDLdoGrabMouse();
	else
		SDLdoUngrabMouse();
}

//
// I_OsPolling
//
void I_OsPolling(void)
{
	SDL_Keymod mod;

#ifndef __EMSCRIPTEN__
	if (consolevent)
		I_GetConsoleEvents();
#endif

	I_UpdateControllers();
	I_GetMouseEvents();

	I_GetEvent();

	mod = SDL_GetModState();
	/* Handle here so that our state is always synched with the system. */
	shiftdown = ctrldown = altdown = 0;
	capslock = false;
	if (mod & SDL_KMOD_LSHIFT) shiftdown |= 1;
	if (mod & SDL_KMOD_RSHIFT) shiftdown |= 2;
#ifdef __APPLE__
	if (mod & SDL_KMOD_LGUI)    ctrldown |= 1;
	if (mod & SDL_KMOD_RGUI)    ctrldown |= 2;
#else
	if (mod & SDL_KMOD_LCTRL)   ctrldown |= 1;
	if (mod & SDL_KMOD_RCTRL)   ctrldown |= 2;
#endif
	if (mod & SDL_KMOD_LALT)     altdown |= 1;
	if (mod & SDL_KMOD_RALT)     altdown |= 2;
	if (mod & SDL_KMOD_CAPS) capslock = true;
}

//
// I_UpdateNoBlit
//
void I_UpdateNoBlit(void)
{
	if (rendermode == render_none)
		return;
	if (exposevideo)
	{
#ifdef HWRENDER
		if (HWRENDERING)
		{
			sdlhwbackend_t *backend = SDLHW_Current();
			if (backend)
				backend->FinishUpdate(cv_vidwait.value);
		}
		else
#endif
		if (rendermode == render_soft)
		{
			SDL_RenderTexture(renderer, texture, NULL, NULL);
			SDL_RenderPresent(renderer);
		}
	}
	exposevideo = false;
}

// I_SkipFrame
//
// Returns true if it thinks we can afford to skip this frame
// from PrBoom's src/SDL/i_video.c
static inline boolean I_SkipFrame(void)
{
#if 1
	return false;
#else
	static boolean skip = false;

	if (rendermode != render_soft)
		return false;

	skip = !skip;

	switch (gamestate)
	{
		case GS_LEVEL:
			if (!paused)
				return false;
			/* FALLTHRU */
		case GS_TIMEATTACK:
		case GS_WAITINGPLAYERS:
			return skip; // Skip odd frames
		default:
			return false;
	}
#endif
}

//
// I_FinishUpdate
//

static SDL_Rect src_rect = { 0, 0, 0, 0 };

void I_FinishUpdate(void)
{
	if (rendermode == render_none)
		return; //Alam: No software or OpenGl surface

	if (I_SkipFrame())
		return;

	SCR_CalculateFPS();

	SCR_DisplayTicRate();

	if (cv_showping.value && netgame && consoleplayer != serverplayer)
		SCR_DisplayLocalPing();

	if (marathonmode)
		SCR_DisplayMarathonInfo();

	ST_MovieInfoDrawer();

	if (rendermode == render_soft && screens[0])
	{
		SDL_LockSurface(vidSurface);
		// copy pixels ourselves to the video surface (prevents a crash in libsdl)
		UINT32 *restrict dst = vidSurface->pixels;
		const UINT8 *restrict src = screens[0];
		const INT32 count = vid.width * vid.height;
		for (INT32 i = 0; i < count; i++)
			*dst++ = localPalette[*src++];
		SDL_UnlockSurface(vidSurface);
		// Fury -- there's no way around UpdateTexture, the GL backend uses it anyway
		SDL_LockSurface(vidSurface);
		SDL_UpdateTexture(texture, &src_rect, vidSurface->pixels, vidSurface->pitch);
		SDL_UnlockSurface(vidSurface);

		SDL_RenderClear(renderer);
		SDL_FRect src_frect;
		SDL_RectToFRect(&src_rect, &src_frect);
		SDL_RenderTexture(renderer, texture, &src_frect, NULL);
		SDL_RenderPresent(renderer);
	}
#ifdef HWRENDER
	else if (HWRENDERING)
	{
		sdlhwbackend_t *backend = SDLHW_Current();
		// Final postprocess step of palette rendering, after everything else has been drawn.
	if (HWR_ShouldUsePaletteRendering())
	{
		HWD.pfnMakeScreenTexture(HWD_SCREENTEXTURE_GENERIC2);
		HWD.pfnSetShader(HWR_GetShaderFromTarget(SHADER_PALETTE_POSTPROCESS));
		HWD.pfnDrawScreenTexture(HWD_SCREENTEXTURE_GENERIC2, NULL, 0);
		HWD.pfnUnSetShader();
	}
		if (backend)
			backend->FinishUpdate(cv_vidwait.value);
	}
#endif
	exposevideo = false;
}

//
// I_UpdateNoVsync
//
void I_UpdateNoVsync(void)
{
	INT32 real_vidwait = cv_vidwait.value;
	cv_vidwait.value = 0;
	I_FinishUpdate();
	cv_vidwait.value = real_vidwait;
}

//
// I_ReadScreen
//
void I_ReadScreen(UINT8 *scr)
{
	if (rendermode != render_soft)
		I_Error ("I_ReadScreen: called while in non-software mode");
	else
		VID_BlitLinearScreen(screens[0], scr,
			vid.width*vid.bpp, vid.height,
			vid.rowbytes, vid.rowbytes);
}

//
// I_SetPalette
//
void I_SetPalette(RGBA_t *palette)
{
	size_t i;
	for (i = 0; i < 256; i++)
	{
		localPalette[i] = 0xff000000;
		localPalette[i] |= palette[i].s.red << 0;
		localPalette[i] |= palette[i].s.green << 8;
		localPalette[i] |= palette[i].s.blue << 16;
	}
}

// return number of fullscreen + X11 modes
INT32 VID_NumModes(void)
{
	if (USE_FULLSCREEN && numVidModes != -1)
		return numVidModes - firstEntry;
	else
		return MAXWINMODES;
}

const char *VID_GetModeName(INT32 modeNum)
{
#if 0
	if (USE_FULLSCREEN && numVidModes != -1) // fullscreen modes
	{
		modeNum += firstEntry;
		if (modeNum >= numVidModes)
			return NULL;

		sprintf(&vidModeName[modeNum][0], "%dx%d",
			modeList[modeNum]->w,
			modeList[modeNum]->h);
	}
	else // windowed modes
	{
#endif
	if (modeNum == -1)
	{
		return fallback_resolution_name;
	}
		if (modeNum > MAXWINMODES)
			return NULL;

		sprintf(&vidModeName[modeNum][0], "%dx%d",
			windowedModes[modeNum][0],
			windowedModes[modeNum][1]);
	//}
	return &vidModeName[modeNum][0];
}

INT32 VID_GetModeForSize(INT32 w, INT32 h)
{
	int i;
	for (i = 0; i < MAXWINMODES; i++)
	{
		if (windowedModes[i][0] == w && windowedModes[i][1] == h)
		{
			return i;
		}
	}
	return -1;
#if 0
	INT32 matchMode = -1, i;
	VID_PrepareModeList();
	if (USE_FULLSCREEN && numVidModes != -1)
	{
		for (i=firstEntry; i<numVidModes; i++)
		{
			if (modeList[i]->w == w &&
			    modeList[i]->h == h)
			{
				matchMode = i;
				break;
			}
		}
		if (-1 == matchMode) // use smaller mode
		{
			w -= w%BASEVIDWIDTH;
			h -= h%BASEVIDHEIGHT;
			for (i=firstEntry; i<numVidModes; i++)
			{
				if (modeList[i]->w == w &&
				    modeList[i]->h == h)
				{
					matchMode = i;
					break;
				}
			}
			if (-1 == matchMode) // use smallest mode
				matchMode = numVidModes-1;
		}
		matchMode -= firstEntry;
	}
	else
	{
		for (i=0; i<MAXWINMODES; i++)
		{
			if (windowedModes[i][0] == w &&
			    windowedModes[i][1] == h)
			{
				matchMode = i;
				break;
			}
		}
		if (-1 == matchMode) // use smaller mode
		{
			w -= w%BASEVIDWIDTH;
			h -= h%BASEVIDHEIGHT;
			for (i=0; i<MAXWINMODES; i++)
			{
				if (windowedModes[i][0] == w &&
				    windowedModes[i][1] == h)
				{
					matchMode = i;
					break;
				}
			}
			if (-1 == matchMode) // use smallest mode
				matchMode = MAXWINMODES-1;
		}
	}
	return matchMode;
#endif
}

void VID_PrepareModeList(void)
{
	// Under SDL, we just use the windowed modes list, and scale in windowed fullscreen.
	allow_fullscreen = true;
#if 0
	INT32 i;

	firstEntry = 0;

#ifdef HWRENDER
	if (rendermode == render_opengl)
		modeList = SDL_ListModes(NULL, SDL_OPENGL|SDL_FULLSCREEN);
	else
#endif
	modeList = SDL_ListModes(NULL, surfaceFlagsF|SDL_HWSURFACE); //Alam: At least hardware surface

	if (disable_fullscreen?0:cv_fullscreen.value) // only fullscreen needs preparation
	{
		if (-1 != numVidModes)
		{
			for (i=0; i<numVidModes; i++)
			{
				if (modeList[i]->w <= MAXVIDWIDTH &&
					modeList[i]->h <= MAXVIDHEIGHT)
				{
					firstEntry = i;
					break;
				}
			}
		}
	}
	allow_fullscreen = true;
#endif
}


static UINT32 refresh_rate;
static UINT32 VID_GetRefreshRate(void)
{
	SDL_DisplayID display;
	const SDL_DisplayMode *m;

	if (SDL_WasInit(SDL_INIT_VIDEO) == 0)
	{
		// Video not init yet.
		return 0;
	}

	display = window ? SDL_GetDisplayForWindow(window) : 0;
	if (!display)
		display = SDL_GetPrimaryDisplay();

	m = SDL_GetCurrentDisplayMode(display);
	if (m == NULL)
	{
		// Error has occurred.
		return 0;
	}

	return (UINT32)lroundf(m->refresh_rate);
}


static bool Impl_CreateContext(void)
{
	// Renderer-specific stuff
#ifdef HWRENDER
	if (HWRENDERING && (VID_HardwareState(rendermode) != VID_GL_LIBRARY_ERROR))
	{
		sdlhwbackend_t *backend = SDLHW_Current();
		if (backend && !backend->CreateContext(window))
		{
			CONS_Printf(M_GetText("Couldn't create the %s context: %s\n"), backend->name, SDL_GetError());
			return false;
		}
	}
	else
#endif
	if (rendermode == render_soft)
	{
		if (!renderer)
		{
			renderer = SDL_CreateRenderer(window, usesoftrenderer ? SDL_SOFTWARE_RENDERER : NULL);
			if (renderer && !usesoftrenderer)
				SDL_SetRenderVSync(renderer, cv_vidwait.value ? 1 : 0);
		}
		if (renderer == NULL)
		{
			CONS_Printf(M_GetText("Couldn't create rendering context: %s\n"), SDL_GetError());
			return false;
		}
		SDL_SetRenderLogicalPresentation(renderer, BASEVIDWIDTH, BASEVIDHEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	}
	return true;
}

void VID_CheckHardwareLoaded(rendermode_t oldrender)
{
#ifdef HWRENDER
	if (VID_HardwareState(rendermode) == VID_GL_LIBRARY_ERROR) // Well, it didn't work the first time anyway.
	{
		rendermode = oldrender;
		if (RENDERMODE_IS_HARDWARE(chosenrendermode)) // fallback to software
			rendermode = render_soft;
		if (setrenderneeded)
		{
			CV_StealthSetValue(&cv_renderer, oldrender);
			setrenderneeded = 0;
		}
	}
#endif
}

#ifdef HWRENDER
static void Impl_DestroyWindow(void)
{
	// Destroy the current window, if it exists.
	if (window)
	{
		SDLHW_ReleaseWindow(window);
		SDL_DestroyWindow(window);
		window = NULL;
	}

	// Destroy the current window rendering context, if that also exists.
	if (renderer)
	{
		SDL_DestroyRenderer(renderer);
		renderer = NULL;
	}
}
#endif

void VID_CheckRenderer(void)
{
	boolean rendererchanged = false;
	boolean contextcreated = false;
	rendermode_t oldrenderer = rendermode;

	if (dedicated)
		return;

	if (setrenderneeded)
	{
		rendermode = setrenderneeded;
		rendererchanged = true;

#ifdef HWRENDER
		if (HWRENDERING)
		{
			// The mode the player asked for. VID_CheckHardwareLoaded
			// and VID_StartupHardware can change rendermode.
			const rendermode_t hwmode = rendermode;
			sdlhwbackend_t *oldbackend = SDLHW_GetBackend(oldrenderer);
			sdlhwbackend_t *newbackend = SDLHW_GetBackend(hwmode);
			INT32 hwstate;

			VID_CheckHardwareLoaded(oldrenderer);

			// Going from a hardware backend to another one: whatever was
			// uploaded to the old one can't be used with the new one.
			if (rendermode == hwmode && hwmode != oldrenderer
			&& oldbackend && oldbackend->state == VID_GL_LIBRARY_LOADED)
				HWR_DriverDeactivate(oldrenderer);

#if (defined(__APPLE__) || defined(_WIN32))
			// macOS and Windows need the window and rendering context to be remade on every renderer switch, or else the video will freeze.
			Impl_DestroyWindow();
#else
			// Some backends can't be moved between windows.
			if (rendermode == hwmode && hwmode != oldrenderer
			&& ((oldbackend && oldbackend->needsnewwindow) || (newbackend && newbackend->needsnewwindow)))
				Impl_DestroyWindow();
#endif

			// Initialise the backend before calling SDLSetMode!!!
			// This is because SDLSetMode calls the backend's SetMode, which needs it.
			hwstate = VID_HardwareState(hwmode);
			VID_StartupHardware(hwmode);

			if (hwstate == VID_GL_LIBRARY_NOTLOADED)
			{
				// Loaded successfully!
				if (VID_HardwareState(hwmode) == VID_GL_LIBRARY_LOADED)
				{
#if !defined(__APPLE__) && !defined(_WIN32)
					if (!newbackend->needsnewwindow) // already done otherwise
					{
						Impl_DestroyWindow();

						// Create a new window.
						Impl_CreateWindow(USE_FULLSCREEN);

						// From there, the graphics context was already created.
						contextcreated = true;
					}
#endif // __APPLE__
				}
			}
			else if (hwstate == VID_GL_LIBRARY_ERROR)
				rendererchanged = false;
		}
		else
		{
#if (defined(__APPLE__) || defined(_WIN32)) && defined(HWRENDER)
			// macOS and Windows need the window and rendering context to be remade on every renderer switch, or else the video will freeze.
			Impl_DestroyWindow();
#else
			// Leaving a backend that can't share its window
			sdlhwbackend_t *oldbackend = SDLHW_GetBackend(oldrenderer);
			if (oldbackend && oldbackend->needsnewwindow)
				Impl_DestroyWindow();
#endif
		}
#endif // HWRENDER

		if (!contextcreated)
		{
			// Create a new window.
			Impl_CreateWindow(USE_FULLSCREEN);
			Impl_CreateContext();
		}

		setrenderneeded = 0;
	}

	SDLSetMode(vid.width, vid.height, USE_FULLSCREEN, (rendererchanged ? false : true));
	Impl_VideoSetupBuffer();

	if (rendermode == render_soft)
	{

		if (rendererchanged)
		{
#ifdef HWRENDER
			if (vid.glstate == VID_GL_LIBRARY_LOADED)  // Only if a hardware backend ever loaded!
				HWR_FreeTextureCache();
#endif
			SCR_SetDrawFuncs();
		}
	}
#ifdef HWRENDER
	else if (HWRENDERING)
	{
		if (rendererchanged)
		{
			R_InitHardwareMode();
			V_SetPalette(0);
		}
	}
#else
	(void)oldrenderer;
#endif
}

INT32 VID_SetMode(INT32 modeNum)
{
	SDLdoUngrabMouse();

	vid.recalc = 1;
	vid.bpp = 1;

#ifdef NATIVESCREENRES
	if (cv_nativeres.value)
	{
		int i, numdisplays = 0;
		SDL_DisplayID *displays = SDL_GetDisplays(&numdisplays);

		for (i = 0; displays && i < numdisplays; i++)
		{
			const SDL_DisplayMode *resolution = SDL_GetCurrentDisplayMode(displays[i]);
			if (resolution)
			{
				// SDL3 display modes are in window coordinates; scale to pixels
				float density = resolution->pixel_density > 0.0f ? resolution->pixel_density : 1.0f;
				vid.width = (INT32)(resolution->w * density) / (cv_nativeresdiv.value);
				vid.height = (INT32)(resolution->h * density) / (cv_nativeresdiv.value);

#if TARGET_OS_IPHONE
				// hack to fix NATIVESCREENRES on iOS
				// SDL may not report anything other than a portrait resolution
				if (vid.width < vid.height)
				{
					INT32 temp = vid.width;
					vid.width = vid.height;
					vid.height = temp;
				}
#endif

				if (vid.width > MAXVIDWIDTH)
					vid.width = MAXVIDWIDTH;
				else if (vid.width < BASEVIDWIDTH)
					vid.width = BASEVIDWIDTH;

				if (vid.height > MAXVIDHEIGHT)
					vid.height = MAXVIDHEIGHT;
				else if (vid.height < BASEVIDHEIGHT)
					vid.height = BASEVIDHEIGHT;

				break;
			}
		}
		SDL_free(displays);

		vid.modenum = VID_GetModeForSize(cv_scr_width.value, cv_scr_height.value);
	}
	else
#endif
	{
		if (modeNum < 0)
			modeNum = 0;
		if (modeNum >= MAXWINMODES)
			modeNum = MAXWINMODES-1;

		vid.width = windowedModes[modeNum][0];
		vid.height = windowedModes[modeNum][1];
		vid.modenum = modeNum;
	}

	//Impl_SetWindowName("SRB2 "VERSIONSTRING);
	src_rect.w = vid.width;
	src_rect.h = vid.height;

	refresh_rate = VID_GetRefreshRate();

	VID_CheckRenderer();
	return 1;
}

static bool Impl_CreateWindow(bool fullscreen)
{
	SDL_WindowFlags flags = 0;

	if (rendermode == render_none) // dedicated
		return true; // Monster Iestyn -- not sure if it really matters what we return here tbh

	if (window != NULL)
		return false;

	if (fullscreen)
		flags |= SDL_WINDOW_FULLSCREEN;

	if (borderlesswindow)
		flags |= SDL_WINDOW_BORDERLESS;

#ifdef HWRENDER
	if (HWRENDERING)
	{
		sdlhwbackend_t *backend = SDLHW_Current();
		if (backend)
			backend->WindowFlags(&flags);
	}
#endif

#if TARGET_OS_IPHONE
	// iOS requires apps to size their content based on screen coordinates rather than content size.
	flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif

#ifdef MOBILE_PLATFORM
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif

	// Create a window
	window = SDL_CreateWindow("SRB2 Legacy "VERSIONSTRING, realwidth, realheight, flags);

	if (window == NULL)
	{
		CONS_Printf(M_GetText("Couldn't create window: %s\n"), SDL_GetError());
		return false;
	}

	// Text input is per-window in SDL3; carry the current state over to the new window
	I_SetTextInputMode(textinputmode);

	I_SetBorderlessWindow();

	return Impl_CreateContext();
}

/*
static void Impl_SetWindowName(const char *title)
{
	if (window == NULL)
	{
		return;
	}
	SDL_SetWindowTitle(window, title);
}
*/

static void Impl_SetWindowIcon(void)
{
	if (window == NULL || icoSurface == NULL)
	{
		return;
	}
	//SDLSTUB(); // Monster Iestyn: why is this stubbed?
	SDL_SetWindowIcon(window, icoSurface);
}

static void Impl_VideoSetupBuffer(void)
{
	size_t size;
	// Set up game's software render buffer
	//if (rendermode == render_soft)
	{
		vid.rowbytes = vid.width * vid.bpp;
		vid.direct = NULL;
		size = vid.rowbytes*vid.height * NUMSCREENS;
		vid.buffer = calloc(size, NUMSCREENS);
		if (!vid.buffer)
		{
			I_Error("%s", M_GetText("Not enough memory for video buffer\n"));
		}
	}
}

void I_StartupGraphics(void)
{
	if (dedicated)
	{
		rendermode = render_none;
		return;
	}
	if (graphics_started)
		return;

	COM_AddCommand ("vid_nummodes", NULL, VID_Command_NumModes_f);
	COM_AddCommand ("vid_info", NULL, VID_Command_Info_f);
	COM_AddCommand ("vid_modelist", NULL, VID_Command_ModeList_f);
	COM_AddCommand ("vid_mode", NULL, VID_Command_Mode_f);
	CV_RegisterVar (&cv_vidwait);
	CV_RegisterVar (&cv_stretch);
	CV_RegisterVar (&cv_alwaysgrabmouse);
	disable_mouse = M_CheckParm("-nomouse");
	disable_fullscreen = M_CheckParm("-win") ? 1 : 0;

	keyboard_started = true;

#if !defined(HAVE_TTF)
	// Previously audio was init here for questionable reasons?
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
	{
		CONS_Printf(M_GetText("Couldn't initialize SDL's Video System: %s\n"), SDL_GetError());
		return;
	}
#endif
	{
		const char *vd = SDL_GetCurrentVideoDriver();
		//CONS_Printf(M_GetText("Starting up with video driver: %s\n"), vd);
		if (vd && (
			strncasecmp(vd, "gcvideo", 8) == 0 ||
			strncasecmp(vd, "fbcon", 6) == 0 ||
			strncasecmp(vd, "wii", 4) == 0 ||
			strncasecmp(vd, "psl1ght", 8) == 0
		))
			framebuffer = true;
	}

#ifdef HWRENDER_OPENGL
	if (M_CheckParm("-opengl"))
		chosenrendermode = rendermode = render_opengl;
	else
#endif
#ifdef HWRENDER_SDLGPU
	if (M_CheckParm("-sdlgpu"))
		chosenrendermode = rendermode = render_sdlgpu;
	else
#endif
#ifdef HWRENDER
	if (M_CheckParm("-software"))
#endif
		chosenrendermode = rendermode = render_soft;

	usesoftrenderer = M_CheckParm("-softblit");
	borderlesswindow = M_CheckParm("-borderless");

	//SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY>>1,SDL_DEFAULT_REPEAT_INTERVAL<<2);
	//VID_Command_ModeList_f();
#ifdef HWRENDER
	if (M_CheckParm("-nogl"))
	{
		// Don't startup OpenGL
		sdlhwbackend_t *glbackend = SDLHW_GetBackend(render_opengl);
		if (glbackend)
			glbackend->state = VID_GL_LIBRARY_ERROR;
		if (chosenrendermode == render_opengl)
			chosenrendermode = rendermode = render_soft;
		vid.glstate = VID_GL_LIBRARY_ERROR;
	}
	if (RENDERMODE_IS_HARDWARE(chosenrendermode))
		VID_StartupHardware(chosenrendermode);
#endif
	// Fury: we do window initialization after GL setup to allow
	// SDL_GL_LoadLibrary to work well on Windows

	// Create window
	//Impl_CreateWindow(USE_FULLSCREEN);
	//Impl_SetWindowName("SRB2 "VERSIONSTRING);
	VID_SetMode(VID_GetModeForSize(BASEVIDWIDTH, BASEVIDHEIGHT));

	vid.width = BASEVIDWIDTH; // Default size for startup
	vid.height = BASEVIDHEIGHT; // BitsPerPixel is the SDL interface's
	vid.recalc = true; // Set up the console stufff
	vid.direct = NULL; // Maybe direct access?
	vid.bpp = 1; // This is the game engine's Bpp
	vid.WndParent = NULL; //For the window?

#ifdef HAVE_TTF
	I_ShutdownTTF();
#endif
	// Window icon
#ifdef HAVE_IMAGE
	icoSurface = IMG_ReadXPMFromArray(SDL_icon_xpm);
#endif
	Impl_SetWindowIcon();

#ifdef __EMSCRIPTEN__
	VID_SetMode(VID_GetModeForSize(BASEVIDWIDTH*2, BASEVIDHEIGHT*2));
#else
	VID_SetMode(VID_GetModeForSize(BASEVIDWIDTH, BASEVIDHEIGHT));
#endif

	if (M_CheckParm("-nomousegrab"))
		mousegrabok = false;
#if 0 // defined (_DEBUG)
	else
	{
		char videodriver[4] = {'S','D','L',0};
		if (!M_CheckParm("-mousegrab") &&
		    *strncpy(videodriver, SDL_GetCurrentVideoDriver(), 4) != '\0' &&
		    strncasecmp("x11",videodriver,4) == 0)
			mousegrabok = false; //X11's XGrabPointer not good
	}
#endif
	realwidth = (Uint16)vid.width;
	realheight = (Uint16)vid.height;

	//VID_Command_Info_f();
	SDLdoUngrabMouse();

	SDL_RaiseWindow(window);

	if (mousegrabok && !disable_mouse)
		SDLdoGrabMouse();

	graphics_started = true;
}

INT32 VID_HardwareState(rendermode_t mode)
{
#ifdef HWRENDER
	const sdlhwbackend_t *backend = SDLHW_GetBackend(mode);
	if (backend)
		return backend->state;
#else
	(void)mode;
#endif
	return -1; // VID_GL_LIBRARY_ERROR
}

const char *VID_HardwareName(rendermode_t mode)
{
#ifdef HWRENDER
	const sdlhwbackend_t *backend = SDLHW_GetBackend(mode);
	if (backend)
		return backend->name;
#else
	(void)mode;
#endif
	return "Hardware";
}

void VID_StartupHardware(rendermode_t mode)
{
#ifdef HWRENDER
	sdlhwbackend_t *backend = SDLHW_GetBackend(mode);

	if (!backend || backend->state == VID_GL_LIBRARY_ERROR)
		return;

	if (backend->state == VID_GL_LIBRARY_NOTLOADED)
	{
		struct hwdriver_s driver;

		CONS_Printf("VID_StartupHardware(%s)...\n", backend->name);

		memset(&driver, 0, sizeof (driver));
		backend->FillDriver(&driver);

		// let load the library
		backend->state = (driver.pfnInit && driver.pfnInit()) ? VID_GL_LIBRARY_LOADED : VID_GL_LIBRARY_ERROR;

		if (backend->state == VID_GL_LIBRARY_ERROR)
		{
			// Whatever the driver was bound to is still good.
			if (vid.glstate == VID_GL_LIBRARY_NOTLOADED)
				vid.glstate = VID_GL_LIBRARY_ERROR;
			rendermode = render_soft;
			setrenderneeded = 0;
			return;
		}
	}

	// Bind the hardware driver to this backend.
	// Only the table is replaced, so this is cheap, and it's what makes switching between backends possible.
	backend->FillDriver(&HWD);
	vid.glstate = VID_GL_LIBRARY_LOADED;
#else
	(void)mode;
#endif
}

void I_ShutdownGraphics(void)
{
	const rendermode_t oldrendermode = rendermode;

	rendermode = render_none;
	if (icoSurface) SDL_DestroySurface(icoSurface);
	icoSurface = NULL;
	if (oldrendermode == render_soft)
	{
		if (vidSurface) SDL_DestroySurface(vidSurface);
		vidSurface = NULL;
		if (vid.buffer) free(vid.buffer);
		vid.buffer = NULL;
	}

	I_OutputMsg("I_ShutdownGraphics(): ");

	// was graphics initialized anyway?
	if (!graphics_started)
	{
		I_OutputMsg("graphics never started\n");
		return;
	}
	graphics_started = false;
	I_OutputMsg("shut down\n");

#ifdef HWRENDER
	SDLHW_DestroyContexts();
#endif
	SDL_QuitSubSystem(SDL_INIT_VIDEO);
	framebuffer = false;
}

UINT32 I_GetRefreshRate(void)
{
	// Moved to VID_GetRefreshRate.
	// Precalculating it like that won't work as
	// well for windowed mode since you can drag
	// the window around, but very slow PCs might have
	// trouble querying mode over and over again.
	return refresh_rate;
}

static void Impl_SetVsync(void)
{
	if (renderer && !usesoftrenderer)
		SDL_SetRenderVSync(renderer, cv_vidwait.value ? 1 : 0);
}

void I_SetBorderlessWindow(void)
{
	bool bordered = (cv_fullscreen.value == 2) ? false : true;
	if (window)
		SDL_SetWindowBordered(window, bordered);
}

void I_SetTextInputMode(boolean active)
{
	textinputmode = active;

	// SDL3 ties text input to a window; without one the state is applied on creation
	if (window == NULL)
		return;

	if (active)
		SDL_StartTextInput(window);
	else
		SDL_StopTextInput(window);
}

boolean I_GetTextInputMode(void)
{
	if (window == NULL)
		return textinputmode;

	return SDL_TextInputActive(window);
}

#endif
