/* sync: pokes at the SYNC extension where it used to go wrong. a trigger moved to
 * another counter by a request that then failed, a trigger taken out of the middle
 * of a counter's list, and a counter destroyed under a client that's waiting on it.
 * every alarm that should fire has to fire, and the server has to still be there
 * at the end. prints "sync: ok" if it all went right */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/extensions/sync.h>

static int evbase, errbase, lasterr;

static int onerror(Display *d, XErrorEvent *e)
{
	(void)d;
	lasterr = e->error_code;
	return 0;
}

static XSyncAlarm alarm_on(Display *d, XSyncCounter c, int at)
{
	XSyncAlarmAttributes a;

	a.trigger.counter = c;
	a.trigger.value_type = XSyncAbsolute;
	XSyncIntToValue(&a.trigger.wait_value, at);
	a.trigger.test_type = XSyncPositiveComparison;
	XSyncIntToValue(&a.delta, 0);
	a.events = True;
	return XSyncCreateAlarm(d, XSyncCACounter | XSyncCAValueType | XSyncCAValue |
	    XSyncCATestType | XSyncCADelta | XSyncCAEvents, &a);
}

static void set(Display *d, XSyncCounter c, int v)
{
	XSyncValue x;

	XSyncIntToValue(&x, v);
	XSyncSetCounter(d, c, x);
}

/* alarm events that come in within a moment */
static int fired(Display *d, XSyncAlarm want)
{
	int n = 0;
	XEvent e;

	XSync(d, False);
	usleep(200000);
	while (XPending(d)) {
		XNextEvent(d, &e);
		if (e.type == evbase + XSyncAlarmNotify &&
		    ((XSyncAlarmNotifyEvent *)&e)->alarm == want &&
		    ((XSyncAlarmNotifyEvent *)&e)->state != XSyncAlarmDestroyed)
			n++;
	}
	return n;
}

int main(void)
{
	Display *d = XOpenDisplay(NULL);
	int major, minor, bad = 0;
	XSyncValue zero;

	if (!d) { fputs("sync: no display\n", stderr); return 1; }
	if (!XSyncQueryExtension(d, &evbase, &errbase) || !XSyncInitialize(d, &major, &minor)) {
		fputs("sync: the server doesn't have it\n", stderr);
		return 1;
	}
	XSetErrorHandler(onerror);
	XSyncIntToValue(&zero, 0);

	/* 1. move an alarm to another counter with a bad test type. the request fails,
	 * and the alarm has to stay on its first counter, which still makes it fire */
	XSyncCounter c1 = XSyncCreateCounter(d, zero), c2 = XSyncCreateCounter(d, zero);
	XSyncAlarm a1 = alarm_on(d, c1, 1);
	XSyncAlarmAttributes ch;
	ch.trigger.counter = c2;
	ch.trigger.test_type = 99;
	lasterr = 0;
	XSyncChangeAlarm(d, a1, XSyncCACounter | XSyncCATestType, &ch);
	XSync(d, False);
	if (lasterr != BadValue) { printf("sync: a bad test type gave error %d, not BadValue\n", lasterr); bad++; }
	XSyncDestroyCounter(d, c2);	/* the old code had the alarm pointing here */
	set(d, c1, 5);
	if (fired(d, a1) != 1) { puts("sync: the alarm left its counter after a failed change"); bad++; }
	XSyncDestroyAlarm(d, a1);

	/* 2. three alarms on one counter, the middle one goes. the other two still fire */
	XSyncCounter c3 = XSyncCreateCounter(d, zero);
	XSyncAlarm x = alarm_on(d, c3, 1), y = alarm_on(d, c3, 1), z = alarm_on(d, c3, 1);
	XSyncDestroyAlarm(d, y);
	set(d, c3, 1);
	XSync(d, False);
	usleep(200000);
	int fx = 0, fz = 0;
	XEvent e;
	while (XPending(d)) {
		XNextEvent(d, &e);
		if (e.type != evbase + XSyncAlarmNotify)
			continue;
		XSyncAlarmNotifyEvent *an = (XSyncAlarmNotifyEvent *)&e;
		/* with a delta of 0 an alarm that fires goes inactive, and says so */
		if (an->alarm == x && an->state != XSyncAlarmDestroyed) fx++;
		if (an->alarm == z && an->state != XSyncAlarmDestroyed) fz++;
	}
	if (fx != 1 || fz != 1) { printf("sync: after taking one out, the others fired %d and %d times\n", fx, fz); bad++; }
	XSyncDestroyAlarm(d, x);
	XSyncDestroyAlarm(d, z);
	XSyncDestroyCounter(d, c3);

	/* 3. a second client waits on two counters at once, and this one destroys both
	 * while it waits. the wait has to end and the server has to survive it */
	XSyncCounter w1 = XSyncCreateCounter(d, zero), w2 = XSyncCreateCounter(d, zero);
	XSync(d, False);
	pid_t pid = fork();
	if (pid == 0) {
		Display *d2 = XOpenDisplay(NULL);
		XSyncWaitCondition wc[2];
		int i;
		if (!d2) _exit(2);
		XSetErrorHandler(onerror);
		for (i = 0; i < 2; i++) {
			wc[i].trigger.counter = i ? w2 : w1;
			wc[i].trigger.value_type = XSyncAbsolute;
			XSyncIntToValue(&wc[i].trigger.wait_value, 100);
			wc[i].trigger.test_type = XSyncPositiveComparison;
			XSyncIntToValue(&wc[i].event_threshold, 0);
		}
		XSyncAwait(d2, wc, 2);
		XSync(d2, False);	/* blocks until the wait is over */
		_exit(0);
	}
	usleep(300000);
	XSyncDestroyCounter(d, w1);
	XSyncDestroyCounter(d, w2);
	XSync(d, False);
	int status = 1, waited;
	for (waited = 0; waited < 30; waited++) {
		if (waitpid(pid, &status, WNOHANG) == pid)
			break;
		usleep(100000);
	}
	if (waited == 30) { puts("sync: the waiting client never woke up"); kill(pid, 9); bad++; }
	else if (status) { printf("sync: the waiting client ended with %d\n", status); bad++; }

	/* and the server's still answering */
	XSyncCounter last = XSyncCreateCounter(d, zero);
	XSyncValue v;
	if (!XSyncQueryCounter(d, last, &v)) { puts("sync: the server stopped answering"); bad++; }

	if (bad)
		return 1;
	puts("sync: ok");
	XCloseDisplay(d);
	return 0;
}
