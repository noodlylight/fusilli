/**
 *
 * Compiz group plugin
 *
 * tab.c
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
 * groupGetCurrentMousePosition
 *
 * Description:
 * Return the current function of the pointer at the given screen.
 * The position is queried trough XQueryPointer directly from the xserver.
 *
 */
Bool
groupGetCurrentMousePosition (CompScreen *s,
			      int        *x,
			      int        *y)
{
    unsigned int rmask;
    int          mouseX, mouseY, winX, winY;
    Window       root;
    Window       child;
    Bool         result;

    result = XQueryPointer (s->display->display, s->root, &root,
			    &child, &mouseX, &mouseY, &winX, &winY, &rmask);

    if (result)
    {
	(*x) = mouseX;
	(*y) = mouseY;
    }

    return result;
}

/*
 * groupGetClippingRegion
 *
 * Description:
 * This function returns a clipping region which is used to clip
 * several events involving window stack such as hover detection
 * in the tab bar or Drag'n'Drop. It creates the clipping region
 * with getting the region of every window above the given window
 * and then adds this region to the clipping region using
 * XUnionRectWithRegion. w->region won't work since it doesn't include
 * the window decoration.
 *
 */
Region
groupGetClippingRegion (CompWindow *w)
{
    CompWindow *cw;
    Region     clip;

    clip = XCreateRegion ();
    if (!clip)
	return NULL;

    for (cw = w->next; cw; cw = cw->next)
    {
	if (!cw->invisible && !(cw->state & CompWindowStateHiddenMask))
	{
	    XRectangle rect;
	    Region     buf;

	    buf = XCreateRegion ();
	    if (!buf)
	    {
		XDestroyRegion (clip);
		return NULL;
	    }

	    rect.x      = WIN_REAL_X (cw);
	    rect.y      = WIN_REAL_Y (cw);
	    rect.width  = WIN_REAL_WIDTH (cw);
	    rect.height = WIN_REAL_HEIGHT (cw);
	    XUnionRectWithRegion (&rect, buf, buf);

	    XUnionRegion (clip, buf, clip);
	    XDestroyRegion (buf);
	}
    }

    return clip;
}


/*
 * groupClearWindowInputShape
 *
 */
void
groupClearWindowInputShape (CompWindow          *w,
			    GroupWindowHideInfo *hideInfo)
{
    XRectangle  *rects;
    int         count = 0, ordering;
    CompDisplay *d = w->screen->display;

    rects = XShapeGetRectangles (d->display, w->id, ShapeInput,
				 &count, &ordering);

    if (count == 0)
	return;

    /* check if the returned shape exactly matches the window shape -
       if that is true, the window currently has no set input shape */
    if ((count == 1) &&
	(rects[0].x == -w->serverBorderWidth) &&
	(rects[0].y == -w->serverBorderWidth) &&
	(rects[0].width == (w->serverWidth + w->serverBorderWidth)) &&
	(rects[0].height == (w->serverHeight + w->serverBorderWidth)))
    {
	count = 0;
    }

    if (hideInfo->inputRects)
	XFree (hideInfo->inputRects);

    hideInfo->inputRects = rects;
    hideInfo->nInputRects = count;
    hideInfo->inputRectOrdering = ordering;

    XShapeSelectInput (d->display, w->id, NoEventMask);

    XShapeCombineRectangles (d->display, w->id, ShapeInput, 0, 0,
			     NULL, 0, ShapeSet, 0);

    XShapeSelectInput (d->display, w->id, ShapeNotify);
}

/*
 * groupSetWindowVisibility
 *
 */
void
groupSetWindowVisibility (CompWindow *w,
			  Bool       visible)
{
    CompDisplay *d = w->screen->display;

    GROUP_WINDOW (w);

    if (!visible && !gw->windowHideInfo)
    {
	GroupWindowHideInfo *info;

	gw->windowHideInfo = info = malloc (sizeof (GroupWindowHideInfo));
	if (!gw->windowHideInfo)
	    return;

	info->inputRects = NULL;
	info->nInputRects = 0;
	info->shapeMask = XShapeInputSelected (d->display, w->id);
	groupClearWindowInputShape (w, info);

	if (w->frame)
	{
	    info->frameWindow = w->frame;
	    XUnmapWindow (d->display, w->frame);
	} else
	    info->frameWindow = None;

	info->skipState = w->state & (CompWindowStateSkipPagerMask |
				      CompWindowStateSkipTaskbarMask);

	changeWindowState (w, w->state |
			   CompWindowStateSkipPagerMask |
			   CompWindowStateSkipTaskbarMask);
    }
    else if (visible && gw->windowHideInfo)
    {
	GroupWindowHideInfo *info = gw->windowHideInfo;

	if (info->nInputRects)
	{
	    XShapeCombineRectangles (d->display, w->id, ShapeInput, 0, 0,
				     info->inputRects, info->nInputRects,
				     ShapeSet, info->inputRectOrdering);
	}
	else
	{
	    XShapeCombineMask (d->display, w->id, ShapeInput,
			       0, 0, None, ShapeSet);
	}

	if (info->inputRects)
	    XFree (info->inputRects);

	XShapeSelectInput (d->display, w->id, info->shapeMask);

	if (info->frameWindow)
	{
	    if (w->attrib.map_state != IsUnmapped)
		XMapWindow (d->display, w->frame);
	}

	changeWindowState (w,
			   (w->state & ~(CompWindowStateSkipPagerMask |
					 CompWindowStateSkipTaskbarMask)) |
			   info->skipState);

	free (info);
	gw->windowHideInfo = NULL;
    }
}

/*
 * groupTabBarTimeout
 *
 * Description:
 * This function is called when the time expired (== timeout).
 * We use this to realize a delay with the bar hiding after tab change.
 * groupHandleAnimation sets up a timer after the animation has finished.
 * This function itself basically just sets the tab bar to a PaintOff status
 * through calling groupSetTabBarVisibility.
 * The PERMANENT mask allows you to force hiding even of
 * PaintPermanentOn tab bars.
 *
 */
static Bool
groupTabBarTimeout (void *data)
{
    GroupSelection *group = (GroupSelection *) data;

    groupTabSetVisibility (group, FALSE, PERMANENT);

    group->tabBar->timeoutHandle = 0;

    return FALSE;	/* This will free the timer. */
}

/*
 * groupShowDelayTimeout
 *
 */
static Bool
groupShowDelayTimeout (void *data)
{
    int            mouseX, mouseY;
    GroupSelection *group = (GroupSelection *) data;
    CompScreen     *s = group->screen;
    CompWindow     *topTab;

    GROUP_SCREEN (s);

    if (!HAS_TOP_WIN (group))
    {
	gs->showDelayTimeoutHandle = 0;
	return FALSE;	/* This will free the timer. */
    }

    topTab = TOP_TAB (group);

    groupGetCurrentMousePosition (s, &mouseX, &mouseY);

    groupRecalcTabBarPos (group, mouseX, WIN_REAL_X (topTab),
			  WIN_REAL_X (topTab) + WIN_REAL_WIDTH (topTab));

    groupTabSetVisibility (group, TRUE, 0);

    gs->showDelayTimeoutHandle = 0;
    return FALSE;	/* This will free the timer. */
}

/*
 * groupTabSetVisibility
 *
 * Description:
 * This function is used to set the visibility of the tab bar.
 * The "visibility" is indicated through the PaintState, which
 * can be PaintOn, PaintOff, PaintFadeIn, PaintFadeOut
 * and PaintPermantOn.
 * Currently the mask paramater is mostely used for the PERMANENT mask.
 * This mask affects how the visible parameter is handled, for example if
 * visibule is set to TRUE and the mask to PERMANENT state it will set
 * PaintPermanentOn state for the tab bar. When visibile is FALSE, mask 0
 * and the current state of the tab bar is PaintPermanentOn it won't do
 * anything because its not strong enough to disable a
 * Permanent-State, for those you need the mask.
 *
 */
void
groupTabSetVisibility (GroupSelection *group,
		       Bool           visible,
		       unsigned int   mask)
{
    GroupTabBar *bar;
    CompWindow  *topTab;
    PaintState  oldState;
    CompScreen  *s;

    if (!group || !group->windows || !group->tabBar || !HAS_TOP_WIN (group))
	return;

    s = group->screen;
    bar = group->tabBar;
    topTab = TOP_TAB (group);
    oldState = bar->state;

    /* hide tab bars for invisible top windows */
    if ((topTab->state & CompWindowStateHiddenMask) || topTab->invisible)
    {
	bar->state = PaintOff;
	groupSwitchTopTabInput (group, TRUE);
    }
    else if (visible && bar->state != PaintPermanentOn && (mask & PERMANENT))
    {
	bar->state = PaintPermanentOn;
	groupSwitchTopTabInput (group, FALSE);
    }
    else if (visible && bar->state == PaintPermanentOn && !(mask & PERMANENT))
    {
	bar->state = PaintOn;
    }
    else if (visible && (bar->state == PaintOff || bar->state == PaintFadeOut))
    {
	if (groupGetBarAnimations (s))
	{
	    bar->bgAnimation = AnimationReflex;
	    bar->bgAnimationTime = groupGetReflexTime (s) * 1000.0;
	}
	bar->state = PaintFadeIn;
	groupSwitchTopTabInput (group, FALSE);
    }
    else if (!visible &&
	     (bar->state != PaintPermanentOn || (mask & PERMANENT)) &&
	     (bar->state == PaintOn || bar->state == PaintPermanentOn ||
	      bar->state == PaintFadeIn))
    {
	bar->state = PaintFadeOut;
	groupSwitchTopTabInput (group, TRUE);
    }

    if (bar->state == PaintFadeIn || bar->state == PaintFadeOut)
	bar->animationTime = (groupGetFadeTime (s) * 1000) - bar->animationTime;

    if (bar->state != oldState)
	groupDamageTabBarRegion (group);
}

/*
 * groupGetDrawOffsetForSlot
 *
 * Description:
 * Its used when the draggedSlot is dragged to another viewport.
 * It calculates a correct offset to the real slot position.
 *
 */
void
groupGetDrawOffsetForSlot (GroupTabBarSlot *slot,
			   int             *hoffset,
			   int             *voffset)
{
    CompWindow *w, *topTab;
    CompScreen *s;
    int        vx, vy, x, y;

    if (!slot || !slot->window)
	return;

    w = slot->window;
    s = w->screen;

    GROUP_WINDOW (w);
    GROUP_SCREEN (s);

    if (slot != gs->draggedSlot)
    {
	if (hoffset)
	    *hoffset = 0;
	if (voffset)
	    *voffset = 0;

	return;
    }

    if (HAS_TOP_WIN (gw->group))
	topTab = TOP_TAB (gw->group);
    else if (HAS_PREV_TOP_WIN (gw->group))
	topTab = PREV_TOP_TAB (gw->group);
    else
    {
	if (hoffset)
	    *hoffset = 0;
	if (voffset)
	    *voffset = 0;
	return;
    }

    x = WIN_CENTER_X (topTab) - WIN_WIDTH (w) / 2;
    y = WIN_CENTER_Y (topTab) - WIN_HEIGHT (w) / 2;

    viewportForGeometry (s, x, y, w->serverWidth, w->serverHeight,
			 w->serverBorderWidth, &vx, &vy);

    if (hoffset)
	*hoffset = ((s->x - vx) % s->hsize) * s->width;

    if (voffset)
	*voffset = ((s->y - vy) % s->vsize) * s->height;
}

/*
 * groupHandleHoverDetection
 *
 * Description:
 * This function is called from groupPreparePaintScreen to handle
 * the hover detection. This is needed for the text showing up,
 * when you hover a thumb on the thumb bar.
 *
 * FIXME: we should better have a timer for that ...
 */
void
groupHandleHoverDetection (GroupSelection *group)
{
    GroupTabBar *bar = group->tabBar;
    CompWindow  *topTab = TOP_TAB (group);
    int         mouseX, mouseY;
    Bool        mouseOnScreen, inLastSlot;

    /* first get the current mouse position */
    mouseOnScreen = groupGetCurrentMousePosition (group->screen,
						  &mouseX, &mouseY);

    if (!mouseOnScreen)
	return;

    /* then check if the mouse is in the last hovered slot --
       this saves a lot of CPU usage */
    inLastSlot = bar->hoveredSlot &&
	         XPointInRegion (bar->hoveredSlot->region, mouseX, mouseY);

    if (!inLastSlot)
    {
	Region          clip;
	GroupTabBarSlot *slot;

	bar->hoveredSlot = NULL;
	clip = groupGetClippingRegion (topTab);

	for (slot = bar->slots; slot; slot = slot->next)
	{
	    /* We need to clip the slot region with the clip region first.
	       This is needed to respect the window stack, so if a window
	       covers a port of that slot, this part won't be used
	       for in-slot-detection. */
	    Region reg = XCreateRegion();
	    if (!reg)
	    {
		XDestroyRegion(clip);
		return;
	    }

	    XSubtractRegion (slot->region, clip, reg);

	    if (XPointInRegion (reg, mouseX, mouseY))
	    {
		bar->hoveredSlot = slot;
		XDestroyRegion (reg);
		break;
	    }

	    XDestroyRegion (reg);
	}

	XDestroyRegion (clip);

	if (bar->textLayer)
	{
	    /* trigger a FadeOut of the text */
	    if ((bar->hoveredSlot != bar->textSlot) &&
		(bar->textLayer->state == PaintFadeIn ||
		 bar->textLayer->state == PaintOn))
	    {
		bar->textLayer->animationTime =
		    (groupGetFadeTextTime (group->screen) * 1000) -
		    bar->textLayer->animationTime;
		bar->textLayer->state = PaintFadeOut;
	    }

	    /* or trigger a FadeIn of the text */
	    else if (bar->textLayer->state == PaintFadeOut &&
		     bar->hoveredSlot == bar->textSlot && bar->hoveredSlot)
	    {
		bar->textLayer->animationTime =
		    (groupGetFadeTextTime (group->screen) * 1000) -
		    bar->textLayer->animationTime;
		bar->textLayer->state = PaintFadeIn;
	    }
	}
    }
}

/*
 * groupHandleTabBarFade
 *
 * Description:
 * This function is called from groupPreparePaintScreen
 * to handle the tab bar fade. It checks the animationTime and updates it,
 * so we can calculate the alpha of the tab bar in the painting code with it.
 *
 */
void
groupHandleTabBarFade (GroupSelection *group,
		       int            msSinceLastPaint)
{
    GroupTabBar *bar = group->tabBar;

    bar->animationTime -= msSinceLastPaint;

    if (bar->animationTime < 0)
	bar->animationTime = 0;

    /* Fade finished */
    if (bar->animationTime == 0)
    {
	if (bar->state == PaintFadeIn)
	{
	    bar->state = PaintOn;
	}
	else if (bar->state == PaintFadeOut)
	{
	    bar->state = PaintOff;

	    if (bar->textLayer)
	    {
		/* Tab-bar is no longer painted, clean up
		   text animation variables. */
		bar->textLayer->animationTime = 0;
		bar->textLayer->state = PaintOff;
		bar->textSlot = bar->hoveredSlot = NULL;

		groupRenderWindowTitle (group);
	    }
	}
    }
}

/*
 * groupHandleTextFade
 *
 * Description:
 * This function is called from groupPreparePaintScreen
 * to handle the text fade. It checks the animationTime and updates it,
 * so we can calculate the alpha of the text in the painting code with it.
 *
 */
void
groupHandleTextFade (GroupSelection *group,
		     int            msSinceLastPaint)
{
    GroupTabBar     *bar = group->tabBar;
    GroupCairoLayer *textLayer = bar->textLayer;

    /* Fade in progress... */
    if ((textLayer->state == PaintFadeIn || textLayer->state == PaintFadeOut) &&
	textLayer->animationTime > 0)
    {
	textLayer->animationTime -= msSinceLastPaint;

	if (textLayer->animationTime < 0)
	    textLayer->animationTime = 0;

	/* Fade has finished. */
	if (textLayer->animationTime == 0)
	{
	    if (textLayer->state == PaintFadeIn)
		textLayer->state = PaintOn;

	    else if (textLayer->state == PaintFadeOut)
		textLayer->state = PaintOff;
	}
    }

    if (textLayer->state == PaintOff && bar->hoveredSlot)
    {
	/* Start text animation for the new hovered slot. */
	bar->textSlot = bar->hoveredSlot;
	textLayer->state = PaintFadeIn;
	textLayer->animationTime =
	    (groupGetFadeTextTime (group->screen) * 1000);

	groupRenderWindowTitle (group);
    }

    else if (textLayer->state == PaintOff && bar->textSlot)
    {
	/* Clean Up. */
	bar->textSlot = NULL;
	groupRenderWindowTitle (group);
    }
}

/*
 * groupHandleTabBarAnimation
 *
 * Description: Handles the different animations for the tab bar defined in
 * GroupAnimationType. Basically that means this function updates
 * tabBar->animation->time as well as checking if the animation is already
 * finished.
 *
 */
void
groupHandleTabBarAnimation (GroupSelection *group,
			    int            msSinceLastPaint)
{
    GroupTabBar *bar = group->tabBar;

    bar->bgAnimationTime -= msSinceLastPaint;

    if (bar->bgAnimationTime <= 0)
    {
	bar->bgAnimationTime = 0;
	bar->bgAnimation = 0;

	groupRenderTabBarBackground (group);
    }
}

/*
 * groupTabChangeActivateEvent
 *
 * Description: Creates a compiz event to let other plugins know about
 * the starting and ending point of the tab changing animation
 */
static void
groupTabChangeActivateEvent (CompScreen *s,
			     Bool	    activating)
{
    CompOption o[2];

    o[0].type = CompOptionTypeInt;
    o[0].name = "root";
    o[0].value.i = s->root;

    o[1].type = CompOptionTypeBool;
    o[1].name = "active";
    o[1].value.b = activating;

    (*s->display->handleCompizEvent) (s->display,
				      "group", "tabChangeActivate", o, 2);
}

/*
 * groupHandleAnimation
 *
 * Description:
 * This function handles the change animation. It's called
 * from groupHandleChanges. Don't let the changeState
 * confuse you, PaintFadeIn equals with the start of the
 * rotate animation and PaintFadeOut is the end of these
 * animation.
 *
 */
void
groupHandleAnimation (GroupSelection *group)
{
    CompScreen *s = group->screen;

    if (group->changeState == TabChangeOldOut)
    {
	CompWindow      *top = TOP_TAB (group);
	Bool            activate;

	/* recalc here is needed (for y value)! */
	groupRecalcTabBarPos (group,
			      (group->tabBar->region->extents.x1 +
			       group->tabBar->region->extents.x2) / 2,
			      WIN_REAL_X (top),
			      WIN_REAL_X (top) + WIN_REAL_WIDTH (top));

	group->changeAnimationTime += groupGetChangeAnimationTime (s) * 500;

	if (group->changeAnimationTime <= 0)
	    group->changeAnimationTime = 0;

	group->changeState = TabChangeNewIn;

	activate = !group->checkFocusAfterTabChange;
	if (!activate)
	{
	    CompFocusResult focus;
	    focus    = allowWindowFocus (top, NO_FOCUS_MASK, s->x, s->y, 0);
	    activate = focus == CompFocusAllowed;
	}

	if (activate)
	    (*s->activateWindow) (top);

	group->checkFocusAfterTabChange = FALSE;
    }

    if (group->changeState == TabChangeNewIn &&
	group->changeAnimationTime <= 0)
    {
	int oldChangeAnimationTime = group->changeAnimationTime;

	groupTabChangeActivateEvent (s, FALSE);

	if (group->prevTopTab)
	    groupSetWindowVisibility (PREV_TOP_TAB (group), FALSE);

	group->prevTopTab = group->topTab;
	group->changeState = NoTabChange;

	if (group->nextTopTab)
	{
	    GroupTabBarSlot *next = group->nextTopTab;
	    group->nextTopTab = NULL;

	    groupChangeTab (next, group->nextDirection);

	    if (group->changeState == TabChangeOldOut)
	    {
		/* If a new animation was started. */
		group->changeAnimationTime += oldChangeAnimationTime;
	    }
	}

	if (group->changeAnimationTime <= 0)
	{
	    group->changeAnimationTime = 0;
	}
	else if (groupGetVisibilityTime (s) != 0.0f &&
		 group->changeState == NoTabChange)
	{
	    groupTabSetVisibility (group, TRUE,
				   PERMANENT | SHOW_BAR_INSTANTLY_MASK);

	    if (group->tabBar->timeoutHandle)
		compRemoveTimeout (group->tabBar->timeoutHandle);

	    group->tabBar->timeoutHandle =
		compAddTimeout (groupGetVisibilityTime (s) * 1000,
				groupGetVisibilityTime (s) * 1200,
				groupTabBarTimeout, group);
	}
    }
}

/* adjust velocity for each animation step (adapted from the scale plugin) */
static int
adjustTabVelocity (CompWindow *w)
{
    float dx, dy, adjust, amount;
    float x1, y1;

    GROUP_WINDOW (w);

    x1 = gw->destination.x;
    y1 = gw->destination.y;

    dx = x1 - (gw->orgPos.x + gw->tx);
    adjust = dx * 0.15f;
    amount = fabs (dx) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    gw->xVelocity = (amount * gw->xVelocity + adjust) / (amount + 1.0f);

    dy = y1 - (gw->orgPos.y + gw->ty);
    adjust = dy * 0.15f;
    amount = fabs (dy) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    gw->yVelocity = (amount * gw->yVelocity + adjust) / (amount + 1.0f);

    if (fabs (dx) < 0.1f && fabs (gw->xVelocity) < 0.2f &&
	fabs (dy) < 0.1f && fabs (gw->yVelocity) < 0.2f)
    {
	gw->xVelocity = gw->yVelocity = 0.0f;
	gw->tx = x1 - w->serverX;
	gw->ty = y1 - w->serverY;

	return 0;
    }
    return 1;
}

static void
groupFinishTabbing (GroupSelection *group)
{
    CompScreen *s = group->screen;
    int        i;

    GROUP_SCREEN (s);

    group->tabbingState = NoTabbing;
    groupTabChangeActivateEvent (s, FALSE);

    if (group->tabBar)
    {
	/* tabbing case - hide all non-toptab windows */
	GroupTabBarSlot *slot;

	for (slot = group->tabBar->slots; slot; slot = slot->next)
	{
	    CompWindow *w = slot->window;
	    if (!w)
		continue;

	    GROUP_WINDOW (w);

	    if (slot == group->topTab || (gw->animateState & IS_UNGROUPING))
		continue;

	    groupSetWindowVisibility (w, FALSE);
	}
	group->prevTopTab = group->topTab;
    }

    for (i = 0; i < group->nWins; i++)
    {
	CompWindow *w = group->windows[i];
	GROUP_WINDOW (w);

	/* move window to target position */
	gs->queued = TRUE;
	moveWindow (w, gw->destination.x - WIN_X (w),
		    gw->destination.y - WIN_Y (w), TRUE, TRUE);
	gs->queued = FALSE;
	syncWindowPosition (w);

	if (group->ungroupState == UngroupSingle &&
	    (gw->animateState & IS_UNGROUPING))
	{
	    groupRemoveWindowFromGroup (w);
	}

	gw->animateState = 0;
	gw->tx = gw->ty = gw->xVelocity = gw->yVelocity = 0.0f;
    }

    if (group->ungroupState == UngroupAll)
	groupDeleteGroup (group);
    else
	group->ungroupState = UngroupNone;
}

/*
 * groupDrawTabAnimation
 *
 * Description:
 * This function is called from groupPreparePaintScreen, to move
 * all the animated windows, with the required animation step.
 * The function goes through all grouped animated windows, calculates
 * the required step using adjustTabVelocity, moves the window,
 * and then checks if the animation is finished for that window.
 *
 */
void
groupDrawTabAnimation (GroupSelection *group,
		       int            msSinceLastPaint)
{
    int        steps, i;
    float      amount, chunk;
    Bool       doTabbing;
    CompScreen *s = group->screen;

    amount = msSinceLastPaint * 0.05f * groupGetTabbingSpeed (s);
    steps = amount / (0.5f * groupGetTabbingTimestep (s));
    if (!steps)
	steps = 1;
    chunk = amount / (float)steps;

    while (steps--)
    {
	doTabbing = FALSE;

	for (i = 0; i < group->nWins; i++)
	{
	    CompWindow *cw = group->windows[i];
	    if (!cw)
		continue;

	    GROUP_WINDOW (cw);

	    if (!(gw->animateState & IS_ANIMATED))
		continue;

	    if (!adjustTabVelocity (cw))
	    {
		gw->animateState |= FINISHED_ANIMATION;
		gw->animateState &= ~IS_ANIMATED;
	    }

	    gw->tx += gw->xVelocity * chunk;
	    gw->ty += gw->yVelocity * chunk;

	    doTabbing |= (gw->animateState & IS_ANIMATED);
	}

	if (!doTabbing)
	{
	    /* tabbing animation finished */
	    groupFinishTabbing (group);
	    break;
	}
    }
}

/*
 * groupUpdateTabBars
 *
 * Description:
 * This function is responsible for showing / unshowing the tab-bars,
 * when the title-bars / tab-bars are hovered.
 * The function is called whenever a new window is entered,
 * checks if the entered window is a window frame (and if the title
 * bar part of that frame was hovered) or if it was the input
 * prevention window of a tab bar, and sets tab-bar visibility
 * according to that.
 *
 */
void
groupUpdateTabBars (CompScreen *s,
		    Window     enteredWin)
{
    CompWindow     *w = NULL;
    GroupSelection *hoveredGroup = NULL;

    GROUP_SCREEN (s);

    /* do nothing if the screen is grabbed, as the frame might be drawn
       transformed */
    if (!otherScreenGrabExist (s, "group", "group-drag", NULL))
    {
	/* first check if the entered window is a frame */
	for (w = s->windows; w; w = w->next)
	{
	    if (w->frame == enteredWin)
		break;
	}
    }

    if (w)
    {
	/* is the window the entered frame belongs to inside
	   a tabbed group? if no, it's not interesting for us */
	GROUP_WINDOW (w);

	if (gw->group && gw->group->tabBar)
	{
	    int mouseX, mouseY;
	    /* it is grouped and tabbed, so now we have to
	       check if we hovered the title bar or the frame */
	    if (groupGetCurrentMousePosition (s, &mouseX, &mouseY))
	    {
		XRectangle rect;
		Region     reg = XCreateRegion();
		if (!reg)
		    return;

		rect.x      = WIN_X (w) - w->input.left;
		rect.y      = WIN_Y (w) - w->input.top;
		rect.width  = WIN_WIDTH (w) + w->input.right;
		rect.height = WIN_Y (w) - rect.y;
		XUnionRectWithRegion (&rect, reg, reg);

		if (XPointInRegion (reg, mouseX, mouseY))
		    hoveredGroup = gw->group;

		XDestroyRegion (reg);
	    }
	}
    }

    /* if we didn't hover a title bar, check if we hovered
       a tab bar (means: input prevention window) */
    if (!hoveredGroup)
    {
	GroupSelection *group;

	for (group = gs->groups; group; group = group->next)
	{
	    if (group->inputPrevention == enteredWin)
	    {
		/* only accept it if the IPW is mapped */
		if (group->ipwMapped)
		{
		    hoveredGroup = group;
		    break;
		}
	    }
	}
    }

    /* if we found a hovered tab bar different than the last one
       (or left a tab bar), hide the old one */
    if (gs->lastHoveredGroup && (hoveredGroup != gs->lastHoveredGroup))
	groupTabSetVisibility (gs->lastHoveredGroup, FALSE, 0);

    /* if we entered a tab bar (or title bar), show the tab bar */
    if (hoveredGroup && HAS_TOP_WIN (hoveredGroup) &&
	!TOP_TAB (hoveredGroup)->grabbed)
    {
	GroupTabBar *bar = hoveredGroup->tabBar;

	if (bar && ((bar->state == PaintOff) || (bar->state == PaintFadeOut)))
	{
	    int showDelayTime = groupGetTabbarShowDelay (s) * 1000;

	    /* Show the tab-bar after a delay,
	       only if the tab-bar wasn't fading out. */
	    if (showDelayTime > 0 && (bar->state == PaintOff))
	    {
		if (gs->showDelayTimeoutHandle)
		    compRemoveTimeout (gs->showDelayTimeoutHandle);
		gs->showDelayTimeoutHandle =
		    compAddTimeout (showDelayTime, (float) showDelayTime * 1.2,
				    groupShowDelayTimeout, hoveredGroup);
	    }
	    else
		groupShowDelayTimeout (hoveredGroup);
	}
    }

    gs->lastHoveredGroup = hoveredGroup;
}

/*
 * groupGetConstrainRegion
 *
 */
static Region
groupGetConstrainRegion (CompScreen *s)
{
    CompWindow *w;
    Region     region;
    REGION     r;
    int        i;

    region = XCreateRegion ();
    if (!region)
	return NULL;

    for (i = 0;i < s->nOutputDev; i++)
	XUnionRegion (&s->outputDev[i].region, region, region);

    r.rects    = &r.extents;
    r.numRects = r.size = 1;

    for (w = s->windows; w; w = w->next)
    {
	if (!w->mapNum)
	    continue;

	if (w->struts)
	{
	    r.extents.x1 = w->struts->top.x;
	    r.extents.y1 = w->struts->top.y;
	    r.extents.x2 = r.extents.x1 + w->struts->top.width;
	    r.extents.y2 = r.extents.y1 + w->struts->top.height;

	    XSubtractRegion (region, &r, region);

	    r.extents.x1 = w->struts->bottom.x;
	    r.extents.y1 = w->struts->bottom.y;
	    r.extents.x2 = r.extents.x1 + w->struts->bottom.width;
	    r.extents.y2 = r.extents.y1 + w->struts->bottom.height;

	    XSubtractRegion (region, &r, region);

	    r.extents.x1 = w->struts->left.x;
	    r.extents.y1 = w->struts->left.y;
	    r.extents.x2 = r.extents.x1 + w->struts->left.width;
	    r.extents.y2 = r.extents.y1 + w->struts->left.height;

	    XSubtractRegion (region, &r, region);

	    r.extents.x1 = w->struts->right.x;
	    r.extents.y1 = w->struts->right.y;
	    r.extents.x2 = r.extents.x1 + w->struts->right.width;
	    r.extents.y2 = r.extents.y1 + w->struts->right.height;

	    XSubtractRegion (region, &r, region);
	}
    }

    return region;
}

/*
 * groupConstrainMovement
 *
 */
static Bool
groupConstrainMovement (CompWindow *w,
			Region     constrainRegion,
			int        dx,
			int        dy,
			int        *new_dx,
			int        *new_dy)
{
    int status, xStatus;
    int origDx = dx, origDy = dy;
    int x, y, width, height;

    GROUP_WINDOW (w);

    if (!gw->group)
	return FALSE;

    if (!dx && !dy)
	return FALSE;

    x = gw->orgPos.x - w->input.left + dx;
    y = gw->orgPos.y - w->input.top + dy;
    width = WIN_REAL_WIDTH (w);
    height = WIN_REAL_HEIGHT (w);

    status = XRectInRegion (constrainRegion, x, y, width, height);

    xStatus = status;
    while (dx && (xStatus != RectangleIn))
    {
	xStatus = XRectInRegion (constrainRegion, x, y - dy, width, height);

	if (xStatus != RectangleIn)
	    dx += (dx < 0) ? 1 : -1;

	x = gw->orgPos.x - w->input.left + dx;
    }

    while (dy && (status != RectangleIn))
    {
	status = XRectInRegion(constrainRegion, x, y, width, height);

	if (status != RectangleIn)
	    dy += (dy < 0) ? 1 : -1;

	y = gw->orgPos.y - w->input.top + dy;
    }

    if (new_dx)
	*new_dx = dx;

    if (new_dy)
	*new_dy = dy;

    return ((dx != origDx) || (dy != origDy));
}

/*
 * groupApplyConstraining
 *
 */
static void
groupApplyConstraining (GroupSelection *group,
			Region         constrainRegion,
			Window         constrainedWindow,
			int            dx,
			int            dy)
{
    int        i;
    CompWindow *w;

    if (!dx && !dy)
	return;

    for (i = 0; i < group->nWins; i++)
    {
	w = group->windows[i];
	GROUP_WINDOW (w);

	/* ignore certain windows: we don't want to apply the constraining
	   results on the constrained window itself, nor do we want to
	   change the target position of unamimated windows and of
	   windows which already are constrained */
	if (w->id == constrainedWindow)
	    continue;

	if (!(gw->animateState & IS_ANIMATED))
	    continue;

	if (gw->animateState & DONT_CONSTRAIN)
	    continue;

	if (!(gw->animateState & CONSTRAINED_X))
	{
	    gw->animateState |= IS_ANIMATED;

	    /* applying the constraining result of another window
	       might move the window offscreen, too, so check
	       if this is not the case */
	    if (groupConstrainMovement (w, constrainRegion, dx, 0, &dx, NULL))
		gw->animateState |= CONSTRAINED_X;

	    gw->destination.x += dx;
	}

	if (!(gw->animateState & CONSTRAINED_Y))
	{
	    gw->animateState |= IS_ANIMATED;

	    /* analog to X case */
	    if (groupConstrainMovement (w, constrainRegion, 0, dy, NULL, &dy))
		gw->animateState |= CONSTRAINED_Y;

	    gw->destination.y += dy;
	}
    }
}

/*
 * groupStartTabbingAnimation
 *
 */
void
groupStartTabbingAnimation (GroupSelection *group,
			    Bool           tab)
{
    CompScreen *s;
    int        i;
    int        dx, dy;
    int        constrainStatus;

    if (!group || (group->tabbingState != NoTabbing))
	return;

    s = group->screen;
    group->tabbingState = (tab) ? Tabbing : Untabbing;
    groupTabChangeActivateEvent (s, TRUE);

    if (!tab)
    {
	/* we need to set up the X/Y constraining on untabbing */
	Region constrainRegion = groupGetConstrainRegion (s);
	Bool   constrainedWindows = TRUE;

	if (!constrainRegion)
	    return;

	/* reset all flags */
	for (i = 0; i < group->nWins; i++)
	{
	    GROUP_WINDOW (group->windows[i]);
	    gw->animateState &= ~(CONSTRAINED_X | CONSTRAINED_Y |
				  DONT_CONSTRAIN);
	}

	/* as we apply the constraining in a flat loop,
	   we may need to run multiple times through this
	   loop until all constraining dependencies are met */
	while (constrainedWindows)
	{
	    constrainedWindows = FALSE;
	    /* loop through all windows and try to constrain their
	       animation path (going from gw->orgPos to
	       gw->destination) to the active screen area */
	    for (i = 0; i < group->nWins; i++)
	    {
		CompWindow *w = group->windows[i];
		GROUP_WINDOW (w);

		/* ignore windows which aren't animated and/or
		   already are at the edge of the screen area */
		if (!(gw->animateState & IS_ANIMATED))
		    continue;

		if (gw->animateState & DONT_CONSTRAIN)
		    continue;

		/* is the original position inside the screen area? */
		constrainStatus = XRectInRegion (constrainRegion,
						 gw->orgPos.x  - w->input.left,
						 gw->orgPos.y - w->input.top,
						 WIN_REAL_WIDTH (w),
						 WIN_REAL_HEIGHT (w));

		/* constrain the movement */
		if (groupConstrainMovement (w, constrainRegion,
					    gw->destination.x - gw->orgPos.x,
					    gw->destination.y - gw->orgPos.y,
					    &dx, &dy))
		{
		    /* handle the case where the window is outside the screen
		       area on its whole animation path */
		    if (constrainStatus != RectangleIn && !dx && !dy)
		    {
			gw->animateState |= DONT_CONSTRAIN;
			gw->animateState |= CONSTRAINED_X | CONSTRAINED_Y;

			/* use the original position as last resort */
			gw->destination.x = gw->mainTabOffset.x;
			gw->destination.y = gw->mainTabOffset.y;
		    }
		    else
		    {
			/* if we found a valid target position, apply
			   the change also to other windows to retain
			   the distance between the windows */
			groupApplyConstraining (group, constrainRegion, w->id,
						dx - gw->destination.x +
						gw->orgPos.x,
						dy - gw->destination.y +
						gw->orgPos.y);

			/* if we hit constraints, adjust the mask and the
			   target position accordingly */
			if (dx != (gw->destination.x - gw->orgPos.x))
			{
			    gw->animateState |= CONSTRAINED_X;
			    gw->destination.x = gw->orgPos.x + dx;
			}

			if (dy != (gw->destination.y - gw->orgPos.y))
			{
			    gw->animateState |= CONSTRAINED_Y;
			    gw->destination.y = gw->orgPos.y + dy;
			}

			constrainedWindows = TRUE;
		    }
		}
	    }
	}
	XDestroyRegion (constrainRegion);
    }
}

/*
 * groupTabGroup
 *
 */
void
groupTabGroup (CompWindow *main)
{
    GroupSelection  *group;
    GroupTabBarSlot *slot;
    CompScreen      *s = main->screen;
    int             width, height;
    int             space, thumbSize;

    GROUP_WINDOW (main);

    group = gw->group;
    if (!group || group->tabBar)
	return;

    if (!s->display->shapeExtension)
    {
	compLogMessage ("group", CompLogLevelError,
			"No X shape extension! Tabbing disabled.");
	return;
    }

    groupInitTabBar (group, main);
    if (!group->tabBar)
	return;

    groupCreateInputPreventionWindow (group);

    group->tabbingState = NoTabbing;
    /* Slot is initialized after groupInitTabBar(group); */
    groupChangeTab (gw->slot, RotateUncertain);
    groupRecalcTabBarPos (gw->group, WIN_CENTER_X (main),
			  WIN_X (main), WIN_X (main) + WIN_WIDTH (main));

    width = group->tabBar->region->extents.x2 -
	    group->tabBar->region->extents.x1;
    height = group->tabBar->region->extents.y2 -
	     group->tabBar->region->extents.y1;

    group->tabBar->textLayer = groupCreateCairoLayer (s, width, height);
    if (group->tabBar->textLayer)
    {
	GroupCairoLayer *layer;

	layer = group->tabBar->textLayer;
	layer->state = PaintOff;
	layer->animationTime = 0;
	groupRenderWindowTitle (group);
    }
    if (group->tabBar->textLayer)
    {
	GroupCairoLayer *layer;

	layer = group->tabBar->textLayer;
	layer->animationTime = groupGetFadeTextTime (s) * 1000;
	layer->state = PaintFadeIn;
    }

    /* we need a buffer for DnD here */
    space = groupGetThumbSpace (s);
    thumbSize = groupGetThumbSize (s);
    group->tabBar->bgLayer = groupCreateCairoLayer (s,
						    width + space + thumbSize,
						    height);
    if (group->tabBar->bgLayer)
    {
	group->tabBar->bgLayer->state = PaintOn;
	group->tabBar->bgLayer->animationTime = 0;
	groupRenderTabBarBackground (group);
    }

    width = group->topTab->region->extents.x2 -
	    group->topTab->region->extents.x1;
    height = group->topTab->region->extents.y2 -
	     group->topTab->region->extents.y1;

    group->tabBar->selectionLayer = groupCreateCairoLayer (s, width, height);
    if (group->tabBar->selectionLayer)
    {
	group->tabBar->selectionLayer->state = PaintOn;
	group->tabBar->selectionLayer->animationTime = 0;
	groupRenderTopTabHighlight (group);
    }

    if (!HAS_TOP_WIN (group))
	return;

    for (slot = group->tabBar->slots; slot; slot = slot->next)
    {
	CompWindow *cw = slot->window;

	GROUP_WINDOW (cw);

	if (gw->animateState & (IS_ANIMATED | FINISHED_ANIMATION))
	    moveWindow (cw,
			gw->destination.x - WIN_X (cw),
			gw->destination.y - WIN_Y (cw),
			FALSE, TRUE);

	/* center the window to the main window */
	gw->destination.x = WIN_CENTER_X (main) - (WIN_WIDTH (cw) / 2);
	gw->destination.y = WIN_CENTER_Y (main) - (WIN_HEIGHT (cw) / 2);

	/* Distance from destination. */
	gw->mainTabOffset.x = WIN_X (cw) - gw->destination.x;
	gw->mainTabOffset.y = WIN_Y (cw) - gw->destination.y;

	if (gw->tx || gw->ty)
	{
	    gw->tx -= (WIN_X (cw) - gw->orgPos.x);
	    gw->ty -= (WIN_Y (cw) - gw->orgPos.y);
	}

	gw->orgPos.x = WIN_X (cw);
	gw->orgPos.y = WIN_Y (cw);

	gw->animateState = IS_ANIMATED;
	gw->xVelocity = gw->yVelocity = 0.0f;
    }

    groupStartTabbingAnimation (group, TRUE);
}

/*
 * groupUntabGroup
 *
 */
void
groupUntabGroup (GroupSelection *group)
{
    int             oldX, oldY;
    CompWindow      *prevTopTab;
    GroupTabBarSlot *slot;

    if (!HAS_TOP_WIN (group))
	return;

    GROUP_SCREEN (group->screen);

    if (group->prevTopTab)
	prevTopTab = PREV_TOP_TAB (group);
    else
    {
	/* If prevTopTab isn't set, we have no choice but using topTab.
	   It happens when there is still animation, which
	   means the tab wasn't changed anyway. */
	prevTopTab = TOP_TAB (group);
    }

    group->lastTopTab = TOP_TAB (group);
    group->topTab = NULL;

    for (slot = group->tabBar->slots; slot; slot = slot->next)
    {
	CompWindow *cw = slot->window;

	GROUP_WINDOW (cw);

	if (gw->animateState & (IS_ANIMATED | FINISHED_ANIMATION))
	{
	    gs->queued = TRUE;
	    moveWindow (cw,
			gw->destination.x - WIN_X (cw),
			gw->destination.y - WIN_Y (cw),
			FALSE, TRUE);
	    gs->queued = FALSE;
	}
	groupSetWindowVisibility (cw, TRUE);

	/* save the old original position - we might need it
	   if constraining fails */
	oldX = gw->orgPos.x;
	oldY = gw->orgPos.y;

	gw->orgPos.x = WIN_CENTER_X (prevTopTab) - WIN_WIDTH (cw) / 2;
	gw->orgPos.y = WIN_CENTER_Y (prevTopTab) - WIN_HEIGHT (cw) / 2;

	gw->destination.x = gw->orgPos.x + gw->mainTabOffset.x;
	gw->destination.y = gw->orgPos.y + gw->mainTabOffset.y;

	if (gw->tx || gw->ty)
	{
	    gw->tx -= (gw->orgPos.x - oldX);
	    gw->ty -= (gw->orgPos.y - oldY);
	}

	gw->mainTabOffset.x = oldX;
	gw->mainTabOffset.y = oldY;

	gw->animateState = IS_ANIMATED;
	gw->xVelocity = gw->yVelocity = 0.0f;
    }

    group->tabbingState = NoTabbing;
    groupStartTabbingAnimation (group, FALSE);

    groupDeleteTabBar (group);
    group->changeAnimationTime = 0;
    group->changeState = NoTabChange;
    group->nextTopTab = NULL;
    group->prevTopTab = NULL;

    damageScreen (group->screen);
}

/*
 * groupChangeTab
 *
 */
Bool
groupChangeTab (GroupTabBarSlot             *topTab,
		ChangeTabAnimationDirection direction)
{
    CompWindow     *w, *oldTopTab;
    GroupSelection *group;
    CompScreen     *s;

    if (!topTab)
	return TRUE;

    w = topTab->window;
    s = w->screen;

    GROUP_WINDOW (w);

    group = gw->group;

    if (!group || group->tabbingState != NoTabbing)
	return TRUE;

    if (group->changeState == NoTabChange && group->topTab == topTab)
	return TRUE;

    if (group->changeState != NoTabChange && group->nextTopTab == topTab)
	return TRUE;

    oldTopTab = group->topTab ? group->topTab->window : NULL;

    if (group->changeState != NoTabChange)
	group->nextDirection = direction;
    else if (direction == RotateLeft)
	group->changeAnimationDirection = 1;
    else if (direction == RotateRight)
	group->changeAnimationDirection = -1;
    else
    {
	int             distanceOld = 0, distanceNew = 0;
	GroupTabBarSlot *slot;

	if (group->topTab)
	    for (slot = group->tabBar->slots; slot && (slot != group->topTab);
		 slot = slot->next, distanceOld++);

	for (slot = group->tabBar->slots; slot && (slot != topTab);
	     slot = slot->next, distanceNew++);

	if (distanceNew < distanceOld)
	    group->changeAnimationDirection = 1;   /*left */
	else
	    group->changeAnimationDirection = -1;  /* right */

	/* check if the opposite direction is shorter */
	if (abs (distanceNew - distanceOld) > (group->tabBar->nSlots / 2))
	    group->changeAnimationDirection *= -1;
    }

    if (group->changeState != NoTabChange)
    {
	if (group->prevTopTab == topTab)
	{
	    /* Reverse animation. */
	    GroupTabBarSlot *tmp = group->topTab;
	    group->topTab = group->prevTopTab;
	    group->prevTopTab = tmp;

	    group->changeAnimationDirection *= -1;
	    group->changeAnimationTime =
		groupGetChangeAnimationTime (s) * 500 -
		group->changeAnimationTime;
	    group->changeState = (group->changeState == TabChangeOldOut) ?
		TabChangeNewIn : TabChangeOldOut;

	    group->nextTopTab = NULL;
	}
	else
	    group->nextTopTab = topTab;
    }
    else
    {
	group->topTab = topTab;

	groupRenderWindowTitle (group);
	groupRenderTopTabHighlight (group);
	if (oldTopTab)
	    addWindowDamage (oldTopTab);
	addWindowDamage (w);
    }

    if (topTab != group->nextTopTab)
    {
	groupSetWindowVisibility (w, TRUE);
	if (oldTopTab)
	{
	    int dx, dy;

	    GROUP_SCREEN (s);

	    dx = WIN_CENTER_X (oldTopTab) - WIN_CENTER_X (w);
	    dy = WIN_CENTER_Y (oldTopTab) - WIN_CENTER_Y (w);

	    gs->queued = TRUE;
	    moveWindow (w, dx, dy, FALSE, TRUE);
	    syncWindowPosition (w);
	    gs->queued = FALSE;
	}

	if (HAS_PREV_TOP_WIN (group))
	{
	    /* we use only the half time here -
	       the second half will be PaintFadeOut */
	    group->changeAnimationTime =
		groupGetChangeAnimationTime (s) * 500;
	    groupTabChangeActivateEvent (s, TRUE);
	    group->changeState = TabChangeOldOut;
	}
	else
	{
	    Bool activate;

	    /* No window to do animation with. */
	    if (HAS_TOP_WIN (group))
		group->prevTopTab = group->topTab;
	    else
		group->prevTopTab = NULL;

	    activate = !group->checkFocusAfterTabChange;
	    if (!activate)
	    {
		CompFocusResult focus;

		focus    = allowWindowFocus (w, NO_FOCUS_MASK, s->x, s->y, 0);
		activate = focus == CompFocusAllowed;
	    }

	    if (activate)
		(*s->activateWindow) (w);

	    group->checkFocusAfterTabChange = FALSE;
	}
    }

    return TRUE;
}

/*
 * groupRecalcSlotPos
 *
 */
static void
groupRecalcSlotPos (GroupTabBarSlot *slot,
		    int             slotPos)
{
    GroupSelection *group;
    XRectangle     box;
    int            space, thumbSize;

    GROUP_WINDOW (slot->window);
    group = gw->group;

    if (!HAS_TOP_WIN (group) || !group->tabBar)
	return;

    space = groupGetThumbSpace (slot->window->screen);
    thumbSize = groupGetThumbSize (slot->window->screen);

    EMPTY_REGION (slot->region);

    box.x = space + ((thumbSize + space) * slotPos);
    box.y = space;

    box.width = thumbSize;
    box.height = thumbSize;

    XUnionRectWithRegion (&box, slot->region, slot->region);
}

/*
 * groupRecalcTabBarPos
 *
 */
void
groupRecalcTabBarPos (GroupSelection *group,
		      int            middleX,
		      int            minX1,
		      int            maxX2)
{
    GroupTabBarSlot *slot;
    GroupTabBar     *bar;
    CompWindow      *topTab;
    Bool            isDraggedSlotGroup = FALSE;
    int             space, barWidth;
    int             thumbSize;
    int             tabsWidth = 0, tabsHeight = 0;
    int             currentSlot;
    XRectangle      box;

    if (!HAS_TOP_WIN (group) || !group->tabBar)
	return;

    GROUP_SCREEN (group->screen);

    bar = group->tabBar;
    topTab = TOP_TAB (group);
    space = groupGetThumbSpace (group->screen);

    /* calculate the space which the tabs need */
    for (slot = bar->slots; slot; slot = slot->next)
    {
	if (slot == gs->draggedSlot && gs->dragged)
	{
	    isDraggedSlotGroup = TRUE;
	    continue;
	}

	tabsWidth += (slot->region->extents.x2 - slot->region->extents.x1);
	if ((slot->region->extents.y2 - slot->region->extents.y1) > tabsHeight)
	    tabsHeight = slot->region->extents.y2 - slot->region->extents.y1;
    }

    /* just a little work-a-round for first call
       FIXME: remove this! */
    thumbSize = groupGetThumbSize (group->screen);
    if (bar->nSlots && tabsWidth <= 0)
    {
	/* first call */
	tabsWidth = thumbSize * bar->nSlots;

	if (bar->nSlots && tabsHeight < thumbSize)
	{
	    /* we need to do the standard height too */
	    tabsHeight = thumbSize;
	}

	if (isDraggedSlotGroup)
	    tabsWidth -= thumbSize;
    }

    barWidth = space * (bar->nSlots + 1) + tabsWidth;

    if (isDraggedSlotGroup)
    {
	/* 1 tab is missing, so we have 1 less border */
	barWidth -= space;
    }

    if (maxX2 - minX1 < barWidth)
	box.x = (maxX2 + minX1) / 2 - barWidth / 2;
    else if (middleX - barWidth / 2 < minX1)
	box.x = minX1;
    else if (middleX + barWidth / 2 > maxX2)
	box.x = maxX2 - barWidth;
    else
	box.x = middleX - barWidth / 2;

    box.y = WIN_Y (topTab);
    box.width = barWidth;
    box.height = space * 2 + tabsHeight;

    groupResizeTabBarRegion (group, &box, TRUE);

    /* recalc every slot region */
    currentSlot = 0;
    for (slot = bar->slots; slot; slot = slot->next)
    {
	if (slot == gs->draggedSlot && gs->dragged)
	    continue;

	groupRecalcSlotPos (slot, currentSlot);
	XOffsetRegion (slot->region,
		       bar->region->extents.x1,
		       bar->region->extents.y1);

	slot->springX = (slot->region->extents.x1 +
			 slot->region->extents.x2) / 2;
	slot->speed = 0;
	slot->msSinceLastMove = 0;

	currentSlot++;
    }

    bar->leftSpringX = box.x;
    bar->rightSpringX = box.x + box.width;

    bar->rightSpeed = 0;
    bar->leftSpeed = 0;

    bar->rightMsSinceLastMove = 0;
    bar->leftMsSinceLastMove = 0;
}

void
groupDamageTabBarRegion (GroupSelection *group)
{
    REGION reg;

    reg.rects = &reg.extents;
    reg.numRects = 1;

    /* we use 15 pixels as damage buffer here, as there is a 10 pixel wide
       border around the selected slot which also needs to be damaged
       properly - however the best way would be if slot->region was
       sized including the border */

#define DAMAGE_BUFFER 20

    reg.extents = group->tabBar->region->extents;

    if (group->tabBar->slots)
    {
	reg.extents.x1 = MIN (reg.extents.x1,
			      group->tabBar->slots->region->extents.x1);
	reg.extents.y1 = MIN (reg.extents.y1,
			      group->tabBar->slots->region->extents.y1);
	reg.extents.x2 = MAX (reg.extents.x2,
			      group->tabBar->revSlots->region->extents.x2);
	reg.extents.y2 = MAX (reg.extents.y2,
			      group->tabBar->revSlots->region->extents.y2);
    }

    reg.extents.x1 -= DAMAGE_BUFFER;
    reg.extents.y1 -= DAMAGE_BUFFER;
    reg.extents.x2 += DAMAGE_BUFFER;
    reg.extents.y2 += DAMAGE_BUFFER;

    damageScreenRegion (group->screen, &reg);
}

void
groupMoveTabBarRegion (GroupSelection *group,
		       int            dx,
		       int            dy,
		       Bool           syncIPW)
{
    groupDamageTabBarRegion (group);

    XOffsetRegion (group->tabBar->region, dx, dy);

    if (syncIPW)
	XMoveWindow (group->screen->display->display,
		     group->inputPrevention,
		     group->tabBar->leftSpringX,
		     group->tabBar->region->extents.y1);

    groupDamageTabBarRegion (group);
}

void
groupResizeTabBarRegion (GroupSelection *group,
			 XRectangle     *box,
			 Bool           syncIPW)
{
    int oldWidth;

    groupDamageTabBarRegion (group);

    oldWidth = group->tabBar->region->extents.x2 -
	group->tabBar->region->extents.x1;

    if (group->tabBar->bgLayer && oldWidth != box->width && syncIPW)
    {
	group->tabBar->bgLayer =
	    groupRebuildCairoLayer (group->screen,
				    group->tabBar->bgLayer,
				    box->width +
				    groupGetThumbSpace (group->screen) +
				    groupGetThumbSize (group->screen),
				    box->height);
	groupRenderTabBarBackground (group);

	/* invalidate old width */
	group->tabBar->oldWidth = 0;
    }

    EMPTY_REGION (group->tabBar->region);
    XUnionRectWithRegion (box, group->tabBar->region, group->tabBar->region);

    if (syncIPW)
    {
	XWindowChanges xwc;

	xwc.x = box->x;
	xwc.y = box->y;
	xwc.width = box->width;
	xwc.height = box->height;

	xwc.stack_mode = Above;
	xwc.sibling = HAS_TOP_WIN (group) ? TOP_TAB (group)->id : None;

	XConfigureWindow (group->screen->display->display,
			  group->inputPrevention,
			  CWSibling | CWStackMode | CWX | CWY |
			  CWWidth | CWHeight,
			  &xwc);
    }

    groupDamageTabBarRegion (group);
}

/*
 * groupInsertTabBarSlotBefore
 *
 */
void
groupInsertTabBarSlotBefore (GroupTabBar     *bar,
			     GroupTabBarSlot *slot,
			     GroupTabBarSlot *nextSlot)
{
    GroupTabBarSlot *prev = nextSlot->prev;
    CompWindow      *w = slot->window;

    GROUP_WINDOW (w);

    if (prev)
    {
	slot->prev = prev;
	prev->next = slot;
    }
    else
    {
	bar->slots = slot;
	slot->prev = NULL;
    }

    slot->next = nextSlot;
    nextSlot->prev = slot;
    bar->nSlots++;

    /* Moving bar->region->extents.x1 / x2 as minX1 / maxX2 will work,
       because the tab-bar got wider now, so it will put it in
       the average between them, which is
       (bar->region->extents.x1 + bar->region->extents.x2) / 2 anyway. */
    groupRecalcTabBarPos (gw->group,
			  (bar->region->extents.x1 +
			   bar->region->extents.x2) / 2,
			  bar->region->extents.x1, bar->region->extents.x2);
}

/*
 * groupInsertTabBarSlotAfter
 *
 */
void
groupInsertTabBarSlotAfter (GroupTabBar     *bar,
			    GroupTabBarSlot *slot,
			    GroupTabBarSlot *prevSlot)
{
    GroupTabBarSlot *next = prevSlot->next;
    CompWindow      *w = slot->window;

    GROUP_WINDOW (w);

    if (next)
    {
	slot->next = next;
	next->prev = slot;
    }
    else
    {
	bar->revSlots = slot;
	slot->next = NULL;
    }

    slot->prev = prevSlot;
    prevSlot->next = slot;
    bar->nSlots++;

    /* Moving bar->region->extents.x1 / x2 as minX1 / maxX2 will work,
       because the tab-bar got wider now, so it will put it in the
       average between them, which is
       (bar->region->extents.x1 + bar->region->extents.x2) / 2 anyway. */
    groupRecalcTabBarPos (gw->group,
			  (bar->region->extents.x1 +
			   bar->region->extents.x2) / 2,
			  bar->region->extents.x1, bar->region->extents.x2);
}

/*
 * groupInsertTabBarSlot
 *
 */
void
groupInsertTabBarSlot (GroupTabBar     *bar,
		       GroupTabBarSlot *slot)
{
    CompWindow *w = slot->window;

    GROUP_WINDOW (w);

    if (bar->slots)
    {
	bar->revSlots->next = slot;
	slot->prev = bar->revSlots;
	slot->next = NULL;
    }
    else
    {
	slot->prev = NULL;
	slot->next = NULL;
	bar->slots = slot;
    }

    bar->revSlots = slot;
    bar->nSlots++;

    /* Moving bar->region->extents.x1 / x2 as minX1 / maxX2 will work,
       because the tab-bar got wider now, so it will put it in
       the average between them, which is
       (bar->region->extents.x1 + bar->region->extents.x2) / 2 anyway. */
    groupRecalcTabBarPos (gw->group,
			  (bar->region->extents.x1 +
			   bar->region->extents.x2) / 2,
			  bar->region->extents.x1, bar->region->extents.x2);
}

/*
 * groupUnhookTabBarSlot
 *
 */
void
groupUnhookTabBarSlot (GroupTabBar     *bar,
		       GroupTabBarSlot *slot,
		       Bool            temporary)
{
    GroupTabBarSlot *tempSlot;
    GroupTabBarSlot *prev = slot->prev;
    GroupTabBarSlot *next = slot->next;
    CompWindow      *w = slot->window;
    CompScreen      *s = w->screen;
    GroupSelection  *group;

    GROUP_WINDOW (w);

    group = gw->group;

    /* check if slot is not already unhooked */
    for (tempSlot = bar->slots; tempSlot; tempSlot = tempSlot->next)
	if (tempSlot == slot)
	    break;

    if (!tempSlot)
	return;

    if (prev)
	prev->next = next;
    else
	bar->slots = next;

    if (next)
	next->prev = prev;
    else
	bar->revSlots = prev;

    slot->prev = NULL;
    slot->next = NULL;
    bar->nSlots--;

    if (!temporary)
    {
	if (IS_PREV_TOP_TAB (w, group))
	    group->prevTopTab = NULL;
	if (IS_TOP_TAB (w, group))
	{
	    group->topTab = NULL;

	    if (next)
		groupChangeTab (next, RotateRight);
	    else if (prev)
		groupChangeTab (prev, RotateLeft);

	    if (groupGetUntabOnClose (s))
		groupUntabGroup (group);
	}
    }

    if (slot == bar->hoveredSlot)
	bar->hoveredSlot = NULL;

    if (slot == bar->textSlot)
    {
	bar->textSlot = NULL;

	if (bar->textLayer)
	{
	    if (bar->textLayer->state == PaintFadeIn ||
		bar->textLayer->state == PaintOn)
	    {
		bar->textLayer->animationTime =
		    (groupGetFadeTextTime (s) * 1000) -
		    bar->textLayer->animationTime;
		bar->textLayer->state = PaintFadeOut;
	    }
	}
    }

    /* Moving bar->region->extents.x1 / x2 as minX1 / maxX2 will work,
       because the tab-bar got thiner now, so
       (bar->region->extents.x1 + bar->region->extents.x2) / 2
       Won't cause the new x1 / x2 to be outside the original region. */
    groupRecalcTabBarPos (group,
			  (bar->region->extents.x1 +
			   bar->region->extents.x2) / 2,
			  bar->region->extents.x1,
			  bar->region->extents.x2);
}

/*
 * groupDeleteTabBarSlot
 *
 */
void
groupDeleteTabBarSlot (GroupTabBar     *bar,
		       GroupTabBarSlot *slot)
{
    CompWindow *w = slot->window;

    GROUP_WINDOW (w);
    GROUP_SCREEN (w->screen);

    groupUnhookTabBarSlot (bar, slot, FALSE);

    if (slot->region)
	XDestroyRegion (slot->region);

    if (slot == gs->draggedSlot)
    {
	gs->draggedSlot = NULL;
	gs->dragged = FALSE;

	if (gs->grabState == ScreenGrabTabDrag)
	    groupGrabScreen (w->screen, ScreenGrabNone);
    }

    gw->slot = NULL;
    groupUpdateWindowProperty (w);
    free (slot);
}

/*
 * groupCreateSlot
 *
 */
void groupCreateSlot (GroupSelection *group,
		      CompWindow     *w)
{
    GroupTabBarSlot *slot;

    GROUP_WINDOW (w);

    if (!group->tabBar)
	return;

    slot = malloc (sizeof (GroupTabBarSlot));
    if (!slot)
        return;

    slot->window = w;

    slot->region = XCreateRegion ();

    groupInsertTabBarSlot (group->tabBar, slot);
    gw->slot = slot;
    groupUpdateWindowProperty (w);
}

#define SPRING_K     groupGetDragSpringK(s)
#define FRICTION     groupGetDragFriction(s)
#define SIZE	     groupGetThumbSize(s)
#define BORDER	     groupGetBorderRadius(s)
#define Y_START_MOVE groupGetDragYDistance(s)
#define SPEED_LIMIT  groupGetDragSpeedLimit(s)

/*
 * groupSpringForce
 *
 */
static inline int
groupSpringForce (CompScreen *s,
		  int        centerX,
		  int        springX)
{
    /* Each slot has a spring attached to it, starting at springX,
       and ending at the center of the slot (centerX).
       The spring will cause the slot to move, using the
       well-known physical formula F = k * dl... */
    return -SPRING_K * (centerX - springX);
}

/*
 * groupDraggedSlotForce
 *
 */
static int
groupDraggedSlotForce (CompScreen *s,
		       int        distanceX,
		       int        distanceY)
{
    /* The dragged slot will make the slot move, to get
       DnD animations (slots will make room for the newly inserted slot).
       As the dragged slot is closer to the slot, it will put
       more force on the slot, causing it to make room for the dragged slot...
       But if the dragged slot gets too close to the slot, they are
       going to be reordered soon, so the force will get lower.

       If the dragged slot is in the other side of the slot,
       it will have to make force in the opposite direction.

       So we the needed funtion is an odd function that goes
       up at first, and down after that.
       Sinus is a function like that... :)

       The maximum is got when x = (x1 + x2) / 2,
       in this case: x = SIZE + BORDER.
       Because of that, for x = SIZE + BORDER,
       we get a force of SPRING_K * (SIZE + BORDER) / 2.
       That equals to the force we get from the the spring.
       This way, the slot won't move when its distance from
       the dragged slot is SIZE + BORDER (which is the default
       distance between slots).
       */

    /* The maximum value */
    float a = SPRING_K * (SIZE + BORDER) / 2;
    /* This will make distanceX == 2 * (SIZE + BORDER) to get 0,
       and distanceX == (SIZE + BORDER) to get the maximum. */
    float b = PI /  (2 * SIZE + 2 * BORDER);

    /* If there is some distance between the slots in the y axis,
       the slot should get less force... For this, we change max
       to a lower value, using a simple linear function. */

    if (distanceY < Y_START_MOVE)
	a *= 1.0f - (float)distanceY / Y_START_MOVE;
    else
	a = 0;

    if (abs (distanceX) < 2 * (SIZE + BORDER))
	return a * sin (b * distanceX);
    else
	return 0;
}

/*
 * groupApplyFriction
 *
 */
static inline void
groupApplyFriction (CompScreen *s,
		    int        *speed)
{
    if (abs (*speed) < FRICTION)
	*speed = 0;
    else if (*speed > 0)
	*speed -= FRICTION;
    else if (*speed < 0)
	*speed += FRICTION;
}

/*
 * groupApplySpeedLimit
 *
 */
static inline void
groupApplySpeedLimit (CompScreen *s,
		      int        *speed)
{
    if (*speed > SPEED_LIMIT)
	*speed = SPEED_LIMIT;
    else if (*speed < -SPEED_LIMIT)
	*speed = -SPEED_LIMIT;
}

/*
 * groupApplyForces
 *
 */
void
groupApplyForces (CompScreen      *s,
		  GroupTabBar     *bar,
		  GroupTabBarSlot *draggedSlot)
{
    GroupTabBarSlot *slot, *slot2;
    int             centerX, centerY;
    int             draggedCenterX, draggedCenterY;

    if (draggedSlot)
    {
	int vx, vy;

	groupGetDrawOffsetForSlot (draggedSlot, &vx, &vy);

	draggedCenterX = ((draggedSlot->region->extents.x1 +
			   draggedSlot->region->extents.x2) / 2) + vx;
	draggedCenterY = ((draggedSlot->region->extents.y1 +
			   draggedSlot->region->extents.y2) / 2) + vy;
    }
    else
    {
	draggedCenterX = 0;
	draggedCenterY = 0;
    }

    bar->leftSpeed += groupSpringForce(s,
				       bar->region->extents.x1,
				       bar->leftSpringX);
    bar->rightSpeed += groupSpringForce(s,
					bar->region->extents.x2,
					bar->rightSpringX);

    if (draggedSlot)
    {
	int leftForce, rightForce;

	leftForce = groupDraggedSlotForce(s,
					  bar->region->extents.x1 -
					  SIZE / 2 - draggedCenterX,
					  abs ((bar->region->extents.y1 +
						bar->region->extents.y2) / 2 -
					       draggedCenterY));

	rightForce = groupDraggedSlotForce (s,
					    bar->region->extents.x2 +
					    SIZE / 2 - draggedCenterX,
					    abs ((bar->region->extents.y1 +
						  bar->region->extents.y2) / 2 -
						 draggedCenterY));

	if (leftForce < 0)
	    bar->leftSpeed += leftForce;
	if (rightForce > 0)
	    bar->rightSpeed += rightForce;
    }

    for (slot = bar->slots; slot; slot = slot->next)
    {
	centerX = (slot->region->extents.x1 + slot->region->extents.x2) / 2;
	centerY = (slot->region->extents.y1 + slot->region->extents.y2) / 2;

	slot->speed += groupSpringForce (s, centerX, slot->springX);

	if (draggedSlot && draggedSlot != slot)
	{
	    int draggedSlotForce;
	    draggedSlotForce =
		groupDraggedSlotForce(s, centerX - draggedCenterX,
				      abs (centerY - draggedCenterY));

	    slot->speed += draggedSlotForce;
	    slot2 = NULL;

	    if (draggedSlotForce < 0)
	    {
		slot2 = slot->prev;
		bar->leftSpeed += draggedSlotForce;
	    }
	    else if (draggedSlotForce > 0)
	    {
		slot2 = slot->next;
		bar->rightSpeed += draggedSlotForce;
	    }

	    while (slot2)
	    {
		if (slot2 != draggedSlot)
		    slot2->speed += draggedSlotForce;

		slot2 = (draggedSlotForce < 0) ? slot2->prev : slot2->next;
	    }
	}
    }

    for (slot = bar->slots; slot; slot = slot->next)
    {
	groupApplyFriction (s, &slot->speed);
	groupApplySpeedLimit (s, &slot->speed);
    }

    groupApplyFriction (s, &bar->leftSpeed);
    groupApplySpeedLimit (s, &bar->leftSpeed);

    groupApplyFriction (s, &bar->rightSpeed);
    groupApplySpeedLimit (s, &bar->rightSpeed);
}

/*
 * groupApplySpeeds
 *
 */
void
groupApplySpeeds (CompScreen     *s,
		  GroupSelection *group,
		  int            msSinceLastRepaint)
{
    GroupTabBar     *bar = group->tabBar;
    GroupTabBarSlot *slot;
    int             move;
    XRectangle      box;
    Bool            updateTabBar = FALSE;

    box.x = bar->region->extents.x1;
    box.y = bar->region->extents.y1;
    box.width = bar->region->extents.x2 - bar->region->extents.x1;
    box.height = bar->region->extents.y2 - bar->region->extents.y1;

    bar->leftMsSinceLastMove += msSinceLastRepaint;
    bar->rightMsSinceLastMove += msSinceLastRepaint;

    /* Left */
    move = bar->leftSpeed * bar->leftMsSinceLastMove / 1000;
    if (move)
    {
	box.x += move;
	box.width -= move;

	bar->leftMsSinceLastMove = 0;
	updateTabBar = TRUE;
    }
    else if (bar->leftSpeed == 0 &&
	     bar->region->extents.x1 != bar->leftSpringX &&
	     (SPRING_K * abs (bar->region->extents.x1 - bar->leftSpringX) <
	      FRICTION))
    {
	/* Friction is preventing from the left border to get
	   to its original position. */
	box.x += bar->leftSpringX - bar->region->extents.x1;
	box.width -= bar->leftSpringX - bar->region->extents.x1;

	bar->leftMsSinceLastMove = 0;
	updateTabBar = TRUE;
    }
    else if (bar->leftSpeed == 0)
	bar->leftMsSinceLastMove = 0;

    /* Right */
    move = bar->rightSpeed * bar->rightMsSinceLastMove / 1000;
    if (move)
    {
	box.width += move;

	bar->rightMsSinceLastMove = 0;
	updateTabBar = TRUE;
    }
    else if (bar->rightSpeed == 0 &&
	     bar->region->extents.x2 != bar->rightSpringX &&
	     (SPRING_K * abs (bar->region->extents.x2 - bar->rightSpringX) <
	      FRICTION))
    {
	/* Friction is preventing from the right border to get
	   to its original position. */
	box.width += bar->leftSpringX - bar->region->extents.x1;

	bar->leftMsSinceLastMove = 0;
	updateTabBar = TRUE;
    }
    else if (bar->rightSpeed == 0)
	bar->rightMsSinceLastMove = 0;

    if (updateTabBar)
	groupResizeTabBarRegion (group, &box, FALSE);

    for (slot = bar->slots; slot; slot = slot->next)
    {
	int slotCenter;

	slot->msSinceLastMove += msSinceLastRepaint;
	move = slot->speed * slot->msSinceLastMove / 1000;
	slotCenter = (slot->region->extents.x1 +
		      slot->region->extents.x2) / 2;

	if (move)
	{
	    XOffsetRegion (slot->region, move, 0);
	    slot->msSinceLastMove = 0;
	}
	else if (slot->speed == 0 &&
		 slotCenter != slot->springX &&
		 SPRING_K * abs (slotCenter - slot->springX) < FRICTION)
	{
	    /* Friction is preventing from the slot to get
	       to its original position. */

	    XOffsetRegion (slot->region, slot->springX - slotCenter, 0);
	    slot->msSinceLastMove = 0;
	}
	else if (slot->speed == 0)
	    slot->msSinceLastMove = 0;
    }
}

/*
 * groupInitTabBar
 *
 */
void
groupInitTabBar (GroupSelection *group,
		 CompWindow     *topTab)
{
    GroupTabBar *bar;
    int         i;

    if (group->tabBar)
	return;

    bar = malloc (sizeof (GroupTabBar));
    if (!bar)
	return;

    bar->slots = NULL;
    bar->nSlots = 0;
    bar->bgAnimation = AnimationNone;
    bar->bgAnimationTime = 0;
    bar->state = PaintOff;
    bar->animationTime = 0;
    bar->timeoutHandle = 0;
    bar->textLayer = NULL;
    bar->bgLayer = NULL;
    bar->selectionLayer = NULL;
    bar->hoveredSlot = NULL;
    bar->textSlot = NULL;
    bar->oldWidth = 0;
    group->tabBar = bar;

    bar->region = XCreateRegion ();

    for (i = 0; i < group->nWins; i++)
	groupCreateSlot (group, group->windows[i]);

    groupRecalcTabBarPos (group, WIN_CENTER_X (topTab),
			  WIN_X (topTab), WIN_X (topTab) + WIN_WIDTH (topTab));
}

/*
 * groupDeleteTabBar
 *
 */
void
groupDeleteTabBar (GroupSelection *group)
{
    GroupTabBar *bar = group->tabBar;

    groupDestroyCairoLayer (group->screen, bar->textLayer);
    groupDestroyCairoLayer (group->screen, bar->bgLayer);
    groupDestroyCairoLayer (group->screen, bar->selectionLayer);

    groupDestroyInputPreventionWindow (group);

    if (bar->timeoutHandle)
	compRemoveTimeout (bar->timeoutHandle);

    while (bar->slots)
	groupDeleteTabBarSlot (bar, bar->slots);

    if (bar->region)
	XDestroyRegion (bar->region);

    free (bar);
    group->tabBar = NULL;
}

/*
 * groupInitTab
 *
 */
Bool
groupInitTab (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    Window     xid;
    CompWindow *w;
    Bool       allowUntab = TRUE;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w   = findWindowAtDisplay (d, xid);
    if (!w)
	return TRUE;

    GROUP_WINDOW (w);

    if (gw->inSelection)
    {
	groupGroupWindows (d, action, state, option, nOption);
	/* If the window was selected, we don't want to
	   untab the group, because the user probably
	   wanted to tab the selected windows. */
	allowUntab = FALSE;
    }

    if (!gw->group)
	return TRUE;

    if (!gw->group->tabBar)
	groupTabGroup (w);
    else if (allowUntab)
	groupUntabGroup (gw->group);

    damageScreen (w->screen);

    return TRUE;
}

/*
 * groupChangeTabLeft
 *
 */
Bool
groupChangeTabLeft (CompDisplay     *d,
		    CompAction      *action,
		    CompActionState state,
		    CompOption      *option,
		    int             nOption)
{
    Window     xid;
    CompWindow *w, *topTab;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w   = topTab = findWindowAtDisplay (d, xid);
    if (!w)
	return TRUE;

    GROUP_WINDOW (w);
    GROUP_SCREEN (w->screen);

    if (!gw->slot || !gw->group)
	return TRUE;

    if (gw->group->nextTopTab)
	topTab = NEXT_TOP_TAB (gw->group);
    else if (gw->group->topTab)
    {
	/* If there are no tabbing animations,
	   topTab is never NULL. */
	topTab = TOP_TAB (gw->group);
    }

    gw = GET_GROUP_WINDOW (topTab, gs);

    if (gw->slot->prev)
	return groupChangeTab (gw->slot->prev, RotateLeft);
    else
	return groupChangeTab (gw->group->tabBar->revSlots, RotateLeft);
}

/*
 * groupChangeTabRight
 *
 */
Bool
groupChangeTabRight (CompDisplay     *d,
		     CompAction      *action,
		     CompActionState state,
		     CompOption      *option,
		     int             nOption)
{
    Window     xid;
    CompWindow *w, *topTab;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w   = topTab = findWindowAtDisplay (d, xid);
    if (!w)
	return TRUE;

    GROUP_WINDOW (w);
    GROUP_SCREEN (w->screen);

    if (!gw->slot || !gw->group)
	return TRUE;

    if (gw->group->nextTopTab)
	topTab = NEXT_TOP_TAB (gw->group);
    else if (gw->group->topTab)
    {
	/* If there are no tabbing animations,
	   topTab is never NULL. */
	topTab = TOP_TAB (gw->group);
    }

    gw = GET_GROUP_WINDOW (topTab, gs);

    if (gw->slot->next)
	return groupChangeTab (gw->slot->next, RotateRight);
    else
	return groupChangeTab (gw->group->tabBar->slots, RotateRight);
}

/*
 * groupSwitchTopTabInput
 *
 */
void
groupSwitchTopTabInput (GroupSelection *group,
			Bool           enable)
{
    if (!group->tabBar || !HAS_TOP_WIN (group))
	return;

    if (!group->inputPrevention)
	groupCreateInputPreventionWindow (group);

    if (!enable)
	XMapWindow (group->screen->display->display,
		    group->inputPrevention);
    else
	XUnmapWindow (group->screen->display->display,
		      group->inputPrevention);

    group->ipwMapped = !enable;
}

/*
 * groupCreateInputPreventionWindow
 *
 */
void
groupCreateInputPreventionWindow (GroupSelection *group)
{
    if (!group->inputPrevention)
    {
	XSetWindowAttributes attrib;
	attrib.override_redirect = TRUE;

	group->inputPrevention =
	    XCreateWindow (group->screen->display->display,
			   group->screen->root, -100, -100, 1, 1, 0,
			   CopyFromParent, InputOnly,
			   CopyFromParent, CWOverrideRedirect, &attrib);
	group->ipwMapped = FALSE;
    }
}

/*
 * groupDestroyInputPreventionWindow
 *
 */
void
groupDestroyInputPreventionWindow (GroupSelection *group)
{
    if (group->inputPrevention)
    {
	XDestroyWindow (group->screen->display->display,
			group->inputPrevention);

	group->inputPrevention = None;
	group->ipwMapped = TRUE;
    }
}
