/* init: the first and only program in the x8664 test vm. there's no shell in there, so this
 * mounts what X needs, loads the graphics driver, starts Xfbdev and puts dwm and a few windows
 * on it. everything it does goes to the serial console, which run.sh saves. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static void say(const char *m) { fprintf(stderr, "x8664-init: %s\n", m); }

static pid_t xpid;

/* children that end: say how, the X server most of all */
static void reap(int sig)
{
	int st;
	pid_t p;
	while ((p = waitpid(-1, &st, WNOHANG)) > 0) {
		if (p != xpid && WIFEXITED(st) && !WEXITSTATUS(st))
			continue;
		fprintf(stderr, "x8664-init: %s %d %s %d\n", p == xpid ? "Xfbdev" : "pid", (int) p,
			WIFSIGNALED(st) ? "killed by signal" : "exited with", WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st));
	}
}

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }

/* wait up to tenths/10 seconds for a file to show up */
static int waitfor(const char *p, int tenths)
{
	while (tenths-- > 0) {
		if (exists(p)) return 1;
		usleep(100000);
	}
	return exists(p);
}

static pid_t run(char *const argv[])
{
	pid_t pid = fork();
	if (pid == 0) {
		execv(argv[0], argv);
		fprintf(stderr, "x8664-init: can't run %s: %s\n", argv[0], strerror(errno));
		_exit(127);
	}
	return pid;
}

static void loadmodules(void)
{
	FILE *f = fopen("/modules/order", "r");
	char name[256];
	if (!f) return;
	while (fgets(name, sizeof name, f)) {
		char path[300];
		name[strcspn(name, "\n")] = 0;
		snprintf(path, sizeof path, "/modules/%s", name);
		int fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd < 0 || syscall(SYS_finit_module, fd, "", 0) != 0)
			fprintf(stderr, "x8664-init: module %s: %s\n", name, strerror(errno));
		else
			fprintf(stderr, "x8664-init: loaded %s\n", name);
		if (fd >= 0) close(fd);
	}
	fclose(f);
}

int main(void)
{
	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	mount("tmpfs", "/tmp", "tmpfs", 0, NULL);
	mkdir("/tmp/.X11-unix", 01777);
	chmod("/tmp/.X11-unix", 01777);

	int con = open("/dev/console", O_RDWR);
	if (con >= 0) { dup2(con, 1); dup2(con, 2); }
	signal(SIGCHLD, reap);

	say("hello");
	loadmodules();
	if (!waitfor("/dev/dri/card0", 100)) { say("no /dev/dri/card0, the graphics driver didn't come up"); goto idle; }
	say("/dev/dri/card0 is there");

	/* udev first: libinput asks it what each input device is */
	mkdir("/run", 0755);
	mount("tmpfs", "/run", "tmpfs", 0, NULL);
	char *udevd[] = { "/bin/systemd-udevd", "--daemon", NULL };
	run(udevd);
	if (waitfor("/run/udev/control", 50)) {
		char *trig[] = { "/bin/udevadm", "trigger", "--action=add", "--subsystem-match=input", NULL };
		char *settle[] = { "/bin/udevadm", "settle", "--timeout=10", NULL };
		signal(SIGCHLD, SIG_DFL);
		waitpid(run(trig), NULL, 0);
		waitpid(run(settle), NULL, 0);
		signal(SIGCHLD, reap);
		say("udev is up");
	} else
		say("udevd didn't start, the old mouse driver it is");

	setenv("PATH", "/bin", 1);
	setenv("HOME", "/root", 1);
	setenv("DISPLAY", ":0", 1);
	setenv("FONTCONFIG_FILE", "/etc/fonts/fonts.conf", 1);
	setenv("XDG_CONFIG_HOME", "/tmp", 1);
	setenv("X8664_EVDEV_DEBUG", "1", 1);
	setenv("X8664_KEYMAP", "gb", 1);
	setenv("X8664_LIBINPUT_DEBUG", "1", 1);

	char *x[] = { "/bin/Xfbdev", ":0", "-ac", "-fp", "/fonts/", "vt1", NULL };
	xpid = run(x);
	if (!waitfor("/tmp/.X11-unix/X0", 300)) { say("Xfbdev didn't start"); goto idle; }
	say("Xfbdev is up");
	sleep(1);

	char *wall[] = { "/bin/wallset", "/wall.jpg", "fill", NULL };
	run(wall);
	sleep(3);
	char *dwm[] = { "/bin/dwm", NULL };
	run(dwm);
	sleep(2);
	char *stat[] = { "/bin/dwmstat", NULL };
	run(stat);
	char *w1[] = { "/bin/win", "kitty", "3600", NULL };
	char *w2[] = { "/bin/win", "firefox", "3600", NULL };
	char *w3[] = { "/bin/win", "notes", "3600", NULL };
	run(w1); run(w2); run(w3);
	say("dwm and three windows started");

	/* extra tests, asked for on the kernel command line: x8664.test=dpms, present or composite */
	{
		char cmd[512] = "";
		int fd = open("/proc/cmdline", O_RDONLY);
		if (fd >= 0) { if (read(fd, cmd, sizeof cmd - 1) < 0) cmd[0] = 0; close(fd); }
		if (strstr(cmd, "x8664.test=composite")) {
			char *ct[] = { "/bin/composite", NULL };
			char *xc[] = { "/bin/xcompmgr", "-c", "-r", "14", "-o", "0.55", "-l", "-16", "-t", "-12", NULL };
			sleep(10);
			say("composite test");
			run(ct);
			sleep(3);
			say("xcompmgr, with shadows");
			run(xc);
		}
		if (strstr(cmd, "x8664.test=present")) {
			char *pr[] = { "/bin/present", NULL };
			sleep(10);
			say("present test");
			run(pr);
		}
		if (strstr(cmd, "x8664.test=dpms")) {
			char *dp[] = { "/bin/dpms", "20", NULL };
			sleep(10);
			say("dpms test");
			run(dp);
		}
	}

idle:
	for (;;) pause();
}
