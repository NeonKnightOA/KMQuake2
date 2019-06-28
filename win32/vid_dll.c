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

// Main windowed and fullscreen graphics interface module.
// This module is used for both the software and OpenGL rendering versions of the Quake refresh engine.

#include "..\client\client.h"
#include "winquake.h"

// Console variables that we need to access from this module
cvar_t *win_alttab_restore_desktop; // Knightmare- whether to restore desktop resolution on alt-tab
cvar_t *vid_gamma;
cvar_t *vid_ref;	// Name of Refresh DLL loaded
cvar_t *vid_xpos;	// X coordinate of window position
cvar_t *vid_ypos;	// Y coordinate of window position
cvar_t *vid_fullscreen;
cvar_t *r_customwidth;
cvar_t *r_customheight;

// Global variables used internally by this module
viddef_t viddef; // Global video state; used by other modules
static qboolean kmgl_active = false;

HWND cl_hwnd; // Main window handle for life of program

extern unsigned sys_msg_time;

#pragma region ======================= Windows key message processing

static byte scantokey[128] =
{
	//	0			1		2			3			4		5				6			7
	//	8			9		A			B			C		D				E			F
	0,			27,		'1',		'2',		'3',	'4',			'5',		'6',
	'7',		'8',	'9',		'0',		'-',	'=',			K_BACKSPACE,9,			// 0
	'q',		'w',	'e',		'r',		't',	'y',			'u',		'i',
	'o',		'p',	'[',		']',		13 ,	K_CTRL,			'a',		's',		// 1
	'd',		'f',	'g',		'h',		'j',	'k',			'l',		';',
	'\'',		'`',	K_SHIFT,	'\\',		'z',	'x',			'c',		'v',		// 2
	'b',		'n',	'm',		',',		'.',	'/',			K_SHIFT,	K_KP_MULT,	// KP_MULT was '*'
	K_ALT,		' ',	K_CAPSLOCK,	K_F1,		K_F2,	K_F3,			K_F4,		K_F5,		// 3
	K_F6,		K_F7,	K_F8,		K_F9,		K_F10,  K_PAUSE,		K_SCROLLOCK,K_HOME,
	K_UPARROW,	K_PGUP,	K_KP_MINUS,	K_LEFTARROW,K_KP_5,	K_RIGHTARROW,	K_KP_PLUS,	K_END,		// 4
	K_DOWNARROW,K_PGDN,	K_INS,		K_DEL,		0,		0,				0,			K_F11,
	K_F12,		0,		0,			0,			0,		0,				0,			0,			// 5
	0,			0,		0,			0,			0,		0,				0,			0,
	0,			0,		0,			0,			0,		0,				0,			0,			// 6
	0,			0,		0,			0,			0,		0,				0,			0,
	0,			0,		0,			0,			0,		0,				0,			0			// 7
};

// Get Quake 2 keynum from compound value returned by WM_KEYDOWN / WM_KEYUP windows messages
static int MapKey(int key)
{
	// Scan code is stored in bits 16-23
	const int scancode = (key >> 16) & 255;

	if (scancode > 127)
		return 0;

	// Bit 24 is set when the key is an extended key, such as the right-hand ALT and CTRL keys that appear on an enhanced 101- or 102-key keyboard.
	const qboolean is_extended = (key & (1 << 24));

	const int result = scantokey[scancode];

	if (!is_extended)
	{
		switch (result)
		{
			case K_HOME:		return K_KP_HOME;
			case K_UPARROW:		return K_KP_UPARROW;
			case K_PGUP:		return K_KP_PGUP;
			case K_LEFTARROW:	return K_KP_LEFTARROW;
			case K_RIGHTARROW:	return K_KP_RIGHTARROW;
			case K_END:			return K_KP_END;
			case K_DOWNARROW:	return K_KP_DOWNARROW;
			case K_PGDN:		return K_KP_PGDN;
			case K_INS:			return K_KP_INS;
			case K_DEL:			return K_KP_DEL;
			default:			return result;
		}
	}
	else
	{
		//mxd. On my keyboard, only right-Ctrl (key 133) is registering as extended
		switch (result)
		{
			case K_ENTER:	return K_KP_ENTER;
			case '/':		return K_KP_SLASH;
			case K_KP_MULT:	return K_KP_PLUS;
			case K_PAUSE:	return K_NUMLOCK;
			default:		return result;
		}
	}
}

#pragma endregion

#pragma region ======================= DLL GLUE

void VID_Printf(int print_level, char *fmt, ...)
{
	va_list	argptr;
	static char msg[MAXPRINTMSG]; //mxd. +static
	
	va_start(argptr, fmt);
	Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	if (print_level == PRINT_ALL)
	{
		Com_Printf("%s", msg);
	}
	else if (print_level == PRINT_DEVELOPER)
	{
		Com_DPrintf("%s", msg);
	}
	else if (print_level == PRINT_ALERT)
	{
		MessageBox(0, msg, "PRINT_ALERT", MB_ICONWARNING);
		OutputDebugString(msg);
	}
}

void VID_Error(int err_level, char *fmt, ...)
{
	va_list	argptr;
	static char msg[MAXPRINTMSG]; //mxd. +static
	
	va_start(argptr, fmt);
	Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
	va_end(argptr);

	Com_Error(err_level, "%s", msg);
}

static void AppActivate(BOOL fActive, BOOL minimize)
{
	Minimized = minimize;

	Key_ClearStates();

	// We don't want to act like we're active if we're minimized
	ActiveApp = (fActive && !Minimized);

	// Minimize/restore mouse-capture on demand
	IN_Activate(ActiveApp);
	S_Activate(ActiveApp);
}

// Main window procedure
LONG WINAPI MainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
		case WM_MOUSEWHEEL:
			if ((short)HIWORD(wParam) > 0)
			{
				Key_Event(K_MWHEELUP, true, sys_msg_time);
				Key_Event(K_MWHEELUP, false, sys_msg_time);
			}
			else
			{
				Key_Event(K_MWHEELDOWN, true, sys_msg_time);
				Key_Event(K_MWHEELDOWN, false, sys_msg_time);
			}
			break;

		case WM_CREATE:
			cl_hwnd = hWnd;
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		case WM_PAINT:
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		case WM_DESTROY:
			// Let sound and input know about this?
			cl_hwnd = NULL;
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		case WM_ACTIVATE:
			{
				// KJB: Watch this for problems in fullscreen modes with Alt-tabbing.
				const int fActive = LOWORD(wParam);
				const int fMinimized = (BOOL)HIWORD(wParam);

				AppActivate(fActive != WA_INACTIVE, fMinimized);

				if (kmgl_active)
					GLimp_AppActivate(fActive != WA_INACTIVE); //mxd. Was !(fActive == WA_INACTIVE)
			}
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		case WM_MOVE:
			if (!vid_fullscreen->integer)
			{
				const int xPos = (short)LOWORD(lParam); // Horizontal position 
				const int yPos = (short)HIWORD(lParam); // Vertical position 

				RECT r = {0, 0, 1, 1};
				const int style = GetWindowLong(hWnd, GWL_STYLE);
				AdjustWindowRect(&r, style, FALSE);

				Cvar_SetValue("vid_xpos", xPos + r.left);
				Cvar_SetValue("vid_ypos", yPos + r.top);
				vid_xpos->modified = false;
				vid_ypos->modified = false;

				if (ActiveApp)
					IN_Activate(true);
			}
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		// This is complicated because Win32 seems to pack multiple mouse events into
		// one update sometimes, so we always check all states and look for events
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_XBUTTONDOWN:// Backslash's imouse explorer buttons
		case WM_XBUTTONUP:	// Backslash's imouse explorer buttons 
		case WM_MOUSEMOVE:
			{
				int temp = 0;

				if (wParam & MK_LBUTTON)
					temp |= 1;

				if (wParam & MK_RBUTTON)
					temp |= 2;

				if (wParam & MK_MBUTTON)
					temp |= 4;
				// Mouse buttons 4 & 5 support
				if (wParam & MK_XBUTTON1)
					temp |= 8;

				if (wParam & MK_XBUTTON2)
					temp |= 16;
				// end Mouse buttons 4 & 5 support

				IN_MouseEvent(temp);
			}
			break;

		case WM_SYSCOMMAND: // Idle's fix
			switch (wParam & 0xfffffff0) // bitshifter's fix for screensaver bug
			{
				case SC_SCREENSAVE:
				case SC_MONITORPOWER:
					return 0;
				case SC_CLOSE:
					CL_Quit_f();
			}
			return DefWindowProc(hWnd, uMsg, wParam, lParam);

		case WM_SYSKEYDOWN:
			if (wParam == 13)
			{
				if (vid_fullscreen)
					Cvar_SetValue("vid_fullscreen", !vid_fullscreen->value);

				return 0;
			}
		// Fall through
		case WM_KEYDOWN:
			Key_Event(MapKey(lParam), true, sys_msg_time);
			break;

		case WM_SYSKEYUP:
		case WM_KEYUP:
			Key_Event(MapKey(lParam), false, sys_msg_time);
			break;

		default: // Pass all unhandled messages to DefWindowProc
			return DefWindowProc(hWnd, uMsg, wParam, lParam);
	}

	// Return 0 if handled message, 1 if not
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

// Console command to restart the video mode and refresh DLL. We do this
// simply by setting the modified flag for the vid_ref variable, which will
// cause the entire video mode and refresh DLL to be reset on the next frame.
void VID_Restart_f(void)
{
	vid_ref->modified = true;
}

void VID_Front_f(void)
{
	SetWindowLong(cl_hwnd, GWL_EXSTYLE, WS_EX_TOPMOST);
	SetForegroundWindow(cl_hwnd);
}

#pragma endregion

#pragma region ======================= Get mode info

typedef struct vidmode_s
{
	const char *description;
	int width, height;
	int mode;
} vidmode_t;

qboolean VID_GetModeInfo(int *width, int *height, int mode)
{
	// Knightmare- added 1280x1024, 1400x1050, 856x480, 1024x480 modes
	static vidmode_t vid_modes[] =
	{
		#include "../qcommon/vid_modes.h"
	};
	static int num_vid_modes = (int)(sizeof(vid_modes) / sizeof(vid_modes[0])); //mxd
	
	if (mode == -1) // Custom mode
	{
		*width  = r_customwidth->integer;
		*height = r_customheight->integer;
		return true;
	}

	if (mode < 0 || mode >= num_vid_modes)
		return false;

	*width  = vid_modes[mode].width;
	*height = vid_modes[mode].height;

	return true;
}

static void VID_UpdateWindowPosAndSize(int x, int y)
{
	RECT r = { 0, 0, viddef.width, viddef.height };
	const int style = GetWindowLong(cl_hwnd, GWL_STYLE);
	AdjustWindowRect(&r, style, FALSE);

	const int w = r.right - r.left;
	const int h = r.bottom - r.top;

	MoveWindow(cl_hwnd, x, y, w, h, TRUE);
}

void VID_NewWindow(int width, int height)
{
	viddef.width  = width;
	viddef.height = height;

	cl.force_refdef = true; // Can't use a paused refdef
}

static void VID_FreeReflib(void)
{
	kmgl_active = false;
}

static void UpdateVideoRef(void)
{
	extern decalpolys_t *active_decals;
	static qboolean reclip_decals = false;

	qboolean vid_reloading = false; // Knightmare- flag to not unnecessarily drop console
	char reason[128];

	if (vid_ref->modified)
	{
		cl.force_refdef = true; // Can't use a paused refdef
		S_StopAllSounds();

		// Unclip decals
		if (active_decals)
		{
			CL_UnclipDecals();
			reclip_decals = true;
		}
	}

	while (vid_ref->modified)
	{
		// Refresh has changed
		vid_ref->modified = false;
		vid_fullscreen->modified = true;
		cl.refresh_prepped = false;
		cls.disable_screen = (cl.cinematictime == 0); // Knightmare added
		vid_reloading = true;
		// end Knightmare

		// Compacted code from VID_LoadRefresh
		if (kmgl_active)
		{
			R_Shutdown();
			VID_FreeReflib();
		}

		Com_Printf("\n------ Renderer Initialization ------\n");

		if (!R_Init(global_hInstance, MainWndProc, reason))
		{
			R_Shutdown();
			VID_FreeReflib();
			Com_Error(ERR_FATAL, "Couldn't initialize OpenGL renderer!\n%s", reason);
		}

		Com_Printf( "------------------------------------\n");

		kmgl_active = true;
	}

	// Added to close loading screen
	if (cl.refresh_prepped && vid_reloading)
		cls.disable_screen = false;

	// Re-clip decals
	if (cl.refresh_prepped && reclip_decals)
	{
		CL_ReclipDecals();
		reclip_decals = false;
	}
}

// This function gets called once just before drawing each frame, and it's sole purpose in life
// is to check to see if any of the video mode parameters have changed, and if they have to 
// update the rendering DLL and/or video mode to match.
void VID_CheckChanges(void)
{
	// Update changed vid_ref
	UpdateVideoRef();

	// Update our window position
	if (vid_xpos->modified || vid_ypos->modified)
	{
		if (!vid_fullscreen->integer)
			VID_UpdateWindowPosAndSize(vid_xpos->integer, vid_ypos->integer);

		vid_xpos->modified = false;
		vid_ypos->modified = false;
	}
}

void VID_Init(void)
{
	// Create the video variables so we know how to start the graphics drivers
	vid_ref = Cvar_Get("vid_ref", "gl", CVAR_ARCHIVE);
	vid_xpos = Cvar_Get("vid_xpos", "3", CVAR_ARCHIVE);
	vid_ypos = Cvar_Get("vid_ypos", "22", CVAR_ARCHIVE);
	vid_fullscreen = Cvar_Get("vid_fullscreen", "1", CVAR_ARCHIVE);
	vid_gamma = Cvar_Get("vid_gamma", "0.8", CVAR_ARCHIVE); // was 1.0
	win_alttab_restore_desktop = Cvar_Get("win_alttab_restore_desktop", "1", CVAR_ARCHIVE);	// Knightmare- whether to restore desktop resolution on alt-tab
	r_customwidth = Cvar_Get("r_customwidth", "1600", CVAR_ARCHIVE);
	r_customheight = Cvar_Get("r_customheight", "1024", CVAR_ARCHIVE);

	// Force vid_ref to gl. Older versions of Lazarus code check only vid_ref = gl for fadein effects
	Cvar_Set("vid_ref", "gl");

	// Add some console commands that we want to handle
	Cmd_AddCommand("vid_restart", VID_Restart_f);
	Cmd_AddCommand("vid_front", VID_Front_f);

	// Start the graphics mode and load refresh DLL
	VID_CheckChanges();
}

void VID_Shutdown(void)
{
	if (kmgl_active)
	{
		R_Shutdown();
		VID_FreeReflib();
	}
}

#pragma endregion