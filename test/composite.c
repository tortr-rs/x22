/* composite: does what a compositing manager does, and checks each step.
 * redirects every window off the screen (manual, like a compositor would), draws in
 * one, waits for DAMAGE to report it, reads the window's own pixmap back through
 * NameWindowPixmap, then paints that pixmap onto the root with RENDER at half
 * opacity and checks the screen. prints "composite: ok" if all of it worked */
#include <stdio.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/damage.h>
#include <xcb/xfixes.h>
#include <xcb/render.h>

static xcb_connection_t *c;
static int ok = 1;

static void check(int good, const char *what)
{
	printf("composite: %-52s %s\n", what, good ? "yes" : "NO");
	if (!good) ok = 0;
}

static uint32_t pixel(xcb_drawable_t d, int x, int y)
{
	xcb_get_image_reply_t *r = xcb_get_image_reply(c,
		xcb_get_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, d, x, y, 1, 1, ~0), NULL);
	uint32_t v = 0xdeadbeef;
	if (r) { v = *(uint32_t *) xcb_get_image_data(r) & 0xffffff; free(r); }
	return v;
}

static xcb_render_pictformat_t format(xcb_render_query_pict_formats_reply_t *f, xcb_visualid_t vis)
{
	xcb_render_pictscreen_iterator_t si = xcb_render_query_pict_formats_screens_iterator(f);
	for (; si.rem; xcb_render_pictscreen_next(&si)) {
		xcb_render_pictdepth_iterator_t di = xcb_render_pictscreen_depths_iterator(si.data);
		for (; di.rem; xcb_render_pictdepth_next(&di)) {
			xcb_render_pictvisual_iterator_t vi = xcb_render_pictdepth_visuals_iterator(di.data);
			for (; vi.rem; xcb_render_pictvisual_next(&vi))
				if (vi.data->visual == vis) return vi.data->format;
		}
	}
	return 0;
}

int main(void)
{
	c = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(c)) { fputs("composite: no display\n", stderr); return 1; }
	xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;

	xcb_composite_query_version_reply_t *cv = xcb_composite_query_version_reply(c, xcb_composite_query_version(c, 0, 4), NULL);
	xcb_damage_query_version_reply_t *dv = xcb_damage_query_version_reply(c, xcb_damage_query_version(c, 1, 1), NULL);
	xcb_xfixes_query_version_reply_t *fv = xcb_xfixes_query_version_reply(c, xcb_xfixes_query_version(c, 4, 0), NULL);
	if (!cv || !dv || !fv) { puts("composite: an extension is missing"); return 1; }
	printf("composite: Composite %u.%u, DAMAGE %u.%u, XFIXES %u.%u\n", cv->major_version, cv->minor_version,
	       dv->major_version, dv->minor_version, fv->major_version, fv->minor_version);

	/* a window to watch, red */
	xcb_window_t w = xcb_generate_id(c);
	uint32_t red[] = { 0xd03030 };
	/* override-redirect, so a window manager that's running doesn't take it and map it
	 * whenever it gets round to it */
	uint32_t vals[] = { red[0], 1, XCB_EVENT_MASK_EXPOSURE };
	xcb_create_window(c, s->root_depth, w, s->root, 100, 100, 200, 150, 0,
			  XCB_WINDOW_CLASS_INPUT_OUTPUT, s->root_visual,
			  XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, vals);
	xcb_map_window(c, w);
	xcb_flush(c);
	xcb_generic_event_t *ev;
	while ((ev = xcb_wait_for_event(c))) {
		int expose = (ev->response_type & 0x7f) == XCB_EXPOSE;
		free(ev);
		if (expose) break;
	}

	/* take everything off the screen: from here on nothing draws there by itself */
	xcb_void_cookie_t rc = xcb_composite_redirect_subwindows_checked(c, s->root, XCB_COMPOSITE_REDIRECT_MANUAL);
	xcb_generic_error_t *err = xcb_request_check(c, rc);
	check(!err, "redirect every window (only one compositor allowed)");
	free(err);

	xcb_damage_damage_t dmg = xcb_generate_id(c);
	xcb_damage_create(c, dmg, w, XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);

	/* draw a green box in the window */
	xcb_gcontext_t gc = xcb_generate_id(c);
	uint32_t green[] = { 0x30b050 };
	xcb_create_gc(c, gc, w, XCB_GC_FOREGROUND, green);
	xcb_rectangle_t box = { 20, 20, 60, 40 };
	xcb_poly_fill_rectangle(c, w, gc, 1, &box);
	xcb_flush(c);

	const xcb_query_extension_reply_t *de = xcb_get_extension_data(c, &xcb_damage_id);
	int got = 0;
	xcb_generic_event_t *e;
	while (!got && (e = xcb_wait_for_event(c))) {
		if ((e->response_type & 0x7f) == de->first_event + XCB_DAMAGE_NOTIFY) got = 1;
		free(e);
	}
	check(got, "drawing in it sends a damage event");
	xcb_damage_subtract(c, dmg, XCB_NONE, XCB_NONE);

	/* the window's contents, from its own pixmap */
	xcb_pixmap_t pix = xcb_generate_id(c);
	xcb_composite_name_window_pixmap(c, w, pix);
	check(pixel(pix, 5, 5) == 0xd03030, "its pixmap has the red background");
	uint32_t px = pixel(pix, 30, 30);
	if (px != 0x30b050) printf("composite: at 30,30 there's %06x\n", px);
	check(px == 0x30b050, "and the green box, where it was drawn");
	check(pixel(s->root, 130, 130) != 0x30b050, "and the screen doesn't, it's off the screen");

	/* paint it on the root at half strength, like a compositor fading a window */
	xcb_render_query_pict_formats_reply_t *pf = xcb_render_query_pict_formats_reply(c, xcb_render_query_pict_formats(c), NULL);
	xcb_render_pictformat_t fmt = format(pf, s->root_visual);
	xcb_render_picture_t src = xcb_generate_id(c), dst = xcb_generate_id(c), mask = xcb_generate_id(c);
	xcb_render_create_picture(c, src, pix, fmt, 0, NULL);
	uint32_t inferiors[] = { XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS };
	xcb_render_create_picture(c, dst, s->root, fmt, XCB_RENDER_CP_SUBWINDOW_MODE, inferiors);
	xcb_render_color_t half = { 0, 0, 0, 0x8000 };
	xcb_rectangle_t whole = { 0, 0, 1, 1 };
	xcb_pixmap_t mp = xcb_generate_id(c);
	xcb_create_pixmap(c, 8, mp, s->root, 1, 1);
	xcb_render_pictformat_t a8 = 0;
	{
		xcb_render_pictforminfo_iterator_t fi = xcb_render_query_pict_formats_formats_iterator(pf);
		for (; fi.rem; xcb_render_pictforminfo_next(&fi))
			if (fi.data->depth == 8 && fi.data->type == XCB_RENDER_PICT_TYPE_DIRECT && fi.data->direct.alpha_mask == 0xff)
				a8 = fi.data->id;
	}
	uint32_t rep[] = { 1 };
	xcb_render_create_picture(c, mask, mp, a8, XCB_RENDER_CP_REPEAT, rep);
	xcb_render_fill_rectangles(c, XCB_RENDER_PICT_OP_SRC, mask, half, 1, &whole);
	/* black first, so half of red over black is a known color */
	xcb_render_color_t black = { 0, 0, 0, 0xffff };
	xcb_rectangle_t area = { 100, 100, 200, 150 };
	xcb_render_fill_rectangles(c, XCB_RENDER_PICT_OP_SRC, dst, black, 1, &area);
	xcb_render_composite(c, XCB_RENDER_PICT_OP_OVER, src, mask, dst, 0, 0, 0, 0, 100, 100, 200, 150);
	uint32_t p = pixel(s->root, 105, 105);
	printf("composite: the screen where the red was, at half strength: %06x\n", p);
	check(((p >> 16) & 0xff) > 0x5a && ((p >> 16) & 0xff) < 0x76 && (p & 0xff00) < 0x2000,
	      "painted on the screen at half strength, by RENDER");

	/* and an XFIXES region, the kind a compositor keeps its damage in */
	xcb_xfixes_region_t rg = xcb_generate_id(c);
	xcb_rectangle_t rr[] = { { 0, 0, 10, 10 }, { 5, 5, 10, 10 } };
	xcb_xfixes_create_region(c, rg, 2, rr);
	xcb_xfixes_fetch_region_reply_t *fr = xcb_xfixes_fetch_region_reply(c, xcb_xfixes_fetch_region(c, rg), NULL);
	check(fr && fr->extents.width == 15 && fr->extents.height == 15, "an XFIXES region of two boxes is 15x15");

	/* give the screen back: the window reappears with its background, and the program
	 * gets an Expose to draw the rest, the same as when it was first mapped */
	while ((e = xcb_poll_for_event(c))) free(e);
	xcb_composite_unredirect_subwindows(c, s->root, XCB_COMPOSITE_REDIRECT_MANUAL);
	xcb_flush(c);
	int exposed = 0;
	while (!exposed && (e = xcb_wait_for_event(c))) {
		if ((e->response_type & 0x7f) == XCB_EXPOSE) exposed = 1;
		free(e);
	}
	check(exposed, "unredirected, the window is asked to redraw");
	check(pixel(s->root, 105, 105) == 0xd03030, "and it's back on the screen");

	puts(ok ? "composite: ok" : "composite: something's off");
	xcb_disconnect(c);
	return !ok;
}
