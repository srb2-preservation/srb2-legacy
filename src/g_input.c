// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  g_input.c
/// \brief handle mouse/keyboard/gamepad inputs,
///        maps inputs to game controls (forward, spin, jump...)

#include "doomdef.h"
#include "doomstat.h"
#include "g_game.h"
#include "g_input.h"
#include "i_gamepad.h"
#include "keys.h"
#include "hu_stuff.h" // need HUFONT start & end
#include "d_net.h"
#include "console.h"

#define MAXMOUSESENSITIVITY 100 // sensitivity steps

static CV_PossibleValue_t mousesens_cons_t[] = {{1, "MIN"}, {MAXMOUSESENSITIVITY, "MAX"}, {0, NULL}};
static CV_PossibleValue_t onecontrolperkey_cons_t[] = {{1, "One"}, {2, "Several"}, {0, NULL}};

// mouse values are used once
consvar_t cv_mousesens = CVAR_INIT ("mousesens", "20", NULL, CV_SAVE, mousesens_cons_t, NULL);
consvar_t cv_mousesens2 = CVAR_INIT ("mousesens2", "20", NULL, CV_SAVE, mousesens_cons_t, NULL);
consvar_t cv_mouseysens = CVAR_INIT ("mouseysens", "20", NULL, CV_SAVE, mousesens_cons_t, NULL);
consvar_t cv_mouseysens2 = CVAR_INIT ("mouseysens2", "20", NULL, CV_SAVE, mousesens_cons_t, NULL);
consvar_t cv_controlperkey = CVAR_INIT ("controlperkey", "One", "The amount of keybinds that an action can be assigned to", CV_SAVE, onecontrolperkey_cons_t, NULL);

INT32 mousex, mousey;
INT32 mlooky; // like mousey but with a custom sensitivity for mlook

INT32 mouse2x, mouse2y, mlook2y;

gamepad_t gamepads[NUM_GAMEPADS];

// current state of the keys: true if pushed
UINT8 gamekeydown[NUMINPUTS];

// two key codes (or virtual key) per game control
INT32 gamecontrol[num_gamecontrols][2];
INT32 gamecontrolbis[num_gamecontrols][2]; // secondary splitscreen player

// Axes don't drive the game while the menu, console or chat has the focus
static boolean GamepadAxesAreBlocked(void)
{
	return menuactive || CON_Ready() || chat_on;
}

static boolean CheckInputDown(UINT8 which, gamecontrols_e gc, boolean checkaxes)
{
	INT32 (*controls)[2] = which == 0 ? gamecontrol : gamecontrolbis;
	UINT8 i;

	for (i = 0; i < 2; i++)
	{
		INT32 key = controls[gc][i];

		if (key >= KEY_GAMEPAD && key < KEY_AXES)
		{
			if (gamepads[which].buttons[key - KEY_GAMEPAD])
				return true;
		}
		else if (key >= KEY_AXES && key < KEY_INV_AXES + NUM_GAMEPAD_AXES)
		{
			if (checkaxes && !GamepadAxesAreBlocked())
			{
				const UINT16 jdeadzone = G_GetGamepadDigitalDeadZone(which);
				const INT16 value = G_GetGamepadAxisValue(which, (key - KEY_AXES) % NUM_GAMEPAD_AXES);

				if (key < KEY_INV_AXES ? value > jdeadzone : value < -jdeadzone)
					return true;
			}
		}
		else if (gamekeydown[key])
			return true;
	}

	return false;
}

boolean G_PlayerInputDown(UINT8 which, gamecontrols_e gc)
{
	return CheckInputDown(which, gc, true);
}

boolean G_CheckDigitalPlayerInput(UINT8 which, gamecontrols_e gc)
{
	return CheckInputDown(which, gc, false);
}

SINT8 G_PlayerInputIsAnalog(UINT8 which, gamecontrols_e gc, UINT8 settings)
{
	INT32 (*controls)[2] = which == 0 ? gamecontrol : gamecontrolbis;
	INT32 key = controls[gc][settings];

	if (key >= KEY_AXES && key < KEY_AXES + NUM_GAMEPAD_AXES)
		return 1;
	else if (key >= KEY_INV_AXES && key < KEY_INV_AXES + NUM_GAMEPAD_AXES)
		return -1;

	return 0;
}

INT16 G_GetAnalogPlayerInput(UINT8 which, gamecontrols_e gc, UINT8 settings)
{
	INT32 (*controls)[2] = which == 0 ? gamecontrol : gamecontrolbis;
	INT32 key = controls[gc][settings];

	if (key >= KEY_AXES && key < KEY_INV_AXES + NUM_GAMEPAD_AXES && !GamepadAxesAreBlocked())
		return G_GetGamepadAxisValue(which, (key - KEY_AXES) % NUM_GAMEPAD_AXES);

	return 0;
}

typedef struct
{
	UINT8 time;
	UINT8 state;
	UINT8 clicks;
} dclick_t;
static dclick_t mousedclicks[MOUSEBUTTONS];
static dclick_t mouse2dclicks[MOUSEBUTTONS];

// protos
static UINT8 G_CheckDoubleClick(UINT8 state, dclick_t *dt);

//
// Remaps the inputs to game controls.
//
// A game control can be triggered by one or more keys/buttons.
//
// Each key/mouse button/gamepad button triggers ONLY ONE game control.
//
void G_MapEventsToControls(event_t *ev)
{
	INT32 i;
	UINT8 flag;

	switch (ev->type)
	{
		case ev_keydown:
			if (ev->data1 < NUMINPUTS)
				gamekeydown[ev->data1] = 1;
#ifdef PARANOIA
			else
			{
				CONS_Debug(DBG_GAMELOGIC, "Bad downkey input %d\n",ev->data1);
			}

#endif
			break;

		case ev_keyup:
			if (ev->data1 < NUMINPUTS)
				gamekeydown[ev->data1] = 0;
#ifdef PARANOIA
			else
			{
				CONS_Debug(DBG_GAMELOGIC, "Bad upkey input %d\n",ev->data1);
			}
#endif
			break;

		case ev_mouse: // buttons are virtual keys
			if (menuactive || CON_Ready() || chat_on)
				break;
			mousex = (INT32)(ev->data2*((cv_mousesens.value*cv_mousesens.value)/110.0f + 0.1f));
			mousey = (INT32)(ev->data3*((cv_mousesens.value*cv_mousesens.value)/110.0f + 0.1f));
			mlooky = (INT32)(ev->data3*((cv_mouseysens.value*cv_mousesens.value)/110.0f + 0.1f));
			break;

		case ev_gamepad_down:
		case ev_gamepad_up:
			if (ev->which < NUM_GAMEPADS)
				gamepads[ev->which].buttons[ev->data1] = ev->type == ev_gamepad_down ? 1 : 0;
			break;

		case ev_gamepad_axis:
			if (ev->which < NUM_GAMEPADS)
				gamepads[ev->which].axes[ev->data1] = ev->data2;
			break;

		case ev_mouse2: // buttons are virtual keys
			if (menuactive || CON_Ready() || chat_on)
				break;
			mouse2x = (INT32)(ev->data2*((cv_mousesens2.value*cv_mousesens2.value)/110.0f + 0.1f));
			mouse2y = (INT32)(ev->data3*((cv_mousesens2.value*cv_mousesens2.value)/110.0f + 0.1f));
			mlook2y = (INT32)(ev->data3*((cv_mouseysens2.value*cv_mousesens2.value)/110.0f + 0.1f));
			break;

		default:
			break;
	}

	// ALWAYS check for mouse double-clicks even if there were no such events
	for (i = 0; i < MOUSEBUTTONS; i++)
	{
		flag = G_CheckDoubleClick(gamekeydown[KEY_MOUSE1+i], &mousedclicks[i]);
		gamekeydown[KEY_DBLMOUSE1+i] = flag;
	}

	for (i = 0; i < MOUSEBUTTONS; i++)
	{
		flag = G_CheckDoubleClick(gamekeydown[KEY_2MOUSE1+i], &mouse2dclicks[i]);
		gamekeydown[KEY_DBL2MOUSE1+i] = flag;
	}
}

//
// General double-click detection routine for any kind of input.
//
static UINT8 G_CheckDoubleClick(UINT8 state, dclick_t *dt)
{
	if (state != dt->state && dt->time > 1)
	{
		dt->state = state;
		if (state)
			dt->clicks++;
		if (dt->clicks == 2)
		{
			dt->clicks = 0;
			return true;
		}
		else
			dt->time = 0;
	}
	else
	{
		dt->time++;
		if (dt->time > 20)
		{
			dt->clicks = 0;
			dt->state = 0;
		}
	}
	return false;
}

const char *const gamepad_button_names[NUM_GAMEPAD_BUTTONS + 1] = {
	"a",
	"b",
	"x",
	"y",
	"back",
	"guide",
	"start",
	"left-stick",
	"right-stick",
	"left-shoulder",
	"right-shoulder",
	"dpad-up",
	"dpad-down",
	"dpad-left",
	"dpad-right",
	"misc1",
	"paddle1",
	"paddle2",
	"paddle3",
	"paddle4",
	"touchpad",
	NULL};

const char *const gamepad_axis_names[NUM_GAMEPAD_AXES + 1] = {
	"left-x",
	"left-y",
	"right-x",
	"right-y",
	"trigger-left",
	"trigger-right",
	NULL};

boolean G_GamepadTypeIsXbox(gamepadtype_e type)
{
	switch (type)
	{
	case GAMEPAD_TYPE_XBOX360:
	case GAMEPAD_TYPE_XBOXONE:
	case GAMEPAD_TYPE_XBOX_SERIES_XS:
	case GAMEPAD_TYPE_XBOX_ELITE:
		return true;
	default:
		return false;
	}
}

boolean G_GamepadTypeIsPlayStation(gamepadtype_e type)
{
	switch (type)
	{
	case GAMEPAD_TYPE_PS3:
	case GAMEPAD_TYPE_PS4:
	case GAMEPAD_TYPE_PS5:
		return true;
	default:
		return false;
	}
}

boolean G_GamepadTypeIsNintendoSwitch(gamepadtype_e type)
{
	switch (type)
	{
	case GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
	case GAMEPAD_TYPE_NINTENDO_SWITCH_JOY_CON_GRIP:
		return true;
	default:
		return G_GamepadTypeIsJoyCon(type);
	}
}

boolean G_GamepadTypeIsJoyCon(gamepadtype_e type)
{
	switch (type)
	{
	case GAMEPAD_TYPE_NINTENDO_SWITCH_JOY_CON_LEFT:
	case GAMEPAD_TYPE_NINTENDO_SWITCH_JOY_CON_RIGHT:
		return true;
	default:
		return false;
	}
}

boolean G_RumbleSupported(UINT8 which)
{
	if (!I_RumbleSupported() || which >= NUM_GAMEPADS)
		return 0;

	return I_GetGamepadRumbleSupported(which);
}

boolean G_RumbleGamepad(UINT8 which, fixed_t large_magnitude, fixed_t small_magnitude, tic_t duration)
{
	haptic_t effect;

	if (!G_RumbleSupported(which))
		return false;

	effect.large_magnitude = large_magnitude;
	effect.small_magnitude = small_magnitude;
	effect.duration = duration;

	return I_RumbleGamepad(which, &effect);
}

void G_StopGamepadRumble(UINT8 which)
{
	if (G_RumbleSupported(which))
		I_StopGamepadRumble(which);
}

fixed_t G_GetLargeMotorFreq(UINT8 which)
{
	if (!G_RumbleSupported(which) || which >= NUM_GAMEPADS)
		return 0;

	gamepad_t *gamepad = &gamepads[which];
	return gamepad->rumble.data.large_magnitude;
}

fixed_t G_GetSmallMotorFreq(UINT8 which)
{
	if (!G_RumbleSupported(which) || which >= NUM_GAMEPADS)
		return 0;

	gamepad_t *gamepad = &gamepads[which];
	return gamepad->rumble.data.small_magnitude;
}

boolean G_GetGamepadRumblePaused(UINT8 which)
{
	return I_GetGamepadRumblePaused(which);
}

boolean G_SetLargeMotorFreq(UINT8 which, fixed_t freq)
{
	return I_SetGamepadLargeMotorFreq(which, freq);
}

boolean G_SetSmallMotorFreq(UINT8 which, fixed_t freq)
{
	return I_SetGamepadSmallMotorFreq(which, freq);
}

void G_SetGamepadRumblePaused(UINT8 which, boolean pause)
{
	if (G_RumbleSupported(which))
		I_SetGamepadRumblePaused(which, pause);
}

// Obtains the value of an axis, and makes it digital if needed
INT16 G_GamepadAxisEventValue(UINT8 which, INT16 value)
{
	gamepad_t *gamepad = &gamepads[which];

	if (gamepad->digital)
	{
		const UINT16 jdeadzone = G_GetGamepadDigitalDeadZone(which);

		if (value < -jdeadzone)
			value = -JOYAXISRANGE - 1;
		else if (value > jdeadzone)
			value = JOYAXISRANGE;
		else
			value = 0;
	}

	return value;
}

INT16 G_GetGamepadAxisValue(UINT8 which, gamepad_axis_e axis)
{
	gamepad_t *gamepad = &gamepads[which];

	if (axis >= NUM_GAMEPAD_AXES)
		return 0;

	return G_GamepadAxisEventValue(which, gamepad->axes[axis]);
}

fixed_t G_GetAdjustedGamepadAxis(UINT8 which, gamepad_axis_e axis, boolean applyDeadzone)
{
	gamepad_t *gamepad = &gamepads[which];

	if (axis >= NUM_GAMEPAD_AXES)
		return 0;

	INT32 value = gamepad->axes[axis];

	if (applyDeadzone && gamepad->digital)
	{
		INT16 deadzone = G_GetGamepadDigitalDeadZone(which);

		if (value < -deadzone)
			value = -JOYAXISRANGE;
		else if (value > deadzone)
			value = JOYAXISRANGE;
		else
			value = 0;
	}
	else if (applyDeadzone)
	{
		INT32 sign = value < 0 ? -1 : 1;
		INT16 deadzone = G_GetGamepadDeadZone(which);
		INT32 magnitude = value * value;
		INT32 nAxis = magnitude / JOYAXISRANGE;
		INT32 nMagnitude = G_BasicDeadZoneCalculation(magnitude, deadzone);

		value = (nAxis * nMagnitude) / JOYAXISRANGE;
		value = min(value * sign, JOYAXISRANGE);
		value = max(value, -JOYAXISRANGE);
	}

	return (value / 32767.0) * FRACUNIT;
}

static UINT16 CalcGamepadDeadZone(fixed_t deadzone)
{
	INT32 value = (JOYAXISRANGE * deadzone) / FRACUNIT;

	if (value < 0)
		value = 0;
	else if (value > JOYAXISRANGE)
		value = JOYAXISRANGE;

	return value;
}

UINT16 G_GetGamepadDeadZone(UINT8 which)
{
	return CalcGamepadDeadZone(cv_deadzone[which].value);
}

UINT16 G_GetGamepadDigitalDeadZone(UINT8 which)
{
	return CalcGamepadDeadZone(cv_digitaldeadzone[which].value);
}

// Take a magnitude of two axes, and adjust it to take out the deadzone
// Will return a value between 0 and JOYAXISRANGE
INT32 G_BasicDeadZoneCalculation(INT32 magnitude, const UINT16 jdeadzone)
{
	INT32 deadzoneAppliedValue = 0;
	INT32 adjustedMagnitude = abs(magnitude);

	if (jdeadzone >= JOYAXISRANGE && adjustedMagnitude >= JOYAXISRANGE) // If the deadzone and magnitude are both 100%...
		return JOYAXISRANGE; // ...return 100% input directly, to avoid dividing by 0
	else if (adjustedMagnitude > jdeadzone) // Otherwise, calculate how much the magnitude exceeds the deadzone
	{
		adjustedMagnitude = min(adjustedMagnitude, JOYAXISRANGE);

		adjustedMagnitude -= jdeadzone;

		deadzoneAppliedValue = (adjustedMagnitude * JOYAXISRANGE) / (JOYAXISRANGE - jdeadzone);
	}

	return deadzoneAppliedValue;
}

INT32 G_RemapGamepadEvent(event_t *event, INT32 *type)
{
	if (event->type == ev_gamepad_down)
	{
		*type = ev_keydown;
		return KEY_GAMEPAD + event->data1;
	}
	else if (event->type == ev_gamepad_up)
	{
		*type = ev_keyup;
		return KEY_GAMEPAD + event->data1;
	}
	else if (event->type == ev_gamepad_axis)
	{
		const UINT16 jdeadzone = G_GetGamepadDigitalDeadZone(event->which);
		const INT16 value = G_GetGamepadAxisValue(event->which, event->data1);

		if (value < -jdeadzone || value > jdeadzone)
			*type = ev_keydown;
		else
			*type = ev_keyup;

		if (value < -jdeadzone)
			return KEY_INV_AXES + event->data1;
		else
			return KEY_AXES + event->data1;
	}

	return event->data1;
}

typedef struct
{
	const char *name;
	const char *menu1;
	const char *menu2;
} button_strings_t;

#define DEF_NAME_BUTTON(str) {.name = str, .menu1 = str " Button", .menu2 = "the " str " Button"}
#define DEF_NAME_SIMPLE(str) {.name = str, .menu1 = NULL, .menu2 = "the " str}
#define DEF_NAME_DPAD(a, b)  {.name = "D-Pad " a, .menu1 = "D-Pad " b, .menu2 = a}

#define PARTIAL_DEF_START [GAMEPAD_BUTTON_A] = { NULL }
#define PARTIAL_DEF_END [NUM_GAMEPAD_BUTTONS - 1] = { NULL }

static const char *GetStringFromButtonList(const button_strings_t *names, gamepad_button_e button, gamepad_string_e type)
{
	switch (type)
	{
	case GAMEPAD_STRING_DEFAULT:
		return names[button].name;
	case GAMEPAD_STRING_MENU1:
		if (names[button].menu1)
			return names[button].menu1;
		else
			return names[button].name;
	case GAMEPAD_STRING_MENU2:
		if (names[button].menu2)
			return names[button].menu2;
		else
			return names[button].name;
	}

	return NULL;
}

const char *G_GetGamepadButtonString(gamepadtype_e type, gamepad_button_e button, gamepad_string_e strtype)
{
	static const button_strings_t base_names[] = {
		[GAMEPAD_BUTTON_A]             = DEF_NAME_BUTTON("A"),
		[GAMEPAD_BUTTON_B]             = DEF_NAME_BUTTON("B"),
		[GAMEPAD_BUTTON_X]             = DEF_NAME_BUTTON("X"),
		[GAMEPAD_BUTTON_Y]             = DEF_NAME_BUTTON("Y"),
		[GAMEPAD_BUTTON_BACK]          = DEF_NAME_BUTTON("Back"),
		[GAMEPAD_BUTTON_GUIDE]         = DEF_NAME_BUTTON("Guide"),
		[GAMEPAD_BUTTON_START]         = DEF_NAME_BUTTON("Start"),
		[GAMEPAD_BUTTON_LEFTSTICK]     = DEF_NAME_SIMPLE("Left Stick"),
		[GAMEPAD_BUTTON_RIGHTSTICK]    = DEF_NAME_SIMPLE("Right Stick"),
		[GAMEPAD_BUTTON_LEFTSHOULDER]  = DEF_NAME_SIMPLE("Left Shoulder"),
		[GAMEPAD_BUTTON_RIGHTSHOULDER] = DEF_NAME_SIMPLE("Right Shoulder"),
		[GAMEPAD_BUTTON_DPAD_UP]       = DEF_NAME_DPAD("Up", "\x1A"),
		[GAMEPAD_BUTTON_DPAD_DOWN]     = DEF_NAME_DPAD("Down", "\x1B"),
		[GAMEPAD_BUTTON_DPAD_LEFT]     = DEF_NAME_DPAD("Left", "\x1C"),
		[GAMEPAD_BUTTON_DPAD_RIGHT]    = DEF_NAME_DPAD("Right", "\x1D"),
		[GAMEPAD_BUTTON_PADDLE1]       = DEF_NAME_SIMPLE("Paddle 1"),
		[GAMEPAD_BUTTON_PADDLE2]       = DEF_NAME_SIMPLE("Paddle 2"),
		[GAMEPAD_BUTTON_PADDLE3]       = DEF_NAME_SIMPLE("Paddle 3"),
		[GAMEPAD_BUTTON_PADDLE4]       = DEF_NAME_SIMPLE("Paddle 4"),
		[GAMEPAD_BUTTON_TOUCHPAD]      = DEF_NAME_SIMPLE("Touchpad"),

		// This one's a bit weird
		// Suffix the numbers in the event SDL adds more misc buttons
		[GAMEPAD_BUTTON_MISC1] = {
			.name  = "Misc. Button",
			.menu1 = "Gamepad Misc.",
			.menu2 = "the Misc. Button"
		},
	};

	button_strings_t const *names = NULL;

	if (G_GamepadTypeIsXbox(type))
	{
		#define BASE_XBOX_NAMES \
			[GAMEPAD_BUTTON_LEFTSHOULDER]  = DEF_NAME_SIMPLE("Left Bumper"), \
			[GAMEPAD_BUTTON_RIGHTSHOULDER] = DEF_NAME_SIMPLE("Right Bumper")

		static const button_strings_t xbox_names[] = { PARTIAL_DEF_START,
			BASE_XBOX_NAMES,
		PARTIAL_DEF_END };

		static const button_strings_t series_xs_names[] = { PARTIAL_DEF_START,
			BASE_XBOX_NAMES,
			[GAMEPAD_BUTTON_MISC1]         = DEF_NAME_BUTTON("Share"),
		PARTIAL_DEF_END };

		static const button_strings_t elite_names[] = { PARTIAL_DEF_START,
			BASE_XBOX_NAMES,
			[GAMEPAD_BUTTON_PADDLE1]       = DEF_NAME_SIMPLE("P1 Paddle"),
			[GAMEPAD_BUTTON_PADDLE2]       = DEF_NAME_SIMPLE("P2 Paddle"),
			[GAMEPAD_BUTTON_PADDLE3]       = DEF_NAME_SIMPLE("P3 Paddle"),
			[GAMEPAD_BUTTON_PADDLE4]       = DEF_NAME_SIMPLE("P4 Paddle"),
		PARTIAL_DEF_END };

		if (type == GAMEPAD_TYPE_XBOX_SERIES_XS) // X|S controllers have a Share button
			names = series_xs_names;
		else if (type == GAMEPAD_TYPE_XBOX_ELITE) // Elite controller has paddles
			names = elite_names;
		else
			names = xbox_names;

		#undef BASE_XBOX_NAMES
	}
	else if (G_GamepadTypeIsPlayStation(type))
	{
		#define BASE_PS_NAMES \
			[GAMEPAD_BUTTON_A]             = DEF_NAME_BUTTON("Cross"), \
			[GAMEPAD_BUTTON_B]             = DEF_NAME_BUTTON("Circle"), \
			[GAMEPAD_BUTTON_X]             = DEF_NAME_BUTTON("Square"), \
			[GAMEPAD_BUTTON_Y]             = DEF_NAME_BUTTON("Triangle"), \
			[GAMEPAD_BUTTON_BACK]          = DEF_NAME_BUTTON("Select"), \
			[GAMEPAD_BUTTON_GUIDE]         = DEF_NAME_BUTTON("PS"), \
			[GAMEPAD_BUTTON_LEFTSTICK]     = DEF_NAME_BUTTON("L3"), \
			[GAMEPAD_BUTTON_RIGHTSTICK]    = DEF_NAME_BUTTON("R3"), \
			[GAMEPAD_BUTTON_LEFTSHOULDER]  = DEF_NAME_BUTTON("L1"), \
			[GAMEPAD_BUTTON_RIGHTSHOULDER] = DEF_NAME_BUTTON("R1")

		static const button_strings_t ps_names[] = {
			BASE_PS_NAMES,
		PARTIAL_DEF_END };

		static const button_strings_t ps5_names[] = {
			BASE_PS_NAMES,
			[GAMEPAD_BUTTON_MISC1] = DEF_NAME_BUTTON("Microphone"),
		PARTIAL_DEF_END };

		names = type == GAMEPAD_TYPE_PS5 ? ps5_names : ps_names;
		#undef BASE_PS_NAMES
	}
	else if (G_GamepadTypeIsNintendoSwitch(type))
	{
		static const button_strings_t switch_names[] = { PARTIAL_DEF_START,
			[GAMEPAD_BUTTON_BACK]          = DEF_NAME_BUTTON("-"),
			[GAMEPAD_BUTTON_GUIDE]         = DEF_NAME_BUTTON("HOME"),
			[GAMEPAD_BUTTON_START]         = DEF_NAME_BUTTON("+"),
			[GAMEPAD_BUTTON_LEFTSHOULDER]  = DEF_NAME_BUTTON("L"),
			[GAMEPAD_BUTTON_RIGHTSHOULDER] = DEF_NAME_BUTTON("R"),
			[GAMEPAD_BUTTON_MISC1]         = DEF_NAME_BUTTON("Capture"),
		PARTIAL_DEF_END };

		names = switch_names;
	}
	else if (type == GAMEPAD_TYPE_AMAZON_LUNA)
	{
		static const button_strings_t luna_names[] = { PARTIAL_DEF_START,
			[GAMEPAD_BUTTON_MISC1] = DEF_NAME_BUTTON("Microphone"),
		PARTIAL_DEF_END };

		names = luna_names;
	}

	const char *str = NULL;

	if (names)
		str = GetStringFromButtonList(names, button, strtype);
	if (str == NULL)
		str = GetStringFromButtonList(base_names, button, strtype);
	if (str)
		return str;

	return "Unknown";
}

#undef DEF_NAME_BUTTON
#undef DEF_NAME_SIMPLE
#undef DEF_NAME_DPAD

#undef PARTIAL_DEF_START
#undef PARTIAL_DEF_END

typedef struct
{
	const char *name;
	const char *menu1;
	const char *menu2;
	const char *name_inv;
	const char *menu1_inv;
	const char *menu2_inv;
} axis_strings_t;

#define DEF_NAME_AXIS(str, a, inv_a, b, inv_b) {\
	str " " a, str " " b, "the " str " " b, \
	str " " inv_a, str " " inv_b, "the " str " " inv_b}
#define DEF_NAME_TRIGGER(str) {str, NULL, "the " str, NULL, NULL, NULL}
#define DEF_NAME_BUTTON(str) {str, str " Button", "the " str " Button", NULL, NULL, NULL}

#define PARTIAL_DEF_START [GAMEPAD_AXIS_LEFTX] = { NULL }

static const char *GetStringFromAxisList(const axis_strings_t *names, gamepad_axis_e axis, gamepad_string_e type, boolean inv)
{
	switch (type)
	{
	case GAMEPAD_STRING_DEFAULT:
		if (inv && names[axis].name_inv)
			return names[axis].name_inv;
		else
			return names[axis].name;
		break;
	case GAMEPAD_STRING_MENU1:
		if (inv && names[axis].menu1_inv)
			return names[axis].menu1_inv;
		if (names[axis].menu1)
			return names[axis].menu1;
		else
			return names[axis].name;
		break;
	case GAMEPAD_STRING_MENU2:
		if (inv && names[axis].menu2_inv)
			return names[axis].menu2_inv;
		if (names[axis].menu2)
			return names[axis].menu2;
		else
			return names[axis].name;
		break;
	}

	return NULL;
}

const char *G_GetGamepadAxisString(gamepadtype_e type, gamepad_axis_e axis, gamepad_string_e strtype, boolean inv)
{
	static const axis_strings_t base_names[] = {
		[GAMEPAD_AXIS_LEFTX]        = DEF_NAME_AXIS("Left Stick",  "X", "X-", "\x1D", "\x1C"),
		[GAMEPAD_AXIS_LEFTY]        = DEF_NAME_AXIS("Left Stick",  "Y", "Y-", "\x1B", "\x1A"),
		[GAMEPAD_AXIS_RIGHTX]       = DEF_NAME_AXIS("Right Stick", "X", "X-", "\x1D", "\x1C"),
		[GAMEPAD_AXIS_RIGHTY]       = DEF_NAME_AXIS("Right Stick", "Y", "Y-", "\x1B", "\x1A"),
		[GAMEPAD_AXIS_TRIGGERLEFT]  = DEF_NAME_TRIGGER("Left Trigger"),
		[GAMEPAD_AXIS_TRIGGERRIGHT] = DEF_NAME_TRIGGER("Right Trigger")
	};

	axis_strings_t const *names = NULL;

	if (G_GamepadTypeIsPlayStation(type))
	{
		static const axis_strings_t ps_names[] = { PARTIAL_DEF_START,
			[GAMEPAD_AXIS_TRIGGERLEFT]  = DEF_NAME_BUTTON("L2"),
			[GAMEPAD_AXIS_TRIGGERRIGHT] = DEF_NAME_BUTTON("R2"),
		};

		names = ps_names;
	}
	else if (G_GamepadTypeIsNintendoSwitch(type))
	{
		static const axis_strings_t switch_names[] = { PARTIAL_DEF_START,
			[GAMEPAD_AXIS_TRIGGERLEFT]  = DEF_NAME_BUTTON("ZL"),
			[GAMEPAD_AXIS_TRIGGERRIGHT] = DEF_NAME_BUTTON("ZR"),
		};

		names = switch_names;
	}

	const char *str = NULL;

	if (names)
		str = GetStringFromAxisList(names, axis, strtype, inv);
	if (str == NULL)
		str = GetStringFromAxisList(base_names, axis, strtype, inv);
	if (str)
		return str;

	return "Unknown";
}

#undef DEF_NAME_AXIS
#undef DEF_NAME_TRIGGER
#undef DEF_NAME_BUTTON

#undef PARTIAL_DEF_START

typedef struct
{
	INT32 keynum;
	const char *name;
} keyname_t;

static keyname_t keynames[] =
{
	{KEY_SPACE, "SPACE"},
	{KEY_CAPSLOCK, "CAPS LOCK"},
	{KEY_ENTER, "ENTER"},
	{KEY_TAB, "TAB"},
	{KEY_ESCAPE, "ESCAPE"},
	{KEY_BACKSPACE, "BACKSPACE"},

	{KEY_NUMLOCK, "NUMLOCK"},
	{KEY_SCROLLLOCK, "SCROLLLOCK"},

	// bill gates keys
	{KEY_LEFTWIN, "LEFTWIN"},
	{KEY_RIGHTWIN, "RIGHTWIN"},
	{KEY_MENU, "MENU"},

	{KEY_LSHIFT, "LSHIFT"},
	{KEY_RSHIFT, "RSHIFT"},
	{KEY_LSHIFT, "SHIFT"},
	{KEY_LCTRL, "LCTRL"},
	{KEY_RCTRL, "RCTRL"},
	{KEY_LCTRL, "CTRL"},
	{KEY_LALT, "LALT"},
	{KEY_RALT, "RALT"},
	{KEY_LALT, "ALT"},

	// keypad keys
	{KEY_KPADSLASH, "KEYPAD /"},
	{KEY_KEYPAD7, "KEYPAD 7"},
	{KEY_KEYPAD8, "KEYPAD 8"},
	{KEY_KEYPAD9, "KEYPAD 9"},
	{KEY_MINUSPAD, "KEYPAD -"},
	{KEY_KEYPAD4, "KEYPAD 4"},
	{KEY_KEYPAD5, "KEYPAD 5"},
	{KEY_KEYPAD6, "KEYPAD 6"},
	{KEY_PLUSPAD, "KEYPAD +"},
	{KEY_KEYPAD1, "KEYPAD 1"},
	{KEY_KEYPAD2, "KEYPAD 2"},
	{KEY_KEYPAD3, "KEYPAD 3"},
	{KEY_KEYPAD0, "KEYPAD 0"},
	{KEY_KPADDEL, "KEYPAD ."},

	// extended keys (not keypad)
	{KEY_HOME, "HOME"},
	{KEY_UPARROW, "UP ARROW"},
	{KEY_PGUP, "PGUP"},
	{KEY_LEFTARROW, "LEFT ARROW"},
	{KEY_RIGHTARROW, "RIGHT ARROW"},
	{KEY_END, "END"},
	{KEY_DOWNARROW, "DOWN ARROW"},
	{KEY_PGDN, "PGDN"},
	{KEY_INS, "INS"},
	{KEY_DEL, "DEL"},

	// other keys
	{KEY_F1, "F1"},
	{KEY_F2, "F2"},
	{KEY_F3, "F3"},
	{KEY_F4, "F4"},
	{KEY_F5, "F5"},
	{KEY_F6, "F6"},
	{KEY_F7, "F7"},
	{KEY_F8, "F8"},
	{KEY_F9, "F9"},
	{KEY_F10, "F10"},
	{KEY_F11, "F11"},
	{KEY_F12, "F12"},

	// KEY_CONSOLE has an exception in the keyname code
	{'`', "TILDE"},
	{KEY_PAUSE, "PAUSE/BREAK"},

	// virtual keys for mouse buttons and joystick buttons
	{KEY_MOUSE1+0,"MOUSE1"},
	{KEY_MOUSE1+1,"MOUSE2"},
	{KEY_MOUSE1+2,"MOUSE3"},
	{KEY_MOUSE1+3,"MOUSE4"},
	{KEY_MOUSE1+4,"MOUSE5"},
	{KEY_MOUSE1+5,"MOUSE6"},
	{KEY_MOUSE1+6,"MOUSE7"},
	{KEY_MOUSE1+7,"MOUSE8"},
	{KEY_2MOUSE1+0,"SEC_MOUSE2"}, // BP: sorry my mouse handler swap button 1 and 2
	{KEY_2MOUSE1+1,"SEC_MOUSE1"},
	{KEY_2MOUSE1+2,"SEC_MOUSE3"},
	{KEY_2MOUSE1+3,"SEC_MOUSE4"},
	{KEY_2MOUSE1+4,"SEC_MOUSE5"},
	{KEY_2MOUSE1+5,"SEC_MOUSE6"},
	{KEY_2MOUSE1+6,"SEC_MOUSE7"},
	{KEY_2MOUSE1+7,"SEC_MOUSE8"},
	{KEY_MOUSEWHEELUP, "Wheel 1 UP"},
	{KEY_MOUSEWHEELDOWN, "Wheel 1 Down"},
	{KEY_2MOUSEWHEELUP, "Wheel 2 UP"},
	{KEY_2MOUSEWHEELDOWN, "Wheel 2 Down"},

	// we use up to 32 buttons in DirectInput

#define DEF_GAMEPAD_NAME(btn, name) {KEY_GAMEPAD+GAMEPAD_BUTTON_##btn, name}
#define DEF_GAMEPAD_AXIS(ax, name) \
	{KEY_AXES+GAMEPAD_AXIS_##ax, name}, \
	{KEY_INV_AXES+GAMEPAD_AXIS_##ax, name "-"}

	DEF_GAMEPAD_NAME(A, "a button"),
	DEF_GAMEPAD_NAME(B, "b button"),
	DEF_GAMEPAD_NAME(X, "x button"),
	DEF_GAMEPAD_NAME(Y, "y button"),

	DEF_GAMEPAD_NAME(BACK, "back button"),
	DEF_GAMEPAD_NAME(GUIDE, "guide button"),
	DEF_GAMEPAD_NAME(START, "start button"),
	DEF_GAMEPAD_NAME(LEFTSTICK, "left stick"),
	DEF_GAMEPAD_NAME(RIGHTSTICK, "right stick"),

	DEF_GAMEPAD_NAME(LEFTSHOULDER, "left shoulder"),
	DEF_GAMEPAD_NAME(RIGHTSHOULDER, "right shoulder"),

	DEF_GAMEPAD_NAME(DPAD_UP, "d-pad up"),
	DEF_GAMEPAD_NAME(DPAD_DOWN, "d-pad down"),
	DEF_GAMEPAD_NAME(DPAD_LEFT, "d-pad left"),
	DEF_GAMEPAD_NAME(DPAD_RIGHT, "d-pad right"),

	DEF_GAMEPAD_NAME(MISC1, "gamepad misc 1"),
	DEF_GAMEPAD_NAME(PADDLE1, "paddle 1"),
	DEF_GAMEPAD_NAME(PADDLE2, "paddle 2"),
	DEF_GAMEPAD_NAME(PADDLE3, "paddle 3"),
	DEF_GAMEPAD_NAME(PADDLE4, "paddle 4"),
	DEF_GAMEPAD_NAME(TOUCHPAD, "touchpad"),

	DEF_GAMEPAD_AXIS(LEFTX, "left stick x"),
	DEF_GAMEPAD_AXIS(LEFTY, "left stick y"),

	DEF_GAMEPAD_AXIS(RIGHTX, "right stick x"),
	DEF_GAMEPAD_AXIS(RIGHTY, "right stick y"),

	DEF_GAMEPAD_AXIS(TRIGGERLEFT, "left trigger"),
	DEF_GAMEPAD_AXIS(TRIGGERRIGHT, "right trigger"),

#undef DEF_GAMEPAD_NAME
#undef DEF_GAMEPAD_AXIS

	{KEY_DBLMOUSE1+0, "DBLMOUSE1"},
	{KEY_DBLMOUSE1+1, "DBLMOUSE2"},
	{KEY_DBLMOUSE1+2, "DBLMOUSE3"},
	{KEY_DBLMOUSE1+3, "DBLMOUSE4"},
	{KEY_DBLMOUSE1+4, "DBLMOUSE5"},
	{KEY_DBLMOUSE1+5, "DBLMOUSE6"},
	{KEY_DBLMOUSE1+6, "DBLMOUSE7"},
	{KEY_DBLMOUSE1+7, "DBLMOUSE8"},
	{KEY_DBL2MOUSE1+0, "DBLSEC_MOUSE2"}, // BP: sorry my mouse handler swap button 1 and 2
	{KEY_DBL2MOUSE1+1, "DBLSEC_MOUSE1"},
	{KEY_DBL2MOUSE1+2, "DBLSEC_MOUSE3"},
	{KEY_DBL2MOUSE1+3, "DBLSEC_MOUSE4"},
	{KEY_DBL2MOUSE1+4, "DBLSEC_MOUSE5"},
	{KEY_DBL2MOUSE1+5, "DBLSEC_MOUSE6"},
	{KEY_DBL2MOUSE1+6, "DBLSEC_MOUSE7"},
	{KEY_DBL2MOUSE1+7, "DBLSEC_MOUSE8"},


	// we use up to 32 buttons in DirectInput


};

static const char *gamecontrolname[num_gamecontrols] =
{
	"nothing", // a key/button mapped to gc_null has no effect
	"forward",
	"backward",
	"strafeleft",
	"straferight",
	"turnleft",
	"turnright",
	"weaponnext",
	"weaponprev",
	"weapon1",
	"weapon2",
	"weapon3",
	"weapon4",
	"weapon5",
	"weapon6",
	"weapon7",
	"weapon8",
	"weapon9",
	"weapon10",
	"fire",
	"firenormal",
	"tossflag",
	"use",
	"camtoggle",
	"camleft",
	"camright",
	"camreset",
	"lookup",
	"lookdown",
	"centerview",
	"mouseaiming",
	"talkkey",
	"teamtalkkey",
	"scores",
	"jump",
	"console",
	"pause",
	"systemmenu",
	"screenshot",
	"recordgif",
	"viewpoint",
	"custom1",
	"custom2",
	"custom3",
};

#define NUMKEYNAMES (sizeof (keynames)/sizeof (keyname_t))

// Converts the name of a joystick button or hat from before the gamepad refactor.
// Both players used to have their own names, but now they share the gamepad keys.
// The buttons follow the layout of an Xbox controller, which the defaults were made for.
// Anything else, such as double clicks on gamepad buttons, doesn't exist anymore.
static INT32 G_OldGamepadKeyStringtoNum(const char *keystr)
{
	static const INT32 oldbuttons[] = {
		GAMEPAD_KEY(A), GAMEPAD_KEY(B), GAMEPAD_KEY(X), GAMEPAD_KEY(Y),
		GAMEPAD_KEY(LEFTSHOULDER), GAMEPAD_KEY(RIGHTSHOULDER),
		GAMEPAD_KEY(BACK), GAMEPAD_KEY(START),
		GAMEPAD_KEY(LEFTSTICK), GAMEPAD_KEY(RIGHTSTICK)
	};

	if (!strnicmp(keystr, "SEC_", 4))
		keystr += 4;

	if (!strnicmp(keystr, "JOY", 3))
	{
		INT32 button = atoi(&keystr[3]);
		if (button >= 1 && button <= (INT32)(sizeof (oldbuttons) / sizeof (oldbuttons[0])))
			return oldbuttons[button - 1];
	}
	else if (!stricmp(keystr, "HATUP"))
		return GAMEPAD_KEY(DPAD_UP);
	else if (!stricmp(keystr, "HATDOWN"))
		return GAMEPAD_KEY(DPAD_DOWN);
	else if (!stricmp(keystr, "HATLEFT"))
		return GAMEPAD_KEY(DPAD_LEFT);
	else if (!stricmp(keystr, "HATRIGHT"))
		return GAMEPAD_KEY(DPAD_RIGHT);

	return 0;
}

//
// Detach any keys associated to the given game control
// - pass the pointer to the gamecontrol table for the player being edited
void G_ClearControlKeys(INT32 (*setupcontrols)[2], INT32 control)
{
	setupcontrols[control][0] = KEY_NULL;
	setupcontrols[control][1] = KEY_NULL;
}

void G_ClearAllControlKeys(void)
{
	INT32 i;
	for (i = 0; i < num_gamecontrols; i++)
	{
		G_ClearControlKeys(gamecontrol, i);
		G_ClearControlKeys(gamecontrolbis, i);
	}
}

//
// Returns the name of a key (or virtual key for mouse and gamepad)
// the input value being an keynum
//
const char *G_KeynumToString(INT32 keynum)
{
	static char keynamestr[8];

	UINT32 j;

	// return a string with the ascii char if displayable
	if (keynum > ' ' && keynum <= 'z' && keynum != KEY_CONSOLE)
	{
		keynamestr[0] = (char)keynum;
		keynamestr[1] = '\0';
		return keynamestr;
	}

	// find a description for special keys
	for (j = 0; j < NUMKEYNAMES; j++)
		if (keynames[j].keynum == keynum)
			return keynames[j].name;

	// create a name for unknown keys
	sprintf(keynamestr, "KEY%d", keynum);
	return keynamestr;
}

INT32 G_KeyStringtoNum(const char *keystr)
{
	UINT32 j;

	if (keystr[0] > ' ' && keystr[0] <= 'z' && !keystr[1])
		return keystr[0];

	if (!strncmp(keystr, "KEY", 3) && keystr[3] >= '0' && keystr[3] <= '9')
		return atoi(&keystr[3]);

	for (j = 0; j < NUMKEYNAMES; j++)
		if (!stricmp(keynames[j].name, keystr))
			return keynames[j].keynum;

	return G_OldGamepadKeyStringtoNum(keystr);
}

void G_InitGamepads(void)
{
	UINT8 i;

	for (i = 0; i < NUM_GAMEPADS; i++)
		gamepads[i].num = i;
}

const char *G_GamepadTypeToString(gamepadtype_e type)
{
	static const char *names[] = {
		"unknown",
		"xbox-360",
		"xbox-one",
		"xbox-series-xs",
		"xbox-elite",
		"ps3",
		"ps4",
		"ps5",
		"switch-pro",
		"switch-joy-con-grip",
		"switch-joy-con-left",
		"switch-joy-con-right",
		"stadia",
		"amazon-luna",
		"steam-controller",
		"virtual"
	};

	return names[type];
}

void G_Controldefault(void)
{
	gamecontrol[gc_forward    ][0] = 'w';
	gamecontrol[gc_backward   ][0] = 's';
	gamecontrol[gc_strafeleft ][0] = 'a';
	gamecontrol[gc_straferight][0] = 'd';
	gamecontrol[gc_turnleft   ][0] = KEY_LEFTARROW;
	gamecontrol[gc_turnright  ][0] = KEY_RIGHTARROW;
	gamecontrol[gc_weaponnext ][0] = 'e';
	gamecontrol[gc_weaponnext ][1] = GAMEPAD_KEY(B); // B
	gamecontrol[gc_weaponprev ][0] = 'q';
	gamecontrol[gc_weaponprev ][1] = GAMEPAD_KEY(X); // X
	gamecontrol[gc_wepslot1   ][0] = '1';
	gamecontrol[gc_wepslot2   ][0] = '2';
	gamecontrol[gc_wepslot3   ][0] = '3';
	gamecontrol[gc_wepslot4   ][0] = '4';
	gamecontrol[gc_wepslot5   ][0] = '5';
	gamecontrol[gc_wepslot6   ][0] = '6';
	gamecontrol[gc_wepslot7   ][0] = '7';
	gamecontrol[gc_wepslot8   ][0] = '8';
	gamecontrol[gc_wepslot9   ][0] = '9';
	gamecontrol[gc_wepslot10  ][0] = '0';
	gamecontrol[gc_fire       ][0] = KEY_RCTRL;
	gamecontrol[gc_fire       ][1] = KEY_MOUSE1+0;
	gamecontrol[gc_firenormal ][0] = 'c';
	gamecontrol[gc_tossflag   ][0] = '\'';
	gamecontrol[gc_tossflag   ][1] = GAMEPAD_KEY(A); // A
	gamecontrol[gc_use        ][0] = KEY_LSHIFT;
	gamecontrol[gc_use        ][1] = GAMEPAD_KEY(LEFTSHOULDER); // LB
	gamecontrol[gc_camtoggle  ][0] = 'v';
	gamecontrol[gc_camtoggle  ][1] = GAMEPAD_KEY(DPAD_UP); // D-Pad Up
	gamecontrol[gc_camleft    ][0] = '[';
	gamecontrol[gc_camright   ][0] = ']';
	gamecontrol[gc_camreset   ][0] = 'r';
	gamecontrol[gc_camreset   ][1] = GAMEPAD_KEY(Y); // Y
	gamecontrol[gc_lookup     ][0] = KEY_UPARROW;
	gamecontrol[gc_lookdown   ][0] = KEY_DOWNARROW;
	gamecontrol[gc_centerview ][0] = KEY_END;
	gamecontrol[gc_centerview ][1] = GAMEPAD_KEY(RIGHTSTICK); // Right Stick
	gamecontrol[gc_talkkey    ][0] = 't';
	gamecontrol[gc_talkkey    ][1] = GAMEPAD_KEY(DPAD_LEFT); // D-Pad Left
	gamecontrol[gc_teamkey    ][0] = 'y';
	gamecontrol[gc_scores     ][0] = KEY_TAB;
	gamecontrol[gc_scores     ][1] = GAMEPAD_KEY(DPAD_RIGHT); // D-Pad Right
	gamecontrol[gc_jump       ][0] = KEY_SPACE;
	gamecontrol[gc_jump       ][1] = GAMEPAD_KEY(RIGHTSHOULDER); // RB
	gamecontrol[gc_console    ][0] = KEY_CONSOLE;
	gamecontrol[gc_pause      ][0] = 'p';
	gamecontrol[gc_pause      ][1] = GAMEPAD_KEY(BACK); // Back
	gamecontrol[gc_screenshot ][0] = KEY_F8;
	gamecontrol[gc_screenshot ][1] = GAMEPAD_KEY(DPAD_DOWN); // D-Pad Down
	gamecontrol[gc_recordgif  ][0] = KEY_F9;
	gamecontrol[gc_viewpoint  ][0] = KEY_F12;
	gamecontrol[gc_systemmenu ][0] = GAMEPAD_KEY(START); // Start
	gamecontrolbis[gc_weaponnext][0] = GAMEPAD_KEY(B); // B
	gamecontrolbis[gc_weaponprev][0] = GAMEPAD_KEY(X); // X
	gamecontrolbis[gc_tossflag  ][0] = GAMEPAD_KEY(A); // A
	gamecontrolbis[gc_use       ][0] = GAMEPAD_KEY(LEFTSHOULDER); // LB
	gamecontrolbis[gc_camreset  ][0] = GAMEPAD_KEY(Y); // Y
	gamecontrolbis[gc_centerview][0] = GAMEPAD_KEY(RIGHTSTICK); // Right Stick
	gamecontrolbis[gc_jump      ][0] = GAMEPAD_KEY(RIGHTSHOULDER); // RB
	//gamecontrolbis[gc_pause     ][0] = GAMEPAD_KEY(BACK); // Back
	//gamecontrolbis[gc_systemmenu][0] = GAMEPAD_KEY(START); // Start
	gamecontrolbis[gc_camtoggle ][0] = GAMEPAD_KEY(DPAD_UP); // D-Pad Up
	gamecontrolbis[gc_screenshot][0] = GAMEPAD_KEY(DPAD_DOWN); // D-Pad Down
	//gamecontrolbis[gc_talkkey   ][0] = GAMEPAD_KEY(DPAD_LEFT); // D-Pad Left
	//gamecontrolbis[gc_scores    ][0] = GAMEPAD_KEY(DPAD_RIGHT); // D-Pad Right
}

void G_SaveKeySetting(FILE *f)
{
	INT32 i;

	for (i = 1; i < num_gamecontrols; i++)
	{
		fprintf(f, "setcontrol \"%s\" \"%s\"", gamecontrolname[i],
			G_KeynumToString(gamecontrol[i][0]));

		if (gamecontrol[i][1])
			fprintf(f, " \"%s\"\n", G_KeynumToString(gamecontrol[i][1]));
		else
			fprintf(f, "\n");
	}

	for (i = 1; i < num_gamecontrols; i++)
	{
		fprintf(f, "setcontrol2 \"%s\" \"%s\"", gamecontrolname[i],
			G_KeynumToString(gamecontrolbis[i][0]));

		if (gamecontrolbis[i][1])
			fprintf(f, " \"%s\"\n", G_KeynumToString(gamecontrolbis[i][1]));
		else
			fprintf(f, "\n");
	}
}

// Gamepad keys are shared by both players, but each of them has their own gamepad,
// so they only clash with the controls of the player that is being changed.
INT32 G_CheckDoubleUsage(INT32 keynum, boolean modify, INT32 (*controls)[2])
{
	INT32 result = gc_null;
	if (cv_controlperkey.value == 1)
	{
		const boolean isgamepadkey = (keynum >= KEY_GAMEPAD && keynum < KEY_INV_AXES + NUM_GAMEPAD_AXES);
		const boolean check1 = (!isgamepadkey || controls == gamecontrol);
		const boolean check2 = (!isgamepadkey || controls == gamecontrolbis);
		INT32 i;
		for (i = 0; i < num_gamecontrols; i++)
		{
			if (check1 && gamecontrol[i][0] == keynum)
			{
				result = i;
				if (modify) gamecontrol[i][0] = KEY_NULL;
			}
			if (check1 && gamecontrol[i][1] == keynum)
			{
				result = i;
				if (modify) gamecontrol[i][1] = KEY_NULL;
			}
			if (check2 && gamecontrolbis[i][0] == keynum)
			{
				result = i;
				if (modify) gamecontrolbis[i][0] = KEY_NULL;
			}
			if (check2 && gamecontrolbis[i][1] == keynum)
			{
				result = i;
				if (modify) gamecontrolbis[i][1] = KEY_NULL;
			}
			if (result && !modify)
				return result;
		}
	}
	return result;
}

static INT32 G_FilterKeyByVersion(INT32 numctrl, INT32 keyidx, INT32 player, INT32 *keynum1, INT32 *keynum2, boolean *nestedoverride)
{
	// Special case: ignore KEY_PAUSE because it's hardcoded
	if (keyidx == 0 && *keynum1 == KEY_PAUSE)
	{
		if (*keynum2 != KEY_PAUSE)
		{
			*keynum1 = *keynum2; // shift down keynum2 and continue
			*keynum2 = 0;
		}
		else
			return -1; // skip setting control
	}
	else if (keyidx == 1 && *keynum2 == KEY_PAUSE)
		return -1; // skip setting control

	if (GETMAJOREXECVERSION(cv_execversion.value) < 27 && ( // v2.1.22
		numctrl == gc_weaponnext || numctrl == gc_weaponprev || numctrl == gc_tossflag ||
		numctrl == gc_use || numctrl == gc_camreset || numctrl == gc_jump ||
		numctrl == gc_pause || numctrl == gc_systemmenu || numctrl == gc_camtoggle ||
		numctrl == gc_screenshot || numctrl == gc_talkkey || numctrl == gc_scores ||
		numctrl == gc_centerview
	))
	{
		INT32 keynum = 0, existingctrl = 0;
		INT32 defaultkey;
		boolean defaultoverride = false;

		// get the default gamecontrol
		if (player == 0 && numctrl == gc_systemmenu)
			defaultkey = gamecontrol[numctrl][0];
		else
			defaultkey = (player == 1 ? gamecontrolbis[numctrl][0] : gamecontrol[numctrl][1]);

		// Assign joypad button defaults if there is an open slot.
		// At this point, gamecontrol/bis should have the default controls
		// (unless LOADCONFIG is being run)
		//
		// If the player runs SETCONTROL in-game, this block should not be reached
		// because EXECVERSION is locked onto the latest version.
		if (keyidx == 0 && !*keynum1)
		{
			if (*keynum2) // push keynum2 down; this is an edge case
			{
				*keynum1 = *keynum2;
				*keynum2 = 0;
				keynum = *keynum1;
			}
			else
			{
				keynum = defaultkey;
				defaultoverride = true;
			}
		}
		else if (keyidx == 1 && (!*keynum2 || (!*keynum1 && *keynum2))) // last one is the same edge case as above
		{
			keynum = defaultkey;
			defaultoverride = true;
		}
		else // default to the specified keynum
			keynum = (keyidx == 1 ? *keynum2 : *keynum1);

		// Did our last call override keynum2?
		if (*nestedoverride)
		{
			defaultoverride = true;
			*nestedoverride = false;
		}

		// Fill keynum2 with the default control
		if (keyidx == 0 && !*keynum2)
		{
			*keynum2 = defaultkey;
			// Tell the next call that this is an override
			*nestedoverride = true;

			// if keynum2 already matches keynum1, we probably recursed
			// so unset it
			if (*keynum1 == *keynum2)
			{
				*keynum2 = 0;
				*nestedoverride = false;
		}
		}

		// check if the key is being used somewhere else before passing it
		// pass it through if it's the same numctrl. This is an edge case -- when using
		// LOADCONFIG, gamecontrol is not reset with default.
		//
		// Also, only check if we're actually overriding, to preserve behavior where
		// config'd keys overwrite default keys.
		if (defaultoverride)
			existingctrl = G_CheckDoubleUsage(keynum, false, player == 1 ? gamecontrolbis : gamecontrol);

		if (keynum && (!existingctrl || existingctrl == numctrl))
			return keynum;
		else if (keyidx == 0 && *keynum2)
		{
			// try it again and push down keynum2
			*keynum1 = *keynum2;
			*keynum2 = 0;
			return G_FilterKeyByVersion(numctrl, keyidx, player, keynum1, keynum2, nestedoverride);
			// recursion *should* be safe because we only assign keynum2 to a joy default
			// and then clear it if we find that keynum1 already has the joy default.
		}
		else
			return 0;
	}

	// All's good, so pass the keynum as-is
	if (keyidx == 1)
		return *keynum2;
	else //if (keyidx == 0)
		return *keynum1;
}

static void setcontrol(INT32 (*gc)[2])
{
	INT32 numctrl;
	const char *namectrl;
	INT32 keynum, keynum1, keynum2;
	INT32 player = ((void*)gc == (void*)&gamecontrolbis ? 1 : 0);
	boolean nestedoverride = false;

	namectrl = COM_Argv(1);
	for (numctrl = 0; numctrl < num_gamecontrols && stricmp(namectrl, gamecontrolname[numctrl]);
		numctrl++)
		;
	if (numctrl == num_gamecontrols)
	{
		CONS_Printf(M_GetText("Control '%s' unknown\n"), namectrl);
		return;
	}
	keynum1 = G_KeyStringtoNum(COM_Argv(2));
	keynum2 = G_KeyStringtoNum(COM_Argv(3));
	keynum = G_FilterKeyByVersion(numctrl, 0, player, &keynum1, &keynum2, &nestedoverride);

	if (keynum >= 0)
	{
		(void)G_CheckDoubleUsage(keynum, true, gc);

		// if keynum was rejected, try it again with keynum2
		if (!keynum && keynum2)
		{
			keynum1 = keynum2; // push down keynum2
			keynum2 = 0;
			keynum = G_FilterKeyByVersion(numctrl, 0, player, &keynum1, &keynum2, &nestedoverride);
			if (keynum >= 0)
				(void)G_CheckDoubleUsage(keynum, true, gc);
		}
	}

	if (keynum >= 0)
		gc[numctrl][0] = keynum;

	if (keynum2)
	{
		keynum = G_FilterKeyByVersion(numctrl, 1, player, &keynum1, &keynum2, &nestedoverride);
		if (keynum >= 0)
		{
			if (keynum != gc[numctrl][0])
				gc[numctrl][1] = keynum;
			else
				gc[numctrl][1] = 0;
		}
	}
	else
		gc[numctrl][1] = 0;
}

void Command_Setcontrol_f(void)
{
	INT32 na;

	na = (INT32)COM_Argc();

	if (na != 3 && na != 4)
	{
		CONS_Printf(M_GetText("setcontrol <controlname> <keyname> [<2nd keyname>]: set controls for player 1\n"));
		return;
	}

	setcontrol(gamecontrol);
}

void Command_Setcontrol2_f(void)
{
	INT32 na;

	na = (INT32)COM_Argc();

	if (na != 3 && na != 4)
	{
		CONS_Printf(M_GetText("setcontrol2 <controlname> <keyname> [<2nd keyname>]: set controls for player 2\n"));
		return;
	}

	setcontrol(gamecontrolbis);
}
