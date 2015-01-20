Fusilli Window Manager
======

## Introduction

Fusilli is a fork of the Unix window manager Compiz. (forked from 0.8.9)

Fusilli has two main goals:
* Preserve the eye candy: spinning cubes, wobbly windows, generally the awesome things that made Compiz famous.
* Reduce complexity and feature duplication. Example: no multiple option systems.

Fusilli is currently under heavy development (no releases yet)

## Plugins Currently Available

3d, addhelper, annotate, blur, clone, commands, cube, decoration, fade, firepaint, grid, matecompat,
minimize, move, neg, obs, place, resize, rotate, scale, screenshot, snow, splash,
staticswitcher, svg, switcher, wall, wallpaper, water, wobbly, wsnames(formerly workspacenames), zoom

Plugins png, imgjpeg, text, regex, inotify were consolidated into core.

The functionality of plugin vpswitch can be simulated by prefixing button bindings with
```
<ClickOnDesktop>
```

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

## Changes to Compiz

### Option system:
* libcompizconfig, bcop and plugins ini, ccp, fuse, gconf, kconfig were removed.
* Options are now handled in core.
* XML metadata parsing was written from scratch.
* Options are stored in a single .xml file (~/.config/fusilli/banana.xml)
* CCSM was replaced with FSM (Fusilli Settings Manager)

### Window Decorations:
* gtk-window-decorator is now fusilli-decorator-gtk
* kde4-window-decorator is now fusilli-decorator-kde4
* fusilli-decorator-gtk now supports marco/GSettings, not metacity/gconf
* The decorator for KDE3 was removed.

### Other:
* Currently dbus support is very limited.
* Currently there is no edge binding support.

### Codebase:
* libdecoration changed to libfusillidecoration
* All names of executables/libs were changed to allow coexistence with compiz on the same system.
* Coding style was changed to tabs.
* MULTIDPY support was dropped.
* CompObject was removed.

