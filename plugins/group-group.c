/**
 *
 * Compiz group plugin
 *
 * group.c
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
 * groupIsGroupWindow
 *
 */
Bool
groupIsGroupWindow (CompWindow *w)
{
    if (w->attrib.override_redirect)
	return FALSE;

    if (w->type & CompWindowTypeDesktopMask)
	return FALSE;

    if (w->invisible)
	return FALSE;

    if (!matchEval (groupGetWindowMatch (w->screen), w))
	return FALSE;

    return TRUE;
}

/*
 * groupDragHoverTimeout
 *
 * Description:
 * Activates a window after a certain time a slot has been dragged over it.
 *
 */
static Bool
groupDragHoverTimeout (void* closure)
{
    CompWindow *w = (CompWindow *) closure;
    if (!w)
	return FALSE;

    GROUP_SCREEN (w->screen);
    GROUP_WINDOW (w);

    if (groupGetBarAnimations (w->screen))
    {
	GroupTabBar *bar = gw->group->tabBar;

	bar->bgAnimation = AnimationPulse;
	bar->bgAnimationTime = groupGetPulseTime (w->screen) * 1000;
    }

    (*w->screen->activateWindow) (w);
    gs->dragHoverTimeoutHandle = 0;

    return FALSE;
}

/*
 * groupCheckWindowProperty
 *
 */
Bool
groupCheckWindowProperty (CompWindow *w,
			  long int   *id,
			  Bool       *tabbed,
			  GLushort   *color)
{
    Atom          type;
    int           retval, fmt;
    unsigned long nitems, exbyte;
    long int      *data;

    GROUP_DISPLAY (w->screen->display);

    retval = XGetWindowProperty (w->screen->display->display, w->id,
	     			 gd->groupWinPropertyAtom, 0, 5, False,
	     			 XA_CARDINAL, &type, &fmt, &nitems, &exbyte,
	     			 (unsigned char **)&data);

    if (retval == Success)
    {
	if (type == XA_CARDINAL && fmt == 32 && nitems == 5)
	{
	    if (id)
		*id = data[0];
	    if (tabbed)
		*tabbed = (Bool) data[1];
	    if (color) {
		color[0] = (GLushort) data[2];
		color[1] = (GLushort) data[3];
		color[2] = (GLushort) data[4];
	    }

	    XFree (data);
	    return TRUE;
	}
	else if (fmt != 0)
	    XFree (data);
    }

    return FALSE;
}

/*
 * groupUpdateWindowProperty
 *
 */
void
groupUpdateWindowProperty (CompWindow *w)
{
    CompDisplay *d = w->screen->display;

    GROUP_WINDOW (w);
    GROUP_DISPLAY (d);

    // Do not change anything in this case
    if (gw->readOnlyProperty)
	return;

    if (gw->group)
    {
	long int buffer[5];

	buffer[0] = gw->group->identifier;
	buffer[1] = (gw->slot) ? TRUE : FALSE;

	/* group color RGB */
	buffer[2] = gw->group->color[0];
	buffer[3] = gw->group->color[1];
	buffer[4] = gw->group->color[2];

	XChangeProperty (d->display, w->id, gd->groupWinPropertyAtom,
			 XA_CARDINAL, 32, PropModeReplace,
			 (unsigned char *) buffer, 5);
    }
    else
    {
	XDeleteProperty (d->display, w->id, gd->groupWinPropertyAtom);
    }
}

static unsigned int
groupUpdateResizeRectangle (CompWindow *w,
			    XRectangle *masterGeometry,
			    Bool       damage)
{
    XRectangle   newGeometry;
    unsigned int mask = 0;
    int          newWidth, newHeight;
    int          widthDiff, heightDiff;

    GROUP_WINDOW (w);
    GROUP_DISPLAY (w->screen->display);

    if (!gw->resizeGeometry || !gd->resizeInfo)
	return 0;

    newGeometry.x = WIN_X (w) + (masterGeometry->x -
				 gd->resizeInfo->origGeometry.x);
    newGeometry.y = WIN_Y (w) + (masterGeometry->y -
				 gd->resizeInfo->origGeometry.y);

    widthDiff = masterGeometry->width - gd->resizeInfo->origGeometry.width;
    newGeometry.width = MAX (1, WIN_WIDTH (w) + widthDiff);
    heightDiff = masterGeometry->height - gd->resizeInfo->origGeometry.height;
    newGeometry.height = MAX (1, WIN_HEIGHT (w) + heightDiff);

    if (constrainNewWindowSize (w,
				newGeometry.width, newGeometry.height,
				&newWidth, &newHeight))
    {

	newGeometry.width  = newWidth;
	newGeometry.height = newHeight;
    }

    if (damage)
    {
	if (memcmp (&newGeometry, gw->resizeGeometry,
		    sizeof (newGeometry)) != 0)
	{
	    addWindowDamage (w);
	}
    }

    if (newGeometry.x != gw->resizeGeometry->x)
    {
	gw->resizeGeometry->x = newGeometry.x;
	mask |= CWX;
    }
    if (newGeometry.y != gw->resizeGeometry->y)
    {
	gw->resizeGeometry->y = newGeometry.y;
	mask |= CWY;
    }
    if (newGeometry.width != gw->resizeGeometry->width)
    {
	gw->resizeGeometry->width = newGeometry.width;
	mask |= CWWidth;
    }
    if (newGeometry.height != gw->resizeGeometry->height)
    {
	gw->resizeGeometry->height = newGeometry.height;
	mask |= CWHeight;
    }

    return mask;
}

/*
 * groupGrabScreen
 *
 */
void
groupGrabScreen (CompScreen           *s,
		 GroupScreenGrabState newState)
{
    GROUP_SCREEN (s);

    if ((gs->grabState != newState) && gs->grabIndex)
    {
	removeScreenGrab (s, gs->grabIndex, NULL);
	gs->grabIndex = 0;
    }

    if (newState == ScreenGrabSelect)
    {
	gs->grabIndex = pushScreenGrab (s, None, "group");
    }
    else if (newState == ScreenGrabTabDrag)
    {
	gs->grabIndex = pushScreenGrab (s, None, "group-drag");
    }

    gs->grabState = newState;
}

/*
 * groupRaiseWindows
 *
 */
static void
groupRaiseWindows (CompWindow     *top,
		   GroupSelection *group)
{
    CompWindow **stack;
    CompWindow *w;
    int        count = 0, i;

    if (group->nWins == 1)
	return;

    stack = malloc ((group->nWins - 1) * sizeof (CompWindow *));
    if (!stack)
	return;

    for (w = group->screen->windows; w; w = w->next)
    {
	GROUP_WINDOW (w);
	if ((w->id != top->id) && (gw->group == group))
	    stack[count++] = w;
    }

    for (i = 0; i < count; i++)
	restackWindowBelow (stack[i], top);

    free (stack);
}

/*
 * groupMinimizeWindows
 *
 */
static void
groupMinimizeWindows (CompWindow     *top,
		      GroupSelection *group,
		      Bool           minimize)
{
    int i;
    for (i = 0; i < group->nWins; i++)
    {
	CompWindow *w = group->windows[i];
	if (w->id == top->id)
	    continue;

	if (minimize)
	    minimizeWindow (w);
	else
	    unminimizeWindow (w);
    }
}

/*
 * groupShadeWindows
 *
 */
static void
groupShadeWindows (CompWindow     *top,
		   GroupSelection *group,
		   Bool           shade)
{
    int i;
    unsigned int state;

    for (i = 0; i < group->nWins; i++)
    {
	CompWindow *w = group->windows[i];
	if (w->id == top->id)
	    continue;

	if (shade)
	    state = w->state | CompWindowStateShadedMask;
	else
	    state = w->state & ~CompWindowStateShadedMask;

	changeWindowState (w, state);
	updateWindowAttributes (w, CompStackingUpdateModeNone);
    }
}

/*
 * groupDeleteGroupWindow
 *
 */
void
groupDeleteGroupWindow (CompWindow *w)
{
    GroupSelection *group;

    GROUP_WINDOW (w);
    GROUP_SCREEN (w->screen);

    if (!gw->group)
	return;

    group = gw->group;

    if (group->tabBar && gw->slot)
    {
	if (gs->draggedSlot && gs->dragged &&
	    gs->draggedSlot->window->id == w->id)
	{
	    groupUnhookTabBarSlot (group->tabBar, gw->slot, FALSE);
	}
	else
	    groupDeleteTabBarSlot (group->tabBar, gw->slot);
    }

    if (group->nWins && group->windows)
    {
	CompWindow **buf = group->windows;

	if (group->nWins > 1)
	{
	    int counter = 0;
	    int i;

	    group->windows = calloc (group->nWins - 1, sizeof(CompWindow *));

	    for (i = 0; i < group->nWins; i++)
	    {
		if (buf[i]->id == w->id)
		    continue;
		group->windows[counter++] = buf[i];
	    }
	    group->nWins = counter;

	    if (group->nWins == 1)
	    {
		/* Glow was removed from this window, too */
		damageWindowOutputExtents (group->windows[0]);
		updateWindowOutputExtents (group->windows[0]);

		if (groupGetAutoUngroup (w->screen))
		{
		    if (group->changeState != NoTabChange)
		    {
			/* a change animation is pending: this most
			   likely means that a window must be moved
			   back onscreen, so we do that here */
			CompWindow *lw = group->windows[0];

			groupSetWindowVisibility (lw, TRUE);
		    }
		    if (!groupGetAutotabCreate (w->screen))
			groupDeleteGroup (group);
		}
	    }
	}
	else
	{
	    group->windows = NULL;
	    groupDeleteGroup (group);
	}

	free (buf);

	damageWindowOutputExtents (w);
	gw->group = NULL;
	updateWindowOutputExtents (w);
	groupUpdateWindowProperty (w);
    }
}

void
groupRemoveWindowFromGroup (CompWindow *w)
{
    GROUP_WINDOW (w);

    if (!gw->group)
	return;

    if (gw->group->tabBar && !(gw->animateState & IS_UNGROUPING) &&
	(gw->group->nWins > 1))
    {
	GroupSelection *group = gw->group;

	/* if the group is tabbed, setup untabbing animation. The
	   window will be deleted from the group at the
	   end of the untabbing. */
	if (HAS_TOP_WIN (group))
	{
	    CompWindow *tw = TOP_TAB (group);
	    int        oldX = gw->orgPos.x;
	    int        oldY = gw->orgPos.y;

	    gw->orgPos.x = WIN_CENTER_X (tw) - (WIN_WIDTH (w) / 2);
	    gw->orgPos.y = WIN_CENTER_Y (tw) - (WIN_HEIGHT (w) / 2);

	    gw->destination.x = gw->orgPos.x + gw->mainTabOffset.x;
	    gw->destination.y = gw->orgPos.y + gw->mainTabOffset.y;

	    gw->mainTabOffset.x = oldX;
	    gw->mainTabOffset.y = oldY;

	    if (gw->tx || gw->ty)
	    {
		gw->tx -= (gw->orgPos.x - oldX);
		gw->ty -= (gw->orgPos.y - oldY);
	    }

	    gw->animateState = IS_ANIMATED;
	    gw->xVelocity = gw->yVelocity = 0.0f;
	}

	/* Although when there is no top-tab, it will never really
	   animate anything, if we don't start the animation,
	   the window will never get removed. */
	groupStartTabbingAnimation (group, FALSE);

	groupSetWindowVisibility (w, TRUE);
	group->ungroupState = UngroupSingle;
	gw->animateState |= IS_UNGROUPING;
    }
    else
    {
	/* no tab bar - delete immediately */
	groupDeleteGroupWindow (w);

	if (groupGetAutotabCreate (w->screen) && groupIsGroupWindow (w))
	{
	    groupAddWindowToGroup (w, NULL, 0);
	    groupTabGroup (w);
	}
    }
}

/*
 * groupDeleteGroup
 *
 */
void
groupDeleteGroup (GroupSelection *group)
{
    GroupSelection *next, *prev;
    CompScreen     *s = group->screen;

    GROUP_SCREEN (s);
    GROUP_DISPLAY (s->display);

    if (group->windows)
    {
	int i;

	if (group->tabBar)
	{
	    /* set up untabbing animation and delete the group
	       at the end of the animation */
	    groupUntabGroup (group);
	    group->ungroupState = UngroupAll;
	    return;
	}

	for (i = 0; i < group->nWins; i++)
	{
	    CompWindow *cw = group->windows[i];
	    GROUP_WINDOW (cw);

	    damageWindowOutputExtents (cw);
	    gw->group = NULL;
	    updateWindowOutputExtents (cw);
	    groupUpdateWindowProperty (cw);

	    if (groupGetAutotabCreate (s) && groupIsGroupWindow (cw))
	    {
		groupAddWindowToGroup (cw, NULL, 0);
		groupTabGroup (cw);
	    }
	}

	free (group->windows);
	group->windows = NULL;
    }
    else if (group->tabBar)
	groupDeleteTabBar (group);

    prev = group->prev;
    next = group->next;

    /* relink stack */
    if (prev || next)
    {
	if (prev)
	{
	    if (next)
		prev->next = next;
	    else
		prev->next = NULL;
	}
	if (next)
	{
	    if (prev)
		next->prev = prev;
	    else
	    {
		next->prev = NULL;
		gs->groups = next;
	    }
	}
    }
    else
	gs->groups = NULL;

    if (group == gs->lastHoveredGroup)
	gs->lastHoveredGroup = NULL;
    if (group == gd->lastRestackedGroup)
	gd->lastRestackedGroup = NULL;

    free (group);
}

/*
 * groupAddWindowToGroup
 *
 */
void
groupAddWindowToGroup (CompWindow     *w,
		       GroupSelection *group,
		       long int       initialIdent)
{
    GROUP_SCREEN (w->screen);
    GROUP_WINDOW (w);

    if (gw->group)
	return;

    if (group)
    {
	CompWindow *topTab = NULL;

	group->windows = realloc (group->windows,
				  sizeof (CompWindow *) * (group->nWins + 1));
	group->windows[group->nWins] = w;
	group->nWins++;
	gw->group = group;

	updateWindowOutputExtents (w);
	groupUpdateWindowProperty (w);

	if (group->nWins == 2)
	{
	    /* first window in the group got its glow, too */
	    updateWindowOutputExtents (group->windows[0]);
	}

	if (group->tabBar)
	{
	    if (HAS_TOP_WIN (group))
		topTab = TOP_TAB (group);
	    else if (HAS_PREV_TOP_WIN (group))
	    {
		topTab = PREV_TOP_TAB (group);
		group->topTab = group->prevTopTab;
		group->prevTopTab = NULL;
	    }

	    if (topTab)
	    {
		if (!gw->slot)
		    groupCreateSlot (group, w);

		gw->destination.x = WIN_CENTER_X (topTab) - (WIN_WIDTH (w) / 2);
		gw->destination.y = WIN_CENTER_Y (topTab) -
		                    (WIN_HEIGHT (w) / 2);
		gw->mainTabOffset.x = WIN_X (w) - gw->destination.x;
		gw->mainTabOffset.y = WIN_Y (w) - gw->destination.y;
		gw->orgPos.x = WIN_X (w);
		gw->orgPos.y = WIN_Y (w);

		gw->xVelocity = gw->yVelocity = 0.0f;

		gw->animateState = IS_ANIMATED;

		groupStartTabbingAnimation (group, TRUE);

		addWindowDamage (w);
	    }
	}
    }
    else
    {
	/* create new group */
	GroupSelection *g = malloc (sizeof (GroupSelection));
	if (!g)
	    return;

	g->windows = malloc (sizeof (CompWindow *));
	if (!g->windows)
	{
	    free (g);
	    return;
	}

	g->windows[0] = w;
	g->screen     = w->screen;
	g->nWins      = 1;

	g->topTab      = NULL;
	g->prevTopTab  = NULL;
	g->nextTopTab  = NULL;

	g->changeAnimationTime      = 0;
	g->changeAnimationDirection = 0;

	g->changeState  = NoTabChange;
	g->tabbingState = NoTabbing;
	g->ungroupState = UngroupNone;

	g->tabBar = NULL;

	g->checkFocusAfterTabChange = FALSE;

	g->grabWindow = None;
	g->grabMask   = 0;

	g->inputPrevention = None;
	g->ipwMapped       = FALSE;

	/* glow color */
	g->color[0] = (int)(rand () / (((double)RAND_MAX + 1) / 0xffff));
	g->color[1] = (int)(rand () / (((double)RAND_MAX + 1) / 0xffff));
	g->color[2] = (int)(rand () / (((double)RAND_MAX + 1) / 0xffff));
	g->color[3] = 0xffff;

	if (initialIdent)
	    g->identifier = initialIdent;
	else
	{
	    /* we got no valid group Id passed, so find out a new valid
	       unique one */
	    GroupSelection *tg;
	    Bool           invalidID = FALSE;

	    g->identifier = gs->groups ? gs->groups->identifier : 0;
	    do
	    {
		invalidID = FALSE;
		for (tg = gs->groups; tg; tg = tg->next)
		{
		    if (tg->identifier == g->identifier)
		    {
			invalidID = TRUE;

			g->identifier++;
			break;
		    }
		}
	    }
	    while (invalidID);
	}

	/* relink stack */
	if (gs->groups)
	    gs->groups->prev = g;

	g->next = gs->groups;
	g->prev = NULL;
	gs->groups = g;

	gw->group = g;

	groupUpdateWindowProperty (w);
    }
}

/*
 * groupGroupWindows
 *
 */
Bool
groupGroupWindows (CompDisplay     *d,
		   CompAction      *action,
		   CompActionState state,
		   CompOption      *option,
		   int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);
    s = findScreenAtDisplay (d, xid);

    if (s)
    {
	GROUP_SCREEN (s);

	if (gs->tmpSel.nWins > 0)
	{
	    int            i;
	    CompWindow     *cw;
	    GroupSelection *group = NULL;
	    Bool           tabbed = FALSE;

	    for (i = 0; i < gs->tmpSel.nWins; i++)
	    {
		cw = gs->tmpSel.windows[i];
		GROUP_WINDOW (cw);

		if (gw->group)
		{
		    if (!tabbed || group->tabBar)
			group = gw->group;

		    if (group->tabBar)
			tabbed = TRUE;
		}
	    }

	    /* we need to do one first to get the pointer of a new group */
	    cw = gs->tmpSel.windows[0];
	    GROUP_WINDOW (cw);

	    if (gw->group && (group != gw->group))
		groupDeleteGroupWindow (cw);
	    groupAddWindowToGroup (cw, group, 0);
	    addWindowDamage (cw);

	    gw->inSelection = FALSE;
	    group = gw->group;

	    for (i = 1; i < gs->tmpSel.nWins; i++)
	    {
		cw = gs->tmpSel.windows[i];
		GROUP_WINDOW (cw);

		if (gw->group && (group != gw->group))
		    groupDeleteGroupWindow (cw);
		groupAddWindowToGroup (cw, group, 0);
		addWindowDamage (cw);

		gw->inSelection = FALSE;
	    }

	    /* exit selection */
	    free (gs->tmpSel.windows);
	    gs->tmpSel.windows = NULL;
	    gs->tmpSel.nWins = 0;
	}
    }

    return FALSE;
}

/*
 * groupUnGroupWindows
 *
 */
Bool
groupUnGroupWindows (CompDisplay     *d,
		     CompAction      *action,
		     CompActionState state,
		     CompOption      *option,
		     int             nOption)
{
    Window     xid;
    CompWindow *w;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w   = findTopLevelWindowAtDisplay (d, xid);
    if (w)
    {
	GROUP_WINDOW (w);

	if (gw->group)
	    groupDeleteGroup (gw->group);
    }

    return FALSE;
}

/*
 * groupRemoveWindow
 *
 */
Bool
groupRemoveWindow (CompDisplay     *d,
		   CompAction      *action,
		   CompActionState state,
		   CompOption      *option,
		   int             nOption)
{
    Window     xid;
    CompWindow *w;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w   = findWindowAtDisplay (d, xid);
    if (w)
    {
	GROUP_WINDOW (w);

	if (gw->group)
	    groupRemoveWindowFromGroup (w);
    }

    return FALSE;
}

/*
 * groupCloseWindows
 *
 */
Bool
groupCloseWindows (CompDisplay     *d,
		   CompAction      *action,
		   CompActionState state,
		   CompOption      *option,
		   int             nOption)
{
    Window     xid;
    CompWindow *w;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w   = findWindowAtDisplay (d, xid);
    if (w)
    {
	GROUP_WINDOW (w);

	if (gw->group)
	{
	    int i;

	    for (i = 0; i < gw->group->nWins; i++)
		closeWindow (gw->group->windows[i],
			     getCurrentTimeFromDisplay (d));
	}
    }

    return FALSE;
}

/*
 * groupChangeColor
 *
 */
Bool
groupChangeColor (CompDisplay     *d,
		  CompAction      *action,
		  CompActionState state,
		  CompOption      *option,
		  int             nOption)
{
    Window     xid;
    CompWindow *w;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w   = findWindowAtDisplay (d, xid);
    if (w)
    {
	GROUP_WINDOW (w);

	if (gw->group)
	{
	    GLushort *color = gw->group->color;
	    float    factor = ((float)RAND_MAX + 1) / 0xffff;

	    color[0] = (int)(rand () / factor);
	    color[1] = (int)(rand () / factor);
	    color[2] = (int)(rand () / factor);

	    groupRenderTopTabHighlight (gw->group);
	    damageScreen (w->screen);
	}
    }

    return FALSE;
}

/*
 * groupSetIgnore
 *
 */
Bool
groupSetIgnore (CompDisplay     *d,
		CompAction      *action,
		CompActionState state,
		CompOption      *option,
		int             nOption)
{
    GROUP_DISPLAY (d);

    gd->ignoreMode = TRUE;

    if (state & CompActionStateInitKey)
	action->state |= CompActionStateTermKey;

    return FALSE;
}

/*
 * groupUnsetIgnore
 *
 */
Bool
groupUnsetIgnore (CompDisplay     *d,
		  CompAction      *action,
		  CompActionState state,
		  CompOption      *option,
		  int             nOption)
{
    GROUP_DISPLAY (d);

    gd->ignoreMode = FALSE;

    action->state &= ~CompActionStateTermKey;

    return FALSE;
}

/*
 * groupHandleButtonPressEvent
 *
 */
static void
groupHandleButtonPressEvent (CompScreen *s,
			     XEvent     *event)
{
    GroupSelection *group;
    int            xRoot, yRoot, button;

    GROUP_SCREEN (s);

    xRoot  = event->xbutton.x_root;
    yRoot  = event->xbutton.y_root;
    button = event->xbutton.button;

    for (group = gs->groups; group; group = group->next)
    {
	if (group->inputPrevention != event->xbutton.window)
	    continue;

	if (!group->tabBar)
	    continue;

	switch (button) {
	case Button1:
	    {
		GroupTabBarSlot *slot;

		for (slot = group->tabBar->slots; slot; slot = slot->next)
		{
		    if (XPointInRegion (slot->region, xRoot, yRoot))
		    {
			gs->draggedSlot = slot;
			/* The slot isn't dragged yet */
			gs->dragged = FALSE;
			gs->prevX = xRoot;
			gs->prevY = yRoot;

			if (!otherScreenGrabExist(s, "group", "group-drag", NULL))
			    groupGrabScreen (s, ScreenGrabTabDrag);
		    }
		}
	    }
	    break;

	case Button4:
	case Button5:
	    {
		CompWindow  *topTab = NULL;
		GroupWindow *gw;

		if (group->nextTopTab)
		    topTab = NEXT_TOP_TAB (group);
		else if (group->topTab)
		{
		    /* If there are no tabbing animations,
		       topTab is never NULL. */
		    topTab = TOP_TAB (group);
		}

		if (!topTab)
		    return;

		gw = GET_GROUP_WINDOW (topTab, gs);

		if (button == Button4)
		{
		    if (gw->slot->prev)
			groupChangeTab (gw->slot->prev, RotateLeft);
		    else
			groupChangeTab (gw->group->tabBar->revSlots,
					RotateLeft);
		}
		else
		{
		    if (gw->slot->next)
			groupChangeTab (gw->slot->next, RotateRight);
		    else
			groupChangeTab (gw->group->tabBar->slots, RotateRight);
		}
		break;
	    }
	}

	break;
    }
}

/*
 * groupHandleButtonReleaseEvent
 *
 */
static void
groupHandleButtonReleaseEvent (CompScreen *s,
			       XEvent     *event)
{
    GroupSelection *group;
    int            vx, vy;
    Region         newRegion;
    Bool           inserted = FALSE;
    Bool           wasInTabBar = FALSE;

    GROUP_SCREEN (s);

    if (event->xbutton.button != 1)
	return;

    if (!gs->draggedSlot)
	return;

    if (!gs->dragged)
    {
	groupChangeTab (gs->draggedSlot, RotateUncertain);
	gs->draggedSlot = NULL;

	if (gs->grabState == ScreenGrabTabDrag)
	    groupGrabScreen (s, ScreenGrabNone);

	return;
    }

    GROUP_WINDOW (gs->draggedSlot->window);

    newRegion = XCreateRegion ();
    if (!newRegion)
	return;

    XUnionRegion (newRegion, gs->draggedSlot->region, newRegion);

    groupGetDrawOffsetForSlot (gs->draggedSlot, &vx, &vy);
    XOffsetRegion (newRegion, vx, vy);

    for (group = gs->groups; group; group = group->next)
    {
	Bool            inTabBar;
	Region          clip, buf;
	GroupTabBarSlot *slot;

	if (!group->tabBar || !HAS_TOP_WIN (group))
	    continue;

	/* create clipping region */
	clip = groupGetClippingRegion (TOP_TAB (group));
	if (!clip)
	    continue;

	buf = XCreateRegion ();
	if (!buf)
	{
	    XDestroyRegion (clip);
	    continue;
	}

	XIntersectRegion (newRegion, group->tabBar->region, buf);
	XSubtractRegion (buf, clip, buf);
	XDestroyRegion (clip);

	inTabBar = !XEmptyRegion (buf);
	XDestroyRegion (buf);

	if (!inTabBar)
	    continue;

	wasInTabBar = TRUE;

	for (slot = group->tabBar->slots; slot; slot = slot->next)
	{
	    GroupTabBarSlot *tmpDraggedSlot;
	    GroupSelection  *tmpGroup;
	    Region          slotRegion, buf;
	    XRectangle      rect;
	    Bool            inSlot;

	    if (slot == gs->draggedSlot)
		continue;

	    slotRegion = XCreateRegion ();
	    if (!slotRegion)
		continue;

	    if (slot->prev && slot->prev != gs->draggedSlot)
	    {
		rect.x = slot->prev->region->extents.x2;
	    }
	    else if (slot->prev && slot->prev == gs->draggedSlot &&
		     gs->draggedSlot->prev)
	    {
		rect.x = gs->draggedSlot->prev->region->extents.x2;
	    }
	    else
		rect.x = group->tabBar->region->extents.x1;

	    rect.y = slot->region->extents.y1;

	    if (slot->next && slot->next != gs->draggedSlot)
	    {
		rect.width = slot->next->region->extents.x1 - rect.x;
	    }
	    else if (slot->next && slot->next == gs->draggedSlot &&
		     gs->draggedSlot->next)
	    {
		rect.width = gs->draggedSlot->next->region->extents.x1 - rect.x;
	    }
	    else
		rect.width = group->tabBar->region->extents.x2;

	    rect.height = slot->region->extents.y2 - slot->region->extents.y1;

	    XUnionRectWithRegion (&rect, slotRegion, slotRegion);

	    buf = XCreateRegion ();
	    if (!buf)
		continue;

	    XIntersectRegion (newRegion, slotRegion, buf);
	    inSlot = !XEmptyRegion (buf);

	    XDestroyRegion (buf);
	    XDestroyRegion (slotRegion);

	    if (!inSlot)
		continue;

	    tmpDraggedSlot = gs->draggedSlot;

	    if (group != gw->group)
	    {
		CompWindow     *w = gs->draggedSlot->window;
		GroupSelection *tmpGroup = gw->group;
		int            oldPosX = WIN_CENTER_X (w);
		int            oldPosY = WIN_CENTER_Y (w);

		/* if the dragged window is not the top tab,
		   move it onscreen */
		if (tmpGroup->topTab && !IS_TOP_TAB (w, tmpGroup))
		{
		    CompWindow *tw = TOP_TAB (tmpGroup);

		    oldPosX = WIN_CENTER_X (tw) + gw->mainTabOffset.x;
		    oldPosY = WIN_CENTER_Y (tw) + gw->mainTabOffset.y;

		    groupSetWindowVisibility (w, TRUE);
		}

		/* Change the group. */
		groupDeleteGroupWindow (gs->draggedSlot->window);
		groupAddWindowToGroup (gs->draggedSlot->window, group, 0);

		/* we saved the original center position in oldPosX/Y before -
		   now we should apply that to the new main tab offset */
		if (HAS_TOP_WIN (group))
		{
		    CompWindow *tw = TOP_TAB (group);
		    gw->mainTabOffset.x = oldPosX - WIN_CENTER_X (tw);
		    gw->mainTabOffset.y = oldPosY - WIN_CENTER_Y (tw);
		}
	    }
	    else
		groupUnhookTabBarSlot (group->tabBar, gs->draggedSlot, TRUE);

	    gs->draggedSlot = NULL;
	    gs->dragged = FALSE;
	    inserted = TRUE;

	    if ((tmpDraggedSlot->region->extents.x1 +
		 tmpDraggedSlot->region->extents.x2 + (2 * vx)) / 2 >
		(slot->region->extents.x1 + slot->region->extents.x2) / 2)
	    {
		groupInsertTabBarSlotAfter (group->tabBar,
					    tmpDraggedSlot, slot);
	    }
	    else
		groupInsertTabBarSlotBefore (group->tabBar,
					     tmpDraggedSlot, slot);

	    groupDamageTabBarRegion (group);

	    /* Hide tab-bars. */
	    for (tmpGroup = gs->groups; tmpGroup; tmpGroup = tmpGroup->next)
	    {
		if (group == tmpGroup)
		    groupTabSetVisibility (tmpGroup, TRUE, 0);
		else
		    groupTabSetVisibility (tmpGroup, FALSE, PERMANENT);
	    }

	    break;
	}

	if (inserted)
	    break;
    }

    XDestroyRegion (newRegion);

    if (!inserted)
    {
	CompWindow     *draggedSlotWindow = gs->draggedSlot->window;
	GroupSelection *tmpGroup;

	for (tmpGroup = gs->groups; tmpGroup; tmpGroup = tmpGroup->next)
	    groupTabSetVisibility (tmpGroup, FALSE, PERMANENT);

	gs->draggedSlot = NULL;
	gs->dragged = FALSE;

	if (groupGetDndUngroupWindow (s) && !wasInTabBar)
	{
	    groupRemoveWindowFromGroup (draggedSlotWindow);
	}
	else if (gw->group && gw->group->topTab)
	{
	    groupRecalcTabBarPos (gw->group,
				  (gw->group->tabBar->region->extents.x1 +
				   gw->group->tabBar->region->extents.x2) / 2,
				  gw->group->tabBar->region->extents.x1,
				  gw->group->tabBar->region->extents.x2);
	}

	/* to remove the painted slot */
	damageScreen (s);
    }

    if (gs->grabState == ScreenGrabTabDrag)
	groupGrabScreen (s, ScreenGrabNone);

    if (gs->dragHoverTimeoutHandle)
    {
	compRemoveTimeout (gs->dragHoverTimeoutHandle);
	gs->dragHoverTimeoutHandle = 0;
    }
}

/*
 * groupHandleMotionEvent
 *
 */

/* the radius to determine if it was a click or a drag */
#define RADIUS 5

static void
groupHandleMotionEvent (CompScreen *s,
			int        xRoot,
			int        yRoot)
{
    GROUP_SCREEN (s);

    if (gs->grabState == ScreenGrabTabDrag)
    {
	int    dx, dy;
	int    vx, vy;
	REGION reg;
	Region draggedRegion = gs->draggedSlot->region;

	reg.rects = &reg.extents;
	reg.numRects = 1;

	dx = xRoot - gs->prevX;
	dy = yRoot - gs->prevY;

	if (gs->dragged || abs (dx) > RADIUS || abs (dy) > RADIUS)
	{
	    gs->prevX = xRoot;
	    gs->prevY = yRoot;

	    if (!gs->dragged)
	    {
		GroupSelection *group;
		BoxRec         *box;

		GROUP_WINDOW (gs->draggedSlot->window);

		gs->dragged = TRUE;

		for (group = gs->groups; group; group = group->next)
		    groupTabSetVisibility (group, TRUE, PERMANENT);

		box = &gw->group->tabBar->region->extents;
		groupRecalcTabBarPos (gw->group, (box->x1 + box->x2) / 2,
				      box->x1, box->x2);
	    }

	    groupGetDrawOffsetForSlot (gs->draggedSlot, &vx, &vy);

	    reg.extents.x1 = draggedRegion->extents.x1 + vx;
	    reg.extents.y1 = draggedRegion->extents.y1 + vy;
	    reg.extents.x2 = draggedRegion->extents.x2 + vx;
	    reg.extents.y2 = draggedRegion->extents.y2 + vy;
	    damageScreenRegion (s, &reg);

	    XOffsetRegion (gs->draggedSlot->region, dx, dy);
	    gs->draggedSlot->springX =
		(gs->draggedSlot->region->extents.x1 +
		 gs->draggedSlot->region->extents.x2) / 2;

	    reg.extents.x1 = draggedRegion->extents.x1 + vx;
	    reg.extents.y1 = draggedRegion->extents.y1 + vy;
	    reg.extents.x2 = draggedRegion->extents.x2 + vx;
	    reg.extents.y2 = draggedRegion->extents.y2 + vy;
	    damageScreenRegion (s, &reg);
	}
    }
    else if (gs->grabState == ScreenGrabSelect)
    {
	groupDamageSelectionRect (s, xRoot, yRoot);
    }
}

/*
 * groupHandleEvent
 *
 */
void
groupHandleEvent (CompDisplay *d,
		  XEvent      *event)
{
    CompWindow *w;
    CompScreen *s;

    GROUP_DISPLAY (d);

    switch (event->type) {
    case MotionNotify:
	s = findScreenAtDisplay (d, event->xmotion.root);
	if (s)
	    groupHandleMotionEvent (s, pointerX, pointerY);
	break;

    case ButtonPress:
	s = findScreenAtDisplay (d, event->xbutton.root);
	if (s)
	    groupHandleButtonPressEvent (s, event);
	break;

    case ButtonRelease:
	s = findScreenAtDisplay (d, event->xbutton.root);
	if (s)
	    groupHandleButtonReleaseEvent (s, event);
	break;

    case MapNotify:
	w = findWindowAtDisplay (d, event->xmap.window);
	if (w)
	{
	    CompWindow *cw;
	    for (cw = w->screen->windows; cw; cw = cw->next)
	    {
		if (w->id == cw->frame)
		{
		    GROUP_WINDOW (cw);
		    if (gw->windowHideInfo)
			XUnmapWindow (cw->screen->display->display, cw->frame);
		}
	    }
	}
	break;

    case UnmapNotify:
	w = findWindowAtDisplay (d, event->xunmap.window);
	if (w)
	{
	    GROUP_WINDOW (w);

	    if (w->pendingUnmaps)
	    {
		if (w->shaded)
		{
		    gw->windowState = WindowShaded;

		    if (gw->group && groupGetShadeAll (w->screen))
			groupShadeWindows (w, gw->group, TRUE);
		}
		else if (w->minimized)
		{
		    gw->windowState = WindowMinimized;

		    if (gw->group && groupGetMinimizeAll (w->screen))
			groupMinimizeWindows (w, gw->group, TRUE);
		}
	    }

	    if (gw->group)
	    {
		if (gw->group->tabBar && IS_TOP_TAB (w, gw->group))
		{
		    /* on unmap of the top tab, hide the tab bar and the
		       input prevention window */
		    groupTabSetVisibility (gw->group, FALSE, PERMANENT);
		}
		if (!w->pendingUnmaps)
		{
		    /* close event */
		    if (!(gw->animateState & IS_UNGROUPING))
		    {
			groupDeleteGroupWindow (w);
			damageScreen (w->screen);
		    }
		}
	    }
	}
	break;

    case ClientMessage:
	if (event->xclient.message_type == d->winActiveAtom)
	{
	    w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
	    {
		GROUP_WINDOW (w);

		if (gw->group && gw->group->tabBar &&
		    !IS_TOP_TAB (w, gw->group))
		{
		    gw->group->checkFocusAfterTabChange = TRUE;
		    groupChangeTab (gw->slot, RotateUncertain);
		}
	    }
	}
	else if (event->xclient.message_type == gd->resizeNotifyAtom)
	{
	    CompWindow *w;
	    w = findWindowAtDisplay (d, event->xclient.window);

	    if (w && gd->resizeInfo && (w == gd->resizeInfo->resizedWindow))
	    {
		GROUP_WINDOW (w);
		GROUP_SCREEN (w->screen);

		if (gw->group)
		{
		    int        i;
		    XRectangle rect;

		    rect.x      = event->xclient.data.l[0];
		    rect.y      = event->xclient.data.l[1];
		    rect.width  = event->xclient.data.l[2];
		    rect.height = event->xclient.data.l[3];

		    for (i = 0; i < gw->group->nWins; i++)
		    {
			CompWindow  *cw = gw->group->windows[i];
			GroupWindow *gcw;

			gcw = GET_GROUP_WINDOW (cw, gs);
			if (gcw->resizeGeometry)
			{
			    if (groupUpdateResizeRectangle (cw, &rect, TRUE))
				addWindowDamage (cw);
			}
		    }
		}
	    }
	}
	break;

    default:
	if (event->type == d->shapeEvent + ShapeNotify)
	{
	    XShapeEvent *se = (XShapeEvent *) event;
	    if (se->kind == ShapeInput)
	    {
		CompWindow *w;
		w = findWindowAtDisplay (d, se->window);
		if (w)
		{
		    GROUP_WINDOW (w);

		    if (gw->windowHideInfo)
			groupClearWindowInputShape (w, gw->windowHideInfo);
		}
	    }
	}
	break;
    }

    UNWRAP (gd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (gd, d, handleEvent, groupHandleEvent);

    switch (event->type) {
    case PropertyNotify:
	if (event->xproperty.atom == d->wmNameAtom)
	{
	    CompWindow *w;
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
	    {
		GROUP_WINDOW (w);

		if (gw->group && gw->group->tabBar &&
		    gw->group->tabBar->textSlot    &&
		    gw->group->tabBar->textSlot->window == w)
		{
		    /* make sure we are using the updated name */
		    groupRenderWindowTitle (gw->group);
		    groupDamageTabBarRegion (gw->group);
		}
	    }
	}
	break;

    case EnterNotify:
	{
	    CompWindow *w;
	    w = findWindowAtDisplay (d, event->xcrossing.window);
	    if (w)
	    {
		GROUP_WINDOW (w);
		GROUP_SCREEN (w->screen);

		if (gs->showDelayTimeoutHandle)
		    compRemoveTimeout (gs->showDelayTimeoutHandle);

		if (w->id != w->screen->grabWindow)
		    groupUpdateTabBars (w->screen, w->id);

		if (gw->group)
		{
		    if (gs->draggedSlot && gs->dragged &&
			IS_TOP_TAB (w, gw->group))
		    {
			int hoverTime;
			hoverTime = groupGetDragHoverTime (w->screen) * 1000;
			if (gs->dragHoverTimeoutHandle)
			    compRemoveTimeout (gs->dragHoverTimeoutHandle);

			if (hoverTime > 0)
			    gs->dragHoverTimeoutHandle =
				compAddTimeout (hoverTime,
						(float) hoverTime * 1.2,
						groupDragHoverTimeout, w);
		    }
		}
	    }
	}
	break;

    case ConfigureNotify:
	{
	    CompWindow *w;

	    w = findWindowAtDisplay (d, event->xconfigure.window);
	    if (w)
	    {
		GROUP_WINDOW (w);

		if (gw->group && gw->group->tabBar &&
		    IS_TOP_TAB (w, gw->group)      &&
		    gw->group->inputPrevention && gw->group->ipwMapped)
		{
		    XWindowChanges xwc;

		    xwc.stack_mode = Above;
		    xwc.sibling = w->id;

		    XConfigureWindow (w->screen->display->display,
				      gw->group->inputPrevention,
				      CWSibling | CWStackMode, &xwc);
		}

		if (event->xconfigure.above != None)
		{
		    if (gw->group && !gw->group->tabBar &&
			(gw->group != gd->lastRestackedGroup))
		    {
			if (groupGetRaiseAll (w->screen))
			    groupRaiseWindows (w, gw->group);
		    }
		    if (w->managed && !w->attrib.override_redirect)
			gd->lastRestackedGroup = gw->group;
		}
	    }
	}
	break;

    default:
	break;
    }
}

/*
 * groupGetOutputExtentsForWindow
 *
 */
void
groupGetOutputExtentsForWindow (CompWindow        *w,
				CompWindowExtents *output)
{
    GROUP_SCREEN (w->screen);
    GROUP_WINDOW (w);

    UNWRAP (gs, w->screen, getOutputExtentsForWindow);
    (*w->screen->getOutputExtentsForWindow) (w, output);
    WRAP (gs, w->screen, getOutputExtentsForWindow,
	  groupGetOutputExtentsForWindow);

    if (gw->group && gw->group->nWins > 1)
    {
	GROUP_DISPLAY (w->screen->display);

	int glowSize = groupGetGlowSize (w->screen);
	int glowType = groupGetGlowType (w->screen);
	int glowTextureSize = gd->glowTextureProperties[glowType].textureSize;
	int glowOffset = gd->glowTextureProperties[glowType].glowOffset;

	glowSize = glowSize * (glowTextureSize - glowOffset) / glowTextureSize;

	/* glowSize is the size of the glow outside the window decoration
	 * (w->input), while w->output includes the size of w->input
	 * this is why we have to add w->input here */
	output->left   = MAX (output->left, glowSize + w->input.left);
	output->right  = MAX (output->right, glowSize + w->input.right);
	output->top    = MAX (output->top, glowSize + w->input.top);
	output->bottom = MAX (output->bottom, glowSize + w->input.bottom);
    }
}

/*
 * groupWindowResizeNotify
 *
 */
void
groupWindowResizeNotify (CompWindow *w,
			 int        dx,
			 int        dy,
			 int        dwidth,
			 int        dheight)
{
    CompScreen *s = w->screen;

    GROUP_SCREEN (s);
    GROUP_WINDOW (w);

    if (gw->resizeGeometry)
    {
	free (gw->resizeGeometry);
	gw->resizeGeometry = NULL;
    }

    UNWRAP (gs, s, windowResizeNotify);
    (*s->windowResizeNotify) (w, dx, dy, dwidth, dheight);
    WRAP (gs, s, windowResizeNotify, groupWindowResizeNotify);

    if (gw->glowQuads)
	groupComputeGlowQuads (w, &gs->glowTexture.matrix);

    if (gw->group && gw->group->tabBar && IS_TOP_TAB (w, gw->group))
    {
	if (gw->group->tabBar->state != PaintOff)
	{
	    groupRecalcTabBarPos (gw->group, pointerX,
				  WIN_X (w), WIN_X (w) + WIN_WIDTH (w));
	}
    }
}

/*
 * groupWindowMoveNotify
 *
 */
void
groupWindowMoveNotify (CompWindow *w,
		       int        dx,
		       int        dy,
		       Bool       immediate)
{
    CompScreen *s = w->screen;
    Bool       viewportChange;
    int        i;

    GROUP_SCREEN (s);
    GROUP_DISPLAY (s->display);
    GROUP_WINDOW (w);

    UNWRAP (gs, s, windowMoveNotify);
    (*s->windowMoveNotify) (w, dx, dy, immediate);
    WRAP (gs, s, windowMoveNotify, groupWindowMoveNotify);

    if (gw->glowQuads)
	groupComputeGlowQuads (w, &gs->glowTexture.matrix);

    if (!gw->group || gs->queued)
	return;

    /* FIXME: we need a reliable, 100% safe way to detect window
       moves caused by viewport changes here */
    viewportChange = ((dx && !(dx % w->screen->width)) ||
		      (dy && !(dy % w->screen->height)));

    if (viewportChange && (gw->animateState & IS_ANIMATED))
    {
	gw->destination.x += dx;
	gw->destination.y += dy;
    }

    if (gw->group->tabBar && IS_TOP_TAB (w, gw->group))
    {
	GroupTabBarSlot *slot;
	GroupTabBar     *bar = gw->group->tabBar;

	bar->rightSpringX += dx;
	bar->leftSpringX += dx;

	groupMoveTabBarRegion (gw->group, dx, dy, TRUE);

	for (slot = bar->slots; slot; slot = slot->next)
	{
	    XOffsetRegion (slot->region, dx, dy);
	    slot->springX += dx;
	}
    }

    if (!groupGetMoveAll (s) || gd->ignoreMode ||
	(gw->group->tabbingState != NoTabbing) ||
	(gw->group->grabWindow != w->id) ||
	!(gw->group->grabMask & CompWindowGrabMoveMask))
    {
	return;
    }

    for (i = 0; i < gw->group->nWins; i++)
    {
	CompWindow *cw = gw->group->windows[i];
	if (!cw)
	    continue;

	if (cw->id == w->id)
	    continue;

	GROUP_WINDOW (cw);

	if (cw->state & MAXIMIZE_STATE)
	{
	    if (viewportChange)
		groupEnqueueMoveNotify (cw, -dx, -dy, immediate, TRUE);
	}
	else if (!viewportChange)
	{
	    gw->needsPosSync = TRUE;
	    groupEnqueueMoveNotify (cw, dx, dy, immediate, FALSE);
	}
    }
}

void
groupWindowGrabNotify (CompWindow   *w,
		       int          x,
		       int          y,
		       unsigned int state,
		       unsigned int mask)
{
    CompScreen *s = w->screen;

    GROUP_SCREEN (s);
    GROUP_DISPLAY (s->display);
    GROUP_WINDOW (w);

    if (gw->group && !gd->ignoreMode && !gs->queued)
    {
	Bool doResizeAll;
	int  i;

	doResizeAll = groupGetResizeAll (s) &&
	              (mask & CompWindowGrabResizeMask);

	if (gw->group->tabBar)
	    groupTabSetVisibility (gw->group, FALSE, 0);

	for (i = 0; i < gw->group->nWins; i++)
	{
	    CompWindow *cw = gw->group->windows[i];
	    if (!cw)
		continue;

	    if (cw->id != w->id)
	    {
		GroupWindow *gcw = GET_GROUP_WINDOW (cw, gs);

		groupEnqueueGrabNotify (cw, x, y, state, mask);

		if (doResizeAll && !(cw->state & MAXIMIZE_STATE))
		{
		    if (!gcw->resizeGeometry)
			gcw->resizeGeometry = malloc (sizeof (XRectangle));
		    if (gcw->resizeGeometry)
		    {
			gcw->resizeGeometry->x      = WIN_X (cw);
			gcw->resizeGeometry->y      = WIN_Y (cw);
			gcw->resizeGeometry->width  = WIN_WIDTH (cw);
			gcw->resizeGeometry->height = WIN_HEIGHT (cw);
		    }
		}
	    }
	}

	if (doResizeAll)
	{
	    if (!gd->resizeInfo)
		gd->resizeInfo = malloc (sizeof (GroupResizeInfo));

	    if (gd->resizeInfo)
	    {
		gd->resizeInfo->resizedWindow       = w;
		gd->resizeInfo->origGeometry.x      = WIN_X (w);
		gd->resizeInfo->origGeometry.y      = WIN_Y (w);
		gd->resizeInfo->origGeometry.width  = WIN_WIDTH (w);
		gd->resizeInfo->origGeometry.height = WIN_HEIGHT (w);
	    }
	}

	gw->group->grabWindow = w->id;
	gw->group->grabMask = mask;
    }

    UNWRAP (gs, s, windowGrabNotify);
    (*s->windowGrabNotify) (w, x, y, state, mask);
    WRAP (gs, s, windowGrabNotify, groupWindowGrabNotify);
}

void
groupWindowUngrabNotify (CompWindow *w)
{
    CompScreen *s = w->screen;

    GROUP_SCREEN (s);
    GROUP_DISPLAY (s->display);
    GROUP_WINDOW (w);

    if (gw->group && !gd->ignoreMode && !gs->queued)
    {
	int        i;
	XRectangle rect;

	groupDequeueMoveNotifies (s);

	if (gd->resizeInfo)
	{
	    rect.x      = WIN_X (w);
	    rect.y      = WIN_Y (w);
	    rect.width  = WIN_WIDTH (w);
	    rect.height = WIN_HEIGHT (w);
	}

	for (i = 0; i < gw->group->nWins; i++)
	{
	    CompWindow *cw = gw->group->windows[i];
	    if (!cw)
		continue;

	    if (cw->id != w->id)
	    {
		GROUP_WINDOW (cw);

		if (gw->resizeGeometry)
		{
		    unsigned int mask;

		    gw->resizeGeometry->x      = WIN_X (cw);
		    gw->resizeGeometry->y      = WIN_Y (cw);
		    gw->resizeGeometry->width  = WIN_WIDTH (cw);
		    gw->resizeGeometry->height = WIN_HEIGHT (cw);

		    mask = groupUpdateResizeRectangle (cw, &rect, FALSE);
		    if (mask)
		    {
			XWindowChanges xwc;
			xwc.x      = gw->resizeGeometry->x;
			xwc.y      = gw->resizeGeometry->y;
			xwc.width  = gw->resizeGeometry->width;
			xwc.height = gw->resizeGeometry->height;

			if (w->mapNum && (mask & (CWWidth | CWHeight)))
			    sendSyncRequest (w);

			configureXWindow (cw, mask, &xwc);
		    }
		    else
		    {
			free (gw->resizeGeometry);
			gw->resizeGeometry =  NULL;
		    }
		}
		if (gw->needsPosSync)
		{
		    syncWindowPosition (cw);
		    gw->needsPosSync = FALSE;
		}
		groupEnqueueUngrabNotify (cw);
	    }
	}

	if (gd->resizeInfo)
	{
	    free (gd->resizeInfo);
	    gd->resizeInfo = NULL;
	}

	gw->group->grabWindow = None;
	gw->group->grabMask = 0;
    }

    UNWRAP (gs, s, windowUngrabNotify);
    (*s->windowUngrabNotify) (w);
    WRAP( gs, s, windowUngrabNotify, groupWindowUngrabNotify);
}

Bool
groupDamageWindowRect (CompWindow *w,
		       Bool       initial,
		       BoxPtr     rect)
{
    Bool       status;
    CompScreen *s = w->screen;

    GROUP_SCREEN (s);
    GROUP_WINDOW (w);

    UNWRAP (gs, s, damageWindowRect);
    status = (*s->damageWindowRect) (w,initial,rect);
    WRAP (gs, s, damageWindowRect, groupDamageWindowRect);

    if (initial)
    {
	if (groupGetAutotabCreate (s) && groupIsGroupWindow (w))
	{
	    if (!gw->group && (gw->windowState == WindowNormal))
	    {
		groupAddWindowToGroup (w, NULL, 0);
		groupTabGroup (w);
	    }
	}

	if (gw->group)
	{
	    if (gw->windowState == WindowMinimized)
	    {
		if (groupGetMinimizeAll (s))
		    groupMinimizeWindows (w, gw->group, FALSE);
	    }
	    else if (gw->windowState == WindowShaded)
	    {
		if (groupGetShadeAll (s))
		    groupShadeWindows (w, gw->group, FALSE);
	    }
	}

	gw->windowState = WindowNormal;
    }

    if (gw->resizeGeometry)
    {
	BoxRec box;

	groupGetStretchRectangle (w, &box, NULL, NULL);
	groupDamagePaintRectangle (s, &box);
    }

    if (gw->slot)
    {
	int    vx, vy;
	Region reg;

	groupGetDrawOffsetForSlot (gw->slot, &vx, &vy);
	if (vx || vy)
	{
	    reg = XCreateRegion ();
	    XUnionRegion (reg, gw->slot->region, reg);
	    XOffsetRegion (reg, vx, vy);
	}
	else
	    reg = gw->slot->region;

	damageScreenRegion (s, reg);

	if (vx || vy)
	    XDestroyRegion (reg);
    }

    return status;
}

void
groupWindowStateChangeNotify (CompWindow   *w,
			      unsigned int lastState)
{
    CompScreen *s = w->screen;

    GROUP_DISPLAY (s->display);
    GROUP_SCREEN (s);
    GROUP_WINDOW (w);

    if (gw->group && !gd->ignoreMode)
    {
	if (((lastState & MAXIMIZE_STATE) != (w->state & MAXIMIZE_STATE)) &&
	    groupGetMaximizeUnmaximizeAll (s))
	{
	    int i;
	    for (i = 0; i < gw->group->nWins; i++)
	    {
		CompWindow *cw = gw->group->windows[i];
		if (!cw)
		    continue;

		if (cw->id == w->id)
		    continue;

		maximizeWindow (cw, w->state & MAXIMIZE_STATE);
	    }
	}
    }

    UNWRAP (gs, s, windowStateChangeNotify);
    (*s->windowStateChangeNotify) (w, lastState);
    WRAP (gs, s, windowStateChangeNotify, groupWindowStateChangeNotify);
}

void
groupActivateWindow (CompWindow *w)
{
    CompScreen *s = w->screen;

    GROUP_SCREEN (s);
    GROUP_WINDOW (w);

    if (gw->group && gw->group->tabBar && !IS_TOP_TAB (w, gw->group))
	groupChangeTab (gw->slot, RotateUncertain);

    UNWRAP (gs, s, activateWindow);
    (*s->activateWindow) (w);
    WRAP (gs, s, activateWindow, groupActivateWindow);
}
