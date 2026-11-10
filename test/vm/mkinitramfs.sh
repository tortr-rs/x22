#!/bin/sh
# packs the test vm: init, Xfbdev, dwm and the little test programs, every library they load,
# two fonts for dwm, the cursor font, the wallpaper, and the kernel's graphics driver.
# usage: mkinitramfs.sh OUT.cpio.gz   (run from anywhere, finds the rest itself)
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/../.." && pwd)
out=${1:-$here/initramfs.cpio.gz}
kver=${KVER:-$(uname -r)}
wall=${WALL:-$HOME/Pictures/wallpapers/teal.jpg}
r=$(mktemp -d)
trap 'rm -rf "$r"' EXIT

mkdir -p "$r"/bin "$r"/proc "$r"/sys "$r"/dev "$r"/tmp "$r"/root "$r"/modules "$r"/fonts "$r"/etc/fonts "$r"/xfonts
cp "$here/init" "$r/init"
cp "$top/xc/programs/Xserver/Xfbdev" "$top/dwm/dwm" "$top/test/win" "$top/test/dpms" "$top/test/present" "$top/test/composite" "$r/bin/"
# a compositing manager for x22.test=composite: XCOMPMGR=/path/to/xcompmgr, or one built in ~/src
for x in "${XCOMPMGR:-$HOME/src/xcompmgr/xcompmgr-1.1.10/xcompmgr}"; do [ -x "$x" ] && cp "$x" "$r/bin/xcompmgr"; done
# the desktop bits from torter's dwm, if they're installed
for b in wallset dwmstat; do
	p=$(command -v $b || true)
	[ -n "$p" ] && cp "$p" "$r/bin/"
done

# udev, so libinput can find the mice and touchpads: udevd is udevadm under another name
cp -L "$(command -v udevadm)" "$r/bin/udevadm"
ln -s udevadm "$r/bin/systemd-udevd"
mkdir -p "$r/usr/lib/udev/rules.d" "$r/etc/udev" "$r/run"
for u in 50-udev-default 60-input-id 60-persistent-input 80-libinput-device-groups 90-libinput-fuzz-override; do
	cp /usr/lib/udev/rules.d/$u.rules "$r/usr/lib/udev/rules.d/"
done
cp /usr/lib/udev/libinput-* "$r/usr/lib/udev/"
mkdir -p "$r/usr/share"
cp -r /usr/share/libinput "$r/usr/share/libinput"
[ -f /etc/udev/hwdb.bin ] && cp /etc/udev/hwdb.bin "$r/etc/udev/"
for b in /usr/lib/udev/libinput-*; do ldd "$b" 2>/dev/null | grep -o '/[^ ]*' >> "$r/tmp/extralibs" || true; done

# every shared library, at the path the loader looks for it
for b in "$r"/bin/*; do
	ldd "$b" 2>/dev/null | grep -o '/[^ ]*' || true
done | cat - "$r/tmp/extralibs" | sort -u | while read -r lib; do
	mkdir -p "$r$(dirname "$lib")"
	cp -L "$lib" "$r$lib"
done
# glibc loads these by name at run time, ldd doesn't see them
for lib in /usr/lib64/libnss_files.so.2 /lib64/libnss_files.so.2; do
	[ -f "$lib" ] && { mkdir -p "$r$(dirname "$lib")"; cp -L "$lib" "$r$lib"; }
done

# fonts: dwm's two, through fontconfig, and the X cursor font for Xfbdev
cp -L "$(fc-match -f '%{file}' 'Liberation Mono')" "$(fc-match -f '%{file}' 'Symbols Nerd Font Mono')" "$r/xfonts/"
cat > "$r/etc/fonts/fonts.conf" <<'CONF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>/xfonts</dir>
  <cachedir>/tmp/fontconfig</cachedir>
</fontconfig>
CONF
cp "$top"/run/fonts/* "$r/fonts/"
[ -f "$wall" ] && cp "$wall" "$r/wall.jpg"

# the graphics card qemu gives us (-vga std) is bochs. its driver and what it needs, in order
for m in drm_kms_helper drm_shmem_helper drm_client_lib bochs; do
	f=$(modinfo -k "$kver" -F filename "$m")
	case "$f" in
		*.zst) zstd -dq "$f" -o "$r/modules/$m.ko" ;;
		*.xz) xz -dc "$f" > "$r/modules/$m.ko" ;;
		*.gz) gzip -dc "$f" > "$r/modules/$m.ko" ;;
		*) cp "$f" "$r/modules/$m.ko" ;;
	esac
	echo "$m.ko" >> "$r/modules/order"
done

rm -f "$r/tmp/extralibs"
(cd "$r" && find . | cpio -o -H newc --quiet | gzip -1) > "$out"
echo "$out: $(du -h "$out" | cut -f1)"
