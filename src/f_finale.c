// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1993-1996 by id Software, Inc.
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2018 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  f_finale.c
/// \brief Title screen, intro, game evaluation, and credits.

#include "doomdef.h"
#include "doomstat.h"
#include "d_main.h"
#include "f_finale.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "r_local.h"
#include "s_sound.h"
#include "i_time.h"
#include "i_video.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "i_system.h"
#include "m_menu.h"
#include "dehacked.h"
#include "g_input.h"
#include "console.h"
#include "m_random.h"
#include "y_inter.h"
#include "m_cond.h"
#include "p_local.h"
#include "p_setup.h"

#include "lua_hud.h"


// Stage of animation:
// 0 = text, 1 = art screen
static INT32 finalecount;
INT32 titlescrollxspeed = 80;
INT32 titlescrollyspeed = 0;
UINT8 titlemapinaction = TITLEMAP_OFF;

static INT32 timetonext; // Delay between screen changes
static INT32 continuetime; // Short delay when continuing

static tic_t animtimer; // Used for some animation timings
static INT32 roidtics; // Asteroid spinning

static INT32 deplete;
static tic_t stoptimer;

static boolean keypressed = false;

// (no longer) De-Demo'd Title Screen
// menu presentation state
char curbgname[8];
SINT8 curfadevalue;
INT32 curbgcolor;
INT32 curbgxspeed;
INT32 curbgyspeed;
boolean curbghide;

static fixed_t curbgx = 0;
static fixed_t curbgy = 0;
static UINT8  curDemo = 0;
static UINT32 demoDelayLeft;
static UINT32 demoIdleLeft;

// customizable title screen graphics

ttmode_enum ttmode = TTMODE_OLD;
UINT8 ttscale = 1; // FRACUNIT / ttscale
// ttmode user vars
char ttname[9];
INT16 ttx = 0;
INT16 tty = 0;
INT16 ttloop = -1;
UINT16 tttics = 1;

boolean curhidepics;
ttmode_enum curttmode;
UINT8 curttscale;
// ttmode user vars
char curttname[9];
INT16 curttx;
INT16 curtty;
INT16 curttloop;
UINT16 curtttics;

// ttmode old
static patch_t *ttbanner; // white banner with "robo blast" and "2"
static patch_t *ttwing; // wing background
static patch_t *ttsonic; // "SONIC"
static patch_t *ttswave1; // Title Sonics
static patch_t *ttswave2;
static patch_t *ttswip1;
static patch_t *ttsprep1;
static patch_t *ttsprep2;
static patch_t *ttspop1;
static patch_t *ttspop2;
static patch_t *ttspop3;
static patch_t *ttspop4;
static patch_t *ttspop5;
static patch_t *ttspop6;
static patch_t *ttspop7;

// ttmode alacroix
static SINT8 testttscale = 0;
static SINT8 activettscale = 0;
boolean ttavailable[6];
boolean ttloaded[6];

static patch_t *ttribb[6][TTMAX_ALACROIX];
static patch_t *ttsont[6][TTMAX_ALACROIX];
static patch_t *ttrobo[6][TTMAX_ALACROIX];
static patch_t *tttwot[6][TTMAX_ALACROIX];
static patch_t *ttembl[6][TTMAX_ALACROIX];
static patch_t *ttrbtx[6][TTMAX_ALACROIX];
static patch_t *ttsoib[6][TTMAX_ALACROIX];
static patch_t *ttsoif[6][TTMAX_ALACROIX];
static patch_t *ttsoba[6][TTMAX_ALACROIX];
static patch_t *ttsobk[6][TTMAX_ALACROIX];
static patch_t *ttsodh[6][TTMAX_ALACROIX];
static patch_t *tttaib[6][TTMAX_ALACROIX];
static patch_t *tttaif[6][TTMAX_ALACROIX];
static patch_t *tttaba[6][TTMAX_ALACROIX];
static patch_t *tttabk[6][TTMAX_ALACROIX];
static patch_t *tttabt[6][TTMAX_ALACROIX];
static patch_t *tttaft[6][TTMAX_ALACROIX];
static patch_t *ttknib[6][TTMAX_ALACROIX];
static patch_t *ttknif[6][TTMAX_ALACROIX];
static patch_t *ttknba[6][TTMAX_ALACROIX];
static patch_t *ttknbk[6][TTMAX_ALACROIX];
static patch_t *ttkndh[6][TTMAX_ALACROIX];

#define TTEMBL (ttembl[activettscale-1])
#define TTRIBB (ttribb[activettscale-1])
#define TTSONT (ttsont[activettscale-1])
#define TTROBO (ttrobo[activettscale-1])
#define TTTWOT (tttwot[activettscale-1])
#define TTRBTX (ttrbtx[activettscale-1])
#define TTSOIB (ttsoib[activettscale-1])
#define TTSOIF (ttsoif[activettscale-1])
#define TTSOBA (ttsoba[activettscale-1])
#define TTSOBK (ttsobk[activettscale-1])
#define TTSODH (ttsodh[activettscale-1])
#define TTTAIB (tttaib[activettscale-1])
#define TTTAIF (tttaif[activettscale-1])
#define TTTABA (tttaba[activettscale-1])
#define TTTABK (tttabk[activettscale-1])
#define TTTABT (tttabt[activettscale-1])
#define TTTAFT (tttaft[activettscale-1])
#define TTKNIB (ttknib[activettscale-1])
#define TTKNIF (ttknif[activettscale-1])
#define TTKNBA (ttknba[activettscale-1])
#define TTKNBK (ttknbk[activettscale-1])
#define TTKNDH (ttkndh[activettscale-1])

static boolean sonic_blink = false;
static boolean sonic_blink_twice = false;
static boolean sonic_blinked_already = false;
static INT32 sonic_idle_start = 0;
static INT32 sonic_idle_end = 0;
static boolean tails_blink = false;
static boolean tails_blink_twice = false;
static boolean tails_blinked_already = false;
static INT32 tails_idle_start = 0;
static INT32 tails_idle_end = 0;
static boolean knux_blink = false;
static boolean knux_blink_twice = false;
static boolean knux_blinked_already = false;
static INT32 knux_idle_start = 0;
static INT32 knux_idle_end = 0;

// ttmode user
static patch_t *ttuser[TTMAX_USER];
static INT32 ttuser_count = 0;

static boolean goodending;
static patch_t *endbrdr[2]; // border - blue, white, pink - where have i seen those colours before?
static patch_t *endbgsp[3]; // nebula, sun, planet
static patch_t *endegrk[2]; // eggrock - replaced midway through good ending
static patch_t *endfwrk[3]; // firework - replaced with skin when good ending
static patch_t *endspkl[3]; // sparkle
static patch_t *endglow[2]; // glow aura - replaced with black rock's midway through good ending
static patch_t *endxpld[4]; // mini explosion
static patch_t *endescp[5]; // escape pod + flame
static INT32 sparkloffs[3][2]; // eggrock explosions/blackrock sparkles
static INT32 sparklloop;

//
// PROMPT STATE
//
boolean promptactive = false;
static mobj_t *promptmo;
static INT16 promptpostexectag;
static boolean promptblockcontrols;
static char *promptpagetext = NULL;
static INT32 callpromptnum = INT32_MAX;
static INT32 callpagenum = INT32_MAX;
static INT32 callplayer = INT32_MAX;

//
// CUTSCENE TEXT WRITING
//
static const char *cutscene_basetext = NULL;
static char cutscene_disptext[1024];
static INT32 cutscene_baseptr = 0;
static INT32 cutscene_writeptr = 0;
static INT32 cutscene_textcount = 0;
static INT32 cutscene_textspeed = 0;
static UINT8 cutscene_boostspeed = 0;
static tic_t cutscene_lasttextwrite = 0;

//
// This alters the text string cutscene_disptext.
// Use the typical string drawing functions to display it.
// Returns 0 if \0 is reached (end of input)
//
static UINT8 F_WriteText(void)
{
	INT32 numtowrite = 1;
	const char *c;
	tic_t ltw = I_GetTime();

	if (cutscene_lasttextwrite == ltw)
		return 1; // singletics prevention
	cutscene_lasttextwrite = ltw;

	if (cutscene_boostspeed)
	{
		// for custom cutscene speedup mode
		numtowrite = 8;
	}
	else
	{
		// Don't draw any characters if the count was 1 or more when we started
		if (--cutscene_textcount >= 0)
			return 1;

		if (cutscene_textspeed < 7)
			numtowrite = 8 - cutscene_textspeed;
	}

	for (;numtowrite > 0;++cutscene_baseptr)
	{
		c = &cutscene_basetext[cutscene_baseptr];
		if (!c || !*c || *c=='#')
			return 0;

		// \xA0 - \xAF = change text speed
		if ((UINT8)*c >= 0xA0 && (UINT8)*c <= 0xAF)
		{
			cutscene_textspeed = (INT32)((UINT8)*c - 0xA0);
			continue;
		}
		// \xB0 - \xD2 = delay character for up to one second (35 tics)
		else if ((UINT8)*c >= 0xB0 && (UINT8)*c <= (0xB0+TICRATE-1))
		{
			cutscene_textcount = (INT32)((UINT8)*c - 0xAF);
			numtowrite = 0;
			continue;
		}

		cutscene_disptext[cutscene_writeptr++] = *c;

		// Ignore other control codes (color)
		if ((UINT8)*c < 0x80)
			--numtowrite;
	}
	// Reset textcount for next tic based on speed
	// if it wasn't already set by a delay.
	if (cutscene_textcount < 0)
	{
		cutscene_textcount = 0;
		if (cutscene_textspeed > 7)
			cutscene_textcount = cutscene_textspeed - 7;
	}
	return 1;
}

static void F_NewCutscene(const char *basetext)
{
	cutscene_basetext = basetext;
	memset(cutscene_disptext,0,sizeof(cutscene_disptext));
	cutscene_writeptr = cutscene_baseptr = 0;
	cutscene_textspeed = 9;
	cutscene_textcount = TICRATE/2;
}


// =============
//  INTRO SCENE
// =============
#define NUMINTROSCENES 16
INT32 intro_scenenum = 0;
INT32 intro_curtime = 0;

const char *introtext[NUMINTROSCENES];

static tic_t introscenetime[NUMINTROSCENES] =
{
	 7*TICRATE + (TICRATE/2),	// STJr Presents
	11*TICRATE + (TICRATE/2),	// Two months had passed since...
	15*TICRATE + (TICRATE/2),	// As it was about to drain the rings...
	14*TICRATE,					// What Sonic, Tails, and Knuckles...
	18*TICRATE,					// About once every year, a strange...
	19*TICRATE + (TICRATE/2),	// Curses! Eggman yelled. That ridiculous...
	19*TICRATE + (TICRATE/4),	// It was only later that he had an idea...
	10*TICRATE + (TICRATE/2),	// Before beginning his scheme, Eggman decided to give Sonic...
	16*TICRATE,					// We're ready to fire in 15 seconds, the robot said...
	16*TICRATE,					// Meanwhile, Sonic was tearing across the zones...
	16*TICRATE + (TICRATE/2),	// Sonic knew he was getting closer to the city...
	17*TICRATE,					// Greenflower City was gone...
	16*TICRATE + (TICRATE/2),	// You're not quite as dead as we thought, huh?...
	18*TICRATE + (TICRATE/2),	// Eggman took this as his cue and blasted off...
	16*TICRATE,					// Easy! We go find Eggman and stop his...
	25*TICRATE,					// I'm just finding what mission obje...
};

// custom intros
void F_StartCustomCutscene(INT32 cutscenenum, boolean precutscene, boolean resetplayer);

void F_StartIntro(void)
{
	S_StopMusic();

	if (introtoplay)
	{
		if (!cutscenes[introtoplay - 1])
			D_StartTitle();
		else
			F_StartCustomCutscene(introtoplay - 1, false, false);
		return;
	}

	introtext[0] = " #";

	introtext[1] = M_GetText(
	"Two months had passed since Dr. Eggman\n"
	"tried to take over the world using his\n"
	"Ring Satellite.\n#");

	introtext[2] = M_GetText(
	"As it was about to drain the rings\n"
	"away from the planet, Sonic burst into\n"
	"the Satellite and for what he thought\n"
	"would be the last time,\xB4 defeated\n"
	"Dr. Eggman.\n#");

	introtext[3] = M_GetText(
	"\nWhat Sonic, Tails, and Knuckles had\n"
	"not anticipated was that Eggman would\n"
	"return,\xB8 bringing an all new threat.\n#");

	introtext[4] = M_GetText(
	"\xA8""About once every year, a strange asteroid\n"
	"hovers around the planet.\xBF It suddenly\n"
	"appears from nowhere, circles around, and\n"
	"\xB6- just as mysteriously as it arrives -\xB6\n"
	"vanishes after about two months.\xBF\n"
	"No one knows why it appears, or how.\n#");

	introtext[5] = M_GetText(
	"\xA7\"Curses!\"\xA9\xBA Eggman yelled. \xA7\"That hedgehog\n"
	"and his ridiculous friends will pay\n"
	"dearly for this!\"\xA9\xC8 Just then his scanner\n"
	"blipped as the Black Rock made its\n"
	"appearance from nowhere.\xBF Eggman looked at\n"
	"the screen, and just shrugged it off.\n#");

	introtext[6] = M_GetText(
	"It was only later\n"
	"that he had an\n"
	"idea. \xBF\xA7\"The Black\n"
	"Rock usually has a\n"
	"lot of energy\n"
	"within it\xAC...\xA7\xBF\n"
	"If I can somehow\n"
	"harness this,\xB8 I\n"
	"can turn it into\n"
	"the ultimate\n"
	"battle station\xAC...\xA7\xBF\n"
	"And every last\n"
	"person will be\n"
	"begging for mercy,\xB8\xA8\n"
	"including Sonic!\"\n#");

	introtext[7] = M_GetText(
	"\xA8\nBefore beginning his scheme,\n"
	"Eggman decided to give Sonic\n"
	"a reunion party...\n#");

	introtext[8] = M_GetText(
	"\xA5\"We're\xB6 ready\xB6 to\xB4 fire\xB6 in\xB6 15\xB6 seconds!\"\xA8\xB8\n"
	"The robot said, his voice crackling a\n"
	"little down the com-link. \xBF\xA7\"Good!\"\xA8\xB8\n"
	"Eggman sat back in his Egg-Mobile and\n"
	"began to count down as he saw the\n"
	"GreenFlower city on the main monitor.\n#");

	introtext[9] = M_GetText(
	"\xA5\"10...\xD2""9...\xD2""8...\"\xA8\xD2\n"
	"Meanwhile, Sonic was tearing across the\n"
	"zones. Everything became a blur as he\n"
	"ran around loops, skimmed over water,\n"
	"and catapulted himself off rocks with\n"
	"his phenomenal speed.\n#");

	introtext[10] = M_GetText(
	"\xA5\"6...\xD2""5...\xD2""4...\"\xA8\xD2\n"
	"Sonic knew he was getting closer to the\n"
	"City, and pushed himself harder.\xB4 Finally,\n"
	"the city appeared in the horizon.\xD2\xD2\n"
	"\xA5\"3...\xD2""2...\xD2""1...\xD2""Zero.\"\n#");

	introtext[11] = M_GetText(
	"GreenFlower City was gone.\xC4\n"
	"Sonic arrived just in time to see what\n"
	"little of the 'ruins' were left.\n"
	"Everyone and everything in the city\n"
	"had been obliterated.\n#");

	introtext[12] = M_GetText(
	"\xA7\"You're not quite as dead as we thought,\n"
	"huh?\xBF Are you going to tell us your plan as\n"
	"usual or will I \xA8\xB4'have to work it out'\xA7 or\n"
	"something?\"\xD2\xD2\n"
	"\"We'll see\xAA...\xA7\xBF let's give you a quick warm\n"
	"up, Sonic!\xA6\xC4 JETTYSYNS!\xA7\xBD Open fire!\"\n#");

	introtext[13] = M_GetText(
	"Eggman took this\n"
	"as his cue and\n"
	"blasted off,\n"
	"leaving Sonic\n"
	"and Tails behind.\xB6\n"
	"Tails looked at\n"
	"the ruins of the\n"
	"Greenflower City\n"
	"with a grim face\n"
	"and sighed.\xC6\n"
	"\xA7\"Now\xB6 what do we\n"
	"do?\",\xA9 he asked.\n#");

	introtext[14] = M_GetText(
	"\xA7\"Easy!\xBF We go\n"
	"find Eggman\n"
	"and stop his\n"
	"latest\n"
	"insane plan.\xBF\n"
	"Just like\n"
	"we've always\n"
	"done,\xBA right?\xD2\n\n"
	"\xAE...\xA9\xD2\n\n"
	"\"Tails, what\n"
	"\xAA*ARE*\xA9 you\n"
	"doing?\"\n#");

	introtext[15] = M_GetText(
	"\xA8\"I'm just finding what mission obje\xAC\xB1...\xBF\n"
	"\xA6""a-\xB8""ha!\xBF Here it is!\xA8\xBF This will only give us\n"
	"the robot's primary objective.\xBF It says\xAC\xB1...\"\n"
	"\xD2\xA3\x83"
	"* LOCATE  AND  RETRIEVE:  CHAOS  EMERALDS *"
	"\xBF\n"
	"*  CLOSEST  LOCATION:  GREENFLOWER  ZONE  *"
	"\x80\n\xA9\xD2\xD2"
	"\"All right, then\xAF... \xD2\xD2\xA7let's go!\"\n#");

/*
	"What are we waiting for? The first emerald is ours!" Sonic was about to
	run, when he saw a shadow pass over him, he recognized the silhouette
	instantly.
	"Knuckles!" Sonic said. The echidna stopped his glide and landed
	facing Sonic. "What are you doing here?"
	He replied, "This crisis affects the Floating Island,
	if that explosion I saw is anything to go by."
	If you're willing to help then... let's go!"
*/

	G_SetGamestate(GS_INTRO);
	gameaction = ga_nothing;
	paused = false;
	CON_ToggleOff();
	F_NewCutscene(introtext[0]);

	intro_scenenum = 0;
	finalecount = animtimer = stoptimer = 0;
	roidtics = BASEVIDWIDTH - 64;
	timetonext = introscenetime[intro_scenenum];
}

//
// F_IntroDrawScene
//
static void F_IntroDrawScene(void)
{
	boolean highres = false;
	INT32 cx = 8, cy = 128;
	patch_t *background = NULL;
	INT32 bgxoffs = 0;
	void *patch;

	// DRAW A FULL PIC INSTEAD OF FLAT!
	if (intro_scenenum == 0);
	else if (intro_scenenum == 1)
		background = W_CachePatchName("INTRO1", PU_PATCH);
	else if (intro_scenenum == 2)
	{
		background = W_CachePatchName("INTRO2", PU_PATCH);
		highres = true;
	}
	else if (intro_scenenum == 3)
		background = W_CachePatchName("INTRO3", PU_PATCH);
	else if (intro_scenenum == 4)
		background = W_CachePatchName("INTRO4", PU_PATCH);
	else if (intro_scenenum == 5)
	{
		if (intro_curtime >= 5*TICRATE)
			background = W_CachePatchName("RADAR", PU_PATCH);
		else
		{
			background = W_CachePatchName("DRAT", PU_PATCH);
			highres = true;
		}
	}
	else if (intro_scenenum == 6)
	{
		background = W_CachePatchName("INTRO6", PU_PATCH);
		cx = 180;
		cy = 8;
	}
	else if (intro_scenenum == 7)
	{
		if (intro_curtime >= 6*TICRATE)
			background = W_CachePatchName("SGRASS5", PU_PATCH);
		else
			background = W_CachePatchName("SGRASS1", PU_PATCH);
	}
	else if (intro_scenenum == 8)
	{
		background = W_CachePatchName("WATCHING", PU_PATCH);
		highres = true;
	}
	else if (intro_scenenum == 9)
	{
		background = W_CachePatchName("ZOOMING", PU_PATCH);
		highres = true;
	}
	else if (intro_scenenum == 10);
	else if (intro_scenenum == 11)
		background = W_CachePatchName("INTRO5", PU_PATCH);
	else if (intro_scenenum == 12)
	{
		if (intro_curtime >= 7*TICRATE)
			background = W_CachePatchName("CONFRONT", PU_PATCH);
		else
			background = W_CachePatchName("REVENGE", PU_PATCH);
		highres = true;
	}
	else if (intro_scenenum == 13)
	{
		background = W_CachePatchName("TAILSSAD", PU_PATCH);
		highres = true;
		bgxoffs = 144;
		cx = 8;
		cy = 8;
	}
	else if (intro_scenenum == 14)
	{
		if (intro_curtime >= 7*TICRATE)
			background = W_CachePatchName("SONICDO2", PU_PATCH);
		else
			background = W_CachePatchName("SONICDO1", PU_PATCH);
		highres = true;
		cx = 224;
		cy = 8;
	}
	else if (intro_scenenum == 15)
	{
		background = W_CachePatchName("INTRO7", PU_PATCH);
		highres = true;
	}

	V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);

	if (background)
	{
		if (highres)
			V_DrawSmallScaledPatch(bgxoffs, 0, 0, background);
		else
			V_DrawScaledPatch(bgxoffs, 0, 0, background);
	}
	else if (intro_scenenum == 0) // STJr presents
	{
		// "Waaaaaaah" intro
		if (finalecount-TICRATE/2 < 4*TICRATE+23) {
			// aspect is FRACUNIT/2 for 4:3 (source) resolutions, smaller for 16:10 (SRB2) resolutions
			fixed_t aspect = (FRACUNIT + (FRACUNIT*4/3 - FRACUNIT*vid.width/vid.height)/2)>>1;
			fixed_t x,y;
			V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 2);
			if (finalecount < 30) { // Cry!
				if (finalecount < 4)
					S_StopMusic();
				if (finalecount == 4)
					S_ChangeMusicInternal("stjr", false);
				x = (BASEVIDWIDTH<<FRACBITS)/2 - FixedMul(334<<FRACBITS, aspect)/2;
				y = (BASEVIDHEIGHT<<FRACBITS)/2 - FixedMul(358<<FRACBITS, aspect)/2;
				V_DrawSciencePatch(x, y, 0, (patch = W_CachePatchName("WAHH1", PU_PATCH)), aspect);
				W_UnlockCachedPatch(patch);
				if (finalecount > 6) {
					V_DrawSciencePatch(x, y, 0, (patch = W_CachePatchName("WAHH2", PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
				}
				if (finalecount > 10) {
					V_DrawSciencePatch(x, y, 0, (patch = W_CachePatchName("WAHH3", PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
				}
				if (finalecount > 14) {
					V_DrawSciencePatch(x, y, 0, (patch = W_CachePatchName("WAHH4", PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
				}
			}
			else if (finalecount-30 < 20) { // Big eggy
				background = W_CachePatchName("FEEDIN", PU_PATCH);
				x = (BASEVIDWIDTH<<FRACBITS)/2 - FixedMul(560<<FRACBITS, aspect)/2;
				y = (BASEVIDHEIGHT<<FRACBITS) - FixedMul(477<<FRACBITS, aspect);
				V_DrawSciencePatch(x, y, V_SNAPTOBOTTOM, background, aspect);
			}
			else if (finalecount-50 < 30) { // Zoom out
				fixed_t scale = FixedDiv(aspect, FixedDiv((finalecount-50)<<FRACBITS, (15<<FRACBITS))+FRACUNIT);
				background = W_CachePatchName("FEEDIN", PU_PATCH);
				x = (BASEVIDWIDTH<<FRACBITS)/2 - FixedMul(560<<FRACBITS, aspect)/2 + (FixedMul(560<<FRACBITS, aspect) - FixedMul(560<<FRACBITS, scale));
				y = (BASEVIDHEIGHT<<FRACBITS) - FixedMul(477<<FRACBITS, scale);
				V_DrawSciencePatch(x, y, V_SNAPTOBOTTOM, background, scale);
			}
			else
			{
				{
					// Draw tiny eggy
					fixed_t scale = FixedMul(FRACUNIT/3, aspect);
					background = W_CachePatchName("FEEDIN", PU_PATCH);
					x = (BASEVIDWIDTH<<FRACBITS)/2 - FixedMul(560<<FRACBITS, aspect)/2 + (FixedMul(560<<FRACBITS, aspect) - FixedMul(560<<FRACBITS, scale));
					y = (BASEVIDHEIGHT<<FRACBITS) - FixedMul(477<<FRACBITS, scale);
					V_DrawSciencePatch(x, y, V_SNAPTOBOTTOM, background, scale);
				}

				if (finalecount-84 < 58) { // Pure Fat is driving up!
					int ftime = (finalecount-84);
					x = (-189*FRACUNIT) + (FixedMul((6<<FRACBITS)+FRACUNIT/3, ftime<<FRACBITS) - FixedMul((6<<FRACBITS)+FRACUNIT/3, FixedDiv(FixedMul(ftime<<FRACBITS, ftime<<FRACBITS), 120<<FRACBITS)));
					y = (BASEVIDHEIGHT<<FRACBITS) - FixedMul(417<<FRACBITS, aspect);
					// Draw the body
					V_DrawSciencePatch(x, y, V_SNAPTOLEFT|V_SNAPTOBOTTOM, (patch = W_CachePatchName("PUREFAT1", PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
					// Draw the door
					V_DrawSciencePatch(x+FixedMul(344<<FRACBITS, aspect), y+FixedMul(292<<FRACBITS, aspect), V_SNAPTOLEFT|V_SNAPTOBOTTOM, (patch = W_CachePatchName("PUREFAT2", PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
					// Draw the wheel
					V_DrawSciencePatch(x+FixedMul(178<<FRACBITS, aspect), y+FixedMul(344<<FRACBITS, aspect), V_SNAPTOLEFT|V_SNAPTOBOTTOM, (patch = W_CachePatchName(va("TYRE%02u",(abs(finalecount-144)/3)%16), PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
					// Draw the wheel cover
					V_DrawSciencePatch(x+FixedMul(88<<FRACBITS, aspect), y+FixedMul(238<<FRACBITS, aspect), V_SNAPTOLEFT|V_SNAPTOBOTTOM, (patch = W_CachePatchName("PUREFAT3", PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
				} else { // Pure Fat has stopped!
					y = (BASEVIDHEIGHT<<FRACBITS) - FixedMul(417<<FRACBITS, aspect);
					// Draw the body
					V_DrawSciencePatch(0, y, V_SNAPTOLEFT|V_SNAPTOBOTTOM, (patch = W_CachePatchName("PUREFAT1", PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
					// Draw the wheel
					V_DrawSciencePatch(FixedMul(178<<FRACBITS, aspect), y+FixedMul(344<<FRACBITS, aspect), V_SNAPTOLEFT|V_SNAPTOBOTTOM, (patch = W_CachePatchName("TYRE00", PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
					// Draw the wheel cover
					V_DrawSciencePatch(FixedMul(88<<FRACBITS, aspect), y+FixedMul(238<<FRACBITS, aspect), V_SNAPTOLEFT|V_SNAPTOBOTTOM, (patch = W_CachePatchName("PUREFAT3", PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
					// Draw the door
					if (finalecount-TICRATE/2 > 4*TICRATE) { // Door is being raised!
						int ftime = (finalecount-TICRATE/2-4*TICRATE);
						y -= FixedDiv((ftime*ftime)<<FRACBITS, 23<<FRACBITS);
					}
					V_DrawSciencePatch(FixedMul(344<<FRACBITS, aspect), y+FixedMul(292<<FRACBITS, aspect), V_SNAPTOLEFT|V_SNAPTOBOTTOM, (patch = W_CachePatchName("PUREFAT2", PU_PATCH)), aspect);
					W_UnlockCachedPatch(patch);
				}
			}
		} else {
			V_DrawCreditString((160 - V_CreditStringWidth("SONIC TEAM JR")/2)<<FRACBITS, 80<<FRACBITS, 0, "SONIC TEAM JR");
			V_DrawCreditString((160 - V_CreditStringWidth("PRESENTS")/2)<<FRACBITS, 96<<FRACBITS, 0, "PRESENTS");
		}
	}
	else if (intro_scenenum == 10) // Sky Runner
	{
		if (timetonext > 5*TICRATE && timetonext < 6*TICRATE)
		{
			if (!(finalecount & 3))
				background = W_CachePatchName("BRITEGG1", PU_PATCH);
			else
				background = W_CachePatchName("DARKEGG1", PU_PATCH);

			V_DrawScaledPatch(0, 0, 0, background);
		}
		else if (timetonext > 3*TICRATE && timetonext < 4*TICRATE)
		{
			if (!(finalecount & 3))
				background = W_CachePatchName("BRITEGG2", PU_PATCH);
			else
				background = W_CachePatchName("DARKEGG2", PU_PATCH);

			V_DrawScaledPatch(0, 0, 0, background);
		}
		else if (timetonext > 1*TICRATE && timetonext < 2*TICRATE)
		{
			if (!(finalecount & 3))
				background = W_CachePatchName("BRITEGG3", PU_PATCH);
			else
				background = W_CachePatchName("DARKEGG3", PU_PATCH);

			V_DrawScaledPatch(0, 0, 0, background);
		}
		else
		{
			F_SkyScroll(curbgname);
			if (timetonext == 6)
			{
				stoptimer = finalecount;
				animtimer = finalecount % 16;
			}
			else if (timetonext >= 0 && timetonext < 6)
			{
				animtimer = stoptimer;
				deplete -= 32;
			}
			else
			{
				animtimer = finalecount % 16;
				deplete = 160;
			}

			if (finalecount & 1)
			{
				V_DrawScaledPatch(deplete, 8, 0, (patch = W_CachePatchName("RUN2", PU_PATCH)));
				W_UnlockCachedPatch(patch);
				V_DrawScaledPatch(deplete, 72, 0, (patch = W_CachePatchName("PEELOUT2", PU_PATCH)));
				W_UnlockCachedPatch(patch);
			}
			else
			{
				V_DrawScaledPatch(deplete, 8, 0, (patch = W_CachePatchName("RUN1", PU_PATCH)));
				W_UnlockCachedPatch(patch);
				V_DrawScaledPatch(deplete, 72, 0, (patch = W_CachePatchName("PEELOUT1", PU_PATCH)));
				W_UnlockCachedPatch(patch);
			}

			{ // Fixing up the black box rendering to look right in resolutions <16:10 -Red
				INT32 y = 112;
				INT32 h = BASEVIDHEIGHT - 112;
				if (vid.height != BASEVIDHEIGHT * vid.dupy)
				{
					INT32 adjust = (vid.height/vid.dupy)-200;
					adjust /= 2;
					y += adjust;
					h += adjust;
					V_DrawFill(0, 0, BASEVIDWIDTH, adjust, 31); // Render a black bar on top so it keeps the "cinematic" windowboxing... I just prefer it this way. -Red
				}
				V_DrawFill(0, y, BASEVIDWIDTH, h, 31);
			}
		}
	}

	W_UnlockCachedPatch(background);

	if (intro_scenenum == 4) // The asteroid SPINS!
	{
		if (roidtics >= 0)
		{
			V_DrawScaledPatch(roidtics, 24, 0,
				(patch = W_CachePatchName(va("ROID00%.2d", intro_curtime%35), PU_PATCH)));
			W_UnlockCachedPatch(patch);
		}
	}

	if (animtimer)
		animtimer--;

	if (intro_scenenum == 7 && intro_curtime > 7*TICRATE)
	{
		patch_t *sgrass;

		if (intro_curtime >= 7*TICRATE + ((TICRATE/7)*2))
			sgrass = W_CachePatchName("SGRASS4", PU_PATCH);
		else if (intro_curtime >= 7*TICRATE + (TICRATE/7))
			sgrass = W_CachePatchName("SGRASS3", PU_PATCH);
		else
			sgrass = W_CachePatchName("SGRASS2", PU_PATCH);
		V_DrawScaledPatch(123, 4, 0, sgrass);

		W_UnlockCachedPatch(sgrass);
	}

	V_DrawString(cx, cy, 0, cutscene_disptext);
}

//
// F_IntroDrawer
//
void F_IntroDrawer(void)
{
	if (timetonext <= 0)
	{
		if (intro_scenenum == 0)
		{
			if (rendermode != render_none)
			{
				F_WipeStartScreen();
				V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);
				F_WipeEndScreen();
				F_RunWipe(99,true);
			}

			S_ChangeMusicInternal("read_m", false);
		}
		else if (intro_scenenum == 3)
			roidtics = BASEVIDWIDTH - 64;
		else if (intro_scenenum == 10)
		{
			// The only fade to white in the entire damn game.
			if (rendermode != render_none)
			{
				F_WipeStartScreen();
				V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 0);
				F_WipeEndScreen();
				F_RunWipe(99,true);
			}
		}
		else if (intro_scenenum == 15)
		{
			if (rendermode != render_none)
			{
				F_WipeStartScreen();
				V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);
				F_WipeEndScreen();
				F_RunWipe(99,true);
			}

			// Stay on black for a bit. =)
			{
				tic_t nowtime, quittime, lasttime;
				nowtime = lasttime = I_GetTime();
				quittime = nowtime + NEWTICRATE*2; // Shortened the quit time, used to be 2 seconds
				while (quittime > nowtime)
				{
					while (!((nowtime = I_GetTime()) - lasttime))
					{
						I_Sleep(cv_sleep.value);
						I_UpdateTime(cv_timescale.value);
					}
					lasttime = nowtime;

					I_OsPolling();
					I_UpdateNoBlit();
					M_Drawer(); // menu is drawn even on top of wipes
					I_FinishUpdate(); // Update the screen with the image Tails 06-19-2001
				}
			}

			D_StartTitle();
			wipegamestate = GS_INTRO;
			return;
		}
		F_NewCutscene(introtext[++intro_scenenum]);
		timetonext = introscenetime[intro_scenenum];

		F_WipeStartScreen();
		wipegamestate = -1;
		animtimer = stoptimer = 0;
	}

	intro_curtime = introscenetime[intro_scenenum] - timetonext;

	if (rendermode != render_none)
	{
		if (intro_scenenum == 5 && intro_curtime == 5*TICRATE)
		{
			patch_t *radar = W_CachePatchName("RADAR", PU_PATCH);

			F_WipeStartScreen();
			V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);
			V_DrawScaledPatch(0, 0, 0, radar);
			W_UnlockCachedPatch(radar);
			V_DrawString(8, 128, 0, cutscene_disptext);

			F_WipeEndScreen();
			F_RunWipe(99,true);
		}
		else if (intro_scenenum == 7 && intro_curtime == 6*TICRATE) // Force a wipe here
		{
			patch_t *grass = W_CachePatchName("SGRASS5", PU_PATCH);

			F_WipeStartScreen();
			V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);
			V_DrawScaledPatch(0, 0, 0, grass);
			W_UnlockCachedPatch(grass);
			V_DrawString(8, 128, 0, cutscene_disptext);

			F_WipeEndScreen();
			F_RunWipe(99,true);
		}
		else if (intro_scenenum == 12 && intro_curtime == 7*TICRATE)
		{
			patch_t *confront = W_CachePatchName("CONFRONT", PU_PATCH);

			F_WipeStartScreen();
			V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);
			V_DrawSmallScaledPatch(0, 0, 0, confront);
			W_UnlockCachedPatch(confront);
			V_DrawString(8, 128, 0, cutscene_disptext);

			F_WipeEndScreen();
			F_RunWipe(99,true);
		}
		if (intro_scenenum == 14 && intro_curtime == 7*TICRATE)
		{
			patch_t *sdo = W_CachePatchName("SONICDO2", PU_PATCH);

			F_WipeStartScreen();
			V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);
			V_DrawSmallScaledPatch(0, 0, 0, sdo);
			W_UnlockCachedPatch(sdo);
			V_DrawString(224, 8, 0, cutscene_disptext);

			F_WipeEndScreen();
			F_RunWipe(99,true);
		}
	}

	F_IntroDrawScene();
}

//
// F_IntroTicker
//
void F_IntroTicker(void)
{
	// advance animation
	finalecount++;

	if (finalecount % 3 == 0)
		roidtics--;

	timetonext--;

	F_WriteText();

	// check for skipping
	if (keypressed)
		keypressed = false;
}

//
// F_IntroResponder
//
boolean F_IntroResponder(event_t *event)
{
	INT32 key = event->data1;

	// remap virtual keys (mouse & joystick buttons)
	switch (key)
	{
		case KEY_MOUSE1:
			key = KEY_ENTER;
			break;
		case KEY_MOUSE1 + 1:
			key = KEY_BACKSPACE;
			break;
		case KEY_JOY1:
		case KEY_JOY1 + 2:
			key = KEY_ENTER;
			break;
		case KEY_JOY1 + 3:
			key = 'n';
			break;
		case KEY_JOY1 + 1:
			key = KEY_BACKSPACE;
			break;
		case KEY_HAT1:
			key = KEY_UPARROW;
			break;
		case KEY_HAT1 + 1:
			key = KEY_DOWNARROW;
			break;
		case KEY_HAT1 + 2:
			key = KEY_LEFTARROW;
			break;
		case KEY_HAT1 + 3:
			key = KEY_RIGHTARROW;
			break;
	}

	if (event->type != ev_keydown && key != 301)
		return false;

	if (key != 27 && key != KEY_ENTER && key != KEY_SPACE && key != KEY_BACKSPACE)
		return false;

	if (keypressed)
		return false;

	keypressed = true;
	return true;
}

// =========
//  CREDITS
// =========
static const char *credits[] = {
	"\1Sonic Robo Blast II",
	"\1Credits",
	"",
	"\1Game Design",
	"Ben \"Mystic\" Geyer",
	"\"SSNTails\"",
	"Johnny \"Sonikku\" Wallbank",
	"",
	"\1Programming",
	"Alam \"GBC\" Arias",
	"Logan \"GBA\" Arias",
	"Callum Dickinson",
	"Scott \"Graue\" Feeney",
	"Nathan \"Jazz\" Giroux",
	"Vivian \"toaster\" Grannell",
	"Kepa \"Nev3r\" Iceta",
	"Thomas \"Shadow Hog\" Igoe",
	"Iestyn \"Monster Iestyn\" Jealous",
	"Ronald \"Furyhunter\" Kinard", // The SDL2 port
	"John \"JTE\" Muniz",
	"Ehab \"Wolfy\" Saeed",
	"\"Kaito Sinclaire\"",
	"\"SSNTails\"",
	"Marco \"mazmazz\" Zafra",
	"",
	"\1Programming",
	"\1Assistance",
	"\"chi.miru\"", // helped port slope drawing code from ZDoom
	"Andrew \"orospakr\" Clunis",
	"Gregor \"Oogaland\" Dick",
	"Louis-Antoine \"LJSonic\" de Moulins", // for fixing 2.1's netcode (de Rochefort doesn't quite fit on the screen sorry lol)
	"Victor \"Steel Titanium\" Fuentes",
	"Julio \"Chaos Zero 64\" Guir",
	"\"Jimita\"",
	"\"Kalaron\"", // Coded some of Sryder13's collection of OpenGL fixes, especially fog
	"\"Lat'\"", // SRB2-CHAT, the chat window from Kart
	"Matthew \"Shuffle\" Marsalko",
	"Steven \"StroggOnMeth\" McGranahan",
	"\"Morph\"", // For SRB2Morphed stuff
	"Colin \"Sonict\" Pfaff",
	"Sean \"Sryder13\" Ryder",
	"Tasos \"tatokis\" Sahanidis", // Corrected C FixedMul, making 64-bit builds netplay compatible
	"Ben \"Cue\" Woodford",
	// Git contributors with 5+ approved merges of substantive quality,
	// or contributors with at least one groundbreaking merge, may be named.
	// Everyone else is acknowledged under "Special Thanks > SRB2 Community Contributors".
	"",
	"\1Sprite Artists",
	"Odi \"Iceman404\" Atunzu",
	"Victor \"VAdaPEGA\" Ara\x1Fjo", // Araújo -- sorry for our limited font! D:
	"Jim \"MotorRoach\" DeMello",
	"Desmond \"Blade\" DesJardins",
	"Sherman \"CoatRack\" DesJardins",
	"Andrew \"Senku Niola\" Moran",
	"David \"Instant Sonic\" Spencer Jr.",
	"\"SSNTails\"",
	"",
	"\1Texture Artists",
	"Ryan \"Blaze Hedgehog\" Bloom",
	"Buddy \"KinkaJoy\" Fischer",
	"Vivian \"toaster\" Grannell",
	"Kepa \"Nev3r\" Iceta",
	"Jarrett \"JEV3\" Voight",
	"",
	"\1Music and Sound",
	"\1Production",
	"Malcolm \"RedXVI\" Brown",
	"Dave \"DemonTomatoDave\" Bulmer",
	"Paul \"Boinciel\" Clempson",
	"Cyan Helkaraxe",
	"Kepa \"Nev3r\" Iceta",
	"Iestyn \"Monster Iestyn\" Jealous",
	"Jarel \"Arrow\" Jones",
	"Stefan \"Stuf\" Rimalia",
	"Shane Mychal Sexton",
	"\"Spazzo\"",
	"David \"Big Wave Dave\" Spencer Sr.",
	"David \"Instant Sonic\" Spencer Jr.",
	"\"SSNTails\"",
	"",
	"\1Level Design",
	"Matthew \"Fawfulfan\" Chapman",
	"Paul \"Boinciel\" Clempson",
	"Desmond \"Blade\" DesJardins",
	"Sherman \"CoatRack\" DesJardins",
	"Ben \"Mystic\" Geyer",
	"Nathan \"Jazz\" Giroux",
	"Dan \"Blitzzo\" Hagerstrand",
	"Kepa \"Nev3r\" Iceta",
	"Thomas \"Shadow Hog\" Igoe",
	"\"Kaito Sinclaire\"",
	"Wessel \"sphere\" Smit",
	"\"Spazzo\"",
	"\"SSNTails\"",
	"Rob Tisdell",
	"\"Torgo\"",
	"Jarrett \"JEV3\" Voight",
	"Johnny \"Sonikku\" Wallbank",
	"Marco \"mazmazz\" Zafra",
	"",
	"\1Boss Design",
	"Ben \"Mystic\" Geyer",
	"Thomas \"Shadow Hog\" Igoe",
	"John \"JTE\" Muniz",
	"Samuel \"Prime 2.0\" Peters",
	"\"SSNTails\"",
	"Johnny \"Sonikku\" Wallbank",
	"",
	"\1Testing",
	"Hank \"FuriousFox\" Brannock",
	"Cody \"SRB2 Playah\" Koester",
	"Skye \"OmegaVelocity\" Meredith",
	"Stephen \"HEDGESMFG\" Moellering",
	"Nick \"ST218\" Molina",
	"Samuel \"Prime 2.0\" Peters",
	"Colin \"Sonict\" Pfaff",
	"Bill \"Tets\" Reed",
	"",
	"\1Special Thanks",
	"iD Software",
	"Doom Legacy Project",
	"FreeDoom Project", // Used some of the mancubus and rocket launcher sprites for Brak
	"Alex \"MistaED\" Fuller",
	"Pascal \"CodeImp\" vd Heiden", // Doom Builder developer
	"Randi Heit (<!>)", // For their MSPaint <!> sprite that we nicked
	"Simon \"sirjuddington\" Judd", // SLADE developer
	// Acknowledged here are the following:
	// Minor merge request authors, see guideline above
	// Golden - Expanded thin font
	"SRB2 Community Contributors",
	"",
	"\1Produced By",
	"Sonic Team Junior",
	"",
	"\1Published By",
	"A 28K dialup modem",
	"",
	"\1Thank you",
	"\1for playing!",
	NULL
};

static struct {
	UINT32 x, y;
	const char *patch;
} credits_pics[] = {
	{  8, 80+200* 1, "CREDIT01"},
	{  4, 80+200* 2, "CREDIT13"},
	{250, 80+200* 3, "CREDIT12"},
	{  8, 80+200* 4, "CREDIT03"},
	{248, 80+200* 5, "CREDIT11"},
	{  8, 80+200* 6, "CREDIT04"},
	{112, 80+200* 7, "CREDIT10"},
	{240, 80+200* 8, "CREDIT05"},
	{120, 80+200* 9, "CREDIT06"},
	{  8, 80+200*10, "CREDIT07"},
	{  8, 80+200*11, "CREDIT08"},
	{112, 80+200*12, "CREDIT09"},
	{0, 0, NULL}
};

void F_StartCredits(void)
{
	G_SetGamestate(GS_CREDITS);

	// Just in case they're open ... somehow
	M_ClearMenus(true);

	// Save the second we enter the credits
	if ((!modifiedgame || savemoddata) && !(netgame || multiplayer) && !marathonmode && cursaveslot >= 0)
		G_SaveGame((UINT32)cursaveslot);

	if (creditscutscene)
	{
		F_StartCustomCutscene(creditscutscene - 1, false, false);
		return;
	}

	gameaction = ga_nothing;
	paused = false;
	CON_ToggleOff();
	S_StopMusic();

	S_ChangeMusicInternal("credit", false);

	finalecount = 0;
	animtimer = 0;
	timetonext = 2*TICRATE;
}

void F_CreditDrawer(void)
{
	UINT16 i;
	fixed_t y = (80<<FRACBITS) - 5*(animtimer<<FRACBITS)/8;

	V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);

	// Draw background pictures first
	for (i = 0; credits_pics[i].patch; i++)
		V_DrawSciencePatch(credits_pics[i].x<<FRACBITS, (credits_pics[i].y<<FRACBITS) - 4*(animtimer<<FRACBITS)/5, 0, W_CachePatchName(credits_pics[i].patch, PU_PATCH), FRACUNIT>>1);

	// Dim the background
	V_DrawFadeScreen(0x0FF00, 16);

	// Draw credits text on top
	for (i = 0; credits[i]; i++)
	{
		switch(credits[i][0])
		{
		case 0:
			y += 80<<FRACBITS;
			break;
		case 1:
			if (y>>FRACBITS > -20)
				V_DrawCreditString((160 - (V_CreditStringWidth(&credits[i][1])>>1))<<FRACBITS, y, 0, &credits[i][1]);
			y += 30<<FRACBITS;
			break;
		default:
			if (y>>FRACBITS > -10)
				V_DrawStringAtFixed(32<<FRACBITS, y, V_ALLOWLOWERCASE, credits[i]);
			y += 12<<FRACBITS;
			break;
		}
		if (FixedMul(y,vid.dupy) > vid.height)
			break;
	}
}

void F_CreditTicker(void)
{
	// "Simulate" the drawing of the credits so that dedicated mode doesn't get stuck
	UINT16 i;
	fixed_t y = (80<<FRACBITS) - 5*(animtimer<<FRACBITS)/8;

	// Draw credits text on top
	for (i = 0; credits[i]; i++)
	{
		switch(credits[i][0])
		{
			case 0: y += 80<<FRACBITS; break;
			case 1: y += 30<<FRACBITS; break;
			default: y += 12<<FRACBITS; break;
		}
		if (FixedMul(y,vid.dupy) > vid.height)
			break;
	}

	// Do this here rather than in the drawer you doofus! (this is why dedicated mode broke at credits)
	if (!credits[i] && y <= 120<<FRACBITS && !finalecount)
	{
		timetonext = 5*TICRATE+1;
		finalecount = 5*TICRATE;
	}

	if (timetonext)
		timetonext--;
	else
		animtimer++;

	if (finalecount && --finalecount == 0)
		F_StartGameEvaluation();
}

boolean F_CreditResponder(event_t *event)
{
	INT32 key = event->data1;

	// remap virtual keys (mouse & joystick buttons)
	switch (key)
	{
		case KEY_MOUSE1:
			key = KEY_ENTER;
			break;
		case KEY_MOUSE1 + 1:
			key = KEY_BACKSPACE;
			break;
		case KEY_JOY1:
		case KEY_JOY1 + 2:
			key = KEY_ENTER;
			break;
		case KEY_JOY1 + 3:
			key = 'n';
			break;
		case KEY_JOY1 + 1:
			key = KEY_BACKSPACE;
			break;
		case KEY_HAT1:
			key = KEY_UPARROW;
			break;
		case KEY_HAT1 + 1:
			key = KEY_DOWNARROW;
			break;
		case KEY_HAT1 + 2:
			key = KEY_LEFTARROW;
			break;
		case KEY_HAT1 + 3:
			key = KEY_RIGHTARROW;
			break;
	}

	if (!(timesBeaten) && !(netgame || multiplayer))
		return false;

	if (event->type != ev_keydown)
		return false;

	if (key != KEY_ESCAPE && key != KEY_ENTER && key != KEY_SPACE && key != KEY_BACKSPACE)
		return false;

	if (keypressed)
		return true;

	keypressed = true;
	return true;
}

// ============
//  EVALUATION
// ============
#define INTERVAL 50
#define TRANSLEVEL V_80TRANS
static boolean drawemblem = false, drawchaosemblem = false;

void F_StartGameEvaluation(void)
{
	// Credits option in secrets menu
	if (cursaveslot == -2)
	{
		F_StartGameEnd();
		return;
	}

	G_SetGamestate(GS_EVALUATION);

	// Just in case they're open ... somehow
	M_ClearMenus(true);

	// Save the second we enter the evaluation
	// We need to do this again!  Remember, it's possible a mod designed skipped
	// the credits sequence!
	if ((!modifiedgame || savemoddata) && !(netgame || multiplayer) && !marathonmode && cursaveslot >= 0)
		G_SaveGame((UINT32)cursaveslot);

	gameaction = ga_nothing;
	paused = false;
	CON_ToggleOff();

	finalecount = 0;
}

void F_GameEvaluationDrawer(void)
{
	INT32 x, y, i;
	angle_t fa;
	INT32 eemeralds_cur;
	char patchname[7] = "CEMGx0";

	V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);

	// Draw all the good crap here.
	if (ALL7EMERALDS(emeralds))
		V_DrawString(114, 16, 0, "GOT THEM ALL!");
	else if (marathonmode)
		V_DrawString(114, 16, 0, "THANKS FOR THE RUN!");
	else
		V_DrawString(124, 16, 0, "TRY AGAIN!");

	eemeralds_cur = (finalecount % 360)<<FRACBITS;

	for (i = 0; i < 7; ++i)
	{
		fa = (FixedAngle(eemeralds_cur)>>ANGLETOFINESHIFT) & FINEMASK;
		x = (BASEVIDWIDTH<<(FRACBITS-1)) + (60*FINECOSINE(fa));
		y = ((BASEVIDHEIGHT+16)<<(FRACBITS-1)) + (60*FINESINE(fa));
		eemeralds_cur += (360<<FRACBITS)/7;

		patchname[4] = 'A'+(char)i;
		V_DrawFixedPatch(x, y, FRACUNIT, ((emeralds & (1<<i)) ? 0 : TRANSLEVEL), W_CachePatchName(patchname, PU_PATCH), NULL);
	}

	if (finalecount == 5*TICRATE)
	{
		if ((!modifiedgame || savemoddata) && !(netgame || multiplayer))
		{
			++timesBeaten;

			if (ALL7EMERALDS(emeralds))
				++timesBeatenWithEmeralds;
			if (ultimatemode)
				++timesBeatenUltimate;

			if (M_UpdateUnlockablesAndExtraEmblems())
				S_StartSound(NULL, sfx_ncitem);

			G_SaveGameData();
		}
	}

	if (finalecount >= 5*TICRATE)
	{
		if (drawemblem)
			V_DrawScaledPatch(120, 192, 0, W_CachePatchName("NWNGA0", PU_PATCH));

		if (drawchaosemblem)
			V_DrawScaledPatch(200, 192, 0, W_CachePatchName("NWNGA0", PU_PATCH));

		V_DrawString(8, 16, V_YELLOWMAP, "Unlocked:");

		if (!(netgame) && (!modifiedgame || savemoddata))
		{
			INT32 startcoord = 32;

			for (i = 0; i < MAXUNLOCKABLES; i++)
			{
				if (unlockables[i].conditionset && unlockables[i].conditionset < MAXCONDITIONSETS
					&& unlockables[i].type && !unlockables[i].nocecho)
				{
					if (unlockables[i].unlocked)
						V_DrawString(8, startcoord, 0, unlockables[i].name);
					startcoord += 8;
				}
			}
		}
		else if (netgame)
			V_DrawString(8, 96, V_YELLOWMAP, "Prizes only\nawarded in\nsingle player!");
		else
			V_DrawString(8, 96, V_YELLOWMAP, "Prizes not\nawarded in\nmodified games!");
	}
	if (marathonmode)
	{
		const char *rtatext, *cuttext, *endingtext;
		rtatext = (marathonmode & Playing()) ? "In-game timer" : "RTA timer";
		cuttext = (marathonmode & MA_NOCUTSCENES) ? "" : " w/ cutscenes";
		endingtext = va("%s, %s%s", skins[players[consoleplayer].skin].realname, rtatext, cuttext);
		V_DrawCenteredString(BASEVIDWIDTH/2, 182, V_SNAPTOBOTTOM|(ultimatemode ? V_REDMAP : V_YELLOWMAP), endingtext);
	}
}

void F_GameEvaluationTicker(void)
{
	finalecount++;

	if (finalecount > 10*TICRATE)
		F_StartGameEnd();
}

// ==========
//  GAME END
// ==========
void F_StartGameEnd(void)
{
	G_SetGamestate(GS_GAMEEND);

	gameaction = ga_nothing;
	paused = false;
	CON_ToggleOff();
	S_StopMusic();

	// In case menus are still up?!!
	M_ClearMenus(true);

	timetonext = TICRATE;
}

//
// F_GameEndDrawer
//
void F_GameEndDrawer(void)
{
	// this function does nothing
}

//
// F_GameEndTicker
//
void F_GameEndTicker(void)
{
	if (timetonext > 0)
		timetonext--;
	else
		D_StartTitle();
}

// ==============
//  TITLE SCREEN
// ==============
static void F_CacheTitleScreen(void)
{
	ttbanner = W_CachePatchName("TTBANNER", PU_PATCH);
	ttwing = W_CachePatchName("TTWING", PU_PATCH);
	ttsonic = W_CachePatchName("TTSONIC", PU_PATCH);
	ttswave1 = W_CachePatchName("TTSWAVE1", PU_PATCH);
	ttswave2 = W_CachePatchName("TTSWAVE2", PU_PATCH);
	ttswip1 = W_CachePatchName("TTSWIP1", PU_PATCH);
	ttsprep1 = W_CachePatchName("TTSPREP1", PU_PATCH);
	ttsprep2 = W_CachePatchName("TTSPREP2", PU_PATCH);
	ttspop1 = W_CachePatchName("TTSPOP1", PU_PATCH);
	ttspop2 = W_CachePatchName("TTSPOP2", PU_PATCH);
	ttspop3 = W_CachePatchName("TTSPOP3", PU_PATCH);
	ttspop4 = W_CachePatchName("TTSPOP4", PU_PATCH);
	ttspop5 = W_CachePatchName("TTSPOP5", PU_PATCH);
	ttspop6 = W_CachePatchName("TTSPOP6", PU_PATCH);
	ttspop7 = W_CachePatchName("TTSPOP7", PU_PATCH);
}

void F_InitMenuPresValues(void)
{
	curbgx = 0;
	curbgy = 0;
	prevMenuId = 0;
	activeMenuId = MainDef.menuid;

	// Set defaults for presentation values
	strncpy(curbgname, "TITLESKY", 8);
	curfadevalue = 16;
	curbgcolor = -1;
	curbgxspeed = titlescrollxspeed;
	curbgyspeed = titlescrollyspeed;
	curbghide = false;

	curhidepics = hidetitlepics;
	curttmode = ttmode;
	curttscale = ttscale;
	strncpy(curttname, ttname, 9);
	curttx = ttx;
	curtty = tty;
	curttloop = ttloop;
	curtttics = tttics;

	// Find current presentation values
	M_SetMenuCurBackground((gamestate == GS_TIMEATTACK) ? "SRB2BACK" : "TITLESKY");
	M_SetMenuCurFadeValue(16);
	M_SetMenuCurTitlePics();
}

//
// F_SkyScroll
//
void F_SkyScroll(const char *patchname)
{
	INT32 x, basey = 0;
	INT32 dupz = (vid.dupx < vid.dupy ? vid.dupx : vid.dupy);
	patch_t *pat;

	if (rendermode == render_none)
		return;

	if (!patchname || !patchname[0])
	{
		V_DrawFill(0, 0, vid.width, vid.height, 31);
		return;
	}

	pat = W_CachePatchName(patchname, PU_PATCH);

	if (!curbgxspeed && !curbgyspeed)
	{
		V_DrawPatchFill(pat);
		W_UnlockCachedPatch(pat);
		return;
	}

	// Modulo the background scrolling to prevent jumps from integer overflows
	// We already load the background patch here, so we can modulo it here
	// to avoid also having to load the patch in F_MenuPresTicker
	curbgx %= pat->width  * 16;
	curbgy %= pat->height * 16;

	// Ooh, fancy frame interpolation
	x     = ((curbgx*dupz) + FixedInt((rendertimefrac_unpaused-FRACUNIT) * curbgxspeed*dupz)) / 16;
	basey = ((curbgy*dupz) + FixedInt((rendertimefrac_unpaused-FRACUNIT) * curbgyspeed*dupz)) / 16;

	if (x     > 0) // Make sure that we don't leave the left or top sides empty
		x     -= pat->width  * dupz;
	if (basey > 0)
		basey -= pat->height * dupz;

	for (; x < vid.width; x += pat->width * dupz)
	{
		for (INT32 y = basey; y < vid.height; y += pat->height * dupz)
			V_DrawScaledPatch(x, y, V_NOSCALESTART, pat);
	}

	W_UnlockCachedPatch(pat);
}

mobj_t *titlemapcameraref = NULL;

#define LOADTTGFX(arr, name, maxf) \
lumpnum = W_CheckNumForName(name); \
if (lumpnum != LUMPERROR) \
{ \
	arr[0] = W_CachePatchName(name, PU_LEVEL); \
	arr[min(1, maxf-1)] = 0; \
} \
else if (strlen(name) <= 6) \
{ \
	fixed_t cnt = strlen(name); \
	strncpy(lumpname, name, 7); \
	for (i = 0; i < maxf-1; i++) \
	{ \
		sprintf(&lumpname[cnt], "%.2hu", (UINT16)(i+1)); \
		lumpname[8] = 0; \
		lumpnum = W_CheckNumForName(lumpname); \
		if (lumpnum != LUMPERROR) \
			arr[i] = W_CachePatchName(lumpname, PU_LEVEL); \
		else \
			break; \
	} \
	arr[min(i, maxf-1)] = 0; \
} \
else \
	arr[0] = 0;

void F_StartTitleScreen(void)
{
	if (menupres[MN_MAIN].musname[0])
		S_ChangeMusic(menupres[MN_MAIN].musname, menupres[MN_MAIN].mustrack, menupres[MN_MAIN].muslooping);
	else
		S_ChangeMusicInternal("titles", looptitle);

	if (gamestate != GS_TITLESCREEN && gamestate != GS_WAITINGPLAYERS)
	{
		ttuser_count =\
		 ttloaded[0] = ttloaded[1] = ttloaded[2] = ttloaded[3] = ttloaded[4] = ttloaded[5] =\
		 testttscale = activettscale =\
		 sonic_blink = sonic_blink_twice = sonic_idle_start = sonic_idle_end =\
		 tails_blink = tails_blink_twice = tails_idle_start = tails_idle_end =\
		 knux_blink  = knux_blink_twice  = knux_idle_start  = knux_idle_end  = 0;

		sonic_blinked_already = tails_blinked_already = knux_blinked_already = 1; // don't blink on the first idle cycle

		if (curttmode == TTMODE_ALACROIX)
			finalecount = -3; // hack so that frames don't advance during the entry wipe
		else
			finalecount = 0;
		wipetypepost = menupres[MN_MAIN].enterwipe;
	}
	else
		wipegamestate = GS_TITLESCREEN;

	if (titlemap)
	{
		gamestate_t prevwipegamestate = wipegamestate;
		titlemapinaction = TITLEMAP_LOADING;
		gamemap = titlemap;

		if (!mapheaderinfo[gamemap-1])
			P_AllocMapHeader(gamemap-1);

		maptol = mapheaderinfo[gamemap-1]->typeoflevel;
		globalweather = mapheaderinfo[gamemap-1]->weather;

		G_DoLoadLevel(true);
		if (!titlemap)
			return;

		players[displayplayer].playerstate = PST_DEAD; // Don't spawn the player in dummy (I'm still a filthy cheater)

		// Set Default Position
		mapthing_t *startpos;
		if (playerstarts[0])
			startpos = playerstarts[0];
		else if (deathmatchstarts[0])
			startpos = deathmatchstarts[0];
		else
			startpos = NULL;

		if (startpos)
		{
			camera.x = startpos->x << FRACBITS;
			camera.y = startpos->y << FRACBITS;
			camera.subsector = R_PointInSubsector(camera.x, camera.y);
			camera.z = camera.subsector->sector->floorheight + ((startpos->options >> ZSHIFT) << FRACBITS);
			camera.angle = (startpos->angle % 360)*ANG1;
			camera.aiming = 0;
		}
		else
		{
			camera.x = camera.y = camera.z = camera.angle = camera.aiming = 0;
			camera.subsector = NULL; // toast is filthy too
		}

		camera.chase = true;
		camera.height = 0;

		wipegamestate = prevwipegamestate;
	}
	else
	{
		titlemapinaction = TITLEMAP_OFF;
		gamemap = 1; // g_game.c
		CON_ClearHUD();
	}

	G_SetGamestate(GS_TITLESCREEN);

	// IWAD dependent stuff.

	animtimer = 0;

	demoDelayLeft = demoDelayTime;
	demoIdleLeft = demoIdleTime;

	switch(curttmode)
	{
		case TTMODE_OLD:
		case TTMODE_NONE:
			ttbanner = W_CachePatchName("TTBANNER", PU_LEVEL);
			ttwing = W_CachePatchName("TTWING", PU_LEVEL);
			ttsonic = W_CachePatchName("TTSONIC", PU_LEVEL);
			ttswave1 = W_CachePatchName("TTSWAVE1", PU_LEVEL);
			ttswave2 = W_CachePatchName("TTSWAVE2", PU_LEVEL);
			ttswip1 = W_CachePatchName("TTSWIP1", PU_LEVEL);
			ttsprep1 = W_CachePatchName("TTSPREP1", PU_LEVEL);
			ttsprep2 = W_CachePatchName("TTSPREP2", PU_LEVEL);
			ttspop1 = W_CachePatchName("TTSPOP1", PU_LEVEL);
			ttspop2 = W_CachePatchName("TTSPOP2", PU_LEVEL);
			ttspop3 = W_CachePatchName("TTSPOP3", PU_LEVEL);
			ttspop4 = W_CachePatchName("TTSPOP4", PU_LEVEL);
			ttspop5 = W_CachePatchName("TTSPOP5", PU_LEVEL);
			ttspop6 = W_CachePatchName("TTSPOP6", PU_LEVEL);
			ttspop7 = W_CachePatchName("TTSPOP7", PU_LEVEL);
			break;

		// don't load alacroix gfx yet; we do that upon first draw.
		case TTMODE_ALACROIX:
			break;

		case TTMODE_USER:
		{
			UINT16 i;
			lumpnum_t lumpnum;
			char lumpname[9];

			LOADTTGFX(ttuser, curttname, TTMAX_USER)
			break;
		}
	}
}

static void F_UnloadAlacroixGraphics(SINT8 oldttscale)
{
	// This all gets freed by PU_LEVEL when exiting the menus.
	// When re-visiting the menus (e.g., from exiting in-game), the gfx are force-reloaded.
	// So leftover addresses here should not be a problem.

	UINT16 i;
	oldttscale--; // zero-based index
	for (i = 0; i < TTMAX_ALACROIX; i++)
	{
		if(ttembl[oldttscale][i]) { Z_Free(ttembl[oldttscale][i]); ttembl[oldttscale][i] = 0; }
		if(ttribb[oldttscale][i]) { Z_Free(ttribb[oldttscale][i]); ttribb[oldttscale][i] = 0; }
		if(ttsont[oldttscale][i]) { Z_Free(ttsont[oldttscale][i]); ttsont[oldttscale][i] = 0; }
		if(ttrobo[oldttscale][i]) { Z_Free(ttrobo[oldttscale][i]); ttrobo[oldttscale][i] = 0; }
		if(tttwot[oldttscale][i]) { Z_Free(tttwot[oldttscale][i]); tttwot[oldttscale][i] = 0; }
		if(ttrbtx[oldttscale][i]) { Z_Free(ttrbtx[oldttscale][i]); ttrbtx[oldttscale][i] = 0; }
		if(ttsoib[oldttscale][i]) { Z_Free(ttsoib[oldttscale][i]); ttsoib[oldttscale][i] = 0; }
		if(ttsoif[oldttscale][i]) { Z_Free(ttsoif[oldttscale][i]); ttsoif[oldttscale][i] = 0; }
		if(ttsoba[oldttscale][i]) { Z_Free(ttsoba[oldttscale][i]); ttsoba[oldttscale][i] = 0; }
		if(ttsobk[oldttscale][i]) { Z_Free(ttsobk[oldttscale][i]); ttsobk[oldttscale][i] = 0; }
		if(ttsodh[oldttscale][i]) { Z_Free(ttsodh[oldttscale][i]); ttsodh[oldttscale][i] = 0; }
		if(tttaib[oldttscale][i]) { Z_Free(tttaib[oldttscale][i]); tttaib[oldttscale][i] = 0; }
		if(tttaif[oldttscale][i]) { Z_Free(tttaif[oldttscale][i]); tttaif[oldttscale][i] = 0; }
		if(tttaba[oldttscale][i]) { Z_Free(tttaba[oldttscale][i]); tttaba[oldttscale][i] = 0; }
		if(tttabk[oldttscale][i]) { Z_Free(tttabk[oldttscale][i]); tttabk[oldttscale][i] = 0; }
		if(tttabt[oldttscale][i]) { Z_Free(tttabt[oldttscale][i]); tttabt[oldttscale][i] = 0; }
		if(tttaft[oldttscale][i]) { Z_Free(tttaft[oldttscale][i]); tttaft[oldttscale][i] = 0; }
		if(ttknib[oldttscale][i]) { Z_Free(ttknib[oldttscale][i]); ttknib[oldttscale][i] = 0; }
		if(ttknif[oldttscale][i]) { Z_Free(ttknif[oldttscale][i]); ttknif[oldttscale][i] = 0; }
		if(ttknba[oldttscale][i]) { Z_Free(ttknba[oldttscale][i]); ttknba[oldttscale][i] = 0; }
		if(ttknbk[oldttscale][i]) { Z_Free(ttknbk[oldttscale][i]); ttknbk[oldttscale][i] = 0; }
		if(ttkndh[oldttscale][i]) { Z_Free(ttkndh[oldttscale][i]); ttkndh[oldttscale][i] = 0; }
	}
	ttloaded[oldttscale] = false;
}

static void F_LoadAlacroixGraphics(SINT8 newttscale)
{
	UINT16 i, j;
	lumpnum_t lumpnum;
	char lumpname[9];
	char names[22][5] = {
		"EMBL",
		"RIBB",
		"SONT",
		"ROBO",
		"TWOT",
		"RBTX",
		"SOIB",
		"SOIF",
		"SOBA",
		"SOBK",
		"SODH",
		"TAIB",
		"TAIF",
		"TABA",
		"TABK",
		"TABT",
		"TAFT",
		"KNIB",
		"KNIF",
		"KNBA",
		"KNBK",
		"KNDH"
	};
	char lumpnames[22][7];

	newttscale--; // 0-based index

	if (!ttloaded[newttscale])
	{
		for (j = 0; j < 22; j++)
			sprintf(&lumpnames[j][0], "T%.1hu%s", (UINT8)newttscale+1, names[j]);

		LOADTTGFX(ttembl[newttscale], lumpnames[0], TTMAX_ALACROIX)
		LOADTTGFX(ttribb[newttscale], lumpnames[1], TTMAX_ALACROIX)
		LOADTTGFX(ttsont[newttscale], lumpnames[2], TTMAX_ALACROIX)
		LOADTTGFX(ttrobo[newttscale], lumpnames[3], TTMAX_ALACROIX)
		LOADTTGFX(tttwot[newttscale], lumpnames[4], TTMAX_ALACROIX)
		LOADTTGFX(ttrbtx[newttscale], lumpnames[5], TTMAX_ALACROIX)
		LOADTTGFX(ttsoib[newttscale], lumpnames[6], TTMAX_ALACROIX)
		LOADTTGFX(ttsoif[newttscale], lumpnames[7], TTMAX_ALACROIX)
		LOADTTGFX(ttsoba[newttscale], lumpnames[8], TTMAX_ALACROIX)
		LOADTTGFX(ttsobk[newttscale], lumpnames[9], TTMAX_ALACROIX)
		LOADTTGFX(ttsodh[newttscale], lumpnames[10], TTMAX_ALACROIX)
		LOADTTGFX(tttaib[newttscale], lumpnames[11], TTMAX_ALACROIX)
		LOADTTGFX(tttaif[newttscale], lumpnames[12], TTMAX_ALACROIX)
		LOADTTGFX(tttaba[newttscale], lumpnames[13], TTMAX_ALACROIX)
		LOADTTGFX(tttabk[newttscale], lumpnames[14], TTMAX_ALACROIX)
		LOADTTGFX(tttabt[newttscale], lumpnames[15], TTMAX_ALACROIX)
		LOADTTGFX(tttaft[newttscale], lumpnames[16], TTMAX_ALACROIX)
		LOADTTGFX(ttknib[newttscale], lumpnames[17], TTMAX_ALACROIX)
		LOADTTGFX(ttknif[newttscale], lumpnames[18], TTMAX_ALACROIX)
		LOADTTGFX(ttknba[newttscale], lumpnames[19], TTMAX_ALACROIX)
		LOADTTGFX(ttknbk[newttscale], lumpnames[20], TTMAX_ALACROIX)
		LOADTTGFX(ttkndh[newttscale], lumpnames[21], TTMAX_ALACROIX)

		ttloaded[newttscale] = true;
	}
}

#undef LOADTTGFX

static void F_FigureActiveTtScale(void)
{
	SINT8 newttscale = max(1, min(6, vid.dupx));
	SINT8 oldttscale = activettscale;

	if (newttscale == testttscale)
		return;
	testttscale = newttscale;

	// If ttscale is unavailable: look for lower scales, then higher scales.
	for (; newttscale >= 1; newttscale--)
	{
		if (ttavailable[newttscale-1])
			break;
	}

	for (; newttscale <= 6; newttscale++)
	{
		if (ttavailable[newttscale-1])
			break;
	}

	activettscale = (newttscale >= 1 && newttscale <= 6) ? newttscale : 0;

	// We have a new ttscale, so load gfx
	if(oldttscale > 0)
		F_UnloadAlacroixGraphics(oldttscale);

	if(activettscale > 0)
		F_LoadAlacroixGraphics(activettscale);
}

// (no longer) De-Demo'd Title Screen
void F_TitleScreenDrawer(void)
{
	boolean hidepics;
	fixed_t sc = FRACUNIT / max(1, curttscale);

	if (modeattacking)
		return; // We likely came here from retrying. Don't do a damn thing.

	// Jimita: Load title screen patches.
	if (needpatchrecache)
		F_CacheTitleScreen();

	// Draw that sky!
	if (!titlemapinaction)
	{
		if (curbgcolor >= 0)
			V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, curbgcolor);
		else if (!curbghide || gamestate == GS_WAITINGPLAYERS)
			F_SkyScroll(curbgname);
	}

	// Don't draw outside of the title screen, or if the patch isn't there.
	if (gamestate != GS_TITLESCREEN && gamestate != GS_WAITINGPLAYERS)
		return;

	// rei|miru: use title pics?
	if (hidetitlepics)
		goto luahook;

	switch(curttmode)
	{
		case TTMODE_OLD:
		case TTMODE_NONE:
			V_DrawSciencePatch(30<<FRACBITS, 14<<FRACBITS, 0, ttwing, sc);

			if (finalecount < 57)
			{
				if (finalecount == 35)
					V_DrawSciencePatch(115<<FRACBITS, 15<<FRACBITS, 0, ttspop1, sc);
				else if (finalecount == 36)
					V_DrawSciencePatch(114<<FRACBITS, 15<<FRACBITS, 0,ttspop2, sc);
				else if (finalecount == 37)
					V_DrawSciencePatch(113<<FRACBITS, 15<<FRACBITS, 0,ttspop3, sc);
				else if (finalecount == 38)
					V_DrawSciencePatch(112<<FRACBITS, 15<<FRACBITS, 0,ttspop4, sc);
				else if (finalecount == 39)
					V_DrawSciencePatch(111<<FRACBITS, 15<<FRACBITS, 0,ttspop5, sc);
				else if (finalecount == 40)
					V_DrawSciencePatch(110<<FRACBITS, 15<<FRACBITS, 0, ttspop6, sc);
				else if (finalecount >= 41 && finalecount <= 44)
					V_DrawSciencePatch(109<<FRACBITS, 15<<FRACBITS, 0, ttspop7, sc);
				else if (finalecount >= 45 && finalecount <= 48)
					V_DrawSciencePatch(108<<FRACBITS, 12<<FRACBITS, 0, ttsprep1, sc);
				else if (finalecount >= 49 && finalecount <= 52)
					V_DrawSciencePatch(107<<FRACBITS, 9<<FRACBITS, 0, ttsprep2, sc);
				else if (finalecount >= 53 && finalecount <= 56)
					V_DrawSciencePatch(106<<FRACBITS, 6<<FRACBITS, 0, ttswip1, sc);
				V_DrawSciencePatch(93<<FRACBITS, 106<<FRACBITS, 0, ttsonic, sc);
			}
			else
			{
				V_DrawSciencePatch(93<<FRACBITS, 106<<FRACBITS, 0,ttsonic, sc);
				if (finalecount/5 & 1)
					V_DrawSciencePatch(100<<FRACBITS, 3<<FRACBITS, 0,ttswave1, sc);
				else
					V_DrawSciencePatch(100<<FRACBITS, 3<<FRACBITS, 0,ttswave2, sc);
			}

			V_DrawSciencePatch(48<<FRACBITS, 142<<FRACBITS, 0,ttbanner, sc);
			break;

		case TTMODE_ALACROIX:
			//
			// PRE-INTRO: WING ON BLACK BACKGROUND
			//

			// Figure the gfx scale and load gfx if necessary
			F_FigureActiveTtScale();

			if (!activettscale) // invalid scale, draw nothing
				break;
			sc = FRACUNIT / activettscale;

			// Start at black background. Draw it until tic 30, where we replace with a white flash.
			//
			// TODO: How to NOT draw the titlemap while this background is drawn?
			//
			if (finalecount <= 29)
				V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);

			// Draw emblem
			V_DrawSciencePatch(40<<FRACBITS, 20<<FRACBITS, 0, TTEMBL[0], sc);

			// Animate SONIC ROBO BLAST 2 before the white flash at tic 30.
			if (finalecount <= 29)
			{
				// Ribbon unfurls, revealing SONIC text, from tic 0 to tic 24. SONIC text is pre-baked into this ribbon graphic.
				V_DrawSciencePatch(39<<FRACBITS, 88<<FRACBITS, 0, TTRIBB[min(max(0, finalecount), 24)], sc);

				// Animate SONIC text while the ribbon unfurls, from tic 0 to tic 28.
				if(finalecount >= 0)
					V_DrawSciencePatch(89<<FRACBITS, 92<<FRACBITS, 0, TTSONT[min(finalecount, 28)], sc);

				// Fade in ROBO BLAST 2 starting at tic 10.
				if (finalecount > 9)
				{
					INT32 fadeval = 0;

					// Fade between tic 10 and tic 29.
					if (finalecount < 30)
					{
						UINT8 fadecounter = 30-finalecount;
						switch(fadecounter)
						{
							case 20: case 19: fadeval = V_90TRANS; break;
							case 18: case 17: fadeval = V_80TRANS; break;
							case 16: case 15: fadeval = V_70TRANS; break;
							case 14: case 13: fadeval = V_60TRANS; break;
							case 12: case 11: fadeval = V_TRANSLUCENT; break;
							case 10: case 9: fadeval = V_40TRANS; break;
							case 8: case 7: fadeval = V_30TRANS; break;
							case 6: case 5: fadeval = V_20TRANS; break;
							case 4: case 3: fadeval = V_10TRANS; break;
						}
					}
					V_DrawSciencePatch(79<<FRACBITS, 132<<FRACBITS, fadeval, TTROBO[0], sc);

					// Draw the TWO from tic 16 to tic 31, so the TWO lands right when the screen flashes white.
					if(finalecount > 15)
						V_DrawSciencePatch(106<<FRACBITS, 118<<FRACBITS, fadeval, TTTWOT[min(finalecount-16, 15)], sc);
				}
			}

			//
			// ALACROIX CHARACTER FRAMES
			//
			// Start all animation from tic 34 (or whenever the white flash begins to fade; see below.)
			// Delay the start a bit for better music timing.
			//

#define CHARSTART 41
#define SONICSTART (CHARSTART+0)
#define SONICIDLE (SONICSTART+57)
#define SONICX 89
#define SONICY 13
#define TAILSSTART (CHARSTART+27)
#define TAILSIDLE (TAILSSTART+60)
#define TAILSX 35
#define TAILSY 19
#define KNUXSTART (CHARSTART+44)
#define KNUXIDLE (KNUXSTART+70)
#define KNUXX 167
#define KNUXY 7

			// Decide who gets to blink or not.
			// Make this decision at the END of an idle/blink cycle.
			// Upon first idle, both idle_start and idle_end will be 0.

			if (finalecount >= KNUXIDLE)
			{
				if (!knux_idle_start || finalecount - knux_idle_start >= knux_idle_end)
				{
					if (knux_blink)
					{
						knux_blink = false; // don't run the cycle twice in a row
						knux_blinked_already = true;
					}
					else if (knux_blinked_already) // or after the first non-blink cycle, either.
						knux_blinked_already = false;
					else
					{
						// make this chance higher than Sonic/Tails because Knux's idle cycle is longer
						knux_blink = !(M_RandomKey(100) % 2);
						knux_blink_twice = knux_blink ? !(M_RandomKey(100) % 5) : false;
					}
					knux_idle_start = finalecount;
				}

				knux_idle_end = knux_blink ? (knux_blink_twice ? 17 : 7) : 46;
			}

			if (finalecount >= TAILSIDLE)
			{
				if (!tails_idle_start || finalecount - tails_idle_start >= tails_idle_end)
				{
					if (tails_blink)
					{
						tails_blink = false; // don't run the cycle twice in a row
						tails_blinked_already = true;
					}
					else if (tails_blinked_already) // or after the first non-blink cycle, either.
						tails_blinked_already = false;
					else
					{
						tails_blink = !(M_RandomKey(100) % 3);
						tails_blink_twice = tails_blink ? !(M_RandomKey(100) % 5) : false;
					}
					tails_idle_start = finalecount;
				}

				// Tails does not actually have a non-blink idle cycle, but make up a number
				// so he can still blink.
				tails_idle_end = tails_blink ? (tails_blink_twice ? 17 : 7) : 30;
			}

			if (finalecount >= SONICIDLE)
			{
				if (!sonic_idle_start || finalecount - sonic_idle_start >= sonic_idle_end)
				{
					if (sonic_blink)
					{
						sonic_blink = false; // don't run the cycle twice in a row
						sonic_blinked_already = true;
					}
					else if (sonic_blinked_already) // or after the first non-blink cycle, either.
						sonic_blinked_already = false;
					else
					{
						sonic_blink = !(M_RandomKey(100) % 3);
						sonic_blink_twice = sonic_blink ? !(M_RandomKey(100) % 5) : false;
					}
					sonic_idle_start = finalecount;
				}

				sonic_idle_end = sonic_blink ? (sonic_blink_twice ? 17 : 7) : 25;
			}


			//
			// BACK TAIL LAYER
			//

			if (finalecount >= TAILSSTART)
			{
				if (finalecount >= TAILSIDLE)
				{
					//
					// Tails Back Tail Layer Idle
					//
					SINT8 taftcount = (finalecount - (TAILSIDLE)) % 41;
					if      (taftcount >= 0   && taftcount < 5  )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[0 ], sc);
					else if (taftcount >= 5   && taftcount < 9 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[1 ], sc);
					else if (taftcount >= 9   && taftcount < 12 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[2 ], sc);
					else if (taftcount >= 12  && taftcount < 14 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[3 ], sc);
					else if (taftcount >= 14  && taftcount < 17 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[4 ], sc);
					else if (taftcount >= 17  && taftcount < 21 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[5 ], sc);
					else if (taftcount >= 21  && taftcount < 24 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[6 ], sc);
					else if (taftcount >= 24  && taftcount < 25 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[7 ], sc);
					else if (taftcount >= 25  && taftcount < 28 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[8 ], sc);
					else if (taftcount >= 28  && taftcount < 31 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[9 ], sc);
					else if (taftcount >= 31  && taftcount < 35 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[10], sc);
					else if (taftcount >= 35  && taftcount < 41 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABT[11], sc);
				}
			}

			//
			// FRONT TAIL LAYER
			//

			if (finalecount >= TAILSSTART)
			{
				if (finalecount >= TAILSIDLE)
				{
					//
					// Tails Front Tail Layer Idle
					//
					SINT8 tabtcount = (finalecount - (TAILSIDLE)) % 41;
					if      (tabtcount >= 0   && tabtcount < 6  )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[0 ], sc);
					else if (tabtcount >= 6   && tabtcount < 11 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[1 ], sc);
					else if (tabtcount >= 11  && tabtcount < 15 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[2 ], sc);
					else if (tabtcount >= 15  && tabtcount < 18 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[3 ], sc);
					else if (tabtcount >= 18  && tabtcount < 19 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[4 ], sc);
					else if (tabtcount >= 19  && tabtcount < 22 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[5 ], sc);
					else if (tabtcount >= 22  && tabtcount < 27 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[6 ], sc);
					else if (tabtcount >= 27  && tabtcount < 30 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[7 ], sc);
					else if (tabtcount >= 30  && tabtcount < 31 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[8 ], sc);
					else if (tabtcount >= 31  && tabtcount < 34 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[9 ], sc);
					else if (tabtcount >= 34  && tabtcount < 37 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[10], sc);
					else if (tabtcount >= 37  && tabtcount < 41 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAFT[11], sc);
				}
			}

			//
			// BACK LAYER CHARACTERS
			//

			if (finalecount >= KNUXSTART)
			{
				if (finalecount < KNUXIDLE)
				{
					//
					// Knux Back Layer Intro
					//
					if      (finalecount >= KNUXSTART+0   && finalecount < KNUXSTART+6  )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[0 ], sc);
					else if (finalecount >= KNUXSTART+6   && finalecount < KNUXSTART+10 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[1 ], sc);
					else if (finalecount >= KNUXSTART+10  && finalecount < KNUXSTART+13 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[2 ], sc);
					else if (finalecount >= KNUXSTART+13  && finalecount < KNUXSTART+15 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[3 ], sc);
					else if (finalecount >= KNUXSTART+15  && finalecount < KNUXSTART+18 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[4 ], sc);
					else if (finalecount >= KNUXSTART+18  && finalecount < KNUXSTART+22 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[5 ], sc);
					else if (finalecount >= KNUXSTART+22  && finalecount < KNUXSTART+28 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[6 ], sc);
					else if (finalecount >= KNUXSTART+28  && finalecount < KNUXSTART+32 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[7 ], sc);
					else if (finalecount >= KNUXSTART+32  && finalecount < KNUXSTART+35 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[8 ], sc);
					else if (finalecount >= KNUXSTART+35  && finalecount < KNUXSTART+40 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[9 ], sc);
					else if (finalecount >= KNUXSTART+40  && finalecount < KNUXSTART+41 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[10], sc);
					else if (finalecount >= KNUXSTART+41  && finalecount < KNUXSTART+44 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[11], sc);
					else if (finalecount >= KNUXSTART+44  && finalecount < KNUXSTART+50 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[12], sc);
					else if (finalecount >= KNUXSTART+50  && finalecount < KNUXSTART+56 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[13], sc);
					else if (finalecount >= KNUXSTART+56  && finalecount < KNUXSTART+57 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[14], sc);
					else if (finalecount >= KNUXSTART+57  && finalecount < KNUXSTART+60 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[15], sc);
					else if (finalecount >= KNUXSTART+60  && finalecount < KNUXSTART+63 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[16], sc);
					else if (finalecount >= KNUXSTART+63  && finalecount < KNUXSTART+67 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[17], sc);
					else if (finalecount >= KNUXSTART+67  && finalecount < KNUXSTART+70 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIB[18], sc);
					// Start idle animation (frame K20-B)
				}
				else
				{
					//
					// Knux Back Layer Idle
					//
					if (!knux_blink)
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNBA[0], sc);
					else
					{
						//
						// Knux Blinking
						//
						SINT8 idlecount = finalecount - knux_idle_start;
						if      (idlecount >= 0  && idlecount < 2 )
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNBK[0], sc);
						else if (idlecount >= 2  && idlecount < 6 )
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNBK[1], sc);
						else if (idlecount >= 6  && idlecount < 7 )
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNBK[2], sc);
						// We reach this point if knux_blink_twice == true
						else if (idlecount >= 7  && idlecount < 10)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNBA[0], sc);
						else if (idlecount >= 10 && idlecount < 12)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNBK[0], sc);
						else if (idlecount >= 12 && idlecount < 16)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNBK[1], sc);
						else if (idlecount >= 16 && idlecount < 17)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNBK[2], sc);
					}
				}
			}

			if (finalecount >= TAILSSTART)
			{
				if (finalecount < TAILSIDLE)
				{
					//
					// Tails Back Layer Intro
					//
					if      (finalecount >= TAILSSTART+0   && finalecount < TAILSSTART+6  )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[0 ], sc);
					else if (finalecount >= TAILSSTART+6   && finalecount < TAILSSTART+10 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[1 ], sc);
					else if (finalecount >= TAILSSTART+10  && finalecount < TAILSSTART+12 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[2 ], sc);
					else if (finalecount >= TAILSSTART+12  && finalecount < TAILSSTART+16 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[3 ], sc);
					else if (finalecount >= TAILSSTART+16  && finalecount < TAILSSTART+22 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[4 ], sc);
					else if (finalecount >= TAILSSTART+22  && finalecount < TAILSSTART+23 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[5 ], sc);
					else if (finalecount >= TAILSSTART+23  && finalecount < TAILSSTART+26 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[6 ], sc);
					else if (finalecount >= TAILSSTART+26  && finalecount < TAILSSTART+30 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[7 ], sc);
					else if (finalecount >= TAILSSTART+30  && finalecount < TAILSSTART+35 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[8 ], sc);
					else if (finalecount >= TAILSSTART+35  && finalecount < TAILSSTART+41 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[9 ], sc);
					else if (finalecount >= TAILSSTART+41  && finalecount < TAILSSTART+43 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[10], sc);
					else if (finalecount >= TAILSSTART+43  && finalecount < TAILSSTART+47 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[11], sc);
					else if (finalecount >= TAILSSTART+47  && finalecount < TAILSSTART+51 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[12], sc);
					else if (finalecount >= TAILSSTART+51  && finalecount < TAILSSTART+53 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[13], sc);
					else if (finalecount >= TAILSSTART+53  && finalecount < TAILSSTART+56 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[14], sc);
					else if (finalecount >= TAILSSTART+56  && finalecount < TAILSSTART+60 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIB[15], sc);
					// Start idle animation (frame T17-B)
				}
				else
				{
					//
					// Tails Back Layer Idle
					//
					if (!tails_blink)
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABA[0], sc);
					else
					{
						//
						// Tails Blinking
						//
						SINT8 idlecount = finalecount - tails_idle_start;
						if      (idlecount >= +0  && idlecount < +2 )
							V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABK[0], sc);
						else if (idlecount >= +2  && idlecount < +6 )
							V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABK[1], sc);
						else if (idlecount >= +6  && idlecount < +7 )
							V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABK[2], sc);
						// We reach this point if tails_blink_twice == true
						else if (idlecount >= +7  && idlecount < +10)
							V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABA[0], sc);
						else if (idlecount >= +10 && idlecount < +12)
							V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABK[0], sc);
						else if (idlecount >= +12 && idlecount < +16)
							V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABK[1], sc);
						else if (idlecount >= +16 && idlecount < +17)
							V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTABK[2], sc);
					}
				}
			}

			if (finalecount >= SONICSTART)
			{
				if (finalecount < SONICIDLE)
				{
					//
					// Sonic Back Layer Intro
					//
					if      (finalecount >= SONICSTART+0   && finalecount < SONICSTART+6  )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[0 ], sc);
					else if (finalecount >= SONICSTART+6   && finalecount < SONICSTART+11 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[1 ], sc);
					else if (finalecount >= SONICSTART+11  && finalecount < SONICSTART+14 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[2 ], sc);
					else if (finalecount >= SONICSTART+14  && finalecount < SONICSTART+18 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[3 ], sc);
					else if (finalecount >= SONICSTART+18  && finalecount < SONICSTART+19 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[4 ], sc);
					else if (finalecount >= SONICSTART+19  && finalecount < SONICSTART+27 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[5 ], sc);
					else if (finalecount >= SONICSTART+27  && finalecount < SONICSTART+31 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[6 ], sc);
					//else if (finalecount >= SONICSTART+31  && finalecount < SONICSTART+33 )
					//  Frame is blank
					//	V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[7 ], sc);
					else if (finalecount >= SONICSTART+33  && finalecount < SONICSTART+36 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[8 ], sc);
					else if (finalecount >= SONICSTART+36  && finalecount < SONICSTART+40 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[9 ], sc);
					else if (finalecount >= SONICSTART+40  && finalecount < SONICSTART+44 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[10], sc);
					else if (finalecount >= SONICSTART+44  && finalecount < SONICSTART+47 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[11], sc);
					else if (finalecount >= SONICSTART+47  && finalecount < SONICSTART+49 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[12], sc);
					else if (finalecount >= SONICSTART+49  && finalecount < SONICSTART+50 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[13], sc);
					else if (finalecount >= SONICSTART+50  && finalecount < SONICSTART+53 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[14], sc);
					else if (finalecount >= SONICSTART+53  && finalecount < SONICSTART+57 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIB[15], sc);
					// Start idle animation (frame S17-B)
				}
				else
				{
					//
					// Sonic Back Layer Idle
					//
					if (!sonic_blink)
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOBA[0], sc);
					else
					{
						//
						// Sonic Blinking
						//
						SINT8 idlecount = finalecount - sonic_idle_start;
						if      (idlecount >= 0  && idlecount < 2 )
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOBK[0], sc);
						else if (idlecount >= 2  && idlecount < 6 )
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOBK[1], sc);
						else if (idlecount >= 6  && idlecount < 7 )
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOBK[2], sc);
						// We reach this point if sonic_blink_twice == true
						else if (idlecount >= 7  && idlecount < 10)
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOBA[0], sc);
						else if (idlecount >= 10 && idlecount < 12)
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOBK[0], sc);
						else if (idlecount >= 12 && idlecount < 16)
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOBK[1], sc);
						else if (idlecount >= 16 && idlecount < 17)
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOBK[2], sc);
					}
				}
			}

			//
			// LOGO LAYER
			//

			// After tic 34, starting when the flash fades,
			// draw the combined ribbon and SONIC ROBO BLAST 2 logo. Note the different Y value, because this
			// graphic is cropped differently from the unfurling ribbon.
			if (finalecount > 34)
				V_DrawSciencePatch(39<<FRACBITS, 93<<FRACBITS, 0, TTRBTX[0], sc);

			//
			// FRONT LAYER CHARACTERS
			//

			if (finalecount >= KNUXSTART)
			{
				if (finalecount < KNUXIDLE)
				{
					//
					// Knux Front Layer Intro
					//
					if      (finalecount >= KNUXSTART+22  && finalecount < KNUXSTART+28 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIF[6 ], sc);
					else if (finalecount >= KNUXSTART+28  && finalecount < KNUXSTART+32 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIF[7 ], sc);
					else if (finalecount >= KNUXSTART+32  && finalecount < KNUXSTART+35 )
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNIF[8 ], sc);
				}
				else
				{
					//
					// Knux Front Layer Idle
					//
					if (!knux_blink)
					{
						SINT8 idlecount = finalecount - knux_idle_start;
						if      (idlecount >= 0  && idlecount < 5 )
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[0 ], sc);
						else if (idlecount >= 5  && idlecount < 10)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[1 ], sc);
						else if (idlecount >= 10 && idlecount < 13)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[2 ], sc);
						else if (idlecount >= 13 && idlecount < 14)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[3 ], sc);
						else if (idlecount >= 14 && idlecount < 17)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[4 ], sc);
						else if (idlecount >= 17 && idlecount < 21)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[5 ], sc);
						else if (idlecount >= 21 && idlecount < 27)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[6 ], sc);
						else if (idlecount >= 27 && idlecount < 32)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[7 ], sc);
						else if (idlecount >= 32 && idlecount < 34)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[8 ], sc);
						else if (idlecount >= 34 && idlecount < 37)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[9 ], sc);
						else if (idlecount >= 37 && idlecount < 39)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[10], sc);
						else if (idlecount >= 39 && idlecount < 42)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[11], sc);
						else if (idlecount >= 42 && idlecount < 46)
							V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[12], sc);
					}
					else
						V_DrawSciencePatch(KNUXX<<FRACBITS, KNUXY<<FRACBITS, 0, TTKNDH[0 ], sc);
				}
			}

			if (finalecount >= TAILSSTART)
			{
				if (finalecount < TAILSIDLE)
				{
					//
					// Tails Front Layer Intro
					//
					if      (finalecount >= TAILSSTART+26  && finalecount < TAILSSTART+30 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIF[7 ], sc);
					else if (finalecount >= TAILSSTART+30  && finalecount < TAILSSTART+35 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIF[8 ], sc);
					else if (finalecount >= TAILSSTART+35  && finalecount < TAILSSTART+41 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIF[9 ], sc);
					else if (finalecount >= TAILSSTART+41  && finalecount < TAILSSTART+43 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIF[10], sc);
					else if (finalecount >= TAILSSTART+43  && finalecount < TAILSSTART+47 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIF[11], sc);
					else if (finalecount >= TAILSSTART+47  && finalecount < TAILSSTART+51 )
						V_DrawSciencePatch(TAILSX<<FRACBITS, TAILSY<<FRACBITS, 0, TTTAIF[12], sc);
				}
				// No Tails Front Layer Idle
			}

			if (finalecount >= SONICSTART)
			{
				if (finalecount < SONICIDLE)
				{
					//
					// Sonic Front Layer Intro
					//
					if      (finalecount >= SONICSTART+19  && finalecount < SONICSTART+27 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIF[5 ], sc);
					else if (finalecount >= SONICSTART+27  && finalecount < SONICSTART+31 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIF[6 ], sc);
					else if (finalecount >= SONICSTART+31  && finalecount < SONICSTART+33 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIF[7 ], sc);
					else if (finalecount >= SONICSTART+33  && finalecount < SONICSTART+36 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIF[8 ], sc);
					else if (finalecount >= SONICSTART+36  && finalecount < SONICSTART+40 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIF[9 ], sc);
					else if (finalecount >= SONICSTART+40  && finalecount < SONICSTART+44 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIF[10], sc);
					else if (finalecount >= SONICSTART+44  && finalecount < SONICSTART+47 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIF[11], sc);
					// ...
					else if (finalecount >= SONICSTART+53  && finalecount < SONICSTART+57 )
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSOIF[15], sc);
				}
				else
				{
					//
					// Sonic Front Layer Idle
					//
					if (!sonic_blink)
					{
						SINT8 idlecount = finalecount - sonic_idle_start;
						if      (idlecount >= 0  && idlecount < 5 )
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSODH[0], sc);
						else if (idlecount >= 5  && idlecount < 8 )
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSODH[1], sc);
						else if (idlecount >= 8  && idlecount < 9 )
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSODH[2], sc);
						else if (idlecount >= 9  && idlecount < 12)
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSODH[3], sc);
						else if (idlecount >= 12 && idlecount < 17)
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSODH[4], sc);
						else if (idlecount >= 17 && idlecount < 19)
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSODH[5], sc);
						else if (idlecount >= 19 && idlecount < 21)
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSODH[6], sc);
						else if (idlecount >= 21 && idlecount < 22)
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSODH[7], sc);
						else if (idlecount >= 22 && idlecount < 25)
							V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSODH[8], sc);
					}
					else
						V_DrawSciencePatch(SONICX<<FRACBITS, SONICY<<FRACBITS, 0, TTSODH[0], sc);
				}
			}

			// Flash at tic 30, timed to O__TITLE percussion. Hold the flash until tic 34.
			// After tic 34, fade the flash until tic 44.
			if (finalecount > 29 && finalecount < 35)
				V_DrawFadeScreen(0, 9);
			else if (finalecount > 34 && 44-finalecount > 0 && 44-finalecount < 10)
				V_DrawFadeScreen(0, 44-finalecount);

#undef CHARSTART
#undef SONICSTART
#undef SONICIDLE
#undef SONICX
#undef SONICY
#undef TAILSSTART
#undef TAILSIDLE
#undef TAILSX
#undef TAILSY
#undef KNUXSTART
#undef KNUXIDLE
#undef KNUXX
#undef KNUXY

			break;

		case TTMODE_USER:
			if (!ttuser[max(0, ttuser_count)])
			{
				if(curttloop > -1 && ttuser[curttloop])
					ttuser_count = curttloop;
				else if (ttuser[max(0, ttuser_count-1)])
					ttuser_count = max(0, ttuser_count-1);
				else
					break; // draw nothing
			}

			V_DrawSciencePatch(curttx<<FRACBITS, curtty<<FRACBITS, 0, ttuser[ttuser_count], sc);

			if (!(finalecount % max(1, curtttics)))
				ttuser_count++;
			break;
	}

luahook:
	LUAh_TitleHUD();
}

// separate animation timer for backgrounds, since we also count
// during GS_TIMEATTACK
void F_MenuPresTicker(void)
{
	curbgx += curbgxspeed;
	curbgy += curbgyspeed;
}

// (no longer) De-Demo'd Title Screen
void F_TitleScreenTicker(boolean run)
{
	if (run)
		finalecount++;

	// don't trigger if doing anything besides idling on title
	if (gameaction != ga_nothing || gamestate != GS_TITLESCREEN)
		return;

	// Execute the titlemap camera settings
	if (titlemapinaction)
	{
		thinker_t *th;
		mobj_t *mo2;
		mobj_t *cameraref = NULL;

		// If there's a Line 422 Switch Cut-Away view, don't force us.
		if (!titlemapcameraref || titlemapcameraref->type != MT_ALTVIEWMAN)
		{
			for (th = thinkercap.next; th != &thinkercap; th = th->next)
			{
				if (th->function != (actionf_p1)P_MobjThinker) // Not a mobj thinker
					continue;

				mo2 = (mobj_t *)th;

				if (!mo2)
					continue;

				if (mo2->type != MT_ALTVIEWMAN)
					continue;

				cameraref = titlemapcameraref = mo2;
				break;
			}
		}
		else
			cameraref = titlemapcameraref;

		if (cameraref)
		{
			camera.x = cameraref->x;
			camera.y = cameraref->y;
			camera.z = cameraref->z;
			camera.angle = cameraref->angle;
			camera.aiming = cameraref->cusval;
			camera.subsector = cameraref->subsector;
		}
		else
		{
			// Default behavior: Do a lil' camera spin if a title map is loaded;
			camera.angle += titlescrollxspeed*ANG1/64;
		}
	}
	// no demos to play? or, are they disabled?
	if (!cv_rollingdemos.value || !numDemos)
		return;

	// Wait for a while (for the music to finish, preferably)
	// before starting demos
	if (demoDelayLeft)
	{
		--demoDelayLeft;
		return;
	}

	// Hold up for a bit if menu or console active
	if (menuactive || CON_Ready())
	{
		demoIdleLeft = demoIdleTime;
		return;
	}

	// is it time?
	if (!(--demoIdleLeft))
	{
		char dname[9];
		lumpnum_t l;

		// prevent console spam if failed
		demoIdleLeft = demoIdleTime;

		// Replay intro when done cycling through demos
		if (curDemo == numDemos)
		{
			curDemo = 0;
			F_StartIntro();
			return;
		}

		// Setup demo name
		snprintf(dname, 9, "DEMO_%03u", ++curDemo);

		if ((l = W_CheckNumForName(dname)) == LUMPERROR)
		{
			CONS_Alert(CONS_ERROR, M_GetText("Demo lump \"%s\" doesn't exist\n"), dname);
			F_StartIntro();
			return;
		}

		titledemo = true;
		G_DoPlayDemo(dname);
	}
}

void F_TitleDemoTicker(void)
{
	keypressed = false;
}

// ==========
//  CONTINUE
// ==========
void F_StartContinue(void)
{
	I_Assert(!netgame && !multiplayer);

	if (players[consoleplayer].continues <= 0)
	{
		Command_ExitGame_f();
		return;
	}

	G_SetGamestate(GS_CONTINUING);
	gameaction = ga_nothing;

	keypressed = false;
	paused = false;
	CON_ToggleOff();

	// In case menus are still up?!!
	M_ClearMenus(true);

	S_ChangeMusicInternal("contsc", false);
	S_StopSounds();

	timetonext = TICRATE*11;
}

//
// F_ContinueDrawer
// Moved continuing out of the HUD (hack removal!!)
//
void F_ContinueDrawer(void)
{
	patch_t *contsonic;
	INT32 i, x = (BASEVIDWIDTH/2) + 4, ncontinues = players[consoleplayer].continues;
	if (ncontinues > 20)
		ncontinues = 20;

	if (imcontinuing)
		contsonic = W_CachePatchName("CONT2", PU_PATCH);
	else
		contsonic = W_CachePatchName("CONT1", PU_PATCH);

	V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);
	V_DrawCenteredString(BASEVIDWIDTH/2, 100, 0, "CONTINUE?");

	// Draw a Sonic!
	V_DrawScaledPatch((BASEVIDWIDTH - SHORT(contsonic->width))/2, 32, 0, contsonic);

	// Draw the continue markers! Show continues minus one.
	x -= ncontinues * 6;
	for (i = 0; i < ncontinues; ++i)
		V_DrawContinueIcon(x + (i*12), 140, 0, players[consoleplayer].skin, players[consoleplayer].skincolor);

	V_DrawCenteredString(BASEVIDWIDTH/2, 168, 0, va("\x82*\x80" " %02d " "\x82*\x80", timetonext/TICRATE));
}

void F_ContinueTicker(void)
{
	if (!imcontinuing)
	{
		// note the setup to prevent 2x reloading
		if (timetonext >= 0)
			timetonext--;
		if (timetonext == 0)
			Command_ExitGame_f();
	}
	else
	{
		// note the setup to prevent 2x reloading
		if (continuetime >= 0)
			continuetime--;
		if (continuetime == 0)
			G_Continue();
	}
}

boolean F_ContinueResponder(event_t *event)
{
	INT32 key = event->data1;

	if (keypressed)
		return true;

	if (timetonext >= 21*TICRATE/2)
		return false;
	if (event->type != ev_keydown)
		return false;

	// remap virtual keys (mouse & joystick buttons)
	switch (key)
	{
		case KEY_ENTER:
		case KEY_SPACE:
		case KEY_MOUSE1:
		case KEY_JOY1:
		case KEY_JOY1 + 2:
			break;
		default:
			return false;
	}

	keypressed = true;
	imcontinuing = true;
	continuetime = TICRATE;
	S_StartSound(0, sfx_itemup);
	return true;
}

// ==================
//  CUSTOM CUTSCENES
// ==================
static INT32 scenenum, cutnum;
static INT32 picxpos, picypos, picnum, pictime;
static INT32 textxpos, textypos;
static boolean cutsceneover = false;
static boolean runningprecutscene = false, precutresetplayer = false;

static void F_AdvanceToNextScene(void)
{

	if (rendermode != render_none)
	{
		F_WipeStartScreen();

		// Fade to any palette color you want.
		if (cutscenes[cutnum]->scene[scenenum].fadecolor)
		{
			V_DrawFill(0,0,BASEVIDWIDTH,BASEVIDHEIGHT,cutscenes[cutnum]->scene[scenenum].fadecolor);

			F_WipeEndScreen();
			F_RunWipe(cutscenes[cutnum]->scene[scenenum].fadeinid, true);

			F_WipeStartScreen();
		}
	}

	// Don't increment until after endcutscene check
	// (possible overflow / bad patch names from the one tic drawn before the fade)
	if (scenenum+1 >= cutscenes[cutnum]->numscenes)
	{
		F_EndCutScene();
		return;
	}
	++scenenum;

	timetonext = 0;
	stoptimer = 0;
	picnum = 0;
	picxpos = cutscenes[cutnum]->scene[scenenum].xcoord[picnum];
	picypos = cutscenes[cutnum]->scene[scenenum].ycoord[picnum];

	if (cutscenes[cutnum]->scene[scenenum].musswitch[0])
		S_ChangeMusicEx(cutscenes[cutnum]->scene[scenenum].musswitch,
			cutscenes[cutnum]->scene[scenenum].musswitchflags,
			cutscenes[cutnum]->scene[scenenum].musicloop,
			cutscenes[cutnum]->scene[scenenum].musswitchposition, 0, 0);

	// Fade to the next
	F_NewCutscene(cutscenes[cutnum]->scene[scenenum].text);

	picnum = 0;
	picxpos = cutscenes[cutnum]->scene[scenenum].xcoord[picnum];
	picypos = cutscenes[cutnum]->scene[scenenum].ycoord[picnum];
	textxpos = cutscenes[cutnum]->scene[scenenum].textxpos;
	textypos = cutscenes[cutnum]->scene[scenenum].textypos;

	animtimer = pictime = cutscenes[cutnum]->scene[scenenum].picduration[picnum];

	if (rendermode != render_none)
	{
		F_CutsceneDrawer();

		F_WipeEndScreen();
		F_RunWipe(cutscenes[cutnum]->scene[scenenum].fadeoutid, true);
	}
}

void F_EndCutScene(void)
{
	cutsceneover = true; // do this first, just in case G_EndGame or something wants to turn it back false later
	if (runningprecutscene)
	{
		if (server)
			D_MapChange(gamemap, gametype, ultimatemode, precutresetplayer, 0, true, false);
	}
	else
	{
		if (cutnum == creditscutscene-1)
			F_StartGameEvaluation();
		else if (cutnum == introtoplay-1)
			D_StartTitle();
		else if (nextmap < 1100-1)
			G_NextLevel();
		else
			G_EndGame();
	}
}

void F_StartCustomCutscene(INT32 cutscenenum, boolean precutscene, boolean resetplayer)
{
	if (!cutscenes[cutscenenum])
		return;

	G_SetGamestate(GS_CUTSCENE);

	gameaction = ga_nothing;
	paused = false;
	CON_ToggleOff();

	F_NewCutscene(cutscenes[cutscenenum]->scene[0].text);

	cutsceneover = false;
	runningprecutscene = precutscene;
	precutresetplayer = resetplayer;

	scenenum = picnum = 0;
	cutnum = cutscenenum;
	picxpos = cutscenes[cutnum]->scene[0].xcoord[0];
	picypos = cutscenes[cutnum]->scene[0].ycoord[0];
	textxpos = cutscenes[cutnum]->scene[0].textxpos;
	textypos = cutscenes[cutnum]->scene[0].textypos;

	pictime = cutscenes[cutnum]->scene[0].picduration[0];

	keypressed = false;
	finalecount = 0;
	timetonext = 0;
	animtimer = cutscenes[cutnum]->scene[0].picduration[0]; // Picture duration
	stoptimer = 0;

	if (cutscenes[cutnum]->scene[0].musswitch[0])
		S_ChangeMusicEx(cutscenes[cutnum]->scene[0].musswitch,
			cutscenes[cutnum]->scene[0].musswitchflags,
			cutscenes[cutnum]->scene[0].musicloop,
			cutscenes[cutnum]->scene[scenenum].musswitchposition, 0, 0);
	else
		S_StopMusic();
}

//
// F_CutsceneDrawer
//
void F_CutsceneDrawer(void)
{
	V_DrawFill(0,0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);

	if (cutscenes[cutnum]->scene[scenenum].picname[picnum][0] != '\0')
	{
		if (cutscenes[cutnum]->scene[scenenum].pichires[picnum])
			V_DrawSmallScaledPatch(picxpos, picypos, 0,
				W_CachePatchName(cutscenes[cutnum]->scene[scenenum].picname[picnum], PU_PATCH));
		else
			V_DrawScaledPatch(picxpos,picypos, 0,
				W_CachePatchName(cutscenes[cutnum]->scene[scenenum].picname[picnum], PU_PATCH));
	}


	V_DrawString(textxpos, textypos, 0, cutscene_disptext);
}

void F_CutsceneTicker(void)
{
	INT32 i;

	// Online clients tend not to instantly get the map change, so just wait
	// and don't send 30 of them.
	if (cutsceneover)
		return;

	// advance animation
	finalecount++;
	cutscene_boostspeed = 0;


	for (i = 0; i < MAXPLAYERS; i++)
	{
		if (netgame && i != serverplayer && !IsPlayerAdmin(i))
			continue;

		if (players[i].cmd.buttons & BT_USE)
		{
			keypressed = false;
			cutscene_boostspeed = 1;
			if (timetonext)
				timetonext = 2;
		}
	}

	if (animtimer)
	{
		animtimer--;
		if (animtimer <= 0)
		{
			if (picnum < 7 && cutscenes[cutnum]->scene[scenenum].picname[picnum+1][0] != '\0')
			{
				picnum++;
				picxpos = cutscenes[cutnum]->scene[scenenum].xcoord[picnum];
				picypos = cutscenes[cutnum]->scene[scenenum].ycoord[picnum];
				pictime = cutscenes[cutnum]->scene[scenenum].picduration[picnum];
				animtimer = pictime;
			}
			else
				timetonext = 2;
		}
	}

	if (timetonext)
		--timetonext;

	if (++stoptimer > 2 && timetonext == 1)
		F_AdvanceToNextScene();
	else if (!timetonext && !F_WriteText())
		timetonext = 5*TICRATE + 1;
}

boolean F_CutsceneResponder(event_t *event)
{
	if (cutnum == introtoplay-1)
		return F_IntroResponder(event);

	return false;
}

// ================
//  WAITINGPLAYERS
// ================

void F_StartWaitingPlayers(void)
{
	wipegamestate = GS_TITLESCREEN; // technically wiping from title screen
	finalecount = 0;
}

void F_WaitingPlayersTicker(void)
{
	if (paused)
		return;

	finalecount++;

	// dumb hack, only start the music on the 1st tick so if you instantly go into the map you aren't hearing a tic of music
	if (finalecount == 2)
		S_ChangeMusicInternal("chrsel", true);
}

void F_WaitingPlayersDrawer(void)
{
	const char *waittext1 = "You will join";
	const char *waittext2 = "next level...";

	V_DrawFill(0, 0, BASEVIDWIDTH, BASEVIDHEIGHT, 31);

	V_DrawCreditString((160 - (V_CreditStringWidth(waittext1)>>1))<<FRACBITS, 48<<FRACBITS, 0, waittext1);
	V_DrawCreditString((160 - (V_CreditStringWidth(waittext2)>>1))<<FRACBITS, 64<<FRACBITS, 0, waittext2);
}
