/* win: the smallest X program there is. opens a window with a title and waits.
 * win TITLE [SECONDS] [#rrggbb]   for testing window managers on x22 when there's no xterm around */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>

int main(int argc, char **argv)
{
	Display *d = XOpenDisplay(NULL);
	if (!d) { fputs("win: no display\n", stderr); return 1; }
	unsigned long bg = argc > 3 ? strtoul(argv[3] + (argv[3][0] == '#'), NULL, 16) : 0x1c1f2a;
	Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 0, 0, 300, 200, 0, 0, bg);
	XStoreName(d, w, argc > 1 ? argv[1] : "win");
	XMapWindow(d, w);
	XFlush(d);
	sleep(argc > 2 ? atoi(argv[2]) : 60);
	return 0;
}
