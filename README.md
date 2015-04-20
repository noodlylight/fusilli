Fusilli Window Manager
======

## Introduction

Fusilli is an OpenGL compositing window manager for the X Window System.

It is a fork of Compiz (0.8)

## Plugins Currently Available

3d, addhelper, annotate, blur, clone, colorfilter, commands, cube, decoration, expo, extrawm, ezoom, fade, firepaint, grid, mag, matecompat,
minimize, move, neg, obs, opacify, place, resize, ring, rotate, scale, screenshot, shift, showmouse, snow, splash,
staticswitcher, svg, switcher, thumbnail, titleinfo, trailfocus, wall, wallpaper, water, winrules, wobbly, workarounds, wsnames

Plugins png, imgjpeg, text, regex, inotify, mousepoll were consolidated into core.

## Option System

Options are stored in ~/.config/fusilli/banana.xml (you can change this file using the --bananafile parameter of fusilli)

You can edit this file directly, but it is easier to use the GUI configuration tool - fsm (Fusilli Settings Manager)

## Changes to Compiz

* CCSM was removed and replaced with FSM.
* The gtk window decorator (fusilli-decorator-gtk) now supports marco-1.8 instead of metacity.

## Installing

### Arch Linux
Package available in AUR: https://aur.archlinux.org/packages/fusilli-git/

### Gentoo Linux

An ebuild is available in folder gentoo/ of this repo.

### Other distros

Standard procedure applies:

```
./autogen.sh --prefix=PREFIX
make
make install

```