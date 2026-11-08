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
cp "$top/xc/programs/Xserver/Xfbdev" "$top/dwm/dwm" "$top/test/win" "$r/bin/"
# the desktop bits from torter's dwm, if they're installed
for b in wallset dwmstat; do
	p=$(command -v $b || true)
	[ -n "$p" ] && cp "$p" "$r/bin/"
done

# every shared library, at the path the loader looks for it
for b in "$r"/bin/*; do
	ldd "$b" 2>/dev/null | grep -o '/[^ ]*' || true
done | sort -u | while read -r lib; do
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

(cd "$r" && find . | cpio -o -H newc --quiet | gzip -1) > "$out"
echo "$out: $(du -h "$out" | cut -f1)"
