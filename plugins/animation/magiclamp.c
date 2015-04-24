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

void
fxMagicLampInitGrid (CompWindow *w,
		     int *gridWidth, int *gridHeight)
{
    *gridWidth = 2;
    *gridHeight = animGetI (w, ANIM_SCREEN_OPTION_MAGIC_LAMP_GRID_RES);
}

void
fxVacuumInitGrid (CompWindow *w,
		  int *gridWidth, int *gridHeight)
{
    *gridWidth = 2;
    *gridHeight = animGetI (w, ANIM_SCREEN_OPTION_VACUUM_GRID_RES);
}

Bool
fxMagicLampInit (CompWindow * w)
{
    ANIM_WINDOW(w);

    XRectangle *icon = &aw->com.icon;
    int screenHeight = w->screen->height;
    aw->minimizeToTop = (WIN_Y(w) + WIN_H(w) / 2) >
	(icon->y + icon->height / 2);
    int maxWaves;
    float waveAmpMin, waveAmpMax;
    float distance;

    if (aw->com.curAnimEffect == AnimEffectMagicLamp)
    {
	maxWaves = animGetI (w, ANIM_SCREEN_OPTION_MAGIC_LAMP_MAX_WAVES);
	waveAmpMin =
	    animGetF (w, ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MIN);
	waveAmpMax =
	    animGetF (w, ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MAX);
    }
    else
    {
	maxWaves = 0;
	waveAmpMin = 0;
	waveAmpMax = 0;
    }
    if (waveAmpMax < waveAmpMin)
	waveAmpMax = waveAmpMin;

    if (maxWaves == 0)
    {
	aw->magicLampWaveCount = 0;
	return TRUE;
    }

    // Initialize waves

    if (aw->minimizeToTop)
	distance = WIN_Y(w) + WIN_H(w) - icon->y;
    else
	distance = icon->y - WIN_Y(w);

    aw->magicLampWaveCount =
	1 + (float)maxWaves *distance / screenHeight;

    if (!(aw->magicLampWaves))
    {
	aw->magicLampWaves =
	    calloc(aw->magicLampWaveCount, sizeof(WaveParam));
	if (!aw->magicLampWaves)
	{
	    compLogMessage ("animation", CompLogLevelError,
			    "Not enough memory");
	    return FALSE;
	}
    }
    // Compute wave parameters

    int ampDirection = (RAND_FLOAT() < 0.5 ? 1 : -1);
    int i;
    float minHalfWidth = 0.22f;
    float maxHalfWidth = 0.38f;

    for (i = 0; i < aw->magicLampWaveCount; i++)
    {
	aw->magicLampWaves[i].amp =
	    ampDirection * (waveAmpMax - waveAmpMin) *
	    rand() / RAND_MAX + ampDirection * waveAmpMin;
	aw->magicLampWaves[i].halfWidth =
	    RAND_FLOAT() * (maxHalfWidth -
			    minHalfWidth) + minHalfWidth;

	// avoid offset at top and bottom part by added waves
	float availPos = 1 - 2 * aw->magicLampWaves[i].halfWidth;
	float posInAvailSegment = 0;

	if (i > 0)
	    posInAvailSegment =
		(availPos / aw->magicLampWaveCount) * rand() / RAND_MAX;

	aw->magicLampWaves[i].pos =
	    (posInAvailSegment +
	     i * availPos / aw->magicLampWaveCount +
	     aw->magicLampWaves[i].halfWidth);

	// switch wave direction
	ampDirection *= -1;
    }

    return TRUE;
}

void
fxMagicLampModelStep (CompWindow *w, float time)
{
    defaultAnimStep (w, time);

    ANIM_WINDOW(w);

    Model *model = aw->com.model;
    XRectangle *icon = &aw->com.icon;

    if ((aw->com.curWindowEvent == WindowEventOpen ||
	 aw->com.curWindowEvent == WindowEventClose) &&
	((aw->com.curAnimEffect == AnimEffectMagicLamp &&
	  animGetB (w, ANIM_SCREEN_OPTION_MAGIC_LAMP_MOVING_END)) ||
	 (aw->com.curAnimEffect == AnimEffectVacuum &&
	  animGetB (w, ANIM_SCREEN_OPTION_VACUUM_MOVING_END))))
    {
	// Update icon position
	getMousePointerXY (w->screen, &icon->x, &icon->y);
    }
    float forwardProgress = defaultAnimProgress (w);

    if (aw->magicLampWaveCount > 0 && !aw->magicLampWaves)
	return;

    float iconCloseEndY;
    float iconFarEndY;
    float winFarEndY;
    float winVisibleCloseEndY;

    float iconShadowLeft =
	((float)(w->output.left - w->input.left)) * 
	icon->width / w->width;
    float iconShadowRight =
	((float)(w->output.right - w->input.right)) * 
	icon->width / w->width;

    float sigmoid0 = sigmoid(0);
    float sigmoid1 = sigmoid(1);
    float winw = WIN_W(w);
    float winh = WIN_H(w);

    if (aw->minimizeToTop)
    {
	iconFarEndY = icon->y;
	iconCloseEndY = icon->y + icon->height;
	winFarEndY = WIN_Y(w) + winh;
	winVisibleCloseEndY = WIN_Y(w);
	if (winVisibleCloseEndY < iconCloseEndY)
	    winVisibleCloseEndY = iconCloseEndY;
    }
    else
    {
	iconFarEndY = icon->y + icon->height;
	iconCloseEndY = icon->y;
	winFarEndY = WIN_Y(w);
	winVisibleCloseEndY = WIN_Y(w) + winh;
	if (winVisibleCloseEndY > iconCloseEndY)
	    winVisibleCloseEndY = iconCloseEndY;
    }

    float preShapePhaseEnd = 0.22f;
    float preShapeProgress  = 0;
    float postStretchProgress = 0;
    float stretchProgress = 0;
    float stretchPhaseEnd =
	preShapePhaseEnd + (1 - preShapePhaseEnd) *
	(iconCloseEndY -
	 winVisibleCloseEndY) / ((iconCloseEndY - winFarEndY) +
				 (iconCloseEndY - winVisibleCloseEndY));
    if (stretchPhaseEnd < preShapePhaseEnd + 0.1)
	stretchPhaseEnd = preShapePhaseEnd + 0.1;

    if (forwardProgress < preShapePhaseEnd)
    {
	preShapeProgress = forwardProgress / preShapePhaseEnd;

	// Slow down "shaping" toward the end
	preShapeProgress = 1 - decelerateProgress(1 - preShapeProgress);
    }

    if (forwardProgress < preShapePhaseEnd)
    {
	stretchProgress = forwardProgress / stretchPhaseEnd;
    }
    else
    {
	if (forwardProgress < stretchPhaseEnd)
	{
	    stretchProgress = forwardProgress / stretchPhaseEnd;
	}
	else
	{
	    postStretchProgress =
		(forwardProgress - stretchPhaseEnd) / (1 - stretchPhaseEnd);
	}
    }

    Object *object = model->objects;
    int i;
    for (i = 0; i < model->numObjects; i++, object++)
    {
	float origx = w->attrib.x + (winw * object->gridPosition.x -
				     w->output.left) * model->scale.x;
	float origy = w->attrib.y + (winh * object->gridPosition.y -
				     w->output.top) * model->scale.y;

	float iconx =
	    (icon->x - iconShadowLeft) + 
	    (icon->width + iconShadowLeft + iconShadowRight) *
	    object->gridPosition.x;
	float icony = icon->y + icon->height * object->gridPosition.y;

	float stretchedPos;
	if (aw->minimizeToTop)
	    stretchedPos =
		object->gridPosition.y * origy +
		(1 - object->gridPosition.y) * icony;
	else
	    stretchedPos =
		(1 - object->gridPosition.y) * origy +
		object->gridPosition.y * icony;

	// Compute current y position
	if (forwardProgress < preShapePhaseEnd)
	{
	    object->position.y =
		(1 - stretchProgress) * origy +
		stretchProgress * stretchedPos;
	}
	else
	{
	    if (forwardProgress < stretchPhaseEnd)
	    {
		object->position.y =
		    (1 - stretchProgress) * origy +
		    stretchProgress * stretchedPos;
	    }
	    else
	    {
		object->position.y =
		    (1 - postStretchProgress) *
		    stretchedPos +
		    postStretchProgress *
		    (stretchedPos + (iconCloseEndY - winFarEndY));
	    }
	}

	// Compute "target shape" x position
	float fx = ((iconCloseEndY - object->position.y) / 
		    (iconCloseEndY - winFarEndY));
	float fy = ((sigmoid(fx) - sigmoid0) /
		    (sigmoid1 - sigmoid0));
	float targetx = fy * (origx - iconx) + iconx;

	// Apply waves
	int i;
	for (i = 0; i < aw->magicLampWaveCount; i++)
	{
	    float cosfx = ((fx - aw->magicLampWaves[i].pos) /
			   aw->magicLampWaves[i].halfWidth);
	    if (cosfx < -1 || cosfx > 1)
		continue;
	    targetx +=
		aw->magicLampWaves[i].amp * model->scale.x *
		(cos(cosfx * M_PI) + 1) / 2;
	}

	// Compute current x position
	if (forwardProgress < preShapePhaseEnd)
	    object->position.x =
		(1 - preShapeProgress) * origx + preShapeProgress * targetx;
	else	    
	    object->position.x = targetx;

	if (aw->minimizeToTop)
	{
	    if (object->position.y < iconFarEndY)
		object->position.y = iconFarEndY;
	}
	else
	{
	    if (object->position.y > iconFarEndY)
		object->position.y = iconFarEndY;
	}

	// No need to set object->position.z to 0, since they won't be used
	// due to modelAnimIs3D being FALSE for magic lamp.
    }
}

