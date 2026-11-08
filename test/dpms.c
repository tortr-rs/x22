/* dpms: asks the server to switch the monitor off, waits, and on again.
 * dpms [SECONDS]   the server should really cut the signal, not just draw black */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>

int main(int argc, char **argv)
{
	Display *d = XOpenDisplay(NULL);
	int ev, err;
	if (!d) { fputs("dpms: no display\n", stderr); return 1; }
	if (!DPMSQueryExtension(d, &ev, &err) || !DPMSCapable(d)) {
		fputs("dpms: the server can't switch the monitor off\n", stderr);
		return 1;
	}
	DPMSEnable(d);
	DPMSForceLevel(d, DPMSModeOff);
	XSync(d, False);
	fputs("dpms: monitor off\n", stderr);
	sleep(argc > 1 ? atoi(argv[1]) : 10);
	DPMSForceLevel(d, DPMSModeOn);
	XSync(d, False);
	fputs("dpms: monitor on\n", stderr);
	return 0;
}
