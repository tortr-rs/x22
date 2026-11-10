X8664 and the X security fixes since 2008
=========================================

XFree86 4.8 came out in 2008. X.Org has fixed a lot of security bugs in the X
server since then, and most of the code they were in is code we have too. This
is the list: every X.Org advisory about the server since January 2008, and what
we did about each CVE in it.

  fixed      ported here, by hand (the code has moved on too far for the patches
             to apply), and tested in the vm
  had it     XFree86 4.8 already had a fix of its own
  not ours   the code isn't in X8664: XInput 2, DRI2/3, glamor, GLX, Xwayland,
             Xquartz, the setuid Xorg server, and so on
  to do      not looked at yet, or not done yet

The list of advisories is https://www.x.org/wiki/Development/Security/ and the
fixes are in X.Org's git history, which says which CVE each one is for.

One fix covers a lot of them at once: every request handler for clients with
the other byte order now checks the request is long enough before it swaps
anything in it. That was 382 handlers in 38 files.


2007-12 / 2008-01  (Jan 17 2008)
  CVE-2007-5760  XFree86-Misc out of bounds index               fixed (and the same off-by-one in TOG-CUP and DGA)
  CVE-2007-5958  security policy file existence                 had it (Fopen)
  CVE-2007-6427  XInput memory corruption                       had it
  CVE-2007-6428  TOG-CUP memory corruption                      fixed
  CVE-2007-6429  MIT-SHM and EVI integer overflows              had it
  CVE-2008-0006  PCF font parser overflow                       fixed (every glyph has to fit in the bitmap data)

2008-06  (Jun 11 2008)
  CVE-2008-1377  RECORD and SECURITY memory corruption          had it, and big requests fixed
  CVE-2008-1379  MIT-SHM arbitrary memory read                  had it
  CVE-2008-2360  RENDER AllocateGlyph overflow                  had it
  CVE-2008-2361  RENDER CreateCursor overflow                   had it
  CVE-2008-2362  RENDER gradients                               not ours (no gradients in 4.8's RENDER)

2011-10
  CVE-2011-4028  lock file disclosure                           fixed
  CVE-2011-4029  lock file chmod race                           had it (fchmod)

2013
  CVE-2013-1940          input flushed on VT switch (Xorg evdev) not ours
  CVE-2013-4396  ImageText use after free                       fixed
  CVE-2013-6462  libXfont BDF                                   fixed

2014-12  (Dec 9 2014)
  CVE-2014-8091  RPC auth unchecked malloc                      fixed
  CVE-2014-8092  dix integer overflows                          fixed (REQUEST_FIXED_SIZE, GetHosts,
                                                               PutImage, region sizes)
  CVE-2014-8093  GLX                                            not ours (GLX isn't built)
  CVE-2014-8094  DRI2                                           not ours
  CVE-2014-8095  XInput swapped lengths                         fixed (the swapped handlers)
  CVE-2014-8096  XC-MISC swapped length                         fixed (the swapped handlers)
  CVE-2014-8097  DBE lengths                                    fixed
  CVE-2014-8098  GLX                                            not ours
  CVE-2014-8099  XVideo swapped lengths                         fixed (the swapped handlers)
  CVE-2014-8100  RENDER lengths                                 fixed
  CVE-2014-8101  RandR swapped lengths                          fixed (the swapped handlers)
  CVE-2014-8102  XFIXES swapped length                          fixed (the swapped handlers)
  CVE-2014-8103  DRI3 and Present                               not ours (DRI3), and our Present is our own code: it checks every length and looks windows up by id

2015
  CVE-2015-0255  XKB GetGeometry information leak               fixed (the counted strings in SetGeometry)
  CVE-2015-1802..1804    libXfont BDF                           fixed
  CVE-2015-3164  Xwayland open sockets                          not ours

2017
  CVE-2017-2624  MIT-MAGIC-COOKIE timing                        fixed
  CVE-2017-12176 EstablishConnection length                     fixed
  CVE-2017-12177 DBE GetVisualInfo length                       fixed
  CVE-2017-12178 XI2 ChangeHierarchy                            not ours
  CVE-2017-12179 XI2 barriers                                   not ours
  CVE-2017-12180..12182  hw/xfree86 extensions                  not ours
  CVE-2017-12183 XFIXES lengths                                 fixed
  CVE-2017-12184..12187  lengths in Xinerama, DGA, etc.         fixed (Xinerama, saver, Xv; the rest is hw/xfree86)
  CVE-2017-13721 MIT-SHM resource id                            had it (ShmAttach checks the id, 4.8 has no CreateSegment)
  CVE-2017-13723 XKB text buffers                               fixed

2018
  CVE-2018-14665 -logfile/-modulepath on setuid Xorg            not ours (nothing here is setuid)

2020
  CVE-2020-14345 XkbSetNames bounds                             fixed
  CVE-2020-14346 XIChangeHierarchy                              not ours
  CVE-2020-14347 uninitialised pixmap memory                    fixed (new pixmaps are zeroed)
  CVE-2020-14360 XkbSetMap lengths                              fixed (every step is checked)
  CVE-2020-14361 XkbSelectEvents underflow                      fixed
  CVE-2020-14362 RECORD RegisterClients underflow               fixed
  CVE-2020-25712 XkbSetDeviceInfo overflows                     fixed

2021
  CVE-2021-3472  XChangeFeedbackControl underflow               fixed
  CVE-2021-4008  RENDER CompositeGlyphs swapped                 fixed
  CVE-2021-4009  XFIXES pointer barriers                        not ours
  CVE-2021-4010  SCREEN-SAVER Suspend swapped                   not ours (4.8 has no Suspend request)
  CVE-2021-4011  RECORD CreateContext swapped                   fixed

2022
  CVE-2022-2319  XkbSetGeometry lengths                         fixed
  CVE-2022-2320  XkbSetDeviceInfo                               fixed (checked before it's applied, always was)
  CVE-2022-4283  XKB radio groups in _XkbCopyNames              not ours (no _XkbCopyNames)
  CVE-2022-46340 XTEST GenericEvent                             not ours (no GenericEvent before XI2)
  CVE-2022-46341 XI passive grab detail                         not ours (XI2)
  CVE-2022-46342 Xv RTVideoNotify                               fixed
  CVE-2022-46343 SCREEN-SAVER resource                          fixed
  CVE-2022-46344 XI2 ChangeProperty                             not ours

2023
  CVE-2023-0494  XI DeepCopyPointerClasses                      not ours (XI2)
  CVE-2023-1393  Composite overlay window use after free      not ours (Composite 0.2 has no overlay window)
  CVE-2023-5367  XI2 and RandR 1.2 properties                   not ours
  CVE-2023-5380  PointerWindows on screen switch                not ours (XI2 enter/leave code)
  CVE-2023-6377  XI XkbActions for buttons                      not ours (master devices)
  CVE-2023-6478  RandR 1.2 properties                           not ours
  CVE-2023-6816  logical button maps                            not ours (XI2)
  CVE-2024-0229  XI ButtonClass                                 fixed (the DeviceStateNotify part; the rest is master devices)
  CVE-2024-0408  GLX                                            not ours
  CVE-2024-0409  Xephyr/Xwayland cursor                         not ours
  CVE-2024-21885 XI2 hierarchy events                           not ours
  CVE-2024-21886 disabling a master device                      not ours

2024
  CVE-2024-31080..31082  XI2 / Xquartz reply lengths            not ours
  CVE-2024-31083 RENDER AddGlyphs refcount                      not ours (4.8 adds each glyph whole, with its refcount)
  CVE-2024-9632  XkbSetCompatMap                                not ours (4.8 reallocs every time)

2025
  CVE-2025-26594 root cursor                                    fixed
  CVE-2025-26595 XkbVModMaskText                                fixed
  CVE-2025-26596 XkbSizeKeySyms                                 fixed
  CVE-2025-26597 XkbChangeTypesOfKey                            fixed
  CVE-2025-26598 XI2 barriers                                   not ours
  CVE-2025-26599 Composite border clip                          fixed (4.8 left the window unmapped instead)
  CVE-2025-26600 frozen device removal                          not ours (nothing removes a device while running)
  CVE-2025-26601 SYNC alarms                                    fixed
  CVE-2025-49175 RENDER animated cursors                        fixed
  CVE-2025-49176 big request length overflow                    fixed
  CVE-2025-49177 XFIXES SetClientDisconnectMode                 not ours (XFIXES 3 has no such request)
  CVE-2025-49178 input buffer ignoreBytes                       not ours (4.8 doesn't skip requests)
  CVE-2025-49179 RECORD RegisterClients overflow                fixed
  CVE-2025-49180 RandR providers in Xorg                        not ours
  CVE-2025-62229 Present notifies                               not ours (our own Present, see 2014-8103)
  CVE-2025-62230 XKB interest resource                          fixed
  CVE-2025-62231 XkbSetCompatMap overflow                       fixed

2026
  CVE-2026-33999 XkbSetCompatMap buffer reuse                   not ours (4.8 reallocs every time)
  CVE-2026-34000 XkbSetGeometry colour index                    fixed
  CVE-2026-34001 SYNC fences                                    not ours (no fences in SYNC 3.0)
  CVE-2026-34002 XkbSetMap modifier map                         fixed
  CVE-2026-34003 XkbSetMap key types                            fixed
  CVE-2026-50256 font alias names                               fixed
  CVE-2026-50257 SYNC fences                                    not ours
  CVE-2026-50258 XKB key type levels                            fixed (with CVE-2026-34003)
  CVE-2026-50259 XkbSetMap key types                            fixed (with CVE-2026-34003)
  CVE-2026-50260 SYNC FreeCounter                               fixed
  CVE-2026-50261 SYNC SyncChangeCounter                         fixed
  CVE-2026-50262 GLX                                            not ours
  CVE-2026-50263 screen saver window, and DRI2                  to do (DRI2 isn't ours; for the saver we
                                                                haven't found which path it is yet)
  CVE-2026-55999 glamor fonts                                   not ours
  CVE-2026-56000 GLX                                            not ours
  CVE-2026-56001 bitmap scaler size                             fixed
  CVE-2026-56002 PCF glyph data                                 fixed
  CVE-2026-56003 scaled font properties                         fixed
  CVE-2026-59679, CVE-2026-44950  libXfont2 font server   not ours (font servers are off, see below)
  CVE-2026-88812 XkbSetGeometry doodad text                     fixed (no text, no doodad)
  CVE-2026-93515 Present notifies                               not ours (our own Present)
  CVE-2026-93516 passive grabs on removed devices               not ours (nothing removes a device while running)
  CVE-2026-93517 GLX                                            not ours
  CVE-2026-93518 XKB map sizes 16 bits                          fixed
  CVE-2026-93519 XI2 barriers                                   not ours
  CVE-2026-93520 XKB key names array                            fixed
  CVE-2026-93521 RandR provider properties                      not ours
  CVE-2026-93522 glamor                                         not ours
  CVE-2026-93523 XI2 passive ungrab                             not ours
  CVE-2026-93524 XkbSetMap key widths                           fixed
  CVE-2026-93536 XI2 gestures                                   not ours

Most of the length bugs had a second half: lengths worked out from the
request's own length field, which is 0 in a big request. All of those use the
real length now (client->req_len), in XInput, XKB, SHAPE, XTEST, the screen
saver, Multi-Buffering and SECURITY.

Also fixed on the way, found while porting:
  - XkbSetGeometry never checked a shape's primary and approx outline numbers
  - XkbVModIndexText wrote one byte past its buffer for C output (X.Org has this too)
  - XkbAddGeomOverlay and its rows weren't checked for running out of memory
  - the swapped DBE SwapBuffers only swapped the first window
  - the screen saver used its screen data after it had been freed, in two
    places (X.Org fixed one of them in 2026)
  - RENDER AddGlyphs leaked a glyph on a short request
  - SYNC: an alarm whose new values were refused left its trigger half
    changed (X.Org's CVE-2025-26601 fix covers this too)


The font library
----------------

The server has the font library built in (xc/lib/font), so the libXfont
advisories are ours too, the ones above and these:

  CVE-2011-2895  compressed (.Z) fonts                          fixed
  CVE-2014-0209  fonts.dir and fonts.alias sizes                fixed
  CVE-2014-0210, CVE-2014-0211  font server replies          not ours (font servers are off)
  CVE-2017-13720 font name patterns                             fixed
  CVE-2017-13722 PCF property names                             fixed
  CVE-2017-16611 symlinks in font directories                   fixed (fonts are opened with O_NOFOLLOW)

Some of it is turned off rather than fixed, the way X.Org did: the font server
client (xfs, CVE-2014-0210, 0211, CVE-2026-59679, 44950), the Speedo, Type1 and
CID renderers, and the SNF and PMF bitmap formats. Fonts come from PCF and BDF
files and FreeType now. test/fonts opens a bad BDF or two, a font that's a
symlink, and a scaled bitmap font, and checks the server is still there after.

Found here
----------

Bugs that were in XFree86 4.8 and that we found ourselves, while testing the
fixes above. As far as we know X.Org fixed or rewrote these code paths long ago,
so there's no CVE for them.

  SYNC ChangeAlarm with a bad test type: the error went back to the client, but
    the bad test type stayed in the alarm. With a delta of 0 it never went
    inactive when it fired, and the server added 0 to it forever: any client
    could hang the server. The whole request is checked now before any of it
    goes into the alarm, and a later failure puts the alarm back how it was.
  SYNC CreateAlarm when AddResource fails: AddResource frees the alarm itself,
    and CreateAlarm freed it a second time. Fixed.
  SYNC CreateAlarm failing after the alarm's trigger was put on its counter: the
    alarm was freed and the counter kept pointing at it. It comes off the
    counter first now.

test/sync goes through all of these, and the SYNC fixes above.
