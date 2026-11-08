/* See LICENSE file for copyright and license details. */
/* torter's dwm. the old one is in config.h.before-claude */

#include <X11/XF86keysym.h>

/* appearance */
static const unsigned int borderpx  = 2;        /* border pixel of windows */
static const unsigned int gappx     = 8;        /* gap between tiled windows, none when one is alone */
static const unsigned int snap      = 16;       /* snap pixel */
static const int showbar            = 1;        /* 0 means no bar */
static const int topbar             = 1;        /* 0 means bottom bar */
/* text in liberation mono, the tag icons from the nerd font symbols */
static const char *fonts[]          = { "Liberation Mono:size=10", "Symbols Nerd Font Mono:size=11" };
static const char dmenufont[]       = "Liberation Mono:size=10";

/* night sky: a dark blue-black bar, a warm amber for whatever has focus */
static const char col_bg[]          = "#11131a";   /* bar */
static const char col_fg[]          = "#a9b1c6";   /* text */
static const char col_dim[]         = "#2a2f3d";   /* borders of windows you're not in */
static const char col_selfg[]       = "#11131a";   /* text on the amber */
static const char col_sel[]         = "#e0a43c";   /* focus */
static const char *colors[][3]      = {
	/*               fg         bg       border   */
	[SchemeNorm] = { col_fg,    col_bg,  col_dim },
	[SchemeSel]  = { col_selfg, col_sel, col_sel },
};

/* tagging: a number and what usually lives there. terminal, web, code, files, chat, music, games, video, other */
static const char *tags[] = {
	"1 ", "2 ", "3 ", "4 ", "5 ",
	"6 ", "7 ", "8 ", "9 ",
};

static const Rule rules[] = {
	/* xprop(1):
	 *	WM_CLASS(STRING) = instance, class
	 *	WM_NAME(STRING) = title
	 */
	/* class                  instance  title                 tags mask  isfloating  monitor */
	{ "Gimp",                 NULL,     NULL,                 0,         1,          -1 },
	{ "firefox",              NULL,     NULL,                 1 << 1,    0,          -1 },
	{ "steam",                NULL,     NULL,                 1 << 6,    0,          -1 },
	{ "kitty-float",          NULL,     NULL,                 0,         1,          -1 },
	{ "Pavucontrol",          NULL,     NULL,                 0,         1,          -1 },
	{ "Nm-connection-editor", NULL,     NULL,                 0,         1,          -1 },
	{ NULL,                   NULL,     "Picture-in-Picture", ~0,        1,          -1 },
};

/* layout(s) */
static const float mfact     = 0.55; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* number of clients in master area */
static const int resizehints = 0;    /* 0: terminals fill their tile, no gaps around kitty */
static const int lockfullscreen = 1; /* 1 will force focus on the fullscreen window */
static const int refreshrate = 120;  /* refresh rate (per second) for client move/resize */

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
};

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY,      toggletag,      {.ui = 1 << TAG} },

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static char dmenumon[8]; /* component of dmenucmd, manipulated in spawn() */
static const char *dmenucmd[] = { "dmenu_run", "-i", "-m", dmenumon, "-fn", dmenufont, "-nb", col_bg, "-nf", col_fg, "-sb", col_sel, "-sf", col_selfg, "-p", "run:", NULL };
static const char *termcmd[]  = { "kitty", NULL };
/* a terminal that floats in the middle, for quick things */
static const char *floatterm[] = { "kitty", "--class", "kitty-float", "-o", "initial_window_width=100c", "-o", "initial_window_height=30c", NULL };

/* sound through pipewire, never past 100% (the -l 1 below) */
static const char *micmute[] = { "wpctl", "set-mute", "@DEFAULT_AUDIO_SOURCE@", "toggle", NULL };
/* after a volume key, poke dwmstat so the bar shows the new number now, not in 20 seconds */
#define VOL(cmd) SHCMD(cmd "; pkill -USR1 -x dwmstat")

/* wallpapers: wall picks from ~/Pictures/wallpapers */
static const char *wallmenu[] = { "wall", NULL };
static const char *wallnext[] = { "wall", "next", NULL };
static const char *wallprev[] = { "wall", "prev", NULL };

static const Key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY,                       XK_d,      spawn,          {.v = dmenucmd } },
	{ MODKEY,                       XK_t,      spawn,          {.v = termcmd } },
	{ MODKEY|ShiftMask,             XK_Return, spawn,          {.v = termcmd } },
	{ MODKEY|ShiftMask,             XK_t,      spawn,          {.v = floatterm } },
	{ MODKEY,                       XK_b,      togglebar,      {0} },
	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } },
	/* i adds a window to the master side, o takes one off (it was on d, which also opened dmenu) */
	{ MODKEY,                       XK_i,      incnmaster,     {.i = +1 } },
	{ MODKEY,                       XK_o,      incnmaster,     {.i = -1 } },
	{ MODKEY,                       XK_h,      setmfact,       {.f = -0.05} },
	{ MODKEY,                       XK_l,      setmfact,       {.f = +0.05} },
	{ MODKEY|ShiftMask,             XK_h,      setmfact,       {.f = 1.55} },   /* back to the starting split */
	{ MODKEY,                       XK_Return, zoom,           {0} },
	{ MODKEY,                       XK_Tab,    view,           {0} },
	{ MODKEY,                       XK_q,      killclient,     {0} },
	/* layouts: r for tiled (it had no key), f floating, m monocle, space flips to the last one */
	{ MODKEY,                       XK_r,      setlayout,      {.v = &layouts[0]} },
	{ MODKEY,                       XK_f,      setlayout,      {.v = &layouts[1]} },
	{ MODKEY,                       XK_m,      setlayout,      {.v = &layouts[2]} },
	{ MODKEY,                       XK_space,  setlayout,      {0} },
	{ MODKEY|ShiftMask,             XK_space,  togglefloating, {0} },
	{ MODKEY,                       XK_0,      view,           {.ui = ~0 } },
	{ MODKEY|ShiftMask,             XK_0,      tag,            {.ui = ~0 } },
	{ MODKEY,                       XK_comma,  focusmon,       {.i = -1 } },
	{ MODKEY,                       XK_period, focusmon,       {.i = +1 } },
	{ MODKEY|ShiftMask,             XK_comma,  tagmon,         {.i = -1 } },
	{ MODKEY|ShiftMask,             XK_period, tagmon,         {.i = +1 } },
	/* the sound keys on the keyboard */
	{ 0,                            XF86XK_AudioRaiseVolume, spawn, VOL("wpctl set-volume -l 1 @DEFAULT_AUDIO_SINK@ 5%+") },
	{ 0,                            XF86XK_AudioLowerVolume, spawn, VOL("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-") },
	{ 0,                            XF86XK_AudioMute,        spawn, VOL("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle") },
	{ 0,                            XF86XK_AudioMicMute,     spawn, {.v = micmute } },
	/* gaps: super+minus smaller, super+equal bigger, super+shift+equal back to normal */
	{ MODKEY,                       XK_minus,  setgaps,        {.i = -2 } },
	{ MODKEY,                       XK_equal,  setgaps,        {.i = +2 } },
	{ MODKEY|ShiftMask,             XK_equal,  setgaps,        {.i = 0 } },
	/* wallpaper: super+w a menu, super+shift+w the next one, super+ctrl+w the one before */
	{ MODKEY,                       XK_w,      spawn,          {.v = wallmenu } },
	{ MODKEY|ShiftMask,             XK_w,      spawn,          {.v = wallnext } },
	{ MODKEY|ControlMask,           XK_w,      spawn,          {.v = wallprev } },
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	/* shift+q restarts dwm (.xinitrc starts it again, windows stay open), ctrl+shift+q logs out */
	{ MODKEY|ShiftMask,             XK_q,      quit,           {0} },
	{ MODKEY|ControlMask|ShiftMask, XK_q,      spawn,          SHCMD("touch \"$XDG_RUNTIME_DIR/dwm-logout\"; kill $PPID") },
};

/* button definitions */
/* click can be ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static const Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkLtSymbol,          0,              Button1,        setlayout,      {0} },
	{ ClkLtSymbol,          0,              Button3,        setlayout,      {.v = &layouts[2]} },
	{ ClkWinTitle,          0,              Button2,        zoom,           {0} },
	{ ClkStatusText,        0,              Button2,        spawn,          {.v = termcmd } },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkTagBar,            0,              Button1,        view,           {0} },
	{ ClkTagBar,            0,              Button3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         Button1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         Button3,        toggletag,      {0} },
};
