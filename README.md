Fusilli
======

OpenGL Compositing Window Manager

High in carbohydrates.

A fork of Compiz (0.8).

Changes to Compiz
======
Option system:
* libcompizconfig, bcop and plugins ini, ccp, fuse, gconf, kconfig were removed.
* Options are now handled in core. 
* XML metadata parsing was written from scratch.
* Options are stored in a single .xml file (~/.config/fusilli/banana.xml)
* CCSM was replaced with FSM (Fusilli Settings Manager)

Decorator:
* gtk-window-decorator is now fusilli-decorator-gtk
* Changed to support marco/GSettings, not metacity/gconf
* The decorator for KDE3 was removed.

Other:
* Currently there is no dbus support.
* Currently there is no edge binding support.

Codebase: 
* libdecoration changed to libfusillidecoration
* All names of executables/libs were changed to allow coexistence with compiz on the same system.
* Coding style was changed to tabs. MULTIDPY support was dropped.

Plugins Currently Available
======
annotate, blur, clone, commands, cube, decoration, fade, glib, matecompat, minimize, move, obs, place, resize, rotate, scale, screenshot, svg, switcher, water, wobbly, zoom

Plugins png, imgjpeg, text, regex, inotify were consolidated into core.

Philosophy
======
* Preserve eye candy
* Simplicity
* No feature duplication: one option system, one workspace type etc.

Plans
======
* Tiling 

