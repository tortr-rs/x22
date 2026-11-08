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
	signal(SIGCHLD, SIG_IGN);   /* no zombies, we don't wait for anyone */

	say("hello");
	loadmodules();
	if (!waitfor("/dev/dri/card0", 100)) { say("no /dev/dri/card0, the graphics driver didn't come up"); goto idle; }
	say("/dev/dri/card0 is there");

	setenv("PATH", "/bin", 1);
	setenv("HOME", "/root", 1);
	setenv("DISPLAY", ":0", 1);
	setenv("FONTCONFIG_FILE", "/etc/fonts/fonts.conf", 1);
	setenv("XDG_CONFIG_HOME", "/tmp", 1);
	setenv("X8664_EVDEV_DEBUG", "1", 1);

	char *x[] = { "/bin/Xfbdev", ":0", "-ac", "-fp", "/fonts/", "vt1", NULL };
	run(x);
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

idle:
	for (;;) pause();
}
