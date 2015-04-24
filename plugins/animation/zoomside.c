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

// =====================  Effect: Zoom and Sidekick  =========================

Bool
fxSidekickInit (CompWindow * w)
{
    ANIM_WINDOW(w);

    // determine number of rotations randomly in [0.9, 1.1] range
    aw->numZoomRotations =
	animGetF (w, ANIM_SCREEN_OPTION_SIDEKICK_NUM_ROTATIONS) *
	(1.0f + 0.2f * rand() / RAND_MAX - 0.1f);

    float winCenterX = WIN_X(w) + WIN_W(w) / 2.0;
    float iconCenterX = aw->com.icon.x + aw->com.icon.width / 2.0;

    // if window is to the right of icon, rotate clockwise instead
    // to make rotation look more pleasant
    if (winCenterX > iconCenterX)
	aw->numZoomRotations *= -1;

    return fxZoomInit (w);
}

static float
fxZoomGetSpringiness (CompWindow *w)
{
    ANIM_WINDOW(w);

    if (aw->com.curAnimEffect == AnimEffectZoom)
	return 2 * animGetF (w, ANIM_SCREEN_OPTION_ZOOM_SPRINGINESS);
    else if (aw->com.curAnimEffect == AnimEffectSidekick)
	return 1.6 * animGetF (w, ANIM_SCREEN_OPTION_SIDEKICK_SPRINGINESS);
    else
	return 0.0f;
}

Bool
fxZoomInit (CompWindow * w)
{
    ANIM_WINDOW(w);

    if ((aw->com.curAnimEffect == AnimEffectSidekick &&
	 (animGetI (w, ANIM_SCREEN_OPTION_SIDEKICK_ZOOM_FROM_CENTER) ==
	  ZoomFromCenterOn ||
	  ((aw->com.curWindowEvent == WindowEventMinimize ||
	    aw->com.curWindowEvent == WindowEventUnminimize) &&
	   animGetI (w, ANIM_SCREEN_OPTION_SIDEKICK_ZOOM_FROM_CENTER) ==
	   ZoomFromCenterMin) ||
	  ((aw->com.curWindowEvent == WindowEventOpen ||
	    aw->com.curWindowEvent == WindowEventClose) &&
	   animGetI (w, ANIM_SCREEN_OPTION_SIDEKICK_ZOOM_FROM_CENTER) ==
	   ZoomFromCenterCreate))) ||
	(aw->com.curAnimEffect == AnimEffectZoom &&
	 (animGetI (w, ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER) ==
	  ZoomFromCenterOn ||
	  ((aw->com.curWindowEvent == WindowEventMinimize ||
	    aw->com.curWindowEvent == WindowEventUnminimize) &&
	   animGetI (w, ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER) ==
	   ZoomFromCenterMin) ||
	  ((aw->com.curWindowEvent == WindowEventOpen ||
	    aw->com.curWindowEvent == WindowEventClose) &&
	   animGetI (w, ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER) ==
	   ZoomFromCenterCreate))))
    {
	aw->com.icon.x =
	    WIN_X(w) + WIN_W(w) / 2 - aw->com.icon.width / 2;
	aw->com.icon.y =
	    WIN_Y(w) + WIN_H(w) / 2 - aw->com.icon.height / 2;
    }

    // allow extra time for spring damping / deceleration
    if ((aw->com.curWindowEvent == WindowEventUnminimize ||
	 aw->com.curWindowEvent == WindowEventOpen) &&
	fxZoomGetSpringiness (w) > 1e-4)
    {
	aw->com.animTotalTime /= SPRINGY_ZOOM_PERCEIVED_T;
    }
    else if ((aw->com.curAnimEffect == AnimEffectZoom ||
	      aw->com.curAnimEffect == AnimEffectSidekick) &&
	     (aw->com.curWindowEvent == WindowEventOpen ||
	      aw->com.curWindowEvent == WindowEventClose))
    {
	aw->com.animTotalTime /= NONSPRINGY_ZOOM_PERCEIVED_T;
    }
    else
    {
	aw->com.animTotalTime /= ZOOM_PERCEIVED_T;
    }
    aw->com.animRemainingTime = aw->com.animTotalTime;

    aw->com.usingTransform = TRUE;

    return defaultAnimInit (w);
}

void fxZoomAnimProgress (CompWindow *w,
			 float *moveProgress,
			 float *scaleProgress,
			 Bool neverSpringy)
{
    ANIM_WINDOW(w);

    float forwardProgress =
	1 - aw->com.animRemainingTime /
	(aw->com.animTotalTime - aw->com.timestep);
    forwardProgress = MIN(forwardProgress, 1);
    forwardProgress = MAX(forwardProgress, 0);

    float x = forwardProgress;
    Bool backwards = FALSE;
    int animProgressDir = 1;

    if (aw->com.curWindowEvent == WindowEventUnminimize ||
	aw->com.curWindowEvent == WindowEventOpen)
	animProgressDir = 2;
    if (aw->com.animOverrideProgressDir != 0)
	animProgressDir = aw->com.animOverrideProgressDir;
    if ((animProgressDir == 1 &&
	 (aw->com.curWindowEvent == WindowEventUnminimize ||
	  aw->com.curWindowEvent == WindowEventOpen)) ||
	(animProgressDir == 2 &&
	 (aw->com.curWindowEvent == WindowEventMinimize ||
	  aw->com.curWindowEvent == WindowEventClose)))
	backwards = TRUE;
    if (backwards)
	x = 1 - x;

    float dampBase = (pow(1-pow(x,1.2)*0.5,10)-pow(0.5,10))/(1-pow(0.5,10));
    float nonSpringyProgress =
	1 - pow(decelerateProgressCustom(1 - x, .5f, .8f), 1.7f);

    if (moveProgress && scaleProgress)
    {
	float damping =
	    pow(dampBase, 0.5);

	float damping2 =
	    ((pow(1-(pow(x,0.7)*0.5),10)-pow(0.5,10))/(1-pow(0.5,10))) *
	    0.7 + 0.3;
	float springiness = 0;

	// springy only when appearing
	if ((aw->com.curWindowEvent == WindowEventUnminimize ||
	     aw->com.curWindowEvent == WindowEventOpen) &&
	    !neverSpringy)
	{
	    springiness = fxZoomGetSpringiness (w);
	}
		
	float springyMoveProgress =
	    cos(2*M_PI*pow(x,1)*1.25) * damping * damping2;

	if (springiness > 1e-4f)
	{
	    if (x > 0.2)
	    {
		springyMoveProgress *= springiness;
	    }
	    else
	    {
		// interpolate between (springyMoveProgress * springiness)
		// and springyMoveProgress for smooth transition at 0.2
		// (where it crosses y=0)
		float progressUpto02 = x / 0.2f;
		springyMoveProgress =
		    (1 - progressUpto02) * springyMoveProgress +
		    progressUpto02 * springyMoveProgress * springiness;
	    }
	    *moveProgress = 1 - springyMoveProgress;
	}
	else
	{
	    *moveProgress = nonSpringyProgress;
	}
	if (aw->com.curWindowEvent == WindowEventUnminimize ||
	    aw->com.curWindowEvent == WindowEventOpen)
	    *moveProgress = 1 - *moveProgress;
	if (backwards)
	    *moveProgress = 1 - *moveProgress;

	float scProgress = nonSpringyProgress;
	if (aw->com.curWindowEvent == WindowEventUnminimize ||
	    aw->com.curWindowEvent == WindowEventOpen)
	    scProgress = 1 - scProgress;
	if (backwards)
	    scProgress = 1 - scProgress;

	*scaleProgress =
	    pow(scProgress, 1.25);
    }
}

void
fxZoomUpdateWindowAttrib (CompWindow * w,
			  WindowPaintAttrib * wAttrib)
{
    ANIM_WINDOW(w);

    float forwardProgress;
    float dummy;

    fxZoomAnimProgress (w, &dummy, &forwardProgress, FALSE);

    wAttrib->opacity =
	(GLushort) (aw->com.storedOpacity * (1 - forwardProgress));
}

static void
getZoomCenterScaleFull (CompWindow *w,
			Point *pCurCenter, Point *pCurScale,
			Point *pWinCenter, Point *pIconCenter,
			float *pRotateProgress)
{
    ANIM_WINDOW(w);

    Point winCenter =
	{(WIN_X(w) + WIN_W(w) / 2.0),
	 (WIN_Y(w) + WIN_H(w) / 2.0)};
    Point iconCenter =
	{aw->com.icon.x + aw->com.icon.width / 2.0,
	 aw->com.icon.y + aw->com.icon.height / 2.0};
    Point winSize =
	{WIN_W(w), WIN_H(w)};
    winSize.x = (winSize.x == 0 ? 1 : winSize.x);
    winSize.y = (winSize.y == 0 ? 1 : winSize.y);

    float scaleProgress;
    float moveProgress;
    float rotateProgress = 0;

    if (aw->com.curAnimEffect == AnimEffectSidekick)
    {
	fxZoomAnimProgress (w, &moveProgress, &scaleProgress, FALSE);
	rotateProgress = moveProgress;
    }
    else if (aw->com.curAnimEffect == AnimEffectZoom)
    {
	fxZoomAnimProgress (w, &moveProgress, &scaleProgress, FALSE);
    }
    else
    {
	// other effects use this for minimization
	fxZoomAnimProgress (w, &moveProgress, &scaleProgress, TRUE);
    }

    Point curCenter =
	{(1 - moveProgress) * winCenter.x + moveProgress * iconCenter.x,
	 (1 - moveProgress) * winCenter.y + moveProgress * iconCenter.y};
    Point curScale =
	{((1 - scaleProgress) * winSize.x + scaleProgress * aw->com.icon.width) /
	 winSize.x,
	 ((1 - scaleProgress) * winSize.y + scaleProgress * aw->com.icon.height) /
	 winSize.y};

    // Copy calculated variables
    if (pCurCenter)
	*pCurCenter = curCenter;
    if (pCurScale)
	*pCurScale = curScale;
    if (pWinCenter)
	*pWinCenter = winCenter;
    if (pIconCenter)
	*pIconCenter = iconCenter;
    if (pRotateProgress)
	*pRotateProgress = rotateProgress;
}

inline void
getZoomCenterScale (CompWindow *w,
		    Point *pCurCenter, Point *pCurScale)
{
    getZoomCenterScaleFull (w, pCurCenter, pCurScale, NULL, NULL, NULL);
}

void
applyZoomTransform (CompWindow * w)
{
    ANIM_WINDOW(w);

    CompTransform *transform = &aw->com.transform;
    
    Point curCenter;
    Point curScale;
    Point winCenter;
    Point iconCenter;
    float rotateProgress;

    getZoomCenterScaleFull (w, &curCenter, &curScale,
			    &winCenter, &iconCenter, &rotateProgress);

    if (fxZoomGetSpringiness (w) == 0.0f &&
	(aw->com.curAnimEffect == AnimEffectZoom ||
	 aw->com.curAnimEffect == AnimEffectSidekick) &&
	(aw->com.curWindowEvent == WindowEventOpen ||
	 aw->com.curWindowEvent == WindowEventClose))
    {
	matrixTranslate (transform,
			 iconCenter.x, iconCenter.y, 0);
	matrixScale (transform, curScale.x, curScale.y, curScale.y);
	matrixTranslate (transform,
			 -iconCenter.x, -iconCenter.y, 0);

	if (aw->com.curAnimEffect == AnimEffectSidekick)
	{
	    matrixTranslate (transform, winCenter.x, winCenter.y, 0);
	    matrixRotate (transform, rotateProgress * 360 * aw->numZoomRotations,
			  0.0f, 0.0f, 1.0f);
	    matrixTranslate (transform, -winCenter.x, -winCenter.y, 0);
	}
    }
    else
    {
	matrixTranslate (transform, winCenter.x, winCenter.y, 0);
	float tx, ty;
	if (aw->com.curAnimEffect != AnimEffectZoom)
	{
	    // avoid parallelogram look
	    float maxScale = MAX(curScale.x, curScale.y);
	    matrixScale (transform, maxScale, maxScale, maxScale);
	    tx = (curCenter.x - winCenter.x) / maxScale;
	    ty = (curCenter.y - winCenter.y) / maxScale;
	}
	else
	{
	    matrixScale (transform, curScale.x, curScale.y, curScale.y);
	    tx = (curCenter.x - winCenter.x) / curScale.x;
	    ty = (curCenter.y - winCenter.y) / curScale.y;
	}
	matrixTranslate (transform, tx, ty, 0);
	if (aw->com.curAnimEffect == AnimEffectSidekick)
	{
	    matrixRotate (transform, rotateProgress * 360 * aw->numZoomRotations,
			  0.0f, 0.0f, 1.0f);
	}
	matrixTranslate (transform, -winCenter.x, -winCenter.y, 0);
    }
}

