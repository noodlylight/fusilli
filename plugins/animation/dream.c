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

Bool
fxDreamAnimInit (CompWindow * w)
{
    ANIM_WINDOW(w);

    if (fxDreamZoomToIcon (w))
    {
	aw->com.animTotalTime /= ZOOM_PERCEIVED_T;
	aw->com.usingTransform = TRUE;
    }
    else
	aw->com.animTotalTime /= DREAM_PERCEIVED_T;

    aw->com.animRemainingTime = aw->com.animTotalTime;

    return defaultAnimInit (w);
}

static void inline
fxDreamModelStepObject (CompWindow * w,
			Model * model,
			Object * object,
			float forwardProgress,
			float waveAmpMax)
{
    float waveWidth = 10.0f;
    float waveSpeed = 7.0f;

    float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
				 w->output.left) * model->scale.x;
    float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
				 w->output.top) * model->scale.y;

    object->position.x =
	origx +
	forwardProgress * waveAmpMax * model->scale.x *
	sin(object->gridPosition.y * M_PI * waveWidth +
	    waveSpeed * forwardProgress);
    object->position.y = origy;
}

void
fxDreamModelStep (CompWindow *w, float time)
{
    defaultAnimStep (w, time);

    ANIM_WINDOW(w);

    Model *model = aw->com.model;

    float forwardProgress = getProgressAndCenter (w, NULL);

    float waveAmpMax = MIN(WIN_H(w), WIN_W(w)) * 0.125f;

    Object *object = model->objects;
    int i;
    for (i = 0; i < model->numObjects; i++, object++)
	fxDreamModelStepObject(w,
			       model,
			       object,
			       forwardProgress,
			       waveAmpMax);
}

void
fxDreamUpdateWindowAttrib (CompWindow * w,
			   WindowPaintAttrib * wAttrib)
{
    ANIM_WINDOW(w);

    if (fxDreamZoomToIcon (w))
    {
	fxZoomUpdateWindowAttrib (w, wAttrib);
	return;
    }

    float forwardProgress = defaultAnimProgress (w);

    wAttrib->opacity = (GLushort) (aw->com.storedOpacity * (1 - forwardProgress));
}

Bool
fxDreamZoomToIcon (CompWindow *w)
{
    ANIM_WINDOW(w);
    return ((aw->com.curWindowEvent == WindowEventMinimize ||
	     aw->com.curWindowEvent == WindowEventUnminimize) &&
	    animGetB (w, ANIM_SCREEN_OPTION_DREAM_Z2TOM));
}

