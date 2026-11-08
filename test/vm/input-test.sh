#!/bin/sh
# boots the test vm, waits for dwm, then presses super+j twice and moves the mouse,
# with a picture before, after the keys, and after the mouse. if the amber border moved and so did the pointer,
# keyboard and mouse both reach the server.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
kernel=${KERNEL:-/boot/vmlinuz-$(uname -r)}
img=$here/initramfs.cpio.gz
sock=$(mktemp -u /tmp/x8664-vm.XXXXXX)
[ -f "$img" ] || "$here/mkinitramfs.sh" "$img"
accel=tcg
[ -w /dev/kvm ] && accel=kvm
qemu-system-x86_64 -machine accel=$accel -m 1024 -smp 2 \
	-kernel "$kernel" -initrd "$img" \
	-append "console=ttyS0 quiet loglevel=3 rdinit=/init" \
	-vga std -display none -serial file:"$here/input.log" \
	-monitor unix:"$sock",server,nowait -no-reboot &
q=$!
trap 'kill $q 2>/dev/null; rm -f "$sock"' EXIT
sleep ${WAIT:-140}
python3 - "$sock" "$here" <<'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(2)
def cmd(c, wait=0.5):
    s.sendall((c + "\n").encode()); time.sleep(wait)
    try: s.recv(65536)
    except socket.timeout: pass
cmd("screendump %s/input-before.png -f png" % sys.argv[2], 2)
for k in __import__("os").environ.get("FIRST", "").split():
    cmd("sendkey " + k, 2)
for _ in range(int(__import__("os").environ.get("PRESSES", "2"))): cmd("sendkey meta_l-j", 3)
cmd("screendump %s/input-keys.png -f png" % sys.argv[2], 2)
for _ in range(10): cmd("mouse_move 40 30", 0.3)
time.sleep(4)
cmd("screendump %s/input-after.png -f png" % sys.argv[2], 2)
PY
