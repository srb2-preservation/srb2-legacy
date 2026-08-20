#include "../doomdef.h"
#include "../doomstat.h"
#include "../d_main.h"
#include "../v_video.h"
#include "../r_main.h"
#include "../command.h"
#include "../g_input.h"
#include "../keys.h"
#include "../screen.h"
#include "../i_system.h"
#include "../i_video.h"

#include "something.h"

#include <GL/gl.h>
#include <stdlib.h>
#include <GLFW/glfw3.h>

UINT8 graphics_started = 0;
boolean highcolor = false;

rendermode_t rendermode = render_soft;
rendermode_t chosenrendermode = render_soft;

boolean allow_fullscreen = false;

consvar_t cv_vidwait = CVAR_INIT ("vid_wait", "Off", "Synchronize framerate with refresh rate, eliminating screen tearing", CV_SAVE|CV_CALL|CV_NOINIT, CV_OnOff, NULL);

static GLuint screenTex;
GLFWwindow *window;
static UINT8 *rgba;
static UINT32 pal32[256];

static INT32 windowedModes[21][2] =
{
	{1920,1200}, // 1.60,6.00
	{1920,1080}, // 1.66
	{1680,1050}, // 1.60,5.25
	{1600,1200}, // 1.33
	{1600,1000}, // 1.60,5.00
	{1600, 900}, // 1.66
	{1536, 864}, // 1.66,4.80
	{1366, 768}, // 1.66
	{1440, 900}, // 1.60,4.50
	{1280,1024}, // 1.33?
	{1280, 960}, // 1.33,4.00
	{1280, 800}, // 1.60,4.00
	{1280, 720}, // 1.66
	{1152, 864}, // 1.33,3.60
	{1024, 768}, // 1.33,3.20
	{ 960, 600}, // 1.60,3.00
	{ 800, 600}, // 1.33,2.50
	{ 640, 480}, // 1.33,2.00
	{ 640, 400}, // 1.60,2.00
	{ 320, 240}, // 1.33,1.00
	{ 320, 200}, // 1.60,1.00
};

void I_StartupGraphics(void)
{
	memset(pal32, 0, sizeof(pal32));
	CV_RegisterVar(&cv_vidwait);

    	if (!glfwInit())
		I_Error("Failed to initialize GLFW");

	VID_SetMode(20);
	allow_fullscreen = true;
	graphics_started = true;
}

void I_ShutdownGraphics(void)
{
	if (window)
		glfwDestroyWindow(window);

	glfwTerminate();
}

void VID_StartupOpenGL(void){}

void I_SetPalette(RGBA_t *pal)
{
    if (!pal)
        return;

    for (int i = 0; i < 256; i++)
    {
		pal32[i] =
			(255 << 24) |
			((UINT32)pal[i].s.blue  << 16) |
			((UINT32)pal[i].s.green << 8)  |
			((UINT32)pal[i].s.red);
    }

    if (vid.buffer)
        memset(vid.buffer, 0, vid.width * vid.height);
}

INT32 VID_NumModes(void)
{
	return 21;
}

INT32 VID_GetModeForSize(INT32 w, INT32 h)
{
	(void)w;
	(void)h;
	return 0;
}

void VID_PrepareModeList(void){}

void I_SetResolution(INT32 width, INT32 height) {}

INT32 VID_SetMode(INT32 modeNum)
{
	vid.recalc = 1;
    vid.width = windowedModes[modeNum][0];;
    vid.height = windowedModes[modeNum][1];;
    vid.rowbytes = vid.width;
    vid.bpp = 1;

    vid.buffer = calloc(1, vid.width * vid.height);

	VID_CheckRenderer();
    return 0;
}

void VID_CheckRenderer(void)
{
	GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    free(rgba);
    rgba = calloc(1, vid.width * vid.height * 4);

	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

	if (window)
	{
		glfwDestroyWindow(window);
		window = NULL;
	}

    window = glfwCreateWindow(vid.width, vid.height, "SRB2 Legacy "VERSIONSTRING, cv_fullscreen.value ? primaryMonitor : NULL, NULL);
    if (!window)
		I_Error("Failed to create window!\n");

	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwMakeContextCurrent(window);

	glGenTextures(1, &screenTex);
	glBindTexture(GL_TEXTURE_2D, screenTex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, vid.width, vid.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    glDisable(GL_DEPTH_TEST);
    glClearColor(0,0,0,1);

	if (cv_fullscreen.value == 1)
	{
		glfwSetWindowMonitor(window, primaryMonitor, 0, 0, vid.width, vid.height, I_GetRefreshRate());
	}
	else
	{
		I_SetBorderlessWindow();
		glfwSetWindowMonitor(window, NULL, 0, 0, vid.width, vid.height, 0);
	}
}

void VID_CheckGLLoaded(rendermode_t oldrender)
{
	(void)oldrender;
}

static char vidModeName[33][32]; // allow 33 different modes
const char *VID_GetModeName(INT32 modeNum)
{
	if (modeNum == -1)
	{
		return "idk";
	}
	if (modeNum > 21)
		return NULL;

	sprintf(&vidModeName[modeNum][0], "%dx%d",
		windowedModes[modeNum][0],
		windowedModes[modeNum][1]);
	return &vidModeName[modeNum][0];
}

UINT32 I_GetRefreshRate(void) { return 60; }

void I_UpdateNoBlit(void) {}

void I_FinishUpdate(void)
{
	if (!window && !screens[0] && !vid.buffer)
    	return;

	SCR_CalculateFPS();

	if (marathonmode)
		SCR_DisplayMarathonInfo();

	if (cv_ticrate.value)
		SCR_DisplayTicRate();

	if (cv_showping.value && netgame && consoleplayer != serverplayer)
		SCR_DisplayLocalPing();

	for (int i = 0; i < vid.width * vid.height; i++)
	{
		UINT32 c = pal32[screens[0][i]];

		rgba[i*4 + 0] = (c >> 0)  & 255; // red
		rgba[i*4 + 1] = (c >> 8)  & 255; // green
		rgba[i*4 + 2] = (c >> 16) & 255; // blue
		rgba[i*4 + 3] = 255;			 // yeen
	}

	glBindTexture(GL_TEXTURE_2D, screenTex);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, vid.width, vid.height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, screenTex);

	glBegin(GL_QUADS);
		glTexCoord2f(0, 1); glVertex2f(-1, -1);
		glTexCoord2f(1, 1); glVertex2f( 1, -1);
		glTexCoord2f(1, 0); glVertex2f( 1,  1);
		glTexCoord2f(0, 0); glVertex2f(-1,  1);
	glEnd();

	glDisable(GL_TEXTURE_2D);

    glfwSwapBuffers(window);
    glfwPollEvents();
}

void I_UpdateNoVsync(void) {}

void I_WaitVBL(INT32 count)
{
	(void)count;
}

void I_ReadScreen(UINT8 *scr)
{
	if (!screens[0] && !scr)
    	return;

	VID_BlitLinearScreen(screens[0], scr, vid.width*vid.bpp, vid.height, vid.rowbytes, vid.rowbytes);
}

void I_BeginRead(void){}

void I_EndRead(void){}

void I_SetBorderlessWindow(void)
{
	glfwSetWindowAttrib(window, GLFW_DECORATED, (cv_fullscreen.value == 2 ? GLFW_FALSE : GLFW_TRUE));
}
