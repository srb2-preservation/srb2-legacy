#include "mapload.h"
#include "w_wad.h"
#include "z_zone.h"
#include "console.h"
#include "m_misc.h"		// Token reading stuff.
#include "m_fixed.h"
#include "fastcmp.h"	// Fast token comparison.

#include "doomtype.h"
#include "doomdata.h"	// mapvertex_t, maplinedef_t...
#include "r_defs.h"		// vertex_t, sector_t, side_t...

#include "byteptr.h"

#include "r_state.h"
#include "p_setup.h"

#include "i_video.h" // rendermode

#include "r_sky.h" // skyflatnum, SKYFLATNAME

#include "dehacked.h" // get_number

#include "p_spec.h" // P_SetupLevelFlatAnims

#include "m_bbox.h" // Bounding box stuff
#include "r_main.h" // R_PointToAngle2()

// Store positions for relevant map data spread through TEXTMAP, we will need them later.
// TODO: Find the real max values for the map data.
UINT32 mapthingsPos[UINT16_MAX];
UINT32 linesPos[UINT16_MAX];
UINT32 sidesPos[UINT16_MAX];
UINT32 vertexesPos[UINT16_MAX];
UINT32 sectorsPos[UINT16_MAX];

#define MAXLEVELFLATS 256
levelflat_t *foundflats;


static boolean MDAT_TextmapCount (UINT8 *data, size_t size)
{
	char *token;

	// Determine total amount of map data in TEXTMAP.
	// Look for namespace at the beginning.
	if (fastcmp(M_GetToken((char *)data), "namespace"))
		// Check if namespace is valid.
		if (fastcmp((token = M_GetToken(NULL)), "srb2"))
		{
			while ((token = M_GetToken(NULL)) != NULL && M_GetTokenPos() < size)
			{
				// Avoid anything inside bracketed stuff, only look for external keywords.
				// Assuming there's only one level of bracket nesting.
				if (fastcmp(token, "{"))
					while (!fastcmp(token, "}"))
						token = M_GetToken(NULL);

				// Check for valid fields.
				else if (fastcmp(token, "thing"))
					mapthingsPos[nummapthings++] = M_GetTokenPos();
				else if (fastcmp(token, "linedef"))
					linesPos[numlines++] = M_GetTokenPos();
				else if (fastcmp(token, "sidedef"))
					sidesPos[numsides++] = M_GetTokenPos();
				else if (fastcmp(token, "vertex"))
					vertexesPos[numvertexes++] = M_GetTokenPos();
				else if (fastcmp(token, "sector"))
					sectorsPos[numsectors++] = M_GetTokenPos();
				else
					CONS_Alert(CONS_NOTICE, "Unknown field '%s'.\n", token);
			}
		}
		else
		{
			CONS_Alert(CONS_WARNING, "Invalid namespace '%s', only 'srb2' is supported.\n", token);
			return false;
		}
	else
	{
		CONS_Alert(CONS_WARNING, "No namespace at beginning of lump!\n");
		return false;
	}
	return true;
}




/** Auxiliary function for ParseUDMFStuff.
  *
  * \param Vertex number.
  * \param Parameter string.
  */
static void TextmapVertex(UINT32 i, char *param)
{
	if (fastcmp(param, "x"))
		vertexes[i].x = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "y"))
		vertexes[i].y = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
}

/** Auxiliary function for ParseUDMFStuff.
  *
  * \param Sector number.
  * \param Parameter string.
  */
static void TextmapSector(UINT32 i, char *param)
{
	if (fastcmp(param, "heightfloor"))
		sectors[i].floorheight = atol(M_GetToken(NULL)) << FRACBITS;
	else if (fastcmp(param, "heightceiling"))
		sectors[i].ceilingheight = atol(M_GetToken(NULL)) << FRACBITS;
	if (fastcmp(param, "texturefloor"))
		sectors[i].floorpic = P_AddLevelFlat(M_GetToken(NULL), foundflats);
	else if (fastcmp(param, "textureceiling"))
		sectors[i].ceilingpic = P_AddLevelFlat(M_GetToken(NULL), foundflats);
	else if (fastcmp(param, "lightlevel"))
		sectors[i].lightlevel = atol(M_GetToken(NULL));
	// TODO: Separate the 4 fields.
	else if (fastcmp(param, "special"))
		sectors[i].special = atol(M_GetToken(NULL));
	else if (fastcmp(param, "id"))
		sectors[i].tag = atol(M_GetToken(NULL));
	else if (fastcmp(param, "xpanningfloor"))
		sectors[i].floor_xoffs = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "ypanningfloor"))
		sectors[i].floor_yoffs = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "xpanningceiling"))
		sectors[i].ceiling_xoffs = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "ypanningceiling"))
		sectors[i].ceiling_yoffs = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "rotationfloor"))
		sectors[i].floorpic_angle = FixedAngle(FLOAT_TO_FIXED(atof(M_GetToken(NULL))));
	else if (fastcmp(param, "rotationceiling"))
		sectors[i].ceilingpic_angle = FixedAngle(FLOAT_TO_FIXED(atof(M_GetToken(NULL))));
//	else if (fastcmp(param, "gravity"))
//		sectors[i].gravity = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "flip") && fastcmp("true", M_GetToken(NULL)))
		sectors[i].verticalflip = true;
//	else if (fastcmp(param, "heatwave") && fastcmp("true", M_GetToken(NULL)))
//		sectors[i].udmfflags |= SFU_HEATWAVE;
	else if (fastcmp(param, "xscalefloor"))
		sectors[i].floor_scale = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "xscaleceiling"))
		sectors[i].ceiling_scale = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
}

/** Auxiliary function for ParseUDMFStuff.
  *
  * \param Side number.
  * \param Parameter string.
  */
static void TextmapSide(UINT32 i, char *param)
{
	if (fastcmp(param, "offsetx"))
		sides[i].textureoffset = atol(M_GetToken(NULL))<<FRACBITS;
	else if (fastcmp(param, "offsety"))
		sides[i].rowoffset = atol(M_GetToken(NULL))<<FRACBITS;
	else if (fastcmp(param, "texturetop"))
		sides[i].toptexture = R_TextureNumForName(M_GetToken(NULL));
	else if (fastcmp(param, "texturebottom"))
		sides[i].bottomtexture = R_TextureNumForName(M_GetToken(NULL));
	else if (fastcmp(param, "texturemiddle"))
		sides[i].midtexture = R_TextureNumForName(M_GetToken(NULL));
	else if (fastcmp(param, "sector"))
		sides[i].sector = &sectors[atol(M_GetToken(NULL))];
	else if (fastcmp(param, "repeatcnt"))
		sides[i].repeatcnt = atol(M_GetToken(NULL));
	else if (fastcmp(param, "scalex_top"))
		sides[i].scalex_top = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "scaley_top"))
		sides[i].scaley_top = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "scalex_mid"))
		sides[i].scalex_mid = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "scaley_mid"))
		sides[i].scaley_mid = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "scalex_bottom"))
		sides[i].scalex_bot = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "scaley_bottom"))
		sides[i].scaley_bot = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "offsetx_top"))
		sides[i].offsetx_top = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "offsety_top"))
		sides[i].offsety_top = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "offsetx_mid"))
		sides[i].offsetx_mid = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "offsety_mid"))
		sides[i].offsety_mid = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "offsetx_bottom"))
		sides[i].offsetx_bot = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "offsety_bottom"))
		sides[i].offsety_bot = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
}

/** Auxiliary function for ParseUDMFStuff.
  *
  * \param Line number.
  * \param Parameter string.
  */
static void TextmapLine(UINT32 i, char *param)
{
	if (fastcmp(param, "id"))
		lines[i].tag = atol(M_GetToken(NULL));
	else if (fastcmp(param, "special"))
		lines[i].special = atol(M_GetToken(NULL));
	else if (fastcmp(param, "v1"))
		lines[i].v1 = (vertex_t*) (size_t) atol(M_GetToken(NULL));
	else if (fastcmp(param, "v2"))
		lines[i].v2 = (vertex_t*) (size_t) atol(M_GetToken(NULL));
//	else if (fastcmp(param, "v1"))
//		lines[i].v1 = &vertexes[atol(M_GetToken(NULL))];
//	else if (fastcmp(param, "v2"))
//		lines[i].v2 = &vertexes[atol(M_GetToken(NULL))];
	else if (fastcmp(param, "sidefront"))
		lines[i].sidenum[0] = atol(M_GetToken(NULL));
	else if (fastcmp(param, "sideback"))
		lines[i].sidenum[1] = atol(M_GetToken(NULL));
/*	else if (fastncmp(param, "arg", 3) && strlen(param) > 3)
	{
		if (fastcmp(param + 4, "str"))
		{
			size_t argnum = param[3] - '0';
			if (argnum < 0 || argnum >= NUMLINESTRINGARGS)
			{
				CONS_Debug(DBG_SETUP, "Invalid linedef string argument number: %d\n", argnum);
				return;
			}
			char* token = M_GetToken(NULL);
			lines[i].stringargs[argnum] = Z_Malloc(strlen(token)+1, PU_LEVEL, NULL);
			M_Memcpy(lines[i].stringargs[argnum], token, strlen(token) + 1);
		}
		else
		{
			size_t argnum = atol(param + 3);
			if (argnum < 0 || argnum >= NUMLINEARGS)
			{
				CONS_Debug(DBG_SETUP, "Invalid linedef argument number: %d\n", argnum);
				return;
			}
			lines[i].args[argnum] = atol(M_GetToken(NULL));
		}
	}*/
//	else if (fastcmp(param, "alpha"))
//		lines[i].alpha = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
//	else if (fastcmp(param, "executordelay"))
//		lines[i].executordelay = atol(M_GetToken(NULL));
	// Flags
	else if (fastcmp(param, "blocking") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_IMPASSIBLE;
	else if (fastcmp(param, "blockmonsters") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_BLOCKMONSTERS;
	else if (fastcmp(param, "twosided") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_TWOSIDED;
	else if (fastcmp(param, "dontpegtop") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_DONTPEGTOP;
	else if (fastcmp(param, "dontpegbottom") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_DONTPEGBOTTOM;
	else if (fastcmp(param, "skewtd") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_EFFECT1;
	else if (fastcmp(param, "noclimb") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_NOCLIMB;
	else if (fastcmp(param, "noskew") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_EFFECT2;
	else if (fastcmp(param, "midpeg") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_EFFECT3;
	else if (fastcmp(param, "midsolid") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_EFFECT4;
	else if (fastcmp(param, "wrapmidtex") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_EFFECT5;
	else if (fastcmp(param, "nosonic") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_NOSONIC;
	else if (fastcmp(param, "notails") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_NOSONIC;
	else if (fastcmp(param, "noknux") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_NOKNUX;
	else if (fastcmp(param, "bouncy") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_BOUNCY;
	else if (fastcmp(param, "transfer") && fastcmp("true", M_GetToken(NULL)))
		lines[i].flags |= ML_TFERLINE;
//	else if (fastcmp(param, "fogwall") && fastcmp("true", M_GetToken(NULL)))
//		lines[i].udmfflags |= MLU_FOGWALL;
//	else if (fastcmp(param, "horizoneffect") && fastcmp("true", M_GetToken(NULL)))
//		lines[i].udmfflags |= MLU_HORIZON;
//	else if (fastcmp(param, "notriggerorder") && fastcmp("true", M_GetToken(NULL)))
//		lines[i].udmfflags |= MLU_NOTRIGGERORDER;
}

/** Auxiliary function for ParseUDMFStuff.
  *
  * \param Thing number.
  * \param Parameter string.
  */
static void TextmapThing(UINT32 i, char *param)
{
/*	if (fastcmp(param, "id"))
		mapthings[i].tag = atol(M_GetToken(NULL));
	else*/ if (fastcmp(param, "x"))
		mapthings[i].x = atol(M_GetToken(NULL));
	else if (fastcmp(param, "y"))
		mapthings[i].y = atol(M_GetToken(NULL));
	else if (fastcmp(param, "height"))
		mapthings[i].z = atol(M_GetToken(NULL));
	else if (fastcmp(param, "angle"))
		mapthings[i].angle = atol(M_GetToken(NULL));
//	else if (fastcmp(param, "pitch"))
//		mapthings[i].pitch = atol(M_GetToken(NULL));
//	else if (fastcmp(param, "roll"))
//		mapthings[i].roll = atol(M_GetToken(NULL));
	else if (fastcmp(param, "type"))
		mapthings[i].type = atol(M_GetToken(NULL));
	else if (fastcmp(param, "scale"))
		mapthings[i].scale = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
	else if (fastcmp(param, "scalex"))
		mapthings[i].scale = FLOAT_TO_FIXED(atof(M_GetToken(NULL)));
/*	else if (fastcmp(param, "spawntrigger"))
		mapthings[i].spawntrigger = atol(M_GetToken(NULL));
	else if (fastcmp(param, "seetrigger"))
		mapthings[i].seetrigger = atol(M_GetToken(NULL));
	else if (fastcmp(param, "paintrigger"))
		mapthings[i].paintrigger = atol(M_GetToken(NULL));
	else if (fastcmp(param, "meleetrigger"))
		mapthings[i].meleetrigger = atol(M_GetToken(NULL));
	else if (fastcmp(param, "missiletrigger"))
		mapthings[i].missiletrigger = atol(M_GetToken(NULL));
	else if (fastcmp(param, "deathtrigger"))
		mapthings[i].deathtrigger = atol(M_GetToken(NULL));
	else if (fastcmp(param, "xdeathtrigger"))
		mapthings[i].xdeathtrigger = atol(M_GetToken(NULL));
	else if (fastcmp(param, "raisetrigger"))
		mapthings[i].raisetrigger = atol(M_GetToken(NULL));
	else if (fastncmp(param, "param", 5) && strlen(param) > 5)
	{
		if (fastcmp(param + 6, "str"))
		{
			size_t argnum = param[5] - '0';
			if (argnum < 0 || argnum >= NUMLINESTRINGARGS)
			{
				CONS_Debug(DBG_SETUP, "Invalid Thing string parameter number: %d\n", argnum);
				return;
			}
			char* token = M_GetToken(NULL);
			mapthings[i].stringparams[argnum] = Z_Malloc(strlen(token) + 1, PU_LEVEL, NULL);
			M_Memcpy(mapthings[i].stringparams[argnum], token, strlen(token) + 1);
		}
		else
		{
			size_t paramnum = atol(param + 5);
			if (paramnum < 0 || paramnum >= NUMTHINGPARAMS)
			{
				CONS_Debug(DBG_SETUP, "Invalid Thing parameter number: %d\n", paramnum);
				return;
			}
			mapthings[i].params[paramnum] = atol(M_GetToken(NULL));
		}
	}*/


	// Flags
	else if (fastcmp(param, "extra") && fastcmp("true", M_GetToken(NULL)))
		mapthings[i].options |= 1;
	else if (fastcmp(param, "flip") && fastcmp("true", M_GetToken(NULL)))
		mapthings[i].options |= MTF_OBJECTFLIP;
	else if (fastcmp(param, "special") && fastcmp("true", M_GetToken(NULL)))
		mapthings[i].options |= MTF_OBJECTSPECIAL;
	else if (fastcmp(param, "ceiling") && fastcmp("true", M_GetToken(NULL)))
		mapthings[i].options |= MTF_OBJECTFLIP;
}

/** From a given position table, run a specified parser function through a {}-encapsuled text.
  *
  * \param Positions array of encapsulated data of a given type, in the TEXTMAP data.
  * \param Structure number (mapthings, sectors, ...).
  * \param Parser function pointer.
  */
static void TextmapParse(UINT32 dataPos[], size_t num, void (*parser)(UINT32, char *))
{
	UINT32 i;
	char *token;
	for (i = 0; i < num; i++)
	{
		M_SetTokenPos(dataPos[i]);
		if (fastcmp(M_GetToken(NULL), "{"))
			while (!fastcmp(token = M_GetToken(NULL), "}"))
				parser(i, token);
		else
			CONS_Alert(CONS_WARNING, "Invalid UDMF data capsule!\n");
	}
}

/** Initialize map data; common to all map formats.
  *
  * Sets up fields that aren't necessarily specified by the map data but are required to be initialized by the engine.
  * Common shared defaults are also set in this function.
  */
void MDAT_Initialize ()
{
	UINT32 i/*, j*/;

	line_t		*ld = lines;
	side_t		*sd = sides;
	sector_t	*ss = sectors;
	mapthing_t	*mt = mapthings;

	for (i = 0, ld = lines; i < numlines; i++, ld++)
	{
	// Initialization.
#ifdef WALLSPLATS
		ld->splats = NULL;
#endif
	// Defaults.
	}

	for (i = 0, sd = sides; i < numsides; i++, sd++)
	{
	// Initialization.

	// Defaults.
		sd->scalex_top = sd->scaley_top = sd->scalex_mid = sd->scaley_mid = sd->scalex_bot = sd->scaley_bot = FRACUNIT;
		sd->offsetx_top = sd->offsety_top = sd->offsetx_mid = sd->offsety_mid = sd->offsetx_bot = sd->offsety_bot = 0;
	}

	for (i = 0, ss = sectors; i < numsectors; i++, ss++)
	{
	// Initialization.
		ss->nexttag = ss->firsttag = -1;
		ss->spawn_nexttag = ss->spawn_firsttag = -1;

		memset(&ss->soundorg, 0, sizeof(ss->soundorg));
		ss->validcount = 0;

		ss->thinglist = NULL;
		ss->touching_thinglist = NULL;
		ss->preciplist = NULL;
		ss->touching_preciplist = NULL;

		ss->floordata = NULL;
		ss->ceilingdata = NULL;
		ss->lightingdata = NULL;

		ss->linecount = 0;
		ss->lines = NULL;

		ss->heightsec = -1;
		ss->camsec = -1;
		ss->floorlightsec = -1;
		ss->ceilinglightsec = -1;
		ss->crumblestate = 0;
		ss->ffloors = NULL;
		ss->lightlist = NULL;
		ss->numlights = 0;
		ss->attached = NULL;
		ss->attachedsolid = NULL;
		ss->numattached = 0;
		ss->maxattached = 1;
		ss->moved = true;
		ss->bottommap = ss->midmap = ss->topmap = -1;
		ss->gravity = NULL;
		ss->cullheight = NULL;
//		ss->gravity = FRACUNIT;
		ss->verticalflip = false;
//		ss->udmfflags = 0;
		ss->flags = 0;
		ss->flags |= SF_FLIPSPECIAL_FLOOR;

		ss->floorspeed = 0;
		ss->ceilspeed = 0;

#ifdef HWRENDER // ----- for special tricks with HW renderer -----
		ss->pseudoSector = false;
		ss->virtualFloor = false;
		ss->virtualCeiling = false;
		ss->sectorLines = NULL;
		ss->stackList = NULL;
		ss->lineoutLength = -1.0l;
#endif // ----- end special tricks -----

	// Defaults.
		ss->extra_colormap = NULL;

		ss->floor_xoffs = ss->ceiling_xoffs = ss->floor_yoffs = ss->ceiling_yoffs = 0;
		ss->spawn_flr_xoffs = ss->spawn_ceil_xoffs = ss->spawn_flr_yoffs = ss->spawn_ceil_yoffs = 0;
		ss->floorpic_angle = ss->ceilingpic_angle = 0;
		ss->spawn_flrpic_angle = ss->spawn_ceilpic_angle = 0;

		ss->floor_scale = FRACUNIT;
		ss->ceiling_scale = FRACUNIT;
	}

	for (i = 0, mt = mapthings; i < nummapthings; i++, mt++)
	{
	// Initialization.

	// Defaults.
		// Binary has no access to these.
		mt->scale = FRACUNIT;
		/*mt->pitch = mt->roll = 0;
		mt->scale = FRACUNIT;
		for (j = 0; j < NUMTHINGPARAMS; j++)
			mt->params[j] = 0;
		mt->spawntrigger = 0;
		mt->seetrigger = 0;
		mt->paintrigger = 0;
		mt->meleetrigger = 0;
		mt->missiletrigger = 0;
		mt->deathtrigger = 0;
		mt->xdeathtrigger = 0;
		mt->raisetrigger = 0;*/
	}
}

/** Set map defaults in UDMF format.
  *
  * Due to UDMF's format specs, some fields are assumed to have default values.
  * Therefore, it is necessary to set said fields beforehand.
  */
void MDAT_TextmapDefaults ()
{
	UINT32 i/*, j*/;

	line_t *ld;
	side_t *sd;
	sector_t *sc;
	mapthing_t *mt;

	for (i = 0, ld = lines; i < numlines; i++, ld++)
	{
		ld->tag = 0;
		ld->special = 0;
		ld->sidenum[1] = 0xffff;
//		ld->alpha = FRACUNIT;
//		for (j = 0; j < NUMLINEARGS; j++)
//			ld->args[j] = 0;
//		ld->executordelay = 0;
//		ld->udmfflags = 0;
	}

	for (i = 0, sd = sides; i < numsides; i++, sd++)
	{
		sd->rowoffset = 0;
		sd->textureoffset = 0;

		sd->toptexture = R_TextureNumForName("-");
		sd->midtexture = R_TextureNumForName("-");
		sd->bottomtexture = R_TextureNumForName("-");
		sd->repeatcnt = 0;


//		sd->colormap_data = NULL;
	}

	for (i = 0, sc = sectors; i < numsectors; i++, sc++)
	{
		sc->floorheight = 0;
		sc->ceilingheight = 0;

		/// \todo Doing this for floorpic and ceilingpic doesn't work, probably.
		sc->floorpic = 0;
		sc->ceilingpic = 0;
		sc->lightlevel = 0;
		sc->special = 0;
		sc->tag = 0;
	}

	for (i = 0, mt = mapthings; i < nummapthings; i++, mt++)
	{
		mt->z = 0;
		mt->angle = /*mt->pitch = mt->roll =*/ 0;
//		mt->scale = FRACUNIT;
//		for (j = 0; j < NUMTHINGPARAMS; j++)
//			mt->params[j] = 0;
//		mt->spawntrigger = 0;
//		mt->seetrigger = 0;
//		mt->paintrigger = 0;
//		mt->meleetrigger = 0;
//		mt->missiletrigger = 0;
//		mt->deathtrigger = 0;
//		mt->xdeathtrigger = 0;
//		mt->raisetrigger = 0;
	}
}

void MDAT_LoadLinedefs (UINT8 *data)
{
	maplinedef_t *mld = (maplinedef_t *)data;
	line_t *ld = lines;
	size_t i;

	for (i = 0; i < numlines; i++, mld++, ld++)
	{
		ld->v1 = (vertex_t*) (size_t) SHORT(mld->v1);
		ld->v2 = (vertex_t*) (size_t) SHORT(mld->v2);
		ld->flags = SHORT(mld->flags);
		ld->special = SHORT(mld->special);
		ld->tag = SHORT(mld->tag);
		ld->sidenum[0] = SHORT(mld->sidenum[0]);
		ld->sidenum[1] = SHORT(mld->sidenum[1]);

		if (ld->sidenum[0] != 0xffff && ld->special)
			sides[ld->sidenum[0]].special = ld->special;
		if (ld->sidenum[1] != 0xffff && ld->special)
			sides[ld->sidenum[1]].special = ld->special;
	}
}

/** Loads mapthings from binary data.
  *
  * \param *data Data pointer.
  */
void MDAT_LoadMapthings(UINT8 *data)
{
	mapthing_t *mt = mapthings;;
	size_t i;

	// Spawn axis points first so they are
	// at the front of the list for fast searching.
	for (i = 0; i < nummapthings; i++, mt++)
	{
		mt->x = READINT16(data);
		mt->y = READINT16(data);
		mt->angle = READINT16(data);
		mt->type = READUINT16(data);
		mt->options = READUINT16(data);
		mt->extrainfo = (UINT8)(mt->type >> 12);
		mt->type &= 4095;
	}
}

/** Sets up the ingame sectors from binary data.
  *
  * \param *data Data pointer.
  */
void MDAT_LoadSectors(UINT8 *data)
{
	mapsector_t *ms = (mapsector_t *)data;
	sector_t *ss = sectors;
	size_t i;

	for (i = 0; i < numsectors; i++, ss++, ms++)
	{
		ss->floorheight = SHORT(ms->floorheight)<<FRACBITS;
		ss->ceilingheight = SHORT(ms->ceilingheight)<<FRACBITS;

		ss->floorpic = P_AddLevelFlat(ms->floorpic, foundflats);
		ss->ceilingpic = P_AddLevelFlat(ms->ceilingpic, foundflats);

		ss->lightlevel = SHORT(ms->lightlevel);
		ss->special = SHORT(ms->special);
		ss->tag = SHORT(ms->tag);
	}
}

/** Loads the vertexes for a level.
  *
  * \param lump VERTEXES lump number.
  */
void MDAT_LoadVertexes(UINT8 *data)
{
	mapvertex_t *ml = (mapvertex_t *)data;
	vertex_t *li = vertexes;
	size_t i;

	// Copy and convert vertex coordinates, internal representation as fixed.
	for (i = 0; i < numvertexes; i++, li++, ml++)
	{
		li->x = SHORT(ml->x)<<FRACBITS;
		li->y = SHORT(ml->y)<<FRACBITS;
	}
}

static void MDAT_LoadSidedefs(void *data)
{
	UINT16 i;
	INT32 num;

	for (i = 0; i < numsides; i++)
	{
		register mapsidedef_t *msd = (mapsidedef_t *)data + i;
		register side_t *sd = sides + i;
		register sector_t *sec;
		UINT16 sector_num = SHORT(msd->sector);

		sd->textureoffset = SHORT(msd->textureoffset)<<FRACBITS;
		sd->rowoffset = SHORT(msd->rowoffset)<<FRACBITS;

		if (sector_num >= numsectors) /* cph 2006/09/30 - catch out-of-range sector numbers; use sector 0 instead */
		{
			CONS_Debug(DBG_SETUP, "MDAT_LoadSidedefs: sidedef %u has out-of-range sector num %u\n", i, sector_num);
			sector_num = 0;
		}
		sd->sector = sec = &sectors[sector_num];

		// refined to allow colormaps to work as wall textures if invalid as colormaps
		// but valid as textures.
		switch (sd->special)
		{
			case 63: // variable colormap via 242 linedef
			case 606: //SoM: 4/4/2000: Just colormap transfer
				// SoM: R_CreateColormap will only create a colormap in software mode...
				// Perhaps we should just call it instead of doing the calculations here.
				if (rendermode == render_soft || rendermode == render_none)
				{
					if (msd->toptexture[0] == '#' || msd->bottomtexture[0] == '#')
					{
						sec->midmap = R_CreateColormap(msd->toptexture, msd->midtexture,
							msd->bottomtexture);
						sd->toptexture = sd->bottomtexture = 0;
					}
					else
					{
						if ((num = R_CheckTextureNumForName(msd->toptexture)) == -1)
							sd->toptexture = 0;
						else
							sd->toptexture = num;
						if ((num = R_CheckTextureNumForName(msd->midtexture)) == -1)
							sd->midtexture = 0;
						else
							sd->midtexture = num;
						if ((num = R_CheckTextureNumForName(msd->bottomtexture)) == -1)
							sd->bottomtexture = 0;
						else
							sd->bottomtexture = num;
					}
					break;
				}
#ifdef HWRENDER
				else
				{
					// for now, full support of toptexture only
					if ((msd->toptexture[0] == '#' && msd->toptexture[1] && msd->toptexture[2] && msd->toptexture[3] && msd->toptexture[4] && msd->toptexture[5] && msd->toptexture[6])
						|| (msd->bottomtexture[0] == '#' && msd->bottomtexture[1] && msd->bottomtexture[2] && msd->bottomtexture[3] && msd->bottomtexture[4] && msd->bottomtexture[5] && msd->bottomtexture[6]))
					{
						char *col;

						sec->midmap = R_CreateColormap(msd->toptexture, msd->midtexture,
							msd->bottomtexture);
						sd->toptexture = sd->bottomtexture = 0;
#define HEX2INT(x) (x >= '0' && x <= '9' ? x - '0' : x >= 'a' && x <= 'f' ? x - 'a' + 10 : x >= 'A' && x <= 'F' ? x - 'A' + 10 : 0)
#define ALPHA2INT(x) (x >= 'a' && x <= 'z' ? x - 'a' : x >= 'A' && x <= 'Z' ? x - 'A' : x >= '0' && x <= '9' ? 25 : 0)
						sec->extra_colormap = &extra_colormaps[sec->midmap];

						if (msd->toptexture[0] == '#' && msd->toptexture[1] && msd->toptexture[2] && msd->toptexture[3] && msd->toptexture[4] && msd->toptexture[5] && msd->toptexture[6])
						{
							col = msd->toptexture;

							sec->extra_colormap->rgba =
								(HEX2INT(col[1]) << 4) + (HEX2INT(col[2]) << 0) +
								(HEX2INT(col[3]) << 12) + (HEX2INT(col[4]) << 8) +
								(HEX2INT(col[5]) << 20) + (HEX2INT(col[6]) << 16);

							// alpha
							if (msd->toptexture[7])
								sec->extra_colormap->rgba += (ALPHA2INT(col[7]) << 24);
							else
								sec->extra_colormap->rgba += (25 << 24);
						}
						else
							sec->extra_colormap->rgba = 0;

						if (msd->bottomtexture[0] == '#' && msd->bottomtexture[1] && msd->bottomtexture[2] && msd->bottomtexture[3] && msd->bottomtexture[4] && msd->bottomtexture[5] && msd->bottomtexture[6])
						{
							col = msd->bottomtexture;

							sec->extra_colormap->fadergba =
								(HEX2INT(col[1]) << 4) + (HEX2INT(col[2]) << 0) +
								(HEX2INT(col[3]) << 12) + (HEX2INT(col[4]) << 8) +
								(HEX2INT(col[5]) << 20) + (HEX2INT(col[6]) << 16);

							// alpha
							if (msd->bottomtexture[7])
								sec->extra_colormap->fadergba += (ALPHA2INT(col[7]) << 24);
							else
								sec->extra_colormap->fadergba += (25 << 24);
						}
						else
							sec->extra_colormap->fadergba = 0x19000000; // default alpha, (25 << 24)
#undef ALPHA2INT
#undef HEX2INT
					}
					else
					{
						if ((num = R_CheckTextureNumForName(msd->toptexture)) == -1)
							sd->toptexture = 0;
						else
							sd->toptexture = num;

						if ((num = R_CheckTextureNumForName(msd->midtexture)) == -1)
							sd->midtexture = 0;
						else
							sd->midtexture = num;

						if ((num = R_CheckTextureNumForName(msd->bottomtexture)) == -1)
							sd->bottomtexture = 0;
						else
							sd->bottomtexture = num;
					}
					break;
				}
#endif

			case 413: // Change music
			{
				char process[8+1];

				sd->toptexture = sd->midtexture = sd->bottomtexture = 0;
				if (msd->bottomtexture[0] != '-' || msd->bottomtexture[1] != '\0')
				{
					M_Memcpy(process,msd->bottomtexture,8);
					process[8] = '\0';
					sd->bottomtexture = get_number(process)-1;
				}
				M_Memcpy(process,msd->toptexture,8);
				process[8] = '\0';
				sd->text = Z_Malloc(7, PU_LEVEL, NULL);

				// If they type in O_ or D_ and their music name, just shrug,
				// then copy the rest instead.
				if ((process[0] == 'O' || process[0] == 'D') && process[7])
					M_Memcpy(sd->text, process+2, 6);
				else // Assume it's a proper music name.
					M_Memcpy(sd->text, process, 6);
				sd->text[6] = 0;
				break;
			}

			case 4: // Speed pad parameters
			case 414: // Play SFX
			{
				sd->toptexture = sd->midtexture = sd->bottomtexture = 0;
				if (msd->toptexture[0] != '-' || msd->toptexture[1] != '\0')
				{
					char process[8+1];
					M_Memcpy(process,msd->toptexture,8);
					process[8] = '\0';
					sd->toptexture = get_number(process);
				}
				break;
			}

			case 9: // Mace parameters
			case 14: // Bustable block parameters
			case 15: // Fan particle spawner parameters
			case 425: // Calls P_SetMobjState on calling mobj
			case 434: // Custom Power
			case 442: // Calls P_SetMobjState on mobjs of a given type in the tagged sectors
			{
				char process[8*3+1];
				memset(process,0,8*3+1);
				sd->toptexture = sd->midtexture = sd->bottomtexture = 0;
				if (msd->toptexture[0] == '-' && msd->toptexture[1] == '\0')
					break;
				else
					M_Memcpy(process,msd->toptexture,8);
				if (msd->midtexture[0] != '-' || msd->midtexture[1] != '\0')
					M_Memcpy(process+strlen(process), msd->midtexture, 8);
				if (msd->bottomtexture[0] != '-' || msd->bottomtexture[1] != '\0')
					M_Memcpy(process+strlen(process), msd->bottomtexture, 8);
				sd->toptexture = get_number(process);
				break;
			}

			case 443: // Calls a named Lua function
			{
				char process[8*3+1];
				memset(process,0,8*3+1);
				sd->toptexture = sd->midtexture = sd->bottomtexture = 0;
				if (msd->toptexture[0] == '-' && msd->toptexture[1] == '\0')
					break;
				else
					M_Memcpy(process,msd->toptexture,8);
				if (msd->midtexture[0] != '-' || msd->midtexture[1] != '\0')
					M_Memcpy(process+strlen(process), msd->midtexture, 8);
				if (msd->bottomtexture[0] != '-' || msd->bottomtexture[1] != '\0')
					M_Memcpy(process+strlen(process), msd->bottomtexture, 8);
				sd->text = Z_Malloc(strlen(process)+1, PU_LEVEL, NULL);
				M_Memcpy(sd->text, process, strlen(process)+1);
				break;
			}

			default: // normal cases
				if (msd->toptexture[0] == '#')
				{
					char *col = msd->toptexture;
					sd->toptexture = sd->bottomtexture =
						((col[1]-'0')*100 + (col[2]-'0')*10 + col[3]-'0') + 1;
					sd->midtexture = R_TextureNumForName(msd->midtexture);
				}
				else
				{
					sd->midtexture = R_TextureNumForName(msd->midtexture);
					sd->toptexture = R_TextureNumForName(msd->toptexture);
					sd->bottomtexture = R_TextureNumForName(msd->bottomtexture);
				}
				break;
		}

		// Default binary.
		sd->scalex_top = sd->scaley_top = sd->scalex_mid = sd->scaley_mid = sd->scalex_bot = sd->scaley_bot = FRACUNIT;
		CONS_Printf("%d :  %d, %d, %d\n", i, sd->scalex_top, sd->scalex_mid, sd->scalex_bot);
	}
}


/** Loads the map data from a virtual resource.
 */
void MDAT_LoadMapdata(virtres_t* virt)
{
	virtlump_t* virtvertexes = NULL, * virtsectors = NULL, * virtsidedefs = NULL, * virtlinedefs = NULL, * virtthings = NULL;
	virtlump_t* vtextmap = vres_Find(virt, "TEXTMAP");
	UDMF = vtextmap != NULL ? true : false;

	// Count map data.
	if (UDMF)
	{
		nummapthings = 0;
		numlines = 0;
		numsides = 0;
		numvertexes = 0;
		numsectors = 0;

		MDAT_TextmapCount(vtextmap->data, vtextmap->size);
	}
	else
	{
		virtthings		= vres_Find(virt, "THINGS");
		virtvertexes	= vres_Find(virt, "VERTEXES");
		virtsectors		= vres_Find(virt, "SECTORS");
		virtsidedefs	= vres_Find(virt, "SIDEDEFS");
		virtlinedefs	= vres_Find(virt, "LINEDEFS");

		numvertexes		= virtvertexes->size/ sizeof (mapvertex_t);
		numsectors		= virtsectors->size / sizeof (mapsector_t);
		numsides		= virtsidedefs->size/ sizeof (mapsidedef_t);
		numlines		= virtlinedefs->size/ sizeof (maplinedef_t);
		nummapthings	= virtthings->size	/ (5 * sizeof (INT16));
	}

	if (numvertexes <= 0)
		I_Error("Level has no vertices");
	if (numsectors <= 0)
		I_Error("Level has no sectors");
	if (numsides <= 0)
		I_Error("Level has no sidedefs");
	if (numlines <= 0)
		I_Error("Level has no linedefs");

	vertexes	= Z_Calloc(numvertexes * sizeof (*vertexes), PU_LEVEL, NULL);
	sectors		= Z_Calloc(numsectors * sizeof (*sectors), PU_LEVEL, NULL);
	sides		= Z_Calloc(numsides * sizeof (*sides), PU_LEVEL, NULL);
	lines 		= Z_Calloc(numlines * sizeof (*lines), PU_LEVEL, NULL);
	mapthings	= Z_Calloc(nummapthings * sizeof (*mapthings), PU_LEVEL, NULL);

	MDAT_Initialize();

	// Allocate a big chunk of memory as big as our MAXLEVELFLATS limit.
	//Fab : FIXME: allocate for whatever number of flats - 512 different flats per level should be plenty
	foundflats = calloc(MAXLEVELFLATS, sizeof (*foundflats));
	if (foundflats == NULL)
		I_Error("Ran out of memory while loading sector flats\n");

	numlevelflats = 0;

	// Load map data.
	if (UDMF)
	{
		MDAT_TextmapDefaults(); // UDMF-specific defaults (since some fields may get omitted).
		TextmapParse(vertexesPos,	numvertexes,	TextmapVertex);
		TextmapParse(sectorsPos,	numsectors,		TextmapSector);
		TextmapParse(sidesPos,		numsides,		TextmapSide);
		TextmapParse(linesPos,		numlines,		TextmapLine);
		TextmapParse(mapthingsPos,	nummapthings,	TextmapThing);
	}
	else
	{
		MDAT_LoadVertexes	(virtvertexes->data);
		MDAT_LoadSectors	(virtsectors->data);
		MDAT_LoadLinedefs	(virtlinedefs->data);
		MDAT_LoadSidedefs	(virtsidedefs->data);
		MDAT_LoadMapthings	(virtthings->data);
	}

	// set the sky flat num
	skyflatnum = P_AddLevelFlat(SKYFLATNAME, foundflats);

	// copy table for global usage
	levelflats = M_Memcpy(Z_Calloc(numlevelflats * sizeof (*levelflats), PU_LEVEL, NULL), foundflats, numlevelflats * sizeof (levelflat_t));
	free(foundflats);

	// search for animated flats and set up
	P_SetupLevelFlatAnims();

	R_ClearTextureNumCache(true);
}

static void MDAT_SetupLinesVertexes (void)
{
	line_t *ld = lines;
	vertex_t *v1, *v2;
	size_t i;

	for (i = 0; i < numlines; i++, ld++)
	{
		ld->v1 = v1 = &vertexes[(size_t) ld->v1];
		ld->v2 = v2 = &vertexes[(size_t) ld->v2];
		ld->dx = v2->x - v1->x;
		ld->dy = v2->y - v1->y;

		if (!ld->dx)
			ld->slopetype = ST_VERTICAL;
		else if (!ld->dy)
			ld->slopetype = ST_HORIZONTAL;
		else if ((ld->dy > 0) == (ld->dx > 0))
			ld->slopetype = ST_POSITIVE;
		else
			ld->slopetype = ST_NEGATIVE;

		if (v1->x < v2->x)
		{
			ld->bbox[BOXLEFT] = v1->x;
			ld->bbox[BOXRIGHT] = v2->x;
		}
		else
		{
			ld->bbox[BOXLEFT] = v2->x;
			ld->bbox[BOXRIGHT] = v1->x;
		}

		if (v1->y < v2->y)
		{
			ld->bbox[BOXBOTTOM] = v1->y;
			ld->bbox[BOXTOP] = v2->y;
		}
		else
		{
			ld->bbox[BOXBOTTOM] = v2->y;
			ld->bbox[BOXTOP] = v1->y;
		}
	}
}

/** Computes the length of a seg in fracunits.
  * This is needed for splats.
  *
  * \param seg Seg to compute length for.
  * \return Length in fracunits.
  */
fixed_t P_SegLength(seg_t *seg)
{
	fixed_t dx, dy;

	// make a vector (start at origin)
	dx = seg->v2->x - seg->v1->x;
	dy = seg->v2->y - seg->v1->y;

	return FixedHypot(dx, dy);
}

#ifdef HWRENDER
/** Computes the length of a seg in float.
  * Required by OpenGL.
  *
  * \param seg Seg to compute length for.
  * \return Length in fracunits.
  */
static inline float P_SegLengthf(seg_t *seg)
{
	float dx, dy;

	// make a vector (start at origin)
	dx = FIXED_TO_FLOAT(seg->v2->x - seg->v1->x);
	dy = FIXED_TO_FLOAT(seg->v2->y - seg->v1->y);

	return (float)hypot(dx, dy);
}
#endif

/** Loads the SEGS resource from a level.
  *
  * \param data Raw segs data
  */
static void P_LoadSegs(UINT8 *data)
{
	size_t i;
	INT32 linedef, side;
	mapseg_t *ml = (mapseg_t *)data;
	seg_t *li = segs;
	line_t *ldef;

	for (i = 0; i < numsegs; i++, li++, ml++)
	{
		li->v1 = &vertexes[SHORT(ml->v1)];
		li->v2 = &vertexes[SHORT(ml->v2)];

#ifdef HWRENDER // not win32 only 19990829 by Kin
		// used for the hardware render
		if (rendermode != render_soft && rendermode != render_none)
		{
			li->flength = P_SegLengthf(li);
			//Hurdler: 04/12/2000: for now, only used in hardware mode
			li->lightmaps = NULL; // list of static lightmap for this seg
		}
		li->pv1 = li->pv2 = NULL;
#endif

		li->angle = (SHORT(ml->angle))<<FRACBITS;
		li->offset = (SHORT(ml->offset))<<FRACBITS;
		linedef = SHORT(ml->linedef);
		ldef = &lines[linedef];
		li->linedef = ldef;
		li->side = side = SHORT(ml->side);
		li->sidedef = &sides[ldef->sidenum[side]];
		li->frontsector = sides[ldef->sidenum[side]].sector;
		if (ldef-> flags & ML_TWOSIDED)
			li->backsector = sides[ldef->sidenum[side^1]].sector;
		else
			li->backsector = 0;

		li->numlights = 0;
		li->rlights = NULL;
	}
}

/** Loads the SSECTORS resource from a level.
  *
  * \param data Raw subsector data
  */
static inline void P_LoadSubsectors(void *data)
{
	mapsubsector_t *ms = (mapsubsector_t *)data;
	subsector_t *ss = subsectors;
	size_t i;

	for (i = 0; i < numsubsectors; i++, ss++, ms++)
	{
		ss->sector = NULL;
		ss->numlines = SHORT(ms->numsegs);
		ss->firstline = SHORT(ms->firstseg);
#ifdef FLOORSPLATS
		ss->splats = NULL;
#endif
		ss->validcount = 0;
	}
}


/** Loads nodes from binary data.
  *
  * \param *data Data pointer.
  */
static void P_LoadNodes(UINT8 *data)
{
	size_t i;
	UINT8 j, k;
	mapnode_t *mn;
	node_t *no;

	mn = (mapnode_t *)data;
	no = nodes;

	for (i = 0; i < numnodes; i++, no++, mn++)
	{
		no->x = SHORT(mn->x)<<FRACBITS;
		no->y = SHORT(mn->y)<<FRACBITS;
		no->dx = SHORT(mn->dx)<<FRACBITS;
		no->dy = SHORT(mn->dy)<<FRACBITS;
		for (j = 0; j < 2; j++)
		{
			no->children[j] = SHORT(mn->children[j]);
			for (k = 0; k < 4; k++)
				no->bbox[j][k] = SHORT(mn->bbox[j][k])<<FRACBITS;
		}
	}
}

// Auxiliary function: Shrink node ID from 32-bit to 16-bit.
static UINT16 ShrinkNodeID(UINT32 x) {
	UINT16 mask = (x >> 16) & 0xC000;
	UINT16 result = x;
	return result | mask;
}

boolean MDAT_LoadNodes (virtres_t* virt)
{
	virtlump_t* virtssectors	= vres_Find(virt, "SSECTORS");
	virtlump_t* virtnodes		= vres_Find(virt, "NODES");
	virtlump_t* virtsegs		= vres_Find(virt, "SEGS");

	nodetype_t nodetype = NT_UNSUPPORTED;

	if (UDMF)
	{
		UDMF = true;
		virtnodes = vres_Find(virt, "ZNODES");
		if (!memcmp(virtnodes->data, "XGLN", 4))
			nodetype = NT_XGLN;
		else if (!memcmp(virtnodes->data, "XGL3", 4))
			nodetype = NT_XGL3;
	}
	else
	{
		UDMF = false;
		// Detect nodes.
		if (!virtsegs || !virtsegs->size)
		{
			// Possibly ZDoom extended nodes: SSECTORS is empty, NODES has a signature.
			if (!virtssectors || !virtssectors->size)
			{
				if (!memcmp(virtnodes->data, "XNOD", 4))
					nodetype = NT_XNOD;
				else if (!memcmp(virtnodes->data, "ZNOD", 4)) // Compressed variant.
					nodetype = NT_ZNOD;
			}
			// Possibly GL nodes: NODES ignored, SSECTORS takes precedence as nodes lump, (It is confusing yeah) and has a signature.
			else
			{
				if (!memcmp(virtssectors->data, "XGLN", 4))
				{
					virtnodes = virtssectors;
					nodetype = NT_XGLN;
				}
				else if (!memcmp(virtssectors->data, "ZGLN", 4)) // Compressed variant.
				{
					virtnodes = virtssectors;
					nodetype = NT_ZGLN;
				}
				else if (!memcmp(virtssectors->data, "XGL3", 4)) // Compressed variant.
				{
					virtnodes = virtssectors;
					nodetype = NT_ZGL3;
				}
			}
		}
		else // Traditional binary map format.
			nodetype = NT_BINARY;
	}

	switch (nodetype)
	{
	case NT_BINARY:
	{
		MDAT_SetupLinesVertexes();

		numsubsectors	= virtssectors->size/ sizeof (mapsubsector_t);
		numnodes		= virtnodes->size	/ sizeof (mapnode_t);
		numsegs			= virtsegs->size	/ sizeof (mapseg_t);

		if (numsubsectors <= 0)
			I_Error("Level has no subsectors (did you forget to run it through a nodesbuilder?)");
		if (numnodes <= 0)
			I_Error("Level has no nodes");
		if (numsegs <= 0)
			I_Error("Level has no segs");

		subsectors	= Z_Calloc(numsubsectors * sizeof (*subsectors), PU_LEVEL, NULL);
		nodes		= Z_Calloc(numnodes * sizeof (*nodes), PU_LEVEL, NULL);
		segs		= Z_Calloc(numsegs * sizeof (*segs), PU_LEVEL, NULL);

		P_LoadSubsectors(virtssectors->data);
		P_LoadNodes		(virtnodes->data);
		P_LoadSegs		(virtsegs->data);
	}
		break;
	case NT_XNOD:
	case NT_XGLN:
	case NT_XGL3:
	{
		size_t i, j, k;
		UINT8* data = virtnodes->data;
		data += 4;

		// Extra vertexes - used for rendering, irrelevant gamelogic-wise
		UINT32 orivtx = READUINT32(data);
		UINT32 xtrvtx = READUINT32(data);

		if (numvertexes != orivtx)
		{
			CONS_Printf("Vertex count in map data (%d) and nodes' (%d) differ!\n", numvertexes, orivtx);
			return false;
		}
		numvertexes+= xtrvtx;

		/// I am a villain. -Nev3r
		vertexes = Z_Realloc(vertexes, numvertexes * sizeof (*vertexes), PU_LEVEL, NULL);
		MDAT_SetupLinesVertexes();

		for (i = orivtx; i < numvertexes; i++)
		{
			vertexes[i].x = READFIXED(data);
			vertexes[i].y = READFIXED(data);
		}

		// Subsectors
		numsubsectors	= READUINT32(data);
		subsectors		= Z_Calloc(numsubsectors * sizeof (*subsectors), PU_LEVEL, NULL);

		for (i = 0; i < numsubsectors; i++)
			subsectors[i].numlines = READUINT32(data);

		// Segs
		numsegs		= READUINT32(data);
		segs		= Z_Calloc(numsegs * sizeof (*segs), PU_LEVEL, NULL);

		for (i = 0, k = 0; i < numsubsectors; i++)
		{
			subsectors[i].firstline = k;
			for (j = 0; j < subsectors[i].numlines; j++, k++)
			{
				if (nodetype == NT_XGLN)
				{
					UINT16 linenum;
					UINT32 vert;
					vert = READUINT32(data);
					segs[k].v1 = &vertexes[vert];
					if (j == 0)
						segs[k + subsectors[i].numlines - 1].v2 = &vertexes[vert];
					else
						segs[k - 1].v2 = segs[k].v1;
					data += 4;// partner; can be ignored by software renderer;
					linenum = READUINT16(data);
					if (linenum == 0xFFFF)
					{
						segs[k].glseg = true;
						//segs[k].linedef = 0xFFFFFFFF;
						segs[k].linedef = &lines[0]; /// \todo Not meant to do this.
					}
					else
					{
						segs[k].glseg = false;
						segs[k].linedef = &lines[linenum];
					}
					segs[k].side = READUINT8(data);
				}
				else if (nodetype == NT_XGL3)
				{
					UINT32 linenum;
					UINT32 vert;
					vert = READUINT32(data);
					segs[k].v1 = &vertexes[vert];
					if (j == 0)
						segs[k + subsectors[i].numlines - 1].v2 = &vertexes[vert];
					else
						segs[k - 1].v2 = segs[k].v1;
					data += 4;// partner; can be ignored by software renderer;
					linenum = READUINT32(data);
					if (linenum == 0xFFFFFFFF)
					{
						segs[k].glseg = true;
						//segs[k].linedef = 0xFFFFFFFF;
						segs[k].linedef = &lines[0]; /// \todo Not meant to do this.
					}
					else
					{
						segs[k].glseg = false;
						segs[k].linedef = &lines[linenum];
					}
					segs[k].side = READUINT8(data);
				}
				else if (nodetype == NT_XNOD)
				{
					segs[k].v1		= &vertexes[READUINT32(data)];
					segs[k].v2		= &vertexes[READUINT32(data)];
					segs[k].linedef	= &lines[READUINT16(data)];
					segs[k].side	= READUINT8(data);
				}
			}
		}

		{
			INT32 side;
			seg_t *li;

			for (i = 0, li = segs; i < numsegs; i++, li++)
			{
				vertex_t *v1 = li->v1;
				vertex_t *v2 = li->v2;
				li->angle = R_PointToAngle2(v1->x, v1->y, v2->x, v2->y);
				//li->angle = 0;
				li->offset = FixedHypot(v1->x - li->linedef->v1->x, v1->y - li->linedef->v1->y);
				side = li->side;
				li->sidedef = &sides[li->linedef->sidenum[side]];

				li->frontsector = sides[li->linedef->sidenum[side]].sector;
				if (li->linedef->flags & ML_TWOSIDED)
					li->backsector = sides[li->linedef->sidenum[side^1]].sector;
				else
					li->backsector = 0;

				segs[i].numlights = 0;
				segs[i].rlights = NULL;
			}
		}

		// Nodes
		numnodes = READINT32(data);
		nodes		= Z_Calloc(numnodes * sizeof (*nodes), PU_LEVEL, NULL);
		if (nodetype == NT_XGL3)
		{
			UINT32 x, y, dx, dy;
			UINT32 c0, c1;
			node_t *mn;
			for (i = 0, mn = nodes; i < numnodes; i++, mn++)
			{
				// Splitter.
				x = READINT32(data);
				y = READINT32(data);
				dx = READINT32(data);
				dy = READINT32(data);
				mn->x = x;
				mn->y = y;
				mn->dx = dx;
				mn->dy = dy;

				// Bounding boxes and children.
				for (j = 0; j < 2; j++)
					for (k = 0; k < 4; k++)
						mn->bbox[j][k] = READINT16(data)<<FRACBITS;
				c0 = READUINT32(data);
				c1 = READUINT32(data);
				mn->children[0] = ShrinkNodeID(c0); /// \todo Use UINT32 for node children in a future, instead?
				mn->children[1] = ShrinkNodeID(c1);
			}
		}
		else
		{
			UINT32 c0, c1;
			node_t *mn;
			for (i = 0, mn = nodes; i < numnodes; i++, mn++)
			{
				// Splitter.
				mn->x = READINT16(data)<<FRACBITS;
				mn->y = READINT16(data)<<FRACBITS;
				mn->dx = READINT16(data)<<FRACBITS;
				mn->dy = READINT16(data)<<FRACBITS;
				// Bounding boxes and children.
				for (j = 0; j < 2; j++)
					for (k = 0; k < 4; k++)
						mn->bbox[j][k] = READINT16(data)<<FRACBITS;
				c0 = READUINT32(data);
				c1 = READUINT32(data);
				mn->children[0] = ShrinkNodeID(c0); /// \todo Use UINT32 for node children in a future, instead?
				mn->children[1] = ShrinkNodeID(c1);
			}
		}
	}
		break;
	default:
		CONS_Printf("Unsupported node format.\n");
		return false;
	}
	return true;
}
