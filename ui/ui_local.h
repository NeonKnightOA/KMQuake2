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

#pragma once

#define MAXMENUITEMS		64

#define MTYPE_SLIDER		0
#define MTYPE_LIST			1
#define MTYPE_ACTION		2
#define MTYPE_SPINCONTROL	3
#define MTYPE_SEPARATOR		4
#define MTYPE_FIELD			5

#define QMF_LEFT_JUSTIFY	0x00000001
#define QMF_GRAYED			0x00000002
#define QMF_NUMBERSONLY		0x00000004
//#define QMF_SKINLIST		0x00000008 //mxd. Why did skinlist require special handling?
#define QMF_HIDDEN			0x00000010

#define RCOLUMN_OFFSET  (MENU_FONT_SIZE * 2) // was 16
#define LCOLUMN_OFFSET (-MENU_FONT_SIZE * 2) // was -16

#define SLIDER_RANGE 10

#define DEFAULT_MENU_Y 140 //mxd

//mxd. "Back" button titles
#define MENU_BACK_CLOSE "Close menu"
#define MENU_BACK_TO_MAIN "Back to Main menu"
#define MENU_BACK_TO_GAME "Back to Game menu"
#define MENU_BACK_TO_CONTROLS "Back to Controls menu"
#define MENU_BACK_TO_MULTIPLAYER "Back to Multiplayer menu"
#define MENU_BACK_TO_JOINSERVER "Back to Join server menu"
#define MENU_BACK_TO_STARTSERVER "Back to Start server menu"
#define MENU_BACK_TO_OPTIONS "Back to Options menu"
#define MENU_BACK_TO_VIDEO "Back to Video menu"

static const char *yesno_names[] = { "No", "Yes", 0 }; //mxd. Used in almost every menu

typedef struct _tag_menuframework
{
	int x, y;
	int cursor;

	int nitems; // Number of menu items. Increased automatically when adding items via Menu_AddItem. Initialize to 0.
	int nslots;
	void *items[64];

	const char *statusbar;

	void (*cursordraw)( struct _tag_menuframework *m );
	
} menuframework_s;

typedef struct
{
	int type;
	const char *name;
	int x, y;
	menuframework_s *parent;
	int cursor_offset;
	int localdata[4];
	unsigned flags;

	const char *statusbar;

	void (*callback)(void *self);
	void (*statusbarfunc)(void *self);
	void (*ownerdraw)(void *self);
	void (*cursordraw)(void *self);
} menucommon_s;

typedef struct
{
	menucommon_s generic;

	char buffer[80];
	int cursor;
	int length;
	int visible_length;
	int visible_offset;
} menufield_s;

typedef struct 
{
	menucommon_s generic;

	float minvalue;
	float maxvalue;
	float curvalue;

	cvar_t *cvar; //mxd. If set, use to display value, otherwise use curvalue
	int numdecimals; //mxd. Number of decimals to print after dot. 0 == strip trailing zeroes.
} menuslider_s;

typedef struct
{
	menucommon_s generic;

	int curvalue;

	const char **itemnames; // Last item is expected to be "\0"
	int numitemnames;
} menulist_s;

typedef struct
{
	menucommon_s generic;
} menuaction_s;

typedef struct
{
	menucommon_s generic;
} menuseparator_s;

typedef enum
{
	LISTBOX_TEXT,
	LISTBOX_IMAGE,
	LISTBOX_TEXTIMAGE
} listboxtype_t;

typedef enum
{
	SCROLL_X,
	SCROLL_Y
} scrolltype_t;

typedef struct
{
	menucommon_s generic;

	listboxtype_t type;
	scrolltype_t scrolltype;
	int items_x;
	int items_y;
	int item_width;
	int item_height;
	int scrollpos;
	int curvalue;

	const char **itemnames;
	int numitemnames;
} menulistbox_s;

typedef struct
{
	float min[2];
	float max[2];
	int index;
} buttonmenuobject_t;

typedef struct
{
	int	min[2];
	int max[2];
	void (*OpenMenu)(void);
} mainmenuobject_t;

qboolean Field_Key(menufield_s *field, int key);

void Menu_AddItem(menuframework_s *menu, void *item);
void Menu_AdjustCursor(menuframework_s *menu, int dir);
void Menu_Center(menuframework_s *menu);
void Menu_Draw(menuframework_s *menu);
void *Menu_ItemAtCursor(menuframework_s *m);
qboolean Menu_SelectItem(menuframework_s *s);
qboolean Menu_MouseSelectItem(menucommon_s *item);
//void Menu_SetStatusBar(menuframework_s *s, const char *string);
void Menu_SlideItem(menuframework_s *s, int dir);
int Menu_TallySlots(menuframework_s *menu);

void Menu_DrawString(int x, int y, const char *string, int alpha);
void Menu_DrawStringDark(int x, int y, const char *string, int alpha);
void Menu_DrawStringR2L(int x, int y, const char *string, int alpha);
void Menu_DrawStringR2LDark(int x, int y, const char *string, int alpha);

void Menu_DrawTextBox(int x, int y, int width, int lines);
void Menu_DrawBanner(char *name);
void Menu_DrawStatusBar(const char *string); //mxd

void UI_Draw_Cursor(void);

int UI_CenteredX(const menucommon_s *generic, const int menux); //mxd
int UI_MenuDepth(); //mxd. Returns current menu depth

//=======================================================

// menu_main.c

#define NUM_MAINMENU_CURSOR_FRAMES	15

#define MOUSEBUTTON1	0
#define MOUSEBUTTON2	1

#define LOADSCREEN_NAME		"/gfx/ui/unknownmap.pcx"
#define UI_BACKGROUND_NAME	"/gfx/ui/menu_background.pcx"
#define UI_NOSCREEN_NAME	"/gfx/ui/noscreen.pcx"

/*#define UI_MOUSECURSOR_MAIN_PIC		"/gfx/ui/cursors/m_cur_main.pcx"
#define UI_MOUSECURSOR_HOVER_PIC	"/gfx/ui/cursors/m_cur_hover.pcx"
#define UI_MOUSECURSOR_CLICK_PIC	"/gfx/ui/cursors/m_cur_click.pcx"
#define UI_MOUSECURSOR_OVER_PIC		"/gfx/ui/cursors/m_cur_over.pcx"
#define UI_MOUSECURSOR_TEXT_PIC		"/gfx/ui/cursors/m_cur_text.pcx"*/

#define UI_MOUSECURSOR_PIC	"/gfx/ui/cursors/m_mouse_cursor.pcx"

extern cvar_t *ui_cursor_scale;
cursor_t cursor;

static char *menu_in_sound		= "misc/menu1.wav";
static char *menu_move_sound	= "misc/menu2.wav";
static char *menu_out_sound		= "misc/menu3.wav";

extern qboolean m_entersound; // Played after drawing a frame, so caching won't disrupt the sound

void M_Menu_Main_f(void);
	void M_Menu_Game_f(void);
		void M_Menu_LoadGame_f(void);
		void M_Menu_SaveGame_f(void);
		void M_Menu_PlayerConfig_f(void);
			void M_Menu_DownloadOptions_f(void);
		void M_Menu_Credits_f(void);
	void M_Menu_Multiplayer_f(void);
		void M_Menu_JoinServer_f(void);
			void M_Menu_AddressBook_f(void);
		void M_Menu_StartServer_f(void);
			void M_Menu_DMOptions_f(void);
		void M_Menu_PlayerConfig_f(void);
		void M_Menu_DownloadOptions_f(void);
	void M_Menu_Video_f(void);
		void M_Menu_Video_Advanced_f(void);
	void M_Menu_Options_f(void);
		void M_Menu_Options_Sound_f(void);
		void M_Menu_Options_Controls_f(void);
			void M_Menu_Keys_f(void);
		void M_Menu_Options_Screen_f(void);
		void M_Menu_Options_Effects_f(void);
		void M_Menu_Options_Interface_f(void);
	void M_Menu_Quit_f(void);

	//void M_Menu_Credits(void); //mxd. Missing definition

static char *creditsBuffer;

// ui_subsystem.c
void UI_AddButton(buttonmenuobject_t *thisObj, int index, float x, float y, float w, float h);
void UI_AddMainButton(mainmenuobject_t *thisObj, int index, int x, int y, char *name);
void UI_RefreshCursorMenu(void);
void UI_RefreshCursorLink(void);
void UI_RefreshCursorButtons(void);
void UI_PushMenu(void (*draw)(void), const char *(*key)(int k));
//void UI_ForceMenuOff(void); //mxd. Redundant declaration
void UI_PopMenu(void);
void UI_BackMenu(void *unused);
const char *Default_MenuKey(menuframework_s *m, int key);

// ui_main.c
void M_Main_Draw(void);
void UI_CheckMainMenuMouse(void);

// ui_game_saveload.c
void UI_InitSavegameData(void);

// ui_credits.c
void M_Credits_MenuDraw(void);

// ui_options_screen.c
void Options_Screen_MenuDraw(void);
void MenuCrosshair_MouseClick(void);

// ui_mp_startserver.c
//void UI_LoadArenas(void); //mxd. Used in ui_mp_startserver.c only
void UI_LoadMapList(void);

// ui_mp_playersetup.c
void PlayerConfig_MenuDraw(void);
void PlayerConfig_MouseClick(void);
void PConfigAccept(void);