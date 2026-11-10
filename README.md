X22
===

Our X server, for Chamesle Linux. A fork of XFree86 4.8.0, the last XFree86,
from December 2008, brought up to date one piece at a time. The goal is X that's
as smooth as Wayland without giving up a single X program.

The name: it started out as X8664, X for x86-64, the way XFree86 was free X for
the 386. It's X22 now.

Where it's at
-------------

Early, but it draws on real screens now. Two servers build:

  Xvfb     draws into memory, for testing. dwm runs on it.
  Xfbdev   draws on a real screen through the kernel (kms, /dev/dri/card0).
           Tested in qemu so far, not on real hardware yet. Mouse and
           keyboard come from the old Linux console drivers for now.

The plan, and the news as it happens: https://x22.chamesle.org

What's in here
--------------

  xc/      XFree86 4.8.0. The first commit is the release exactly as it came
           (src-1 to src-4, checked against XFree86's sha256 sums and signature).
           Everything after that is ours, and git log says what and why.
  dwm/     dwm 6.8 from suckless with our config, the first window manager we
           run on it.
  test/    tiny X programs for poking at the server. win opens a window,
           masks checks the colors come out right. test/vm/run.sh boots your
           kernel in qemu with Xfbdev and dwm and saves a picture of the screen.

Building
--------

You need gcc, make, the X11 headers, freetype and zlib.

  cd xc
  make World BOOTSTRAPCFLAGS="-std=gnu89 -fcommon -w" CC=gcc

xc/config/cf/host.def decides what gets built. It's Xvfb and Xfbdev for now,
with the 2008 C allowed through a 2026 compiler. That takes a few minutes. The
servers end up in xc/programs/Xserver/.

Running it
----------

It needs a font for the cursor. Put cursor.bdf and a fixed font (6x13 is good)
in a folder, run mkfontdir there, then:

  xc/programs/Xserver/Xvfb :86 -ac -screen 0 1280x800x24 -fp /that/folder/ &
  DISPLAY=:86 dwm/dwm &
  DISPLAY=:86 test/win hello

-ac lets any local program connect, which is fine for testing and nothing else.

License
-------

XFree86's code keeps the licenses it came with, mostly the XFree86 License 1.1
and the MIT/X11 license. dwm is MIT/X. Our changes are under the same license as
the file they're in.

XFree86 is a trademark of The XFree86 Project, Inc. X22 is our own project and
isn't made or endorsed by them.
