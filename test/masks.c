/* masks: what the server says its colours look like. on x8664 at depth 24 this should say
 * r ff0000 g 00ff00 b 0000ff, and amber should come out as e0a43c. */
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

int main(void)
{
	Display *d = XOpenDisplay(NULL);
	if (!d) { fputs("masks: no display\n", stderr); return 1; }
	Visual *v = DefaultVisual(d, 0);
	XColor c;
	printf("depth %d  r %06lx g %06lx b %06lx\n", DefaultDepth(d, 0), v->red_mask, v->green_mask, v->blue_mask);
	XParseColor(d, DefaultColormap(d, 0), "#e0a43c", &c);
	XAllocColor(d, DefaultColormap(d, 0), &c);
	printf("amber #e0a43c is pixel %06lx\n", c.pixel);
	return c.pixel != 0xe0a43c;
}
