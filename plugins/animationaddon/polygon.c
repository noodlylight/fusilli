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

#include <GL/glu.h>
#include "animationaddon.h"

#define CLIP_LIST_INCREMENT 20
#define MIN_WINDOW_GRID_SIZE 10


typedef struct
{
    float dist;
    float x, y; // relative from center

} spoke_vertex_t;

typedef struct
{
    float direction;
    float length;
    spoke_vertex_t * spoke_vertex;

} spoke_t;

typedef struct
{
    Bool is_triangle;       // false if 4 sided, true if 3 sided
    float centerX, centerY;
    float pt0X, pt0Y,
	  pt1X, pt1Y,
	  pt2X, pt2Y,
	  pt3X, pt3Y;     // if is_triangle is true, these are unused

} shard_t;


static Bool ensureLargerClipCapacity(PolygonSet * pset)
{
    if (pset->clipCapacity == pset->nClips)	// if list full
    {
	Clip4Polygons *newList = realloc
	    (pset->clips, sizeof(Clip4Polygons) *
	     (pset->clipCapacity + CLIP_LIST_INCREMENT));
	if (!newList)
	    return FALSE;
	// reset newly allocated part of this memory to 0
	memset(newList + pset->clipCapacity,
	       0, sizeof(Clip4Polygons) * CLIP_LIST_INCREMENT);

	int *newList2 = realloc
	    (pset->lastClipInGroup, sizeof(int) *
	     (pset->clipCapacity + CLIP_LIST_INCREMENT));
	if (!newList2)
	{
	    // shrink back the new larger clips memory
	    pset->clips = realloc
		(newList, sizeof(Clip4Polygons) * pset->clipCapacity);

	    // if that fails, just use the larger memory reallocated above
	    if (!pset->clips)
	    	pset->clips = newList;

	    return FALSE;
	}
	// reset newly allocated part of this memory to 0
	memset(newList2 + pset->clipCapacity,
	       0, sizeof(int) * CLIP_LIST_INCREMENT);

	pset->clips = newList;
	pset->clipCapacity += CLIP_LIST_INCREMENT;
	pset->lastClipInGroup = newList2;
    }
    return TRUE;
}

// Frees up polygon objects in pset
void
freePolygonObjects(PolygonSet * pset)
{
    PolygonObject *p = pset->polygons;

    if (!p)
    {
	pset->nPolygons = 0;
	return;
    }
    int i;

    for (i = 0; i < pset->nPolygons; i++, p++)
    {
	if (p->nVertices > 0)
	{
	    if (p->vertices)
		free(p->vertices);
	    if (p->sideIndices)
		free(p->sideIndices);
	    if (p->normals)
		free(p->normals);
	}
	if (p->effectParameters)
	    free(p->effectParameters);
    }
    free(pset->polygons);
    pset->polygons = 0;
    pset->nPolygons = 0;
}

// Frees up intersecting polygon info of PolygonSet clips
static void freeClipsPolygons(PolygonSet * pset)
{
    int k;

    for (k = 0; k < pset->clipCapacity; k++)
    {
	if (pset->clips[k].intersectingPolygons)
	{
	    free(pset->clips[k].intersectingPolygons);
	    pset->clips[k].intersectingPolygons = 0;
	}
	if (pset->clips[k].polygonVertexTexCoords)
	{
	    free(pset->clips[k].polygonVertexTexCoords);
	    pset->clips[k].polygonVertexTexCoords = 0;
	}
	pset->clips[k].nIntersectingPolygons = 0;
    }
}

// Frees up the whole polygon set
void
freePolygonSet (AnimAddonWindow * aw)
{
    PolygonSet *pset = aw->eng.polygonSet;

    freePolygonObjects(pset);
    freeClipsPolygons(pset);
    
    if (pset->clips)
	free(pset->clips);
    if (pset->lastClipInGroup)
	free (pset->lastClipInGroup);

    free(pset);
    aw->eng.polygonSet = 0;
}

// Tessellates window into extruded rectangular objects
Bool
tessellateIntoRectangles(CompWindow * w,
			 int gridSizeX, int gridSizeY, float thickness)
{
    ANIMADDON_WINDOW (w);

    PolygonSet *pset = aw->eng.polygonSet;

    if (!pset)
	return FALSE;

    int winLimitsX;				// boundaries of polygon tessellation
    int winLimitsY;
    int winLimitsW;
    int winLimitsH;

    if (pset->includeShadows)
    {
	winLimitsX = WIN_X(w);
	winLimitsY = WIN_Y(w);
	winLimitsW = WIN_W(w) - 1; // avoid artifact on right edge
	winLimitsH = WIN_H(w);
    }
    else
    {
	winLimitsX = BORDER_X(w);
	winLimitsY = BORDER_Y(w);
	winLimitsW = BORDER_W(w);
	winLimitsH = BORDER_H(w);
    }
    float minRectSize = MIN_WINDOW_GRID_SIZE;
    float rectW = winLimitsW / (float)gridSizeX;
    float rectH = winLimitsH / (float)gridSizeY;

    if (rectW < minRectSize)
	gridSizeX = winLimitsW / minRectSize;	// int div.
    if (rectH < minRectSize)
	gridSizeY = winLimitsH / minRectSize;	// int div.

    if (pset->nPolygons != gridSizeX * gridSizeY)
    {
	if (pset->nPolygons > 0)
	    freePolygonObjects(pset);

	pset->nPolygons = gridSizeX * gridSizeY;

	pset->polygons = calloc(pset->nPolygons, sizeof(PolygonObject));
	if (!pset->polygons)
	{
	    compLogMessage ("animationaddon", CompLogLevelError,
			    "Not enough memory");
	    pset->nPolygons = 0;
	    return FALSE;
	}
    }

    thickness /= w->screen->width;
    pset->thickness = thickness;
    pset->nTotalFrontVertices = 0;

    float cellW = (float)winLimitsW / gridSizeX;
    float cellH = (float)winLimitsH / gridSizeY;
    float halfW = cellW / 2;
    float halfH = cellH / 2;

    float halfThick = pset->thickness / 2;
    PolygonObject *p = pset->polygons;
    int x, y;

    for (y = 0; y < gridSizeY; y++)
    {
	float posY = winLimitsY + cellH * (y + 0.5);

	for (x = 0; x < gridSizeX; x++, p++)
	{
	    p->centerPos.x = p->centerPosStart.x =
		winLimitsX + cellW * (x + 0.5);
	    p->centerPos.y = p->centerPosStart.y = posY;
	    p->centerPos.z = p->centerPosStart.z = -halfThick;
	    p->rotAngle = p->rotAngleStart = 0;

	    p->centerRelPos.x = (x + 0.5) / gridSizeX;
	    p->centerRelPos.y = (y + 0.5) / gridSizeY;

	    p->nSides = 4;
	    p->nVertices = 2 * 4;
	    pset->nTotalFrontVertices += 4;

	    // 4 front, 4 back vertices
	    if (!p->vertices)
	    {
		p->vertices = calloc(8 * 3, sizeof(GLfloat));
	    }

	    if (!p->vertices)
	    {
		compLogMessage ("animationaddon", CompLogLevelError,
				"Not enough memory");
		freePolygonObjects(pset);
		return FALSE;
	    }

	    // Vertex normals
	    if (!p->normals)
	    {
		p->normals = calloc(8 * 3, sizeof(GLfloat));
	    }
	    if (!p->normals)
	    {
		compLogMessage ("animationaddon", CompLogLevelError,
				"Not enough memory");
		freePolygonObjects(pset);
		return FALSE;
	    }

	    GLfloat *pv = p->vertices;

	    // Determine 4 front vertices in ccw direction
	    pv[0] = -halfW;
	    pv[1] = -halfH;
	    pv[2] = halfThick;

	    pv[3] = -halfW;
	    pv[4] = halfH;
	    pv[5] = halfThick;

	    pv[6] = halfW;
	    pv[7] = halfH;
	    pv[8] = halfThick;

	    pv[9] = halfW;
	    pv[10] = -halfH;
	    pv[11] = halfThick;

	    // Determine 4 back vertices in cw direction
	    pv[12] = halfW;
	    pv[13] = -halfH;
	    pv[14] = -halfThick;

	    pv[15] = halfW;
	    pv[16] = halfH;
	    pv[17] = -halfThick;

	    pv[18] = -halfW;
	    pv[19] = halfH;
	    pv[20] = -halfThick;

	    pv[21] = -halfW;
	    pv[22] = -halfH;
	    pv[23] = -halfThick;

	    // 16 indices for 4 sides (for quads)
	    if (!p->sideIndices)
	    {
		p->sideIndices = calloc(4 * 4, sizeof(GLushort));
	    }
	    if (!p->sideIndices)
	    {
		compLogMessage ("animationaddon", CompLogLevelError,
				"Not enough memory");
		freePolygonObjects(pset);
		return FALSE;
	    }

	    GLushort *ind = p->sideIndices;
	    GLfloat *nor = p->normals;

	    int id = 0;
	    
	    // Left face
	    ind[id++] = 6; // First vertex
	    ind[id++] = 1;
	    ind[id++] = 0;
	    ind[id++] = 7;
	    nor[6 * 3 + 0] = -1; // Flat shading only uses 1st vertex's normal
	    nor[6 * 3 + 1] = 0; // in a polygon, vertex 6 for this face.
	    nor[6 * 3 + 2] = 0;

	    // Bottom face
	    ind[id++] = 1;
	    ind[id++] = 6;
	    ind[id++] = 5;
	    ind[id++] = 2;
	    nor[1 * 3 + 0] = 0;
	    nor[1 * 3 + 1] = 1;
	    nor[1 * 3 + 2] = 0;

	    // Right face
	    ind[id++] = 2;
	    ind[id++] = 5;
	    ind[id++] = 4;
	    ind[id++] = 3;
	    nor[2 * 3 + 0] = 1;
	    nor[2 * 3 + 1] = 0;
	    nor[2 * 3 + 2] = 0;

	    // Top face
	    ind[id++] = 7;
	    ind[id++] = 0;
	    ind[id++] = 3;
	    ind[id++] = 4;
	    nor[7 * 3 + 0] = 0;
	    nor[7 * 3 + 1] = -1;
	    nor[7 * 3 + 2] = 0;

	    // Front face normal
	    nor[0] = 0;
	    nor[1] = 0;
	    nor[2] = 1;

	    // Back face normal
	    nor[4 * 3 + 0] = 0;
	    nor[4 * 3 + 1] = 0;
	    nor[4 * 3 + 2] = -1;

	    // Determine bounding box (to test intersection with clips)
	    p->boundingBox.x1 = -halfW + p->centerPos.x;
	    p->boundingBox.y1 = -halfH + p->centerPos.y;
	    p->boundingBox.x2 = ceil(halfW + p->centerPos.x);
	    p->boundingBox.y2 = ceil(halfH + p->centerPos.y);

	    p->boundSphereRadius =
		sqrt (halfW * halfW + halfH * halfH + halfThick * halfThick);
	}
    }
    return TRUE;
}

// Tessellates window into extruded hexagon objects
Bool
tessellateIntoHexagons(CompWindow * w,
		       int gridSizeX, int gridSizeY, float thickness)
{
    ANIMADDON_WINDOW (w);

    PolygonSet *pset = aw->eng.polygonSet;

    if (!pset)
	return FALSE;

    int winLimitsX;				// boundaries of polygon tessellation
    int winLimitsY;
    int winLimitsW;
    int winLimitsH;

    if (pset->includeShadows)
    {
	winLimitsX = WIN_X(w);
	winLimitsY = WIN_Y(w);
	winLimitsW = WIN_W(w) - 1; // avoid artifact on right edge
	winLimitsH = WIN_H(w);
    }
    else
    {
	winLimitsX = BORDER_X(w);
	winLimitsY = BORDER_Y(w);
	winLimitsW = BORDER_W(w);
	winLimitsH = BORDER_H(w);
    }
    float minSize = 20;
    float hexW = winLimitsW / (float)gridSizeX;
    float hexH = winLimitsH / (float)gridSizeY;

    if (hexW < minSize)
	gridSizeX = winLimitsW / minSize;	// int div.
    if (hexH < minSize)
	gridSizeY = winLimitsH / minSize;	// int div.

    int nPolygons = (gridSizeY + 1) * gridSizeX + (gridSizeY + 1) / 2;

    if (pset->nPolygons != nPolygons)
    {
	if (pset->nPolygons > 0)
	    freePolygonObjects(pset);

	pset->nPolygons = nPolygons;

	pset->polygons = calloc(pset->nPolygons, sizeof(PolygonObject));
	if (!pset->polygons)
	{
	    compLogMessage ("animationaddon", CompLogLevelError,
			    "Not enough memory");
	    pset->nPolygons = 0;
	    return FALSE;
	}
    }

    thickness /= w->screen->width;
    pset->thickness = thickness;
    pset->nTotalFrontVertices = 0;

    float cellW = (float)winLimitsW / gridSizeX;
    float cellH = (float)winLimitsH / gridSizeY;
    float halfW = cellW / 2;
    float twoThirdsH = 2*cellH / 3;
    float thirdH = cellH / 3;

    float halfThick = pset->thickness / 2;
    PolygonObject *p = pset->polygons;
    int x, y;

    for (y = 0; y < gridSizeY+1; y++)
    {
	float posY = winLimitsY + cellH * (y);
	int numPolysinRow = (y%2==0) ? gridSizeX : (gridSizeX + 1);
	// Clip polygons to the window dimensions
	float topY, topRightY, topLeftY, bottomY, bottomLeftY, bottomRightY;
	if(y == 0){
	    topY = topRightY = topLeftY = 0;
	    bottomY = twoThirdsH;
	    bottomLeftY = bottomRightY = thirdH;
	} else if(y == gridSizeY){
	    bottomY = bottomLeftY = bottomRightY = 0;
	    topY = -twoThirdsH;
	    topLeftY = topRightY = -thirdH;
	} else {
	    topY = -twoThirdsH;
	    topLeftY = topRightY = -thirdH;
	    bottomLeftY = bottomRightY = thirdH;
	    bottomY = twoThirdsH;
	}

	for (x = 0; x < numPolysinRow; x++, p++)
	{
	    // Clip odd rows when necessary
	    float topLeftX, topRightX, bottomLeftX, bottomRightX;
	    if(y%2 == 1){
		if(x == 0){
		    topLeftX = bottomLeftX = 0;
		    topRightX = halfW;
		    bottomRightX = halfW;
		} else if(x == numPolysinRow-1){
		    topRightX = bottomRightX = 0;
		    topLeftX = -halfW;
		    bottomLeftX = -halfW;
		} else {
		    topLeftX = bottomLeftX = -halfW;
		    topRightX = bottomRightX = halfW;
		}
	    } else {
		topLeftX = bottomLeftX = -halfW;
		topRightX = bottomRightX = halfW;
	    }
			
	    p->centerPos.x = p->centerPosStart.x =
		winLimitsX + cellW * (x + (y%2 ? 0.0 : 0.5));
	    p->centerPos.y = p->centerPosStart.y = posY;
	    p->centerPos.z = p->centerPosStart.z = -halfThick;
	    p->rotAngle = p->rotAngleStart = 0;

	    p->centerRelPos.x = (x + 0.5) / gridSizeX;
	    p->centerRelPos.y = (y + 0.5) / gridSizeY;

	    p->nSides = 6;
	    p->nVertices = 2 * 6;
	    pset->nTotalFrontVertices += 6;

	    // 6 front, 6 back vertices
	    if (!p->vertices)
	    {
		p->vertices = calloc(6 * 2 * 3, sizeof(GLfloat));
		if (!p->vertices)
		{
		    compLogMessage ("animationaddon", CompLogLevelError,
				    "Not enough memory");
		    freePolygonObjects(pset);
		    return FALSE;
		}
	    }

	    // Vertex normals
	    if (!p->normals)
	    {
		p->normals = calloc(6 * 2 * 3, sizeof(GLfloat));
	    }
	    if (!p->normals)
	    {
		compLogMessage ("animationaddon", CompLogLevelError,
				"Not enough memory");
		freePolygonObjects(pset);
		return FALSE;
	    }

	    GLfloat *pv = p->vertices;

	    // Determine 6 front vertices in ccw direction
	    // Starting at top
	    pv[0] = 0;
	    pv[1] = topY;
	    pv[2] = halfThick;

	    pv[3] = topLeftX;
	    pv[4] = topLeftY;
	    pv[5] = halfThick;

	    pv[6] = bottomLeftX;
	    pv[7] = bottomLeftY;
	    pv[8] = halfThick;

	    pv[9] = 0;
	    pv[10] = bottomY;
	    pv[11] = halfThick;

	    pv[12] = bottomRightX;
	    pv[13] = bottomRightY;
	    pv[14] = halfThick;

	    pv[15] = topRightX;
	    pv[16] = topRightY;
	    pv[17] = halfThick;
			
	    // Determine 6 back vertices in cw direction
	    pv[18] = topRightX;
	    pv[19] = topRightY;
	    pv[20] = -halfThick;

	    pv[21] = bottomRightX;
	    pv[22] = bottomRightY;
	    pv[23] = -halfThick;
			
	    pv[24] = 0;
	    pv[25] = bottomY;
	    pv[26] = -halfThick;

	    pv[27] = bottomLeftX;
	    pv[28] = bottomLeftY;
	    pv[29] = -halfThick;
			
	    pv[30] = topLeftX;
	    pv[31] = topLeftY;
	    pv[32] = -halfThick;

	    pv[33] = 0;
	    pv[34] = topY;
	    pv[35] = -halfThick;

	    // 24 indices per 6 sides (for quads)
	    if (!p->sideIndices)
	    {
		p->sideIndices = calloc(4 * 6, sizeof(GLushort));
	    }
	    if (!p->sideIndices)
	    {
		compLogMessage ("animationaddon", CompLogLevelError,
				"Not enough memory");
		freePolygonObjects(pset);
		return FALSE;
	    }

	    GLushort *ind = p->sideIndices;
	    GLfloat *nor = p->normals;

	    int id = 0;

	    // Approximate normals

	    // upper left side face
	    ind[id++] = 11; // First vertex
	    ind[id++] = 10;
	    ind[id++] = 1;
	    ind[id++] = 0;
	    nor[11 * 3 + 0] = -1; // Flat shading only uses 1st vertex's normal
	    nor[11 * 3 + 1] = -1; // in a polygon, vertex 11 for this face.
	    nor[11 * 3 + 2] = 0;
	    if (y == 0) // top half cropped
		nor[11 * 3 + 0] = 0;

	    // left side face
	    ind[id++] = 1;
	    ind[id++] = 10;
	    ind[id++] = 9;
	    ind[id++] = 2;
	    nor[1 * 3 + 0] = -1;
	    nor[1 * 3 + 1] = 0;
	    nor[1 * 3 + 2] = 0;

	    // lower left side face
	    ind[id++] = 2;
	    ind[id++] = 9;
	    ind[id++] = 8;
	    ind[id++] = 3;
	    nor[2 * 3 + 0] = -1;
	    nor[2 * 3 + 1] = 1;
	    nor[2 * 3 + 2] = 0;
	    if (y == gridSizeY) // bottom half cropped
		nor[2 * 3 + 0] = 0;

	    // lower right side face
	    ind[id++] = 3;
	    ind[id++] = 8;
	    ind[id++] = 7;
	    ind[id++] = 4;
	    nor[3 * 3 + 0] = 1;
	    nor[3 * 3 + 1] = 1;
	    nor[3 * 3 + 2] = 0;
	    if (y == gridSizeY) // bottom half cropped
		nor[3 * 3 + 0] = 0;

	    // right side face
	    ind[id++] = 4;
	    ind[id++] = 7;
	    ind[id++] = 6;
	    ind[id++] = 5;
	    nor[4 * 3 + 0] = 1;
	    nor[4 * 3 + 1] = 0;
	    nor[4 * 3 + 2] = 0;

	    // upper right side face
	    ind[id++] = 5;
	    ind[id++] = 6;
	    ind[id++] = 11;
	    ind[id++] = 0;
	    nor[5 * 3 + 0] = 1;
	    nor[5 * 3 + 1] = -1;
	    nor[5 * 3 + 2] = 0;
	    if (y == 0) // top half cropped
		nor[5 * 3 + 0] = 0;

	    // Front face normal
	    nor[0] = 0;
	    nor[1] = 0;
	    nor[2] = 1;

	    // Back face normal
	    nor[6 * 3 + 0] = 0;
	    nor[6 * 3 + 1] = 0;
	    nor[6 * 3 + 2] = -1;

	    // Determine bounding box (to test intersection with clips)
	    p->boundingBox.x1 = topLeftX + p->centerPos.x;
	    p->boundingBox.y1 = topY + p->centerPos.y;
	    p->boundingBox.x2 = ceil(bottomRightX + p->centerPos.x);
	    p->boundingBox.y2 = ceil(bottomY + p->centerPos.y);

	    p->boundSphereRadius = sqrt((topRightX - topLeftX) * (topRightX - topLeftX) / 4 +
					(bottomY - topY) * (bottomY - topY) / 4 +
					halfThick * halfThick);
	}
    }
    if (pset->nPolygons != p - pset->polygons)
	compLogMessage ("animationaddon", CompLogLevelError,
			"%s: Error in tessellateIntoHexagons at line %d!",
			__FILE__, __LINE__);
    return TRUE;
}

void
polygonsStoreClips (CompWindow * w,
		    int nClip, BoxPtr pClip, int nMatrix, CompMatrix * matrix)
{
    ANIMADDON_WINDOW (w);

    PolygonSet *pset = aw->eng.polygonSet;

    // if polygon set is not valid or effect is not 3D (glide w/thickness=0)
    if (!pset)
	return;

    Bool dontStoreClips = TRUE;

    // If this clip doesn't match the corresponding stored clip,
    // clear the stored clips from this point (aw->nClipsPassed)
    // to the end and store the new ones instead.

    if (aw->nClipsPassed < pset->nClips) // if we have clips stored earlier
    {
	Clip4Polygons *c = pset->clips + aw->nClipsPassed;
	// the stored clip at position aw->nClipsPassed

	// if either clip coordinates or texture matrix is different
	if (memcmp(pClip, &c->box, sizeof(Box)) ||
	    memcmp(matrix, &c->texMatrix, sizeof(CompMatrix)))
	{
	    // get rid of the clips from here (aw->nClipsPassed) to the end
	    pset->nClips = aw->nClipsPassed;
	    dontStoreClips = FALSE;
	}
    }
    else
	dontStoreClips = FALSE;

    if (dontStoreClips)
    {
	aw->nClipsPassed += nClip;
	return;
    }

    // For each clip passed to this function
    for (; nClip--; pClip++, aw->nClipsPassed++)
    {
	// New clip

	if (!ensureLargerClipCapacity(pset))
	{
	    compLogMessage ("animationaddon", CompLogLevelError,
			    "Not enough memory");
	    return;
	}

	Clip4Polygons *newClip = &pset->clips[pset->nClips];

	newClip->id = aw->nClipsPassed;
	memcpy(&newClip->box, pClip, sizeof(Box));
	memcpy(&newClip->texMatrix, matrix, sizeof(CompMatrix));
	// nMatrix is not used for now
	// (i.e. only first texture matrix is considered)

	// avoid clipping along window edge
	// for the "window contents" clip
	if (pClip->x1 == BORDER_X(w) &&
	    pClip->y1 == BORDER_Y(w) &&
	    pClip->x2 == BORDER_X(w) + BORDER_W(w) &&
	    pClip->y2 == BORDER_Y(w) + BORDER_H(w))
	{
	    newClip->boxf.x1 = pClip->x1 - 0.1f;
	    newClip->boxf.y1 = pClip->y1 - 0.1f;
	    newClip->boxf.x2 = pClip->x2 + 0.1f;
	    newClip->boxf.y2 = pClip->y2 + 0.1f;
	}
	else
	{
	    newClip->boxf.x1 = pClip->x1;
	    newClip->boxf.y1 = pClip->y1;
	    newClip->boxf.x2 = pClip->x2;
	    newClip->boxf.y2 = pClip->y2;
	}

	pset->nClips++;
	aw->clipsUpdated = TRUE;
    }
}

/*        90        //degree orientation
 *         |
 *    180--+--0
 *         |
 *        270
 * This function tessellates the window into radial shards, with
 * each shard split into the number of "tiers". This forms a broken
 * glass or spiderweb appearance.
 */
Bool
tessellateIntoGlass (CompWindow * w,
		     int spoke_multiplier, int tier_num, float thickness)
{
    ANIMADDON_WINDOW(w);

    PolygonSet *pset = aw->eng.polygonSet;

    int i, j;
    int spoke_num = 4 * spoke_multiplier;
    int winLimitsX, winLimitsY, winLimitsW, winLimitsH;
    float centerX, centerY;
    float  spoke_range;
    float top_bottom_length, left_right_length;

    spoke_t spoke[spoke_num];
    memset (spoke, 0, sizeof (spoke_t) * spoke_num);

    for (i = 0; i < spoke_num; i++)
    {
	spoke[i].spoke_vertex = calloc (tier_num, sizeof (spoke_vertex_t));
    }

    spoke_range = 2 * M_PI / spoke_num;

    if (pset->includeShadows)
    {
	winLimitsX = WIN_X (w);
	winLimitsY = WIN_Y (w);
	winLimitsW = WIN_W (w) - 1; // avoid artifact on right edge
	winLimitsH = WIN_H (w);
    }
    else
    {
	winLimitsX = BORDER_X (w);
	winLimitsY = BORDER_Y (w);
	winLimitsW = BORDER_W (w);
	winLimitsH = BORDER_H (w);
    }

    //tessellation looks horrible if its too small, its better
    //just to skip it
    if (winLimitsW < 100 || winLimitsH < 100)
	return FALSE;

    centerX = (winLimitsW / 2.0) + winLimitsX;
    centerY = (winLimitsH / 2.0) + winLimitsY;

    /* Calculate corner angles */
    float corner_angle[4];
    corner_angle[0] = atanf ((centerY - winLimitsY) /
			     (winLimitsX + winLimitsW - centerX));
    corner_angle[1] = M_PI - corner_angle[0];
    corner_angle[2] = M_PI + corner_angle[0];
    corner_angle[3] = 2 * M_PI - corner_angle[0];

    float range;

    //calculate the vertex positions
    for (i = 0; i < spoke_num; i++)
    {
	/* The spokes must go into the corners. The remaining spokes fit between the corner spokes */
	if ((i % spoke_multiplier) == 0)
	{
	    spoke[i].direction = corner_angle[i / spoke_multiplier];
	}
	else
	{
	    range = corner_angle[((i / spoke_multiplier) + 1) % 4 ] -
		corner_angle[i / spoke_multiplier];
	    if (range < 0)
		range = 2 * M_PI - corner_angle[i / spoke_multiplier] +
		    corner_angle[((i / spoke_multiplier) + 1) % 4 ];

	    spoke[i].direction = corner_angle[i / spoke_multiplier] +
		(i % spoke_multiplier) * range / spoke_multiplier;

	    if (spoke[i].direction > 2 * M_PI)
		spoke[i].direction -= 2 * M_PI;

	    // Random direction
	    spoke[i].direction += range * (float) rand () / 3 / RAND_MAX;
	}


	//calculate the length of the spoke
	//calculate top/bottom lenght
	if ((spoke[i].direction < M_PI))
	    top_bottom_length = (centerY - winLimitsY);
	else
	    top_bottom_length = ((winLimitsY + winLimitsH)- centerY);

	top_bottom_length /= sinf (spoke[i].direction);
	if (top_bottom_length < 0) top_bottom_length *= -1;

	//calculate left right length
	if ((spoke[i].direction < M_PI / 2) || (spoke[i].direction > 3 * M_PI / 2))
	    left_right_length = (winLimitsX + winLimitsW) - centerX;
	else
	    left_right_length = centerX - winLimitsX;

	left_right_length /= cosf (spoke[i].direction);

	if (left_right_length < 0) left_right_length *= -1;

	//take the smaller of the two
	if (left_right_length < top_bottom_length)
	    spoke[i].length = left_right_length;
	else
	    spoke[i].length = top_bottom_length;


	float percent = 1.0 / ((float) tier_num);
	//calculate spoke vertexes
	for (j = 0 ; j < tier_num; j++)
	{
	    spoke[i].spoke_vertex[j].dist = percent * (j + 1) * spoke[i].length;

	    spoke[i].spoke_vertex[j].x =
		centerX + (spoke[i].spoke_vertex[j].dist * cos (spoke[i].direction));
	    spoke[i].spoke_vertex[j].y =
		centerY + (spoke[i].spoke_vertex[j].dist * sin (spoke[i].direction));
	}
    }


    shard_t shards[spoke_num][tier_num];

    //calculate the center and bounds of each polygon
    for (i = 0; i < spoke_num; i++)
    {
	for (j = 0; j < tier_num; j++)
	{
	    switch (j)
	    {
	    case 0:
		//the first tier is triangles
		shards[i][j].is_triangle = TRUE;
		shards[i][j].pt0X = centerX;
		shards[i][j].pt0Y = centerY;

		shards[i][j].pt1X = spoke[i].spoke_vertex[j].x;
		shards[i][j].pt1Y = spoke[i].spoke_vertex[j].y;

		shards[i][j].pt2X = spoke[(i + 1) % spoke_num].spoke_vertex[j].x;
		shards[i][j].pt2Y = spoke[(i + 1) % spoke_num].spoke_vertex[j].y;

		shards[i][j].pt3X = shards[i][j].pt0X;//fourth point is not used
		shards[i][j].pt3Y = shards[i][j].pt0Y;

		//find lengths
		shards[i][j].centerX = (shards[i][j].pt2X + shards[i][j].pt1X + shards[i][j].pt0X)/3;
		shards[i][j].centerY = (shards[i][j].pt2Y + shards[i][j].pt1Y + shards[i][j].pt0Y)/3;

		break;



	    default:
		//the other tiers are 4 sided polygons
		shards[i][j].is_triangle = FALSE;
		shards[i][j].pt0X = spoke[i].spoke_vertex[j - 1].x;
		shards[i][j].pt0Y = spoke[i].spoke_vertex[j - 1].y;

		shards[i][j].pt1X = spoke[i].spoke_vertex[j].x;
		shards[i][j].pt1Y = spoke[i].spoke_vertex[j].y;

		if (i != spoke_num - 1)
		{
		    shards[i][j].pt2X = spoke[i + 1].spoke_vertex[j].x;
		    shards[i][j].pt2Y = spoke[i + 1].spoke_vertex[j].y;

		    shards[i][j].pt3X = spoke[i + 1].spoke_vertex[j - 1].x;
		    shards[i][j].pt3Y = spoke[i + 1].spoke_vertex[j - 1].y;
		}
		else
		{
		    shards[i][j].pt2X = spoke[0].spoke_vertex[j].x;
		    shards[i][j].pt2Y = spoke[0].spoke_vertex[j].y;

		    shards[i][j].pt3X = spoke[0].spoke_vertex[j - 1].x;
		    shards[i][j].pt3Y = spoke[0].spoke_vertex[j - 1].y;
		}

		//calculate the center of the polygon
		shards[i][j].centerX =
		    (shards[i][j].pt0X + shards[i][j].pt1X + shards[i][j].pt2X + shards[i][j].pt3X)/4;
		shards[i][j].centerY =
		    (shards[i][j].pt0Y + shards[i][j].pt1Y + shards[i][j].pt2Y + shards[i][j].pt3Y)/4;

		break;
	    }
	}

    }


    //set up polygons
    if (pset->nPolygons != spoke_num * tier_num)
    {
	if (pset->nPolygons > 0)
	    freePolygonObjects (pset);

	pset->nPolygons = spoke_num * tier_num;

	pset->polygons = calloc (pset->nPolygons, sizeof (PolygonObject));
	if (!pset->polygons)
	{
	    compLogMessage ("animationaddon",
			    CompLogLevelError, "Not enough memory");
	    pset->nPolygons = 0;
	    return FALSE;
	}
    }

    thickness /= w->screen->width;
    pset->thickness = thickness;
    pset->nTotalFrontVertices = 0;

    float halfThick = pset->thickness / 2;
    PolygonObject *p = pset->polygons;
    int xc, yc;

    for (yc = 0; yc <  spoke_num; yc++) //spokes
    {

	for (xc = 0; xc < tier_num; xc++, p++) //tiers
	{
	    p->centerPos.y = p->centerPosStart.y =
		shards[yc][xc].centerY;

	    p->centerPos.x = p->centerPosStart.x =
		shards[yc][xc].centerX;

	    p->centerPos.z = p->centerPosStart.z = -halfThick;
	    p->centerRelPos.x = (shards[yc][xc].centerX - winLimitsX) / winLimitsW;
	    p->centerRelPos.y = (shards[yc][xc].centerY - winLimitsY) / winLimitsH;

	    p->rotAngle = p->rotAngleStart = 0;

	    p->nSides = 4;
	    p->nVertices = 2 * 4;
	    pset->nTotalFrontVertices += 4;

	    // 4 front, 4 back vertices
	    if (!p->vertices)
	    {
		p->vertices = calloc (8 * 3, sizeof (GLfloat));
	    }

	    if (!p->vertices)
	    {
		compLogMessage ("animationaddon",
				CompLogLevelError, "Not enough memory");
		freePolygonObjects (pset);
		return FALSE;
	    }

	    // Vertex normals
	    if (!p->normals)
	    {
		p->normals = calloc (8 * 3, sizeof (GLfloat));
	    }
	    if (!p->normals)
	    {
		compLogMessage ("animationaddon",
				CompLogLevelError,
				"Not enough memory");
		freePolygonObjects (pset);
		return FALSE;
	    }

	    GLfloat *pv = p->vertices;

	    // Determine 4 front vertices in ccw direction
	    pv[0] = -shards[yc][xc].centerX + shards[yc][xc].pt3X;
	    pv[1] = -shards[yc][xc].centerY + shards[yc][xc].pt3Y;
	    pv[2] = halfThick;

	    pv[3] = -shards[yc][xc].centerX + shards[yc][xc].pt2X;
	    pv[4] = -shards[yc][xc].centerY + shards[yc][xc].pt2Y;
	    pv[5] = halfThick;

	    pv[6] = -shards[yc][xc].centerX + shards[yc][xc].pt1X;
	    pv[7] = -shards[yc][xc].centerY + shards[yc][xc].pt1Y;
	    pv[8] = halfThick;

	    pv[9] = -shards[yc][xc].centerX + shards[yc][xc].pt0X;
	    pv[10] = -shards[yc][xc].centerY + shards[yc][xc].pt0Y;
	    pv[11] = halfThick;

	    // Determine 4 back vertices in cw direction
	    pv[12] = -shards[yc][xc].centerX + shards[yc][xc].pt0X;
	    pv[13] = -shards[yc][xc].centerY + shards[yc][xc].pt0Y;
	    pv[14] = -halfThick;

	    pv[15] = -shards[yc][xc].centerX + shards[yc][xc].pt1X;
	    pv[16] = -shards[yc][xc].centerY + shards[yc][xc].pt1Y;
	    pv[17] = -halfThick;

	    pv[18] = -shards[yc][xc].centerX + shards[yc][xc].pt2X;
	    pv[19] = -shards[yc][xc].centerY + shards[yc][xc].pt2Y;
	    pv[20] = -halfThick;

	    pv[21] = -shards[yc][xc].centerX + shards[yc][xc].pt3X;
	    pv[22] = -shards[yc][xc].centerY + shards[yc][xc].pt3Y;
	    pv[23] = -halfThick;

	    // 16 indices for 4 sides (for quads)
	    if (!p->sideIndices)
	    {
		p->sideIndices = calloc (4 * 4, sizeof (GLushort));
	    }
	    if (!p->sideIndices)
	    {
		compLogMessage ("animationaddon",
				CompLogLevelError, "Not enough memory");
		freePolygonObjects (pset);
		return FALSE;
	    }

	    GLushort *ind = p->sideIndices;
	    GLfloat *nor = p->normals;

	    int id = 0;

	    // Left face
	    ind[id++] = 6; // First vertex
	    ind[id++] = 1;
	    ind[id++] = 0;
	    ind[id++] = 7;
	    nor[6 * 3 + 0] = -1; // Flat shading only uses 1st vertex's normal
	    nor[6 * 3 + 1] = 0; // in a polygon, vertex 6 for this face.
	    nor[6 * 3 + 2] = 0;

	    // Bottom face
	    ind[id++] = 1;
	    ind[id++] = 6;
	    ind[id++] = 5;
	    ind[id++] = 2;
	    nor[1 * 3 + 0] = 0;
	    nor[1 * 3 + 1] = 1;
	    nor[1 * 3 + 2] = 0;

	    // Right face
	    ind[id++] = 2;
	    ind[id++] = 5;
	    ind[id++] = 4;
	    ind[id++] = 3;
	    nor[2 * 3 + 0] = 1;
	    nor[2 * 3 + 1] = 0;
	    nor[2 * 3 + 2] = 0;

	    // Top face
	    ind[id++] = 7;
	    ind[id++] = 0;
	    ind[id++] = 3;
	    ind[id++] = 4;
	    nor[7 * 3 + 0] = 0;
	    nor[7 * 3 + 1] = -1;
	    nor[7 * 3 + 2] = 0;

	    // Front face normal
	    nor[0] = 0;
	    nor[1] = 0;
	    nor[2] = 1;

	    // Back face normal
	    nor[4 * 3 + 0] = 0;
	    nor[4 * 3 + 1] = 0;
	    nor[4 * 3 + 2] = -1;

	    // Determine bounding box (to test intersection with clips)
	    p->boundingBox.x1 = p->centerPos.x - shards[yc][xc].pt3X;
	    p->boundingBox.y1 = p->centerPos.y - shards[yc][xc].pt3Y;
	    p->boundingBox.x2 = ceil (shards[yc][xc].pt1X + p->centerPos.x);
	    p->boundingBox.y2 = ceil (shards[yc][xc].pt1Y + p->centerPos.y);

	    float dist[4] = {0}, longest_dist = 0;
	    dist[0] = sqrt (powf ((shards[yc][xc].centerX - shards[yc][xc].pt0X), 2) +
			    powf ((shards[yc][xc].centerY - shards[yc][xc].pt0Y), 2) +
			    powf (thickness,2));
	    dist[1] = sqrt (powf ((shards[yc][xc].centerX - shards[yc][xc].pt1X), 2) +
			    powf ((shards[yc][xc].centerY - shards[yc][xc].pt1Y), 2) +
			    powf (thickness,2));
	    dist[2] = sqrt (powf ((shards[yc][xc].centerX - shards[yc][xc].pt2X), 2) +
			    powf ((shards[yc][xc].centerY - shards[yc][xc].pt2Y), 2) +
			    powf (thickness,2));
	    dist[3] = sqrt (powf ((shards[yc][xc].centerX - shards[yc][xc].pt3X), 2) +
			    powf ((shards[yc][xc].centerY - shards[yc][xc].pt3Y), 2) +
			    powf (thickness,2));

	    int k;
	    for (k = 0; k < 4; k++)
	    {
		if (dist[k] > longest_dist)
		    longest_dist = dist[k];
	    }

	    p->boundSphereRadius = longest_dist;
	}
    }

    return TRUE;
}

// For each rectangular clip, this function finds polygons which
// have a bounding box that intersects the clip. For intersecting
// polygons, it computes the texture coordinates for the vertices
// of that polygon (to draw the clip texture).
static Bool processIntersectingPolygons(CompScreen * s, PolygonSet * pset)
{
    int j;

    for (j = pset->firstNondrawnClip; j < pset->nClips; j++)
    {
	Clip4Polygons *c = pset->clips + j;
	Box *cb = &c->box;
	int nFrontVerticesTilThisPoly = 0;

	c->nIntersectingPolygons = 0;

	// TODO: If it doesn't affect speed much, for each clip,
	// consider doing 2 passes, counting the intersecting polygons
	// in the 1st pass and allocating just enough space for those
	// polygons instead of all polygons in the 2nd pass.
	int i;

	for (i = 0; i < pset->nPolygons; i++)
	{
	    PolygonObject *p = pset->polygons + i;

	    Box *bb = &p->boundingBox;

	    if (bb->x2 <= cb->x1)
		continue;		// no intersection
	    if (bb->y2 <= cb->y1)
		continue;		// no intersection
	    if (bb->x1 >= cb->x2)
		continue;		// no intersection
	    if (bb->y1 >= cb->y2)
		continue;		// no intersection

	    // There is intersection, add clip info

	    if (!c->intersectingPolygons)
	    {
		c->intersectingPolygons =
		    calloc(pset->nPolygons, sizeof(int));
	    }
	    // allocate tex coords
	    // 2 {x, y} * 2 {front, back} * <total # of polygon front vertices>
	    if (!c->polygonVertexTexCoords)
	    {
		c->polygonVertexTexCoords =
		    calloc(2 * 2 * pset->nTotalFrontVertices, sizeof(GLfloat));
	    }
	    if (!c->intersectingPolygons || !c->polygonVertexTexCoords)
	    {
		compLogMessage ("animationaddon", CompLogLevelError,
				"Not enough memory");
		freeClipsPolygons(pset);
		return FALSE;
	    }
	    c->intersectingPolygons[c->nIntersectingPolygons] = i;

	    int k;

	    for (k = 0; k < p->nSides; k++)
	    {
		float x = p->vertices[3 * k] +
		    p->centerPosStart.x;
		float y = p->vertices[3 * k + 1] +
		    p->centerPosStart.y;
		GLfloat tx;
		GLfloat ty;
		if (c->texMatrix.xy != 0.0f || c->texMatrix.yx != 0.0f)
		{	// "non-rect" coordinates
		    tx = COMP_TEX_COORD_XY(&c->texMatrix, x, y);
		    ty = COMP_TEX_COORD_YX(&c->texMatrix, x, y);
		}
		else
		{
		    tx = COMP_TEX_COORD_X(&c->texMatrix, x);
		    ty = COMP_TEX_COORD_Y(&c->texMatrix, y);
		}
		// for front vertices
		int ti = 2 * (2 * nFrontVerticesTilThisPoly + k);

		c->polygonVertexTexCoords[ti] = tx;
		c->polygonVertexTexCoords[ti + 1] = ty;

		// for back vertices
		ti = 2 * (2 * nFrontVerticesTilThisPoly +
			  (2 * p->nSides - 1 - k));
		c->polygonVertexTexCoords[ti] = tx;
		c->polygonVertexTexCoords[ti + 1] = ty;
	    }
	    c->nIntersectingPolygons++;
	    nFrontVerticesTilThisPoly += p->nSides;
	}
    }

    return TRUE;
}

// Correct perspective appearance by skewing
static void
getPerspectiveCorrectionMat (CompWindow *w,
			     PolygonObject *p,
			     GLfloat *mat,
			     CompTransform *matf)
{
    CompScreen *s = w->screen;
    ANIMADDON_SCREEN (s);
    Point center;

    if (p) // for CorrectPerspectivePolygon
    {
	// use polygon's center
	center.x = p->centerPos.x;
	center.y = p->centerPos.y;
    }
    else // for CorrectPerspectiveWindow
    {
	// use window's center
	center.x = WIN_X(w) + WIN_W(w) / 2;
	center.y = WIN_Y(w) + WIN_H(w) / 2;
    }

    GLfloat skewx = -(((center.x - as->output->region.extents.x1) -
		       as->output->width / 2) * 1.15);
    GLfloat skewy = -(((center.y - as->output->region.extents.y1) -
		       as->output->height / 2) * 1.15);

    if (mat)
    {
	// column-major order skew matrix
	GLfloat skewMat[16] =
	    {1,0,0,0,
	     0,1,0,0,
	     skewx,skewy,1,0,
	     0,0,0,1};
	memcpy (mat, skewMat, 16 * sizeof (GLfloat));
    }
    else if (matf)
    {
	// column-major order skew matrix
	float skewMat[16] =
	    {1,0,0,0,
	     0,1,0,0,
	     skewx,skewy,1,0,
	     0,0,0,1};
	memcpy (matf->m, skewMat, 16 * sizeof (float));
    }
}

static void
prepareDrawingForAttrib (CompScreen *s,
			 FragmentAttrib *attrib)
{
    if (s->canDoSaturated && attrib->saturation != COLOR)
    {
	GLfloat constant[4];

	if (s->canDoSlightlySaturated && attrib->saturation > 0)
	{
	    constant[3] = attrib->opacity / 65535.0f;
	    constant[0] = constant[1] = constant[2] = constant[3] *
		attrib->brightness / 65535.0f;

	    glTexEnvfv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);
	}
	else
	{
	    constant[3] = attrib->opacity / 65535.0f;
	    constant[0] = constant[1] = constant[2] = constant[3] *
		attrib->brightness / 65535.0f;

	    constant[0] = 0.5f + 0.5f * RED_SATURATION_WEIGHT   * constant[0];
	    constant[1] = 0.5f + 0.5f * GREEN_SATURATION_WEIGHT * constant[1];
	    constant[2] = 0.5f + 0.5f * BLUE_SATURATION_WEIGHT  * constant[2];

	    glTexEnvfv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);
	}
    }
    else
    {
	attrib->brightness *= 0.76;

	GLushort color = (attrib->opacity * attrib->brightness) >> 16;

	screenTexEnvMode (s, GL_MODULATE);
	glColor4us (color, color, color, attrib->opacity);
    }
}

void
polygonsDrawCustomGeometry (CompWindow * w)
{
    CompScreen *s = w->screen;

    ANIMADDON_DISPLAY (s->display);
    ANIMADDON_WINDOW (w);

    aw->nDrawGeometryCalls++;

    // draw windows only on current viewport unless it's on all viewports
    if ((s->windowOffsetX != 0 || s->windowOffsetY != 0) &&
	!windowOnAllViewports (w))
    {
	return;
	// since this is not the viewport the window was drawn
	// just before animation started
    }
    PolygonSet *pset = aw->eng.polygonSet;

    // if polygon set is not valid or effect is not 3D (glide w/thickness=0)
    if (!pset)
	return;

    // TODO: Fix the source of the crash problem
    // (uninitialized lastClipInGroup)
    // instead of doing this uninitialized value check
    if (pset->firstNondrawnClip < 0 ||
	pset->firstNondrawnClip > pset->nClips ||
	(!aw->clipsUpdated &&
	 (pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] < 0 ||
	  pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] >= pset->nClips)))
    {
	return;
    }

    if (aw->clipsUpdated && aw->nDrawGeometryCalls > 0)
    {
	if (!processIntersectingPolygons(s, pset))
	{
	    return;
	}
    }

    int lastClip;				// last clip to draw

    if (aw->clipsUpdated)
    {
	lastClip = pset->nClips - 1;
    }
    else
    {
	lastClip = pset->lastClipInGroup[aw->nDrawGeometryCalls - 1];
    }

    float forwardProgress = ad->animBaseFunctions->defaultAnimProgress (w);

    // OpenGL stuff starts here

    GLboolean normalArrayWas = FALSE;

    if (pset->thickness > 0)
    {
	glPushAttrib(GL_NORMALIZE);
	glEnable(GL_NORMALIZE);

	normalArrayWas = glIsEnabled(GL_NORMAL_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);
    }

    if (pset->doLighting)
    {
	glPushAttrib(GL_SHADE_MODEL);
	glShadeModel(GL_FLAT);

	glPushAttrib(GL_LIGHT0);
	glPushAttrib(GL_COLOR_MATERIAL);
	glPushAttrib(GL_LIGHTING);
	glEnable(GL_COLOR_MATERIAL);
	glEnable(GL_LIGHTING);

	GLfloat ambientLight[] = { 0.3f, 0.3f, 0.3f, 0.3f };
	GLfloat diffuseLight[] = { 0.9f, 0.9f, 0.9f, 0.9f };
	GLfloat light0Position[] = { -0.5f, 0.5f, 9.0f, 0.0f };

	glLightfv(GL_LIGHT0, GL_AMBIENT, ambientLight);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuseLight);
	glLightfv(GL_LIGHT0, GL_POSITION, light0Position);
    }

    glPushMatrix();

    glPushAttrib(GL_STENCIL_BUFFER_BIT);
    glDisable(GL_STENCIL_TEST);

    if (pset->doDepthTest)
    {
	// Depth test
	glPushAttrib(GL_DEPTH_FUNC);
	glPushAttrib(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_DEPTH_TEST);
    }

    // Clip planes
    GLdouble clipPlane0[] = { 1, 0, 0, 0 };
    GLdouble clipPlane1[] = { 0, 1, 0, 0 };
    GLdouble clipPlane2[] = { -1, 0, 0, 0 };
    GLdouble clipPlane3[] = { 0, -1, 0, 0 };

    // Save old color values
    GLfloat oldColor[4];

    glGetFloatv(GL_CURRENT_COLOR, oldColor);

    // Determine where we are called from in paint.c's drawWindowTexture
    // to find out how we should change the opacity
    GLint prevActiveTexture = GL_TEXTURE0_ARB;
    Bool saturationFull = TRUE;

    if (w->screen->canDoSaturated && aw->com->curPaintAttrib.saturation != COLOR)
    {
	saturationFull = FALSE;
	if (w->screen->canDoSlightlySaturated &&
	    aw->com->curPaintAttrib.saturation > 0)
	{
	    if (aw->com->curPaintAttrib.opacity < OPAQUE ||
		aw->com->curPaintAttrib.brightness != BRIGHT)
		prevActiveTexture = GL_TEXTURE3_ARB;
	    else
		prevActiveTexture = GL_TEXTURE2_ARB;
	}
	else
	    prevActiveTexture = GL_TEXTURE1_ARB;
    }

    float opacity = aw->com->curPaintAttrib.opacity / 65535.0;

    float newOpacity = opacity;
    float fadePassedBy;

    glPushAttrib(GL_BLEND);
    glEnable(GL_BLEND);

    if (saturationFull)
	screenTexEnvMode(w->screen, GL_MODULATE);

    // if fade-out duration is not specified per polygon
    if (pset->allFadeDuration > -1.0f)
    {
	fadePassedBy = forwardProgress - (1 - pset->allFadeDuration);

	// if "fade out starting point" is passed
	if (fadePassedBy > 1e-5)	// if true, allFadeDuration should be > 0
	{
	    float opacityFac;

	    if (aw->deceleratingMotion)
		opacityFac = 1 - ad->animBaseFunctions->decelerateProgress
		    (fadePassedBy / pset->allFadeDuration);
	    else
		opacityFac = 1 - fadePassedBy / pset->allFadeDuration;
	    if (opacityFac < 0)
		opacityFac = 0;
	    if (opacityFac > 1)
		opacityFac = 1;
	    newOpacity = opacity * opacityFac;
	}
    }

    GLfloat skewMat[16];
    if (pset->correctPerspective == CorrectPerspectiveWindow)
	getPerspectiveCorrectionMat (w, NULL, skewMat, NULL);

    int pass;
    // 0: draw opaque ones
    // 2: draw transparent ones
    for (pass = 0; pass < 2; pass++)
    {
	int j;

	for (j = pset->firstNondrawnClip; j <= lastClip; j++)
	{
	    Clip4Polygons *c = pset->clips + j;
	    int nFrontVerticesTilThisPoly = 0;
	    int nNewSides = 0;
	    int i;

	    for (i = 0; i < c->nIntersectingPolygons;
		 i++, nFrontVerticesTilThisPoly += nNewSides)
	    {
		PolygonObject *p =
		    pset->polygons + c->intersectingPolygons[i];
		nNewSides = p->nSides;

		float newOpacityPolygon = newOpacity;

		// if fade-out duration is specified per polygon
		if (pset->allFadeDuration == -1.0f)
		{
		    fadePassedBy = forwardProgress - p->fadeStartTime;
		    // if "fade out starting point" is passed
		    if (fadePassedBy > 1e-5)	// if true, then allFadeDuration > 0
		    {
			float opacityFac;

			if (aw->deceleratingMotion)
			    opacityFac =
				1 - ad->animBaseFunctions->decelerateProgress
				(fadePassedBy / p->fadeDuration);
			else
			    opacityFac = 1 - fadePassedBy / p->fadeDuration;
			if (opacityFac < 0)
			    opacityFac = 0;
			if (opacityFac > 1)
			    opacityFac = 1;
			newOpacityPolygon = newOpacity * opacityFac;
		    }
		}

		if (newOpacityPolygon < 1e-5)	// if polygon object is invisible
		    continue;

		if (pass == 0)
		{
		    if (newOpacityPolygon < 0.9999)	// if not fully opaque
			continue;	// draw only opaque ones in pass 0
		}
		else if (newOpacityPolygon > 0.9999)	// if fully opaque
		    continue;	// draw only non-opaque ones in pass 1

		glPushMatrix();

		if (pset->correctPerspective == CorrectPerspectivePolygon)
		    getPerspectiveCorrectionMat (w, p, skewMat, NULL);

		if (pset->correctPerspective != CorrectPerspectiveNone)
		    glMultMatrixf (skewMat);

		// Center
		glTranslatef(p->centerPos.x, p->centerPos.y, p->centerPos.z);

		// Scale z first
		glScalef(1.0f, 1.0f, 1.0f / s->width);

		if (pset->extraPolygonTransformFunc)
		    pset->extraPolygonTransformFunc (p);

		// Move by "rotation axis offset"
		glTranslatef(p->rotAxisOffset.x, p->rotAxisOffset.y,
			     p->rotAxisOffset.z);

		// Rotate by desired angle
		glRotatef(p->rotAngle, p->rotAxis.x, p->rotAxis.y,
			  p->rotAxis.z);

		// Move back to center
		glTranslatef(-p->rotAxisOffset.x, -p->rotAxisOffset.y,
			     -p->rotAxisOffset.z);

		// Scale back
		glScalef(1.0f, 1.0f, s->width);


		clipPlane0[3] = -(c->boxf.x1 - p->centerPosStart.x);
		clipPlane1[3] = -(c->boxf.y1 - p->centerPosStart.y);
		clipPlane2[3] = (c->boxf.x2 - p->centerPosStart.x);
		clipPlane3[3] = (c->boxf.y2 - p->centerPosStart.y);
		glClipPlane(GL_CLIP_PLANE0, clipPlane0);
		glClipPlane(GL_CLIP_PLANE1, clipPlane1);
		glClipPlane(GL_CLIP_PLANE2, clipPlane2);
		glClipPlane(GL_CLIP_PLANE3, clipPlane3);

		int k;

		for (k = 0; k < 4; k++)
		    glEnable(GL_CLIP_PLANE0 + k);
		Bool fadeBackAndSides =
		    pset->backAndSidesFadeDur > 0 &&
		    forwardProgress <= pset->backAndSidesFadeDur;

		float newOpacityPolygon2 = newOpacityPolygon;

		if (fadeBackAndSides)
		{
		    // Fade-in opacity for back face and sides
		    newOpacityPolygon2 *=
			(forwardProgress / pset->backAndSidesFadeDur);
		}

		FragmentAttrib attrib = aw->com->curPaintAttrib;
		attrib.opacity = newOpacityPolygon2 * OPAQUE;

		prepareDrawingForAttrib (s, &attrib);

		// Draw back face
		glVertexPointer(3, GL_FLOAT, 0, p->vertices + 3 * p->nSides);
		if (pset->thickness > 0)
		    glNormalPointer(GL_FLOAT, 0, p->normals + 3 * p->nSides);
		else
		    glNormal3f (0.0f, 0.0f, -1.0f);
		glTexCoordPointer(2, GL_FLOAT, 0,
				  c->polygonVertexTexCoords +
				  2 * (2 * nFrontVerticesTilThisPoly +
				       p->nSides));
		glDrawArrays(GL_POLYGON, 0, p->nSides);

		// Vertex coords
		glVertexPointer(3, GL_FLOAT, 0, p->vertices);
		if (pset->thickness > 0)
		    glNormalPointer(GL_FLOAT, 0, p->normals);
		else
		    glNormal3f (0.0f, 0.0f, 1.0f);
		glTexCoordPointer(2, GL_FLOAT, 0,
				  c->polygonVertexTexCoords +
				  2 * 2 * nFrontVerticesTilThisPoly);

		// Draw quads for sides
		for (k = 0; k < p->nSides; k++)
		{
		    // GL_QUADS uses a different vertex normal than the first
		    // so I use GL_POLYGON to make sure the normals are right.
		    glDrawElements(GL_POLYGON, 4,
				   GL_UNSIGNED_SHORT,
				   p->sideIndices + k * 4);
		}

		// if opacity was changed just above
		if (fadeBackAndSides)
		{
		    // Go back to normal opacity for front face
		    attrib = aw->com->curPaintAttrib;
		    attrib.opacity = newOpacityPolygon * OPAQUE;
		    prepareDrawingForAttrib (s, &attrib);
		}
		// Draw front face
		glDrawArrays(GL_POLYGON, 0, p->nSides);
		for (k = 0; k < 4; k++)
		    glDisable(GL_CLIP_PLANE0 + k);

		glPopMatrix();
	    }
	}
    }
    // Restore
    // -----------------------------------------

    // Restore old color values
    glColor4f(oldColor[0], oldColor[1], oldColor[2], oldColor[3]);

    glPopAttrib(); // GL_BLEND

    if (pset->doDepthTest)
    {
	glPopAttrib(); // GL_DEPTH_TEST
	glPopAttrib(); // GL_DEPTH_FUNC
    }

    glPopAttrib(); // GL_STENCIL_BUFFER_BIT

    // Restore texture stuff
    if (saturationFull)
	screenTexEnvMode(w->screen, GL_REPLACE);

    glPopMatrix();

    if (pset->doLighting)
    {
	glPopAttrib(); // GL_LIGHTING
	glPopAttrib(); // GL_COLOR_MATERIAL
	glPopAttrib(); // GL_LIGHT0
	glPopAttrib(); // GL_SHADE_MODEL
    }

    if (pset->thickness > 0)
    {
	glPopAttrib(); // GL_NORMALIZE

	if (normalArrayWas)
	    glEnableClientState(GL_NORMAL_ARRAY);
	else
	    glDisableClientState(GL_NORMAL_ARRAY);
    }
    else
	glNormal3f (0.0f, 0.0f, -1.0f);

    if (aw->clipsUpdated)		// set end mark for this group of clips
	pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] = lastClip;

    // Next time, start drawing from next group of clips
    pset->firstNondrawnClip =
	pset->lastClipInGroup[aw->nDrawGeometryCalls - 1] + 1;
}

void
polygonsPrePaintWindow (CompWindow * w)
{
    ANIMADDON_WINDOW (w);

    aw->nDrawGeometryCalls = 0;

    if (aw->eng.polygonSet)
	aw->eng.polygonSet->firstNondrawnClip = 0;
}

void
polygonsPostPaintWindow (CompWindow * w)
{
    ANIMADDON_WINDOW (w);
    if (aw->clipsUpdated &&	// clips should be dropped only in the 1st step
	aw->eng.polygonSet && aw->nDrawGeometryCalls == 0)	// if clips not drawn
    {
	// drop these unneeded clips (e.g. ones passed by blurfx)
	aw->eng.polygonSet->nClips = aw->eng.polygonSet->firstNondrawnClip;
    }
}

// Computes polygon's new position and orientation
// with linear movement
void
polygonsLinearAnimStepPolygon (CompWindow *w,
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

    p->centerPos.x = moveProgress * p->finalRelPos.x + p->centerPosStart.x;
    p->centerPos.y = moveProgress * p->finalRelPos.y + p->centerPosStart.y;
    p->centerPos.z = 1.0f / w->screen->width *
	moveProgress * p->finalRelPos.z + p->centerPosStart.z;

    p->rotAngle = moveProgress * p->finalRotAng + p->rotAngleStart;
}

// Similar to polygonsLinearAnimStepPolygon,
// but slightly ac/decelerates movement
void
polygonsDeceleratingAnimStepPolygon (CompWindow *w,
				     PolygonObject *p,
				     float forwardProgress)
{
    ANIMADDON_DISPLAY (w->screen->display);

    float moveProgress = forwardProgress - p->moveStartTime;

    if (p->moveDuration > 0)
	moveProgress /= p->moveDuration;
    if (moveProgress < 0)
	moveProgress = 0;
    else if (moveProgress > 1)
	moveProgress = 1;

    moveProgress = ad->animBaseFunctions->decelerateProgress (moveProgress);

    p->centerPos.x = moveProgress * p->finalRelPos.x + p->centerPosStart.x;
    p->centerPos.y = moveProgress * p->finalRelPos.y + p->centerPosStart.y;
    p->centerPos.z = 1.0f / w->screen->width *
	moveProgress * p->finalRelPos.z + p->centerPosStart.z;

    p->rotAngle = moveProgress * p->finalRotAng + p->rotAngleStart;
}

static inline AnimStepPolygonProc
getAnimStepPolygonFunc (AnimAddonWindow *aw)
{
    void *extraProp = aw->com->curAnimEffect->properties.extraProperties;
    if (extraProp)
	return ((AnimAddonEffectProperties *)extraProp)->animStepPolygonFunc;
    return &polygonsLinearAnimStepPolygon; // Use linear polygon step by default
}

void
polygonsAnimStep (CompWindow *w, float time)
{
    CompScreen *s = w->screen;

    ANIMADDON_DISPLAY (s->display);
    ANIMADDON_WINDOW (w);

    ad->animBaseFunctions->defaultAnimStep (w, time);

    float forwardProgress = ad->animBaseFunctions->defaultAnimProgress (w);

    if (aw->eng.polygonSet)
    {
	AnimStepPolygonProc polygonStepFunc = getAnimStepPolygonFunc (aw);

	int i;
	for (i = 0; i < aw->eng.polygonSet->nPolygons; i++)
	    polygonStepFunc (w, &aw->eng.polygonSet->polygons[i], forwardProgress);
    }
    else
	compLogMessage ("animationaddon", CompLogLevelDebug,
			"%s: pset null at line %d\n",__FILE__,  __LINE__);
}

void
polygonsUpdateBB (CompOutput *output,
		  CompWindow * w,
		  Box *BB)
{
    CompScreen *s = w->screen;
    ANIMADDON_DISPLAY (s->display);
    ANIMADDON_WINDOW (w);

    PolygonSet *pset = aw->eng.polygonSet;
    if (!pset)
	return;

    CompTransform wTransform;
    CompTransform wTransform2;

    matrixGetIdentity (&wTransform2);
    ad->animBaseFunctions->prepareTransform (s, output, &wTransform,
					     &wTransform2);

    GLdouble dModel[16];
    GLdouble dProjection[16];
    int i;
    for (i = 0; i < 16; i++)
    {
	dProjection[i] = s->projection[i];
    }
    GLint viewport[4] =
	{output->region.extents.x1,
	 output->region.extents.y1,
	 output->width,
	 output->height};
    GLdouble px, py, pz;

    PolygonObject *p = aw->eng.polygonSet->polygons;
    CompTransform *modelViewTransform = &wTransform;

    CompTransform skewMat;
    if (pset->correctPerspective == CorrectPerspectiveWindow)
    {
	getPerspectiveCorrectionMat (w, NULL, NULL, &skewMat);
	matrixMultiply (&wTransform2, &wTransform, &skewMat);
    }
    if (pset->correctPerspective == CorrectPerspectiveWindow ||
	pset->correctPerspective == CorrectPerspectivePolygon)
	modelViewTransform = &wTransform2;

    for (i = 0; i < aw->eng.polygonSet->nPolygons; i++, p++)
    {
	if (pset->correctPerspective == CorrectPerspectivePolygon)
	{
	    getPerspectiveCorrectionMat (w, p, NULL, &skewMat);
	    matrixMultiply (&wTransform2, &wTransform, &skewMat);
	}

	// if modelViewTransform == wTransform2, then
	// it changes for each polygon
	int j;
	for (j = 0; j < 16; j++)
	    dModel[j] = modelViewTransform->m[j];

	Point3d center = p->centerPos;
	float radius = p->boundSphereRadius + 2;

	// Take rotation axis offset into consideration and
	// properly enclose polygon in the bounding cube
	// whatever the rotation angle is:

	// Add rotation axis offset to center (rotated) polygon correctly
	// within bounding cube
	center.x += p->rotAxisOffset.x;
	center.y += p->rotAxisOffset.y;
	center.z += p->rotAxisOffset.z / s->width;

	// Add rotation axis offset to radius to enlarge the bounding cube
	radius += MAX (MAX (fabs(p->rotAxisOffset.x),
			    fabs(p->rotAxisOffset.y)),
		       fabs(p->rotAxisOffset.z));

	float zradius = radius / s->width;

#define N_POINTS 8
	// Corners of bounding cube
	Point3d cubeCorners[N_POINTS] =
	    {{center.x - radius, center.y - radius, center.z + zradius},
	     {center.x - radius, center.y + radius, center.z + zradius},
	     {center.x + radius, center.y - radius, center.z + zradius},
	     {center.x + radius, center.y + radius, center.z + zradius},
	     {center.x - radius, center.y - radius, center.z - zradius},
	     {center.x - radius, center.y + radius, center.z - zradius},
	     {center.x + radius, center.y - radius, center.z - zradius},
	     {center.x + radius, center.y + radius, center.z - zradius}};
	Point3d *pnt = cubeCorners;

	for (j = 0; j < N_POINTS; j++, pnt++)
	{
	    if (!gluProject (pnt->x, pnt->y, pnt->z,
			     dModel, dProjection, viewport,
			     &px, &py, &pz))
		return;

	    py = s->height - py;
	    ad->animBaseFunctions->expandBoxWithPoint (BB, px + 0.5, py + 0.5);
	}
#undef N_POINTS
    }
}

Bool
polygonsAnimInit (CompWindow * w)
{
    ANIMADDON_WINDOW (w);

    aw->deceleratingMotion = (getAnimStepPolygonFunc (aw) ==
			      polygonsDeceleratingAnimStepPolygon);

    // Allocate polygon set if null
    if (!aw->eng.polygonSet)
	aw->eng.polygonSet = calloc(1, sizeof(PolygonSet));

    if (!aw->eng.polygonSet)
    {
	compLogMessage ("animationaddon", CompLogLevelError,
			"Not enough memory");
	return FALSE;
    }
    aw->eng.polygonSet->allFadeDuration = -1.0f;
    return TRUE;
}

Bool
polygonsPrePreparePaintScreen (CompWindow * w,
			       int msSinceLastPaint)
{
    ANIMADDON_WINDOW (w);

    aw->nClipsPassed = 0;
    aw->clipsUpdated = FALSE;

    return FALSE;
}

void
polygonsRefresh (CompWindow * w, Bool animInitialized)
{
    ANIMADDON_WINDOW (w);

    if (aw && aw->eng.polygonSet && !animInitialized)
	// to refresh polygon coords
	freePolygonSet (aw);
}

void
polygonsCleanup (CompWindow * w)
{
    ANIMADDON_WINDOW (w);

    if (aw && aw->eng.polygonSet)
	freePolygonSet(aw);
}

void
polygonsPrePaintOutput (CompScreen *s, CompOutput *output)
{
    ANIMADDON_SCREEN (s);

    as->output = output;

    // Find out if an animation running now uses depth test
    Bool depthUsed = FALSE;
    CompWindow *w;
    for (w = s->windows; w; w = w->next)
    {
	if (w->destroyed)
	    continue;

	ANIMADDON_WINDOW (w);

	if (aw && aw->com &&
	    aw->com->animRemainingTime > 0 &&
	    aw->eng.polygonSet &&
	    aw->eng.polygonSet->doDepthTest)
	{
	    depthUsed = TRUE;
	    break;
	}
    }
    if (depthUsed)
    {
	glClearDepth(1000.0f);
	glClear(GL_DEPTH_BUFFER_BIT);
    }
}

