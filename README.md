Fusilli Window Manager
======

## Introduction

Fusilli is a fork of Compiz.

It is written under the guidance of his noodly appendages.

Utilizes banana-oriented programming.

## Plugins (aka appendages)

3d, addhelper, annotate, blur, clone, colorfilter, commands, cube, decoration, ezoom, fade, firepaint, grid, mag, matecompat,
minimize, move, neg, obs, place, resize, rotate, scale, screenshot, showmouse, snow, splash,
staticswitcher, svg, switcher, thumbnail, wall, wallpaper, water, wobbly, wsnames

Plugins png, imgjpeg, text, regex, inotify, mousepoll were boiled into a soup.

## Option System

The main innovation offered by fusilli is the Banana Configuration System.

It is based on XML and Cavendish bananas.

Critical parts of the source code (file banana.c) were written under the influence of fusilloni all'arrabiata (both from Lidl)

Options are stored in ~/.config/fusilli/banana.xml (you can change this file using the --bananafile parameter of fusilli)

Configuration happens using Fusilli Settings Manager.

It can be invoked from the command-line with:
```
fsm
```

## Installing

### Arch Linux
Package available in AUR: https://aur.archlinux.org/packages/fusilli-git/

### Other distros

Standard procedure applies:

```
./autogen.sh --prefix=PREFIX
make
make install

```