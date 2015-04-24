/*
 * Animation plugin for compiz/beryl
 *
 * animation.c
 *
 * Copyright : (C) 2006 Erkin Bahceci
 * E-mail    : erkinbah@gmail.com
 *
 * Based on Wobbly and Minimize plugins by
 *           : David Reveman
 * E-mail    : davidr@novell.com>
 *
 * Particle system added by : (C) 2006 Dennis Kasprzyk
 * E-mail                   : onestone@beryl-project.org
 *
 * Beam-Up added by : Florencio Guimaraes
 * E-mail           : florencio@nexcorp.com.br
 *
 * Hexagon tessellator added by : Mike Slegeir
 * E-mail                       : mikeslegeir@mail.utexas.edu>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "animation-internal.h"

// =====================  Effect: Dodge  =========================

static void
fxDodgeProcessSubject (CompWindow *wCur,
		       Region wRegion,
		       Region dodgeRegion,
		       Bool alwaysInclude)
{
    XRectangle rect;
    rect.x = WIN_X(wCur);
    rect.y = WIN_Y(wCur);
    rect.width = WIN_W(wCur);
    rect.height = WIN_H(wCur);
    Region wCurRegion = XCreateRegion();
    if (!wCurRegion)
	return;

    XUnionRectWithRegion(&rect, &emptyRegion, wCurRegion);
    if (!alwaysInclude)
    {
	Region intersectionRegion = XCreateRegion();
	if (intersectionRegion)
	{
	    XIntersectRegion(wRegion, wCurRegion,
			     intersectionRegion);
	    if (!XEmptyRegion(intersectionRegion))
		XUnionRegion(dodgeRegion, wCurRegion, dodgeRegion);
	    XDestroyRegion (intersectionRegion);
	}
    }
    else
	XUnionRegion(dodgeRegion, wCurRegion, dodgeRegion);

    XDestroyRegion (wCurRegion);
}

// Returns FALSE if the subject is destroyed or if there was an error when
// calculating the dodge box
static Bool
fxDodgeFindDodgeBox (CompWindow *w, XRectangle *dodgeBox)
{
    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    if (!aw->dodgeSubjectWin)  // if the subject is destroyed
	return FALSE;

    // Find the box to be dodged, it can contain multiple windows
    // when there are dialog/utility windows of subject windows
    // (stacked in the moreToBePaintedNext chain)
    // Then this would be a bounding box of the subject windows
    // intersecting with dodger.
    Region wRegion = XCreateRegion();
    if (!wRegion)
	return FALSE;

    Region dodgeRegion = XCreateRegion();
    if (!dodgeRegion)
    {
	XDestroyRegion (wRegion);
	return FALSE;
    }

    XRectangle rect;
    rect.x = WIN_X(w);
    rect.y = WIN_Y(w);
    rect.width = WIN_W(w);
    rect.height = WIN_H(w);

    int dodgeMaxAmount = (int)aw->dodgeMaxAmount;

    // to compute if subject(s) intersect with dodger w,
    // enlarge dodger window's box so that it encloses all of the covered
    // region during dodge movement. This corrects the animation when
    // there are >1 subjects (a window with its dialog/utility windows).
    switch (aw->dodgeDirection)
    {
    case 0:
	rect.y += dodgeMaxAmount;
	rect.height -= dodgeMaxAmount;
	break;
    case 1:
	rect.height += dodgeMaxAmount;
	break;
    case 2:
	rect.x += dodgeMaxAmount;
	rect.width -= dodgeMaxAmount;
	break;
    case 3:
	rect.width += dodgeMaxAmount;
	break;
    }
    XUnionRectWithRegion(&rect, &emptyRegion, wRegion);

    AnimWindow *awCur;
    CompWindow *wCur = aw->dodgeSubjectWin;
    for (; wCur; wCur = awCur->moreToBePaintedNext)
    {
	fxDodgeProcessSubject(wCur, wRegion, dodgeRegion,
			      wCur == aw->dodgeSubjectWin);
	awCur = GET_ANIM_WINDOW(wCur, as);
	if (!awCur)
	    break;
    }

    AnimWindow *awSubj = GET_ANIM_WINDOW(aw->dodgeSubjectWin, as);
    wCur = awSubj->moreToBePaintedPrev;
    for (; wCur; wCur = awCur->moreToBePaintedPrev)
    {
	fxDodgeProcessSubject(wCur, wRegion, dodgeRegion, FALSE);
	awCur = GET_ANIM_WINDOW(wCur, as);
	if (!awCur)
	    break;
    }

    XClipBox(dodgeRegion, dodgeBox);

    XDestroyRegion (wRegion);
    XDestroyRegion (dodgeRegion);

    return TRUE;
}

static void
applyDodgeTransform (CompWindow * w, CompTransform *transform)
{
    ANIM_WINDOW(w);

    if (aw->isDodgeSubject)
	return;

    float amount = sin(M_PI * aw->com.transformProgress) * aw->dodgeMaxAmount;

    if (aw->dodgeDirection > 1) // if x axis
	matrixTranslate (transform, amount, 0.0f, 0.0f);
    else
	matrixTranslate (transform, 0.0f, amount, 0.0f);
}

void
fxDodgeAnimStep (CompWindow *w, float time)
{
    XRectangle dodgeBox;

    defaultAnimStep (w, time);

    ANIM_WINDOW(w);

    aw->com.transformProgress = 0;

    float forwardProgress = defaultAnimProgress (w);
    if (forwardProgress > aw->com.transformStartProgress)
    {
	aw->com.transformProgress = 
	    (forwardProgress - aw->com.transformStartProgress) /
	    (1 - aw->com.transformStartProgress);
    }

    if (!aw->isDodgeSubject &&
	aw->com.transformProgress <= 0.5f &&
	fxDodgeFindDodgeBox (w, &dodgeBox))
    {
	// Update dodge amount if subject window has moved during dodge
	float newDodgeAmount =
	    DODGE_AMOUNT_BOX(dodgeBox, w, aw->dodgeDirection);

	// Only update if amount got larger
	if (((newDodgeAmount > 0 && aw->dodgeMaxAmount > 0) ||
	     (newDodgeAmount < 0 && aw->dodgeMaxAmount < 0)) &&
	    abs(newDodgeAmount) > abs(aw->dodgeMaxAmount))
	{
	    aw->dodgeMaxAmount = newDodgeAmount;
	}
    }

    matrixGetIdentity (&aw->com.transform);
    applyDodgeTransform (w, &aw->com.transform);
}

void
fxDodgeUpdateWindowTransform (CompWindow *w,
			      CompTransform *wTransform)
{
    ANIM_WINDOW(w);

    if (aw->isDodgeSubject)
	return;

    applyTransform (wTransform, &aw->com.transform);
}

void
fxDodgePostPreparePaintScreen (CompWindow *w)
{
    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    // Only dodge subjects should be processed here
    if (!aw->isDodgeSubject)
	return;

    if (!aw->restackInfo)
	return;

    if (aw->skipPostPrepareScreen)
	return;

    // Dodgy window
    CompWindow *dw;
    AnimWindow *adw = NULL;
    for (dw = aw->dodgeChainStart; dw; dw = adw->dodgeChainNext)
    {
	adw = GET_ANIM_WINDOW(dw, as);
	if (!adw)
	    break;
	// find the first dodging window that hasn't yet
	// reached 50% progress yet. The subject window should be
	// painted right behind that one (or right in front of it if
	// the subject window is being lowered).
	if (!(adw->com.transformProgress > 0.5f))
	    break;
    }
    AnimWindow *awOldHost = NULL;
	
    if (aw->restackInfo->raised &&
	dw != aw->winThisIsPaintedBefore) // w's host is changing
    {
	if (aw->winThisIsPaintedBefore)
	{
	    // Clear old host
	    awOldHost = GET_ANIM_WINDOW(aw->winThisIsPaintedBefore, as);			
	    awOldHost->winToBePaintedBeforeThis = NULL;
	}
	if (dw && adw) // if a dodgy win. is still at <0.5 progress
	{
	    // Put subject right behind adw (new host)
	    adw->winToBePaintedBeforeThis = w;
	}
	// otherwise all dodgy win.s have passed 0.5 progress

	CompWindow *wCur = w;
	while (wCur)
	{
	    AnimWindow *awCur = GET_ANIM_WINDOW(wCur, as);
	    awCur->winThisIsPaintedBefore = dw; // dw can be null, which is ok
	    wCur = awCur->moreToBePaintedNext;
	}
    }
    else if (!aw->restackInfo->raised)
    {
	// Put subject right in front of dw
	// But we need to find the dodgy window above dw
	// (since we need to put subject *behind* another one)

	CompWindow *wDodgeChainAbove = NULL;

	if (dw && adw) // if a dodgy win. is still at <0.5 progress
	{
	    if (adw->dodgeChainPrev)
		wDodgeChainAbove = adw->dodgeChainPrev;
	    else
		wDodgeChainAbove = aw->restackInfo->wOldAbove;

	    if (!wDodgeChainAbove)
		compLogMessage ("animation", CompLogLevelError,
				"%s: error at line %d", __FILE__, __LINE__);
	    else if (aw->winThisIsPaintedBefore !=
		     wDodgeChainAbove) // w's host is changing
	    {
		AnimWindow *adw2 = GET_ANIM_WINDOW(wDodgeChainAbove, as);

		// Put subject right behind adw2 (new host)
		adw2->winToBePaintedBeforeThis = w;
	    }
	}
	if (aw->winThisIsPaintedBefore &&
	    aw->winThisIsPaintedBefore != wDodgeChainAbove)
	{
	    awOldHost = GET_ANIM_WINDOW(aw->winThisIsPaintedBefore, as);

	    // Clear old host
	    awOldHost->winToBePaintedBeforeThis = NULL;
	}
	// otherwise all dodgy win.s have passed 0.5 progress

	// wDodgeChainAbove can be null, which is ok
	aw->winThisIsPaintedBefore = wDodgeChainAbove;
    }
}

void
fxDodgeUpdateBB (CompOutput *output,
		 CompWindow * w,
		 Box *BB)
{
    ANIM_WINDOW(w);

    if (!aw->isDodgeSubject)
	compTransformUpdateBB (output, w, BB);
}
