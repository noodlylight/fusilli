/**
 *
 * Compiz group plugin
 *
 * queues.c
 *
 * Copyright : (C) 2006-2007 by Patrick Niklaus, Roi Cohen, Danny Baumann
 * Authors: Patrick Niklaus <patrick.niklaus@googlemail.com>
 *          Roi Cohen       <roico.beryl@gmail.com>
 *          Danny Baumann   <maniac@opencompositing.org>
 *
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
 **/

#include "group-internal.h"

/*
 * functions enqueuing pending notifies
 *
 */

/* forward declaration */
static Bool groupDequeueTimer (void *closure);

void
groupEnqueueMoveNotify (CompWindow *w,
			int        dx,
			int        dy,
			Bool       immediate,
			Bool       sync)
{
    GroupPendingMoves *move;

    GROUP_SCREEN (w->screen);

    move = malloc (sizeof (GroupPendingMoves));
    if (!move)
	return;

    move->w  = w;
    move->dx = dx;
    move->dy = dy;

    move->immediate = immediate;
    move->sync      = sync;
    move->next      = NULL;

    if (gs->pendingMoves)
    {
	GroupPendingMoves *temp;
	for (temp = gs->pendingMoves; temp->next; temp = temp->next);

	temp->next = move;
    }
    else
	gs->pendingMoves = move;

    if (!gs->dequeueTimeoutHandle)
    {
	gs->dequeueTimeoutHandle =
	    compAddTimeout (0, 0, groupDequeueTimer, (void *) w->screen);
    }
}

static void
groupDequeueSyncs (GroupPendingSyncs *syncs)
{
    GroupPendingSyncs *sync;

    while (syncs)
    {
	sync = syncs;
	syncs = sync->next;
	
	GROUP_WINDOW (sync->w);
	if (gw->needsPosSync)
	{
	    syncWindowPosition (sync->w);
	    gw->needsPosSync = FALSE;
	}

	free (sync);
    }

}

void
groupDequeueMoveNotifies (CompScreen *s)
{
    GroupPendingMoves *move;
    GroupPendingSyncs *syncs = NULL, *sync;

    GROUP_SCREEN (s);

    gs->queued = TRUE;

    while (gs->pendingMoves)
    {
	move = gs->pendingMoves;
	gs->pendingMoves = move->next;

	moveWindow (move->w, move->dx, move->dy, TRUE, move->immediate);
	if (move->sync)
	{
	    sync = malloc (sizeof (GroupPendingSyncs));
	    if (sync)
	    {
		GROUP_WINDOW (move->w);

		gw->needsPosSync = TRUE;
		sync->w          = move->w;
		sync->next       = syncs;
		syncs            = sync;
	    }
	}
	free (move);
    }

    if (syncs)
	groupDequeueSyncs (syncs);

    gs->queued = FALSE;
}

void
groupEnqueueGrabNotify (CompWindow   *w,
			int          x,
			int          y,
			unsigned int state,
			unsigned int mask)
{
    GroupPendingGrabs *grab;

    GROUP_SCREEN (w->screen);

    grab = malloc (sizeof (GroupPendingGrabs));
    if (!grab)
	return;

    grab->w = w;
    grab->x = x;
    grab->y = y;

    grab->state = state;
    grab->mask  = mask;
    grab->next  = NULL;

    if (gs->pendingGrabs)
    {
	GroupPendingGrabs *temp;
	for (temp = gs->pendingGrabs; temp->next; temp = temp->next);

	temp->next = grab;
    }
    else
	gs->pendingGrabs = grab;

    if (!gs->dequeueTimeoutHandle)
    {
	gs->dequeueTimeoutHandle =
	    compAddTimeout (0, 0, groupDequeueTimer, (void *) w->screen);
    }
}

static void
groupDequeueGrabNotifies (CompScreen *s)
{
    GroupPendingGrabs *grab;

    GROUP_SCREEN (s);

    gs->queued = TRUE;

    while (gs->pendingGrabs)
    {
	grab = gs->pendingGrabs;
	gs->pendingGrabs = gs->pendingGrabs->next;

	(*(grab->w)->screen->windowGrabNotify) (grab->w,
						grab->x, grab->y,
						grab->state, grab->mask);

	free (grab);
    }

    gs->queued = FALSE;
}

void
groupEnqueueUngrabNotify (CompWindow *w)
{
    GroupPendingUngrabs *ungrab;

    GROUP_SCREEN (w->screen);

    ungrab = malloc (sizeof (GroupPendingUngrabs));

    if (!ungrab)
	return;

    ungrab->w    = w;
    ungrab->next = NULL;

    if (gs->pendingUngrabs)
    {
	GroupPendingUngrabs *temp;
	for (temp = gs->pendingUngrabs; temp->next; temp = temp->next);

	temp->next = ungrab;
    }
    else
	gs->pendingUngrabs = ungrab;

    if (!gs->dequeueTimeoutHandle)
    {
	gs->dequeueTimeoutHandle =
	    compAddTimeout (0, 0, groupDequeueTimer, (void *) w->screen);
    }
}

static void
groupDequeueUngrabNotifies (CompScreen *s)
{
    GroupPendingUngrabs *ungrab;

    GROUP_SCREEN (s);

    gs->queued = TRUE;

    while (gs->pendingUngrabs)
    {
	ungrab = gs->pendingUngrabs;
	gs->pendingUngrabs = gs->pendingUngrabs->next;

	(*(ungrab->w)->screen->windowUngrabNotify) (ungrab->w);

	free (ungrab);
    }

    gs->queued = FALSE;
}

static Bool
groupDequeueTimer (void *closure)
{
    CompScreen *s = (CompScreen *) closure;

    GROUP_SCREEN (s);

    groupDequeueMoveNotifies (s);
    groupDequeueGrabNotifies (s);
    groupDequeueUngrabNotifies (s);

    gs->dequeueTimeoutHandle = 0;

    return FALSE;
}
