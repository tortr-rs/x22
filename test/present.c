/* present: checks the Present extension. waits for frames with NotifyMSC, then shows
 * 20 pixmaps one frame apart and prints when each one really went up.
 * prints "present: ok" at the end if the timings look like a real clock */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <xcb/xcb.h>
#include <xcb/present.h>

static xcb_connection_t *c;
static xcb_window_t win;

/* the next CompleteNotify, or exit if the server stops talking */
static xcb_present_complete_notify_event_t *complete(int *idles)
{
	xcb_generic_event_t *e;
	while ((e = xcb_wait_for_event(c))) {
		if ((e->response_type & 0x7f) == XCB_GE_GENERIC) {
			xcb_ge_generic_event_t *g = (void *) e;
			if (g->event_type == XCB_PRESENT_EVENT_COMPLETE_NOTIFY)
				return (void *) e;
			if (g->event_type == XCB_PRESENT_EVENT_IDLE_NOTIFY)
				(*idles)++;
		}
		free(e);
	}
	fputs("present: the server went away\n", stderr);
	exit(1);
}

int main(void)
{
	c = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(c)) { fputs("present: no display\n", stderr); return 1; }
	xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

	const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c, &xcb_present_id);
	if (!ext || !ext->present) { fputs("present: the server doesn't have it\n", stderr); return 1; }
	xcb_present_query_version_reply_t *v =
		xcb_present_query_version_reply(c, xcb_present_query_version(c, 1, 0), NULL);
	if (!v) { fputs("present: no version reply\n", stderr); return 1; }
	printf("present: version %u.%u\n", v->major_version, v->minor_version);

	win = xcb_generate_id(c);
	uint32_t vals[] = { s->black_pixel };
	xcb_create_window(c, s->root_depth, win, s->root, 100, 100, 200, 200, 0,
			  XCB_WINDOW_CLASS_INPUT_OUTPUT, s->root_visual, XCB_CW_BACK_PIXEL, vals);
	xcb_map_window(c, win);
	xcb_present_select_input(c, xcb_generate_id(c), win,
		XCB_PRESENT_EVENT_MASK_COMPLETE_NOTIFY | XCB_PRESENT_EVENT_MASK_IDLE_NOTIFY);

	/* where the clock is now */
	xcb_present_notify_msc(c, win, 0, 0, 0, 0);
	xcb_flush(c);
	int idles = 0;
	xcb_present_complete_notify_event_t *e = complete(&idles);
	uint64_t msc0 = e->msc, ust0 = e->ust;
	free(e);

	/* 60 frames from now: how long did that take? */
	xcb_present_notify_msc(c, win, 1, msc0 + 60, 0, 0);
	xcb_flush(c);
	e = complete(&idles);
	double secs = (e->ust - ust0) / 1e6;
	printf("present: frame %" PRIu64 " to %" PRIu64 " took %.3f s, %.1f frames a second\n",
	       msc0, e->msc, secs, (e->msc - msc0) / secs);
	int ok = e->msc == msc0 + 60 && secs > 0.2 && secs < 3;
	uint64_t msc = e->msc;
	free(e);

	/* 20 pictures, each aimed at the frame after the last */
	xcb_pixmap_t pix[2];
	xcb_gcontext_t gc = xcb_generate_id(c);
	xcb_create_gc(c, gc, win, 0, NULL);
	for (int i = 0; i < 2; i++) {
		pix[i] = xcb_generate_id(c);
		xcb_create_pixmap(c, s->root_depth, pix[i], win, 200, 200);
		uint32_t fg[] = { i ? 0x2a8a8a : 0xe0e0e0 };
		xcb_change_gc(c, gc, XCB_GC_FOREGROUND, fg);
		xcb_rectangle_t r = { 0, 0, 200, 200 };
		xcb_poly_fill_rectangle(c, pix[i], gc, 1, &r);
	}
	int late = 0, missed = 0;
	for (uint32_t n = 0; n < 20; n++) {
		xcb_present_pixmap(c, win, pix[n & 1], 100 + n, 0, 0, 0, 0, 0, 0, 0, 0,
				   msc + 1, 0, 0, 0, NULL);
		xcb_flush(c);
		e = complete(&idles);
		if (e->serial != 100 + n || e->kind != XCB_PRESENT_COMPLETE_KIND_PIXMAP) ok = 0;
		if (e->msc != msc + 1) late++;
		if (e->msc > msc + 2) missed++;
		msc = e->msc;
		free(e);
	}
	printf("present: 20 pictures, %d a frame late, %d more than that, %d idle notices\n",
	       late, missed, idles);
	if (idles != 20 || missed > 2) ok = 0;
	puts(ok ? "present: ok" : "present: something's off");
	xcb_disconnect(c);
	return !ok;
}
