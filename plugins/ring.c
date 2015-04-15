/*
 *
 * Compiz ring switcher plugin
 *
 * ring.c
 *
 * Copyright : (C) 2007 by Danny Baumann
 * E-mail    : maniac@opencompositing.org
 *
 * Based on scale.c and switcher.c:
 * Copyright : (C) 2007 David Reveman
 * E-mail    : davidr@novell.com
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <X11/Xatom.h>

#include <compiz-core.h>
#include <compiz-text.h>
#include "ring_options.h"

typedef enum {
    RingStateNone = 0,
    RingStateOut,
    RingStateSwitching,
    RingStateIn
} RingState;

typedef enum {
    RingTypeNormal = 0,
    RingTypeGroup,
    RingTypeAll
} RingType;

static int displayPrivateIndex;

typedef struct _RingSlot {
    int   x, y;            /* thumb center coordinates */
    float scale;           /* size scale (fit to maximal thumb size) */
    float depthScale;      /* scale for depth impression */
    float depthBrightness; /* brightness for depth impression */
} RingSlot;

typedef struct _RingDrawSlot {
    CompWindow *w;
    RingSlot   **slot;
} RingDrawSlot;

typedef struct _RingDisplay {
    int		    screenPrivateIndex;
    HandleEventProc handleEvent;
    TextFunc        *textFunc;
} RingDisplay;

typedef struct _RingScreen {
    int windowPrivateIndex;

    PreparePaintScreenProc preparePaintScreen;
    DonePaintScreenProc    donePaintScreen;
    PaintOutputProc        paintOutput;
    PaintWindowProc        paintWindow;
    DamageWindowRectProc   damageWindowRect;

    int  grabIndex;

    RingState state;
    RingType  type;
    Bool      moreAdjust;
    Bool      rotateAdjust;

    Bool paintingSwitcher;

    int     rotTarget;
    int     rotAdjust;
    GLfloat rVelocity;

    /* only used for sorting */
    CompWindow   **windows;
    RingDrawSlot *drawSlots;
    int          windowsSize;
    int          nWindows;

    Window clientLeader;
    CompWindow *selectedWindow;

    /* text display support */
    CompTextData *textData;

    CompMatch match;
    CompMatch *currentMatch;
} RingScreen;

typedef struct _RingWindow {
    RingSlot *slot;

    GLfloat xVelocity;
    GLfloat yVelocity;
    GLfloat scaleVelocity;

    GLfloat tx;
    GLfloat ty;
    GLfloat scale;
    Bool    adjust;
} RingWindow;

#define PI 3.1415926
#define DIST_ROT (3600 / rs->nWindows)
#define ICON_SIZE 64
#define MAX_ICON_SIZE 256

#define GET_RING_DISPLAY(d)				      \
    ((RingDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define RING_DISPLAY(d)		     \
    RingDisplay *rd = GET_RING_DISPLAY (d)

#define GET_RING_SCREEN(s, rd)					  \
    ((RingScreen *) (s)->base.privates[(rd)->screenPrivateIndex].ptr)

#define RING_SCREEN(s)							   \
    RingScreen *rs = GET_RING_SCREEN (s, GET_RING_DISPLAY (s->display))

#define GET_RING_WINDOW(w, rs)					  \
    ((RingWindow *) (w)->base.privates[(rs)->windowPrivateIndex].ptr)

#define RING_WINDOW(w)					       \
    RingWindow *rw = GET_RING_WINDOW  (w,		       \
	             GET_RING_SCREEN  (w->screen,	       \
		     GET_RING_DISPLAY (w->screen->display)))

static Bool
isRingWin (CompWindow *w)
{
    RING_SCREEN (w->screen);

    if (w->destroyed)
	return FALSE;

    if (w->attrib.override_redirect)
	return FALSE;

    if (w->wmType & (CompWindowTypeDockMask | CompWindowTypeDesktopMask))
	return FALSE;

    if (!w->mapNum || w->attrib.map_state != IsViewable)
    {
	if (ringGetMinimized (w->screen))
	{
	    if (!w->minimized && !w->inShowDesktopMode && !w->shaded)
		return FALSE;
	}
	else
    	    return FALSE;
    }

    if (rs->type == RingTypeNormal)
    {
	if (!w->mapNum || w->attrib.map_state != IsViewable)
	{
	    if (w->serverX + w->width  <= 0    ||
		w->serverY + w->height <= 0    ||
		w->serverX >= w->screen->width ||
		w->serverY >= w->screen->height)
		return FALSE;
	}
	else
	{
	    if (!(*w->screen->focusWindow) (w))
		return FALSE;
	}
    }
    else if (rs->type == RingTypeGroup &&
	     rs->clientLeader != w->clientLeader &&
	     rs->clientLeader != w->id)
    {
	return FALSE;
    }

    if (w->state & CompWindowStateSkipTaskbarMask)
	return FALSE;

    if (!matchEval (rs->currentMatch, w))
	return FALSE;

    return TRUE;
}

static void 
ringFreeWindowTitle (CompScreen *s)
{
    RING_SCREEN (s);
    RING_DISPLAY (s->display);

    if (!rs->textData)
	return;

    (rd->textFunc->finiTextData) (s, rs->textData);
    rs->textData = NULL;
}

static void 
ringRenderWindowTitle (CompScreen *s)
{
    CompTextAttrib attrib;
    int            ox1, ox2, oy1, oy2;

    RING_SCREEN (s);
    RING_DISPLAY (s->display);

    ringFreeWindowTitle (s);

    if (!rd->textFunc)
	return;

    if (!rs->selectedWindow)
	return;

    if (!ringGetWindowTitle (s))
	return;

    getCurrentOutputExtents (s, &ox1, &oy1, &ox2, &oy2);

    /* 75% of the output device as maximum width */
    attrib.maxWidth = (ox2 - ox1) * 3 / 4;
    attrib.maxHeight = 100;

    attrib.size = ringGetTitleFontSize (s);
    attrib.color[0] = ringGetTitleFontColorRed (s);
    attrib.color[1] = ringGetTitleFontColorGreen (s);
    attrib.color[2] = ringGetTitleFontColorBlue (s);
    attrib.color[3] = ringGetTitleFontColorAlpha (s);
    attrib.flags = CompTextFlagWithBackground | CompTextFlagEllipsized;
    if (ringGetTitleFontBold (s))
	attrib.flags |= CompTextFlagStyleBold;
    attrib.family = "Sans";
    attrib.bgHMargin = 15;
    attrib.bgVMargin = 15;
    attrib.bgColor[0] = ringGetTitleBackColorRed (s);
    attrib.bgColor[1] = ringGetTitleBackColorGreen (s);
    attrib.bgColor[2] = ringGetTitleBackColorBlue (s);
    attrib.bgColor[3] = ringGetTitleBackColorAlpha (s);


    rs->textData = (rd->textFunc->renderWindowTitle) (s,
						      (rs->selectedWindow ?
						       rs->selectedWindow->id :
						       None),
						      rs->type == RingTypeAll,
						      &attrib);
}

static void
ringDrawWindowTitle (CompScreen *s)
{
    float      x, y;
    int        ox1, ox2, oy1, oy2;

    RING_SCREEN (s);
    RING_DISPLAY (s->display);

    getCurrentOutputExtents (s, &ox1, &oy1, &ox2, &oy2);

    x = ox1 + ((ox2 - ox1) / 2) - ((int)rs->textData->width / 2);

    /* assign y (for the lower corner!) according to the setting */
    switch (ringGetTitleTextPlacement (s))
    {
	case TitleTextPlacementCenteredOnScreen:
	    y = oy1 + ((oy2 - oy1) / 2) + ((int)rs->textData->height / 2);
	    break;
	case TitleTextPlacementAboveRing:
	case TitleTextPlacementBelowRing:
	    {
		XRectangle workArea;
		getWorkareaForOutput (s, s->currentOutputDev, &workArea);

	    	if (ringGetTitleTextPlacement (s) ==
		    TitleTextPlacementAboveRing)
    		    y = oy1 + workArea.y + (int)rs->textData->height;
		else
		    y = oy1 + workArea.y + workArea.height;
	    }
	    break;
	default:
	    return;
	    break;
    }

    (rd->textFunc->drawText) (s, rs->textData, floor (x), floor (y), 1.0f);
}

static Bool
ringPaintWindow (CompWindow		 *w,
       		 const WindowPaintAttrib *attrib,
		 const CompTransform	 *transform,
		 Region		         region,
		 unsigned int		 mask)
{
    CompScreen *s = w->screen;
    Bool       status;

    RING_SCREEN (s);

    if (rs->state != RingStateNone)
    {
	WindowPaintAttrib sAttrib = *attrib;
	Bool		  scaled = FALSE;

	RING_WINDOW (w);

    	if (w->mapNum)
	{
	    if (!w->texture->pixmap && !w->bindFailed)
		bindWindow (w);
	}

	if (rw->adjust || rw->slot)
	{
	    scaled = rw->adjust || (rw->slot && rs->paintingSwitcher);
	    mask |= PAINT_WINDOW_NO_CORE_INSTANCE_MASK;
	}
	else if (rs->state != RingStateIn)
	{
	    if (ringGetDarkenBack (s))
	    {
		/* modify brightness of the other windows */
		sAttrib.brightness = sAttrib.brightness / 2;
	    }
	}

	UNWRAP (rs, s, paintWindow);
	status = (*s->paintWindow) (w, &sAttrib, transform, region, mask);
	WRAP (rs, s, paintWindow, ringPaintWindow);

	if (scaled && w->texture->pixmap)
	{
	    FragmentAttrib fragment;
	    CompTransform  wTransform = *transform;

	    if (mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK)
		return FALSE;

	    initFragmentAttrib (&fragment, &w->lastPaint);

	    if (rw->slot)
	    {
    		fragment.brightness = (float) fragment.brightness * 
		                      rw->slot->depthBrightness;

		if (w != rs->selectedWindow)
		    fragment.opacity = (float)fragment.opacity *
			               ringGetInactiveOpacity (s) / 100;
	    }

	    if (w->alpha || fragment.opacity != OPAQUE)
		mask |= PAINT_WINDOW_TRANSLUCENT_MASK;

	    matrixTranslate (&wTransform, w->attrib.x, w->attrib.y, 0.0f);
	    matrixScale (&wTransform, rw->scale, rw->scale, 1.0f);
	    matrixTranslate (&wTransform,
			     rw->tx / rw->scale - w->attrib.x,
			     rw->ty / rw->scale - w->attrib.y,
			     0.0f);

	    glPushMatrix ();
	    glLoadMatrixf (wTransform.m);

	    (*s->drawWindow) (w, &wTransform, &fragment, region,
			      mask | PAINT_WINDOW_TRANSFORMED_MASK);

	    glPopMatrix ();
	}

	if (scaled && (rs->state != RingStateIn) &&
	    ((ringGetOverlayIcon (s) != OverlayIconNone) || 
	     !w->texture->pixmap))
	{
	    CompIcon *icon;

	    icon = getWindowIcon (w, MAX_ICON_SIZE, MAX_ICON_SIZE);
	    if (!icon)
		icon = w->screen->defaultIcon;

	    if (icon && (icon->texture.name || iconToTexture (w->screen, icon)))
	    {
		REGION              iconReg;
		CompMatrix          matrix;
		float               scale;
		float               x, y;
		int                 width, height;
		int                 scaledWinWidth, scaledWinHeight;
		RingOverlayIconEnum iconOverlay;

		scaledWinWidth  = w->attrib.width  * rw->scale;
		scaledWinHeight = w->attrib.height * rw->scale;

		if (!w->texture->pixmap)
		    iconOverlay = OverlayIconBig;
		else
		    iconOverlay = ringGetOverlayIcon (s);

	    	switch (iconOverlay) {
    		case OverlayIconNone:
		case OverlayIconEmblem:
		    scale = (rw->slot) ? rw->slot->depthScale : 1.0f;
		    scale = MIN ((scale * ICON_SIZE / icon->width),
				 (scale * ICON_SIZE / icon->height));
		    break;
		case OverlayIconBig:
		default:
		    if (w->texture->pixmap)
		    {
			/* only change opacity if not painting an
			   icon for a minimized window */
			sAttrib.opacity /= 3;
			scale = MIN (((float) scaledWinWidth / icon->width),
				     ((float) scaledWinHeight / icon->height));
		    }
		    else
		    {
			scale =
			    0.8f * ((rw->slot) ? rw->slot->depthScale : 1.0f);
			scale = MIN ((scale * ringGetThumbWidth (s) /
				      icon->width),
				     (scale * ringGetThumbHeight (s) /
				      icon->height));
		    }
		    break;
		}

		width  = icon->width  * scale;
		height = icon->height * scale;

	    	switch (iconOverlay) {
		case OverlayIconNone:
		case OverlayIconEmblem:
		    x = w->attrib.x + scaledWinWidth - width;
		    y = w->attrib.y + scaledWinHeight - height;
		    break;
		case OverlayIconBig:
		default:
		    x = w->attrib.x + scaledWinWidth / 2 - width / 2;
		    y = w->attrib.y + scaledWinHeight / 2 - height / 2;
		    break;
		}

		x += rw->tx;
		y += rw->ty;

		mask |=
		    PAINT_WINDOW_BLEND_MASK | PAINT_WINDOW_TRANSFORMED_MASK;

		iconReg.rects    = &iconReg.extents;
		iconReg.numRects = 1;

		iconReg.extents.x1 = w->attrib.x;
		iconReg.extents.y1 = w->attrib.y;
		iconReg.extents.x2 = w->attrib.x + icon->width;
		iconReg.extents.y2 = w->attrib.y + icon->height;

		matrix = icon->texture.matrix;
		matrix.x0 -= (w->attrib.x * icon->texture.matrix.xx);
		matrix.y0 -= (w->attrib.y * icon->texture.matrix.yy);

		w->vCount = w->indexCount = 0;
		(*w->screen->addWindowGeometry) (w, &matrix, 1,
	    					 &iconReg, &infiniteRegion);

		if (w->vCount)
		{
		    FragmentAttrib fragment;
		    CompTransform  wTransform = *transform;

		    if (!w->texture->pixmap)
			sAttrib.opacity = w->paint.opacity;

		    initFragmentAttrib (&fragment, &sAttrib);

		    if (rw->slot)
			fragment.brightness = (float) fragment.brightness * 
			                      rw->slot->depthBrightness;

		    matrixTranslate (&wTransform, 
				     w->attrib.x, w->attrib.y, 0.0f);
		    matrixScale (&wTransform, scale, scale, 1.0f);
		    matrixTranslate (&wTransform,
				     (x - w->attrib.x) / scale - w->attrib.x,
				     (y - w->attrib.y) / scale - w->attrib.y,
				     0.0f);

		    glPushMatrix ();
		    glLoadMatrixf (wTransform.m);

		    (*w->screen->drawWindowTexture) (w, &icon->texture,
						     &fragment, mask);

		    glPopMatrix ();
		}
	    }
	}
    }
    else
    {
	UNWRAP (rs, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP (rs, s, paintWindow, ringPaintWindow);
    }

    return status;
}

static inline float 
ringLinearInterpolation (float valX, 
			 float minX, float maxX, 
			 float minY, float maxY)
{
    double factor = (maxY - minY) / (maxX - minX);
    return (minY + (factor * (valX - minX)));
}

static void
switchActivateEvent (CompScreen *s,
		     Bool	activating)
{
    CompOption o[2];

    o[0].type = CompOptionTypeInt;
    o[0].name = "root";
    o[0].value.i = s->root;

    o[1].type = CompOptionTypeBool;
    o[1].name = "active";
    o[1].value.b = activating;

    (*s->display->handleCompizEvent) (s->display, "ring", "activate", o, 2);
}

static int
compareWindows (const void *elem1,
		const void *elem2)
{
    CompWindow *w1 = *((CompWindow **) elem1);
    CompWindow *w2 = *((CompWindow **) elem2);

    if (w1->mapNum && !w2->mapNum)
	return -1;

    if (w2->mapNum && !w1->mapNum)
	return 1;

    return w2->activeNum - w1->activeNum;
}

static int 
compareRingWindowDepth (const void *elem1, 
			const void *elem2)
{
    RingSlot *a1   = *(((RingDrawSlot *) elem1)->slot);
    RingSlot *a2   = *(((RingDrawSlot *) elem2)->slot);

    if (a1->y < a2->y)
	return -1;
    else if (a1->y > a2->y)
	return 1;
    else
	return 0;
}

static Bool
layoutThumbs (CompScreen *s)
{
    CompWindow *w;
    float      baseAngle, angle;
    int        index;
    int        ww, wh;
    float      xScale, yScale;
    int        ox1, ox2, oy1, oy2;
    int        centerX, centerY;
    int        ellipseA, ellipseB;
    
    RING_SCREEN (s);

    if ((rs->state == RingStateNone) || (rs->state == RingStateIn))
	return FALSE;

    baseAngle = (2 * PI * rs->rotTarget) / 3600;

    getCurrentOutputExtents (s, &ox1, &oy1, &ox2, &oy2);

    /* the center of the ellipse is in the middle 
       of the used output device */
    centerX = ox1 + (ox2 - ox1) / 2;
    centerY = oy1 + (oy2 - oy1) / 2;
    ellipseA = ((ox2 - ox1) * ringGetRingWidth (s)) / 200;
    ellipseB = ((oy2 - oy1) * ringGetRingHeight (s)) / 200;

    for (index = 0; index < rs->nWindows; index++)
    {
	w = rs->windows[index];

	RING_WINDOW (w);

	if (!rw->slot)
	    rw->slot = malloc (sizeof (RingSlot));

	if (!rw->slot)
	    return FALSE;

	/* we subtract the angle from the base angle 
	   to order the windows clockwise */
	angle = baseAngle - (index * (2 * PI / rs->nWindows));

	rw->slot->x = centerX + (ringGetRingClockwise (s) ? -1 : 1) * 
	                        ((float) ellipseA * sin (angle));
	rw->slot->y = centerY + ((float) ellipseB * cos (angle));

	ww = w->attrib.width  + w->input.left + w->input.right;
	wh = w->attrib.height + w->input.top  + w->input.bottom;

	if (ww > ringGetThumbWidth (s))
	    xScale = (float)(ringGetThumbWidth (s)) / (float) ww;
	else
	    xScale = 1.0f;

	if (wh > ringGetThumbHeight (s))
	    yScale = (float)(ringGetThumbHeight (s)) / (float) wh;
	else
	    yScale = 1.0f;

	rw->slot->scale = MIN (xScale, yScale);

	/* scale and brightness are obtained by doing a linear inter-
	   polation - the y positions are the x values for the interpolation
	   (the larger Y is, the nearer is the window), and scale/brightness
	   are the y values for the interpolation */
	rw->slot->depthScale = 
	    ringLinearInterpolation (rw->slot->y, 
				     centerY - ellipseB, centerY + ellipseB, 
				     ringGetMinScale (s), 1.0f);

	rw->slot->depthBrightness = 
	    ringLinearInterpolation (rw->slot->y, 
				     centerY - ellipseB, centerY + ellipseB, 
				     ringGetMinBrightness (s), 1.0f);

	rs->drawSlots[index].w    = w;
	rs->drawSlots[index].slot = &rw->slot;
    }

    /* sort the draw list so that the windows with the 
       lowest Y value (the windows being farest away)
       are drawn first */
    qsort (rs->drawSlots, rs->nWindows, sizeof (RingDrawSlot), 
	   compareRingWindowDepth);

    return TRUE;
}

static void
ringAddWindowToList (CompScreen *s,
		     CompWindow *w)
{
    RING_SCREEN (s);

    if (rs->windowsSize <= rs->nWindows)
    {
	rs->windows = realloc (rs->windows,
			       sizeof (CompWindow *) * (rs->nWindows + 32));
	if (!rs->windows)
	    return;

	rs->drawSlots = realloc (rs->drawSlots,
				 sizeof (RingDrawSlot) * (rs->nWindows + 32));

	if (!rs->drawSlots)
	    return;

	rs->windowsSize = rs->nWindows + 32;
    }

    rs->windows[rs->nWindows++] = w;
}

static Bool
ringUpdateWindowList (CompScreen *s)
{
    int i;
    RING_SCREEN (s);

    qsort (rs->windows, rs->nWindows, sizeof (CompWindow *), compareWindows);

    rs->rotTarget = 0;
    for (i = 0; i < rs->nWindows; i++)
    {
	if (rs->windows[i] == rs->selectedWindow)
	    break;

	rs->rotTarget += DIST_ROT;
    }

    return layoutThumbs (s);
}

static Bool
ringCreateWindowList (CompScreen *s)
{
    CompWindow *w;

    RING_SCREEN (s);

    rs->nWindows = 0;

    for (w = s->windows; w; w = w->next)
    {
	if (isRingWin (w))
	{
	    RING_WINDOW (w);

	    ringAddWindowToList (s, w);
	    rw->adjust = TRUE;
	}
    }

    return ringUpdateWindowList (s);
}

static void
switchToWindow (CompScreen *s,
		Bool	   toNext)
{
    CompWindow *w;
    int	       cur;

    RING_SCREEN (s);

    if (!rs->grabIndex)
	return;

    for (cur = 0; cur < rs->nWindows; cur++)
    {
	if (rs->windows[cur] == rs->selectedWindow)
	    break;
    }

    if (cur == rs->nWindows)
	return;

    if (toNext)
	w = rs->windows[(cur + 1) % rs->nWindows];
    else
	w = rs->windows[(cur + rs->nWindows - 1) % rs->nWindows];

    if (w)
    {
	CompWindow *old = rs->selectedWindow;

	rs->selectedWindow = w;
	if (old != w)
	{
	    if (toNext)
		rs->rotAdjust += DIST_ROT;
	    else
		rs->rotAdjust -= DIST_ROT;

	    rs->rotateAdjust = TRUE;

	    damageScreen (s);
	    ringRenderWindowTitle (s);
	}
    }
}

static int
ringCountWindows (CompScreen *s)
{
    CompWindow *w;
    int	       count = 0;

    for (w = s->windows; w; w = w->next)
    {
	if (isRingWin (w))
	    count++;
    }

    return count;
}

static int
adjustRingRotation (CompScreen *s,
		    float      chunk)
{
    float dx, adjust, amount;
    int   change;

    RING_SCREEN(s);

    dx = rs->rotAdjust;

    adjust = dx * 0.15f;
    amount = fabs (dx) * 1.5f;
    if (amount < 0.2f)
	amount = 0.2f;
    else if (amount > 2.0f)
	amount = 2.0f;

    rs->rVelocity = (amount * rs->rVelocity + adjust) / (amount + 1.0f);

    if (fabs (dx) < 0.1f && fabs (rs->rVelocity) < 0.2f)
    {
	rs->rVelocity = 0.0f;
	rs->rotTarget += rs->rotAdjust;
	rs->rotAdjust = 0;
	return 0;
    }

    change = rs->rVelocity * chunk;
    if (!change)
    {
	if (rs->rVelocity)
	    change = (rs->rotAdjust > 0) ? 1 : -1;
    }

    rs->rotAdjust -= change;
    rs->rotTarget += change;

    if (!layoutThumbs (s))
	return FALSE;

    return TRUE;
}

static int
adjustRingVelocity (CompWindow *w)
{
    float dx, dy, ds, adjust, amount;
    float x1, y1, scale;

    RING_WINDOW (w);

    if (rw->slot)
    {
	scale = rw->slot->scale * rw->slot->depthScale;
	x1 = rw->slot->x - (w->attrib.width * scale) / 2;
	y1 = rw->slot->y - (w->attrib.height * scale) / 2;
    }
    else
    {
	scale = 1.0f;
	x1 = w->attrib.x;
	y1 = w->attrib.y;
    }

    dx = x1 - (w->attrib.x + rw->tx);

    adjust = dx * 0.15f;
    amount = fabs (dx) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    rw->xVelocity = (amount * rw->xVelocity + adjust) / (amount + 1.0f);

    dy = y1 - (w->attrib.y + rw->ty);

    adjust = dy * 0.15f;
    amount = fabs (dy) * 1.5f;
    if (amount < 0.5f)
	amount = 0.5f;
    else if (amount > 5.0f)
	amount = 5.0f;

    rw->yVelocity = (amount * rw->yVelocity + adjust) / (amount + 1.0f);

    ds = scale - rw->scale;
    adjust = ds * 0.1f;
    amount = fabs (ds) * 7.0f;
    if (amount < 0.01f)
	amount = 0.01f;
    else if (amount > 0.15f)
	amount = 0.15f;

    rw->scaleVelocity = (amount * rw->scaleVelocity + adjust) /
	(amount + 1.0f);

    if (fabs (dx) < 0.1f && fabs (rw->xVelocity) < 0.2f &&
	fabs (dy) < 0.1f && fabs (rw->yVelocity) < 0.2f &&
	fabs (ds) < 0.001f && fabs (rw->scaleVelocity) < 0.002f)
    {
	rw->xVelocity = rw->yVelocity = rw->scaleVelocity = 0.0f;
	rw->tx = x1 - w->attrib.x;
	rw->ty = y1 - w->attrib.y;
	rw->scale = scale;

	return 0;
    }

    return 1;
}

static Bool
ringPaintOutput (CompScreen		  *s,
		 const ScreenPaintAttrib *sAttrib,
		 const CompTransform	  *transform,
		 Region		          region,
		 CompOutput		  *output,
		 unsigned int		  mask)
{
    Bool status;

    RING_SCREEN (s);

    if (rs->state != RingStateNone)
	mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;

    UNWRAP (rs, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
    WRAP (rs, s, paintOutput, ringPaintOutput);

    if (rs->state != RingStateNone)
    {
	int           i;
	CompTransform sTransform = *transform;

	transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);
	glPushMatrix ();
	glLoadMatrixf (sTransform.m);

	rs->paintingSwitcher = TRUE;

	for (i = 0; i < rs->nWindows; i++)
	{
	    if (rs->drawSlots[i].slot && *(rs->drawSlots[i].slot))
	    {
		CompWindow *w = rs->drawSlots[i].w;

		(*s->paintWindow) (w, &w->paint, &sTransform,
				   &infiniteRegion, 0);
	    }
	}

	rs->paintingSwitcher = FALSE;

	if (rs->textData && rs->state != RingStateIn)
	    ringDrawWindowTitle (s);
	
	glPopMatrix ();
    }

    return status;
}

static void
ringPreparePaintScreen (CompScreen *s,
			int	    msSinceLastPaint)
{
    RING_SCREEN (s);

    if (rs->state != RingStateNone && (rs->moreAdjust || rs->rotateAdjust))
    {
	CompWindow *w;
	int        steps;
	float      amount, chunk;

	amount = msSinceLastPaint * 0.05f * ringGetSpeed (s);
	steps  = amount / (0.5f * ringGetTimestep (s));

	if (!steps) 
	    steps = 1;
	chunk  = amount / (float) steps;

	while (steps--)
	{
	    rs->rotateAdjust = adjustRingRotation (s, chunk);
	    rs->moreAdjust = FALSE;

	    for (w = s->windows; w; w = w->next)
	    {
		RING_WINDOW (w);

		if (rw->adjust)
		{
		    rw->adjust = adjustRingVelocity (w);

		    rs->moreAdjust |= rw->adjust;

		    rw->tx += rw->xVelocity * chunk;
		    rw->ty += rw->yVelocity * chunk;
		    rw->scale += rw->scaleVelocity * chunk;
		}
		else if (rw->slot)
		{
		    rw->scale = rw->slot->scale * rw->slot->depthScale;
	    	    rw->tx = rw->slot->x - w->attrib.x -
			     (w->attrib.width * rw->scale) / 2;
	    	    rw->ty = rw->slot->y - w->attrib.y - 
			     (w->attrib.height * rw->scale) / 2;
		}
	    }

	    if (!rs->moreAdjust && !rs->rotateAdjust)
		break;
	}
    }

    UNWRAP (rs, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (rs, s, preparePaintScreen, ringPreparePaintScreen);
}

static void
ringDonePaintScreen (CompScreen *s)
{
    RING_SCREEN (s);

    if (rs->state != RingStateNone)
    {
	if (rs->moreAdjust)
	{
	    damageScreen (s);
	}
	else
	{
	    if (rs->rotateAdjust)
		damageScreen (s);

	    if (rs->state == RingStateIn)
		rs->state = RingStateNone;
	    else if (rs->state == RingStateOut)
		rs->state = RingStateSwitching;
	}

	if (rs->state == RingStateNone)
	    switchActivateEvent (s, FALSE);
    }

    UNWRAP (rs, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (rs, s, donePaintScreen, ringDonePaintScreen);
}

static Bool
ringTerminate (CompDisplay     *d,
	       CompAction      *action,
	       CompActionState state,
	       CompOption      *option,
	       int	        nOption)
{
    CompScreen *s;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    for (s = d->screens; s; s = s->next)
    {
	RING_SCREEN (s);

	if (xid && s->root != xid)
	    continue;

	if (rs->grabIndex)
    	{
    	    removeScreenGrab (s, rs->grabIndex, 0);
    	    rs->grabIndex = 0;
	}

	if (rs->state != RingStateNone)
    	{
    	    CompWindow *w;

    	    for (w = s->windows; w; w = w->next)
    	    {
    		RING_WINDOW (w);
    
		if (rw->slot)
		{
		    free (rw->slot);
		    rw->slot = NULL;

    		    rw->adjust = TRUE;
		}
	    }
	    rs->moreAdjust = TRUE;
    	    rs->state = RingStateIn;
    	    damageScreen (s);

	    if (!(state & CompActionStateCancel) &&
		rs->selectedWindow && !rs->selectedWindow->destroyed)
	    {
		sendWindowActivationRequest (s, rs->selectedWindow->id);
	    }
	}
    }

    if (action)
	action->state &= ~(CompActionStateTermKey |
			   CompActionStateTermButton |
			   CompActionStateTermEdge);

    return FALSE;
}

static Bool
ringInitiate (CompScreen      *s,
 	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int	      nOption)
{
    CompMatch *match;
    int       count; 

    RING_SCREEN (s);

    if (otherScreenGrabExist (s, "ring", NULL))
	return FALSE;
	   
    rs->currentMatch = ringGetWindowMatch (s);

    match = getMatchOptionNamed (option, nOption, "match", NULL);
    if (match)
    {
	matchFini (&rs->match);
	matchInit (&rs->match);
	if (matchCopy (&rs->match, match))
	{
	    matchUpdate (s->display, &rs->match);
	    rs->currentMatch = &rs->match;
	}
    }

    count = ringCountWindows (s);

    if (count < 1)
	return FALSE;

    if (!rs->grabIndex)
    {
	if (ringGetSelectWithMouse (s))
	    rs->grabIndex = pushScreenGrab (s, s->normalCursor, "ring");
	else
	    rs->grabIndex = pushScreenGrab (s, s->invisibleCursor, "ring");
    }

    if (rs->grabIndex)
    {
	rs->state = RingStateOut;

	if (!ringCreateWindowList (s))
	    return FALSE;

	rs->selectedWindow = rs->windows[0];
	ringRenderWindowTitle (s);
	rs->rotTarget = 0;

    	rs->moreAdjust = TRUE;
	damageScreen (s);

	switchActivateEvent (s, TRUE);
    }

    return TRUE;
}

static Bool
ringDoSwitch (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption,
	      Bool            nextWindow,
	      RingType        type)
{
    CompScreen *s;
    Window     xid;
    Bool       ret = TRUE;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    s = findScreenAtDisplay (d, xid);
    if (s)
    {
	RING_SCREEN (s);

	if ((rs->state == RingStateNone) || (rs->state == RingStateIn))
	{
	    if (type == RingTypeGroup)
	    {
    		CompWindow *w;
    		w = findWindowAtDisplay (d, getIntOptionNamed (option, nOption,
    							       "window", 0));
    		if (w)
    		{
    		    rs->type = RingTypeGroup;
    		    rs->clientLeader = 
			(w->clientLeader) ? w->clientLeader : w->id;
		    ret = ringInitiate (s, action, state, option, nOption);
		}
	    }
	    else
	    {
		rs->type = type;
		ret = ringInitiate (s, action, state, option, nOption);
	    }

	    if (state & CompActionStateInitKey)
		action->state |= CompActionStateTermKey;

	    if (state & CompActionStateInitEdge)
		action->state |= CompActionStateTermEdge;
	    else if (state & CompActionStateInitButton)
		action->state |= CompActionStateTermButton;
	}

	if (ret)
    	    switchToWindow (s, nextWindow);
    }

    return ret;
}

static Bool
ringNext (CompDisplay     *d,
  	  CompAction      *action,
	  CompActionState state,
	  CompOption      *option,
	  int	           nOption)
{
    return ringDoSwitch (d, action, state, option, nOption,
			 TRUE, RingTypeNormal); 
}

static Bool
ringPrev (CompDisplay     *d,
  	  CompAction      *action,
	  CompActionState state,
	  CompOption      *option,
	  int	           nOption)
{
    return ringDoSwitch (d, action, state, option, nOption,
			 FALSE, RingTypeNormal); 
}

static Bool
ringNextAll (CompDisplay     *d,
	     CompAction      *action,
   	     CompActionState state,
   	     CompOption      *option,
   	     int	     nOption)
{
    return ringDoSwitch (d, action, state, option, nOption,
			 TRUE, RingTypeAll); 
}

static Bool
ringPrevAll (CompDisplay     *d,
	     CompAction      *action,
   	     CompActionState state,
   	     CompOption      *option,
   	     int	     nOption)
{
    return ringDoSwitch (d, action, state, option, nOption,
			 FALSE, RingTypeAll); 
}

static Bool
ringNextGroup (CompDisplay     *d,
     	       CompAction      *action,
     	       CompActionState state,
     	       CompOption      *option,
     	       int	       nOption)
{
    return ringDoSwitch (d, action, state, option, nOption,
			 TRUE, RingTypeGroup); 
}

static Bool
ringPrevGroup (CompDisplay     *d,
     	       CompAction      *action,
     	       CompActionState state,
     	       CompOption      *option,
     	       int	       nOption)
{
    return ringDoSwitch (d, action, state, option, nOption,
			 FALSE, RingTypeGroup); 
}

static void
ringWindowSelectAt (CompScreen *s,
		    int        x,
		    int        y,
		    Bool       terminate)
{
    int    i;
    CompWindow *selected = NULL;

    RING_SCREEN (s);

    if (!ringGetSelectWithMouse (s))
	return;
 
    /* first find the top-most window the mouse 
       pointer is over */
    for (i = rs->nWindows - 1; i >= 0; i--)
    {
    	if (rs->drawSlots[i].slot && *(rs->drawSlots[i].slot))
	{
	    CompWindow *w = rs->drawSlots[i].w;

	    RING_WINDOW (w);

    	    if ((x >= (rw->tx + w->attrib.x)) &&
		(x <= (rw->tx + w->attrib.x + (w->attrib.width * rw->scale))) &&
		(y >= (rw->ty + w->attrib.y)) &&
		(y <= (rw->ty + w->attrib.y + (w->attrib.height * rw->scale))))
	    {
		/* we have found one, select it */
		selected = w;
		break;
	    }
	}
    }

    if (selected && terminate)
    {
	CompOption o;

	o.type = CompOptionTypeInt;
	o.name = "root";
	o.value.i = s->root;

	rs->selectedWindow = selected;

	ringTerminate (s->display, NULL, 0, &o, 1);
    }
    else if (!terminate && (selected != rs->selectedWindow || !rs->textData))
    {
	if (!selected)
	{
	    ringFreeWindowTitle (s);
	}
	else
	{
	    rs->selectedWindow = selected;
	    ringRenderWindowTitle (s);
	}
	damageScreen (s);
    }
}

static void 
ringWindowRemove (CompDisplay *d, 
		  CompWindow  *w)
{
    if (w)
    {
	Bool   inList = FALSE;
	int    j, i = 0;
	CompWindow *selected;

	RING_SCREEN (w->screen);

	if (rs->state == RingStateNone)
	    return;

	if (isRingWin (w))
    	    return;

	selected = rs->selectedWindow;

	while (i < rs->nWindows)
	{
	    if (w == rs->windows[i])
	    {
		inList = TRUE;

		if (w == selected)
		{
		    if (i < (rs->nWindows - 1))
			selected = rs->windows[i + 1];
		    else
			selected = rs->windows[0];

		    rs->selectedWindow = selected;
		    ringRenderWindowTitle (w->screen);
		}

		rs->nWindows--;
		for (j = i; j < rs->nWindows; j++)
		    rs->windows[j] = rs->windows[j + 1];
	    }
	    else
	    {
		i++;
	    }
	}

	if (!inList)
	    return;

	if (rs->nWindows == 0)
	{
	    CompOption o;

	    o.type = CompOptionTypeInt;
	    o.name = "root";
	    o.value.i = w->screen->root;

	    ringTerminate (d, NULL, 0, &o, 1);
	    return;
	}

	// Let the window list be updated to avoid crash
	// when a window is closed while ending (RingStateIn).
	if (!rs->grabIndex && rs->state != RingStateIn)
	    return;

	if (ringUpdateWindowList (w->screen))
	{
	    rs->moreAdjust = TRUE;
	    rs->state = RingStateOut;
	    damageScreen (w->screen);
	}
    }
}

static void
ringHandleEvent (CompDisplay *d,
		 XEvent      *event)
{
    CompScreen *s;
    CompWindow *w = NULL;

    switch (event->type) {
    case DestroyNotify:
	/* We need to get the CompWindow * for event->xdestroywindow.window
	   here because in the (*d->handleEvent) call below, that CompWindow's
	   id will become 1, so findWindowAtDisplay won't be able to find the
	   CompWindow after that. */
	w = findWindowAtDisplay (d, event->xdestroywindow.window);
	break;
    }

    RING_DISPLAY (d);

    UNWRAP (rd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (rd, d, handleEvent, ringHandleEvent);

    switch (event->type) {
    case PropertyNotify:
	if (event->xproperty.atom == XA_WM_NAME)
	{
	    CompWindow *w;
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
	    {
    		RING_SCREEN (w->screen);
    		if (rs->grabIndex && (w == rs->selectedWindow))
    		{
    		    ringRenderWindowTitle (w->screen);
    		    damageScreen (w->screen);
		}
	    }
	}
	break;
    case ButtonPress:
	if (event->xbutton.button == Button1)
	{
	    s = findScreenAtDisplay (d, event->xbutton.root);
	    if (s)
	    {
    		RING_SCREEN (s);

    		if (rs->grabIndex)
    		    ringWindowSelectAt (s, 
					event->xbutton.x_root, 
					event->xbutton.y_root,
					TRUE);
	    }
	}
	break;
    case MotionNotify:
	s = findScreenAtDisplay (d, event->xmotion.root);
	if (s)
	{
	    RING_SCREEN (s);

	    if (rs->grabIndex)
		ringWindowSelectAt (s,
				    event->xmotion.x_root,
				    event->xmotion.y_root,
				    FALSE);
	}
    case UnmapNotify:
	w = findWindowAtDisplay (d, event->xunmap.window);
	ringWindowRemove (d, w);
	break;
    case DestroyNotify:
	ringWindowRemove (d, w);
	break;
    }
}

static Bool
ringDamageWindowRect (CompWindow *w,
		      Bool	  initial,
		      BoxPtr     rect)
{
    Bool       status = FALSE;
    CompScreen *s = w->screen;

    RING_SCREEN (s);

    if (initial)
    {
	if (rs->grabIndex && isRingWin (w))
	{
	    ringAddWindowToList (s, w);
	    if (ringUpdateWindowList (s))
	    {
		RING_WINDOW (w);

		rw->adjust = TRUE;
		rs->moreAdjust = TRUE;
		rs->state = RingStateOut;
		damageScreen (s);
	    }
	}
    }
    else if (rs->state == RingStateSwitching)
    {
	RING_WINDOW (w);

	if (rw->slot)
	{
	    damageTransformedWindowRect (w,
					 rw->scale, rw->scale,
					 rw->tx, rw->ty,
					 rect);

	    status = TRUE;
	}
    }

    UNWRAP (rs, s, damageWindowRect);
    status |= (*s->damageWindowRect) (w, initial, rect);
    WRAP (rs, s, damageWindowRect, ringDamageWindowRect);

    return status;
}

static Bool
ringInitDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    RingDisplay *rd;
    int         index;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    rd = malloc (sizeof (RingDisplay));
    if (!rd)
	return FALSE;

    rd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (rd->screenPrivateIndex < 0)
    {
	free (rd);
	return FALSE;
    }

    if (checkPluginABI ("text", TEXT_ABIVERSION) &&
	getPluginDisplayIndex (d, "text", &index))
    {
	rd->textFunc = d->base.privates[index].ptr;
    }
    else
    {
	compLogMessage ("ring", CompLogLevelWarn,
			"No compatible text plugin found.");
	rd->textFunc = NULL;
    }

    ringSetNextKeyInitiate (d, ringNext);
    ringSetNextKeyTerminate (d, ringTerminate);
    ringSetPrevKeyInitiate (d, ringPrev);
    ringSetPrevKeyTerminate (d, ringTerminate);
    ringSetNextAllKeyInitiate (d, ringNextAll);
    ringSetNextAllKeyTerminate (d, ringTerminate);
    ringSetPrevAllKeyInitiate (d, ringPrevAll);
    ringSetPrevAllKeyTerminate (d, ringTerminate);
    ringSetNextGroupKeyInitiate (d, ringNextGroup);
    ringSetNextGroupKeyTerminate (d, ringTerminate);
    ringSetPrevGroupKeyInitiate (d, ringPrevGroup);
    ringSetPrevGroupKeyTerminate (d, ringTerminate);

    ringSetNextButtonInitiate (d, ringNext);
    ringSetNextButtonTerminate (d, ringTerminate);
    ringSetPrevButtonInitiate (d, ringPrev);
    ringSetPrevButtonTerminate (d, ringTerminate);
    ringSetNextAllButtonInitiate (d, ringNextAll);
    ringSetNextAllButtonTerminate (d, ringTerminate);
    ringSetPrevAllButtonInitiate (d, ringPrevAll);
    ringSetPrevAllButtonTerminate (d, ringTerminate);
    ringSetNextGroupButtonInitiate (d, ringNextGroup);
    ringSetNextGroupButtonTerminate (d, ringTerminate);
    ringSetPrevGroupButtonInitiate (d, ringPrevGroup);
    ringSetPrevGroupButtonTerminate (d, ringTerminate);

    WRAP (rd, d, handleEvent, ringHandleEvent);

    d->base.privates[displayPrivateIndex].ptr = rd;

    return TRUE;
}

static void
ringFiniDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    RING_DISPLAY (d);

    freeScreenPrivateIndex (d, rd->screenPrivateIndex);

    UNWRAP (rd, d, handleEvent);

    free (rd);
}

static Bool
ringInitScreen (CompPlugin *p,
		CompScreen *s)
{
    RingScreen *rs;

    RING_DISPLAY (s->display);

    rs = malloc (sizeof (RingScreen));
    if (!rs)
	return FALSE;

    rs->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (rs->windowPrivateIndex < 0)
    {
	free (rs);
	return FALSE;
    }

    rs->grabIndex = 0;

    rs->state = RingStateNone;

    rs->windows     = NULL;
    rs->drawSlots   = NULL;
    rs->windowsSize = 0;

    rs->paintingSwitcher = FALSE;

    rs->selectedWindow = NULL;

    rs->moreAdjust   = FALSE;
    rs->rotateAdjust = FALSE;

    rs->rotAdjust = 0;
    rs->rVelocity = 0;

    rs->textData = NULL;

    matchInit (&rs->match);

    WRAP (rs, s, preparePaintScreen, ringPreparePaintScreen);
    WRAP (rs, s, donePaintScreen, ringDonePaintScreen);
    WRAP (rs, s, paintOutput, ringPaintOutput);
    WRAP (rs, s, paintWindow, ringPaintWindow);
    WRAP (rs, s, damageWindowRect, ringDamageWindowRect);

    s->base.privates[rd->screenPrivateIndex].ptr = rs;

    return TRUE;
}

static void
ringFiniScreen (CompPlugin *p,
		CompScreen *s)
{
    RING_SCREEN (s);

    freeWindowPrivateIndex (s, rs->windowPrivateIndex);

    UNWRAP (rs, s, preparePaintScreen);
    UNWRAP (rs, s, donePaintScreen);
    UNWRAP (rs, s, paintOutput);
    UNWRAP (rs, s, paintWindow);
    UNWRAP (rs, s, damageWindowRect);

    matchFini (&rs->match);

    ringFreeWindowTitle (s);

    if (rs->windows)
	free (rs->windows);

    if (rs->drawSlots)
	free (rs->drawSlots);

    free (rs);
}

static Bool
ringInitWindow (CompPlugin *p,
		CompWindow *w)
{
    RingWindow *rw;

    RING_SCREEN (w->screen);

    rw = malloc (sizeof (RingWindow));
    if (!rw)
	return FALSE;

    rw->slot = 0;
    rw->scale = 1.0f;
    rw->tx = rw->ty = 0.0f;
    rw->adjust = FALSE;
    rw->xVelocity = rw->yVelocity = 0.0f;
    rw->scaleVelocity = 0.0f;

    w->base.privates[rs->windowPrivateIndex].ptr = rw;

    return TRUE;
}

static void
ringFiniWindow (CompPlugin *p,
		CompWindow *w)
{
    RING_WINDOW (w);

    if (rw->slot)
	free (rw->slot);

    free (rw);
}

static CompBool
ringInitObject (CompPlugin *p,
		CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) ringInitDisplay,
	(InitPluginObjectProc) ringInitScreen,
	(InitPluginObjectProc) ringInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
ringFiniObject (CompPlugin *p,
		CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) ringFiniDisplay,
	(FiniPluginObjectProc) ringFiniScreen,
	(FiniPluginObjectProc) ringFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
ringInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
ringFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable ringVTable = {
    "ring",
    0,
    ringInit,
    ringFini,
    ringInitObject,
    ringFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &ringVTable;
}
