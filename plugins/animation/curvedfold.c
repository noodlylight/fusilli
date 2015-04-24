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

// =====================  Effect: Curved Fold  =========================

static inline float
getObjectZ (Model *model,
	    float forwardProgress,
	    float sinForProg,
	    float relDistToCenter,
	    float curveMaxAmp)
{
    return -(sinForProg *
	     (1 - pow (pow(2 * relDistToCenter, 1.3), 2)) *
	     curveMaxAmp *
	     model->scale.x);
}

static void inline
fxCurvedFoldModelStepObject(CompWindow * w,
			    Model * model,
			    Object * object,
			    float forwardProgress,
			    float sinForProg,
			    float curveMaxAmp)
{
    ANIM_WINDOW(w);

    float origx = w->attrib.x + (WIN_W(w) * object->gridPosition.x -
				 w->output.left) * model->scale.x;
    float origy = w->attrib.y + (WIN_H(w) * object->gridPosition.y -
				 w->output.top) * model->scale.y;

    object->position.x = origx;

    if (aw->com.curWindowEvent == WindowEventShade ||
	aw->com.curWindowEvent == WindowEventUnshade)
    {
	// Execute shade mode

	// find position in window contents
	// (window contents correspond to 0.0-1.0 range)
	float relPosInWinContents =
	    (object->gridPosition.y * WIN_H(w) -
	     model->topHeight) / w->height;
	float relDistToCenter = fabs(relPosInWinContents - 0.5);

	if (object->gridPosition.y == 0)
	{
	    object->position.y = WIN_Y(w);
	    object->position.z = 0;
	}
	else if (object->gridPosition.y == 1)
	{
	    object->position.y = 
		(1 - forwardProgress) * origy +
		forwardProgress *
		(WIN_Y(w) + model->topHeight + model->bottomHeight);
	    object->position.z = 0;
	}
	else
	{
	    object->position.y =
		(1 - forwardProgress) * origy +
		forwardProgress * (WIN_Y(w) + model->topHeight);
	    object->position.z =
		getObjectZ (model, forwardProgress, sinForProg, relDistToCenter,
			    curveMaxAmp);
	}
    }
    else
    {
	// Execute normal mode

	// find position within window borders
	// (border contents correspond to 0.0-1.0 range)
	float relPosInWinBorders =
	    (object->gridPosition.y * WIN_H(w) -
	     (w->output.top - w->input.top)) / BORDER_H(w);
	float relDistToCenter = fabs(relPosInWinBorders - 0.5);

	// prevent top & bottom shadows from extending too much
	if (relDistToCenter > 0.5)
	    relDistToCenter = 0.5;

	object->position.y =
	    (1 - forwardProgress) * origy +
	    forwardProgress * (BORDER_Y(w) + BORDER_H(w) / 2.0);
	object->position.z =
	    getObjectZ (model, forwardProgress, sinForProg, relDistToCenter,
			curveMaxAmp);
    }
}

void
fxCurvedFoldModelStep (CompWindow *w, float time)
{
    defaultAnimStep (w, time);

    ANIM_WINDOW(w);

    Model *model = aw->com.model;

    float forwardProgress = getProgressAndCenter (w, NULL);

    float curveMaxAmp = 0.4 * pow ((float)WIN_H (w) / w->screen->height, 0.4) *
	animGetF (w, ANIM_SCREEN_OPTION_CURVED_FOLD_AMP_MULT);

    float sinForProg = sin(forwardProgress * M_PI / 2);

    Object *object = model->objects;

    int i;
    for (i = 0; i < model->numObjects; i++, object++)
	fxCurvedFoldModelStepObject
	    (w,
	     model,
	     object,
	     forwardProgress,
	     sinForProg,
	     curveMaxAmp);
}

void
fxFoldUpdateWindowAttrib(CompWindow * w,
			 WindowPaintAttrib * wAttrib)
{
    ANIM_WINDOW(w);

    if (aw->com.curWindowEvent == WindowEventOpen ||
	aw->com.curWindowEvent == WindowEventClose ||
	((aw->com.curWindowEvent == WindowEventMinimize ||
	  aw->com.curWindowEvent == WindowEventUnminimize) &&
	 ((aw->com.curAnimEffect == AnimEffectCurvedFold &&
	   !animGetB (w, ANIM_SCREEN_OPTION_CURVED_FOLD_Z2TOM)) ||
	  (aw->com.curAnimEffect == AnimEffectHorizontalFolds &&
	   !animGetB (w, ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_Z2TOM)))))
    {
	float forwardProgress = defaultAnimProgress (w);

	wAttrib->opacity =
	    (GLushort) (aw->com.storedOpacity * (1 - forwardProgress));
    }
    else if ((aw->com.curWindowEvent == WindowEventMinimize ||
	      aw->com.curWindowEvent == WindowEventUnminimize) &&
	     ((aw->com.curAnimEffect == AnimEffectCurvedFold &&
	       animGetB (w, ANIM_SCREEN_OPTION_CURVED_FOLD_Z2TOM)) ||
	      (aw->com.curAnimEffect == AnimEffectHorizontalFolds &&
	       animGetB (w, ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_Z2TOM))))
    {
	fxZoomUpdateWindowAttrib (w, wAttrib);
    }
    // if shade/unshade, don't do anything
}

Bool
fxCurvedFoldZoomToIcon (CompWindow *w)
{
    ANIM_WINDOW(w);
    return ((aw->com.curWindowEvent == WindowEventMinimize ||
	     aw->com.curWindowEvent == WindowEventUnminimize) &&
	    animGetB (w, ANIM_SCREEN_OPTION_CURVED_FOLD_Z2TOM));
}

