/*
   Copyright (c) 2002  XFree86 Inc
*/
/* $XFree86: xc/programs/Xserver/Xext/xres.c,v 1.11 2005/10/14 15:16:12 tsi Exp $ */

#define NEED_EVENTS
#define NEED_REPLIES
#include <X11/X.h>
#include <X11/Xproto.h>
#include "misc.h"
#include "os.h"
#include "dixstruct.h"
#include "extnsionst.h"
#include "swaprep.h"
#include <X11/extensions/XResproto.h>
#include "pixmapstr.h"
#include "modinit.h"

#ifdef RES

static int
ProcXResQueryVersion (ClientPtr client)
{
    REQUEST(xXResQueryVersionReq);
    xXResQueryVersionReply rep;
    CARD16 client_major, client_minor;  /* not used */

    REQUEST_SIZE_MATCH (xXResQueryVersionReq);

    client_major = stuff->client_major;
    client_minor = stuff->client_minor;
    (void) client_major;
    (void) client_minor;

    rep.type = X_Reply;
    rep.length = 0;
    rep.sequenceNumber = client->sequence;
    rep.server_major = XRES_MAJOR_VERSION;
    rep.server_minor = XRES_MINOR_VERSION;   
    if (client->swapped) { 
        int n;
        swaps(&rep.sequenceNumber, n);
        swapl(&rep.length, n);     
        swaps(&rep.server_major, n);
        swaps(&rep.server_minor, n);
    }
    WriteToClient(client, sizeof (xXResQueryVersionReply), (char *)&rep);
    return (client->noClientException);
}

static int
ProcXResQueryClients (ClientPtr client)
{
    /* REQUEST(xXResQueryClientsReq); */
    xXResQueryClientsReply rep;
    int *current_clients;
    int i, num_clients;

    REQUEST_SIZE_MATCH(xXResQueryClientsReq);

    current_clients = ALLOCATE_LOCAL((currentMaxClients - 1) * sizeof(int));

    num_clients = 0;
    for(i = 1; i < currentMaxClients; i++) {
       if(clients[i]) {
           current_clients[num_clients] = i;
           num_clients++;   
       }
    }

    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.num_clients = num_clients;
    rep.length = rep.num_clients * sz_xXResClient >> 2;
    if (client->swapped) {
        int n;
        swaps (&rep.sequenceNumber, n);
        swapl (&rep.length, n);
        swapl (&rep.num_clients, n);
    }   
    WriteToClient (client, sizeof (xXResQueryClientsReply), (char *) &rep);

    if(num_clients) {
        xXResClient scratch;

        for(i = 0; i < num_clients; i++) {
            scratch.resource_base = clients[current_clients[i]]->clientAsMask;
            scratch.resource_mask = RESOURCE_ID_MASK;
        
            if(client->swapped) {
                int n;
                swapl (&scratch.resource_base, n);
                swapl (&scratch.resource_mask, n);
            }
            WriteToClient (client, sz_xXResClient, (char *) &scratch);
        }
    }

    DEALLOCATE_LOCAL(current_clients);

    return (client->noClientException);
}


static void
ResFindAllRes (pointer value, XID id, RESTYPE type, pointer cdata)
{
    int *counts = (int *)cdata;

    counts[(type & TypeMask) - 1]++;
}

static int
ProcXResQueryClientResources (ClientPtr client)
{
    REQUEST(xXResQueryClientResourcesReq);
    xXResQueryClientResourcesReply rep;
    int i, clientID, num_types;
    int *counts;

    REQUEST_SIZE_MATCH(xXResQueryClientResourcesReq);

    clientID = CLIENT_ID(stuff->xid);

    /* we could remove the (clientID == 0) check if we wanted to allow
       probing the X-server's resource usage */
    if(!clientID || (clientID >= currentMaxClients) || !clients[clientID]) {
        client->errorValue = stuff->xid;
        return BadValue;
    }

    counts = ALLOCATE_LOCAL((lastResourceType + 1) * sizeof(int));

    memset(counts, 0, (lastResourceType + 1) * sizeof(int));

    FindAllClientResources(clients[clientID], ResFindAllRes, counts);

    num_types = 0;

    for(i = 0; i <= lastResourceType; i++) {
       if(counts[i]) num_types++;
    }

    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.num_types = num_types;
    rep.length = rep.num_types * sz_xXResType >> 2;
    if (client->swapped) {
        int n;
        swaps (&rep.sequenceNumber, n);
        swapl (&rep.length, n);
        swapl (&rep.num_types, n);
    }   
    WriteToClient (client,sizeof(xXResQueryClientResourcesReply),(char*)&rep);

    if(num_types) {
        xXResType scratch;

        /* <= like the counting above: with < a client holding the last type
         * got a reply that promised one more entry than it sent */
        for(i = 0; i <= lastResourceType; i++) {
            if(!counts[i]) continue;

            if(!ResourceNames[i + 1]) {
                char buf[40];
                sprintf(buf, "Unregistered resource %i", i + 1);
                RegisterResourceName(i + 1, buf);
            }

            scratch.resource_type = ResourceNames[i + 1];
            scratch.count = counts[i];

            if(client->swapped) {
                int n;
                swapl (&scratch.resource_type, n);
                swapl (&scratch.count, n);
            }
            WriteToClient (client, sz_xXResType, (char *) &scratch);
        }
    }

    DEALLOCATE_LOCAL(counts);
    
    return (client->noClientException);
}

static void 
ResFindPixmaps (pointer value, XID id, pointer cdata)
{
   unsigned long *bytes = (unsigned long *)cdata;
   PixmapPtr pix = (PixmapPtr)value;

   *bytes += (pix->devKind * pix->drawable.height);
}

static int
ProcXResQueryClientPixmapBytes (ClientPtr client)
{
    REQUEST(xXResQueryClientPixmapBytesReq);
    xXResQueryClientPixmapBytesReply rep;
    int clientID;
    unsigned long bytes;

    REQUEST_SIZE_MATCH(xXResQueryClientPixmapBytesReq);

    clientID = CLIENT_ID(stuff->xid);

    /* we could remove the (clientID == 0) check if we wanted to allow
       probing the X-server's resource usage */
    if(!clientID || (clientID >= currentMaxClients) || !clients[clientID]) {
        client->errorValue = stuff->xid;
        return BadValue;
    }

    bytes = 0;

    FindClientResourcesByType(clients[clientID], RT_PIXMAP, ResFindPixmaps, 
                              (pointer)(&bytes));

    rep.type = X_Reply;
    rep.sequenceNumber = client->sequence;
    rep.length = 0;
    rep.bytes = bytes;
#ifdef XSERVER64
    rep.bytes_overflow = bytes >> 32;
#else
    rep.bytes_overflow = 0;
#endif
    if (client->swapped) {
        int n;
        swaps (&rep.sequenceNumber, n);
        swapl (&rep.length, n);
        swapl (&rep.bytes, n);
        swapl (&rep.bytes_overflow, n);
    }
    WriteToClient (client,sizeof(xXResQueryClientPixmapBytesReply),(char*)&rep);

    return (client->noClientException);
}


/* X-Resource 1.2: QueryClientIds (which client, which pid) and QueryResourceBytes */

typedef struct {
    char *buf;
    int used, size, count;
    Bool failed;
} ResReplyBuf;

static pointer
ResReserve(ResReplyBuf *b, int n)
{
    pointer at;

    if (b->failed)
	return NULL;
    if (b->used + n > b->size) {
	int size = b->size ? b->size : 256;
	char *nbuf;

	while (size < b->used + n) {
	    if (size > (1 << 26)) {
		b->failed = TRUE;
		return NULL;
	    }
	    size *= 2;
	}
	if (!(nbuf = xrealloc(b->buf, size))) {
	    b->failed = TRUE;
	    return NULL;
	}
	b->buf = nbuf;
	b->size = size;
    }
    at = b->buf + b->used;
    b->used += n;
    return at;
}

static void
ResAddClientId(ResReplyBuf *b, ClientPtr target, CARD32 mask, Bool swapped)
{
    xXResClientIdValue *v;
    int n;

    if (!mask || (mask & X_XResClientXIDMask)) {
	if (!(v = ResReserve(b, sz_xXResClientIdValue)))
	    return;
	v->spec.client = target->clientAsMask;
	v->spec.mask = X_XResClientXIDMask;
	v->length = 0;
	if (swapped) {
	    swapl(&v->spec.client, n);
	    swapl(&v->spec.mask, n);
	}
	b->count++;
    }
    if (mask & X_XResLocalClientPIDMask) {
	int pid = LocalClientPid(target);
	CARD32 *value;

	if (pid == -1)
	    return;
	if (!(v = ResReserve(b, sz_xXResClientIdValue + 4)))
	    return;
	value = (CARD32 *)((char *)v + sz_xXResClientIdValue);
	v->spec.client = target->clientAsMask;
	v->spec.mask = X_XResLocalClientPIDMask;
	v->length = 4;
	*value = pid;
	if (swapped) {
	    swapl(&v->spec.client, n);
	    swapl(&v->spec.mask, n);
	    swapl(&v->length, n);
	    swapl(value, n);
	}
	b->count++;
    }
}

static int
ProcXResQueryClientIds (ClientPtr client)
{
    REQUEST(xXResQueryClientIdsReq);
    xXResQueryClientIdsReply rep;
    xXResClientIdSpec *specs;
    ResReplyBuf b = { NULL, 0, 0, 0, FALSE };
    CARD32 i;
    int c, n;

    REQUEST_AT_LEAST_SIZE(xXResQueryClientIdsReq);
    if (stuff->numSpecs > (client->req_len - (sz_xXResQueryClientIdsReq >> 2)) / 2 ||
	client->req_len != (sz_xXResQueryClientIdsReq >> 2) + 2 * stuff->numSpecs)
	return BadLength;
    specs = (xXResClientIdSpec *)&stuff[1];

    for (i = 0; i < stuff->numSpecs; i++) {
	CARD32 id = specs[i].client, mask = specs[i].mask;

	if (id == 0) {
	    for (c = 1; c < currentMaxClients; c++)
		if (clients[c])
		    ResAddClientId(&b, clients[c], mask, client->swapped);
	} else {
	    c = CLIENT_ID(id);
	    if (c > 0 && c < currentMaxClients && clients[c])
		ResAddClientId(&b, clients[c], mask, client->swapped);
	}
    }
    if (b.failed) {
	xfree(b.buf);
	return BadAlloc;
    }

    rep.type = X_Reply;
    rep.pad1 = 0;
    rep.sequenceNumber = client->sequence;
    rep.length = b.used >> 2;
    rep.numIds = b.count;
    rep.pad2 = rep.pad3 = rep.pad4 = rep.pad5 = rep.pad6 = 0;
    if (client->swapped) {
	swaps(&rep.sequenceNumber, n);
	swapl(&rep.length, n);
	swapl(&rep.numIds, n);
    }
    WriteToClient(client, sizeof(rep), (char *)&rep);
    if (b.used)
	WriteToClient(client, b.used, b.buf);
    xfree(b.buf);
    return client->noClientException;
}

typedef struct {
    ResReplyBuf *b;
    XID resource;
    Atom type;
    Bool swapped;
} ResBytesCtx;

static void
ResAddBytes (pointer value, XID id, RESTYPE type, pointer cdata)
{
    ResBytesCtx *ctx = (ResBytesCtx *)cdata;
    xXResResourceSizeValue *v;
    Atom atom = 0;
    unsigned long bytes = 0;
    CARD32 refs = 1;
    int n;

    if (ctx->resource && id != ctx->resource)
	return;
    if ((type & TypeMask) <= lastResourceType && ResourceNames)
	atom = ResourceNames[type & TypeMask];
    if (ctx->type && atom != ctx->type)
	return;
    if (type == RT_PIXMAP) {
	PixmapPtr pix = (PixmapPtr)value;
	bytes = (unsigned long)pix->devKind * pix->drawable.height;
	refs = pix->refcnt;
    }
    if (!(v = ResReserve(ctx->b, sz_xXResResourceSizeValue)))
	return;
    v->size.spec.resource = id;
    v->size.spec.type = atom;
    v->size.bytes = bytes > 0xffffffffUL ? 0xffffffffUL : bytes;
    v->size.refCount = refs;
    v->size.useCount = 1;
    v->numCrossReferences = 0;
    if (ctx->swapped) {
	swapl(&v->size.spec.resource, n);
	swapl(&v->size.spec.type, n);
	swapl(&v->size.bytes, n);
	swapl(&v->size.refCount, n);
	swapl(&v->size.useCount, n);
	swapl(&v->numCrossReferences, n);
    }
    ctx->b->count++;
}

static int
ProcXResQueryResourceBytes (ClientPtr client)
{
    REQUEST(xXResQueryResourceBytesReq);
    xXResQueryResourceBytesReply rep;
    xXResResourceIdSpec *specs;
    ResReplyBuf b = { NULL, 0, 0, 0, FALSE };
    ResBytesCtx ctx;
    CARD32 i;
    int c, first, last, n;

    REQUEST_AT_LEAST_SIZE(xXResQueryResourceBytesReq);
    if (stuff->numSpecs > (client->req_len - (sz_xXResQueryResourceBytesReq >> 2)) / 2 ||
	client->req_len != (sz_xXResQueryResourceBytesReq >> 2) + 2 * stuff->numSpecs)
	return BadLength;
    specs = (xXResResourceIdSpec *)&stuff[1];

    if (stuff->client) {
	c = CLIENT_ID(stuff->client);
	if (c <= 0 || c >= currentMaxClients || !clients[c]) {
	    client->errorValue = stuff->client;
	    return BadValue;
	}
	first = last = c;
    } else {
	first = 1;
	last = currentMaxClients - 1;
    }

    ctx.b = &b;
    ctx.swapped = client->swapped;
    for (i = 0; i < stuff->numSpecs; i++) {
	ctx.resource = specs[i].resource;
	ctx.type = specs[i].type;
	if (ctx.type && !ValidAtom(ctx.type)) {
	    xfree(b.buf);
	    client->errorValue = ctx.type;
	    return BadAtom;
	}
	for (c = first; c <= last; c++) {
	    if (!clients[c])
		continue;
	    if (ctx.resource && CLIENT_ID(ctx.resource) != c)
		continue;
	    FindAllClientResources(clients[c], ResAddBytes, &ctx);
	}
    }
    if (b.failed) {
	xfree(b.buf);
	return BadAlloc;
    }

    rep.type = X_Reply;
    rep.pad1 = 0;
    rep.sequenceNumber = client->sequence;
    rep.length = b.used >> 2;
    rep.numSizes = b.count;
    rep.pad2 = rep.pad3 = rep.pad4 = rep.pad5 = rep.pad6 = 0;
    if (client->swapped) {
	swaps(&rep.sequenceNumber, n);
	swapl(&rep.length, n);
	swapl(&rep.numSizes, n);
    }
    WriteToClient(client, sizeof(rep), (char *)&rep);
    if (b.used)
	WriteToClient(client, b.used, b.buf);
    xfree(b.buf);
    return client->noClientException;
}

static void
ResResetProc (ExtensionEntry *extEntry) { }

static int
ProcResDispatch (ClientPtr client)
{
    REQUEST(xReq);
    switch (stuff->data) {
    case X_XResQueryVersion:
        return ProcXResQueryVersion(client);
    case X_XResQueryClients:
        return ProcXResQueryClients(client);
    case X_XResQueryClientResources:
        return ProcXResQueryClientResources(client);
    case X_XResQueryClientPixmapBytes:
        return ProcXResQueryClientPixmapBytes(client);
    case X_XResQueryClientIds:
        return ProcXResQueryClientIds(client);
    case X_XResQueryResourceBytes:
        return ProcXResQueryResourceBytes(client);
    default: break;
    }

    return BadRequest;
}

/* swap the counts first, check them against req_len, then the specs */
static int
SProcXResQueryClientIds (ClientPtr client)
{
    REQUEST(xXResQueryClientIdsReq);
    xXResClientIdSpec *specs;
    CARD32 i;
    int n;

    REQUEST_AT_LEAST_SIZE(xXResQueryClientIdsReq);
    swapl(&stuff->numSpecs, n);
    if (stuff->numSpecs > (client->req_len - (sz_xXResQueryClientIdsReq >> 2)) / 2 ||
	client->req_len != (sz_xXResQueryClientIdsReq >> 2) + 2 * stuff->numSpecs)
	return BadLength;
    specs = (xXResClientIdSpec *)&stuff[1];
    for (i = 0; i < stuff->numSpecs; i++) {
	swapl(&specs[i].client, n);
	swapl(&specs[i].mask, n);
    }
    return ProcXResQueryClientIds(client);
}

static int
SProcXResQueryResourceBytes (ClientPtr client)
{
    REQUEST(xXResQueryResourceBytesReq);
    xXResResourceIdSpec *specs;
    CARD32 i;
    int n;

    REQUEST_AT_LEAST_SIZE(xXResQueryResourceBytesReq);
    swapl(&stuff->client, n);
    swapl(&stuff->numSpecs, n);
    if (stuff->numSpecs > (client->req_len - (sz_xXResQueryResourceBytesReq >> 2)) / 2 ||
	client->req_len != (sz_xXResQueryResourceBytesReq >> 2) + 2 * stuff->numSpecs)
	return BadLength;
    specs = (xXResResourceIdSpec *)&stuff[1];
    for (i = 0; i < stuff->numSpecs; i++) {
	swapl(&specs[i].resource, n);
	swapl(&specs[i].type, n);
    }
    return ProcXResQueryResourceBytes(client);
}

static int
SProcXResQueryVersion (ClientPtr client)
{
    REQUEST(xXResQueryVersionReq);
    int n;

    REQUEST_SIZE_MATCH (xXResQueryVersionReq);
    swaps(&stuff->client_major,n);
    swaps(&stuff->client_minor,n);
    return ProcXResQueryVersion(client);
}

static int
SProcXResQueryClientResources (ClientPtr client)
{
    REQUEST(xXResQueryClientResourcesReq);
    int n;

    REQUEST_SIZE_MATCH (xXResQueryClientResourcesReq);
    swapl(&stuff->xid,n);
    return ProcXResQueryClientResources(client);
}

static int
SProcXResQueryClientPixmapBytes (ClientPtr client)
{
    REQUEST(xXResQueryClientPixmapBytesReq);
    int n;

    REQUEST_SIZE_MATCH (xXResQueryClientPixmapBytesReq);
    swapl(&stuff->xid,n);
    return ProcXResQueryClientPixmapBytes(client);
}

static int
SProcResDispatch (ClientPtr client)
{
    REQUEST(xReq);
    REQUEST_AT_LEAST_SIZE(xReq);
    int n;

    swaps(&stuff->length,n);

    switch (stuff->data) {
    case X_XResQueryVersion:
        return SProcXResQueryVersion(client);
    case X_XResQueryClients:  /* nothing to swap */
        return ProcXResQueryClients(client);
    case X_XResQueryClientResources:
        return SProcXResQueryClientResources(client);
    case X_XResQueryClientPixmapBytes:
        return SProcXResQueryClientPixmapBytes(client);
    case X_XResQueryClientIds:
        return SProcXResQueryClientIds(client);
    case X_XResQueryResourceBytes:
        return SProcXResQueryResourceBytes(client);
    default: break;
    }

    return BadRequest;
}

void
ResExtensionInit(INITARGS)
{
    (void) AddExtension(XRES_NAME, 0, 0,
                            ProcResDispatch, SProcResDispatch,
                            ResResetProc, StandardMinorOpcode);

    RegisterResourceName(RT_NONE, "NONE");
    RegisterResourceName(RT_WINDOW, "WINDOW");
    RegisterResourceName(RT_PIXMAP, "PIXMAP");
    RegisterResourceName(RT_GC, "GC");
    RegisterResourceName(RT_FONT, "FONT");
    RegisterResourceName(RT_CURSOR, "CURSOR");
    RegisterResourceName(RT_COLORMAP, "COLORMAP");
    RegisterResourceName(RT_CMAPENTRY, "COLORMAP ENTRY");
    RegisterResourceName(RT_OTHERCLIENT, "OTHER CLIENT");
    RegisterResourceName(RT_PASSIVEGRAB, "PASSIVE GRAB");
}

#endif
