/**
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
 * Hexagon tessellator added by : Mike Slegeir
 * E-mail                       : mikeslegeir@mail.utexas.edu>
 *
 * Fold and Skewer added by : Tomasz Kołodziejski
 * E-mail                   : tkolodziejski@gmail.com
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
 **/

#include "animationaddon.h"

static void
getDirection (int *dir, int *c, int direction)
{
    switch (direction)
    {
    case 0:
	// left
	dir[(*c)++] = 0;
	break;
    case 1:
	// right
	dir[(*c)++] = 1;
	break;
    case 2:
	// left-right   
	dir[(*c)++] = 0;
	dir[(*c)++] = 1;
	break;
    case 3:
	// up
	dir[(*c)++] = 2;
	break;
    case 4:
	// downs
	dir[(*c)++] = 3;
	break;
    case 5:
	// up-down
	dir[(*c)++] = 2;
	dir[(*c)++] = 3;
	break;
    case 6:
	// in
	dir[(*c)++] = 4;
	break;
    case 7:
	// out
	dir[(*c)++] = 5;
	break;
    case 8:
	// in-out
	dir[(*c)++] = 4;
	dir[(*c)++] = 5;
	break;
    case 9:
	// random
	getDirection (dir, c, floor (RAND_FLOAT () * 8));
	break;
    }
}

Bool
fxSkewerInit (CompWindow * w)
{
    if (!polygonsAnimInit (w))
	return FALSE;

    CompScreen *s = w->screen;

    ANIMADDON_WINDOW (w);

    aw->com->animTotalTime /= SKEWER_PERCEIVED_T;
    aw->com->animRemainingTime = aw->com->animTotalTime;

    float thickness = animGetF (w, ANIMADDON_SCREEN_OPTION_SKEWER_THICKNESS);
    int rotation = animGetI (w, ANIMADDON_SCREEN_OPTION_SKEWER_ROTATION);
    int gridSizeX = animGetI (w, ANIMADDON_SCREEN_OPTION_SKEWER_GRIDSIZE_X);
    int gridSizeY = animGetI (w, ANIMADDON_SCREEN_OPTION_SKEWER_GRIDSIZE_Y);

    int dir[2];			// directions array
    int c = 0;			// number of directions

    getDirection (dir, &c,
		  animGetI (w, ANIMADDON_SCREEN_OPTION_SKEWER_DIRECTION));

    if (animGetI (w, ANIMADDON_SCREEN_OPTION_SKEWER_TESS) == PolygonTessHex)
    {
	if (!tessellateIntoHexagons (w, gridSizeX, gridSizeY, thickness))
	    return FALSE;
    }
    else
    {
	if (!tessellateIntoRectangles (w, gridSizeX, gridSizeY, thickness))
	    return FALSE;
    }

    PolygonSet *pset = aw->eng.polygonSet;
    PolygonObject *p = pset->polygons;

    int times[pset->nPolygons];
    int last_time = pset->nPolygons - 1;

    int i;
    for (i = 0; i < pset->nPolygons; i++)
	times[i] = i;

    for (i = 0; i < pset->nPolygons; i++, p++)
    {
	if (c > 0)
	{
	    switch (dir[(int)floor (RAND_FLOAT () * c)])
	    {
	    case 0:
		// left
		p->finalRelPos.x = -s->width;
		p->rotAxis.x = rotation;
		break;

	    case 1:
		// right
		p->finalRelPos.x = s->width;
		p->rotAxis.x = rotation;
		break;

	    case 2:
		// up
		p->finalRelPos.y = -s->height;
		p->rotAxis.y = rotation;
		break;

	    case 3:
		// down
		p->finalRelPos.y = s->height;
		p->rotAxis.y = rotation;
		break;

	    case 4:
		// in
		p->finalRelPos.z = -.8 * DEFAULT_Z_CAMERA * s->width;
		p->rotAxis.x = rotation;
		p->rotAxis.y = rotation;
		break;

	    case 5:
		// out
		p->finalRelPos.z = .8 * DEFAULT_Z_CAMERA * s->width;
		p->rotAxis.x = rotation;
		p->rotAxis.y = rotation;
		break;
	    }

	    p->finalRotAng = rotation;
	}
	// if no direction is set - just fade

	// choose random start_time
	int rand_time = floor (RAND_FLOAT () * last_time);

	p->moveStartTime = 0.8 / (float)pset->nPolygons * times[rand_time];
	p->moveDuration = 1 - p->moveStartTime;

	p->fadeStartTime = p->moveStartTime + 0.2;
	p->fadeDuration = 1 - p->fadeStartTime;

	times[rand_time] = times[last_time];	// copy last one over times[rand_time]
	last_time--;		//descrease last_time
    }

    pset->doDepthTest = TRUE;
    pset->doLighting = TRUE;
    pset->correctPerspective = CorrectPerspectiveWindow;

    return TRUE;
}

void
fxSkewerAnimStepPolygon (CompWindow *w,
			 PolygonObject *p,
			 float forwardProgress)
{
    float moveProgress = forwardProgress - p->moveStartTime;

    if (p->moveDuration > 0)
	moveProgress /= p->moveDuration;
    if (moveProgress < 0)
	moveProgress = 0;
    else if (moveProgress > 1)
	moveProgress = 1;

    p->centerPos.x =
	p->centerPosStart.x + pow (moveProgress, 2) * p->finalRelPos.x;

    p->centerPos.y =
	p->centerPosStart.y + pow (moveProgress, 2) * p->finalRelPos.y;

    p->centerPos.z =
	p->centerPosStart.z +
	pow (moveProgress, 2) * p->finalRelPos.z * 1.0f / w->screen->width;

    // rotate
    p->rotAngle = pow (moveProgress, 2) * p->finalRotAng + p->rotAngleStart;
}
