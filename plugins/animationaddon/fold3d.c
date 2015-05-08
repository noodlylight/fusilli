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

Bool
fxFoldInit (CompWindow * w)
{
    if (!polygonsAnimInit (w))
	return FALSE;

    ANIMADDON_WINDOW (w);

    aw->com->animTotalTime /= FOLD_PERCEIVED_T;
    aw->com->animRemainingTime = aw->com->animTotalTime;

    int gridSizeX = animGetI (w, ANIMADDON_SCREEN_OPTION_FOLD_GRIDSIZE_X);
    int gridSizeY = animGetI (w, ANIMADDON_SCREEN_OPTION_FOLD_GRIDSIZE_Y);

    if (!tessellateIntoRectangles (w, gridSizeX, gridSizeY, 1.0f))
	return FALSE;

    PolygonSet *pset = aw->eng.polygonSet;
    PolygonObject *p = pset->polygons;

    // handle other non-zero values
    int fold_in = animGetI (w, ANIMADDON_SCREEN_OPTION_FOLD_DIR) == 0 ? 1 : 0;

    float rows_duration;
    float fduration;

    if (gridSizeY == 1)
    {
	fduration = 1.0f / (float)(gridSizeY + 2 * ceil (gridSizeX / 2));
	rows_duration = 0;
    }
    else
    {
	fduration =
	    1.0f / (float)(gridSizeY + 2 * ceil (gridSizeX / 2) + 1 + fold_in);
	rows_duration = (gridSizeY - 1 + fold_in) * fduration;
    }

    float duration = fduration * 2;
    float start;
    int i;
    int j = 0;
    int k = 0;

    for (i = 0; i < pset->nPolygons; i++, p++)
    {
	if (i > pset->nPolygons - gridSizeX - 1)
	{
	    // bottom row polygons
	    if (j < gridSizeX / 2)
	    {
		// the left ones
		start = rows_duration + duration * j++;

		p->rotAxis.y = -180;
		p->finalRotAng = 180;

		p->fadeStartTime = start + fduration;
		p->fadeDuration = fduration;
	    }
	    else if (j == gridSizeX / 2)
	    {
		// the middle one
		start = rows_duration + j * duration;

		p->rotAxis.y = 90;
		p->finalRotAng = 90;

		p->fadeStartTime = start + fduration;
		p->fadeDuration = fduration;
		j++;
	    }
	    else
	    {
		// the right ones
		start = rows_duration + (j - 2) * duration + duration * k--;

		p->rotAxis.y = 180;
		p->finalRotAng = 180;

		p->fadeStartTime = start + fduration;
		p->fadeDuration = fduration;
	    }
	}
	else
	{
	    // main rows
	    int row = i / gridSizeX;	// [0; gridSizeY-1]

	    start = row * fduration;
	    p->rotAxis.x = 180;
	    p->finalRelPos.y = row; // number of row, not finalRelPos!!
	    p->finalRotAng = 180;

	    p->fadeDuration = fduration;
	    p->fadeStartTime = start;

	    if (row < gridSizeY - 2 || fold_in)
		p->fadeStartTime += fduration;
	}
	p->moveStartTime = start;
	p->moveDuration = duration;
    }
    pset->doDepthTest = TRUE;
    pset->doLighting = TRUE;
    pset->correctPerspective = CorrectPerspectiveWindow;

    return TRUE;
}

void
fxFoldAnimStepPolygon (CompWindow *w,
			 PolygonObject *p,
			 float forwardProgress)
{
    int dir = animGetI (w, ANIMADDON_SCREEN_OPTION_FOLD_DIR) == 0 ? 1 : -1;

    int gridSizeX = animGetI (w, ANIMADDON_SCREEN_OPTION_FOLD_GRIDSIZE_X);
    int gridSizeY = animGetI (w, ANIMADDON_SCREEN_OPTION_FOLD_GRIDSIZE_Y);

    float moveProgress = forwardProgress - p->moveStartTime;

    if (p->moveDuration > 0)
	moveProgress /= p->moveDuration;
    if (moveProgress < 0)
	moveProgress = 0;
    else if (moveProgress > 1)
	moveProgress = 1;

    float const_x = BORDER_W (w) / (float)gridSizeX;	//  width of single piece
    float const_y = BORDER_H (w) / (float)gridSizeY;	// height of single piece

    p->rotAngle = dir * moveProgress * p->finalRotAng;

    if (p->rotAxis.x == 180)
    {
	if (p->finalRelPos.y == gridSizeY - 2)
	{
	    // it means the last row
	    p->centerPos.y =
		p->centerPosStart.y + const_y / 2.0f -
		cos (p->rotAngle * M_PI / 180.0f) * const_y / 2.0f;
	    p->centerPos.z =
		p->centerPosStart.z +
		1.0f / w->screen->width * (sin (-p->rotAngle * M_PI / 180.0f) *
					   const_y / 2.0f);
	}
	else
	{
	    // rows
	    if (fabs (p->rotAngle) < 90)
	    {
		// 1. rotate 90
		p->centerPos.y =
		    p->centerPosStart.y + const_y / 2.0f -
		    cos (p->rotAngle * M_PI / 180.0f) * const_y / 2.0f;
		p->centerPos.z =
		    p->centerPosStart.z +
		    1.0f / w->screen->width *
		    (sin (-p->rotAngle * M_PI / 180.0f) * const_y / 2.0f);
	    }
	    else
	    {
		// 2. rotate faster 180
		float alpha = p->rotAngle - dir * 90;	// [0 - 45]
		float alpha2 = 2 * alpha;	// [0 - 90]

		p->rotAngle = (p->rotAngle - dir * 90) * 2 + dir * 90;

		p->centerPos.y =
		    p->centerPosStart.y + const_y / 2.0f + const_y -
		    cos (alpha * M_PI / 180.0f) * const_y + dir *
		    sin (alpha2 * M_PI / 180.0f) * const_y / 2.0f;

		p->centerPos.z =
		    p->centerPosStart.z +
		    1.0f / w->screen->width *
		    (-sin (alpha * M_PI / 180.0f) * const_y - dir *
		     cos (alpha2 * M_PI / 180.0f) * const_y / 2.0f);
	    }
	}
    }
    else if (p->rotAxis.y == -180)
    {
	// simple blocks left
	p->centerPos.x =
	    p->centerPosStart.x + const_x / 2.0f -
	    cos (p->rotAngle * M_PI / 180.0f) * const_x / 2.0f;

	p->centerPos.z =
	    p->centerPosStart.z -
	    1.0f / w->screen->width * (sin (p->rotAngle * M_PI / 180.0f) *
				       const_x / 2.0f);
    }
    else if (p->rotAxis.y == 180)
    {
	// simple blocks right
	p->centerPos.x =
	    p->centerPosStart.x - const_x / 2.0f +
	    cos (-p->rotAngle * M_PI / 180.0f) * const_x / 2.0f;

	p->centerPos.z =
	    p->centerPosStart.z +
	    1.0f / w->screen->width * (sin (-p->rotAngle * M_PI / 180.0f) *
				       const_x / 2.0f);
    }
}
