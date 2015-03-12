/*
 *
 * Compiz thumbnail plugin
 *
 * thumbnail.c
 *
 * Copyright : (C) 2007 by Dennis Kasprzyk
 * E-mail    : onestone@beryl-project.org
 *
 * Based on thumbnail.c:
 * Copyright : (C) 2007 Stjepan Glavina
 * E-mail    : stjepang@gmail.com
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
#include <sys/types.h>
#include <unistd.h>

#include <compiz-core.h>
#include <compiz-text.h>
#include <compiz-mousepoll.h>

#include <X11/Xatom.h>

#include "thumbnail_tex.h"
#include "thumbnail_options.h"

#define GET_THUMB_DISPLAY(d)				       \
    ((ThumbDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define THUMB_DISPLAY(d)		       \
    ThumbDisplay *td = GET_THUMB_DISPLAY (d)

#define GET_THUMB_SCREEN(s, td)				   \
    ((ThumbScreen *) (s)->base.privates[(td)->screenPrivateIndex].ptr)

#define THUMB_SCREEN(s)						      \
    ThumbScreen *ts = GET_THUMB_SCREEN (s, GET_THUMB_DISPLAY (s->display))

#define WIN_X(w) ((w)->attrib.x - (w)->input.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->input.top)
#define WIN_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define WIN_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

#define TEXT_DISTANCE 10

static int displayPrivateIndex;

typedef struct _ThumbDisplay
{
    int screenPrivateIndex;

    HandleEventProc handleEvent;

    MousePollFunc *mpFunc;
    TextFunc      *textFunc;
} ThumbDisplay;

typedef struct _Thumbnail
{
    int   x;
    int   y;
    int   width;
    int   height;
    float scale;
    float opacity;
    int   offset;

    CompWindow *win;
    CompWindow *dock;

    CompTextData *textData;
} Thumbnail;

typedef struct _ThumbScreen
{
    CompTimeoutHandle displayTimeout;

    PreparePaintScreenProc     preparePaintScreen;
    PaintOutputProc            paintOutput;
    PaintWindowProc            paintWindow;
    DonePaintScreenProc        donePaintScreen;
    DamageWindowRectProc       damageWindowRect;
    WindowResizeNotifyProc     windowResizeNotify;
    PaintTransformedOutputProc paintTransformedOutput;

    CompWindow *dock;
    CompWindow *pointedWin;

    Bool      showingThumb;
    Thumbnail thumb;
    Thumbnail oldThumb;
    Bool      painted;

    CompTexture glowTexture;
    CompTexture windowTexture;

    int x;
    int y;

    PositionPollingHandle pollHandle;
} ThumbScreen;

static void
freeThumbText (CompScreen *s,
	       Thumbnail  *t)
{
    THUMB_DISPLAY (s->display);

    if (!t->textData)
	return;

    (td->textFunc->finiTextData) (s, t->textData);
    t->textData = NULL;
}

static void
renderThumbText (CompScreen *s,
		 Thumbnail  *t,
		 Bool       freeThumb)
{
    CompTextAttrib tA;

    THUMB_DISPLAY (s->display);

    if (freeThumb)
	freeThumbText (s, t);

    if (!td->textFunc)
	return;

    tA.maxWidth   = t->width;
    tA.maxHeight  = 100;

    tA.size       = thumbnailGetFontSize (s);
    tA.color[0]   = thumbnailGetFontColorRed (s);
    tA.color[1]   = thumbnailGetFontColorGreen (s);
    tA.color[2]   = thumbnailGetFontColorBlue (s);
    tA.color[3]   = thumbnailGetFontColorAlpha (s);
    tA.flags      = CompTextFlagEllipsized;
    if (thumbnailGetFontBold (s))
	tA.flags |= CompTextFlagStyleBold;
    tA.family     = "Sans";

    t->textData = (td->textFunc->renderWindowTitle) (s, t->win->id, FALSE, &tA);
}

static void
damageThumbRegion (CompScreen *s,
		   Thumbnail  *t)
{
    REGION region;

    region.extents.x1 = t->x - t->offset;
    region.extents.y1 = t->y - t->offset;
    region.extents.x2 = region.extents.x1 + t->width + (t->offset * 2);
    region.extents.y2 = region.extents.y1 + t->height + (t->offset * 2);

    if (t->textData)
	region.extents.y2 += t->textData->height + TEXT_DISTANCE;

    region.rects    = &region.extents;
    region.numRects = region.size = 1;

    damageScreenRegion (s, &region);
}

#define GET_DISTANCE(a,b) \
    (sqrt((((a)[0] - (b)[0]) * ((a)[0] - (b)[0])) + \
	  (((a)[1] - (b)[1]) * ((a)[1] - (b)[1]))))

static void
thumbUpdateThumbnail (CompScreen *s)
{
    int        igMidPoint[2], tMidPoint[2];
    int        tPos[2], tmpPos[2];
    float      distance = 1000000;
    int        off, oDev, tHeight;
    int        ox1, oy1, ox2, oy2, ow, oh;
    float      maxSize = thumbnailGetThumbSize (s);
    double     scale  = 1.0;
    CompWindow *w;

    THUMB_SCREEN (s);

    if (ts->thumb.win == ts->pointedWin)
	return;

    if (ts->thumb.opacity > 0.0 && ts->oldThumb.opacity > 0.0)
	return;

    if (ts->thumb.win)
	damageThumbRegion (s, &ts->thumb);

    freeThumbText (s, &ts->oldThumb);

    ts->oldThumb       = ts->thumb;
    ts->thumb.textData = NULL;
    ts->thumb.win      = ts->pointedWin;
    ts->thumb.dock     = ts->dock;

    if (!ts->thumb.win || !ts->dock)
    {
	ts->thumb.win  = NULL;
        ts->thumb.dock = NULL;
	return;
    }

    w = ts->thumb.win;

    /* do we nee to scale the window down? */
    if (WIN_W (w) > maxSize || WIN_H (w) > maxSize)
    {
	if (WIN_W (w) >= WIN_H (w))
	    scale = maxSize / WIN_W (w);
	else
	    scale = maxSize / WIN_H (w);
    }

    ts->thumb.width  = WIN_W (w)* scale;
    ts->thumb.height = WIN_H (w) * scale;
    ts->thumb.scale  = scale;

    if (thumbnailGetTitleEnabled (s))
	renderThumbText (s, &ts->thumb, FALSE);
    else
	freeThumbText (s, &ts->thumb);

    igMidPoint[0] = w->iconGeometry.x + (w->iconGeometry.width / 2);
    igMidPoint[1] = w->iconGeometry.y + (w->iconGeometry.height / 2);

    off = thumbnailGetBorder (s);
    oDev = outputDeviceForPoint (s,
				 w->iconGeometry.x +
				 (w->iconGeometry.width / 2),
				 w->iconGeometry.y +
				 (w->iconGeometry.height / 2));

    if (s->nOutputDev == 1 || oDev > s->nOutputDev)
    {
	ox1 = 0;
	oy1 = 0;
	ox2 = s->width;
	oy2 = s->height;
	ow  = s->width;
	oh  = s->height;
    }
    else
    {
	ox1 = s->outputDev[oDev].region.extents.x1;
	ox2 = s->outputDev[oDev].region.extents.x2;
	oy1 = s->outputDev[oDev].region.extents.y1;
	oy2 = s->outputDev[oDev].region.extents.y2;
	ow  = ox2 - ox1;
	oh  = oy2 - oy1;
    }

    tHeight = ts->thumb.height;
    if (ts->thumb.textData)
	tHeight += ts->thumb.textData->height + TEXT_DISTANCE;

    // failsave position
    tPos[0] = igMidPoint[0] - (ts->thumb.width / 2.0);

    if (w->iconGeometry.y - tHeight >= 0)
	tPos[1] = w->iconGeometry.y - tHeight;
    else
	tPos[1] = w->iconGeometry.y + w->iconGeometry.height;

    // above
    tmpPos[0] = igMidPoint[0] - (ts->thumb.width / 2.0);

    if (tmpPos[0] - off < ox1)
	tmpPos[0] = ox1 + off;

    if (tmpPos[0] + off + ts->thumb.width > ox2)
    {
	if (ts->thumb.width + (2 * off) <= ow)
	    tmpPos[0] = ox2 - ts->thumb.width - off;
	else
	    tmpPos[0] = ox1 + off;
    }

    tMidPoint[0] = tmpPos[0] + (ts->thumb.width / 2.0);

    tmpPos[1] = WIN_Y (ts->dock) - tHeight - off;
    tMidPoint[1] = tmpPos[1] + (tHeight / 2.0);

    if (tmpPos[1] > oy1)
    {
	tPos[0]  = tmpPos[0];
	tPos[1]  = tmpPos[1];
	distance = GET_DISTANCE (igMidPoint, tMidPoint);
    }

    // below
    tmpPos[1] = WIN_Y (ts->dock) + WIN_H (ts->dock) + off;

    tMidPoint[1] = tmpPos[1] + (tHeight / 2.0);

    if (tmpPos[1] + tHeight + off < oy2 &&
	GET_DISTANCE (igMidPoint, tMidPoint) < distance)
    {
	tPos[0]  = tmpPos[0];
	tPos[1]  = tmpPos[1];
	distance = GET_DISTANCE (igMidPoint, tMidPoint);
    }

    // left
    tmpPos[1] = igMidPoint[1] - (tHeight / 2.0);

    if (tmpPos[1] - off < oy1)
	tmpPos[1] = oy1 + off;

    if (tmpPos[1] + off + tHeight > oy2)
    {
	if (tHeight + (2 * off) <= oh)
	    tmpPos[1] = oy2 - ts->thumb.height - off;
	else
	    tmpPos[1] = oy1 + off;
    }

    tMidPoint[1] = tmpPos[1] + (tHeight / 2.0);

    tmpPos[0] = WIN_X (ts->dock) - ts->thumb.width - off;
    tMidPoint[0] = tmpPos[0] + (ts->thumb.width / 2.0);

    if (tmpPos[0] > ox1 && GET_DISTANCE (igMidPoint, tMidPoint) < distance)
    {
	tPos[0]  = tmpPos[0];
	tPos[1]  = tmpPos[1];
	distance = GET_DISTANCE (igMidPoint, tMidPoint);
    }

    // right
    tmpPos[0] = WIN_X (ts->dock) + WIN_W (ts->dock) + off;

    tMidPoint[0] = tmpPos[0] + (ts->thumb.width / 2.0);

    if (tmpPos[0] + ts->thumb.width + off < ox2 &&
	GET_DISTANCE (igMidPoint, tMidPoint) < distance)
    {
	tPos[0]  = tmpPos[0];
	tPos[1]  = tmpPos[1];
	distance = GET_DISTANCE (igMidPoint, tMidPoint);
    }

    ts->thumb.x       = tPos[0];
    ts->thumb.y       = tPos[1];
    ts->thumb.offset  = off;
    ts->thumb.opacity = 0.0;

    damageThumbRegion (s, &ts->thumb);
}

static Bool
thumbShowThumbnail (void *vs)
{
    CompScreen *s = (CompScreen *) vs;

    THUMB_SCREEN (s);

    ts->showingThumb   = TRUE;
    ts->displayTimeout = 0;

    thumbUpdateThumbnail (s);
    damageThumbRegion (s, &ts->thumb);

    return FALSE;
}

static Bool
checkPosition (CompWindow *w)
{
    if (thumbnailGetCurrentViewport (w->screen))
    {
	if (w->serverX >= w->screen->width    ||
	    w->serverX + w->serverWidth <= 0  ||
	    w->serverY >= w->screen->height   ||
	    w->serverY + w->serverHeight <= 0)
	{
	    return FALSE;
	}
    }

    return TRUE;
}

static void
positionUpdate (CompScreen *s,
		int        x,
		int        y)
{
    CompWindow *cw    = s->windows;
    CompWindow *found = NULL;

    THUMB_SCREEN (s);

    for (; cw && !found; cw = cw->next)
    {
	if (cw->destroyed)
	    continue;

	if (!cw->iconGeometrySet)
	    continue;

	if (cw->attrib.map_state != IsViewable)
	    continue;
	
	if (cw->state & CompWindowStateSkipTaskbarMask)
	    continue;

	if (cw->state & CompWindowStateSkipPagerMask)
	    continue;

	if (!cw->managed)
	    continue;

	if (!cw->texture->pixmap)
	    continue;

	if (x >= cw->iconGeometry.x                          &&
	    x < cw->iconGeometry.x + cw->iconGeometry.width  &&
	    y >= cw->iconGeometry.y                          &&
	    y < cw->iconGeometry.y + cw->iconGeometry.height &&
	    checkPosition (cw))
	{
	    found = cw;

	}
    }

    if (found)
    {
	if (!ts->showingThumb &&
	    !(ts->thumb.opacity != 0.0 && ts->thumb.win == found))
	{
	    if (ts->displayTimeout)

	    {
		if (ts->pointedWin != found)
		{
		    compRemoveTimeout (ts->displayTimeout);
		    ts->displayTimeout =
		        compAddTimeout (thumbnailGetShowDelay (s),
					thumbnailGetShowDelay (s) + 500,
					thumbShowThumbnail, s);
		}
	    }
	    else
	    {
		ts->displayTimeout =
		    compAddTimeout (thumbnailGetShowDelay (s),
				    thumbnailGetShowDelay (s) + 500,
				    thumbShowThumbnail, s);
	    }
        }

        ts->pointedWin = found;
        thumbUpdateThumbnail (s);
    }
    else
    {
	if (ts->displayTimeout)
	{
	    compRemoveTimeout (ts->displayTimeout);
	    ts->displayTimeout = 0;
	}

	ts->pointedWin   = NULL;
	ts->showingThumb = FALSE;
    }
}


static void
thumbWindowResizeNotify (CompWindow *w,
			 int        dx,
			 int        dy,
			 int        dwidth,
			 int        dheight)
{
    CompScreen *s = w->screen;

    THUMB_SCREEN (s);

    thumbUpdateThumbnail (s);

    UNWRAP (ts, s, windowResizeNotify);
    (*s->windowResizeNotify) (w, dx, dy, dwidth, dheight);
    WRAP (ts, s, windowResizeNotify, thumbWindowResizeNotify);
}

static void
thumbHandleEvent (CompDisplay * d,
		  XEvent * event)
{
    THUMB_DISPLAY (d);

    UNWRAP (td, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (td, d, handleEvent, thumbHandleEvent);

    CompWindow *w;

    switch (event->type)
    {
    case PropertyNotify:
	if (event->xproperty.atom == d->wmNameAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);

	    if (w)
	    {
		THUMB_SCREEN (w->screen);

		if (ts->thumb.win == w && thumbnailGetTitleEnabled (w->screen))
		    renderThumbText (w->screen, &ts->thumb, TRUE);
	    }
	}
	break;

    case ButtonPress:
	{
	    CompScreen *s = findScreenAtDisplay (d, event->xbutton.root);

	    if (!s)
		break;

	    THUMB_SCREEN (s);

	    if (ts->displayTimeout)
	    {
		compRemoveTimeout (ts->displayTimeout);
		ts->displayTimeout = 0;
	    }

	    ts->pointedWin   = 0;
	    ts->showingThumb = FALSE;
	}
	break;

    case EnterNotify:
	w = findWindowAtDisplay (d, event->xcrossing.window);
	if (w)
	{
	    CompScreen *s = w->screen;

	    THUMB_SCREEN (s);
	    THUMB_DISPLAY (w->screen->display);

	    if (w->wmType & CompWindowTypeDockMask)
	    {
		if (ts->dock != w)
		{
		    ts->dock = w;

		    if (ts->displayTimeout)
		    {
			compRemoveTimeout (ts->displayTimeout);
			ts->displayTimeout = 0;
		    }

		    ts->pointedWin   = NULL;
		    ts->showingThumb = FALSE;
		}

		if (!ts->pollHandle)
		{
		    ts->pollHandle =
			(*td->mpFunc->addPositionPolling) (s, positionUpdate);
		}
	    }
	    else
	    {
		ts->dock = NULL;

		if (ts->displayTimeout)
		{
		    compRemoveTimeout (ts->displayTimeout);
		    ts->displayTimeout = 0;
		}

		ts->pointedWin   = NULL;
		ts->showingThumb = FALSE;

		if (ts->pollHandle)
		{
		    (*td->mpFunc->removePositionPolling) (s, ts->pollHandle);
		    ts->pollHandle = 0;
		}
	    }
	}
	break;
    case LeaveNotify:
	w = findWindowAtDisplay (d, event->xcrossing.window);
	if (w)
	{
	    THUMB_SCREEN (w->screen);
	    THUMB_DISPLAY (w->screen->display);

	    if (w->wmType & CompWindowTypeDockMask)
	    {
		ts->dock = NULL;

		if (ts->displayTimeout)
		{
		    compRemoveTimeout (ts->displayTimeout);
		    ts->displayTimeout = 0;
		}

		ts->pointedWin   = NULL;
		ts->showingThumb = FALSE;

		if (ts->pollHandle)
		{
		    (*td->mpFunc->removePositionPolling) (w->screen,
		 					  ts->pollHandle);
		    ts->pollHandle = 0;
		}
	    }
	}
	break;

    default:
	break;
    }
}


static void
thumbPaintThumb (CompScreen          *s,
		 Thumbnail           *t,
		 const CompTransform *transform)
{
    AddWindowGeometryProc oldAddWindowGeometry;
    CompWindow            *w = t->win;
    int                   wx = t->x;
    int                   wy = t->y;
    float                 width  = t->width;
    float                 height = t->height;
    WindowPaintAttrib     sAttrib;
    unsigned int          mask = PAINT_WINDOW_TRANSFORMED_MASK |
	                         PAINT_WINDOW_TRANSLUCENT_MASK;

    THUMB_SCREEN (s);

    if (!w)
	return;

    sAttrib = w->paint;

    if (t->textData)
	height += t->textData->height + TEXT_DISTANCE;

    /* Wrap drawWindowGeometry to make sure the general
       drawWindowGeometry function is used */
    oldAddWindowGeometry = s->addWindowGeometry;
    s->addWindowGeometry = addWindowGeometry;

    if (w->texture->pixmap)
    {
	int            off = t->offset;
	GLenum         filter = s->display->textureFilter;
	FragmentAttrib fragment;
	CompTransform  wTransform = *transform;

	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	
	if (thumbnailGetWindowLike (s))
	{
	    glColor4f (1.0, 1.0, 1.0, t->opacity);
	    enableTexture (s, &ts->windowTexture, COMP_TEXTURE_FILTER_GOOD);
	}
	else
	{
	    glColor4us (thumbnailGetThumbColorRed (s),
			thumbnailGetThumbColorGreen (s),
			thumbnailGetThumbColorBlue (s),
			thumbnailGetThumbColorAlpha (s) * t->opacity);

	    enableTexture (s, &ts->glowTexture, COMP_TEXTURE_FILTER_GOOD);
	}

	glBegin (GL_QUADS);

	glTexCoord2f (1, 0);
	glVertex2f (wx, wy);
	glVertex2f (wx, wy + height);
	glVertex2f (wx + width, wy + height);
	glVertex2f (wx + width, wy);

	glTexCoord2f (0, 1);
	glVertex2f (wx - off, wy - off);
	glTexCoord2f (0, 0);
	glVertex2f (wx - off, wy);
	glTexCoord2f (1, 0);
	glVertex2f (wx, wy);
	glTexCoord2f (1, 1);
	glVertex2f (wx, wy - off);

	glTexCoord2f (1, 1);
	glVertex2f (wx + width, wy - off);
	glTexCoord2f (1, 0);
	glVertex2f (wx + width, wy);
	glTexCoord2f (0, 0);
	glVertex2f (wx + width + off, wy);
	glTexCoord2f (0, 1);
	glVertex2f (wx + width + off, wy - off);

	glTexCoord2f (0, 0);
	glVertex2f (wx - off, wy + height);
	glTexCoord2f (0, 1);
	glVertex2f (wx - off, wy + height + off);
	glTexCoord2f (1, 1);
	glVertex2f (wx, wy + height + off);
	glTexCoord2f (1, 0);
	glVertex2f (wx, wy + height);

	glTexCoord2f (1, 0);
	glVertex2f (wx + width, wy + height);
	glTexCoord2f (1, 1);
	glVertex2f (wx + width, wy + height + off);
	glTexCoord2f (0, 1);
	glVertex2f (wx + width + off, wy + height + off);
	glTexCoord2f (0, 0);
	glVertex2f (wx + width + off, wy + height);

	glTexCoord2f (1, 1);
	glVertex2f (wx, wy - off);
	glTexCoord2f (1, 0);
	glVertex2f (wx, wy);
	glTexCoord2f (1, 0);
	glVertex2f (wx + width, wy);
	glTexCoord2f (1, 1);
	glVertex2f (wx + width, wy - off);

	glTexCoord2f (1, 0);
	glVertex2f (wx, wy + height);
	glTexCoord2f (1, 1);
	glVertex2f (wx, wy + height + off);
	glTexCoord2f (1, 1);
	glVertex2f (wx + width, wy + height + off);
	glTexCoord2f (1, 0);
	glVertex2f (wx + width, wy + height);

	glTexCoord2f (0, 0);
	glVertex2f (wx - off, wy);
	glTexCoord2f (0, 0);
	glVertex2f (wx - off, wy + height);
	glTexCoord2f (1, 0);
	glVertex2f (wx, wy + height);
	glTexCoord2f (1, 0);
	glVertex2f (wx, wy);

	glTexCoord2f (1, 0);
	glVertex2f (wx + width, wy);
	glTexCoord2f (1, 0);
	glVertex2f (wx + width, wy + height);
	glTexCoord2f (0, 0);
	glVertex2f (wx + width + off, wy + height);
	glTexCoord2f (0, 0);
	glVertex2f (wx + width + off, wy);

	glEnd ();

	if (thumbnailGetWindowLike (s))
	{
	    disableTexture (s, &ts->windowTexture);
	}
	else
	{
	    disableTexture (s, &ts->glowTexture);
	}

	glColor4usv (defaultColor);

	glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	if (t->textData)
	{
	    float ox = 0.0;

	    THUMB_DISPLAY (s->display);

	    if (t->textData->width < width)
		ox = (width - (int)t->textData->width) / 2.0;

	    (td->textFunc->drawText) (s, t->textData, wx + ox,
				      wy + height, t->opacity);
	}

	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	glDisable (GL_BLEND);

	screenTexEnvMode (s, GL_REPLACE);

	glColor4usv (defaultColor);

	sAttrib.opacity *= t->opacity;
	sAttrib.yScale = t->scale;
	sAttrib.xScale = t->scale;

	sAttrib.xTranslate = wx - w->attrib.x + w->input.left * sAttrib.xScale;
	sAttrib.yTranslate = wy - w->attrib.y + w->input.top * sAttrib.yScale;

	if (thumbnailGetMipmap (s))
	    s->display->textureFilter = GL_LINEAR_MIPMAP_LINEAR;

	initFragmentAttrib (&fragment, &sAttrib);

	matrixTranslate (&wTransform, w->attrib.x, w->attrib.y, 0.0f);
	matrixScale (&wTransform, sAttrib.xScale, sAttrib.yScale, 1.0f);
	matrixTranslate (&wTransform,
			 sAttrib.xTranslate / sAttrib.xScale - w->attrib.x,
			 sAttrib.yTranslate / sAttrib.yScale - w->attrib.y,
			 0.0f);

	glPushMatrix ();
	glLoadMatrixf (wTransform.m);
	(*s->drawWindow) (w, &wTransform, &fragment, &infiniteRegion, mask);
	glPopMatrix ();

	s->display->textureFilter = filter;
    }

    s->addWindowGeometry = oldAddWindowGeometry;
}

static void
thumbPreparePaintScreen (CompScreen *s,
			 int        ms)
{
    float val = ms;

    THUMB_SCREEN (s);

    val /= 1000;
    val /= thumbnailGetFadeSpeed (s);

    if (s->maxGrab)
    {
	ts->dock = NULL;

	if (ts->displayTimeout)
	{
	    compRemoveTimeout (ts->displayTimeout);
	    ts->displayTimeout = 0;
	}

	ts->pointedWin   = 0;
	ts->showingThumb = FALSE;
    }

    if (ts->showingThumb && ts->thumb.win == ts->pointedWin)
    {
	ts->thumb.opacity = MIN (1.0, ts->thumb.opacity + val);
    }

    if (!ts->showingThumb || ts->thumb.win != ts->pointedWin)
    {
	ts->thumb.opacity = MAX (0.0, ts->thumb.opacity - val);
	if (ts->thumb.opacity == 0.0)
	    ts->thumb.win = NULL;
    }

    if (ts->oldThumb.opacity > 0.0f)
    {
	ts->oldThumb.opacity = MAX (0.0, ts->oldThumb.opacity - val);
	if (ts->oldThumb.opacity == 0.0)
	{
	    damageThumbRegion (s, &ts->oldThumb);
	    freeThumbText (s, &ts->oldThumb);
	    ts->oldThumb.win = NULL;
	}
    }

    UNWRAP (ts, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, ms);
    WRAP (ts, s, preparePaintScreen, thumbPreparePaintScreen);
}

static void
thumbDonePaintScreen (CompScreen *s)
{
    THUMB_SCREEN (s);

    if (ts->thumb.opacity > 0.0 && ts->thumb.opacity < 1.0)
	damageThumbRegion (s, &ts->thumb);

    if (ts->oldThumb.opacity > 0.0 && ts->oldThumb.opacity < 1.0)
	damageThumbRegion (s, &ts->oldThumb);

    UNWRAP (ts, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (ts, s, donePaintScreen, thumbDonePaintScreen);
}

static Bool
thumbPaintOutput (CompScreen              *s,
		  const ScreenPaintAttrib *sAttrib,
		  const CompTransform     *transform,
		  Region                  region,
		  CompOutput              *output,
		  unsigned int            mask)
{
    Bool         status;
    unsigned int newMask = mask;

    THUMB_SCREEN (s);

    ts->painted = FALSE;

    ts->x = s->x;
    ts->y = s->y;

    if ((ts->oldThumb.opacity > 0.0 && ts->oldThumb.win) ||
       	(ts->thumb.opacity > 0.0 && ts->thumb.win))
    {
	newMask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;
    }

    UNWRAP (ts, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region,
	 			output, newMask);
    WRAP (ts, s, paintOutput, thumbPaintOutput);

    if (thumbnailGetAlwaysOnTop (s) && !ts->painted)
    {
	if (ts->oldThumb.opacity > 0.0 && ts->oldThumb.win)
	{
	    CompTransform sTransform = *transform;

	    transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA,
				    &sTransform);
	    glPushMatrix ();
	    glLoadMatrixf (sTransform.m);
	    thumbPaintThumb (s, &ts->oldThumb, &sTransform);
	    glPopMatrix ();
	}

	if (ts->thumb.opacity > 0.0 && ts->thumb.win)
	{
	    CompTransform sTransform = *transform;

	    transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA,
				    &sTransform);
	    glPushMatrix ();
	    glLoadMatrixf (sTransform.m);
	    thumbPaintThumb (s, &ts->thumb, &sTransform);
	    glPopMatrix ();
	}
    }

    return status;
}

static void
thumbPaintTransformedOutput (CompScreen              *s,
			     const ScreenPaintAttrib *sAttrib,
			     const CompTransform     *transform,
			     Region                  region,
			     CompOutput              *output,
			     unsigned int            mask)
{

    THUMB_SCREEN (s);

    UNWRAP (ts, s, paintTransformedOutput);
    (*s->paintTransformedOutput) (s, sAttrib, transform, region, output, mask);
    WRAP (ts, s, paintTransformedOutput, thumbPaintTransformedOutput);

    if (thumbnailGetAlwaysOnTop (s) && ts->x == s->x && ts->y == s->y)
    {
	ts->painted = TRUE;

	if (ts->oldThumb.opacity > 0.0 && ts->oldThumb.win)
	{
	    CompTransform sTransform = *transform;

	    (*s->applyScreenTransform) (s, sAttrib, output, &sTransform);
	    transformToScreenSpace (s, output, -sAttrib->zTranslate,
				    &sTransform);
	    glPushMatrix ();
	    glLoadMatrixf (sTransform.m);
	    thumbPaintThumb (s, &ts->oldThumb, &sTransform);
	    glPopMatrix ();
	}

	if (ts->thumb.opacity > 0.0 && ts->thumb.win)
	{
	    CompTransform sTransform = *transform;

	    (*s->applyScreenTransform) (s, sAttrib, output, &sTransform);
	    transformToScreenSpace (s, output, -sAttrib->zTranslate,
				    &sTransform);
	    glPushMatrix ();
	    glLoadMatrixf (sTransform.m);
	    thumbPaintThumb (s, &ts->thumb, &sTransform);
	    glPopMatrix ();
	}
    }
}

static Bool
thumbPaintWindow (CompWindow              *w,
		  const WindowPaintAttrib *attrib,
		  const CompTransform     *transform,
		  Region                  region,
		  unsigned int            mask)
{
    CompScreen *s = w->screen;
    Bool status;

    THUMB_SCREEN (s);

    UNWRAP (ts, s, paintWindow);
    status = (*s->paintWindow) (w, attrib, transform, region, mask);
    WRAP (ts, s, paintWindow, thumbPaintWindow);

    if (!thumbnailGetAlwaysOnTop (s) && ts->x == s->x && ts->y == s->y)
    {
	if (ts->oldThumb.opacity > 0.0 && ts->oldThumb.win &&
	    ts->oldThumb.dock == w)
	{
	    thumbPaintThumb (s, &ts->oldThumb, transform);
	}

	if (ts->thumb.opacity > 0.0 && ts->thumb.win && ts->thumb.dock == w)
	{
	    thumbPaintThumb (s, &ts->thumb, transform);
	}
    }

    return status;
}

static Bool
thumbDamageWindowRect (CompWindow *w,
		       Bool       initial,
		       BoxPtr     rect)
{
    Bool status;

    THUMB_SCREEN (w->screen);

    if (ts->thumb.win == w && ts->thumb.opacity > 0.0)
	damageThumbRegion (w->screen, &ts->thumb);

    if (ts->oldThumb.win == w && ts->oldThumb.opacity > 0.0)
	damageThumbRegion (w->screen, &ts->oldThumb);

    UNWRAP (ts, w->screen, damageWindowRect);
    status = (*w->screen->damageWindowRect) (w, initial, rect);
    WRAP (ts, w->screen, damageWindowRect, thumbDamageWindowRect);

    return status;
}

static Bool
thumbInitDisplay (CompPlugin  *p,
		  CompDisplay *d)
{
    ThumbDisplay *td;
    int          index;

    if (!checkPluginABI ("core", CORE_ABIVERSION) ||
	!checkPluginABI ("mousepoll", MOUSEPOLL_ABIVERSION))
	return FALSE;

    if (!getPluginDisplayIndex (d, "mousepoll", &index))
	return FALSE;

    td = malloc (sizeof (ThumbDisplay));
    if (!td)
	return FALSE;

    td->screenPrivateIndex = allocateScreenPrivateIndex (d);

    if (td->screenPrivateIndex < 0)
    {
	free (td);
	return FALSE;
    }

    td->mpFunc = d->base.privates[index].ptr;

    if (checkPluginABI ("text", TEXT_ABIVERSION) &&
	getPluginDisplayIndex (d, "text", &index))
    {
	td->textFunc = d->base.privates[index].ptr;
    }
    else
    {
	compLogMessage ("thumbnail", CompLogLevelWarn,
			"No compatible text plugin found.");
	td->textFunc = NULL;
    }

    WRAP (td, d, handleEvent, thumbHandleEvent);

    d->base.privates[displayPrivateIndex].ptr = td;

    return TRUE;
}


static void
thumbFiniDisplay (CompPlugin  *p,
		  CompDisplay *d)
{
    THUMB_DISPLAY (d);

    freeScreenPrivateIndex (d, td->screenPrivateIndex);

    UNWRAP (td, d, handleEvent);

    free (td);
}

static void
thumbFiniWindow (CompPlugin *p,
		 CompWindow *w)
{
    THUMB_SCREEN (w->screen);

    if (ts->thumb.win == w)
    {
	damageThumbRegion (w->screen, &ts->thumb);
	ts->thumb.win = NULL;
	ts->thumb.opacity = 0;
    }

    if (ts->oldThumb.win == w)
    {
	damageThumbRegion (w->screen, &ts->oldThumb);
	ts->oldThumb.win = NULL;
	ts->oldThumb.opacity = 0;
    }

    if (ts->pointedWin == w)
	ts->pointedWin = NULL;
}

static Bool
thumbInitScreen (CompPlugin *p,
		 CompScreen *s)
{
    ThumbScreen *ts;

    THUMB_DISPLAY (s->display);

    ts = calloc (1, sizeof (ThumbScreen));
    if (!ts)
	return FALSE;

    WRAP (ts, s, paintOutput, thumbPaintOutput);
    WRAP (ts, s, damageWindowRect, thumbDamageWindowRect);
    WRAP (ts, s, preparePaintScreen, thumbPreparePaintScreen);
    WRAP (ts, s, donePaintScreen, thumbDonePaintScreen);
    WRAP (ts, s, paintWindow, thumbPaintWindow);
    WRAP (ts, s, windowResizeNotify, thumbWindowResizeNotify);
    WRAP (ts, s, paintTransformedOutput, thumbPaintTransformedOutput);

    ts->dock           = NULL;
    ts->pointedWin     = NULL;
    ts->displayTimeout = 0;
    ts->thumb.win      = NULL;
    ts->oldThumb.win   = NULL;
    ts->showingThumb   = FALSE;
    ts->pollHandle     = 0;

    s->base.privates[td->screenPrivateIndex].ptr = ts;

    initTexture (s, &ts->glowTexture);
    initTexture (s, &ts->windowTexture);

    imageDataToTexture (s, &ts->glowTexture, glowTex, 32, 32,
			GL_RGBA, GL_UNSIGNED_BYTE);
    imageDataToTexture (s, &ts->windowTexture, windowTex, 32, 32,
			GL_RGBA, GL_UNSIGNED_BYTE);

    ts->thumb.textData    = NULL;
    ts->oldThumb.textData = NULL;

    return TRUE;
}


static void
thumbFiniScreen (CompPlugin *p,
		 CompScreen *s)
{
    THUMB_SCREEN (s);

    UNWRAP (ts, s, paintOutput);
    UNWRAP (ts, s, damageWindowRect);
    UNWRAP (ts, s, preparePaintScreen);
    UNWRAP (ts, s, donePaintScreen);
    UNWRAP (ts, s, paintWindow);
    UNWRAP (ts, s, windowResizeNotify);
    UNWRAP (ts, s, paintTransformedOutput);

    if (ts->displayTimeout)
	compRemoveTimeout (ts->displayTimeout);

    if (ts->pollHandle)
    {
	THUMB_DISPLAY (s->display);

	(*td->mpFunc->removePositionPolling) (s, ts->pollHandle);
	ts->pollHandle = 0;
    }

    freeThumbText (s, &ts->thumb);
    freeThumbText (s, &ts->oldThumb);

    finiTexture (s, &ts->glowTexture);
    finiTexture (s, &ts->windowTexture);

    free (ts);
}


static Bool
thumbInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}


static void
thumbFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

static CompBool
thumbInitObject (CompPlugin *p,
		 CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) thumbInitDisplay,
	(InitPluginObjectProc) thumbInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
thumbFiniObject (CompPlugin *p,
		 CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) thumbFiniDisplay,
	(FiniPluginObjectProc) thumbFiniScreen,
	(FiniPluginObjectProc) thumbFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

CompPluginVTable thumbVTable = {
    "thumbnail",
    0,
    thumbInit,
    thumbFini,
    thumbInitObject,
    thumbFiniObject,
    0,
    0
};


CompPluginVTable *
getCompPluginInfo (void)
{
    return &thumbVTable;
}
