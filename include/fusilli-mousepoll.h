/*
 *
 * Compiz mouse position polling plugin
 *
 * Copyright : (C) 2008 by Dennis Kasprzyk
 * E-mail    : onestone@opencompositing.org
 *
 * Copyright : (C) 2015 by Michail Bitzes
 * E-mail    : noodlylight@gmail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _FUSILLI_MOUSEPOLL_H
#define _FUSILLI_MOUSEPOLL_H

typedef int PositionPollingHandle;

typedef void (*PositionUpdateProc) (CompScreen *s,
                                    int        x,
                                    int        y);

void
mousepollInitScreen (CompScreen *s);

void
mousepollFiniScreen (CompScreen *s);

void
mousePollIntervalChanged (void);

PositionPollingHandle
addPositionPollingCallback (CompScreen     *s,
                            PositionUpdateProc update);

void
removePositionPollingCallback (CompScreen            *s,
                               PositionPollingHandle id);

void
getCurrentMousePosition (CompScreen *s,
                         int        *x,
                         int        *y);

#endif
