/* fonts: the font library with good fonts and bad ones. the good ones have to open,
 * also scaled to a size nobody drew, and the bad ones have to be turned down without
 * taking the server with them: a bdf whose glyph box doesn't fit in a short, a bdf
 * that claims a billion properties, and a font that's a symlink to a file the server
 * shouldn't read for a client. prints "fonts: ok" if it all went right */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <X11/Xlib.h>

static int onerror(Display *d, XErrorEvent *e) { (void)d; (void)e; return 0; }

static void put(const char *dir, const char *name, const char *text)
{
	char p[PATH_MAX];
	FILE *f;

	snprintf(p, sizeof p, "%s/%s", dir, name);
	if (!(f = fopen(p, "w"))) { perror(p); exit(1); }
	fputs(text, f);
	fclose(f);
}

static const char *glyph =
	"STARTCHAR A\nENCODING 65\nSWIDTH 500 0\nDWIDTH 6 0\n"
	"BBX %s\nBITMAP\n%sENDCHAR\n";

static void bdf(const char *dir, const char *name, const char *props, const char *bbx, const char *bits)
{
	char text[4096], g[1024];

	snprintf(g, sizeof g, glyph, bbx, bits);
	snprintf(text, sizeof text,
		"STARTFONT 2.1\nFONT %s\nSIZE 13 75 75\nFONTBOUNDINGBOX 6 13 0 -2\n"
		"STARTPROPERTIES %s\nFONT_ASCENT 11\nFONT_DESCENT 2\nENDPROPERTIES\n"
		"CHARS 1\n%sENDFONT\n", name, props, g);
	snprintf(g, sizeof g, "%s.bdf", name);
	put(dir, g, text);
}

int main(int argc, char **argv)
{
	Display *d = XOpenDisplay(NULL);
	char tmp[] = "/tmp/x22-fonts-XXXXXX", **list, **old, *path[2];
	int n, nold, bad = 0;
	XFontStruct *f;

	if (!d) { fputs("fonts: no display\n", stderr); return 1; }
	XSetErrorHandler(onerror);

	/* the fonts the server came up with */
	list = XListFonts(d, "*", 1000, &n);
	if (n < 3) { printf("fonts: only %d fonts listed\n", n); bad++; }
	XFreeFontNames(list);
	if (!(f = XLoadQueryFont(d, "fixed"))) { puts("fonts: fixed won't open"); bad++; }
	else XFreeFont(d, f);
	/* 40 pixels from the 13 pixel bitmap: the bitmap scaler makes this one */
	f = XLoadQueryFont(d, "-misc-fixed-medium-r-semicondensed--40-*-*-*-*-*-iso10646-1");
	if (!f || f->ascent + f->descent < 30) { puts("fonts: the scaled font didn't come out"); bad++; }
	if (f) XFreeFont(d, f);

	/* a directory of bad fonts */
	if (!mkdtemp(tmp)) { perror("mkdtemp"); return 1; }
	bdf(tmp, "toowide", "2", "40000 13 0 -2", "00\n");
	bdf(tmp, "toomanyprops", "1000000000", "6 13 0 -2", "00\n");
	bdf(tmp, "oddline", "2", "6 1 0 0", "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
	    "fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
	    "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\n");
	put(tmp, "secret", "STARTFONT 2.1\nFONT secret\nSIZE 13 75 75\nFONTBOUNDINGBOX 6 13 0 -2\n"
	    "STARTPROPERTIES 0\nENDPROPERTIES\nCHARS 0\nENDFONT\n");
	{
		char from[PATH_MAX], to[PATH_MAX];
		snprintf(from, sizeof from, "%s/secret", tmp);
		snprintf(to, sizeof to, "%s/link.bdf", tmp);
		if (symlink(from, to)) perror("symlink");
	}
	put(tmp, "fonts.dir", "4\ntoowide.bdf toowide\ntoomanyprops.bdf toomanyprops\n"
	    "oddline.bdf oddline\nlink.bdf link\n");

	old = XGetFontPath(d, &nold);
	path[0] = tmp;
	path[1] = old[0];
	XSetFontPath(d, path, 2);
	XSync(d, False);
	{
		const char *names[] = { "toowide", "toomanyprops", "link" };
		int i;
		for (i = 0; i < 3; i++)
			if ((f = XLoadQueryFont(d, names[i]))) {
				printf("fonts: the bad font %s opened\n", names[i]);
				XFreeFont(d, f);
				bad++;
			}
	}
	/* this one is only odd, it can open or not, as long as nothing breaks */
	if ((f = XLoadQueryFont(d, "oddline")))
		XFreeFont(d, f);
	XSetFontPath(d, old, nold);
	XFreeFontPath(old);

	/* and the server's still answering */
	if (!(f = XLoadQueryFont(d, "fixed"))) { puts("fonts: the server stopped answering"); bad++; }
	else XFreeFont(d, f);

	{
		char cmd[PATH_MAX + 16];
		snprintf(cmd, sizeof cmd, "rm -rf %s", tmp);
		if (system(cmd)) fputs("fonts: couldn't clean up\n", stderr);
	}
	(void)argc; (void)argv;
	if (bad)
		return 1;
	puts("fonts: ok");
	XCloseDisplay(d);
	return 0;
}
