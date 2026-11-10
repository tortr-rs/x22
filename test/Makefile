# little programs for poking at x8664
all: win masks dpms present composite sync
win: win.c
	cc -O2 -o $@ win.c -lX11
masks: masks.c
	cc -O2 -o $@ masks.c -lX11
clean:
	rm -f win masks
dpms: dpms.c
	cc -O2 -o $@ dpms.c -lX11 -lXext
present: present.c
	cc -O2 -o $@ present.c -lxcb -lxcb-present
composite: composite.c
	cc -O2 -o $@ composite.c -lxcb -lxcb-composite -lxcb-damage -lxcb-xfixes -lxcb-render
sync: sync.c
	cc -O2 -o $@ sync.c -lX11 -lXext
