/**
 *
 * Compiz group plugin
 *
 * selection.c
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
 * groupWindowInRegion
 *
 */
static Bool
groupWindowInRegion (CompWindow *w,
		     Region     src,
		     float      precision)
{
    Region buf;
    int    i;
    int    area = 0;
    BOX    *box;

    buf = XCreateRegion ();
    if (!buf)
	return FALSE;

    XIntersectRegion (w->region, src, buf);

    /* buf area */
    for (i = 0; i < buf->numRects; i++)
    {
	box = &buf->rects[i];
	area += (box->x2 - box->x1) * (box->y2 - box->y1); /* width * height */
    }

    XDestroyRegion (buf);

    if (area >= WIN_WIDTH (w) * WIN_HEIGHT (w) * precision)
    {
	XSubtractRegion (src, w->region, src);
	return TRUE;
    }

    return FALSE;
}

/*
 * groupFindGroupInWindows
 *
 */
static Bool
groupFindGroupInWindows (GroupSelection *group,
			 CompWindow     **windows,
			 int            nWins)
{
    int i;

    for (i = 0; i < nWins; i++)
    {
	CompWindow *cw = windows[i];
	GROUP_WINDOW (cw);

	if (gw->group == group)
	    return TRUE;
    }

    return FALSE;
}

/*
 * groupFindWindowsInRegion
 *
 */
static CompWindow**
groupFindWindowsInRegion (CompScreen *s,
			  Region     reg,
			  int        *c)
{
    float      precision = groupGetSelectPrecision (s) / 100.0f;
    CompWindow **ret = NULL;
    int        count = 0;
    CompWindow *w;

    for (w = s->reverseWindows; w; w = w->prev)
    {
	if (groupIsGroupWindow (w) &&
	    groupWindowInRegion (w, reg, precision))
	{
	    GROUP_WINDOW (w);
	    if (gw->group && groupFindGroupInWindows (gw->group, ret, count))
		continue;

	    ret = realloc (ret, sizeof (CompWindow) * (count + 1));
	    ret[count] = w;

	    count++;
	}
    }

    (*c) = count;
    return ret;
}

/*
 * groupDeleteSelectionWindow
 *
 */
static void
groupDeleteSelectionWindow (CompWindow *w)
{
    GROUP_SCREEN (w->screen);
    GROUP_WINDOW (w);

    if (gs->tmpSel.nWins > 0 && gs->tmpSel.windows)
    {
	CompWindow **buf = gs->tmpSel.windows;
	int        counter = 0;
	int        i;

	gs->tmpSel.windows = calloc (gs->tmpSel.nWins - 1,
				     sizeof (CompWindow *));

	for (i = 0; i < gs->tmpSel.nWins; i++)
	{
	    if (buf[i]->id == w->id)
		continue;

	    gs->tmpSel.windows[counter++] = buf[i];
	}

	gs->tmpSel.nWins = counter;
	free (buf);
    }

    gw->inSelection = FALSE;
}

/*
 * groupAddWindowToSelection
 *
 */
static void
groupAddWindowToSelection (CompWindow *w)
{
    GROUP_SCREEN (w->screen);
    GROUP_WINDOW (w);

    gs->tmpSel.windows = realloc (gs->tmpSel.windows,
				  sizeof (CompWindow *) *
				  (gs->tmpSel.nWins + 1));

    gs->tmpSel.windows[gs->tmpSel.nWins] = w;
    gs->tmpSel.nWins++;

    gw->inSelection = TRUE;
}

/*
 * groupSelectWindow
 *
 */
static void
groupSelectWindow (CompWindow *w)
{
    GROUP_SCREEN (w->screen);
    GROUP_WINDOW (w);

    /* filter out windows we don't want to be groupable */
    if (!groupIsGroupWindow (w))
	return;

    if (gw->inSelection)
    {
	if (gw->group)
	{
	    /* unselect group */
	    GroupSelection *group = gw->group;
	    CompWindow     **buf = gs->tmpSel.windows;
	    int            i, counter = 0;

	    /* Faster than doing groupDeleteSelectionWindow
	       for each window in this group. */
	    gs->tmpSel.windows = calloc (gs->tmpSel.nWins - gw->group->nWins,
					 sizeof (CompWindow *));

	    for (i = 0; i < gs->tmpSel.nWins; i++)
	    {
		CompWindow *cw = buf[i];
		GROUP_WINDOW (cw);

		if (gw->group == group)
		{
		    gw->inSelection = FALSE;
		    addWindowDamage (cw);
		    continue;
		}

		gs->tmpSel.windows[counter++] = buf[i];
	    }
	    gs->tmpSel.nWins = counter;
	    free (buf);
	}
	else
	{
	    /* unselect single window */
	    groupDeleteSelectionWindow (w);
	    addWindowDamage (w);
	}
    }
    else
    {
	if (gw->group)
	{
	    /* select group */
	    int i;
	    for (i = 0; i < gw->group->nWins; i++)
	    {
		CompWindow *cw = gw->group->windows[i];

		groupAddWindowToSelection (cw);
		addWindowDamage (cw);
	    }
	}
	else
	{
	    /* select single window */
	    groupAddWindowToSelection (w);
	    addWindowDamage (w);
	}
    }
}

/*
 * groupSelectSingle
 *
 */
Bool
groupSelectSingle (CompDisplay     *d,
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
	groupSelectWindow (w);

    return TRUE;
}

/*
 * groupSelect
 *
 */
Bool
groupSelect (CompDisplay     *d,
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
	GROUP_SCREEN (w->screen);

	if (gs->grabState == ScreenGrabNone)
	{
	    groupGrabScreen (w->screen, ScreenGrabSelect);

	    if (state & CompActionStateInitKey)
		action->state |= CompActionStateTermKey;

	    if (state & CompActionStateInitButton)
		action->state |= CompActionStateTermButton;

	    gs->x1 = gs->x2 = pointerX;
	    gs->y1 = gs->y2 = pointerY;
	}

	return TRUE;
    }

    return FALSE;
}

/*
 * groupSelectTerminate
 *
 */
Bool
groupSelectTerminate (CompDisplay     *d,
		      CompAction      *action,
		      CompActionState state,
		      CompOption      *option,
		      int             nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed(option, nOption, "root", 0);
    s = findScreenAtDisplay (d, xid);
    if (s)
    {
	GROUP_SCREEN (s);

	if (gs->grabState == ScreenGrabSelect)
	{
	    groupGrabScreen (s, ScreenGrabNone);

	    if (gs->x1 != gs->x2 && gs->y1 != gs->y2)
	    {
		Region     reg;
		XRectangle rect;
		int        count;
		CompWindow **ws;

		reg = XCreateRegion ();
		if (reg)
		{
		    rect.x      = MIN (gs->x1, gs->x2) - 2;
		    rect.y      = MIN (gs->y1, gs->y2) - 2;
		    rect.width  = MAX (gs->x1, gs->x2) -
			          MIN (gs->x1, gs->x2) + 4;
		    rect.height = MAX (gs->y1, gs->y2) -
			          MIN (gs->y1, gs->y2) + 4;
		    XUnionRectWithRegion (&rect, reg, reg);

		    damageScreenRegion (s, reg);

		    ws = groupFindWindowsInRegion (s, reg, &count);
		    if (ws)
		    {
			/* select windows */
			int i;
			for (i = 0; i < count; i++)
			    groupSelectWindow (ws[i]);

			if (groupGetAutoGroup(s))
			    groupGroupWindows (d, NULL, 0, NULL, 0);

			free (ws);
		    }
		    XDestroyRegion (reg);
		}
	    }
	}
    }

    action->state &= ~(CompActionStateTermKey | CompActionStateTermButton);

    return FALSE;
}

/*
 * groupDamageSelectionRect
 *
 */
void
groupDamageSelectionRect (CompScreen *s,
			  int        xRoot,
			  int        yRoot)
{
    REGION reg;

    GROUP_SCREEN (s);

    reg.rects = &reg.extents;
    reg.numRects = 1;

    reg.extents.x1 = MIN (gs->x1, gs->x2) - 5;
    reg.extents.y1 = MIN (gs->y1, gs->y2) - 5;
    reg.extents.x2 = MAX (gs->x1, gs->x2) + 5;
    reg.extents.y2 = MAX (gs->y1, gs->y2) + 5;
    damageScreenRegion (s, &reg);

    gs->x2 = xRoot;
    gs->y2 = yRoot;

    reg.extents.x1 = MIN (gs->x1, gs->x2) - 5;
    reg.extents.y1 = MIN (gs->y1, gs->y2) - 5;
    reg.extents.x2 = MAX (gs->x1, gs->x2) + 5;
    reg.extents.y2 = MAX (gs->y1, gs->y2) + 5;
    damageScreenRegion (s, &reg);
}
