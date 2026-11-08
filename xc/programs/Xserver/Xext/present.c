/*
 * present.c: the Present extension (1.0), and the Generic Event extension it needs.
 *
 * Present lets a program say "put this picture in my window at frame N" or "tell me
 * when frame N happens", and tells it back exactly when that was. It's how programs
 * line their drawing up with the monitor instead of guessing. XFree86 4.8 is older
 * than Present, and older than generic events (events longer than 32 bytes), which
 * Present's events are, so both are here, written for x8664.
 *
 * What's here: QueryVersion, Pixmap (copied into the window at the target frame),
 * NotifyMSC, SelectInput, QueryCapabilities, and the CompleteNotify and IdleNotify
 * events. What isn't yet: XFixes regions (XFree86 has no XFixes, so valid and update
 * must be None, meaning the whole pixmap), sync fences (SYNC here is 3.0, fences are
 * 3.1), flips straight to the screen (everything is a copy), ConfigureNotify, and
 * clients of the other byte order.
 *
 * Frames come from the screen's driver through PresentSetHooks (kms.c gives the
 * kernel's vblank counter). A screen without hooks gets a 60 Hz clock in software.
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

#define NEED_REPLIES
#define NEED_EVENTS
#include <X11/X.h>
#include <X11/Xproto.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "misc.h"
#include "os.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "resource.h"
#include "scrnintstr.h"
#include "windowstr.h"
#include "pixmapstr.h"
#include "gcstruct.h"
#include "x8664present.h"

/* the protocol's numbers */
#define PRESENT_MAJOR		1
#define PRESENT_MINOR		0
#define X_PresentQueryVersion		0
#define X_PresentPixmap			1
#define X_PresentNotifyMSC		2
#define X_PresentSelectInput		3
#define X_PresentQueryCapabilities	4

#define PresentConfigureNotify	0
#define PresentCompleteNotify	1
#define PresentIdleNotify	2

#define PresentConfigureNotifyMask	1
#define PresentCompleteNotifyMask	2
#define PresentIdleNotifyMask		4
#define PresentAllEvents		7

#define PresentCompleteKindPixmap	0
#define PresentCompleteKindNotifyMSC	1
#define PresentCompleteModeCopy		0
#define PresentCompleteModeSkip		2

#define PresentOptionAsync		1

#define GenericEvent		35	/* the core event number generic events use */

static int presentOpcode;		/* our major opcode, the "extension" in each event */
static RESTYPE presentEventType;	/* a SelectInput, owned by the client that made it */
static PresentHooksPtr presentHooks[MAXSCREENS];

/* ---------- time and frames ---------- */

static uint64_t
presentNow(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (uint64_t) tv.tv_sec * 1000000 + tv.tv_usec;
}

#define SOFT_FRAME	16667	/* microseconds: the software clock's 60 Hz */

static void
presentMsc(ScreenPtr pScreen, uint64_t *ust, uint64_t *msc)
{
    PresentHooksPtr h = presentHooks[pScreen->myNum];

    if (h && h->get_msc && h->get_msc(pScreen, ust, msc))
	return;
    *ust = presentNow();
    *msc = *ust / SOFT_FRAME;
    *ust = *msc * SOFT_FRAME;	/* when this frame started */
}

void
PresentSetHooks(ScreenPtr pScreen, PresentHooksPtr hooks)
{
    presentHooks[pScreen->myNum] = hooks;
}

/* ---------- who wants which events ---------- */

typedef struct _PresentSel {
    struct _PresentSel *next;
    XID eid;
    ClientPtr client;
    Window window;
    CARD32 mask;
} PresentSelRec, *PresentSelPtr;

static PresentSelPtr presentSels;

static int
presentFreeSel(pointer value, XID id)
{
    PresentSelPtr s = value, *p;

    for (p = &presentSels; *p; p = &(*p)->next)
	if (*p == s) {
	    *p = s->next;
	    break;
	}
    xfree(s);
    return Success;
}

/* an event of size bytes, sent to everyone who asked for it on this window.
 * eid goes at offset 12, which is where it is in every Present event */
static void
presentSend(Window window, CARD32 mask, CARD8 *ev, int size)
{
    PresentSelPtr s;

    for (s = presentSels; s; s = s->next) {
	CARD32 eid;
	CARD16 seq;

	if (s->window != window || !(s->mask & mask) || s->client->clientGone)
	    continue;
	eid = s->eid;
	seq = s->client->sequence;
	memcpy(ev + 12, &eid, 4);
	memcpy(ev + 2, &seq, 2);
	WriteToClient(s->client, size, (char *) ev);
    }
}

static void
presentSendComplete(Window window, CARD32 serial, int kind, int mode, uint64_t ust, uint64_t msc)
{
    CARD8 ev[40];
    CARD32 len = 2, w = window;
    CARD16 evtype = PresentCompleteNotify;

    memset(ev, 0, sizeof ev);
    ev[0] = GenericEvent;
    ev[1] = presentOpcode;
    memcpy(ev + 4, &len, 4);		/* 8 bytes past the usual 32 */
    memcpy(ev + 8, &evtype, 2);
    ev[10] = kind;
    ev[11] = mode;
    memcpy(ev + 16, &w, 4);
    memcpy(ev + 20, &serial, 4);
    memcpy(ev + 24, &ust, 8);
    memcpy(ev + 32, &msc, 8);
    presentSend(window, PresentCompleteNotifyMask, ev, sizeof ev);
}

static void
presentSendIdle(Window window, CARD32 serial, Pixmap pixmap)
{
    CARD8 ev[32];
    CARD32 len = 0, w = window;
    CARD16 evtype = PresentIdleNotify;

    memset(ev, 0, sizeof ev);
    ev[0] = GenericEvent;
    ev[1] = presentOpcode;
    memcpy(ev + 4, &len, 4);
    memcpy(ev + 8, &evtype, 2);
    memcpy(ev + 16, &w, 4);
    memcpy(ev + 20, &serial, 4);
    memcpy(ev + 24, &pixmap, 4);
    /* idle_fence at 28 stays None */
    presentSend(window, PresentIdleNotifyMask, ev, sizeof ev);
}

/* ---------- things waiting for a frame ---------- */

typedef struct _PresentVblank {
    struct _PresentVblank *next;
    uint64_t id;
    ScreenPtr pScreen;
    uint64_t msc;
    int kind;		/* PresentCompleteKindPixmap or ...NotifyMSC */
    Window window;
    CARD32 serial;
    PixmapPtr pPixmap;	/* we hold a reference until it's copied */
    Pixmap pixmap;
    INT16 x, y;
    OsTimerPtr timer;	/* only for the software clock */
} PresentVblankRec, *PresentVblankPtr;

static PresentVblankPtr presentQueue;
static uint64_t presentNextId = 1;

/* the copy, then the events. the window may be gone by now */
static void
presentExecute(PresentVblankPtr v, uint64_t ust, uint64_t msc)
{
    WindowPtr pWin = (WindowPtr) LookupIDByType(v->window, RT_WINDOW);

    if (v->kind == PresentCompleteKindPixmap) {
	if (pWin && v->pPixmap && pWin->drawable.depth == v->pPixmap->drawable.depth) {
	    GCPtr gc = GetScratchGC(pWin->drawable.depth, pWin->drawable.pScreen);

	    if (gc) {
		ValidateGC(&pWin->drawable, gc);
		(*gc->ops->CopyArea) (&v->pPixmap->drawable, &pWin->drawable, gc, 0, 0,
				      v->pPixmap->drawable.width, v->pPixmap->drawable.height,
				      v->x, v->y);
		FreeScratchGC(gc);
	    }
	}
	if (v->pPixmap)
	    (*v->pPixmap->drawable.pScreen->DestroyPixmap) (v->pPixmap);
	v->pPixmap = NULL;
	presentSendIdle(v->window, v->serial, v->pixmap);
	presentSendComplete(v->window, v->serial, PresentCompleteKindPixmap,
			    pWin ? PresentCompleteModeCopy : PresentCompleteModeSkip, ust, msc);
    } else
	presentSendComplete(v->window, v->serial, PresentCompleteKindNotifyMSC,
			    PresentCompleteModeCopy, ust, msc);
    if (v->timer)
	TimerFree(v->timer);
    xfree(v);
}

static PresentVblankPtr
presentUnqueue(uint64_t id)
{
    PresentVblankPtr *p, v;

    for (p = &presentQueue; *p; p = &(*p)->next)
	if ((*p)->id == id) {
	    v = *p;
	    *p = v->next;
	    return v;
	}
    return NULL;
}

/* the driver (or the software clock) says frame msc happened */
void
PresentVblankEvent(ScreenPtr pScreen, uint64_t id, uint64_t ust, uint64_t msc)
{
    PresentVblankPtr v = presentUnqueue(id);

    if (v)
	presentExecute(v, ust, msc);
}

static CARD32
presentSoftTick(OsTimerPtr timer, CARD32 now, pointer arg)
{
    PresentVblankPtr v = arg;
    uint64_t ust, msc;

    v->timer = NULL;	/* TimerSet's own timer goes away with the callback returning 0 */
    TimerFree(timer);
    presentMsc(v->pScreen, &ust, &msc);
    if (msc < v->msc)	/* a millisecond timer and a frame counter don't always agree */
	msc = v->msc;
    PresentVblankEvent(v->pScreen, v->id, ust, msc);
    return 0;
}

/* do it now if the frame has come, otherwise wait for it */
static void
presentQueueVblank(PresentVblankPtr v, uint64_t now_msc, uint64_t now_ust)
{
    PresentHooksPtr h = presentHooks[v->pScreen->myNum];

    if (v->msc <= now_msc) {
	presentExecute(v, now_ust, now_msc);
	return;
    }
    v->id = presentNextId++;
    v->next = presentQueue;
    presentQueue = v;
    if (h && h->queue && h->queue(v->pScreen, v->id, v->msc))
	return;
    /* the software clock: frames from the one the caller saw, 60 a second */
    {
	uint64_t due = now_ust + (v->msc - now_msc) * SOFT_FRAME, now = presentNow();
	uint64_t wait = due > now ? due - now : 0;

	v->timer = TimerSet(NULL, 0, (CARD32) (wait / 1000) + 1, presentSoftTick, v);
    }
}

/* the frame to aim for, the way the protocol says: target, or the next frame where
 * msc % divisor == remainder once target has passed */
static uint64_t
presentTarget(uint64_t now, uint64_t target, uint64_t divisor, uint64_t remainder)
{
    if (divisor == 0 || target > now)
	return target;
    target = now - (now % divisor) + (remainder % divisor);
    if (target <= now)
	target += divisor;
    return target;
}

/* ---------- requests ---------- */

static uint64_t
get64(CARD8 *p)
{
    uint64_t v;

    memcpy(&v, p, 8);
    return v;
}

static CARD32
get32(CARD8 *p)
{
    CARD32 v;

    memcpy(&v, p, 4);
    return v;
}

static int
ProcPresentQueryVersion(ClientPtr client)
{
    xGenericReply rep;

    if (client->req_len != 3)
	return BadLength;
    memset(&rep, 0, sizeof rep);
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.data00 = PRESENT_MAJOR;
    rep.data01 = PRESENT_MINOR;
    WriteToClient(client, sizeof rep, (char *) &rep);
    return client->noClientException;
}

static int
ProcPresentPixmap(ClientPtr client)
{
    CARD8 *req = (CARD8 *) client->requestBuffer;
    WindowPtr pWin;
    PixmapPtr pPixmap;
    PresentVblankPtr v;
    uint64_t ust, msc;
    Window window;
    Pixmap pixmap;

    if (client->req_len < 18)
	return BadLength;
    window = get32(req + 4);
    pixmap = get32(req + 8);
    pWin = (WindowPtr) LookupIDByType(window, RT_WINDOW);
    if (!pWin) {
	client->errorValue = window;
	return BadWindow;
    }
    pPixmap = (PixmapPtr) LookupIDByType(pixmap, RT_PIXMAP);
    if (!pPixmap) {
	client->errorValue = pixmap;
	return BadPixmap;
    }
    if (pPixmap->drawable.depth != pWin->drawable.depth)
	return BadMatch;
    /* no XFixes here, so no regions: None, the whole pixmap, is all we take */
    if (get32(req + 16) != None || get32(req + 20) != None)
	return BadImplementation;

    v = xalloc(sizeof *v);
    if (!v)
	return BadAlloc;
    memset(v, 0, sizeof *v);
    v->pScreen = pWin->drawable.pScreen;
    v->kind = PresentCompleteKindPixmap;
    v->window = window;
    v->serial = get32(req + 12);
    v->pixmap = pixmap;
    v->pPixmap = pPixmap;
    pPixmap->refcnt++;
    memcpy(&v->x, req + 24, 2);
    memcpy(&v->y, req + 26, 2);

    presentMsc(v->pScreen, &ust, &msc);
    v->msc = presentTarget(msc, get64(req + 48), get64(req + 56), get64(req + 64));
    /* async: don't wait for a frame that's already gone */
    if ((get32(req + 40) & PresentOptionAsync) && v->msc <= msc + 1)
	v->msc = msc;
    presentQueueVblank(v, msc, ust);
    return client->noClientException;
}

static int
ProcPresentNotifyMSC(ClientPtr client)
{
    CARD8 *req = (CARD8 *) client->requestBuffer;
    WindowPtr pWin;
    PresentVblankPtr v;
    uint64_t ust, msc;
    Window window;

    if (client->req_len != 10)
	return BadLength;
    window = get32(req + 4);
    pWin = (WindowPtr) LookupIDByType(window, RT_WINDOW);
    if (!pWin) {
	client->errorValue = window;
	return BadWindow;
    }
    v = xalloc(sizeof *v);
    if (!v)
	return BadAlloc;
    memset(v, 0, sizeof *v);
    v->pScreen = pWin->drawable.pScreen;
    v->kind = PresentCompleteKindNotifyMSC;
    v->window = window;
    v->serial = get32(req + 8);
    presentMsc(v->pScreen, &ust, &msc);
    v->msc = presentTarget(msc, get64(req + 16), get64(req + 24), get64(req + 32));
    presentQueueVblank(v, msc, ust);
    return client->noClientException;
}

static int
ProcPresentSelectInput(ClientPtr client)
{
    CARD8 *req = (CARD8 *) client->requestBuffer;
    XID eid;
    Window window;
    CARD32 mask;
    PresentSelPtr s;

    if (client->req_len != 4)
	return BadLength;
    eid = get32(req + 4);
    window = get32(req + 8);
    mask = get32(req + 12);
    if (!LookupIDByType(window, RT_WINDOW)) {
	client->errorValue = window;
	return BadWindow;
    }
    if (mask & ~PresentAllEvents) {
	client->errorValue = mask;
	return BadValue;
    }
    /* the same eid again changes its mask, 0 drops it */
    s = (PresentSelPtr) LookupIDByType(eid, presentEventType);
    if (s) {
	if (s->client != client || s->window != window)
	    return BadMatch;
	if (mask == 0)
	    FreeResource(eid, RT_NONE);
	else
	    s->mask = mask;
	return Success;
    }
    if (mask == 0)
	return Success;
    if (!LegalNewID(eid, client)) {
	client->errorValue = eid;
	return BadIDChoice;
    }
    s = xalloc(sizeof *s);
    if (!s)
	return BadAlloc;
    s->eid = eid;
    s->client = client;
    s->window = window;
    s->mask = mask;
    s->next = presentSels;
    presentSels = s;
    if (!AddResource(eid, presentEventType, (pointer) s))
	return BadAlloc;
    return Success;
}

static int
ProcPresentQueryCapabilities(ClientPtr client)
{
    xGenericReply rep;

    if (client->req_len != 2)
	return BadLength;
    memset(&rep, 0, sizeof rep);
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.data00 = 0;	/* no async flips, no fences, no ust-only: plain copies on time */
    WriteToClient(client, sizeof rep, (char *) &rep);
    return client->noClientException;
}

static int
ProcPresentDispatch(ClientPtr client)
{
    switch (((CARD8 *) client->requestBuffer)[1]) {
    case X_PresentQueryVersion:		return ProcPresentQueryVersion(client);
    case X_PresentPixmap:		return ProcPresentPixmap(client);
    case X_PresentNotifyMSC:		return ProcPresentNotifyMSC(client);
    case X_PresentSelectInput:		return ProcPresentSelectInput(client);
    case X_PresentQueryCapabilities:	return ProcPresentQueryCapabilities(client);
    }
    return BadRequest;
}

/* clients of the other byte order: not yet */
static int
SProcPresentDispatch(ClientPtr client)
{
    return BadImplementation;
}

/* ---------- Generic Event Extension, just enough ---------- */

static int
ProcGEDispatch(ClientPtr client)
{
    xGenericReply rep;

    if (((CARD8 *) client->requestBuffer)[1] != 0)	/* GEQueryVersion */
	return BadRequest;
    memset(&rep, 0, sizeof rep);
    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.data00 = 1;	/* 1.0, as two CARD16s: major 1, minor 0 */
    WriteToClient(client, sizeof rep, (char *) &rep);
    return client->noClientException;
}

static int
SProcGEDispatch(ClientPtr client)
{
    return BadImplementation;
}

static void
geReset(ExtensionEntry *e)
{
}

static void
presentReset(ExtensionEntry *e)
{
    PresentVblankPtr v;

    while ((v = presentQueue)) {
	presentQueue = v->next;
	if (v->pPixmap)
	    (*v->pPixmap->drawable.pScreen->DestroyPixmap) (v->pPixmap);
	if (v->timer)
	    TimerFree(v->timer);
	xfree(v);
    }
    memset(presentHooks, 0, sizeof presentHooks);
}

void
PresentExtensionInit(void)
{
    ExtensionEntry *e;

    presentEventType = CreateNewResourceType(presentFreeSel);
    if (!presentEventType)
	return;
    /* AddExtension wants every hook, even one that does nothing */
    if (!AddExtension("Generic Event Extension", 0, 0, ProcGEDispatch, SProcGEDispatch,
		      geReset, StandardMinorOpcode))
	return;
    e = AddExtension("Present", 0, 0, ProcPresentDispatch, SProcPresentDispatch,
		     presentReset, StandardMinorOpcode);
    if (!e)
	return;
    presentOpcode = e->base;
}
