/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "server.h"
#include "../client/ref.h"


/*
===============================================================================

OPERATOR CONSOLE ONLY COMMANDS

These commands can only be entered from stdin or by a remote operator datagram
===============================================================================
*/

/*
====================
SV_SetMaster_f

Specify a list of master servers
====================
*/
void SV_SetMaster_f (void)
{
	// only dedicated servers send heartbeats
	if (!dedicated->value)
	{
		Com_Printf("Only dedicated servers use masters.\n");
		return;
	}

	// make sure the server is listed public
	Cvar_Set("public", "1"); // Vic's fix

	for (int i = 1; i < MAX_MASTERS; i++)
		memset(&master_adr[i], 0, sizeof(master_adr[i]));

	int slot = 1; // slot 0 will always contain the id master
	for (int i = 1; i < Cmd_Argc(); i++)
	{
		if (slot == MAX_MASTERS)
			break;

		if (!NET_StringToAdr(Cmd_Argv(i), &master_adr[i]))
		{
			Com_Printf("Bad address: %s\n", Cmd_Argv(i));
			continue;
		}

		if (master_adr[slot].port == 0)
			master_adr[slot].port = BigShort(PORT_MASTER);

		Com_Printf("Master server at %s\n", NET_AdrToString(master_adr[slot]));
		
		Com_Printf("Sending a ping.\n");
		Netchan_OutOfBandPrint(NS_SERVER, master_adr[slot], "ping");

		slot++;
	}

	svs.last_heartbeat = -9999999;
}


/*
==================
SV_SetPlayer

Sets sv_client and sv_player to the player with idnum Cmd_Argv(1)
==================
*/
qboolean SV_SetPlayer (void)
{
	client_t *cl;
	int i;

	if (Cmd_Argc() < 2)
		return false;

	char *s = Cmd_Argv(1);

	// numeric values are just slot numbers
	if (s[0] >= '0' && s[0] <= '9')
	{
		const int idnum = atoi(Cmd_Argv(1));
		if (idnum < 0 || idnum >= maxclients->value)
		{
			Com_Printf("Bad client slot: %i\n", idnum);
			return false;
		}

		sv_client = &svs.clients[idnum];
		sv_player = sv_client->edict;
		if (!sv_client->state)
		{
			Com_Printf ("Client %i is not active\n", idnum);
			return false;
		}

		return true;
	}

	// check for a name match
	for (i = 0, cl = svs.clients; i < maxclients->value; i++, cl++)
	{
		if (!cl->state)
			continue;

		if (!strcmp(cl->name, s))
		{
			sv_client = cl;
			sv_player = sv_client->edict;
			return true;
		}
	}

	Com_Printf("Userid %s is not on the server\n", s);
	return false;
}


/*
===============================================================================

SAVEGAME FILES

===============================================================================
*/
void R_GrabScreen (void); // Knightmare- screenshots for savegames
void R_ScaledScreenshot (char *name); // Knightmare- screenshots for savegames
void R_FreePic (char *name); // Knightmare- unregisters an image

/*
=====================
SV_WipeSavegame

Delete save/<XXX>/
=====================
*/
void SV_WipeSavegame (char *savename)
{
	char name[MAX_OSPATH];

	Com_DPrintf("SV_WipeSaveGame(%s)\n", savename);

	Com_sprintf(name, sizeof(name), "%s/save/%s/server.ssv", FS_Gamedir(), savename);
	remove(name);
	Com_sprintf (name, sizeof(name), "%s/save/%s/game.ssv", FS_Gamedir(), savename);
	remove(name);
	// Knightmare- delete screenshot
	Com_sprintf(name, sizeof(name), "%s/save/%s/shot.jpg", FS_Gamedir(), savename);
	remove(name);

	Com_sprintf(name, sizeof(name), "%s/save/%s/*.sav", FS_Gamedir(), savename);
	char *s = Sys_FindFirst(name, 0, 0);
	while (s)
	{
		remove(s);
		s = Sys_FindNext(0, 0);
	}
	Sys_FindClose();

	Com_sprintf (name, sizeof(name), "%s/save/%s/*.sv2", FS_Gamedir(), savename);
	s = Sys_FindFirst(name, 0, 0);
	while (s)
	{
		remove (s);
		s = Sys_FindNext(0, 0);
	}
	Sys_FindClose();
}


/*
================
CopyFile
================
*/
void CopyFile (char *src, char *dst)
{
	Com_DPrintf("CopyFile (%s, %s)\n", src, dst);

	FILE *f1 = fopen(src, "rb");
	if (!f1)
		return;

	FILE *f2 = fopen(dst, "wb");
	if (!f2)
	{
		fclose(f1);
		return;
	}

	byte buffer[65536];
	while (true)
	{
		const int len = fread(buffer, 1, sizeof(buffer), f1);
		if (!len)
			break;

		fwrite(buffer, 1, len, f2);
	}

	fclose(f1);
	fclose(f2);
}


/*
================
SV_CopySaveGame
================
*/
void SV_CopySaveGame (char *src, char *dst)
{
	Com_DPrintf("SV_CopySaveGame(%s, %s)\n", src, dst);
	SV_WipeSavegame(dst);

	// copy the savegame over
	char name[MAX_OSPATH], name2[MAX_OSPATH];
	Com_sprintf(name,  sizeof(name),  "%s/save/%s/server.ssv", FS_Gamedir(), src);
	Com_sprintf(name2, sizeof(name2), "%s/save/%s/server.ssv", FS_Gamedir(), dst);
	FS_CreatePath(name2);
	CopyFile(name, name2);

	Com_sprintf(name,  sizeof(name),  "%s/save/%s/game.ssv", FS_Gamedir(), src);
	Com_sprintf(name2, sizeof(name2), "%s/save/%s/game.ssv", FS_Gamedir(), dst);
	CopyFile(name, name2);

	// Knightmare- copy screenshot
	if (strcmp(dst, "kmq2save0")) // no screenshot for start of level autosaves
	{
		Com_sprintf(name,  sizeof(name),  "%s/save/%s/shot.jpg", FS_Gamedir(), src);
		Com_sprintf(name2, sizeof(name2), "%s/save/%s/shot.jpg", FS_Gamedir(), dst);
		CopyFile(name, name2);
	}

	Com_sprintf(name, sizeof(name), "%s/save/%s/", FS_Gamedir(), src);
	const int len = strlen(name);
	Com_sprintf(name, sizeof(name), "%s/save/%s/*.sav", FS_Gamedir(), src);
	char *found = Sys_FindFirst(name, 0, 0 );
	while (found)
	{
		Q_strncpyz(name + len, found + len, sizeof(name) - len);
		Com_sprintf(name2, sizeof(name2), "%s/save/%s/%s", FS_Gamedir(), dst, found + len);
		CopyFile(name, name2);

		// change sav to sv2
		int l = strlen(name);
		Q_strncpyz(name + l - 3, "sv2", sizeof(name) - l + 3);
		l = strlen(name2);
		Q_strncpyz(name2 + l - 3, "sv2", sizeof(name2) - l + 3);
		CopyFile(name, name2);

		found = Sys_FindNext(0, 0);
	}

	Sys_FindClose();
}


/*
==============
SV_WriteLevelFile

==============
*/
void SV_WriteLevelFile (void)
{
	Com_DPrintf("SV_WriteLevelFile()\n");

	char name[MAX_OSPATH];
	Com_sprintf(name, sizeof(name), "%s/save/current/%s.sv2", FS_Gamedir(), sv.name);
	FILE *f = fopen(name, "wb");
	if (!f)
	{
		Com_Printf("Failed to open %s\n", name);
		return;
	}

	fwrite(sv.configstrings, sizeof(sv.configstrings), 1, f);
	CM_WritePortalState(f);
	fclose(f);

	Com_sprintf(name, sizeof(name), "%s/save/current/%s.sav", FS_Gamedir(), sv.name);
	ge->WriteLevel(name);
}


void CM_ReadPortalState (fileHandle_t f);
/*
==============
SV_ReadLevelFile

==============
*/
void SV_ReadLevelFile (void)
{
	Com_DPrintf("SV_ReadLevelFile()\n");

	char name[MAX_OSPATH];
	fileHandle_t f;
	Com_sprintf(name, sizeof(name), "save/current/%s.sv2", sv.name);
	FS_FOpenFile(name, &f, FS_READ);
	if (!f)
	{
		Com_Printf("Failed to open %s\n", name);
		return;
	}

	FS_Read(sv.configstrings, sizeof(sv.configstrings), f);
	CM_ReadPortalState(f);
	FS_FCloseFile(f);

	Com_sprintf(name, sizeof(name), "%s/save/current/%s.sav", FS_Gamedir(), sv.name);
	ge->ReadLevel(name);
}

/*
==============
SV_WriteServerFile

==============
*/
void SV_WriteServerFile (qboolean autosave)
{
	char	fileName[MAX_OSPATH], varName[128], string[128];
	char	comment[32];
	
	Com_DPrintf("SV_WriteServerFile(%s)\n", autosave ? "true" : "false");

	Com_sprintf (fileName, sizeof(fileName), "%s/save/current/server.ssv", FS_Gamedir());
	FILE *f = fopen (fileName, "wb");
	if (!f)
	{
		Com_Printf("Couldn't write %s\n", fileName);
		return;
	}

	// write the comment field
	memset(comment, 0, sizeof(comment));
	if (!autosave)
	{
		time_t aclock;
		time(&aclock);
		struct tm *newtime = localtime(&aclock);
		Com_sprintf(comment, sizeof(comment), "%2i:%i%i %2i/%2i  ", newtime->tm_hour, newtime->tm_min / 10, newtime->tm_min%10, newtime->tm_mon + 1, newtime->tm_mday);
		strncat(comment, sv.configstrings[CS_NAME], sizeof(comment) - 1 - strlen(comment));
	}
	else
	{
		// autosaved
		Com_sprintf(comment, sizeof(comment), "ENTERING %s", sv.configstrings[CS_NAME]);
	}
	fwrite(comment, 1, sizeof(comment), f);

	// write the mapcmd
	fwrite(svs.mapcmd, 1, sizeof(svs.mapcmd), f);

	// write all CVAR_LATCH cvars
	// these will be things like coop, skill, deathmatch, etc
	for (cvar_t *var = cvar_vars; var; var = var->next)
	{
		if (!(var->flags & CVAR_LATCH) || var->flags & CVAR_SAVE_IGNORE) // latched vars that are not saved (game, etc)
			continue;

		if (strlen(var->name) >= sizeof(varName) - 1 || strlen(var->string) >= sizeof(string) - 1)
		{
			Com_Printf("Cvar too long: %s = %s\n", var->name, var->string);
			continue;
		}

		memset(varName, 0, sizeof(varName));
		memset(string, 0, sizeof(string));
		Q_strncpyz(varName, var->name, sizeof(varName));
		Q_strncpyz(string, var->string, sizeof(string));
		fwrite(varName, 1, sizeof(varName), f);
		fwrite(string, 1, sizeof(string), f);
	}

	fclose(f);

	// write game state
	Com_sprintf(fileName, sizeof(fileName), "%s/save/current/game.ssv", FS_Gamedir());
	ge->WriteGame(fileName, autosave);
}

/*
==============
SV_WriteScreenshot
==============
*/
void SV_WriteScreenshot (void)
{
	if (dedicated->value) // can't do this in dedicated mode
		return;

	Com_DPrintf("SV_WriteScreenshot()\n");

	char name[MAX_OSPATH];
	Com_sprintf(name, sizeof(name), "%s/save/current/shot.jpg", FS_Gamedir());
	R_ScaledScreenshot(name);
}

/*
==============
SV_ReadServerFile

==============
*/
void SV_ReadServerFile (void)
{
	fileHandle_t	f;
	char	fileName[MAX_OSPATH], varName[128], string[128];
	char	comment[32];
	char	mapcmd[MAX_TOKEN_CHARS];

	Com_DPrintf("SV_ReadServerFile()\n");

	Com_sprintf(fileName, sizeof(fileName), "save/current/server.ssv");
	FS_FOpenFile(fileName, &f, FS_READ);
	if (!f)
	{
		Com_Printf("Couldn't read %s\n", fileName);
		return;
	}

	// read the comment field
	FS_Read(comment, sizeof(comment), f);

	// read the mapcmd
	FS_Read(mapcmd, sizeof(mapcmd), f);

	// read all CVAR_LATCH cvars
	// these will be things like coop, skill, deathmatch, etc
	while (true)
	{
		if (!FS_FRead(varName, 1, sizeof(varName), f))
			break;

		FS_Read(string, sizeof(string), f);
		Com_DPrintf("Set %s = %s\n", varName, string);
		Cvar_ForceSet(varName, string);
	}

	FS_FCloseFile(f);

	// start a new game fresh with new cvars
	SV_InitGame();

//	strncpy (svs.mapcmd, mapcmd);
	Q_strncpyz(svs.mapcmd, mapcmd, sizeof(svs.mapcmd));

	// read game state
	Com_sprintf(fileName, sizeof(fileName), "%s/save/current/game.ssv", FS_Gamedir());
	ge->ReadGame(fileName);
}


//=========================================================




/*
==================
SV_DemoMap_f

Puts the server in demo mode on a specific map/cinematic
==================
*/
void SV_DemoMap_f (void)
{
	// Knightmare- force off DM, CTF mode
	Cvar_SetValue("ttctf", 0);
	Cvar_SetValue("ctf", 0);
	Cvar_SetValue("deathmatch", 0);

	SV_Map(true, Cmd_Argv(1), false);
}

/*
==================
SV_GameMap_f

Saves the state of the map just being exited and goes to a new map.

If the initial character of the map string is '*', the next map is
in a new unit, so the current savegame directory is cleared of
map files.

Example:

*inter.cin+jail

Clears the archived maps, plays the inter.cin cinematic, then
goes to map jail.bsp.
==================
*/
void SV_GameMap_f (void)
{
	int			i;
	client_t	*cl;

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("Usage: gamemap <map>\n");
		return;
	}

	Com_DPrintf("SV_GameMap(%s)\n", Cmd_Argv(1));

	FS_CreatePath(va("%s/save/current/", FS_Gamedir()));

	// check for clearing the current savegame
	char *map = Cmd_Argv(1);
	if (map[0] == '*')
	{
		// wipe all the *.sav files
		SV_WipeSavegame("current");
	}
	else
	{
		// save the map just exited
		if (sv.state == ss_game)
		{
			// clear all the client inuse flags before saving so that when the level is re-entered, 
			// the clients will spawn at spawn points instead of occupying body shells
			qboolean *savedInuse = malloc(maxclients->value * sizeof(qboolean));
			for (i = 0, cl = svs.clients; i < maxclients->value; i++,cl++)
			{
				savedInuse[i] = cl->edict->inuse;
				cl->edict->inuse = false;
			}

			SV_WriteLevelFile();

			// we must restore these for clients to transfer over correctly
			for (i = 0, cl = svs.clients; i < maxclients->value; i++,cl++)
				cl->edict->inuse = savedInuse[i];

			free(savedInuse);
		}
	}

	// start up the next map
	SV_Map(false, Cmd_Argv(1), false);

	// archive server state
	strncpy(svs.mapcmd, Cmd_Argv(1), sizeof(svs.mapcmd) - 1);

	// copy off the level to the autosave slot
	// Knightmare- don't do this in deathmatch or for cinematics
	char *ext = map + strlen(map) - 4; //mxd
	if (!dedicated->value && !Cvar_VariableValue("deathmatch")
		&& Q_strcasecmp(ext, ".cin") && Q_strcasecmp(ext, ".roq") && Q_strcasecmp(ext, ".pcx"))
	{
		SV_WriteServerFile(true);
		SV_CopySaveGame("current", "kmq2save0");
	}
}

/*
==================
SV_Map_f

Goes directly to a given map without any savegame archiving.
For development work
==================
*/
void SV_Map_f (void)
{
	if (Cmd_Argc() != 2)
	{
		Com_Printf ("Usage: map <mapname>\n");
		return;
	}

	// if not a pcx, demo, or cinematic, check to make sure the level exists
	char *map = Cmd_Argv(1);
	if (!strstr (map, "."))
	{
		char expanded[MAX_QPATH];
		Com_sprintf(expanded, sizeof(expanded), "maps/%s.bsp", map);

		if (FS_LoadFile (expanded, NULL) == -1)
		{
			Com_Printf("Can't find %s\n", expanded);
			return;
		}
	}

	sv.state = ss_dead; // don't save current level when changing
	SV_WipeSavegame("current");
	SV_GameMap_f ();
}

/*
=====================================================================

  SAVEGAMES

=====================================================================
*/
extern	char *load_saveshot;
char sv_loadshotname[MAX_QPATH];

/*
==============
SV_Loadgame_f

==============
*/
void SV_Loadgame_f (void)
{
	if (Cmd_Argc() != 2)
	{
		Com_Printf ("Usage: loadgame <directory>\n");
		return;
	}

	Com_Printf ("Loading game...\n");

	char *dir = Cmd_Argv(1);
	if (strstr(dir, "..") || strstr(dir, "/") || strstr(dir, "\\") )
		Com_Printf("Bad savedir.\n");

	// make sure the server.ssv file exists
	char name[MAX_OSPATH];
	Com_sprintf(name, sizeof(name), "%s/save/%s/server.ssv", FS_Gamedir(), Cmd_Argv(1));
	FILE *f = fopen(name, "rb");
	if (!f)
	{
		Com_Printf("No such savegame: %s\n", name);
		return;
	}
	fclose (f);

	// Knightmare- set saveshot name
	if (!dedicated->value && (!strcmp(Cmd_Argv(1), "quick") || !strcmp(Cmd_Argv(1), "quik")))
	{
		Com_sprintf(sv_loadshotname, sizeof(sv_loadshotname), "save/%s/shot.jpg", Cmd_Argv(1));
		R_FreePic(sv_loadshotname);
		Com_sprintf(sv_loadshotname, sizeof(sv_loadshotname), "/save/%s/shot.jpg", Cmd_Argv(1));
		load_saveshot = sv_loadshotname;
	}

	SV_CopySaveGame(Cmd_Argv(1), "current");

	SV_ReadServerFile();

	// go to the map
	sv.state = ss_dead; // don't save current level when changing
	SV_Map(false, svs.mapcmd, true);
}


/*
==============
SV_Savegame_f

==============
*/
extern char fs_gamedir[MAX_OSPATH];

void SV_Savegame_f (void)
{
	if (sv.state != ss_game)
	{
		Com_Printf ("You must be in a game to save.\n");
		return;
	}

	// Knightmare- fs_gamedir may be getting messed up, causing it to occasinally save in the root dir,
	// thus leading to a hang on game loads, so we reset it here.
	if (!fs_gamedir[0] && fs_gamedirvar->string[0])
		Com_sprintf(fs_gamedir, sizeof(fs_gamedir), "%s/%s", fs_basedir->string, fs_gamedirvar->string);

	if (Cmd_Argc() != 2)
	{
		Com_Printf("Usage: savegame <directory>\n");
		return;
	}

	if (Cvar_VariableValue("deathmatch"))
	{
		Com_Printf("Can't save in a deathmatch\n");
		return;
	}

	if (!strcmp(Cmd_Argv(1), "current"))
	{
		Com_Printf("Can't save to 'current'\n");
		return;
	}

	// Knightmare- grab screen for quicksave
	if (!dedicated->value && (!strcmp(Cmd_Argv(1), "quick") || !strcmp(Cmd_Argv(1), "quik")))
		R_GrabScreen();

	if (maxclients->value == 1 && svs.clients[0].edict->client->ps.stats[STAT_HEALTH] <= 0)
	{
		Com_Printf("\nCan't save game while dead!\n");
		return;
	}

	char *dir = Cmd_Argv(1);
	if (strstr (dir, "..") || strstr (dir, "/") || strstr (dir, "\\") )
		Com_Printf("Bad savedir.\n");

	Com_Printf(S_COLOR_CYAN"Saving game \"%s\"...\n", dir);

	// Archive current level, including all client edicts.
	// When the level is reloaded, they will be shells awaiting a connecting client.
	SV_WriteLevelFile();

	// save server state
	SV_WriteServerFile(false);

	// take screenshot
	SV_WriteScreenshot();

	// copy it off
	SV_CopySaveGame("current", dir);

	Com_Printf(S_COLOR_CYAN"Done.\n");
}

//===============================================================

/*
==================
SV_Kick_f

Kick a user off of the server
==================
*/
void SV_Kick_f (void)
{
	if (!svs.initialized)
	{
		Com_Printf("No server running.\n");
		return;
	}

	if (Cmd_Argc() != 2)
	{
		Com_Printf("Usage: kick <userid>\n");
		return;
	}

	if (!SV_SetPlayer())
		return;

	SV_BroadcastPrintf(PRINT_HIGH, "%s was kicked\n", sv_client->name);
	// print directly, because the dropped client won't get the SV_BroadcastPrintf message
	SV_ClientPrintf(sv_client, PRINT_HIGH, "You were kicked from the game\n");
	SV_DropClient(sv_client);
	sv_client->lastmessage = svs.realtime;	// min case there is a funny zombie
}


/*
================
SV_Status_f
================
*/
void SV_Status_f (void)
{
	int			i;
	client_t	*cl;

	if (!svs.clients)
	{
		Com_Printf("No server running.\n");
		return;
	}

	Com_Printf("map              : %s\n", sv.name);

	Com_Printf("num score ping name            lastmsg address               qport \n");
	Com_Printf("--- ----- ---- --------------- ------- --------------------- ------\n");

	for (i = 0, cl = svs.clients; i < maxclients->value; i++, cl++)
	{
		if (!cl->state)
			continue;

		Com_Printf("%3i ", i);
		Com_Printf("%5i ", cl->edict->client->ps.stats[STAT_FRAGS]);

		if (cl->state == cs_connected)
		{
			Com_Printf("CNCT ");
		}
		else if (cl->state == cs_zombie)
		{
			Com_Printf("ZMBI ");
		}
		else
		{
			const int ping = (cl->ping < 9999 ? cl->ping : 9999);
			Com_Printf("%4i ", ping);
		}

		Com_Printf("%s", cl->name);
		int len = 16 - strlen(cl->name);
		for (int j = 0; j < len; j++)
			Com_Printf(" ");

		Com_Printf("%7i ", svs.realtime - cl->lastmessage);

		char *s = NET_AdrToString(cl->netchan.remote_address);
		Com_Printf ("%s", s);
		len = 22 - strlen(s);
		for (int j = 0; j < len; j++)
			Com_Printf(" ");
		
		Com_Printf("%5i", cl->netchan.qport);

		Com_Printf("\n");
	}

	Com_Printf("\n");
}

/*
==================
SV_ConSay_f
==================
*/
void SV_ConSay_f(void)
{
	client_t *client;
	int		j;
	char	text[1024];

	if (Cmd_Argc() < 2)
		return;

	Q_strncpyz(text, "console: ", sizeof(text));
	char *p = Cmd_Args();

	if (*p == '"')
	{
		p++;
		p[strlen(p) - 1] = 0;
	}

	Q_strncatz(text, p, sizeof(text));

	for (j = 0, client = svs.clients; j < maxclients->value; j++, client++)
	{
		if (client->state == cs_spawned)
			SV_ClientPrintf(client, PRINT_CHAT, "%s\n", text);
	}
}


/*
==================
SV_Heartbeat_f
==================
*/
void SV_Heartbeat_f (void)
{
	svs.last_heartbeat = -9999999;
}


/*
===========
SV_Serverinfo_f

  Examine or change the serverinfo string
===========
*/
void SV_Serverinfo_f (void)
{
	Com_Printf("Server info settings:\n");
	Info_Print(Cvar_Serverinfo());
}


/*
===========
SV_DumpUser_f

Examine all a users info strings
===========
*/
void SV_DumpUser_f (void)
{
	if (Cmd_Argc() != 2)
	{
		Com_Printf ("Usage: info <userid>\n");
		return;
	}

	if (!SV_SetPlayer ())
		return;

	Com_Printf("userinfo\n");
	Com_Printf("--------\n");
	Info_Print(sv_client->userinfo);
}


/*
===================
SV_StartMod
===================
*/
void SV_StartMod (char *mod)
{
	// killserver, start mod, unbind keys, exec configs, and start demos
	Cbuf_AddText("killserver\n");
	Cbuf_AddText(va("game %s\n", mod));
	Cbuf_AddText("unbindall\n");
	Cbuf_AddText("exec default.cfg\n");
	Cbuf_AddText("exec kmq2config.cfg\n");
	Cbuf_AddText("exec autoexec.cfg\n");
	Cbuf_AddText("d1\n");
}

/*
===================
SV_ChangeGame_f

switch to a different mod
===================
*/
void SV_ChangeGame_f (void)
{
	if (Cmd_Argc() < 2)
	{
		Com_Printf("changegame <gamedir> : change game directory\n");
		return;
	}

	SV_StartMod(Cmd_Argv(1));
}


/*
==============
SV_ServerRecord_f

Begins server demo recording.  Every entity and every message will be
recorded, but no playerinfo will be stored.  Primarily for demo merging.
==============
*/
void SV_ServerRecord_f (void)
{
	char	name[MAX_OSPATH];
	char	buf_data[32768];
	sizebuf_t	buf;

	if (Cmd_Argc() != 2)
	{
		Com_Printf("Usage: serverrecord <demoname>\n");
		return;
	}

	if (svs.demofile)
	{
		Com_Printf("Already recording.\n");
		return;
	}

	if (sv.state != ss_game)
	{
		Com_Printf ("You must be in a level to record.\n");
		return;
	}

	//
	// open the demo file
	//
	Com_sprintf(name, sizeof(name), "%s/demos/%s.dm2", FS_Gamedir(), Cmd_Argv(1));

	Com_Printf("recording to \"%s\".\n", name);
	FS_CreatePath(name);
	svs.demofile = fopen(name, "wb");
	if (!svs.demofile)
	{
		Com_Printf("ERROR: couldn't open demo file.\n");
		return;
	}

	// setup a buffer to catch all multicasts
	SZ_Init(&svs.demo_multicast, svs.demo_multicast_buf, sizeof(svs.demo_multicast_buf));

	//
	// write a single giant fake message with all the startup info
	//
	SZ_Init(&buf, buf_data, sizeof(buf_data));

	//
	// serverdata needs to go over for all types of servers
	// to make sure the protocol is right, and to set the gamedir
	//
	// send the serverdata
	MSG_WriteByte(&buf, svc_serverdata);
	MSG_WriteLong(&buf, PROTOCOL_VERSION);
	MSG_WriteLong(&buf, svs.spawncount);
	// 2 means server demo
	MSG_WriteByte(&buf, 2);	// demos are always attract loops
	MSG_WriteString(&buf, Cvar_VariableString ("gamedir"));
	MSG_WriteShort(&buf, -1);
	// send full levelname
	MSG_WriteString(&buf, sv.configstrings[CS_NAME]);

	for (int i = 0 ; i < MAX_CONFIGSTRINGS; i++)
	{
		if (sv.configstrings[i][0])
		{
			MSG_WriteByte(&buf, svc_configstring);
			MSG_WriteShort(&buf, i);
			MSG_WriteString(&buf, sv.configstrings[i]);
		}
	}

	// write it to the demo file
	Com_DPrintf("signon message length: %i\n", buf.cursize);
	int len = LittleLong(buf.cursize);
	fwrite(&len, 4, 1, svs.demofile);
	fwrite(buf.data, buf.cursize, 1, svs.demofile);

	// the rest of the demo file will be individual frames
}


/*
==============
SV_ServerStop_f

Ends server demo recording
==============
*/
void SV_ServerStop_f (void)
{
	if (!svs.demofile)
	{
		Com_Printf ("Not doing a serverrecord.\n");
		return;
	}
	fclose (svs.demofile);
	svs.demofile = NULL;
	Com_Printf ("Recording completed.\n");
}


/*
===============
SV_KillServer_f

Kick everyone off, possibly in preparation for a new game

===============
*/
void SV_KillServer_f (void)
{
	if (!svs.initialized)
		return;

	SV_Shutdown ("Server was killed.\n", false);
	NET_Config ( false );	// close network sockets
}

/*
===============
SV_ServerCommand_f

Let the game dll handle a command
===============
*/
void SV_ServerCommand_f (void)
{
	if (!ge)
	{
		Com_Printf ("No game loaded.\n");
		return;
	}

	ge->ServerCommand();
}

//===========================================================

/*
==================
SV_InitOperatorCommands
==================
*/
void SV_InitOperatorCommands (void)
{
	Cmd_AddCommand ("heartbeat", SV_Heartbeat_f);
	Cmd_AddCommand ("kick", SV_Kick_f);
	Cmd_AddCommand ("status", SV_Status_f);
	Cmd_AddCommand ("serverinfo", SV_Serverinfo_f);
	Cmd_AddCommand ("dumpuser", SV_DumpUser_f);

	Cmd_AddCommand ("changegame", SV_ChangeGame_f); // Knightmare added

	Cmd_AddCommand ("map", SV_Map_f);
	Cmd_AddCommand ("demomap", SV_DemoMap_f);
	Cmd_AddCommand ("gamemap", SV_GameMap_f);
	Cmd_AddCommand ("setmaster", SV_SetMaster_f);

	if ( dedicated->value )
		Cmd_AddCommand ("say", SV_ConSay_f);

	Cmd_AddCommand ("serverrecord", SV_ServerRecord_f);
	Cmd_AddCommand ("serverstop", SV_ServerStop_f);

	Cmd_AddCommand ("save", SV_Savegame_f);
	Cmd_AddCommand ("load", SV_Loadgame_f);

	Cmd_AddCommand ("killserver", SV_KillServer_f);

	Cmd_AddCommand ("sv", SV_ServerCommand_f);
}

