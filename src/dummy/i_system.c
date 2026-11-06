#include "../doomdef.h"
#include "../i_system.h"

UINT8 graphics_started = 0;

UINT8 keyboard_started = 0;

size_t I_GetFreeMem(size_t *total)
{
	*total = 0;
	return 0;
}

precise_t I_GetPreciseTime(void) {
	return 0;
}

UINT64 I_GetPrecisePrecision(void) {
	return 1000000;
}

void I_GetEvent(void){}

void I_OsPolling(void){}

ticcmd_t *I_BaseTiccmd(void)
{
	return NULL;
}

ticcmd_t *I_BaseTiccmd2(void)
{
	return NULL;
}

void I_Quit(void)
{
	exit(0);
}

void I_Error(const char *error, ...)
{
	(void)error;
	exit(-1);
}

void I_InitGamepads(void){}

INT32 I_NumGamepads(void)
{
	return 0;
}

void I_ChangeGamepad(UINT8 which)
{
	(void)which;
}

void I_SetGamepadDigital(UINT8 which, boolean enable)
{
	(void)which;
	(void)enable;
}

void I_ShutdownGamepads(void){}

const char *I_GetGamepadName(INT32 joyindex)
{
	(void)joyindex;
	return NULL;
}

boolean I_RumbleSupported(void)
{
	return false;
}

boolean I_RumbleGamepad(UINT8 which, const haptic_t *effect)
{
	(void)which;
	(void)effect;
	return false;
}

boolean I_GetGamepadRumblePaused(UINT8 which)
{
	(void)which;
	return false;
}

boolean I_SetGamepadLargeMotorFreq(UINT8 which, fixed_t freq)
{
	(void)which;
	(void)freq;
	return false;
}

boolean I_SetGamepadSmallMotorFreq(UINT8 which, fixed_t freq)
{
	(void)which;
	(void)freq;
	return false;
}

void I_SetGamepadRumblePaused(UINT8 which, boolean pause)
{
	(void)which;
	(void)pause;
}

boolean I_GetGamepadRumbleSupported(UINT8 which)
{
	(void)which;
	return false;
}

void I_StopGamepadRumble(UINT8 which)
{
	(void)which;
}

#ifndef NOMUMBLE
void I_UpdateMumble(const mobj_t *mobj, const listener_t listener)
{
	(void)mobj;
	(void)listener;
}
#endif

void I_OutputMsg(const char *error, ...)
{
	(void)error;
}

void I_StartupMouse(void){}

void I_StartupMouse2(void){}

void I_StartupKeyboard(void){}

INT32 I_GetKey(void)
{
	return 0;
}

void I_StartupTimer(void){}

void I_Sleep(UINT32 ms){ (void)ms; }

void I_AddExitFunc(void (*func)())
{
	(void)func;
}

void I_RemoveExitFunc(void (*func)())
{
	(void)func;
}

INT32 I_StartupSystem(void)
{
	return -1;
}

void I_ShutdownSystem(void){}

void I_GetDiskFreeSpace(INT64* freespace)
{
	*freespace = 0;
}

char *I_GetUserName(void)
{
	return NULL;
}

INT32 I_mkdir(const char *dirname, INT32 unixright)
{
	(void)dirname;
	(void)unixright;
	return -1;
}

const char *I_LocateWad(void)
{
	return NULL;
}

void I_GetMouseEvents(void){}

void I_UpdateMouseGrab(void){}

void I_SetTextInputMode(boolean active)
{
	(void)active;
}

boolean I_GetTextInputMode(void)
{
	return false;
}

char *I_GetEnv(const char *name)
{
	(void)name;
	return NULL;
}

INT32 I_PutEnv(char *variable)
{
	(void)variable;
	return -1;
}

INT32 I_ClipboardCopy(const char *data, size_t size)
{
	(void)data;
	(void)size;
	return -1;
}

const char *I_ClipboardPaste(void)
{
	return NULL;
}

size_t I_GetRandomBytes(char *destination, size_t amount)
{
	return 0;
}

void I_RegisterSysCommands(void) {}

int I_OpenURL(const char *url)
{
	(void)url;
	return -1;
}

#include "../sdl/dosstr.c"
