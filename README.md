Fusilli Window Manager
======

## Introduction

Fusilli is a fork of the Unix window manager Compiz. (forked from 0.8.9)

It is written under the guidance of his noodly appendages.

Fusilli utilizes banana-oriented programming.

## Plugins (aka appendages)

3d, addhelper, annotate, blur, clone, commands, cube, decoration, fade, firepaint, grid, matecompat,
minimize, move, neg, obs, place, resize, rotate, scale, screenshot, showmouse, snow, splash,
staticswitcher, svg, switcher, wall, wallpaper, water, wobbly, wsnames(formerly workspacenames), zoom

Plugins png, imgjpeg, text, regex, inotify, mousepoll were consolidated into core.

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






