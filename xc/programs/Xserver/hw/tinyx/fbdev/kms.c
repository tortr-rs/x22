/*
 * kms.c: real screens through the kernel, for x8664.
 *
 * Kernels today mostly don't make /dev/fb0 any more (CONFIG_FB_DEVICE is off on
 * most distros), so the fbdev driver had nothing to open. This sets the screen up
 * the way the kernel wants now, through /dev/dri/card0: find a connected monitor,
 * take its preferred mode, make a "dumb" buffer in memory the card can scan out,
 * and put it on the screen. Then it fills in the same fb_var/fb_fix fields
 * /dev/fb0 would have, so the rest of fbdev.c works as it always did.
 *
 * Step 2 of x8664's plan, and the start of step 3: two buffers that swap at vblank,
 * so the screen never shows half a frame. One monitor for now, that's step 6.
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

#include "fbdev.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

/* ioctl, again if a signal got in the way */
static int
kmsIoctl(int fd, unsigned long req, void *arg)
{
    int r;
    do
	r = ioctl(fd, req, arg);
    while (r < 0 && (errno == EINTR || errno == EAGAIN));
    return r;
}

static void
kmsSay(const char *what)
{
    ErrorF("x8664 kms: %s\n", what);
}

/* the kernel hands back lists in two calls: the first says how many, the second fills them in */
static Bool
kmsResources(int fd, struct drm_mode_card_res *res,
	     __u32 **crtcs, __u32 **conns, __u32 **encs)
{
    memset(res, 0, sizeof *res);
    if (kmsIoctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res) < 0)
	return FALSE;
    *crtcs = calloc(res->count_crtcs + 1, sizeof(__u32));
    *conns = calloc(res->count_connectors + 1, sizeof(__u32));
    *encs = calloc(res->count_encoders + 1, sizeof(__u32));
    if (!*crtcs || !*conns || !*encs)
	return FALSE;
    res->crtc_id_ptr = (unsigned long) *crtcs;
    res->connector_id_ptr = (unsigned long) *conns;
    res->encoder_id_ptr = (unsigned long) *encs;
    res->fb_id_ptr = 0;
    res->count_fbs = 0;
    return kmsIoctl(fd, DRM_IOCTL_MODE_GETRESOURCES, res) == 0;
}

/* a connector with a monitor on it: its first mode (the kernel lists the preferred one first) */
static Bool
kmsConnector(int fd, __u32 id, struct drm_mode_get_connector *c,
	     struct drm_mode_modeinfo *mode, __u32 **encs)
{
    struct drm_mode_modeinfo *modes;
    int i, best;

    memset(c, 0, sizeof *c);
    c->connector_id = id;
    if (kmsIoctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, c) < 0)
	return FALSE;
    if (c->connection != 1 || c->count_modes == 0)	/* 1 is connected */
	return FALSE;

    modes = calloc(c->count_modes, sizeof *modes);
    *encs = calloc(c->count_encoders + 1, sizeof(__u32));
    if (!modes || !*encs) {
	free(modes);
	return FALSE;
    }
    c->modes_ptr = (unsigned long) modes;
    c->encoders_ptr = (unsigned long) *encs;
    c->count_props = 0;
    c->props_ptr = 0;
    c->prop_values_ptr = 0;
    if (kmsIoctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, c) < 0) {
	free(modes);
	return FALSE;
    }
    best = 0;
    for (i = 0; i < (int) c->count_modes; i++)
	if (modes[i].type & DRM_MODE_TYPE_PREFERRED) {
	    best = i;
	    break;
	}
    *mode = modes[best];
    free(modes);
    return TRUE;
}

/* which crtc (the part of the card that sends a picture down a cable) can drive this connector */
static __u32
kmsCrtc(int fd, struct drm_mode_get_connector *c, __u32 *encs,
	__u32 *crtcs, int ncrtcs)
{
    struct drm_mode_get_encoder e;
    int i, j;

    /* what's already lit up, if anything: no flicker, and the console is there */
    if (c->encoder_id) {
	memset(&e, 0, sizeof e);
	e.encoder_id = c->encoder_id;
	if (kmsIoctl(fd, DRM_IOCTL_MODE_GETENCODER, &e) == 0 && e.crtc_id)
	    return e.crtc_id;
    }
    for (i = 0; i < (int) c->count_encoders; i++) {
	memset(&e, 0, sizeof e);
	e.encoder_id = encs[i];
	if (kmsIoctl(fd, DRM_IOCTL_MODE_GETENCODER, &e) < 0)
	    continue;
	for (j = 0; j < ncrtcs; j++)
	    if (e.possible_crtcs & (1u << j))
		return crtcs[j];
    }
    return 0;
}

Bool
kmsInitialize(FbdevPriv *priv)
{
    struct drm_mode_card_res res;
    struct drm_mode_get_connector conn;
    struct drm_mode_create_dumb dumb;
    struct drm_mode_fb_cmd fb;
    struct drm_mode_map_dumb map;
    __u32 *crtcs = NULL, *conns = NULL, *encs = NULL, *cencs = NULL;
    const char *dev = getenv("X8664_CARD");
    char msg[160];
    int i;
    Bool found = FALSE;

    if (!dev)
	dev = "/dev/dri/card0";
    if ((priv->fd = open(dev, O_RDWR | O_CLOEXEC)) < 0) {
	snprintf(msg, sizeof msg, "can't open %s: %s", dev, strerror(errno));
	kmsSay(msg);
	return FALSE;
    }
    /* we're the only one drawing on it now. fails harmlessly if we already are */
    kmsIoctl(priv->fd, DRM_IOCTL_SET_MASTER, NULL);

    if (!kmsResources(priv->fd, &res, &crtcs, &conns, &encs)) {
	kmsSay("this card doesn't do kernel mode-setting");
	goto fail;
    }
    for (i = 0; i < (int) res.count_connectors && !found; i++) {
	free(cencs);
	cencs = NULL;
	if (!kmsConnector(priv->fd, conns[i], &conn, &priv->kmsMode, &cencs))
	    continue;
	priv->kmsCrtc = kmsCrtc(priv->fd, &conn, cencs, crtcs, res.count_crtcs);
	if (priv->kmsCrtc) {
	    priv->kmsConn = conns[i];
	    found = TRUE;
	}
    }
    if (!found) {
	kmsSay("no monitor connected");
	goto fail;
    }

    /* two buffers the card can show: 32 bits a pixel, xrgb, the same order Xvfb uses now */
    for (i = 0; i < 2; i++) {
	memset(&dumb, 0, sizeof dumb);
	dumb.width = priv->kmsMode.hdisplay;
	dumb.height = priv->kmsMode.vdisplay;
	dumb.bpp = 32;
	if (kmsIoctl(priv->fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb) < 0) {
	    kmsSay("the card won't give us a buffer");
	    goto fail;
	}
	priv->kmsHandles[i] = dumb.handle;

	memset(&fb, 0, sizeof fb);
	fb.width = dumb.width;
	fb.height = dumb.height;
	fb.pitch = dumb.pitch;
	fb.bpp = 32;
	fb.depth = 24;
	fb.handle = dumb.handle;
	if (kmsIoctl(priv->fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0) {
	    kmsSay("the card won't take the buffer");
	    goto fail;
	}
	priv->kmsFbs[i] = fb.fb_id;

	memset(&map, 0, sizeof map);
	map.handle = dumb.handle;
	if (kmsIoctl(priv->fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
	    kmsSay("can't map the buffer");
	    goto fail;
	}
	priv->kmsMaps[i] = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED,
				priv->fd, map.offset);
	if (priv->kmsMaps[i] == MAP_FAILED) {
	    kmsSay("can't map the buffer");
	    goto fail;
	}
	memset(priv->kmsMaps[i], 0, dumb.size);
    }
    /* the screen starts on buffer 0, we draw into 1 */
    priv->kmsBack = 1;
    priv->kmsFb = priv->kmsFbs[1];
    priv->fb_base = priv->fb = priv->kmsMaps[1];
    priv->kmsFlipping = FALSE;
    priv->kmsNoFlip = getenv("X8664_NO_FLIP") != NULL;

    /* and now pretend we read all that from /dev/fb0 */
    memset(&priv->var, 0, sizeof priv->var);
    memset(&priv->fix, 0, sizeof priv->fix);
    priv->var.xres = priv->var.xres_virtual = dumb.width;
    priv->var.yres = priv->var.yres_virtual = dumb.height;
    priv->var.bits_per_pixel = 32;
    priv->var.red.offset = 16;
    priv->var.red.length = 8;
    priv->var.green.offset = 8;
    priv->var.green.length = 8;
    priv->var.blue.offset = 0;
    priv->var.blue.length = 8;
    priv->fix.visual = FB_VISUAL_TRUECOLOR;
    priv->fix.line_length = dumb.pitch;
    priv->fix.smem_len = dumb.size;
    priv->kms = TRUE;

    snprintf(msg, sizeof msg, "%s, %dx%d at %d Hz", dev, priv->kmsMode.hdisplay,
	     priv->kmsMode.vdisplay, priv->kmsMode.vrefresh);
    kmsSay(msg);
    free(crtcs); free(conns); free(encs); free(cencs);
    return TRUE;

fail:
    free(crtcs); free(conns); free(encs); free(cencs);
    close(priv->fd);
    priv->fd = -1;
    return FALSE;
}

/* put our buffer on the screen. also after coming back from another vt */
Bool
kmsShow(FbdevPriv *priv)
{
    struct drm_mode_crtc crtc;

    kmsIoctl(priv->fd, DRM_IOCTL_SET_MASTER, NULL);
    memset(&crtc, 0, sizeof crtc);
    crtc.crtc_id = priv->kmsCrtc;
    /* the one that isn't being drawn into, or the only one without flips */
    crtc.fb_id = priv->kmsNoFlip ? priv->kmsFb : priv->kmsFbs[1 - priv->kmsBack];
    crtc.set_connectors_ptr = (unsigned long) &priv->kmsConn;
    crtc.count_connectors = 1;
    crtc.mode = priv->kmsMode;
    crtc.mode_valid = 1;
    if (kmsIoctl(priv->fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0) {
	kmsSay("can't put the picture on the screen");
	return FALSE;
    }
    return TRUE;
}

/* the connector's DPMS property, found once: on, standby, suspend, off (0 to 3, the same
 * numbers X uses) */
static __u32
kmsDpmsProp(FbdevPriv *priv)
{
    static __u32 prop = (__u32) -1;
    struct drm_mode_obj_get_properties op;
    struct drm_mode_get_property gp;
    __u32 *ids;
    __u64 *vals;
    unsigned int i;

    if (prop != (__u32) -1)
	return prop;
    prop = 0;
    memset(&op, 0, sizeof op);
    op.obj_id = priv->kmsConn;
    op.obj_type = DRM_MODE_OBJECT_CONNECTOR;
    if (kmsIoctl(priv->fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) < 0 || !op.count_props)
	return prop;
    ids = calloc(op.count_props, sizeof *ids);
    vals = calloc(op.count_props, sizeof *vals);
    if (ids && vals) {
	op.props_ptr = (unsigned long) ids;
	op.prop_values_ptr = (unsigned long) vals;
	if (kmsIoctl(priv->fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &op) == 0)
	    for (i = 0; i < op.count_props; i++) {
		memset(&gp, 0, sizeof gp);
		gp.prop_id = ids[i];
		if (kmsIoctl(priv->fd, DRM_IOCTL_MODE_GETPROPERTY, &gp) == 0 &&
		    strcmp(gp.name, "DPMS") == 0) {
		    prop = ids[i];
		    break;
		}
	    }
    }
    free(ids);
    free(vals);
    return prop;
}

/* the screensaver: really switch the monitor off, and back on */
Bool
kmsDpms(FbdevPriv *priv, int mode)
{
    struct drm_mode_obj_set_property sp;
    __u32 prop = kmsDpmsProp(priv);

    if (!prop)
	return FALSE;
    memset(&sp, 0, sizeof sp);
    sp.obj_id = priv->kmsConn;
    sp.obj_type = DRM_MODE_OBJECT_CONNECTOR;
    sp.prop_id = prop;
    sp.value = mode;
    if (kmsIoctl(priv->fd, DRM_IOCTL_MODE_OBJ_SETPROPERTY, &sp) < 0) {
	kmsSay("the monitor won't switch power modes");
	return FALSE;
    }
    if (getenv("X8664_EVDEV_DEBUG"))
	ErrorF("x8664 kms: monitor %s\n", mode == 0 ? "on" : mode == 3 ? "off" : "asleep");
    return TRUE;
}

/* going to another vt: let the console (or whoever's there) have the card */
void
kmsHide(FbdevPriv *priv)
{
    kmsIoctl(priv->fd, DRM_IOCTL_DROP_MASTER, NULL);
}

void
kmsFini(FbdevPriv *priv)
{
    struct drm_mode_destroy_dumb d;
    int i;

    if (priv->kmsScreen) {
	RemoveEnabledDevice(priv->fd);
	REGION_UNINIT(priv->kmsScreen, &priv->kmsPending);
	REGION_UNINIT(priv->kmsScreen, &priv->kmsLast);
    }
    for (i = 0; i < 2; i++) {
	munmap(priv->kmsMaps[i], priv->fix.smem_len);
	kmsIoctl(priv->fd, DRM_IOCTL_MODE_RMFB, &priv->kmsFbs[i]);
	memset(&d, 0, sizeof d);
	d.handle = priv->kmsHandles[i];
	kmsIoctl(priv->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
    close(priv->fd);
}

/*
 * Frames. Drawing goes into the shadow copy in ordinary memory. When some of it changed,
 * we copy the changed part into the hidden buffer and ask the kernel to swap the two at
 * the next vblank (a page flip). Until it says it did, more changes just pile up: the
 * monitor can only show one frame per refresh anyway, and the screen never shows a frame
 * we're halfway through drawing, so nothing tears.
 *
 * The hidden buffer is the frame before last, so it gets what changed in the last frame
 * as well as what changed now.
 */
static void
kmsCopy(FbdevPriv *priv, RegionPtr what)
{
    shadowBufPtr pBuf = priv->kmsBuf;
    RegionRec keep = pBuf->damage;

    /* shadowUpdatePacked copies whatever pBuf->damage says, into whatever fbdevWindowLinear
     * points at, which is priv->fb */
    pBuf->damage = *what;
    if (pBuf->randr != SHADOW_ROTATE_0)
	shadowUpdateRotatePacked(priv->kmsScreen, pBuf);
    else
	shadowUpdatePacked(priv->kmsScreen, pBuf);
    pBuf->damage = keep;
}

/* the old way, for cards that can't flip: straight onto the screen, then say where */
static void
kmsDirty(FbdevPriv *priv, RegionPtr damage)
{
    int nbox = REGION_NUM_RECTS(damage), i;
    BoxPtr pbox = REGION_RECTS(damage);
    struct drm_clip_rect clips[64];
    struct drm_mode_fb_dirty_cmd dirty;

    memset(&dirty, 0, sizeof dirty);
    dirty.fb_id = priv->kmsFb;
    if (priv->kmsBuf->randr == SHADOW_ROTATE_0 && nbox <= 64) {
	for (i = 0; i < nbox; i++) {
	    clips[i].x1 = pbox[i].x1;
	    clips[i].y1 = pbox[i].y1;
	    clips[i].x2 = pbox[i].x2;
	    clips[i].y2 = pbox[i].y2;
	}
	dirty.num_clips = nbox;
	dirty.clips_ptr = (unsigned long) clips;
    }
    /* cards that show our buffer directly say ENOSYS, which is fine */
    ioctl(priv->fd, DRM_IOCTL_MODE_DIRTYFB, &dirty);
}

static void
kmsFlush(FbdevPriv *priv)
{
    ScreenPtr pScreen = priv->kmsScreen;
    struct drm_mode_crtc_page_flip flip;
    RegionRec todo;

    if (priv->kmsFlipping || !priv->kmsBuf || !REGION_NOTEMPTY(pScreen, &priv->kmsPending))
	return;

    REGION_NULL(pScreen, &todo);
    REGION_UNION(pScreen, &todo, &priv->kmsPending, &priv->kmsLast);
    kmsCopy(priv, &todo);
    REGION_UNINIT(pScreen, &todo);

    memset(&flip, 0, sizeof flip);
    flip.crtc_id = priv->kmsCrtc;
    flip.fb_id = priv->kmsFb;
    flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
    if (ioctl(priv->fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) < 0) {
	/* no flips on this card (or not right now): show this buffer and stay on it */
	kmsSay("page flips don't work here, drawing straight on the screen");
	priv->kmsNoFlip = TRUE;
	kmsShow(priv);
	kmsDirty(priv, &priv->kmsPending);
	REGION_EMPTY(pScreen, &priv->kmsPending);
	return;
    }
    priv->kmsFlipping = TRUE;
    REGION_COPY(pScreen, &priv->kmsLast, &priv->kmsPending);
    REGION_EMPTY(pScreen, &priv->kmsPending);
}

/* the shadow layer calls this when something was drawn */
void
kmsUpdate(ScreenPtr pScreen, shadowBufPtr pBuf)
{
    KdScreenPriv(pScreen);
    FbdevPriv *priv = pScreenPriv->card->driver;

    priv->kmsBuf = pBuf;
    if (priv->kmsNoFlip) {
	kmsCopy(priv, &pBuf->damage);
	kmsDirty(priv, &pBuf->damage);
	return;
    }
    REGION_UNION(pScreen, &priv->kmsPending, &priv->kmsPending, &pBuf->damage);
    kmsFlush(priv);
}

/* the kernel says the swap happened: the other buffer is the hidden one now */
static void
kmsFlipped(FbdevPriv *priv)
{
    priv->kmsFlipping = FALSE;
    priv->kmsBack = 1 - priv->kmsBack;
    priv->kmsFb = priv->kmsFbs[priv->kmsBack];
    priv->fb = priv->fb_base = priv->kmsMaps[priv->kmsBack];
    /* and whatever piled up while we waited goes out in the next frame */
    kmsFlush(priv);
}

static void
kmsBlock(pointer data, OSTimePtr timeout, pointer readmask)
{
}

static void
kmsWakeup(pointer data, int result, pointer readmask)
{
    FbdevPriv *priv = data;
    char buf[1024];
    struct drm_event *e;
    int n, at;

    if (result <= 0 || !FD_ISSET(priv->fd, (fd_set *) readmask))
	return;
    n = read(priv->fd, buf, sizeof buf);
    for (at = 0; n > 0 && at + (int) sizeof *e <= n; at += e->length) {
	e = (struct drm_event *) (buf + at);
	if (e->length == 0)
	    break;
	if (e->type == DRM_EVENT_FLIP_COMPLETE)
	    kmsFlipped(priv);
    }
}

/* after the screen is set up: listen for the kernel's flip events */
void
kmsStart(ScreenPtr pScreen)
{
    KdScreenPriv(pScreen);
    FbdevPriv *priv = pScreenPriv->card->driver;

    priv->kmsScreen = pScreen;
    REGION_NULL(pScreen, &priv->kmsPending);
    REGION_NULL(pScreen, &priv->kmsLast);
    fcntl(priv->fd, F_SETFL, fcntl(priv->fd, F_GETFL) | O_NONBLOCK);
    AddEnabledDevice(priv->fd);
    RegisterBlockAndWakeupHandlers(kmsBlock, kmsWakeup, priv);
}
