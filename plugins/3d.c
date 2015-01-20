/*
 *
 * Compiz 3d plugin
 *
 * 3d.c
 *
 * Copyright : (C) 2006 by Roi Cohen
 * E-mail    : roico12@gmail.com
 *
 * Modified by : Dennis Kasprzyk <onestone@opencompositing.org>
 *               Danny Baumann <maniac@opencompositing.org>
 *               Robert Carr <racarr@beryl-project.org>
 *               Diogo Ferreira <diogo@underdev.org>
 *		 Kevin Lange <klange@ogunderground.com>
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
 */

/** TODO:
  1. Add 3d shadows / projections.
  2. Add an option to select z-order of windows not only by viewports,
     but also by screens.
*/

#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <compiz-core.h>
#include <compiz-cube.h>
#include "3d_options.h"

#define PI 3.14159265359f

static int displayPrivateIndex;
static int cubeDisplayPrivateIndex = -1;

typedef struct _tdDisplay
{
    int screenPrivateIndex;
} tdDisplay;

typedef struct _tdWindow
{
    Bool is3D;
    Bool ftb;

    float depth;
} tdWindow;

typedef struct _tdScreen
{
    int windowPrivateIndex;

    PreparePaintScreenProc    preparePaintScreen;
    PaintOutputProc	      paintOutput;
    DonePaintScreenProc	      donePaintScreen;
    ApplyScreenTransformProc  applyScreenTransform;
    PaintWindowProc           paintWindow;

    CubePaintViewportProc     paintViewport;
    CubeShouldPaintViewportProc shouldPaintViewport;

    Bool  active;
    Bool  painting3D;
    float currentScale;

    float basicScale;
    float maxDepth;
    Bool  damage;

    Bool withDepth;

    CompTransform bTransform;
} tdScreen;

#define GET_TD_DISPLAY(d)       \
    ((tdDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define TD_DISPLAY(d)   \
    tdDisplay *tdd = GET_TD_DISPLAY (d)

#define GET_TD_SCREEN(s, tdd)   \
    ((tdScreen *) (s)->base.privates[(tdd)->screenPrivateIndex].ptr)

#define TD_SCREEN(s)    \
    tdScreen *tds = GET_TD_SCREEN (s, GET_TD_DISPLAY (s->display))

#define GET_TD_WINDOW(w, tds)                                     \
    ((tdWindow *) (w)->base.privates[(tds)->windowPrivateIndex].ptr)

#define TD_WINDOW(w)    \
    tdWindow *tdw = GET_TD_WINDOW  (w,                     \
		    GET_TD_SCREEN  (w->screen,             \
		    GET_TD_DISPLAY (w->screen->display)))

static Bool
windowIs3D (CompWindow *w)
{
    if (w->attrib.override_redirect)
	return FALSE;

    if (!(w->shaded || w->attrib.map_state == IsViewable))
	return FALSE;

    if (w->state & (CompWindowStateSkipPagerMask |
		    CompWindowStateSkipTaskbarMask))
	return FALSE;
	
    if (!matchEval (tdGetWindowMatch (w->screen), w))
	return FALSE;

    return TRUE;
}

static void
tdPreparePaintScreen (CompScreen *s,
		      int        msSinceLastPaint)
{
    CompWindow *w;
    Bool       active;

    TD_SCREEN (s);
    CUBE_SCREEN (s);

    active = (cs->rotationState != RotationNone) && s->hsize > 2 &&
	     !(tdGetManualOnly(s) && (cs->rotationState != RotationManual));

    if (active || tds->basicScale != 1.0)
    {
	float maxDiv = (float) tdGetMaxWindowSpace (s) / 100;
	float minScale = (float) tdGetMinCubeSize (s) / 100;
	float x, progress;
	
	(*cs->getRotation) (s, &x, &x, &progress);

	tds->maxDepth = 0;
	for (w = s->windows; w; w = w->next)
	{
	    TD_WINDOW (w);
	    tdw->is3D = FALSE;
	    tdw->depth = 0;

	    if (!windowIs3D (w))
		continue;

	    tdw->is3D = TRUE;
	    tds->maxDepth++;
	    tdw->depth = tds->maxDepth;
	}

	minScale =  MAX (minScale, 1.0 - (tds->maxDepth * maxDiv));
	tds->basicScale = 1.0 - ((1.0 - minScale) * progress);
	tds->damage = (progress != 0.0 && progress != 1.0);
    }
    else
    {
	tds->basicScale = 1.0;
    }

    /* comparing float values with != is error prone, so better cache
       the comparison and allow a small difference */
    tds->active       = (fabs (tds->basicScale - 1.0f) > 1e-4);
    tds->currentScale = tds->basicScale;

    UNWRAP (tds, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (tds, s, preparePaintScreen, tdPreparePaintScreen);

    cs->paintAllViewports |= tds->active;
}

/* forward declaration */
static Bool tdPaintWindow (CompWindow              *w,
			   const WindowPaintAttrib *attrib,
			   const CompTransform     *transform,
			   Region                  region,
			   unsigned int            mask);

#define DOBEVEL(corner) (tdGetBevel##corner (s) ? bevel : 0)

#define ADDQUAD(x1,y1,x2,y2)                                      \
	point.x = x1; point.y = y1;                               \
	matrixMultiplyVector (&tPoint, &point, transform);        \
	glVertex4fv (tPoint.v);                                   \
	point.x = x2; point.y = y2;                               \
	matrixMultiplyVector (&tPoint, &point, transform);        \
	glVertex4fv (tPoint.v);                                   \
	matrixMultiplyVector (&tPoint, &point, &tds->bTransform); \
	glVertex4fv (tPoint.v);                                   \
	point.x = x1; point.y = y1;                               \
	matrixMultiplyVector (&tPoint, &point, &tds->bTransform); \
	glVertex4fv (tPoint.v);                                   \

#define ADDBEVELQUAD(x1,y1,x2,y2,m1,m2)             \
	point.x = x1; point.y = y1;                 \
	matrixMultiplyVector (&tPoint, &point, m1); \
	glVertex4fv (tPoint.v);                     \
	matrixMultiplyVector (&tPoint, &point, m2); \
	glVertex4fv (tPoint.v);                     \
	point.x = x2; point.y = y2;                 \
	matrixMultiplyVector (&tPoint, &point, m2); \
	glVertex4fv (tPoint.v);                     \
	matrixMultiplyVector (&tPoint, &point, m1); \
	glVertex4fv (tPoint.v);                     \

static Bool
tdPaintWindowWithDepth (CompWindow              *w,
		     	const WindowPaintAttrib *attrib,
			const CompTransform     *transform,
			Region                  region,
			unsigned int            mask)
{
    Bool           wasCulled;
    Bool           status;
    int            wx, wy, ww, wh;
    int            bevel, cull, cullInv, temp;
    CompScreen     *s = w->screen;
    CompVector     point, tPoint;
    unsigned short *c;

    TD_SCREEN (s);
    TD_WINDOW (w);
    CUBE_SCREEN (s);

    wasCulled = glIsEnabled (GL_CULL_FACE);

    wx = w->attrib.x - w->input.left;
    wy = w->attrib.y - w->input.top;

    ww = w->width + w->input.left + w->input.right;
    wh = w->height + w->input.top + w->input.bottom;

    bevel = tdGetBevel (s);

    glGetIntegerv (GL_CULL_FACE_MODE, &cull);
    cullInv = (cull == GL_BACK)? GL_FRONT : GL_BACK;

    if (ww && wh && !(mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK) &&
	((cs->paintOrder == FTB && tdw->ftb) ||
	(cs->paintOrder == BTF && !tdw->ftb)))
    {
	/* Paint window depth. */
	glPushMatrix ();
	glLoadIdentity ();

	if (cs->paintOrder == BTF)
	    glCullFace (cullInv);
	
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	
	if (w->id == s->display->activeWindow)
	    c = tdGetWidthColor (s);
	else
	    c = tdGetWidthColorInactive (s);

	temp = c[3] * w->paint.opacity;
	temp /= 0xffff;
	glColor4us (c[0], c[1], c[2], temp);

	point.z = 0.0f;
	point.w = 1.0f;

	glBegin (GL_QUADS);

	/* Top */
	ADDQUAD (wx + ww - DOBEVEL (Topleft), wy + 0.01,
		 wx + DOBEVEL (Topright), wy + 0.01);

	/* Bottom */
	ADDQUAD (wx + DOBEVEL (Bottomleft), wy + wh - 0.01,
		 wx + ww - DOBEVEL (Bottomright), wy + wh - 0.01);

	/* Left */
	ADDQUAD (wx + 0.01, wy + DOBEVEL (Topleft),
		 wx + 0.01, wy + wh - DOBEVEL (Bottomleft));

	/* Right */
	ADDQUAD (wx + ww - 0.01, wy + wh - DOBEVEL (Topright),
		 wx + ww - 0.01, wy + DOBEVEL (Bottomright));

	/* Top left bevel */
	if (tdGetBevelTopleft (s))
	{
	    ADDBEVELQUAD (wx + bevel / 2.0f,
			  wy + bevel - bevel / 1.2f,
			  wx, wy + bevel,
			  &tds->bTransform, transform);

	    ADDBEVELQUAD (wx + bevel / 2.0f,
			  wy + bevel - bevel / 1.2f,
			  wx + bevel, wy,
			  transform, &tds->bTransform);

	}

	/* Bottom left bevel */
	if (tdGetBevelBottomleft (s))
	{
	    ADDBEVELQUAD (wx + bevel / 2.0f,
			  wy + wh - bevel + bevel / 1.2f,
			  wx, wy + wh - bevel,
			  transform, &tds->bTransform);

	    ADDBEVELQUAD (wx + bevel / 2.0f,
			  wy + wh - bevel + bevel / 1.2f,
			  wx + bevel, wy + wh,
			  &tds->bTransform, transform);
	}

	/* Bottom right bevel */
	if (tdGetBevelBottomright (s))
	{
	    ADDBEVELQUAD (wx + ww - bevel / 2.0f,
			  wy + wh - bevel + bevel / 1.2f,
			  wx + ww - bevel, wy + wh,
			  transform, &tds->bTransform);

	    ADDBEVELQUAD (wx + ww - bevel / 2.0f,
			  wy + wh - bevel + bevel / 1.2f,
			  wx + ww, wy + wh - bevel,
			  &tds->bTransform, transform);

	}

	/* Top right bevel */
	if (tdGetBevelTopright (s))
	{
	    ADDBEVELQUAD (wx + ww - bevel, wy,
			  wx + ww - bevel / 2.0f,
			  wy + bevel - bevel / 1.2f,
			  transform, &tds->bTransform);

	    ADDBEVELQUAD (wx + ww, wy + bevel,
			  wx + ww - bevel / 2.0f,
			  wy + bevel - bevel / 1.2f,
			  &tds->bTransform, transform);
	}
	glEnd ();

	glColor4usv (defaultColor);
	glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glPopMatrix ();

	if (cs->paintOrder == BTF)
	    glCullFace (cull);
    }

    UNWRAP(tds, s, paintWindow);
    if (cs->paintOrder == BTF)
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
    else
	status = (*s->paintWindow) (w, attrib, &tds->bTransform, region,
				    mask | PAINT_WINDOW_TRANSFORMED_MASK);
    WRAP (tds, s, paintWindow, tdPaintWindow);

    return status;
}

static Bool
tdPaintWindow (CompWindow              *w,
	       const WindowPaintAttrib *attrib,
	       const CompTransform     *transform,
	       Region                  region,
	       unsigned int            mask)
{
    Bool       status;
    CompScreen *s = w->screen;

    TD_SCREEN (s);
    TD_WINDOW (w);

    if (tdw->depth != 0.0f && !tds->painting3D && tds->active)
	mask |= PAINT_WINDOW_NO_CORE_INSTANCE_MASK;

    if (tds->painting3D && tdGetWidth (s) && (tdw->depth != 0.0f) &&
	tds->withDepth)
    {
	status = tdPaintWindowWithDepth (w, attrib, transform,
					 region, mask);
    }
    else
    {
	UNWRAP (tds, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP(tds, s, paintWindow, tdPaintWindow);
    }

    return status;
}

static void
tdApplyScreenTransform (CompScreen		*s,
			const ScreenPaintAttrib *sAttrib,
			CompOutput		*output,
			CompTransform	        *transform)
{
    TD_SCREEN (s);

    UNWRAP (tds, s, applyScreenTransform);
    (*s->applyScreenTransform) (s, sAttrib, output, transform);
    WRAP (tds, s, applyScreenTransform, tdApplyScreenTransform);

    matrixScale (transform,
		 tds->currentScale, tds->currentScale, tds->currentScale);
}

static void
tdPaintViewport (CompScreen              *s,
		 const ScreenPaintAttrib *sAttrib,
		 const CompTransform     *transform,
		 Region                  region,
		 CompOutput              *output,
		 unsigned int            mask)
{
    TD_SCREEN (s);
    CUBE_SCREEN (s);

    if (cs->paintOrder == BTF)
    {
	UNWRAP (tds, cs, paintViewport);
	(*cs->paintViewport) (s, sAttrib, transform, region, output, mask);
	WRAP (tds, cs, paintViewport, tdPaintViewport);
    }

    if (tds->active)
    {
	CompTransform mTransform;
	CompTransform screenSpace;
	CompTransform screenSpaceOffset;
	CompWindow    *w;
	tdWindow      *tdw;
	CompWalker    walk;
	float         wDepth = 0.0;
	float         pointZ = cs->invert * cs->distance;
	int           offX, offY;
	unsigned int  newMask;

	CompVector vPoints[3] = { { .v = { -0.5, 0.0, pointZ, 1.0 } },
	                          { .v = {  0.0, 0.5, pointZ, 1.0 } },
				  { .v = {  0.0, 0.0, pointZ, 1.0 } } };

	if (tds->withDepth)
	    wDepth = -MIN((tdGetWidth (s)) / 30, (1.0 - tds->basicScale) /
			  tds->maxDepth);

	if (wDepth != 0.0)
	{
	    /* all BTF windows in normal order */
	    for (w = s->windows; w; w = w->next)
	    {
		tdw = (tdWindow *)
		      (w)->base.privates[tds->windowPrivateIndex].ptr;

		if (!tdw->is3D)
		    continue;

		tds->currentScale = tds->basicScale +
		                    (tdw->depth * ((1.0 - tds->basicScale) /
				    tds->maxDepth));

		tdw->ftb = (*cs->checkOrientation) (s, sAttrib, transform,
						    output, vPoints);
	    }
	}

	tds->currentScale = tds->basicScale;
	tds->painting3D   = TRUE;

	screenLighting (s, TRUE);

	(*s->initWindowWalker) (s, &walk);

	matrixGetIdentity (&screenSpace);
	transformToScreenSpace (s, output, -sAttrib->zTranslate,
				&screenSpace);

	glPushMatrix ();

	/* paint all windows from bottom to top */
	for (w = (*walk.first) (s); w; w = (*walk.next) (w))
	{
	    if (w->destroyed)
		continue;

	    if (!w->shaded)
	    {
		if (w->attrib.map_state != IsViewable || !w->damaged)
		    continue;
	    }

	    mTransform = *transform;
	    newMask = PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK;

	    tdw = (tdWindow *) (w)->base.privates[tds->windowPrivateIndex].ptr;

	    if (tdw->depth != 0.0f)
	    {
		tds->currentScale = tds->basicScale +
		                    (tdw->depth * ((1.0 - tds->basicScale) /
						   tds->maxDepth));

		if (wDepth != 0.0)
		{
		    tds->currentScale += wDepth;
		    tds->bTransform   = *transform;
		    (*s->applyScreenTransform) (s, sAttrib, output,
						&tds->bTransform);
		    tds->currentScale -= wDepth;
		}

		(*s->applyScreenTransform) (s, sAttrib, output, &mTransform);
		(*s->enableOutputClipping) (s, &mTransform, region, output);

		if ((s->windowOffsetX != 0 || s->windowOffsetY != 0) &&
		    !windowOnAllViewports (w))
		{
		    getWindowMovementForOffset (w, s->windowOffsetX,
						s->windowOffsetY, &offX, &offY);

		    screenSpaceOffset = screenSpace;
		    matrixTranslate (&screenSpaceOffset, offX, offY, 0);

		    if (wDepth != 0.0)
		        matrixMultiply (&tds->bTransform, &tds->bTransform,
					&screenSpaceOffset);
		    matrixMultiply (&mTransform, &mTransform,
				    &screenSpaceOffset);

		    newMask |= PAINT_WINDOW_WITH_OFFSET_MASK;
		}
		else
		{
		    if (wDepth != 0.0)
			matrixMultiply (&tds->bTransform, &tds->bTransform,
					&screenSpace);
		    matrixMultiply (&mTransform, &mTransform, &screenSpace);
		}

		glLoadMatrixf (mTransform.m);

		(*s->paintWindow) (w, &w->paint, &mTransform, &infiniteRegion,
				   newMask);

		(*s->disableOutputClipping) (s);
	    }
	}

	glPopMatrix ();

	tds->painting3D   = FALSE;
	tds->currentScale = tds->basicScale;
    }

    if (cs->paintOrder == FTB)
    {
	UNWRAP (tds, cs, paintViewport);
	(*cs->paintViewport) (s, sAttrib, transform, region, output, mask);
	WRAP (tds, cs, paintViewport, tdPaintViewport);
    }
}

static Bool
tdShouldPaintViewport (CompScreen              *s,
		       const ScreenPaintAttrib *sAttrib,
		       const CompTransform     *transform,
		       CompOutput              *outputPtr,
		       PaintOrder              order)
{
    Bool rv = FALSE;

    TD_SCREEN (s);
    CUBE_SCREEN (s);

    UNWRAP (tds, cs, shouldPaintViewport);
    rv = (*cs->shouldPaintViewport) (s, sAttrib, transform,
				     outputPtr, order);
    WRAP (tds, cs, shouldPaintViewport, tdShouldPaintViewport);

    if (tds->active)
    {
	float pointZ = cs->invert * cs->distance;
	Bool  ftb1, ftb2;

	CompVector vPoints[3] = { { .v = { -0.5, 0.0, pointZ, 1.0 } },
	                          { .v = {  0.0, 0.5, pointZ, 1.0 } },
				  { .v = {  0.0, 0.0, pointZ, 1.0 } } };

	tds->currentScale = 1.0;
 
	ftb1 = (*cs->checkOrientation) (s, sAttrib, transform,
					outputPtr, vPoints);

	tds->currentScale = tds->basicScale;

	ftb2 = (*cs->checkOrientation) (s, sAttrib, transform,
					outputPtr, vPoints);

	return (order == FTB && (ftb1 || ftb2)) ||
	       (order == BTF && (!ftb1 || !ftb2)) || rv;
    }

    return TRUE;
}

static Bool
tdPaintOutput (CompScreen              *s,
	       const ScreenPaintAttrib *sAttrib,
	       const CompTransform     *transform,
	       Region                  region,
	       CompOutput              *output,
	       unsigned int            mask)
{
    Bool status;

    TD_SCREEN (s);

    if (tds->active)
    {
	CompPlugin *p;

	mask |= PAINT_SCREEN_TRANSFORMED_MASK |
	        PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK |
		PAINT_SCREEN_NO_OCCLUSION_DETECTION_MASK;

	tds->withDepth = TRUE;
	
	p = findActivePlugin ("cubeaddon");
	if (p && p->vTable->getObjectOptions)
	{
	    CompOption *option;
	    int	       nOption;

	    option = (*p->vTable->getObjectOptions) (p, (CompObject *)s,
						     &nOption);
	    option = compFindOption (option, nOption, "deformation", 0);

	    if (option)
		tds->withDepth = option->value.i == 0;
	}
    }


    UNWRAP (tds, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
    WRAP (tds, s, paintOutput, tdPaintOutput);

    return status;
}

static void
tdDonePaintScreen (CompScreen *s)
{
    TD_SCREEN (s);

    if (tds->active && tds->damage)
    {
	tds->damage = FALSE;
	damageScreen (s);
    }

    UNWRAP (tds, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (tds, s, donePaintScreen, tdDonePaintScreen);
}

static Bool
tdInitDisplay (CompPlugin  *p,
	       CompDisplay *d)
{
    tdDisplay *tdd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    if (!checkPluginABI ("cube", CUBE_ABIVERSION))
	return FALSE;

    if (!getPluginDisplayIndex (d, "cube", &cubeDisplayPrivateIndex))
	return FALSE;

    tdd = malloc (sizeof (tdDisplay));
    if (!tdd)
	return FALSE;

    tdd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (tdd->screenPrivateIndex < 0)
    {
	free (tdd);
	return FALSE;
    }

    d->base.privates[displayPrivateIndex].ptr = tdd;

    return TRUE;
}

static void
tdFiniDisplay (CompPlugin  *p,
	       CompDisplay *d)
{
    TD_DISPLAY (d);

    freeScreenPrivateIndex (d, tdd->screenPrivateIndex);

    free (tdd);
}

static Bool
tdInitScreen (CompPlugin *p,
	      CompScreen *s)
{
    tdScreen *tds;

    TD_DISPLAY (s->display);
    CUBE_SCREEN (s);

    tds = malloc (sizeof (tdScreen));
    if (!tds)
	return FALSE;

    tds->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (tds->windowPrivateIndex < 0)
    {
	free (tds);
	return FALSE;
    }

    tds->basicScale     = 1.0f;
    tds->currentScale   = 1.0f;

    tds->active     = FALSE;
    tds->painting3D = FALSE;

    s->base.privates[tdd->screenPrivateIndex].ptr = tds;

    WRAP (tds, s, paintWindow, tdPaintWindow);
    WRAP (tds, s, paintOutput, tdPaintOutput);
    WRAP (tds, s, donePaintScreen, tdDonePaintScreen);
    WRAP (tds, s, preparePaintScreen, tdPreparePaintScreen);
    WRAP (tds, s, applyScreenTransform, tdApplyScreenTransform);
    WRAP (tds, cs, paintViewport, tdPaintViewport);
    WRAP (tds, cs, shouldPaintViewport, tdShouldPaintViewport);

    return TRUE;
}

static void
tdFiniScreen (CompPlugin *p,
	      CompScreen *s)
{
    TD_SCREEN (s);
    CUBE_SCREEN (s);

    UNWRAP (tds, s, paintWindow);
    UNWRAP (tds, s, paintOutput);
    UNWRAP (tds, s, donePaintScreen);
    UNWRAP (tds, s, preparePaintScreen);
    UNWRAP (tds, s, applyScreenTransform);
    UNWRAP (tds, cs, paintViewport);
    UNWRAP (tds, cs, shouldPaintViewport);

    freeWindowPrivateIndex (s, tds->windowPrivateIndex);
	
    free (tds);
}

static Bool
tdInitWindow (CompPlugin *p,
	      CompWindow *w)
{
    tdWindow *tdw;

    TD_SCREEN (w->screen);

    tdw = malloc (sizeof (tdWindow));
    if (!tdw)
	return FALSE;

    tdw->is3D  = FALSE;
    tdw->depth = 0.0f;

    w->base.privates[tds->windowPrivateIndex].ptr = tdw;

    return TRUE;
}

static void
tdFiniWindow (CompPlugin *p,
	      CompWindow *w)
{
    TD_WINDOW (w);

    free (tdw);
}

static Bool
tdInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
tdFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

static CompBool
tdInitObject (CompPlugin *p,
	      CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) tdInitDisplay,
	(InitPluginObjectProc) tdInitScreen,
	(InitPluginObjectProc) tdInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
tdFiniObject (CompPlugin *p,
	      CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) tdFiniDisplay,
	(FiniPluginObjectProc) tdFiniScreen,
	(FiniPluginObjectProc) tdFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static CompPluginVTable tdVTable = {
    "3d",
    0,
    tdInit,
    tdFini,
    tdInitObject,
    tdFiniObject,
    0,
    0
};

CompPluginVTable*
getCompPluginInfo (void)
{
    return &tdVTable;
}

