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

// =====================  Effect: Glide  =========================

static void
fxGlideGetParams (CompWindow *w,
		  float *finalDistFac,
		  float *finalRotAng,
		  float *thickness)
{
    ANIM_WINDOW(w);

    if (aw->com.curAnimEffect == AnimEffectGlide1)
    {
	*finalDistFac = animGetF (w, ANIM_SCREEN_OPTION_GLIDE1_AWAY_POS);
	*finalRotAng = animGetF (w, ANIM_SCREEN_OPTION_GLIDE1_AWAY_ANGLE);
    }
    else
    {
	*finalDistFac = animGetF (w, ANIM_SCREEN_OPTION_GLIDE2_AWAY_POS);
	*finalRotAng = animGetF (w, ANIM_SCREEN_OPTION_GLIDE2_AWAY_ANGLE);
    }
}

float
fxGlideAnimProgress (CompWindow *w)
{
    float forwardProgress = defaultAnimProgress (w);

    return decelerateProgress(forwardProgress);
}

static void
applyGlideTransform (CompWindow *w)
{
    ANIM_WINDOW(w);

    CompTransform *transform = &aw->com.transform;

    float finalDistFac;
    float finalRotAng;
    float thickness;

    fxGlideGetParams (w, &finalDistFac, &finalRotAng, &thickness);

    float forwardProgress;
    if (fxGlideZoomToIcon (w))
    {
	float dummy;
	fxZoomAnimProgress (w, &forwardProgress, &dummy, TRUE);
    }
    else
	forwardProgress = fxGlideAnimProgress (w);

    float finalz = finalDistFac * 0.8 * DEFAULT_Z_CAMERA *
	w->screen->width;

    Vector3d rotAxis = {1, 0, 0};
    Point3d rotAxisOffset =
	{WIN_X(w) + WIN_W(w) / 2.0f,
	 WIN_Y(w) + WIN_H(w) / 2.0f,
	 0};
    Point3d translation = {0, 0, finalz * forwardProgress};

    float rotAngle = finalRotAng * forwardProgress;
    aw->glideModRotAngle = fmodf(rotAngle + 720, 360.0f);

    // put back to window position
    matrixTranslate (transform, rotAxisOffset.x, rotAxisOffset.y, 0);

    perspectiveDistortAndResetZ (w->screen, transform);

    // animation movement
    matrixTranslate (transform, translation.x, translation.y, translation.z);

    // animation rotation
    matrixRotate (transform, rotAngle, rotAxis.x, rotAxis.y, rotAxis.z);

    // intentional scaling of z by 0 to prevent weird opacity results and
    // flashing that happen when z coords are between 0 and 1 (bug in compiz?)
    matrixScale (transform, 1.0f, 1.0f, 0.0f);

    // place window rotation axis at origin
    matrixTranslate (transform, -rotAxisOffset.x, -rotAxisOffset.y, 0);
}

void
fxGlideAnimStep (CompWindow *w, float time)
{
    defaultAnimStep (w, time);

    applyGlideTransform (w);
}

void
fxGlideUpdateWindowAttrib (CompWindow * w,
			   WindowPaintAttrib * wAttrib)
{
    ANIM_WINDOW(w);

    if (fxGlideZoomToIcon (w))
    {
	fxZoomUpdateWindowAttrib (w, wAttrib);
	return;
    }

    float forwardProgress = fxGlideAnimProgress (w);

    wAttrib->opacity = aw->com.storedOpacity * (1 - forwardProgress);
}

void
fxGlideUpdateWindowTransform (CompWindow *w,
			      CompTransform *wTransform)
{
    ANIM_WINDOW(w);

    applyTransform (wTransform, &aw->com.transform);
}

Bool
fxGlideInit (CompWindow * w)
{
    ANIM_WINDOW(w);

    if (fxGlideZoomToIcon (w))
    {
	aw->com.animTotalTime /= ZOOM_PERCEIVED_T;
	aw->com.animRemainingTime = aw->com.animTotalTime;
    }

    return defaultAnimInit (w);
}

void fxGlidePrePaintWindow (CompWindow *w)
{
    ANIM_WINDOW(w);

    if (90 < aw->glideModRotAngle &&
	aw->glideModRotAngle < 270)
	glCullFace(GL_FRONT);
}

void fxGlidePostPaintWindow (CompWindow * w)
{
    ANIM_WINDOW(w);

    if (90 < aw->glideModRotAngle &&
	aw->glideModRotAngle < 270)
	glCullFace(GL_BACK);
}

Bool
fxGlideZoomToIcon (CompWindow *w)
{
    ANIM_WINDOW(w);
    return
	((aw->com.curWindowEvent == WindowEventMinimize ||
	  aw->com.curWindowEvent == WindowEventUnminimize) &&
	 ((aw->com.curAnimEffect == AnimEffectGlide1 &&
	   animGetB (w, ANIM_SCREEN_OPTION_GLIDE1_Z2TOM)) ||
	  (aw->com.curAnimEffect == AnimEffectGlide2 &&
	   animGetB (w, ANIM_SCREEN_OPTION_GLIDE2_Z2TOM))));
}

