/* xres: X-Resource 1.2, from both byte orders. prints "xres: ok" */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <xcb/xcb.h>
#include <xcb/res.h>

static int bad;
#define FAIL(...) do { printf("xres: " __VA_ARGS__); putchar('\n'); bad++; } while (0)

/* a pid as the server tells it for this window, or 0 */
static unsigned pid_of(xcb_connection_t *c, xcb_window_t w)
{
	xcb_res_client_id_spec_t spec = { w, XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID };
	xcb_res_query_client_ids_reply_t *r =
	    xcb_res_query_client_ids_reply(c, xcb_res_query_client_ids(c, 1, &spec), NULL);
	xcb_res_client_id_value_iterator_t i;
	unsigned pid = 0;

	if (!r)
		return 0;
	for (i = xcb_res_query_client_ids_ids_iterator(r); i.rem; xcb_res_client_id_value_next(&i))
		if (i.data->spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID)
			pid = *xcb_res_client_id_value_value(i.data);
	free(r);
	return pid;
}

/* the other byte order by hand, xcb only speaks the machine's own */
static unsigned short be16(const unsigned char *p) { return p[0] << 8 | p[1]; }
static unsigned be32(const unsigned char *p) { return (unsigned)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void put16(unsigned char *p, unsigned v) { p[0] = v >> 8; p[1] = v; }
static void put32(unsigned char *p, unsigned v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void writen(int fd, const void *buf, size_t n)
{
	if (write(fd, buf, n) != (ssize_t)n)
		FAIL("short write on the swapped connection");
}

static int readn(int fd, void *buf, size_t n)
{
	size_t got = 0;
	while (got < n) {
		ssize_t r = read(fd, (char *)buf + got, n - got);
		if (r <= 0)
			return -1;
		got += r;
	}
	return 0;
}

static void swapped(const char *display, int opcode, unsigned want_pid)
{
	struct sockaddr_un sa = { AF_UNIX, "" };
	unsigned char b[64], *setup;
	unsigned base, len;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);

	snprintf(sa.sun_path, sizeof sa.sun_path, "/tmp/.X11-unix/X%s", display + 1);
	if (fd < 0 || connect(fd, (void *)&sa, sizeof sa) < 0) { FAIL("can't connect for the swapped test"); return; }
	memset(b, 0, 12);
	b[0] = 'B'; put16(b + 2, 11);			/* big endian, protocol 11.0, no auth */
	writen(fd, b, 12);
	if (readn(fd, b, 8) || b[0] != 1) { FAIL("swapped setup failed"); close(fd); return; }
	len = be16(b + 6) * 4;
	setup = malloc(len);
	if (readn(fd, setup, len)) { FAIL("swapped setup cut short"); free(setup); close(fd); return; }
	base = be32(setup + 4);
	free(setup);

	memset(b, 0, 20);
	b[0] = opcode; b[1] = 4; put16(b + 2, 4);	/* 4 words: header + one spec */
	put32(b + 4, 1);
	put32(b + 8, base);
	put32(b + 12, 2);				/* the pid */
	writen(fd, b, 16);
	if (readn(fd, b, 32) || b[0] != 1) { FAIL("swapped QueryClientIds got an error (%d)", b[1]); close(fd); return; }
	len = be32(b + 4) * 4;
	if (be32(b + 8) != 1 || len != 16) { FAIL("swapped QueryClientIds: %u ids, %u bytes", be32(b + 8), len); close(fd); return; }
	readn(fd, b, len);
	if (be32(b + 4) != 2 || be32(b + 8) != 4 || be32(b + 12) != want_pid)
		FAIL("swapped QueryClientIds said pid %u, not %u", be32(b + 12), want_pid);

	/* a count that doesn't fit the request is BadLength (16) */
	memset(b, 0, 8);
	b[0] = opcode; b[1] = 4; put16(b + 2, 2);
	put32(b + 4, 0x40000000);
	writen(fd, b, 8);
	if (readn(fd, b, 32) || b[0] != 0 || b[1] != 16)
		FAIL("swapped QueryClientIds with a bad count gave %d/%d, not BadLength", b[0], b[1]);

	memset(b, 0, 12);
	b[0] = opcode; b[1] = 5; put16(b + 2, 3);
	put32(b + 4, base); put32(b + 8, 0);
	writen(fd, b, 12);
	if (readn(fd, b, 32) || b[0] != 1)
		FAIL("swapped QueryResourceBytes got an error (%d)", b[1]);
	close(fd);
}

int main(void)
{
	xcb_connection_t *c = xcb_connect(NULL, NULL);
	xcb_screen_t *s;
	xcb_res_query_version_reply_t *v;
	xcb_window_t w;
	xcb_pixmap_t p;
	const char *display = getenv("DISPLAY");

	if (xcb_connection_has_error(c)) { fputs("xres: no display\n", stderr); return 1; }
	s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
	v = xcb_res_query_version_reply(c, xcb_res_query_version(c, 1, 2), NULL);
	if (!v) { puts("xres: the server doesn't have X-Resource"); return 1; }
	if (v->server_major != 1 || v->server_minor < 2) FAIL("version %d.%d, not 1.2", v->server_major, v->server_minor);
	free(v);

	/* the pid behind a window: ours */
	w = xcb_generate_id(c);
	xcb_create_window(c, XCB_COPY_FROM_PARENT, w, s->root, 0, 0, 10, 10, 0,
	    XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, NULL);
	if (pid_of(c, w) != (unsigned)getpid()) FAIL("the window's pid came back as %u, not %d", pid_of(c, w), getpid());
	/* and nothing for an XID nobody owns */
	if (pid_of(c, 0x1fe00000)) FAIL("an XID nobody has came back with a pid");

	/* every client's XID, ours among them */
	{
		xcb_res_client_id_spec_t all = { 0, XCB_RES_CLIENT_ID_MASK_CLIENT_XID };
		xcb_res_query_client_ids_reply_t *r =
		    xcb_res_query_client_ids_reply(c, xcb_res_query_client_ids(c, 1, &all), NULL);
		xcb_res_client_id_value_iterator_t i;
		int found = 0;
		for (i = xcb_res_query_client_ids_ids_iterator(r); r && i.rem; xcb_res_client_id_value_next(&i))
			if (i.data->spec.client == (w & ~0x1fffff))
				found = 1;
		if (!found) FAIL("all the clients, and we weren't one");
		free(r);
	}

	/* the size of a 100x100 pixmap, which at depth 24 is 4 bytes a pixel */
	p = xcb_generate_id(c);
	xcb_create_pixmap(c, s->root_depth, p, s->root, 100, 100);
	{
		xcb_res_resource_id_spec_t spec = { p, 0 };
		xcb_res_query_resource_bytes_reply_t *r =
		    xcb_res_query_resource_bytes_reply(c, xcb_res_query_resource_bytes(c, p, 1, &spec), NULL);
		xcb_res_resource_size_value_iterator_t i;
		unsigned bytes = 0;
		for (i = xcb_res_query_resource_bytes_sizes_iterator(r); r && i.rem; xcb_res_resource_size_value_next(&i))
			if (i.data->size.spec.resource == p)
				bytes = i.data->size.bytes;
		if (bytes < 100 * 100 * 3) FAIL("the pixmap is %u bytes, should be about 40000", bytes);
		free(r);
	}

	/* the old QueryClientResources, which used to leave the last type out */
	{
		xcb_res_query_client_resources_reply_t *r = xcb_res_query_client_resources_reply(c,
		    xcb_res_query_client_resources(c, w), NULL);
		if (!r || r->num_types < 2) FAIL("QueryClientResources: %d types", r ? (int)r->num_types : -1);
		free(r);
	}

	{
		xcb_res_resource_id_spec_t spec = { 0, 0 };
		xcb_generic_error_t *e = NULL;
		xcb_res_query_resource_bytes_reply_t *r =
		    xcb_res_query_resource_bytes_reply(c, xcb_res_query_resource_bytes(c, 0, 0, &spec), &e);
		if (!r) FAIL("QueryResourceBytes with no specs got an error");
		free(r); free(e);
	}

	const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c, &xcb_res_id);
	swapped(display ? display : ":0", ext->major_opcode, getpid());

	/* still there */
	free(xcb_get_input_focus_reply(c, xcb_get_input_focus(c), NULL));
	if (xcb_connection_has_error(c)) FAIL("the connection broke");
	if (bad)
		return 1;
	puts("xres: ok");
	xcb_disconnect(c);
	return 0;
}
