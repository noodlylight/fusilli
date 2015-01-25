/* This file used to be a plugin. It is kept separate due to licensing 
 *  - it's GPLv2 or later while most core files are BSD 
*/

/*
 *
 * Compiz mouse position polling plugin
 *
 * mousepoll.c
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

#include <fusilli-core.h>
#include <fusilli-mousepoll.h>

#define MAX_NUM_SCREENS 9

typedef struct _MousepollClient MousepollClient;

struct _MousepollClient {
	MousepollClient *next;
	MousepollClient *prev;

	PositionPollingHandle id;
	PositionUpdateProc    update;
};

typedef struct _MousepollScreen {
	MousepollClient       *clients;
	PositionPollingHandle freeId;

	CompTimeoutHandle updateHandle;
	int posX;
	int posY;
} MousepollScreen;

MousepollScreen mousepollDataPerScreen[MAX_NUM_SCREENS];

static Bool
getMousePosition (CompScreen *s)
{
	Window       root_return;
	Window       child_return;
	int          rootX, rootY;
	int          winX, winY;
	unsigned int maskReturn;
	Bool         status;

	MousepollScreen *ms = &mousepollDataPerScreen[s->screenNum];

	status = XQueryPointer (display.display, s->root,
	            &root_return, &child_return,
	            &rootX, &rootY, &winX, &winY, &maskReturn);

	if (!status || rootX > s->width || rootY > s->height ||
	                s->root != root_return)
		return FALSE;

	if ((rootX != ms->posX || rootY != ms->posY))
	{
		ms->posX = rootX;
		ms->posY = rootY;
		return TRUE;
	}

	return FALSE;
}

static Bool
updatePosition (void *c)
{
	CompScreen      *s = (CompScreen *)c;
	MousepollClient *mc;

	MousepollScreen *ms = &mousepollDataPerScreen[s->screenNum];

	if (!ms->clients)
		return FALSE;

	if (getMousePosition (s))
	{
		MousepollClient *next;

		for (mc = ms->clients; mc; mc = next)
		{
			next = mc->next;
			if (mc->update)
				(*mc->update) (s, ms->posX, ms->posY);
		}
	}

	return TRUE;
}

PositionPollingHandle
addPositionPollingCallback (CompScreen         *s,
                            PositionUpdateProc update)
{
	MousepollScreen *ms = &mousepollDataPerScreen[s->screenNum];

	Bool start = FALSE;

	MousepollClient *mc = malloc (sizeof (MousepollClient));

	if (!mc)
		return -1;

	if (!ms->clients)
		start = TRUE;

	mc->update = update;
	mc->id     = ms->freeId;
	ms->freeId++;

	mc->prev = NULL;
	mc->next = ms->clients;

	if (ms->clients)
		ms->clients->prev = mc;

	ms->clients = mc;

	if (start)
	{
		getMousePosition (s);

		const BananaValue *
		option_mouse_poll_interval = bananaGetOption (coreBananaIndex,
		                                              "mouse_poll_interval",
		                                              -1);

		ms->updateHandle =
		    compAddTimeout (
		    option_mouse_poll_interval->i / 2,
		    option_mouse_poll_interval->i,
		    updatePosition, s);
	}

	return mc->id;
}

void
removePositionPollingCallback (CompScreen            *s,
                               PositionPollingHandle id)
{
	MousepollScreen *ms = &mousepollDataPerScreen[s->screenNum];

	MousepollClient *mc = ms->clients;

	if (ms->clients && ms->clients->id == id)
	{
		ms->clients = ms->clients->next;
		if (ms->clients)
			ms->clients->prev = NULL;

		free (mc);
		return;
	}

	for (mc = ms->clients; mc; mc = mc->next)
		if (mc->id == id)
		{
			if (mc->next)
				mc->next->prev = mc->prev;
			if (mc->prev)
				mc->prev->next = mc->next;
			free (mc);
			return;
		}

	if (!ms->clients && ms->updateHandle)
	{
		compRemoveTimeout (ms->updateHandle);
		ms->updateHandle = 0;
	}
}

void
getCurrentMousePosition (CompScreen *s,
                         int        *x,
                         int        *y)
{
	MousepollScreen *ms = &mousepollDataPerScreen[s->screenNum];

	if (!ms->clients)
		getMousePosition (s);

	if (x)
		*x = ms->posX;
	if (y)
		*y = ms->posY;
}

/* should go to addScreen() in screen.c but is kept here due to licensing */
void
mousepollInitScreen (CompScreen *s)
{
	MousepollScreen *ms = &mousepollDataPerScreen[s->screenNum];

	ms->posX = 0;
	ms->posY = 0;

	ms->clients = NULL;
	ms->freeId  = 1;

	ms->updateHandle = 0;
}

/* should go to removeScreen() in screen.c but is kept here due to licensing */
void
mousepollFiniScreen (CompScreen *s)
{
	MousepollScreen *ms = &mousepollDataPerScreen[s->screenNum];

	if (ms->updateHandle)
		compRemoveTimeout (ms->updateHandle);
}

/* should go to displayChangeNotify()
 * in display.c but is kept here due to licensing 
 */
void
mousePollIntervalChanged (void)
{
	CompScreen *s;

	const BananaValue *
	option_mouse_poll_interval = bananaGetOption (coreBananaIndex,
	                                              "mouse_poll_interval",
	                                              -1);

	for (s = display.screens; s; s = s->next)
	{
		MousepollScreen *ms = &mousepollDataPerScreen[s->screenNum];

		if (ms->updateHandle)
		{
			compRemoveTimeout (ms->updateHandle);

			ms->updateHandle =
			    compAddTimeout (
			    option_mouse_poll_interval->i / 2,
			    option_mouse_poll_interval->i,
			    updatePosition, s);
		}
	}
}