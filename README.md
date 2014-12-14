Fusilli
======

OpenGL Compositing Window Manager

High in carbohydrates.

A fork of Compiz (0.8).

## Installing

Fusilli can co-exist with compiz on the same system. (any version of compiz)

### Arch Linux
Package available in AUR: https://aur.archlinux.org/packages/fusilli-git/

### Other distros

Standard procedure applies:

```
./autogen.sh --prefix=PREFIX
make
make install

```
## Configuration

A special graphical tool exists, called Fusilli Settings Manager.

It can be invoked from the command-line with:
```
fsm
```

Options are stored in ~/.config/fusilli/banana.xml (you can change this file using the --bananafile parameter of fusilli)

## Plugins Currently Available

annotate, blur, clone, commands, cube, decoration, fade, matecompat, minimize, move, obs, place, resize, rotate, scale, screenshot, svg, switcher, water, wsnames(formerly workspacenames), wobbly, zoom

Plugins png, imgjpeg, text, regex, inotify were consolidated into core.

The functionality of plugin vpswitch can be simulated by prefixing button bindings with
```
<ClickOnDesktop>
```
## Philosophy

* Preserve eye candy
* Simplicity
* No feature duplication: one option system, one workspace type etc.

## Long Term Plans

* Tiling

## Changes to Compiz

### Option system:
* libcompizconfig, bcop and plugins ini, ccp, fuse, gconf, kconfig were removed.
* Options are now handled in core. 
* XML metadata parsing was written from scratch.
* Options are stored in a single .xml file (~/.config/fusilli/banana.xml)
* CCSM was replaced with FSM (Fusilli Settings Manager)

### Decorator:
* gtk-window-decorator is now fusilli-decorator-gtk
* kde4-window-decorator is now fusilli-decorator-kde4
* Changed to support marco/GSettings, not metacity/gconf
* The decorator for KDE3 was removed.

### Other:
* Currently there is no dbus support.
* Currently there is no edge binding support.

### Codebase:
* libdecoration changed to libfusillidecoration
* All names of executables/libs were changed to allow coexistence with compiz on the same system.
* Coding style was changed to tabs.
* MULTIDPY support was dropped.