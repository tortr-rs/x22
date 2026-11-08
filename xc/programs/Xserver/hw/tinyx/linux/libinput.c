/*
 * libinput.c: mice and touchpads through libinput, for x8664.
 *
 * The old mouse driver (mouse.c) reads /dev/input/mice, which is the kernel
 * pretending every pointer is a PS/2 mouse from 1995: a touchpad moves the
 * pointer and that's it. No tapping, no scrolling with two fingers, no palm
 * detection, and the wheel only when the kernel feels like it. libinput is
 * what every other X server and Wayland use for all of that, so this hands
 * the pointers to it and turns what it says into tinyx mouse events.
 *
 * Settings, from the environment because tinyx has no config file:
 *   X8664_TAP=0            no tap to click on touchpads (it's on)
 *   X8664_NATURAL=1        natural scrolling, the content follows the fingers
 *   X8664_LIBINPUT=0       the old driver instead
 *
 * libinput needs udev to tell it what each device is, so this only takes over
 * when udev is running. Without it (a bare initramfs) the old driver stays.
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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libinput.h>
#include <libudev.h>
#include <linux/input-event-codes.h>

static struct libinput *li;
static struct udev *liUdev;
static int liType;
static int liDebug;		/* X8664_LIBINPUT_DEBUG=1 prints what comes in */
static unsigned long liButtons;	/* KD_BUTTON_* held down right now */
static double liRestX, liRestY;	/* the parts of a pixel left over from the last motion */
static double liScroll[2];	/* finger scrolling not yet worth a click, down and right */

/* how far two fingers go for one click of the wheel, in libinput's units (about pixels) */
#define LI_SCROLL_CLICK	15.0

static int
liOpen(const char *path, int flags, void *data)
{
    int fd = open(path, flags | O_CLOEXEC);

    return fd < 0 ? -errno : fd;
}

static void
liClose(int fd, void *data)
{
    close(fd);
}

static const struct libinput_interface liInterface = { liOpen, liClose };

static int
liEnv(const char *name, int dflt)
{
    char *v = getenv(name);

    return v && *v ? atoi(v) : dflt;
}

/* udev is up when its control socket is there: eudev and systemd-udevd both make it */
static int
liUdevRunning(void)
{
    struct stat st;

    return stat("/run/udev/control", &st) == 0 || stat("/run/udev/data", &st) == 0;
}

int
LibinputMouseAvailable(void)
{
    return liEnv("X8664_LIBINPUT", 1) && liUdevRunning();
}

static void
liSetup(struct libinput_device *dev)
{
    int natural = liEnv("X8664_NATURAL", 0);

    if (libinput_device_config_tap_get_finger_count(dev) > 0) {
	libinput_device_config_tap_set_enabled(dev, liEnv("X8664_TAP", 1) ?
	    LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
	libinput_device_config_tap_set_drag_enabled(dev, LIBINPUT_CONFIG_DRAG_ENABLED);
    }
    if (libinput_device_config_scroll_has_natural_scroll(dev))
	libinput_device_config_scroll_set_natural_scroll_enabled(dev, natural);
    /* two fingers scroll on touchpads, which is libinput's default anyway; say so in case it isn't */
    if (libinput_device_config_scroll_get_methods(dev) & LIBINPUT_CONFIG_SCROLL_2FG)
	libinput_device_config_scroll_set_method(dev, LIBINPUT_CONFIG_SCROLL_2FG);
    if (libinput_device_config_dwt_is_available(dev))
	libinput_device_config_dwt_set_enabled(dev, LIBINPUT_CONFIG_DWT_ENABLED);
    if (libinput_device_config_click_get_methods(dev) & LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER)
	libinput_device_config_click_set_method(dev, LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER);
    if (liDebug)
	ErrorF("x8664 libinput: %s\n", libinput_device_get_name(dev));
}

static unsigned long
liButtonBit(uint32_t code)
{
    switch (code) {
    case BTN_LEFT:	return KD_BUTTON_1;
    case BTN_MIDDLE:	return KD_BUTTON_2;
    case BTN_RIGHT:	return KD_BUTTON_3;
    }
    return 0;
}

/* one click of a wheel: X has no scroll events, the wheel is buttons 4 and 5 (6 and 7 sideways) */
static void
liClick(KdMouseInfo *mi, unsigned long button)
{
    KdEnqueueMouseEvent(mi, KD_MOUSE_DELTA | KD_MOUSE_RAW | liButtons | button, 0, 0);
    KdEnqueueMouseEvent(mi, KD_MOUSE_DELTA | KD_MOUSE_RAW | liButtons, 0, 0);
}

static void
liScrollBy(KdMouseInfo *mi, int axis, double v)
{
    unsigned long less = axis ? KD_BUTTON_6 : KD_BUTTON_4;
    unsigned long more = axis ? KD_BUTTON_7 : KD_BUTTON_5;

    liScroll[axis] += v;
    while (liScroll[axis] >= LI_SCROLL_CLICK) {
	liClick(mi, more);
	liScroll[axis] -= LI_SCROLL_CLICK;
    }
    while (liScroll[axis] <= -LI_SCROLL_CLICK) {
	liClick(mi, less);
	liScroll[axis] += LI_SCROLL_CLICK;
    }
}

static void
liPointerScroll(KdMouseInfo *mi, struct libinput_event_pointer *p, enum libinput_event_type type)
{
    static const enum libinput_pointer_axis axes[2] = {
	LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL
    };
    int a;

    for (a = 0; a < 2; a++) {
	if (!libinput_event_pointer_has_axis(p, axes[a]))
	    continue;
	if (type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL)
	    /* 120 is one click of an ordinary wheel, smaller steps add up on fine ones */
	    liScrollBy(mi, a, libinput_event_pointer_get_scroll_value_v120(p, axes[a]) *
			      LI_SCROLL_CLICK / 120.0);
	else {
	    double v = libinput_event_pointer_get_scroll_value(p, axes[a]);

	    liScrollBy(mi, a, v);
	    /* fingers lifted: what's left doesn't carry over to the next scroll */
	    if (v == 0)
		liScroll[a] = 0;
	}
    }
}

static void
liRead(int fd, void *closure)
{
    KdMouseInfo *mi = closure;
    struct libinput_event *ev;

    libinput_dispatch(li);
    while ((ev = libinput_get_event(li))) {
	enum libinput_event_type type = libinput_event_get_type(ev);
	/* pointer events are numbered 400 to 499, and asking for one on anything else is a bug */
	struct libinput_event_pointer *p = type >= LIBINPUT_EVENT_POINTER_MOTION &&
	    type < LIBINPUT_EVENT_TOUCH_DOWN ? libinput_event_get_pointer_event(ev) : NULL;

	switch (type) {
	case LIBINPUT_EVENT_DEVICE_ADDED: {
	    struct libinput_device *dev = libinput_event_get_device(ev);

	    if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER))
		liSetup(dev);
	    break;
	}
	case LIBINPUT_EVENT_POINTER_MOTION: {
	    /* libinput accelerates already, so X's own acceleration stays out of it */
	    double dx = libinput_event_pointer_get_dx(p) + liRestX;
	    double dy = libinput_event_pointer_get_dy(p) + liRestY;
	    int ix = (int) dx, iy = (int) dy;

	    liRestX = dx - ix;
	    liRestY = dy - iy;
	    if (ix || iy)
		KdEnqueueMouseEvent(mi, KD_MOUSE_DELTA | KD_MOUSE_RAW | liButtons, ix, iy);
	    break;
	}
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
	    /* tablets, and the pointer in most virtual machines */
	    ScreenPtr pScreen = screenInfo.screens[0];

	    KdEnqueueMouseEvent(mi, liButtons,
		(int) libinput_event_pointer_get_absolute_x_transformed(p, pScreen->width),
		(int) libinput_event_pointer_get_absolute_y_transformed(p, pScreen->height));
	    break;
	}
	case LIBINPUT_EVENT_POINTER_BUTTON: {
	    unsigned long bit = liButtonBit(libinput_event_pointer_get_button(p));

	    if (!bit)
		break;
	    if (libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED)
		liButtons |= bit;
	    else
		liButtons &= ~bit;
	    KdEnqueueMouseEvent(mi, KD_MOUSE_DELTA | KD_MOUSE_RAW | liButtons, 0, 0);
	    break;
	}
	case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
	case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
	case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS:
	    liPointerScroll(mi, p, type);
	    break;
	default:
	    break;
	}
	if (liDebug && type != LIBINPUT_EVENT_DEVICE_ADDED)
	    ErrorF("x8664 libinput: event %d, buttons %02lx\n", type, liButtons);
	libinput_event_destroy(ev);
    }
}

static int
LibinputInit(void)
{
    KdMouseInfo *mi;
    int fd;

    liDebug = liEnv("X8664_LIBINPUT_DEBUG", 0);
    if (!liType)
	liType = KdAllocInputType();
    /* the first mouse tinyx set up is ours, the old driver gets none */
    for (mi = kdMouseInfo; mi && mi->inputType; mi = mi->next)
	;
    if (!mi)
	return 0;
    if (!(liUdev = udev_new()))
	return 0;
    li = libinput_udev_create_context(&liInterface, NULL, liUdev);
    if (!li || libinput_udev_assign_seat(li, "seat0") != 0) {
	ErrorF("x8664 libinput: no seat, mice and touchpads won't work\n");
	if (li)
	    libinput_unref(li);
	li = NULL;
	udev_unref(liUdev);
	liUdev = NULL;
	return 0;
    }
    fd = libinput_get_fd(li);
    mi->driver = li;
    mi->inputType = liType;
    liButtons = 0;
    if (!KdRegisterFd(liType, fd, liRead, mi))
	return 0;
    /* the devices that are already there come in as DEVICE_ADDED: set them up now */
    liRead(fd, mi);
    return 1;
}

static void
LibinputFini(void)
{
    KdMouseInfo *mi;

    /* libinput owns its fd: tinyx mustn't close it */
    KdUnregisterFds(liType, FALSE);
    for (mi = kdMouseInfo; mi; mi = mi->next)
	if (mi->inputType == liType) {
	    mi->driver = 0;
	    mi->inputType = 0;
	}
    if (li)
	libinput_unref(li);
    li = NULL;
    if (liUdev)
	udev_unref(liUdev);
    liUdev = NULL;
}

KdMouseFuncs LibinputMouseFuncs = {
    LibinputInit,
    LibinputFini,
};
