#!/bin/sh
# boots the x22 test vm, waits, takes a picture of the screen and stops it.
# usage: run.sh [SECONDS] [SHOT.png]     APPEND="x22.test=dpms" adds to the kernel command line
# the console log ends up next to the picture, as SHOT.log
set -eu
here=$(cd "$(dirname "$0")" && pwd)
wait=${1:-90}
shot=${2:-$here/shot.png}
kernel=${KERNEL:-/boot/vmlinuz-$(uname -r)}
img=$here/initramfs.cpio.gz
sock=$(mktemp -u /tmp/x22-vm.XXXXXX)

[ -f "$img" ] || "$here/mkinitramfs.sh" "$img"
accel=tcg
[ -w /dev/kvm ] && accel=kvm

qemu-system-x86_64 -machine accel=$accel -m 1024 -smp 2 \
	-kernel "$kernel" -initrd "$img" \
	-append "console=ttyS0 quiet loglevel=3 rdinit=/init ${APPEND:-}" \
	-vga std -display none -serial file:"${shot%.png}.log" \
	-monitor unix:"$sock",server,nowait -no-reboot &
q=$!
trap 'kill $q 2>/dev/null; rm -f "$sock"' EXIT

sleep "$wait"
python3 - "$sock" "$shot" <<'PY'
import socket, sys, time
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); s.settimeout(2)
s.sendall(("screendump %s -f png\n" % sys.argv[2]).encode()); time.sleep(2)
PY
echo "$shot"
