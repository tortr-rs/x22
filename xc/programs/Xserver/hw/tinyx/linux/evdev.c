/*
 * evdev.c: keyboards through /dev/input, for x8664.
 *
 * The old keyboard driver (keyboard.c) reads the console in raw mode and builds
 * its key map from the console's own map. The console has no idea what the
 * Windows/Super key is, so window managers that live on Super (dwm, most of them
 * now) never saw it. This reads every keyboard the kernel knows about from
 * /dev/input/event*, the same place libinput reads them, and uses its own map:
 * evdev key number + 8 is the X keycode, which is what every X server since
 * about 2008 does, so xmodmap files and keycodes from other X servers work here.
 *
 * The map is US English, with UK on top when X8664_KEYMAP=gb (or uk). More layouts
 * are a few lines each: the keys that differ from US.
 *
 * Copyright (c) 2026 torter and the Chamesle Linux people
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define NEED_EVENTS
#include "tinyx.h"
#include "txkeymap.h"
#include <X11/keysym.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define MAXKBD	8

static int evdevFds[MAXKBD];
static int evdevCount;
static int evdevType;
static int evdevDebug;	/* X8664_EVDEV_DEBUG=1 prints every key */

/* the XF86 keysyms for the media keys, as numbers so we don't need XF86keysym.h */
#define XF86AudioLowerVolume	0x1008FF11
#define XF86AudioMute		0x1008FF12
#define XF86AudioRaiseVolume	0x1008FF13
#define XF86AudioPlay		0x1008FF14
#define XF86AudioStop		0x1008FF15
#define XF86AudioPrev		0x1008FF16
#define XF86AudioNext		0x1008FF17
#define XF86MonBrightnessUp	0x1008FF02
#define XF86MonBrightnessDown	0x1008FF03
#define XF86AudioMicMute	0x1008FFB2

/* evdev key number: plain, with shift. US layout */
static const struct { int code; KeySym plain, shifted; } evdevMap[] = {
    { KEY_ESC, XK_Escape, NoSymbol },
    { KEY_1, XK_1, XK_exclam }, { KEY_2, XK_2, XK_at },
    { KEY_3, XK_3, XK_numbersign }, { KEY_4, XK_4, XK_dollar },
    { KEY_5, XK_5, XK_percent }, { KEY_6, XK_6, XK_asciicircum },
    { KEY_7, XK_7, XK_ampersand }, { KEY_8, XK_8, XK_asterisk },
    { KEY_9, XK_9, XK_parenleft }, { KEY_0, XK_0, XK_parenright },
    { KEY_MINUS, XK_minus, XK_underscore }, { KEY_EQUAL, XK_equal, XK_plus },
    { KEY_BACKSPACE, XK_BackSpace, NoSymbol }, { KEY_TAB, XK_Tab, XK_ISO_Left_Tab },
    { KEY_Q, XK_q, XK_Q }, { KEY_W, XK_w, XK_W }, { KEY_E, XK_e, XK_E },
    { KEY_R, XK_r, XK_R }, { KEY_T, XK_t, XK_T }, { KEY_Y, XK_y, XK_Y },
    { KEY_U, XK_u, XK_U }, { KEY_I, XK_i, XK_I }, { KEY_O, XK_o, XK_O },
    { KEY_P, XK_p, XK_P },
    { KEY_LEFTBRACE, XK_bracketleft, XK_braceleft },
    { KEY_RIGHTBRACE, XK_bracketright, XK_braceright },
    { KEY_ENTER, XK_Return, NoSymbol }, { KEY_LEFTCTRL, XK_Control_L, NoSymbol },
    { KEY_A, XK_a, XK_A }, { KEY_S, XK_s, XK_S }, { KEY_D, XK_d, XK_D },
    { KEY_F, XK_f, XK_F }, { KEY_G, XK_g, XK_G }, { KEY_H, XK_h, XK_H },
    { KEY_J, XK_j, XK_J }, { KEY_K, XK_k, XK_K }, { KEY_L, XK_l, XK_L },
    { KEY_SEMICOLON, XK_semicolon, XK_colon },
    { KEY_APOSTROPHE, XK_apostrophe, XK_quotedbl },
    { KEY_GRAVE, XK_grave, XK_asciitilde }, { KEY_LEFTSHIFT, XK_Shift_L, NoSymbol },
    { KEY_BACKSLASH, XK_backslash, XK_bar },
    { KEY_Z, XK_z, XK_Z }, { KEY_X, XK_x, XK_X }, { KEY_C, XK_c, XK_C },
    { KEY_V, XK_v, XK_V }, { KEY_B, XK_b, XK_B }, { KEY_N, XK_n, XK_N },
    { KEY_M, XK_m, XK_M },
    { KEY_COMMA, XK_comma, XK_less }, { KEY_DOT, XK_period, XK_greater },
    { KEY_SLASH, XK_slash, XK_question }, { KEY_RIGHTSHIFT, XK_Shift_R, NoSymbol },
    { KEY_KPASTERISK, XK_KP_Multiply, NoSymbol }, { KEY_LEFTALT, XK_Alt_L, XK_Meta_L },
    { KEY_SPACE, XK_space, NoSymbol }, { KEY_CAPSLOCK, XK_Caps_Lock, NoSymbol },
    { KEY_F1, XK_F1, NoSymbol }, { KEY_F2, XK_F2, NoSymbol }, { KEY_F3, XK_F3, NoSymbol },
    { KEY_F4, XK_F4, NoSymbol }, { KEY_F5, XK_F5, NoSymbol }, { KEY_F6, XK_F6, NoSymbol },
    { KEY_F7, XK_F7, NoSymbol }, { KEY_F8, XK_F8, NoSymbol }, { KEY_F9, XK_F9, NoSymbol },
    { KEY_F10, XK_F10, NoSymbol }, { KEY_F11, XK_F11, NoSymbol }, { KEY_F12, XK_F12, NoSymbol },
    { KEY_NUMLOCK, XK_Num_Lock, NoSymbol }, { KEY_SCROLLLOCK, XK_Scroll_Lock, NoSymbol },
    { KEY_KP7, XK_KP_Home, XK_KP_7 }, { KEY_KP8, XK_KP_Up, XK_KP_8 },
    { KEY_KP9, XK_KP_Prior, XK_KP_9 }, { KEY_KPMINUS, XK_KP_Subtract, NoSymbol },
    { KEY_KP4, XK_KP_Left, XK_KP_4 }, { KEY_KP5, XK_KP_Begin, XK_KP_5 },
    { KEY_KP6, XK_KP_Right, XK_KP_6 }, { KEY_KPPLUS, XK_KP_Add, NoSymbol },
    { KEY_KP1, XK_KP_End, XK_KP_1 }, { KEY_KP2, XK_KP_Down, XK_KP_2 },
    { KEY_KP3, XK_KP_Next, XK_KP_3 }, { KEY_KP0, XK_KP_Insert, XK_KP_0 },
    { KEY_KPDOT, XK_KP_Delete, XK_KP_Decimal },
    { KEY_102ND, XK_less, XK_greater },
    { KEY_KPENTER, XK_KP_Enter, NoSymbol }, { KEY_RIGHTCTRL, XK_Control_R, NoSymbol },
    { KEY_KPSLASH, XK_KP_Divide, NoSymbol }, { KEY_SYSRQ, XK_Print, NoSymbol },
    { KEY_RIGHTALT, XK_Alt_R, XK_Meta_R },
    { KEY_HOME, XK_Home, NoSymbol }, { KEY_UP, XK_Up, NoSymbol },
    { KEY_PAGEUP, XK_Prior, NoSymbol }, { KEY_LEFT, XK_Left, NoSymbol },
    { KEY_RIGHT, XK_Right, NoSymbol }, { KEY_END, XK_End, NoSymbol },
    { KEY_DOWN, XK_Down, NoSymbol }, { KEY_PAGEDOWN, XK_Next, NoSymbol },
    { KEY_INSERT, XK_Insert, NoSymbol }, { KEY_DELETE, XK_Delete, NoSymbol },
    { KEY_MUTE, XF86AudioMute, NoSymbol },
    { KEY_VOLUMEDOWN, XF86AudioLowerVolume, NoSymbol },
    { KEY_VOLUMEUP, XF86AudioRaiseVolume, NoSymbol },
    { KEY_KPEQUAL, XK_KP_Equal, NoSymbol }, { KEY_PAUSE, XK_Pause, XK_Break },
    { KEY_LEFTMETA, XK_Super_L, NoSymbol }, { KEY_RIGHTMETA, XK_Super_R, NoSymbol },
    { KEY_COMPOSE, XK_Menu, NoSymbol },
    { KEY_NEXTSONG, XF86AudioNext, NoSymbol }, { KEY_PLAYPAUSE, XF86AudioPlay, NoSymbol },
    { KEY_PREVIOUSSONG, XF86AudioPrev, NoSymbol }, { KEY_STOPCD, XF86AudioStop, NoSymbol },
    { KEY_BRIGHTNESSDOWN, XF86MonBrightnessDown, NoSymbol },
    { KEY_BRIGHTNESSUP, XF86MonBrightnessUp, NoSymbol },
    /* KEY_MICMUTE is 248, one past what X keycodes can reach */
};

/* other layouts: just the keys that differ from US, set with X8664_KEYMAP */
static const struct { int code; KeySym plain, shifted; } evdevGb[] = {
    { KEY_2, XK_2, XK_quotedbl },
    { KEY_3, XK_3, XK_sterling },
    { KEY_APOSTROPHE, XK_apostrophe, XK_at },
    { KEY_GRAVE, XK_grave, XK_notsign },
    { KEY_BACKSLASH, XK_numbersign, XK_asciitilde },	/* the key next to enter */
    { KEY_102ND, XK_backslash, XK_bar },		/* the one next to left shift */
};

static const struct {
    const char *name;
    const void *keys;
    int count;
} evdevLayouts[] = {
    { "gb", evdevGb, sizeof evdevGb / sizeof evdevGb[0] },
    { "uk", evdevGb, sizeof evdevGb / sizeof evdevGb[0] },
};

#define BIT(a, n)	((a)[(n) / 8] & (1 << ((n) % 8)))

/* a keyboard has letters and a space bar. that leaves out power buttons, lid switches and mice */
static Bool
evdevIsKeyboard(int fd)
{
    unsigned char keys[KEY_MAX / 8 + 1];

    memset(keys, 0, sizeof keys);
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0)
	return FALSE;
    return BIT(keys, KEY_A) && BIT(keys, KEY_Z) && BIT(keys, KEY_SPACE);
}

static void
evdevOpenAll(void)
{
    char path[32];
    int i, fd;

    if (evdevCount)
	return;
    for (i = 0; i < 32 && evdevCount < MAXKBD; i++) {
	snprintf(path, sizeof path, "/dev/input/event%d", i);
	fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
	    continue;
	if (!evdevIsKeyboard(fd)) {
	    close(fd);
	    continue;
	}
	evdevFds[evdevCount++] = fd;
    }
}

/* fbinit.c asks this before choosing: no keyboards here means the old console driver */
Bool
EvdevKeyboardAvailable(void)
{
    if (getenv("X8664_CONSOLE_KEYBOARD"))
	return FALSE;
    evdevOpenAll();
    return evdevCount > 0;
}

static void
EvdevKeyboardLoad(void)
{
    unsigned int i;
    KeySym *k;
    const char *layout;

    /* scan code n is evdev key n, and X keycode n + 8 */
    kdMinScanCode = 0;
    kdMaxScanCode = 247;
    memset(kdKeymap, 0, sizeof(KeySym) * KD_MAX_LENGTH * KD_MAX_WIDTH);
    for (i = 0; i < sizeof evdevMap / sizeof evdevMap[0]; i++) {
	/* X keycodes stop at 255, so evdev keys stop at 247. writing past that
	 * used to land in the key-is-down table and swallow the first super press */
	if (evdevMap[i].code > kdMaxScanCode)
	    continue;
	k = kdKeymap + evdevMap[i].code * KD_MAX_WIDTH;
	k[0] = evdevMap[i].plain;
	k[1] = evdevMap[i].shifted;
    }

    /* a layout on top, if one was asked for */
    layout = getenv("X8664_KEYMAP");
    for (i = 0; layout && i < sizeof evdevLayouts / sizeof evdevLayouts[0]; i++) {
	const struct { int code; KeySym plain, shifted; } *keys = evdevLayouts[i].keys;
	int j;

	if (strcmp(layout, evdevLayouts[i].name) != 0)
	    continue;
	for (j = 0; j < evdevLayouts[i].count; j++) {
	    k = kdKeymap + keys[j].code * KD_MAX_WIDTH;
	    k[0] = keys[j].plain;
	    k[1] = keys[j].shifted;
	}
	ErrorF("x8664 evdev: %s keyboard layout\n", layout);
	return;
    }
    if (layout && strcmp(layout, "us") != 0)
	ErrorF("x8664 evdev: no layout called %s, using us\n", layout);
}

static void
EvdevKeyboardRead(int fd, void *closure)
{
    struct input_event ev[32];
    int n, i;

    while ((n = read(fd, ev, sizeof ev)) > 0) {
	n /= sizeof ev[0];
	for (i = 0; i < n; i++) {
	    if (ev[i].type != EV_KEY || ev[i].code > 247)
		continue;
	    /* 0 is up, 1 down, 2 the kernel's own repeat, which we leave to X */
	    if (ev[i].value == 2)
		continue;
	    if (evdevDebug)
		ErrorF("x8664 evdev: key %d %s\n", ev[i].code, ev[i].value ? "down" : "up");
	    KdEnqueueKeyboardEvent(ev[i].code, ev[i].value == 0);
	}
    }
}

/* ours alone while X is on screen: the console underneath doesn't get the keys too */
static int
EvdevKeyboardEnable(int fd, void *closure)
{
    ioctl(fd, EVIOCGRAB, (void *) 1);
    return fd;
}

static void
EvdevKeyboardDisable(int fd, void *closure)
{
    ioctl(fd, EVIOCGRAB, (void *) 0);
}

static int
EvdevKeyboardInit(void)
{
    int i;

    evdevOpenAll();
    evdevDebug = getenv("X8664_EVDEV_DEBUG") != NULL;
    if (!evdevType)
	evdevType = KdAllocInputType();
    for (i = 0; i < evdevCount; i++) {
	KdRegisterFd(evdevType, evdevFds[i], EvdevKeyboardRead, 0);
	EvdevKeyboardEnable(evdevFds[i], 0);
	KdRegisterFdEnableDisable(evdevFds[i], EvdevKeyboardEnable, EvdevKeyboardDisable);
    }
    ErrorF("x8664 evdev: %d keyboard%s\n", evdevCount, evdevCount == 1 ? "" : "s");
    return evdevCount > 0;
}

static void
EvdevKeyboardFini(void)
{
    int i;

    for (i = 0; i < evdevCount; i++)
	EvdevKeyboardDisable(evdevFds[i], 0);
    KdUnregisterFds(evdevType, TRUE);
    evdevCount = 0;
}

/* caps lock, num lock and scroll lock lights */
static void
EvdevKeyboardLeds(int leds)
{
    struct input_event ev[3];
    int i;

    memset(ev, 0, sizeof ev);
    ev[0].type = ev[1].type = ev[2].type = EV_LED;
    /* the same bits the console uses: 1 scroll, 2 num, 4 caps */
    ev[0].code = LED_SCROLLL;
    ev[0].value = (leds & 1) != 0;
    ev[1].code = LED_NUML;
    ev[1].value = (leds & 2) != 0;
    ev[2].code = LED_CAPSL;
    ev[2].value = (leds & 4) != 0;
    for (i = 0; i < evdevCount; i++)
	if (write(evdevFds[i], ev, sizeof ev) < 0)
	    ;	/* a keyboard without lights, nothing to do */
}

static void
EvdevKeyboardBell(int volume, int pitch, int duration)
{
}

KdKeyboardFuncs EvdevKeyboardFuncs = {
    EvdevKeyboardLoad,
    EvdevKeyboardInit,
    EvdevKeyboardLeds,
    EvdevKeyboardBell,
    EvdevKeyboardFini,
    3,
};
