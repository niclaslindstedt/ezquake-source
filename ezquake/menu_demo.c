/*

	Demo Menu module

	Stands between menu.c and Ctrl_Tab.c

	Naming convention:
	function mask | usage    | purpose
	--------------|----------|-----------------------------
	Menu_Demo_*   | external | interface for menu.c
	CT_Demo_*     | external | interface for Ctrl_Tab.c
	CL_Demo_*_f   | external | interface for cl_demo.c
	M_Demo_*_f    | external | commands issued by the user
	Demo_         | internal | internal (static) functions

	made by:
		johnnycz, Dec 2006
	last edit:
		$Id: menu_demo.c,v 1.3 2006-12-19 20:40:21 johnnycz Exp $

*/

#include "quakedef.h"

#ifdef _WIN32
#define    DEMO_TIME    FILETIME
#else
#define    DEMO_TIME    time_t
#endif

#define MAX_DEMO_NAME    MAX_OSPATH
#define MAX_DEMO_FILES    2048
#define DEMO_MAXLINES    17

#define DEMO_PLAYLIST_OPTIONS_MAX 5
#define DEMO_PLAYLIST_MAX 100

#define DEMO_OPTIONS_MAX 2

#define DEMO_TAB_MAIN 0
#define DEMO_TAB_PLAYLIST 1
#define DEMO_TAB_OPTIONS 2
#define DEMO_TAB_MAX 2

#define DEMO_PLAYLIST_TAB_MAIN 0

typedef enum direntry_type_s {dt_file = 0, dt_dir, dt_up, dt_msg} direntry_type_t;

typedef enum {
    DEMOPG_BROWSER,				// browse page
	DEMOPG_PLAYLIST,	// playlist page
	DEMOPG_ENTRY, // entry page
	DEMOPG_OPTIONS  // options page
}	demo_tab_t;

typedef struct direntry_s {
	direntry_type_t    type;
	char            *name;
	int                size;
	DEMO_TIME        time;
} direntry_t;

typedef struct demo_playlist_s
{
	char name[_MAX_PATH];
	char path[_MAX_PATH];
	char trackname[16];
} demo_playlist_t;

extern cvar_t demo_dir;
#ifdef GLQUAKE
extern cvar_t     scr_scaleMenu;
extern int        menuwidth;
extern int        menuheight;
#else
#define menuwidth vid.width
#define menuheight vid.height
#endif

extern cvar_t scr_scaleMenu;

// demo browser container
filelist_t demo_filelist;

// demo browser cvars
cvar_t  demo_browser_showsize   = {"demo_browser_showsize",   "1"};
cvar_t  demo_browser_showdate   = {"demo_browser_showdate",   "1"};
cvar_t  demo_browser_showtime   = {"demo_browser_showtime",   "0"};
cvar_t  demo_browser_sortmode   = {"demo_browser_sortmode",   "1"};
cvar_t  demo_browser_showstatus = {"demo_browser_showstatus", "1"};
cvar_t  demo_browser_stripnames = {"demo_browser_stripnames", "1"};
cvar_t  demo_browser_interline  = {"demo_browser_interline",  "0"};

// demo menu container
CTab_t demo_tab;

// playlist structures
demo_playlist_t demo_playlist[256];

char track_name[16];
char default_track[16];
int demo_playlist_started = 0;

cvar_t    demo_playlist_loop = {"demo_playlist_loop","0"};
cvar_t    demo_playlist_track_name = {"demo_playlist_track_name",""};

static int demo_playlist_base = 0;
static int demo_playlist_current_played = 0;
static int demo_playlist_cursor = 0;
static int demo_playlist_num = 0;
static int demo_playlist_opt_cursor = 0;
static int demo_playlist_section = DEMO_PLAYLIST_TAB_MAIN;
static int demo_playlist_started_test = 0;

static int demo_options_cursor = 0;

char demo_track[16];


// Demo Playlist Functions

void M_Demo_Playlist_stop_f (void){
	if (!demo_playlist_started_test){
		demo_playlist_started = 0;
		demo_playlist_started_test = 0;
	}
}

static void Demo_playlist_start (int i){
	//    int test;
	key_dest = key_game;
	m_state = m_none;
	demo_playlist_current_played = i;
	demo_playlist_started_test = 0 ;
	if (cls.demoplayback)
		CL_Disconnect_f();
	demo_playlist_started_test = 0 ;
	demo_playlist_started=1;
	strlcpy(track_name,demo_playlist[demo_playlist_current_played].trackname,sizeof(demo_playlist[demo_playlist_current_played].trackname));
	Cbuf_AddText (va("playdemo \"%s\"\n", demo_playlist[demo_playlist_current_played].path));

}

void CL_Demo_playlist_f (void) {
	demo_playlist_current_played++;
	strlcpy(track_name,demo_playlist[demo_playlist_current_played].trackname,sizeof(demo_playlist[demo_playlist_current_played].trackname));

	if (demo_playlist_current_played == demo_playlist_num  && demo_playlist_loop.value ){
		demo_playlist_current_played = 0;
		Cbuf_AddText (va("playdemo \"%s\"\n", demo_playlist[demo_playlist_current_played].path));
	} else if (demo_playlist_current_played == demo_playlist_num ){
		Com_Printf("End of demo playlist.\n");
		demo_playlist_started = demo_playlist_current_played = 0;
	} else{
		Cbuf_AddText (va("playdemo \"%s\"\n", demo_playlist[demo_playlist_current_played].path));
	}
}

void M_Demo_Playlist_Next_f (void){
	int tmp;

	if (demo_playlist_started == 0)
		return;
	tmp = demo_playlist_current_played + 1 ;
	if (tmp>demo_playlist_num-1)
		tmp = 0 ;

	Demo_playlist_start(tmp);
}

void M_Demo_Playlist_Prev_f (void){
	int tmp;
	if (demo_playlist_started == 0)
		return;
	tmp = demo_playlist_current_played - 1 ;
	if (tmp<0)
		tmp = demo_playlist_num - 1 ;

	Com_Printf("Prev %i\n",tmp);
	Demo_playlist_start(tmp);
}

void M_Demo_Playlist_Clear_f (void){

	int i;

	if (demo_playlist_num == 0)
		return;

	for (i=0; i<=demo_playlist_num;i++){
		demo_playlist[i].name[0]='\0';
		demo_playlist[i].path[0]='\0';
		demo_playlist[i].trackname[0]='\0';
	}
	demo_playlist_num = demo_playlist_started = 0;

}

void M_Demo_Playlist_Stop_f (void){
	M_Demo_Playlist_stop_f();
	Cbuf_AddText("disconnect\n");
}

static void Demo_Playlist_Setup_f (void) {
	strlcpy(demo_track,demo_playlist[demo_playlist_cursor + demo_playlist_base].trackname,sizeof(demo_playlist[demo_playlist_cursor + demo_playlist_base].trackname));
	strlcpy(default_track,demo_playlist_track_name.string,16);
}

static void Demo_Playlist_Del (int i) {
	int y;
	demo_playlist[i].name[0] = '\0';
	demo_playlist[i].path[0] = '\0';
	demo_playlist[i].trackname[0] = '\0';

	for (y=i ; y<=256 && demo_playlist[y+1].name[0]!='\0'; y++ ){
		strlcpy(demo_playlist[y].name,demo_playlist[y+1].name,sizeof(demo_playlist[y+1].name)) ;
		strlcpy(demo_playlist[y].path,demo_playlist[y+1].path,sizeof(demo_playlist[y+1].path)) ;
		strlcpy(demo_playlist[y].trackname,demo_playlist[y+1].trackname,sizeof(demo_playlist[y+1].trackname)) ;
		demo_playlist[y+1].name[0] = '\0';
		demo_playlist[y+1].path[0] = '\0';
		demo_playlist[y+1].trackname[0] = '\0';
	}
	demo_playlist_num--;
	if (demo_playlist_num < 0 )
		demo_playlist_num = 0;
	demo_playlist_cursor--;
	if (demo_playlist_cursor < 0 )
		demo_playlist_cursor = 0;
}

static void Demo_Playlist_Move_Up (int i) {
	demo_playlist_t tmp;
	strlcpy(tmp.name,demo_playlist[i-1].name,sizeof(demo_playlist[i-1].name));
	strlcpy(tmp.path,demo_playlist[i-1].path,sizeof(demo_playlist[i-1].path));
	strlcpy(tmp.trackname,demo_playlist[i-1].trackname,sizeof(demo_playlist[i-1].trackname));
	strlcpy(demo_playlist[i-1].name,demo_playlist[i].name,sizeof(demo_playlist[i-1].name));
	strlcpy(demo_playlist[i-1].path,demo_playlist[i].path,sizeof(demo_playlist[i-1].path));
	strlcpy(demo_playlist[i-1].trackname,demo_playlist[i].trackname,sizeof(demo_playlist[i-1].trackname));
	strlcpy(demo_playlist[i].name,tmp.name,sizeof(tmp.name));
	strlcpy(demo_playlist[i].path,tmp.path,sizeof(tmp.path));
	strlcpy(demo_playlist[i].trackname,tmp.trackname,sizeof(tmp.trackname));
}

static void Demo_Playlist_Move_Down (int i) {
	demo_playlist_t tmp;

	if(i+1 == demo_playlist_num )
		return;

	strlcpy(tmp.name,demo_playlist[i+1].name,sizeof(demo_playlist[i+1].name));
	strlcpy(tmp.path,demo_playlist[i+1].path,sizeof(demo_playlist[i+1].path));
	strlcpy(tmp.trackname,demo_playlist[i+1].trackname,sizeof(demo_playlist[i+1].trackname));
	strlcpy(demo_playlist[i+1].name,demo_playlist[i].name,sizeof(demo_playlist[i+1].name));
	strlcpy(demo_playlist[i+1].path,demo_playlist[i].path,sizeof(demo_playlist[i+1].path));
	strlcpy(demo_playlist[i+1].trackname,demo_playlist[i].trackname,sizeof(demo_playlist[i+1].trackname));
	strlcpy(demo_playlist[i].name,tmp.name,sizeof(tmp.name));
	strlcpy(demo_playlist[i].path,tmp.path,sizeof(tmp.path));
	strlcpy(demo_playlist[i].trackname,tmp.trackname,sizeof(tmp.trackname));
}

static void Demo_Playlist_SelectPrev(void)
{
	demo_playlist_cursor -= DEMO_MAXLINES - 1;
	if (demo_playlist_cursor < 0) {
		demo_playlist_base += demo_playlist_cursor;
		if (demo_playlist_base < 0)
			demo_playlist_base = 0;
		demo_playlist_cursor = 0;
	}
	Demo_Playlist_Setup_f();
}

static void Demo_Playlist_SelectNext(void)
{
	demo_playlist_cursor += DEMO_MAXLINES - 1;
	if (demo_playlist_base + demo_playlist_cursor >= demo_playlist_num)
		demo_playlist_cursor = demo_playlist_num - demo_playlist_base - 1;
	if (demo_playlist_cursor >= DEMO_MAXLINES) {
		demo_playlist_base += demo_playlist_cursor - (DEMO_MAXLINES - 1);
		demo_playlist_cursor = DEMO_MAXLINES - 1;
		if (demo_playlist_base + demo_playlist_cursor >= demo_playlist_num)
			demo_playlist_base = demo_playlist_num - demo_playlist_cursor - 1;
	}
	Demo_Playlist_Setup_f();
}

static void Demo_FormatSize (char *t) {
	char *s;

	for (s = t; *s; s++) {
		if (*s >= '0' && *s <= '9')
			*s = *s - '0' + 18;
		else
			*s |= 128;
	}
}

// ============
// <draw pages>

void CT_Demo_Browser_Draw(int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page)
{
    FL_Draw(&demo_filelist, x, y, w, h);
}


void CT_Demo_Playlist_Draw(int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page)
{
	int i, y2;
	y = y - 48;

	if(demo_playlist_num == 0)
		M_PrintWhite(96, y + 96, "Playlist is empty");
	else{
		for (i = 0; i <= demo_playlist_num - demo_playlist_base && i < DEMO_MAXLINES; i++) {
			y2 = 32 + i * 8 ;
			if (demo_playlist_cursor == i)
				M_PrintWhite (24, y + y2, demo_playlist[demo_playlist_base + i].name);
			else
				M_Print (24, y + y2, demo_playlist[demo_playlist_base + i].name);
		}
		M_DrawCharacter (8, y + 32 + demo_playlist_cursor * 8, FLASHINGARROW());
	}
}

void CT_Demo_Entry_Draw(int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page)
{
	int z;
	y = y - 32;

	if (demo_playlist_started && cls.demoplayback){
		M_Print (24, y + 32, "Currently playing:");
		M_PrintWhite (24, y + 40, demo_playlist[demo_playlist_current_played].name);
	} else{
		M_Print (24, y + 32, "Not playing anything");
	}
	M_Print    (24, y + 56, "Next     demo");
	M_Print    (24, y + 64, "Previous demo");
	M_Print (24, y + 72, "Stop  playlist");
	M_Print (24, y + 80, "Clear playlist");

	if (demo_playlist_num > 0) {
		M_Print (24, y + 96, "Currently selected:");
		M_Print (24, y + 104, demo_playlist[demo_playlist_cursor].name);
	} else {
		M_Print (24, y + 96, "No demo in playlist");
	}

	if (strcasecmp(demo_playlist[demo_playlist_cursor].name + strlen(demo_playlist[demo_playlist_cursor].name) - 4, ".mvd")){
		M_Print (24, y + 120, "Tracking only available with mvds");
	} else{
		M_Print (24, y + 120, "Track");
		M_DrawTextBox (160, y + 112, 16, 1);
		M_PrintWhite (168, y + 120, demo_track);
		if (demo_playlist_opt_cursor == 4 && demo_playlist_num > 0)
			M_DrawCharacter (168 + 8*strlen(demo_track), 120 + y, FLASHINGCURSOR());
	}
	
	z = demo_playlist_opt_cursor + (demo_playlist_opt_cursor >= 4 ? 4 : 0);
	z = y + 56 + z * 8;
	M_DrawCharacter (8, z, FLASHINGARROW());
}

void CT_Demo_Options_Draw(int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page)
{
	M_Print (16, y,  "Loop playlist");
	M_DrawCheckbox (220, y, demo_playlist_loop.value > 0);

	M_Print (16, 16+y, "Default track");
	M_DrawTextBox (160, 8+y, 16, 1);
	M_PrintWhite (168, 16+y, default_track);

	if (demo_options_cursor == 1)
		M_DrawCharacter (168 + 8*strlen(default_track), y + 16, FLASHINGCURSOR());

	M_DrawCharacter (8, y + demo_options_cursor * 16, FLASHINGARROW());
}

// in the end leads calls one of the four functions above
void Menu_Demo_Draw (void)
{
	extern void Demo_Draw(int, int, int, int);

	int x, y, w, h;

#ifdef GLQUAKE
	// do not scale this menu
	if (scr_scaleMenu.value) {
		menuwidth = vid.width;
		menuheight = vid.height;
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity ();
		glOrtho  (0, menuwidth, menuheight, 0, -99999, 99999);
	}
#endif

	w = min(max(512, 320), vid.width) - 8;
	h = min(max(432, 200), vid.height) - 8;
	x = (vid.width - w) / 2;
	y = (vid.height - h) / 2;

	CTab_Draw(&demo_tab, x, y, w, h);
}
// </draw pages>
// =============

// ==============================
// <key processing for each page>

int CT_Demo_Browser_Key(int key, CTab_t *tab, CTabPage_t *page)
{
	extern void M_ToggleMenu_f (void);
	extern void M_LeaveMenu (int);
    qbool processed;

    processed = FL_Key(&demo_filelist, key);

    if (!processed)
    {
        if (key == K_ENTER)
        {
			if (keydown[K_CTRL]) {
				snprintf(demo_playlist[demo_playlist_num].name, sizeof((*demo_playlist).name), "%s", FL_GetCurrentDisplay(&demo_filelist));
				snprintf(demo_playlist[demo_playlist_num].path, sizeof((*demo_playlist).path), "%s", FL_GetCurrentPath(&demo_filelist));
				demo_playlist_num++;
			} else if (keydown[K_SHIFT]) {
				M_LeaveMenu(m_main);
				M_ToggleMenu_f();
				Cbuf_AddText (va("timedemo \"%s\"\n", FL_GetCurrentPath(&demo_filelist)));
			} else {
				M_LeaveMenu(m_main);
				M_ToggleMenu_f();
				Cbuf_AddText(va("playdemo \"%s\"\n", FL_GetCurrentPath(&demo_filelist)));
				processed = true;
			}
        }
    }

    return processed;
}

int CT_Demo_Playlist_Key(int key, CTab_t *tab, CTabPage_t *page)
{
	switch (key) {
	case K_UPARROW:
		if (keydown[K_CTRL] && demo_playlist_cursor + demo_playlist_base > 0)
			Demo_Playlist_Move_Up(demo_playlist_cursor + demo_playlist_base);

		if (demo_playlist_cursor > 0) {
			demo_playlist_cursor--;
		} else if (demo_playlist_base > 0) {
			demo_playlist_base--;
		}
		Demo_Playlist_Setup_f();
		break;

	case K_DOWNARROW:
		if (keydown[K_CTRL] && demo_playlist_cursor + demo_playlist_base < demo_playlist_num)
			Demo_Playlist_Move_Down(demo_playlist_cursor + demo_playlist_base);

		if (demo_playlist_cursor + demo_playlist_base < demo_playlist_num - 1) {
			if (demo_playlist_cursor < DEMO_MAXLINES - 1)
				demo_playlist_cursor++;
			else
				demo_playlist_base++;
		}
		Demo_Playlist_Setup_f();
		break;

	case K_HOME:
		demo_playlist_cursor = 0;
		demo_playlist_base = 0;
		Demo_Playlist_Setup_f();
		break;

	case K_END:
		if (demo_playlist_num > DEMO_PLAYLIST_OPTIONS_MAX) {
			demo_playlist_cursor = DEMO_PLAYLIST_OPTIONS_MAX - 1;
			demo_playlist_base = demo_playlist_num - demo_playlist_cursor - 1;
		} else {
			demo_playlist_base = 0;
			demo_playlist_cursor = demo_playlist_num - 1;
		}
		Demo_Playlist_Setup_f();
		break;

	case K_PGUP:
		Demo_Playlist_SelectPrev();
		break;
	
	case K_PGDN:
		Demo_Playlist_SelectNext();
		break;

	case K_ENTER:
		Demo_playlist_start(demo_playlist_cursor + demo_playlist_base);
		break;

	case K_DEL:
		Demo_Playlist_Del(demo_playlist_cursor + demo_playlist_base);
		break;

	default: return false;
	}

	return true;
}

int CT_Demo_Entry_Key(int key, CTab_t *tab, CTabPage_t *page)
{
	int l;

	switch (key) {
	case K_LEFTARROW: return false;
	case K_RIGHTARROW: return false;
	case K_UPARROW:
		demo_playlist_opt_cursor = demo_playlist_opt_cursor ? demo_playlist_opt_cursor - 1 : DEMO_PLAYLIST_OPTIONS_MAX - 1;
		break;

	case K_DOWNARROW:
		demo_playlist_opt_cursor++;
		demo_playlist_opt_cursor = demo_playlist_opt_cursor % DEMO_PLAYLIST_OPTIONS_MAX;
		break;

	case K_PGUP:
		Demo_Playlist_SelectPrev();
		break;

	case K_PGDN:
		Demo_Playlist_SelectNext();
		break;

	case K_ENTER:
		if (demo_playlist_opt_cursor == 0)
			M_Demo_Playlist_Next_f();
		else if (demo_playlist_opt_cursor == 1)
			M_Demo_Playlist_Prev_f();
		else if (demo_playlist_opt_cursor == 2)
			M_Demo_Playlist_Stop_f();
		else if (demo_playlist_opt_cursor == 3)
			M_Demo_Playlist_Clear_f();
		
		break;

	case K_BACKSPACE:
		if (demo_playlist_opt_cursor == 4) {
			if (strlen(demo_track))
				demo_track[strlen(demo_track)-1] = 0;
			strlcpy(demo_playlist[demo_playlist_cursor + demo_playlist_base].trackname,demo_track,sizeof(demo_playlist->trackname));
		}
		break;


	default:
		if (key < 32 || key > 127)
			return false;

		if (demo_playlist_opt_cursor == 4) {
			l = strlen(demo_track);
			if (l < 15) {
				demo_track[l+1] = 0;
				demo_track[l] = key;
				strlcpy(demo_playlist[demo_playlist_cursor + demo_playlist_base].trackname,demo_track,sizeof(demo_playlist->trackname));
			}
			return true;
		} else return false;
	}

	return true;
}

int CT_Demo_Options_Key(int key, CTab_t *tab, CTabPage_t *page)
{
	int l;

	switch (key) {
	case K_LEFTARROW: return false;
	case K_RIGHTARROW: return false;
	case K_UPARROW:
		demo_options_cursor = demo_options_cursor ? demo_options_cursor - 1 : DEMO_OPTIONS_MAX - 1;
		Demo_Playlist_Setup_f();
		break;

	case K_DOWNARROW:
		demo_options_cursor = (demo_options_cursor + 1) % DEMO_OPTIONS_MAX;
		Demo_Playlist_Setup_f();
		break;

	case K_ENTER:
		Cvar_SetValue (&demo_playlist_loop, !demo_playlist_loop.value);
		break;

	case K_BACKSPACE:
		if (demo_options_cursor == 1) {
			if (strlen(default_track))
				default_track[strlen(default_track)-1] = 0;
			Cvar_Set(&demo_playlist_track_name, default_track);
		}
		break;

	default: 
		if (key < 32 || key > 127)
			return false;
		
		if (demo_options_cursor == 1) {
			l = strlen(default_track);

			if (l < sizeof(default_track)-1) {
				default_track[l+1] = 0;
				default_track[l] = key;
				Cvar_Set(&demo_playlist_track_name, default_track);
			}
			return true;
		} else return false;
	}

	return true;
}

// will lead to call of one of the 4 functions above
void Menu_Demo_Key(int key)
{
	extern void M_Menu_Main_f (void);

    int handled = CTab_Key(&demo_tab, key);

    if (!handled)
    {
        if (key == K_ESCAPE)
        {
            M_Menu_Main_f();
        }
    }
}
// </key processing for each page>

void Menu_Demo_Init(void)
{
	Cvar_SetCurrentGroup(CVAR_GROUP_DEMO);
	Cvar_Register(&demo_browser_showsize);
    Cvar_Register(&demo_browser_showdate);
    Cvar_Register(&demo_browser_showtime);
    Cvar_Register(&demo_browser_sortmode);
    Cvar_Register(&demo_browser_showstatus);
    Cvar_Register(&demo_browser_stripnames);
    Cvar_Register(&demo_browser_interline);
	Cvar_ResetCurrentGroup();

	Cvar_SetCurrentGroup(CVAR_GROUP_SCREEN);
	Cvar_Register (&demo_playlist_loop);
	Cvar_Register (&demo_playlist_track_name);
	Cvar_ResetCurrentGroup();

	Cmd_AddCommand ("demo_playlist_stop", M_Demo_Playlist_Stop_f);
	Cmd_AddCommand ("demo_playlist_next", M_Demo_Playlist_Next_f);
	Cmd_AddCommand ("demo_playlist_prev", M_Demo_Playlist_Prev_f);
	Cmd_AddCommand ("demo_playlist_clear", M_Demo_Playlist_Clear_f);

	FL_Init(&demo_filelist,
        &demo_browser_sortmode,
        &demo_browser_showsize,
        &demo_browser_showdate,
        &demo_browser_showtime,
        &demo_browser_stripnames,
        &demo_browser_interline,
        &demo_browser_showstatus,
		"./qw");
    FL_AddFileType(&demo_filelist, 0, ".qwd");
	FL_AddFileType(&demo_filelist, 1, ".qwz");
	FL_AddFileType(&demo_filelist, 2, ".mvd");
	FL_AddFileType(&demo_filelist, 3, ".dem");

	// initialize tab control
    CTab_Init(&demo_tab);
	CTab_AddPage(&demo_tab, "browser", DEMOPG_BROWSER, NULL, CT_Demo_Browser_Draw, CT_Demo_Browser_Key);
	CTab_AddPage(&demo_tab, "playlist", DEMOPG_PLAYLIST, NULL, CT_Demo_Playlist_Draw, CT_Demo_Playlist_Key);
	CTab_AddPage(&demo_tab, "entry", DEMOPG_ENTRY, NULL, CT_Demo_Entry_Draw, CT_Demo_Entry_Key);
	CTab_AddPage(&demo_tab, "options", DEMOPG_OPTIONS, NULL, CT_Demo_Options_Draw, CT_Demo_Options_Key);
	CTab_SetCurrentId(&demo_tab, DEMOPG_BROWSER);
}
