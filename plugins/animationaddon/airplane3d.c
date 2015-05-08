/*
 * Animation plugin for compiz/beryl
 *
 * airplane3d.c
 *
 * Copyright : (C) 2006 Erkin Bahceci
 * E-mail    : erkinbah@gmail.com
 *
 * Based on Wobbly and Minimize plugins by
 *           : David Reveman
 * E-mail    : davidr@novell.com>
 *
 * Airplane added by : Carlo Palma
 * E-mail            : carlopalma@salug.it
 * Based on code originally written by Mark J. Kilgard
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

// Divide the window in 8 polygons (6 quadrilaters and 2 triangles (all of them draw as quadrilaters))
// Based on tessellateIntoRectangles and tessellateIntoHexagons. Improperly called tessellation.
static Bool
tessellateIntoAirplane (CompWindow * w)
{
    ANIMADDON_WINDOW (w);

    PolygonSet *pset = aw->eng.polygonSet;

    if (!pset)
	return FALSE;

    float winLimitsX;		// boundaries of polygon tessellation
    float winLimitsY;
    float winLimitsW;
    float winLimitsH;

    winLimitsX = BORDER_X (w);
    winLimitsY = BORDER_Y (w);
    winLimitsW = BORDER_W (w);
    winLimitsH = BORDER_H (w);

    int numpol = 8;
    if (pset->nPolygons != numpol)
    {
	if (pset->nPolygons > 0)
	    freePolygonObjects (pset);

	pset->nPolygons = numpol;

	pset->polygons = calloc (pset->nPolygons, sizeof (PolygonObject));
	if (!pset->polygons)
	{
	    compLogMessage ("animationaddon", CompLogLevelError,
			    "Not enough memory");
	    pset->nPolygons = 0;
	    return FALSE;
	}
    }

    float thickness = 0;
    thickness /= w->screen->width;
    pset->thickness = thickness;
    pset->nTotalFrontVertices = 0;

    float W = (float)winLimitsW;
    float H2 = (float)winLimitsH / 2;
    float H3 = (float)winLimitsH / 3;
    float H6 = (float)winLimitsH / 6;
    float halfThick = pset->thickness / 2;

    /**
     *
     * These correspond to the polygons:
     * based on GLUT sample origami.c code by Mark J. Kilgard
     *                  
     *       |-               W              -| 
     *       |-    H2     -|
     *
     * - --  +----+--------+------------------+
     * |     |    |       /                   |
     *       |    | 6   /                     | 
     *       | 7  |   /              5        |
     *   H2  |    | +                         |
     *       |    +--------+------------------+
     *       |  /                 4           |
     * H __  |/____________.__________________|
     *       |\          center               |
     *       |  \                 3           |
     *       |    +--------+------------------+
     *       |    | +                         |
     *       | 0  |   \                       |
     *       |    |  1  \            2        |  
     * |     |    |       \                   |
     * -     +----+--------+------------------+
     *
     *
     */

    PolygonObject *p = pset->polygons;
    int i;

    for (i = 0; i < 8; i++, p++)
    {
	float topRightY, topLeftY, bottomLeftY, bottomRightY;
	float topLeftX, topRightX, bottomLeftX, bottomRightX;

	p->centerPos.x = p->centerPosStart.x = winLimitsX + H2;
	p->centerPos.y = p->centerPosStart.y = winLimitsY + H2;
	p->centerPos.z = p->centerPosStart.z = -halfThick;
	p->rotAngle = p->rotAngleStart = 0;

	p->nSides = 4;
	p->nVertices = 2 * 4;
	pset->nTotalFrontVertices += 4;

	switch (i)
	{
	case 0:
	    topLeftX = -H2;
	    topLeftY = 0;
	    bottomLeftX = -H2;
	    bottomLeftY = H2;
	    bottomRightX = -H3;
	    bottomRightY = H2;
	    topRightX = -H3;
	    topRightY = H6;
	    break;
	case 1:
	    topLeftX = -H3;
	    topLeftY = H6;
	    bottomLeftX = -H3;
	    bottomLeftY = H2;
	    bottomRightX = 0;
	    bottomRightY = H2;
	    topRightX = 0;
	    topRightY = H2;
	    break;
	case 2:
	    topLeftX = -H3;
	    topLeftY = H6;
	    bottomLeftX = 0;
	    bottomLeftY = H2;
	    bottomRightX = W - H2;
	    bottomRightY = H2;
	    topRightX = W - H2;
	    topRightY = H6;
	    break;
	case 3:
	    topLeftX = -H2;
	    topLeftY = 0;
	    bottomLeftX = -H3;
	    bottomLeftY = H6;
	    bottomRightX = W - H2;
	    bottomRightY = H6;
	    topRightX = W - H2;
	    topRightY = 0;
	    break;
	case 4:
	    topLeftX = -H3;
	    topLeftY = -H6;
	    bottomLeftX = -H2;
	    bottomLeftY = 0;
	    bottomRightX = W - H2;
	    bottomRightY = 0;
	    topRightX = W - H2;
	    topRightY = -H6;
	    break;
	case 5:
	    topLeftX = 0;
	    topLeftY = -H2;
	    bottomLeftX = -H3;
	    bottomLeftY = -H6;
	    bottomRightX = W - H2;
	    bottomRightY = -H6;
	    topRightX = W - H2;
	    topRightY = -H2;
	    break;
	case 6:
	    topLeftX = -H3;
	    topLeftY = -H2;
	    bottomLeftX = -H3;
	    bottomLeftY = -H6;
	    bottomRightX = -H3;
	    bottomRightY = -H6;
	    topRightX = 0;
	    topRightY = -H2;
	    break;
	default:
	    topLeftX = -H2;
	    topLeftY = -H2;
	    bottomLeftX = -H2;
	    bottomLeftY = 0;
	    bottomRightX = -H3;
	    bottomRightY = -H6;
	    topRightX = -H3;
	    topRightY = -H2;
	    break;
	}

	// 4 front, 4 back vertices
	if (!p->vertices)
	{
	    p->vertices = calloc (8 * 3, sizeof (GLfloat));
	}
	if (!p->vertices)
	{
	    compLogMessage ("animation", CompLogLevelError,
			    "Not enough memory");
	    freePolygonObjects (pset);
	    return FALSE;
	}

	GLfloat *pv = p->vertices;

	// Determine 4 front vertices in ccw direction
	pv[0] = topLeftX;
	pv[1] = topLeftY;
	pv[2] = halfThick;

	pv[3] = bottomLeftX;
	pv[4] = bottomLeftY;
	pv[5] = halfThick;

	pv[6] = bottomRightX;
	pv[7] = bottomRightY;
	pv[8] = halfThick;

	pv[9] = topRightX;
	pv[10] = topRightY;
	pv[11] = halfThick;

	// Determine 4 back vertices in cw direction
	pv[12] = topRightX;
	pv[13] = topRightY;
	pv[14] = -halfThick;

	pv[15] = bottomRightX;
	pv[16] = bottomRightY;
	pv[17] = -halfThick;

	pv[18] = bottomLeftX;
	pv[19] = bottomLeftY;
	pv[20] = -halfThick;

	pv[21] = topLeftX;
	pv[22] = topLeftY;
	pv[23] = -halfThick;

	// 16 indices for 4 sides (for quad strip)
	if (!p->sideIndices)
	{
	    p->sideIndices = calloc (4 * 4, sizeof (GLushort));
	}
	if (!p->sideIndices)
	{
	    compLogMessage ("animation", CompLogLevelError,
			    "Not enough memory");
	    freePolygonObjects (pset);
	    return FALSE;
	}

	GLushort *ind = p->sideIndices;
	int id = 0;

	ind[id++] = 0;
	ind[id++] = 7;
	ind[id++] = 6;
	ind[id++] = 1;

	ind[id++] = 1;
	ind[id++] = 6;
	ind[id++] = 5;
	ind[id++] = 2;

	ind[id++] = 2;
	ind[id++] = 5;
	ind[id++] = 4;
	ind[id++] = 3;

	ind[id++] = 3;
	ind[id++] = 4;
	ind[id++] = 7;
	ind[id++] = 0;

	if (i < 4)
	{
	    p->boundingBox.x1 = p->centerPos.x + topLeftX;
	    p->boundingBox.y1 = p->centerPos.y + topLeftY;
	    p->boundingBox.x2 = ceil (p->centerPos.x + bottomRightX);
	    p->boundingBox.y2 = ceil (p->centerPos.y + bottomRightY);
	}
	else
	{
	    p->boundingBox.x1 = p->centerPos.x + bottomLeftX;
	    p->boundingBox.y1 = p->centerPos.y + topLeftY;
	    p->boundingBox.x2 = ceil (p->centerPos.x + bottomRightX);
	    p->boundingBox.y2 = ceil (p->centerPos.y + bottomLeftY);
	}
    }
    return TRUE;
}

Bool
fxAirplaneInit (CompWindow * w)
{
    if (!polygonsAnimInit (w))
	return FALSE;

    if (!tessellateIntoAirplane (w))
	return FALSE;

    ANIMADDON_WINDOW (w);

    float airplanePathLength =
	animGetF (w, ANIMADDON_SCREEN_OPTION_AIRPLANE_PATHLENGTH);

    PolygonSet *pset = aw->eng.polygonSet;
    PolygonObject *p = pset->polygons;

    float winLimitsW;		// boundaries of polygon tessellation
    float winLimitsH;

    winLimitsW = BORDER_W (w);
    winLimitsH = BORDER_H (w);

    float H4 = (float)winLimitsH / 4;
    float H6 = (float)winLimitsH / 6;

    int i;
    for (i = 0; i < pset->nPolygons; i++, p++)
    {
	if (!p->effectParameters)
	{
	    p->effectParameters = calloc (1, sizeof (AirplaneEffectParameters));
	}
	if (!p->effectParameters)
	{
	    compLogMessage ("animation", CompLogLevelError,
			    "Not enough memory");
	    return FALSE;
	}

	AirplaneEffectParameters *aep = p->effectParameters;

	p->moveStartTime = 0.00;
	p->moveDuration = 0.19;

	aep->moveStartTime2 = 0.19;
	aep->moveDuration2 = 0.19;

	aep->moveStartTime3 = 0.38;
	aep->moveDuration3 = 0.19;

	aep->moveStartTime4 = 0.58;
	aep->moveDuration4 = 0.09;

	aep->moveDuration5 = 0.41;

	aep->flyFinalRotation.x = 90;
	aep->flyFinalRotation.y = 10;

	aep->flyTheta = 0;

	aep->centerPosFly.x = 0;
	aep->centerPosFly.y = 0;
	aep->centerPosFly.z = 0;

	aep->flyScale = 0;
	aep->flyFinalScale = 6 * (winLimitsW / (w->screen->width / 2));

	switch (i)
	{
	case 0:
	    p->rotAxisOffset.x = -H4;
	    p->rotAxisOffset.y = H4;

	    p->rotAxis.x = 1.00;
	    p->rotAxis.y = 1.00;
	    p->rotAxis.z = 0.00;

	    p->finalRotAng = 179.5;

	    aep->rotAxisOffsetA.x = 0;
	    aep->rotAxisOffsetA.y = 0;

	    aep->rotAxisA.x = 1.00;
	    aep->rotAxisA.y = 0.00;
	    aep->rotAxisA.z = 0.00;

	    aep->finalRotAngA = 84;

	    aep->rotAxisOffsetB.x = 0;
	    aep->rotAxisOffsetB.y = 0;

	    aep->rotAxisB.x = 0.00;
	    aep->rotAxisB.y = 0.00;
	    aep->rotAxisB.z = 0.00;

	    aep->finalRotAngB = 0;
	    break;

	case 1:
	    p->rotAxisOffset.x = -H4;
	    p->rotAxisOffset.y = H4;

	    p->rotAxis.x = 1.00;
	    p->rotAxis.y = 1.00;
	    p->rotAxis.z = 0.00;

	    p->finalRotAng = 179.5;

	    aep->rotAxisOffsetA.x = 0;
	    aep->rotAxisOffsetA.y = 0;

	    aep->rotAxisA.x = 1.00;
	    aep->rotAxisA.y = 0.00;
	    aep->rotAxisA.z = 0.00;

	    aep->finalRotAngA = 84;

	    aep->rotAxisOffsetB.x = 0;
	    aep->rotAxisOffsetB.y = H6;

	    aep->rotAxisB.x = 1.00;
	    aep->rotAxisB.y = 0.00;
	    aep->rotAxisB.z = 0.00;

	    aep->finalRotAngB = -84;
	    break;

	case 2:
	    p->moveDuration = 0.00;

	    p->rotAxisOffset.x = 0;
	    p->rotAxisOffset.y = 0;

	    p->rotAxis.x = 0.00;
	    p->rotAxis.y = 0.00;
	    p->rotAxis.z = 0.00;

	    p->finalRotAng = 0;

	    aep->rotAxisOffsetA.x = 0;
	    aep->rotAxisOffsetA.y = 0;

	    aep->rotAxisA.x = 1.00;
	    aep->rotAxisA.y = 0.00;
	    aep->rotAxisA.z = 0.00;

	    aep->finalRotAngA = 84;

	    aep->rotAxisOffsetB.x = 0;
	    aep->rotAxisOffsetB.y = H6;

	    aep->rotAxisB.x = 1.00;
	    aep->rotAxisB.y = 0.00;
	    aep->rotAxisB.z = 0.00;

	    aep->finalRotAngB = -84;
	    break;

	case 3:
	    p->moveDuration = 0.00;

	    p->rotAxisOffset.x = 0;
	    p->rotAxisOffset.y = 0;

	    p->rotAxis.x = 0.00;
	    p->rotAxis.y = 0.00;
	    p->rotAxis.z = 0.00;

	    p->finalRotAng = 0;

	    aep->rotAxisOffsetA.x = 0;
	    aep->rotAxisOffsetA.y = 0;

	    aep->rotAxisA.x = 1.00;
	    aep->rotAxisA.y = 0.00;
	    aep->rotAxisA.z = 0.00;

	    aep->finalRotAngA = 84;

	    aep->moveDuration3 = 0.00;

	    aep->rotAxisOffsetB.x = 0;
	    aep->rotAxisOffsetB.y = 0;

	    aep->rotAxisB.x = 0.00;
	    aep->rotAxisB.y = 0.00;
	    aep->rotAxisB.z = 0.00;

	    aep->finalRotAngB = 0;
	    break;

	case 4:
	    p->moveDuration = 0.00;

	    p->rotAxisOffset.x = 0;
	    p->rotAxisOffset.y = 0;

	    p->rotAxis.x = 0.00;
	    p->rotAxis.y = 0.00;
	    p->rotAxis.z = 0.00;

	    p->finalRotAng = 0;

	    aep->rotAxisOffsetA.x = 0;
	    aep->rotAxisOffsetA.y = 0;

	    aep->rotAxisA.x = 1.00;
	    aep->rotAxisA.y = 0.00;
	    aep->rotAxisA.z = 0.00;

	    aep->finalRotAngA = -84;

	    aep->moveDuration3 = 0.00;

	    aep->rotAxisOffsetB.x = 0;
	    aep->rotAxisOffsetB.y = 0;

	    aep->rotAxisB.x = 0.00;
	    aep->rotAxisB.y = 0.00;
	    aep->rotAxisB.z = 0.00;

	    aep->finalRotAngB = 0;
	    break;

	case 5:
	    p->moveDuration = 0.00;

	    p->rotAxisOffset.x = 0;
	    p->rotAxisOffset.y = 0;

	    p->rotAxis.x = 0.00;
	    p->rotAxis.y = 0.00;
	    p->rotAxis.z = 0.00;

	    p->finalRotAng = 0;

	    aep->rotAxisOffsetA.x = 0;
	    aep->rotAxisOffsetA.y = 0;

	    aep->rotAxisA.x = 1.00;
	    aep->rotAxisA.y = 0.00;
	    aep->rotAxisA.z = 0.00;

	    aep->finalRotAngA = -84;

	    aep->rotAxisOffsetB.x = 0;
	    aep->rotAxisOffsetB.y = -H6;

	    aep->rotAxisB.x = 1.00;
	    aep->rotAxisB.y = 0.00;
	    aep->rotAxisB.z = 0.00;

	    aep->finalRotAngB = 84;
	    break;

	case 6:
	    p->rotAxisOffset.x = -H4;
	    p->rotAxisOffset.y = -H4;

	    p->rotAxis.x = 1.00;
	    p->rotAxis.y = -1.00;
	    p->rotAxis.z = 0.00;

	    p->finalRotAng = -179.5;

	    aep->rotAxisOffsetA.x = 0;
	    aep->rotAxisOffsetA.y = 0;

	    aep->rotAxisA.x = 1.00;
	    aep->rotAxisA.y = 0.00;
	    aep->rotAxisA.z = 0.00;

	    aep->finalRotAngA = -84;

	    aep->rotAxisOffsetB.x = 0;
	    aep->rotAxisOffsetB.y = -H6;

	    aep->rotAxisB.x = 1.00;
	    aep->rotAxisB.y = 0.00;
	    aep->rotAxisB.z = 0.00;

	    aep->finalRotAngB = 84;
	    break;

	case 7:
	    p->rotAxisOffset.x = -H4;
	    p->rotAxisOffset.y = -H4;

	    p->rotAxis.x = 1.00;
	    p->rotAxis.y = -1.00;
	    p->rotAxis.z = 0.00;

	    p->finalRotAng = -179.5;

	    aep->rotAxisOffsetA.x = 0;
	    aep->rotAxisOffsetA.y = 0;

	    aep->rotAxisA.x = 1.00;
	    aep->rotAxisA.y = 0.00;
	    aep->rotAxisA.z = 0.00;

	    aep->finalRotAngA = -84;

	    aep->rotAxisOffsetB.x = 0;
	    aep->rotAxisOffsetB.y = 0;

	    aep->rotAxisB.x = 0.00;
	    aep->rotAxisB.y = 0.00;
	    aep->rotAxisB.z = 0.00;

	    aep->finalRotAngB = 0;
	    break;
	}
    }

    if (airplanePathLength >= 1)
	pset->allFadeDuration = 0.30f / airplanePathLength;
    else
	pset->allFadeDuration = 0.30f;

    pset->doDepthTest = TRUE;
    pset->doLighting = TRUE;
    pset->correctPerspective = CorrectPerspectivePolygon;

    pset->extraPolygonTransformFunc =
	&AirplaneExtraPolygonTransformFunc;

    // Duration extension
    aw->com->animTotalTime *= 2 + airplanePathLength;
    aw->com->animRemainingTime = aw->com->animTotalTime;

    return TRUE;
}

void
fxAirplaneLinearAnimStepPolygon (CompWindow *w,
				   PolygonObject *p,
				   float forwardProgress)
{
    ANIMADDON_WINDOW (w);

    float airplanePathLength =
	animGetF (w, ANIMADDON_SCREEN_OPTION_AIRPLANE_PATHLENGTH);
    Bool airplaneFly2TaskBar =
	animGetB (w, ANIMADDON_SCREEN_OPTION_AIRPLANE_FLY2TOM);

    AirplaneEffectParameters *aep = p->effectParameters;
    if (!aep)
	return;

    /*  Phase1: folding: flaps, folding center, folding wings.
     *  Phase2: rotate and fly.
     */

    if (forwardProgress > p->moveStartTime &&
	forwardProgress < aep->moveStartTime4)
	// Phase1: folding: flaps, center, wings.
    {
	float moveProgress1 = forwardProgress - p->moveStartTime;
	if (p->moveDuration > 0)
	    moveProgress1 /= p->moveDuration;
	else
	    moveProgress1 = 0;
	if (moveProgress1 < 0)
	    moveProgress1 = 0;
	else if (moveProgress1 > 1)
	    moveProgress1 = 1;

	float moveProgress2 = forwardProgress - aep->moveStartTime2;
	if (aep->moveDuration2 > 0)
	    moveProgress2 /= aep->moveDuration2;
	else
	    moveProgress2 = 0;
	if (moveProgress2 < 0)
	    moveProgress2 = 0;
	else if (moveProgress2 > 1)
	    moveProgress2 = 1;

	float moveProgress3 = forwardProgress - aep->moveStartTime3;
	if (aep->moveDuration3 > 0)
	    moveProgress3 /= aep->moveDuration3;
	else
	    moveProgress3 = 0;
	if (moveProgress3 < 0)
	    moveProgress3 = 0;
	else if (moveProgress3 > 1)
	    moveProgress3 = 1;

	p->centerPos.x = p->centerPosStart.x;
	p->centerPos.y = p->centerPosStart.y;
	p->centerPos.z = p->centerPosStart.z;

	p->rotAngle = moveProgress1 * p->finalRotAng;
	aep->rotAngleA = moveProgress2 * aep->finalRotAngA;
	aep->rotAngleB = moveProgress3 * aep->finalRotAngB;

	aep->flyRotation.x = 0;
	aep->flyRotation.y = 0;
	aep->flyRotation.z = 0;
	aep->flyScale = 0;
    }
    else if (forwardProgress >= aep->moveStartTime4)
	// Phase2: rotate and fly 
    {
	float moveProgress4 = forwardProgress - aep->moveStartTime4;
	if (aep->moveDuration4 > 0)
	    moveProgress4 /= aep->moveDuration4;
	if (moveProgress4 < 0)
	    moveProgress4 = 0;
	else if (moveProgress4 > 1)
	    moveProgress4 = 1;

	float moveProgress5 = forwardProgress - (aep->moveStartTime4 + .01);
	if (aep->moveDuration5 > 0)
	    moveProgress5 /= aep->moveDuration5;
	if (moveProgress5 < 0)
	    moveProgress5 = 0;
	else if (moveProgress5 > 1)
	    moveProgress5 = 1;


	p->rotAngle = p->finalRotAng;
	aep->rotAngleA = aep->finalRotAngA;
	aep->rotAngleB = aep->finalRotAngB;

	aep->flyRotation.x = moveProgress4 * aep->flyFinalRotation.x;
	aep->flyRotation.y = moveProgress4 * aep->flyFinalRotation.y;

	// flying path

	float icondiffx = 0;
	aep->flyTheta = moveProgress5 * -M_PI_2 * airplanePathLength;
	aep->centerPosFly.x = w->screen->width * .4 * sin (2 * aep->flyTheta);

	if (((aw->com->curWindowEvent == WindowEventMinimize ||
	      aw->com->curWindowEvent == WindowEventUnminimize) &&
	     airplaneFly2TaskBar) ||
	    aw->com->curWindowEvent == WindowEventOpen ||
	    aw->com->curWindowEvent == WindowEventClose)
	{
	    // flying path ends at icon/pointer

	    int sign = 1;
	    if (aw->com->curWindowEvent == WindowEventUnminimize ||
		aw->com->curWindowEvent == WindowEventOpen)
		sign = -1;

	    icondiffx =
		(((aw->com->icon.x + aw->com->icon.width / 2)
		  - (p->centerPosStart.x +
		     sign * w->screen->width * .4 *
		     sin (2 * -M_PI_2 * airplanePathLength))) *
		 moveProgress5);
	    aep->centerPosFly.y =
		((aw->com->icon.y + aw->com->icon.height / 2) -
		 p->centerPosStart.y) *
		-sin (aep->flyTheta / airplanePathLength);
	}
	else
	{
	    if (p->centerPosStart.y < w->screen->height * .33 ||
		p->centerPosStart.y > w->screen->height * .66)
		aep->centerPosFly.y =
		    w->screen->height * .6 * sin (aep->flyTheta / 3.4);
	    else
		aep->centerPosFly.y =
		    w->screen->height * .4 * sin (aep->flyTheta / 3.4);
	    if (p->centerPosStart.y < w->screen->height * .33)
		aep->centerPosFly.y *= -1;
	}

	aep->flyFinalRotation.z =
	    ((atan (2.0) + M_PI_2) * sin (aep->flyTheta) - M_PI_2) * 180 / M_PI;
	aep->flyFinalRotation.z += 90;


	if (aw->com->curWindowEvent == WindowEventMinimize ||
	    aw->com->curWindowEvent == WindowEventClose)
	{
	    aep->flyFinalRotation.z *= -1;
	}
	else if (aw->com->curWindowEvent == WindowEventUnminimize ||
		 aw->com->curWindowEvent == WindowEventOpen)
	{
	    aep->centerPosFly.x *= -1;
	}

	aep->flyRotation.z = aep->flyFinalRotation.z;

	p->centerPos.x = p->centerPosStart.x + aep->centerPosFly.x + icondiffx;
	p->centerPos.y = p->centerPosStart.y + aep->centerPosFly.y;
	p->centerPos.z = p->centerPosStart.z + aep->centerPosFly.z;

	aep->flyScale = moveProgress5 * aep->flyFinalScale;
    }
}

void
AirplaneExtraPolygonTransformFunc (PolygonObject * p)
{
    AirplaneEffectParameters *aep = p->effectParameters;
    if (!aep)
	return;

    glRotatef (aep->flyRotation.x, 1, 0, 0);	//rotate on axis X
    glRotatef (-aep->flyRotation.y, 0, 1, 0);	// rotate on axis Y
    glRotatef (aep->flyRotation.z, 0, 0, 1);	// rotate on axis Z

    glScalef (1.0 / (1.0 + aep->flyScale),
	      1.0 / (1.0 + aep->flyScale), 1.0 / (1.0 + aep->flyScale));

    // Move by "rotation axis offset A"
    glTranslatef (aep->rotAxisOffsetA.x, aep->rotAxisOffsetA.y,
		  aep->rotAxisOffsetA.z);

    // Rotate by desired angle A
    glRotatef (aep->rotAngleA, aep->rotAxisA.x, aep->rotAxisA.y,
	       aep->rotAxisA.z);

    // Move back to center from  A
    glTranslatef (-aep->rotAxisOffsetA.x, -aep->rotAxisOffsetA.y,
		  -aep->rotAxisOffsetA.z);


    // Move by "rotation axis offset B"
    glTranslatef (aep->rotAxisOffsetB.x, aep->rotAxisOffsetB.y,
		  aep->rotAxisOffsetB.z);

    // Rotate by desired angle B
    glRotatef (aep->rotAngleB, aep->rotAxisB.x, aep->rotAxisB.y,
	       aep->rotAxisB.z);

    // Move back to center from B
    glTranslatef (-aep->rotAxisOffsetB.x, -aep->rotAxisOffsetB.y,
		  -aep->rotAxisOffsetB.z);
}

void
fxAirplaneAnimStep (CompWindow * w,
		      float time)
{
    ANIMADDON_WINDOW (w);
    ANIMADDON_DISPLAY (w->screen->display);

    polygonsAnimStep (w, time);

    // Make sure the airplane always flies towards mouse pointer
    if (aw->com->curWindowEvent == WindowEventClose)
	ad->animBaseFunctions->getMousePointerXY(w->screen, &aw->com->icon.x, &aw->com->icon.y);
}
