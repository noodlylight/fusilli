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

#include "animationaddon.h"

Bool
fxLeafSpreadInit (CompWindow * w)
{
    if (!polygonsAnimInit (w))
	return FALSE;

    CompScreen *s = w->screen;
    ANIMADDON_WINDOW (w);

    if (!tessellateIntoRectangles(w, 20, 14, 15.0f))
	return FALSE;

    PolygonSet *pset = aw->eng.polygonSet;
    PolygonObject *p = pset->polygons;
    float fadeDuration = 0.26;
    float life = 0.4;
    float spreadFac = 3.5;
    float randYMax = 0.07;
    float winFacX = WIN_W(w) / 800.0;
    float winFacY = WIN_H(w) / 800.0;
    float winFacZ = (WIN_H(w) + WIN_W(w)) / 2.0 / 800.0;

    int i;

    for (i = 0; i < pset->nPolygons; i++, p++)
    {
	p->rotAxis.x = RAND_FLOAT();
	p->rotAxis.y = RAND_FLOAT();
	p->rotAxis.z = RAND_FLOAT();

	float screenSizeFactor = (0.8 * DEFAULT_Z_CAMERA * s->width);
	float speed = screenSizeFactor / 10 * (0.2 + RAND_FLOAT());

	float xx = 2 * (p->centerRelPos.x - 0.5);
	float yy = 2 * (p->centerRelPos.y - 0.5);

	float x =
	    speed * winFacX * spreadFac * (xx +
					   0.5 * (RAND_FLOAT() - 0.5));
	float y =
	    speed * winFacY * spreadFac * (yy +
					   0.5 * (RAND_FLOAT() - 0.5));
	float z = speed * winFacZ * 7 * ((RAND_FLOAT() - 0.5) / 0.5);

	p->finalRelPos.x = x;
	p->finalRelPos.y = y;
	p->finalRelPos.z = z;

	p->moveStartTime =
	    p->centerRelPos.y * (1 - fadeDuration - randYMax) +
	    randYMax * RAND_FLOAT();
	p->moveDuration = 1;

	p->fadeStartTime = p->moveStartTime + life;
	if (p->fadeStartTime > 1 - fadeDuration)
	    p->fadeStartTime = 1 - fadeDuration;
	p->fadeDuration = fadeDuration;

	p->finalRotAng = 150;
    }
    pset->doDepthTest = TRUE;
    pset->doLighting = TRUE;
    pset->correctPerspective = CorrectPerspectivePolygon;

    aw->com->animTotalTime /= LEAFSPREAD_PERCEIVED_T;
    aw->com->animRemainingTime = aw->com->animTotalTime;

    return TRUE;
}
