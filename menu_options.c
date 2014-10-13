/*
Copyright (C) 2011 ezQuake team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*

	Options Menu module

	Uses Ctrl_Tab and Settings modules

	Naming convention:
	function mask | usage    | purpose
	--------------|----------|-----------------------------
	Menu_Opt_*    | external | interface for menu.c
	CT_Opt_*      | external | interface for Ctrl_Tab.c

	made by:
		johnnycz, Jan 2006

*/

#include "quakedef.h"
#include "settings.h"
#include "settings_page.h"
#include "Ctrl_EditBox.h"
#include "vx_stuff.h"
#include "vx_tracker.h"
#include "gl_model.h"
#include "gl_local.h"
#include "tr_types.h"
#include "teamplay.h"
#include "EX_FileList.h"
#include "Ctrl.h"
#include "Ctrl_Tab.h"
#include "input.h"
#include "qsound.h"
#include "menu.h"
#include "keys.h"
#include "hud.h"
#include "hud_common.h"

typedef enum {
	OPTPG_GAME,
	OPTPG_BINDS,
	OPTPG_VIDEO
}	options_tab_t;

CTab_t options_tab;
int options_unichar;	// variable local to this module
cvar_t menu_advanced = { "menu_advanced", "0" };

static qbool InvertMouse(void) { return m_pitch.value < 0; }
const char* InvertMouseRead(void) { return InvertMouse() ? "on" : "off"; }
void InvertMouseToggle(qbool back) { Cvar_SetValue(&m_pitch, -m_pitch.value); }

static int GrenTimers(void) { return cf_grentimers.value; }
const char* GrenTimersRead(void) {
	if (GrenTimers() == 1) {
		return "on";
	}
	else if (GrenTimers() == 2) {
		return "no timer sound";
	}
	return "off";
}
void GrenTimersToggle() {
	int newvalue;

	if (cf_grentimers.value == 2) {
		newvalue = 0;
	}
	else {
		newvalue = cf_grentimers.value + 1;
	}

	Cvar_SetValue(&cf_grentimers, newvalue);
}

static int AutoId(void) { return cf_autoid.value; }
const char* AutoIdRead(void) {
	if (AutoId() == 1) {
		return "on";
	}
	else if (AutoId() == 2) {
		return "only friendlies";
	}
	else if (AutoId() == 3) {
		return "only enemies";
	}
	return "off";
}
void AutoIdToggle() {
	int newvalue;

	if (cf_autoid.value == 3) {
		newvalue = 0;
	}
	else {
		newvalue = cf_autoid.value + 1;
	}

	Cvar_SetValue(&cf_autoid, newvalue);
}

// GAME SETTINGS TAB
extern cvar_t name, cf_sensitivity, in_raw, s_volume, cf_fov, cf_grentimers, cf_autoid;
settings_page settgame;
setting settgame_arr[] = {
	ADDSET_SEPARATOR("Game Settings"),
	ADDSET_STRING	("Name", name),
	ADDSET_NUMBER	("Mouse sensitivity", cf_sensitivity, 1, 20, 0.25),
	ADDSET_CUSTOM	("Invert mouse", InvertMouseRead, InvertMouseToggle, "Inverts the Y axis."),
	ADDSET_BOOL	("Raw mouse input", in_raw),
	ADDSET_NUMBER	("Volume", s_volume, 0, 1, 0.05),
	ADDSET_NUMBER   ("Field of View", cf_fov, 40, 140, 2),
	ADDSET_SEPARATOR("HUD Settings"),
	ADDSET_CUSTOM   ("Grenade timers", GrenTimersRead, GrenTimersToggle, "Displays a grenade countdown."),
	ADDSET_CUSTOM   ("Auto ID", AutoIdRead, AutoIdToggle, "Auto identifies targets."),
};

void CT_Opt_Game_Draw (int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page) {
	Settings_Draw(x, y, w, h, &settgame);
}

int CT_Opt_Game_Key (int k, wchar unichar, CTab_t *tab, CTabPage_t *page) {
	return Settings_Key(&settgame, k, unichar);
}

void OnShow_SettGame (void) { Settings_OnShow(&settgame); }

qbool CT_Opt_Game_Mouse_Event(const mouse_state_t *ms)
{
	return Settings_Mouse_Event(&settgame, ms);
}

CTabPage_Handlers_t options_game_handlers = {
	CT_Opt_Game_Draw,
	CT_Opt_Game_Key,
	OnShow_SettGame,
	CT_Opt_Game_Mouse_Event
};

// BINDINGS TAB

settings_page settbinds;
setting settbinds_arr[] = {
	ADDSET_SEPARATOR("Movement"),
	ADDSET_BIND("forward", "+forward"),
	ADDSET_BIND("back", "+back"),
	ADDSET_BIND("strafe left", "+moveleft"),
	ADDSET_BIND("strafe right", "+moveright"),
	ADDSET_BIND("jump/swim up", "+jump"),
	ADDSET_BIND("swim down", "+movedown"),

	ADDSET_SEPARATOR("Communication"),
	ADDSET_BIND("public message", "messagemode"),
	ADDSET_BIND("team message", "messagemode2"),
	ADDSET_BIND("call medic", "saveme"),

	ADDSET_SEPARATOR("Gameplay"),
	ADDSET_BIND("change class", "changeclass"),
	ADDSET_BIND("change team", "changeteam"),
	ADDSET_BIND("discard ammo", "discard"),
	ADDSET_BIND("drop flag", "dropitems"),
	ADDSET_BIND("class special", "special"),

	ADDSET_SEPARATOR("Combat"),
	ADDSET_BIND("attack", "+attack"),
	ADDSET_BIND("next weapon", "weapnext"),
	ADDSET_BIND("previous weapon", "weapprev"),
	ADDSET_BIND("last weapon", "weaplast"),
	ADDSET_BIND("reload", "reload"),
	ADDSET_BIND("hand grenade", "+gren1"),
	ADDSET_BIND("special grenade", "+gren2"),
	ADDSET_BIND("switch grenades", "grenswitch"),

	ADDSET_SEPARATOR("Miscellaneous"),
	ADDSET_BIND("take screenshot", "screenshot"),
	ADDSET_BIND("show scores", "+showteamscores"),
};

void CT_Opt_Binds_Draw (int x2, int y2, int w, int h, CTab_t *tab, CTabPage_t *page) {
	Settings_Draw(x2, y2, w, h, &settbinds);
}

int CT_Opt_Binds_Key (int k, wchar unichar, CTab_t *tab, CTabPage_t *page) {
	return Settings_Key(&settbinds, k, unichar);
}

void OnShow_SettBinds(void) { Settings_OnShow(&settbinds); }

qbool CT_Opt_Binds_Mouse_Event(const mouse_state_t *ms)
{
	return Settings_Mouse_Event(&settbinds, ms);
}

CTabPage_Handlers_t options_bindings_handlers = {
	CT_Opt_Binds_Draw,
	CT_Opt_Binds_Key,
	OnShow_SettBinds,
	CT_Opt_Binds_Mouse_Event
};

// VIDEO TAB
//
// these variables serve as a temporary storage for user selected settings
// they get initialized with current settings when the page is showed
typedef struct menu_video_settings_s {
	int res;
	int bpp;
	qbool fullscreen;
} menu_system_settings_t;
qbool mss_askmode = false;

// here we store the configuration that user selected in menu
menu_system_settings_t mss_selected;

// here we store the current video config in case the new video settings weren't successfull
menu_system_settings_t mss_previous;

// now, this is a hack for SDL2
SDL_DisplayMode *glmodes = NULL;
int glmodes_size = 0;

// will apply given video settings
static void ApplyVideoSettings(const menu_system_settings_t *s)
{
	SDL_DisplayMode *current = &glmodes[s->res];
	Cvar_SetValue(&vid_width, current->w);
	Cvar_SetValue(&vid_height, current->h);
	Cvar_SetValue(&r_displayRefresh, current->refresh_rate);
	Cvar_SetValue(&r_colorbits, s->bpp);
	Cvar_SetValue(&r_fullscreen, s->fullscreen);
	Cbuf_AddText("vid_restart\n");
	Com_Printf("askmode: %s\n", mss_askmode ? "on" : "off");
}

// will store current video settings into the given structure
static void StoreCurrentVideoSettings(menu_system_settings_t *out)
{
	out->res = VID_GetCurrentModeIndex();
	out->bpp = (int) r_colorbits.value;
	out->fullscreen = (int) r_fullscreen.value;
}

// performed when user hits the "apply" button
void VideoApplySettings (void)
{
	StoreCurrentVideoSettings(&mss_previous);

	ApplyVideoSettings(&mss_selected);

	mss_askmode = true;
}

// two possible results of the "keep these video settings?" dialogue
static void KeepNewVideoSettings (void) { mss_askmode = false; }
static void CancelNewVideoSettings (void) {
	mss_askmode = false;
	ApplyVideoSettings(&mss_previous);
}

const char* BitDepthRead(void) { return mss_selected.bpp == 32 ? "32 bit" : mss_selected.bpp == 16 ? "16 bit" : "use desktop settings"; }
const char* ResolutionRead(void) {
	static char modebuf[64];

	VID_GetModeList(&glmodes, &glmodes_size);
	if (glmodes == NULL) return "";

	SDL_DisplayMode *mode = &glmodes[bound(0, mss_selected.res, glmodes_size-1)];
	snprintf(modebuf, sizeof(modebuf), "%dx%d@%dHz", mode->w, mode->h, mode->refresh_rate);

	return modebuf;
}
const char* FullScreenRead(void) { return mss_selected.fullscreen ? "on" : "off"; }

void ResolutionToggle(qbool back) {
	if (glmodes == NULL) return;
	if (back) mss_selected.res--; else mss_selected.res++;
	mss_selected.res = (mss_selected.res + glmodes_size) % glmodes_size;
}
void BitDepthToggle(qbool back) {
	if (back) {
		switch (mss_selected.bpp) {
		case 0: mss_selected.bpp = 32; return;
		case 16: mss_selected.bpp = 0; return;
		default: mss_selected.bpp = 16; return;
		}
	} else {
		switch (mss_selected.bpp) {
		case 0: mss_selected.bpp = 16; return;
		case 16: mss_selected.bpp = 32; return;
		case 32: mss_selected.bpp = 0; return;
		}
	}
}
void FullScreenToggle(qbool back) { mss_selected.fullscreen = mss_selected.fullscreen ? 0 : 1; }
extern cvar_t v_gamma;
settings_page settvideo;
setting settvideo_arr[] = {

	ADDSET_SEPARATOR("Video"),
	ADDSET_NUMBER	("Gamma", v_gamma, 0.1, 2.0, 0.1),
	ADDSET_NUMBER	("Brightness", gl_brightness, 0.0, 1.0, 0.1),
	ADDSET_CUSTOM("Resolution", ResolutionRead, ResolutionToggle, "Change your screen resolution."),
	ADDSET_BOOL("Vertical Sync", r_swapInterval),
	ADDSET_CUSTOM("Fullscreen", FullScreenRead, FullScreenToggle, "Toggle between fullscreen and windowed mode."),
	ADDSET_ACTION("Apply Changes", VideoApplySettings, "Restarts the renderer and applies the selected resolution."),
#if 0
	ADDSET_NUMBER	("Contrast", v_contrast, 1, 5, 0.1),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_BOOL	("Clear Video Buffer", gl_clear),
	ADDSET_NUMBER	("Anisotropy Filter", gl_anisotropy, 0, 16, 1),
	ADDSET_ENUM	("Quality Mode", gl_texturemode, gl_texturemode_enum),
	ADDSET_BASIC_SECTION(),
	ADDSET_BOOL	("Advanced Options", menu_advanced),
	ADDSET_CUSTOM	("FPS Limit", FpslimitRead, FpslimitToggle, "Limits the amount of frames rendered per second. May help with lag; best to consult forums about the best value for your setup."),
#endif
};

void CT_Opt_Video_Draw (int x2, int y2, int w, int h, CTab_t *tab, CTabPage_t *page) {
	#define ASKBOXWIDTH 300
	if(mss_askmode)
	{
		UI_DrawBox((w-ASKBOXWIDTH)/2, h/2 - 16, ASKBOXWIDTH, 32);
		UI_Print_Center((w-ASKBOXWIDTH)/2, h/2 - 8, ASKBOXWIDTH, "Do you wish to keep these settings?", false);
		UI_Print_Center((w-ASKBOXWIDTH)/2, h/2, ASKBOXWIDTH, "(y/n)", true);
	}
	else
	{
		Settings_Draw(x2, y2, w, h, &settvideo);
	}
}

int CT_Opt_Video_Key (int key, wchar unichar, CTab_t *tab, CTabPage_t *page) {
	if (mss_askmode)
	{

		if (key == 'y' || key == K_ENTER)
		{
			KeepNewVideoSettings();
		}
		else if(key == 'n' || key == K_ESCAPE)
		{
			CancelNewVideoSettings();
		}
		return true;
	}
	else
	{
		return Settings_Key(&settvideo, key, unichar);
	}
}

void OnShow_SettVideo(void)
{
	StoreCurrentVideoSettings(&mss_selected);
	Settings_OnShow(&settvideo);
}

qbool CT_Opt_Video_Mouse_Event(const mouse_state_t *ms)
{
	return Settings_Mouse_Event(&settvideo, ms);
}

CTabPage_Handlers_t options_video_handlers = {
	CT_Opt_Video_Draw,
	CT_Opt_Video_Key,
	OnShow_SettVideo,
	CT_Opt_Video_Mouse_Event
};

//

void Menu_Options_Key(int key, wchar unichar) {
	int handled = CTab_Key(&options_tab, key, unichar);
	options_unichar = unichar;

	if (!handled && (key == K_ESCAPE || key == K_MOUSE2))
		M_Menu_Main_f();
}

void Menu_Options_Draw(void) {
	int x, y, w, h;

	M_Unscale_Menu();

	// this will add top, left and bottom padding
	// right padding is not added because it causes annoying scrollbar behaviour
	// when mouse gets off the scrollbar to the right side of it
	w = vid.width - OPTPADDING; // here used to be a limit to 512x... size
	h = vid.height - OPTPADDING*2;
	x = OPTPADDING;
	y = OPTPADDING;

	CTab_Draw(&options_tab, x, y, w, h);
}

qbool Menu_Options_Mouse_Event(const mouse_state_t *ms)
{
	mouse_state_t nms = *ms;

	if (ms->button_up == 2) {
		Menu_Options_Key(K_MOUSE2, 0);
		return true;
	}

	// we are sending relative coordinates
	nms.x -= OPTPADDING;
	nms.y -= OPTPADDING;
	nms.x_old -= OPTPADDING;
	nms.y_old -= OPTPADDING;

	if (nms.x < 0 || nms.y < 0) return false;

	return CTab_Mouse_Event(&options_tab, &nms);
}

void Menu_Options_Init(void) {
	Settings_MainInit();

	Settings_Page_Init(settgame, settgame_arr);
	Settings_Page_Init(settbinds, settbinds_arr);
	Settings_Page_Init(settvideo, settvideo_arr);

	Cvar_SetCurrentGroup(CVAR_GROUP_MENU);
	Cvar_ResetCurrentGroup();

	CTab_Init(&options_tab);
	CTab_AddPage(&options_tab, "Game", OPTPG_GAME, &options_game_handlers);
	CTab_AddPage(&options_tab, "Key bindings", OPTPG_BINDS, &options_bindings_handlers);
	CTab_AddPage(&options_tab, "Video", OPTPG_VIDEO, &options_video_handlers);
	CTab_SetCurrentId(&options_tab, OPTPG_GAME);
}
