// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2012-2018 by Sally "TehRealSalt" Cochenour.
// Copyright (C) 2012-2016 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  discord.h
/// \brief Discord Rich Presence handling

#ifdef HAVE_DISCORDRPC

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

#include <time.h>

#include "i_system.h"
#include "d_clisrv.h"
#include "d_netcmd.h"
#include "i_net.h"
#include "g_game.h"
#include "p_tick.h"
#include "m_menu.h" // gametype_cons_t
#include "r_things.h" // skins
#include "mserv.h" // ms_RoomId
#include "z_zone.h"
#include "m_random.h" // P_GetInitSeed

#include "discord.h"
#include "doomdef.h"

// Feel free to provide your own, if you care enough to create another Discord app for this :P
#define DISCORD_APPID "1407619178755784715"

// length of IP strings
#define IP_SIZE 16

consvar_t cv_discordrp = CVAR_INIT("discordrp", "On", NULL, CV_SAVE|CV_CALL, CV_OnOff, DRPC_UpdatePresence);

discordRequest_t *discordRequestList = NULL;

#ifdef HAVE_CURL
struct SelfIPbuffer
{
	CURL *curl;
	char *pointer;
	size_t length;
};
static char self_ip[IP_SIZE];
#endif

/*--------------------------------------------------
	static char *DRPC_XORIPString(const char *input)

		Simple XOR encryption/decryption. Not complex or
		very secretive because we aren't sending anything
		that isn't easily accessible via our Master Server anyway.
--------------------------------------------------*/
static char *DRPC_XORIPString(const char *input)
{
	const UINT32 is = (P_GetInitSeed() % UINT8_MAX);
	const UINT8 xor[IP_SIZE] = {is, is+106, is-64, is+251, is-207, is+16, is-28, is+78, is-4, is+118, is-46, is+76, is-153, is+45, is-91, is+100};
	char *output = malloc(sizeof(char) * IP_SIZE);
	UINT8 i;

	for (i = 0; i < IP_SIZE; i++)
	{
		output[i] = input[i] ^ xor[i];
	}

	return output;
}

/*--------------------------------------------------
	static void DRPC_HandleReady(const DiscordUser *user)

		Callback function, ran when the game connects to Discord.

	Input Arguments:-
		user - Struct containing Discord user info.

	Return:-
		None
--------------------------------------------------*/
static void DRPC_HandleReady(const DiscordUser *user)
{
	CONS_Printf("Discord: connected to %s#%s (%s)\n", user->username, user->discriminator, user->userId);
}

/*--------------------------------------------------
	static void DRPC_HandleDisconnect(int err, const char *msg)

		Callback function, ran when disconnecting from Discord.

	Input Arguments:-
		err - Error type
		msg - Error message

	Return:-
		None
--------------------------------------------------*/
static void DRPC_HandleDisconnect(int err, const char *msg)
{
	CONS_Printf("Discord: disconnected (%d: %s)\n", err, msg);
}

/*--------------------------------------------------
	static void DRPC_HandleError(int err, const char *msg)

		Callback function, ran when Discord outputs an error.

	Input Arguments:-
		err - Error type
		msg - Error message

	Return:-
		None
--------------------------------------------------*/
static void DRPC_HandleError(int err, const char *msg)
{
	CONS_Alert(CONS_WARNING, "Discord error (%d: %s)\n", err, msg);
}

/*--------------------------------------------------
	static void DRPC_HandleJoin(const char *secret)

		Callback function, ran when Discord wants to
		connect a player to the game via a channel invite
		or a join request.

	Input Arguments:-
		secret - Value that links you to the server.

	Return:-
		None
--------------------------------------------------*/
static void DRPC_HandleJoin(const char *secret)
{
	char *ip = DRPC_XORIPString(secret);
	CONS_Printf("Connecting to %s via Discord\n", ip);
	COM_BufAddText(va("connect \"%s\"\n", ip));
	free(ip);
}

/*--------------------------------------------------
	static void DRPC_HandleJoinRequest(const DiscordUser *requestUser)

		Callback function, ran when Discord wants to
		ask the player if another Discord user can join
		or not.

	Input Arguments:-
		requestUser - DiscordUser struct for the user trying to connect.

	Return:-
		None
--------------------------------------------------*/
static void DRPC_HandleJoinRequest(const DiscordUser *requestUser)
{
	discordRequest_t *append = discordRequestList;
	discordRequest_t *newRequest = Z_Calloc(sizeof(discordRequest_t), PU_STATIC, NULL);

	// Discord requests exprie after 30 seconds
	newRequest->timer = (30*TICRATE)-1;

	newRequest->username = Z_Calloc(344+1+8, PU_STATIC, NULL);
	snprintf(newRequest->username, 344+1+8, "%s#%s",
		requestUser->username,
		requestUser->discriminator
	);

	newRequest->userID = Z_Calloc(32, PU_STATIC, NULL);
	snprintf(newRequest->userID, 32, "%s", requestUser->userId);

	if (append != NULL)
	{
		discordRequest_t *prev = NULL;

		while (append != NULL)
		{
			// CHECK FOR DUPES!! Ignore any that already exist from the same user.
			if (!strcmp(newRequest->userID, append->userID))
			{
				Discord_Respond(newRequest->userID, DISCORD_REPLY_IGNORE);
				DRPC_RemoveRequest(newRequest);
				return;
			}
			prev = append;
			append = append->next;
		}

		newRequest->prev = prev;
		prev->next = newRequest;
	}
	else
	{
		discordRequestList = newRequest;
	}
}

/*--------------------------------------------------
	void DRPC_RemoveRequest(discordRequest_t *removeRequest)

		See header file for description.
--------------------------------------------------*/
void DRPC_RemoveRequest(discordRequest_t *removeRequest)
{
	if (removeRequest->prev != NULL)
	{
		removeRequest->prev->next = removeRequest->next;
	}

	if (removeRequest->next != NULL)
	{
		removeRequest->next->prev = removeRequest->prev;

		if (removeRequest == discordRequestList)
		{
			discordRequestList = removeRequest->next;
		}
	}
	else
	{
		if (removeRequest == discordRequestList)
		{
			discordRequestList = NULL;
		}
	}

	Z_Free(removeRequest->username);
	Z_Free(removeRequest->userID);
	Z_Free(removeRequest);
}

/*--------------------------------------------------
	void DRPC_Init(void)

		See header file for description.
--------------------------------------------------*/
void DRPC_Init(void)
{
	DiscordEventHandlers handlers;
	memset(&handlers, 0, sizeof(handlers));

	handlers.ready = DRPC_HandleReady;
	handlers.disconnected = DRPC_HandleDisconnect;
	handlers.errored = DRPC_HandleError;
	handlers.joinGame = DRPC_HandleJoin;
	handlers.joinRequest = DRPC_HandleJoinRequest;

	Discord_Initialize(DISCORD_APPID, &handlers, 1, NULL);
	I_AddExitFunc(Discord_Shutdown);
	DRPC_UpdatePresence();
}

#ifdef HAVE_CURL
/*--------------------------------------------------
	static size_t DRPC_WriteServerIP(char *s, size_t size, size_t n, void *userdata)

		Writing function for use with curl. Only intended to be used with simple text.

	Input Arguments:-
		s - Data to write
		size - Always 1.
		n - Length of data
		userdata - Passed in from CURLOPT_WRITEDATA, intended to be SelfIPbuffer

	Return:-
		Number of bytes wrote in this pass.
--------------------------------------------------*/
static size_t DRPC_WriteServerIP(char *s, size_t size, size_t n, void *userdata)
{
	struct SelfIPbuffer *buffer;
	size_t newlength;

	buffer = userdata;

	newlength = buffer->length + size*n;
	buffer->pointer = realloc(buffer->pointer, newlength+1);

	memcpy(buffer->pointer + buffer->length, s, size*n);

	buffer->pointer[newlength] = '\0';
	buffer->length = newlength;

	return size*n;
}
#endif

/*--------------------------------------------------
	static const char *DRPC_GetServerIP(void)

		Retrieves the IP address of the server that you're
		connected to. Will attempt to use curl for getting your
		own IP address, if it's not yours.
--------------------------------------------------*/
static const char *DRPC_GetServerIP(void)
{
	const char *address; 

	// If you're connected
	if (I_GetNodeAddress && (address = I_GetNodeAddress(servernode)) != NULL)
	{
		if (strcmp(address, "self"))
		{
			// We're not the server, so we could successfully get the IP!
			// No need to do anything else :)
			return address; 
		}
	}

#ifdef HAVE_CURL
	// This is a little bit goofy, but
	// there's practically no good way to get your own public IP address,
	// so we've gotta break out curl for this :V
	if (!self_ip[0])
	{
		CURL *curl;

		curl_global_init(CURL_GLOBAL_ALL);
		curl = curl_easy_init();

		if (curl)
		{
			// The API to get your public IP address from.
			// Picked because it's stupid simple and it's been up for a long time.
			const char *api = "http://ip4only.me/api/"; 

			struct SelfIPbuffer buffer;
			CURLcode success;

			buffer.length = 0;
			buffer.pointer = malloc(buffer.length+1);
			buffer.pointer[0] = '\0';

			curl_easy_setopt(curl, CURLOPT_URL, api);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DRPC_WriteServerIP);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);

			success = curl_easy_perform(curl);

			if (success == CURLE_OK)
			{
				char *tmp;
				tmp = strtok(buffer.pointer, ",");

				if (!strcmp(tmp, "IPv4")) // ensure correct type of IP
				{
					tmp = strtok(NULL, ",");
					strncpy(self_ip, tmp, IP_SIZE); // Yay, we have the IP :)
				}
			}

			free(buffer.pointer);
			curl_easy_cleanup(curl);
		}

		curl_global_cleanup();
	}

	if (self_ip[0])
		return self_ip;
	else
#endif
		return NULL; // Could not get your IP for whatever reason, so we cannot do Discord invites
}


/*--------------------------------------------------
	void DRPC_UpdatePresence(void)

		See header file for description.
--------------------------------------------------*/
void DRPC_UpdatePresence(void)
{
	char detailstr[48+1];

	char mapimg[8+1];
	char mapname[5+21+21+2+1];

	char charimg[4+SKINNAMESIZE+1];
	char charname[11+SKINNAMESIZE+1];

	DiscordRichPresence discordPresence;
	memset(&discordPresence, 0, sizeof(discordPresence));

	if (!cv_discordrp.value)
	{
		// User doesn't want to show their game information, so update with empty presence.
		// This just shows that they're playing SRB2Kart. (If that's too much, then they should disable game activity :V)
		Discord_UpdatePresence(&discordPresence);
		return;
	}

/*
#ifdef DEVELOP
	// This way, we can use the invite feature in-dev, but not have snoopers seeing any potential secrets! :P
	discordPresence.largeImageKey = "miscdevelop";
	discordPresence.largeImageText = "Nope.";
	discordPresence.state = "Shh! We're testing!";

	Discord_UpdatePresence(&discordPresence);
	return;
#endif // DEVELOP
*/

	// Server info
	if (netgame)
	{
		const char *join;

		switch (ms_RoomId)
		{
			case -1: discordPresence.state = "Private"; break; // Private server
			case 33: discordPresence.state = "Standard"; break;
			case 28: discordPresence.state = "Casual"; break;
			case 38: discordPresence.state = "Custom Gametypes"; break;
			case 31: discordPresence.state = "OLDC"; break;
			default: discordPresence.state = "Unknown Room"; break; // HOW
		}

		discordPresence.partyId = server_context; // Thanks, whoever gave us Mumble support, for implementing the EXACT thing Discord wanted for this field!
		discordPresence.partySize = D_NumPlayers(); // Players in server
		discordPresence.partyMax = cv_maxplayers.value; // Max players (TODO: use another variable to hold this, so maxplayers doesn't have to be a netvar!)

		// Grab the host's IP for joining.
		if (cv_allownewplayer.value && ((join = DRPC_GetServerIP()) != NULL))
		{
			char *xorjoin = DRPC_XORIPString(join);
			discordPresence.joinSecret = xorjoin;
			free(xorjoin);
		}
	}
	else
	{
		// Offline info
		if (Playing())
			discordPresence.state = "Offline";
		else if (demoplayback)
			discordPresence.state = "Watching Replay";
		else
			discordPresence.state = "Menu";
	}

	// Gametype info
		if ((gamestate == GS_LEVEL || gamestate == GS_INTERMISSION) && Playing())
	{
		if (modeattacking)
			discordPresence.details = "Record Attack";
		else
		{
			snprintf(detailstr, 48, "%s",
				gametype_cons_t[gametype].strvalue
			);
			discordPresence.details = detailstr;
		}
	}

	if ((gamestate == GS_LEVEL || gamestate == GS_INTERMISSION) // Map info
		&& !(demoplayback))
	{
		if ((gamemap >= 1 && gamemap <= 60) // supported race maps
			|| (gamemap >= 136 && gamemap <= 164) && !(savemoddata)) // supported battle maps
		{
			snprintf(mapimg, 8, "%s%s", G_BuildMapName(gamemap), "P");
			strlwr(mapimg);
			discordPresence.largeImageKey = mapimg; // Map image
		}
		else if (mapheaderinfo[gamemap-1]->menuflags & LF2_HIDEINMENU)
		{
			// Hell map, use the method that got you here :P
			discordPresence.largeImageKey = "miscdice";
		}
		else
		{
			// This is probably a custom map!
			discordPresence.largeImageKey = "mapcustom";
		}

		if (mapheaderinfo[gamemap-1]->menuflags & LF2_HIDEINMENU)
		{
			// Hell map, hide the name
			discordPresence.largeImageText = "Map: ???";
		}
		else
		{
			// Map name on tool tip
			snprintf(mapname, 48, "Map: %s", G_BuildMapTitle(gamemap));
			discordPresence.largeImageText = mapname;
		}

		if (gamestate == GS_LEVEL && Playing())
		{
			const time_t currentTime = time(NULL);
			const time_t mapTimeStart = currentTime - ((leveltime) / TICRATE);

			discordPresence.startTimestamp = mapTimeStart;
		}
	}
	else
	{
		discordPresence.largeImageKey = "misctitle";
		discordPresence.largeImageText = "Title Screen";
	}

	// Character info
	if (Playing() && playeringame[consoleplayer] && !players[consoleplayer].spectator)
	{
				// Supported skin names
		static const char *supportedSkins[] = {
			// base game
			"sonic",
			"tails",
			"knuckles",
			NULL
		};

		boolean customChar = true;
		UINT8 checkSkin = 0;

		// Character image
		while (supportedSkins[checkSkin] != NULL)
		{
			if (!strcmp(skins[players[consoleplayer].skin].name, supportedSkins[checkSkin]))
			{
				snprintf(charimg, 21, "char%s", supportedSkins[checkSkin]);
				discordPresence.smallImageKey = charimg;
				customChar = false;
				break;
			}

			checkSkin++;
		}

		if (customChar == true)
		{
			// Use the custom character icon!
			discordPresence.smallImageKey = "charcustom";
		}

		snprintf(charname, 28, "Character: %s", skins[players[consoleplayer].skin].realname);
		discordPresence.smallImageText = charname; // Character name
	}

	Discord_UpdatePresence(&discordPresence);
}

#endif