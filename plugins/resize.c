/*
 * Copyright © 2005 Novell, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Novell, Inc. not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Novell, Inc. makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * NOVELL, INC. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NOVELL, INC. BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: David Reveman <davidr@novell.com>
 *         Michail Bitzes <noodlylight@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xatom.h>
#include <X11/cursorfont.h>

#include <fusilli-core.h>

static int bananaIndex;

static CompKeyBinding initiate_key;
static CompButtonBinding initiate_button;

#define ResizeUpMask    (1L << 0)
#define ResizeDownMask  (1L << 1)
#define ResizeLeftMask  (1L << 2)
#define ResizeRightMask (1L << 3)

#define RESIZE_MODE_NORMAL    0
#define RESIZE_MODE_OUTLINE   1
#define RESIZE_MODE_RECTANGLE 2
#define RESIZE_MODE_STRETCH   3
#define RESIZE_MODE_LAST      RESIZE_MODE_STRETCH

struct _ResizeKeys {
	char     *name;
	int      dx;
	int      dy;
	unsigned int warpMask;
	unsigned int resizeMask;
} rKeys[] = {
	{ "Left",  -1,  0, ResizeLeftMask | ResizeRightMask, ResizeLeftMask },
	{ "Right",  1,  0, ResizeLeftMask | ResizeRightMask, ResizeRightMask },
	{ "Up",     0, -1, ResizeUpMask | ResizeDownMask,    ResizeUpMask },
	{ "Down",   0,  1, ResizeUpMask | ResizeDownMask,    ResizeDownMask }
};

#define NUM_KEYS (sizeof (rKeys) / sizeof (rKeys[0]))

#define MIN_KEY_WIDTH_INC  24
#define MIN_KEY_HEIGHT_INC 24

static int displayPrivateIndex;

typedef struct _ResizeDisplay {
	int             screenPrivateIndex;
	HandleEventProc handleEvent;

	Atom resizeNotifyAtom;
	Atom resizeInformationAtom;

	CompWindow  *w;
	int         mode;
	XRectangle  savedGeometry;
	XRectangle  geometry;

	int          releaseButton;
	unsigned int mask;
	int          pointerDx;
	int          pointerDy;
	KeyCode      key[NUM_KEYS];

	Region       constraintRegion;
	int          inRegionStatus;
	int          lastGoodHotSpotY;
	int          lastGoodWidth;
	int          lastGoodHeight;
} ResizeDisplay;

typedef struct _ResizeScreen {
	int grabIndex;

	WindowResizeNotifyProc windowResizeNotify;
	PaintOutputProc        paintOutput;
	PaintWindowProc        paintWindow;
	DamageWindowRectProc   damageWindowRect;

	Cursor leftCursor;
	Cursor rightCursor;
	Cursor upCursor;
	Cursor upLeftCursor;
	Cursor upRightCursor;
	Cursor downCursor;
	Cursor downLeftCursor;
	Cursor downRightCursor;
	Cursor middleCursor;
	Cursor cursor[NUM_KEYS];
} ResizeScreen;

#define GET_RESIZE_DISPLAY(d) \
        ((ResizeDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define RESIZE_DISPLAY(d) \
        ResizeDisplay *rd = GET_RESIZE_DISPLAY (d)

#define GET_RESIZE_SCREEN(s, rd) \
        ((ResizeScreen *) (s)->base.privates[(rd)->screenPrivateIndex].ptr)

#define RESIZE_SCREEN(s) \
        ResizeScreen *rs = GET_RESIZE_SCREEN (s, GET_RESIZE_DISPLAY (&display))

static void
resizeGetPaintRectangle (CompDisplay *d,
                         BoxPtr      pBox)
{
	RESIZE_DISPLAY (d);

	pBox->x1 = rd->geometry.x - rd->w->input.left;
	pBox->y1 = rd->geometry.y - rd->w->input.top;
	pBox->x2 = rd->geometry.x +
	    rd->geometry.width + rd->w->serverBorderWidth * 2 +
	    rd->w->input.right;

	if (rd->w->shaded)
	{
		pBox->y2 = rd->geometry.y + rd->w->height + rd->w->input.bottom;
	}
	else
	{
		pBox->y2 = rd->geometry.y +
		    rd->geometry.height + rd->w->serverBorderWidth * 2 +
		    rd->w->input.bottom;
	}
}

static void
resizeGetStretchScale (CompWindow *w,
                       BoxPtr     pBox,
                       float      *xScale,
                       float      *yScale)
{
	int width, height;

	width  = w->width  + w->input.left + w->input.right;
	height = w->height + w->input.top  + w->input.bottom;

	*xScale = (width)  ? (pBox->x2 - pBox->x1) / (float) width  : 1.0f;
	*yScale = (height) ? (pBox->y2 - pBox->y1) / (float) height : 1.0f;
}

static void
resizeGetStretchRectangle (CompDisplay *d,
                           BoxPtr      pBox)
{
	BoxRec box;
	float  xScale, yScale;

	RESIZE_DISPLAY (d);

	resizeGetPaintRectangle (d, &box);
	resizeGetStretchScale (rd->w, &box, &xScale, &yScale);

	pBox->x1 = box.x1 - (rd->w->output.left - rd->w->input.left) * xScale;
	pBox->y1 = box.y1 - (rd->w->output.top - rd->w->input.top) * yScale;
	pBox->x2 = box.x2 + rd->w->output.right * xScale;
	pBox->y2 = box.y2 + rd->w->output.bottom * yScale;
}

static void
resizeDamageRectangle (CompScreen *s,
                       BoxPtr     pBox)
{
	REGION reg;

	reg.rects    = &reg.extents;
	reg.numRects = 1;

	reg.extents = *pBox;

	reg.extents.x1 -= 1;
	reg.extents.y1 -= 1;
	reg.extents.x2 += 1;
	reg.extents.y2 += 1;

	damageScreenRegion (s, &reg);
}

static Cursor
resizeCursorFromResizeMask (CompScreen   *s,
                            unsigned int mask)
{
	Cursor cursor;

	RESIZE_SCREEN (s);

	if (mask & ResizeLeftMask)
	{
		if (mask & ResizeDownMask)
			cursor = rs->downLeftCursor;
		else if (mask & ResizeUpMask)
			cursor = rs->upLeftCursor;
		else
			cursor = rs->leftCursor;
	}
	else if (mask & ResizeRightMask)
	{
		if (mask & ResizeDownMask)
			cursor = rs->downRightCursor;
		else if (mask & ResizeUpMask)
			cursor = rs->upRightCursor;
		else
			cursor = rs->rightCursor;
	}
	else if (mask & ResizeUpMask)
	{
		cursor = rs->upCursor;
	}
	else
	{
		cursor = rs->downCursor;
	}

	return cursor;
}

static void
resizeSendResizeNotify (CompDisplay *d)
{
	XEvent xev;

	RESIZE_DISPLAY (d);
	xev.xclient.type    = ClientMessage;
	xev.xclient.display = d->display;
	xev.xclient.format  = 32;

	xev.xclient.message_type = rd->resizeNotifyAtom;
	xev.xclient.window       = rd->w->id;

	xev.xclient.data.l[0] = rd->geometry.x;
	xev.xclient.data.l[1] = rd->geometry.y;
	xev.xclient.data.l[2] = rd->geometry.width;
	xev.xclient.data.l[3] = rd->geometry.height;
	xev.xclient.data.l[4] = 0;

	XSendEvent (d->display,
	            rd->w->screen->root,
	            FALSE,
	            SubstructureRedirectMask | SubstructureNotifyMask,
	            &xev);
}

static void
resizeUpdateWindowProperty (CompDisplay *d)
{
	unsigned long data[4];

	RESIZE_DISPLAY (d);

	data[0] = rd->geometry.x;
	data[1] = rd->geometry.y;
	data[2] = rd->geometry.width;
	data[3] = rd->geometry.height;

	XChangeProperty (d->display, rd->w->id,
	                 rd->resizeInformationAtom,
	                 XA_CARDINAL, 32, PropModeReplace,
	                 (unsigned char*) data, 4);
}

static void
resizeFinishResizing (CompDisplay *d)
{
	RESIZE_DISPLAY (d);

	(*rd->w->screen->windowUngrabNotify) (rd->w);

	XDeleteProperty (d->display,
	                 rd->w->id,
	                 rd->resizeInformationAtom);

	rd->w = NULL;
}

static Region
resizeGetConstraintRegion (CompScreen *s)
{
	Region       region;
	int          i;

	region = XCreateRegion ();
	if (!region)
		return NULL;

	for (i = 0; i < s->nOutputDev; i++)
		XUnionRectWithRegion (&s->outputDev[i].workArea, region, region);

	return region;
}

static Bool
resizeInitiate (BananaArgument   *arg,
                int              nArg)
{
	CompWindow *w;
	Window     xid;

	RESIZE_DISPLAY (&display);

	BananaValue *window = getArgNamed ("window", arg, nArg);

	if (window != NULL)
		xid = window->i;
	else
		xid = 0;

	w = findWindowAtDisplay (xid);
	if (w && (w->actions & CompWindowActionResizeMask))
	{
		unsigned int mask;
		int          x, y;
		int          button;
		int          mods;

		RESIZE_SCREEN (w->screen);

		BananaValue *modifiers = getArgNamed ("modifiers", arg, nArg);
		if (modifiers != NULL)
			mods = modifiers->i;
		else
			mods = 0;

		BananaValue *arg_x = getArgNamed ("x", arg, nArg);

		if (arg_x != NULL)
			x = arg_x->i;
		else
			x = pointerX;

		BananaValue *arg_y = getArgNamed ("y", arg, nArg);

		if (arg_y != NULL)
			y = arg_y->i;
		else
			y = pointerY;

		BananaValue *arg_button = getArgNamed ("button", arg, nArg);

		if (arg_button != NULL)
			button = arg_button->i;
		else
			button = -1;

		BananaValue *arg_mask = getArgNamed ("direction", arg, nArg);

		if (arg_mask != NULL)
			mask = arg_mask->i;
		else
			mask = 0;

		/* Initiate the resize in the direction suggested by the
		 * sector of the window the mouse is in, eg drag in top left
		 * will resize up and to the left.  Keyboard resize starts out
		 * with the cursor in the middle of the window and then starts
		 * resizing the edge corresponding to the next key press. */
		BananaValue *initiated_by_key = getArgNamed ("initiated_by_key",
		                                                 arg, nArg);

		if (initiated_by_key != NULL && initiated_by_key->b)
		{
			mask = 0;
		}
		else if (!mask)
		{
			int sectorSizeX = w->serverWidth / 3;
			int sectorSizeY = w->serverHeight / 3;
			int posX        = x - w->serverX;
			int posY        = y - w->serverY;

			if (posX < sectorSizeX)
				mask |= ResizeLeftMask;
			else if (posX > (2 * sectorSizeX))
				mask |= ResizeRightMask;

			if (posY < sectorSizeY)
				mask |= ResizeUpMask;
			else if (posY > (2 * sectorSizeY))
				mask |= ResizeDownMask;

			/* if the pointer was in the middle of the window,
			   just prevent input to the window */
			if (!mask)
				return TRUE;
		}

		if (otherScreenGrabExist (w->screen, "resize", NULL))
			return FALSE;

		if (rd->w)
			return FALSE;

		if (w->type & (CompWindowTypeDesktopMask |
		               CompWindowTypeDockMask    |
		               CompWindowTypeFullscreenMask))
			return FALSE;

		if (w->attrib.override_redirect)
			return FALSE;

		if (w->shaded)
			mask &= ~(ResizeUpMask | ResizeDownMask);

		rd->w    = w;
		rd->mask = mask;

		rd->savedGeometry.x      = w->serverX;
		rd->savedGeometry.y      = w->serverY;
		rd->savedGeometry.width  = w->serverWidth;
		rd->savedGeometry.height = w->serverHeight;

		rd->geometry = rd->savedGeometry;

		rd->pointerDx = x - pointerX;
		rd->pointerDy = y - pointerY;

		if ((w->state & MAXIMIZE_STATE) == MAXIMIZE_STATE)
		{
			/* if the window is fully maximized, showing the outline or
			   rectangle would be visually distracting as the window can't
			   be resized anyway; so we better don't use them in this case */
			rd->mode = RESIZE_MODE_NORMAL;
		}
		else
		{
			const BananaValue *
			option_mode = bananaGetOption (bananaIndex, "mode", -1);
			rd->mode = option_mode->i;
		}

		if (!rs->grabIndex)
		{
			Cursor cursor;

			if (initiated_by_key != NULL && initiated_by_key->b)
			{
				cursor = rs->middleCursor;
			}
			else
			{
				cursor = resizeCursorFromResizeMask (w->screen, mask);
			}

			rs->grabIndex = pushScreenGrab (w->screen, cursor, "resize");
		}

		if (rs->grabIndex)
		{
			unsigned int grabMask = CompWindowGrabResizeMask |
			                    CompWindowGrabButtonMask;

			BananaValue *option_external = getArgNamed ("external", arg, nArg);

			Bool sourceExternalApp;
			if (option_external != NULL)
				sourceExternalApp = option_external->b;
			else
				sourceExternalApp = FALSE;

			if (sourceExternalApp)
				grabMask |= CompWindowGrabExternalAppMask;

			BoxRec box;

			rd->releaseButton = button;

			(w->screen->windowGrabNotify) (w, x, y, mods, grabMask);

			const BananaValue *
			option_raise_on_click = bananaGetOption (
			    coreBananaIndex, "raise_on_click", -1);

			if (option_raise_on_click->b)
			    updateWindowAttributes (w,
			                    CompStackingUpdateModeAboveFullscreen);

			/* using the paint rectangle is enough here
			   as we don't have any stretch yet */
			resizeGetPaintRectangle (&display, &box);
			resizeDamageRectangle (w->screen, &box);

			if (initiated_by_key != NULL && initiated_by_key->b)
			{
				int xRoot, yRoot;

				xRoot = w->serverX + (w->serverWidth  / 2);
				yRoot = w->serverY + (w->serverHeight / 2);

				warpPointer (w->screen, xRoot - pointerX, yRoot - pointerY);
			}

			if (rd->constraintRegion)
				XDestroyRegion (rd->constraintRegion);

			if (sourceExternalApp)
			{
				/* Prevent resizing beyond work area edges when resize is
				   initiated externally (e.g. with window frame or menu)
				   and not with a key (e.g. alt+button) */

				rd->inRegionStatus   = RectangleOut;
				rd->lastGoodHotSpotY = -1;
				rd->lastGoodWidth    = w->serverWidth;
				rd->lastGoodHeight   = w->serverHeight;
				rd->constraintRegion = resizeGetConstraintRegion (w->screen);
			}
			else
			{
				rd->constraintRegion = NULL;
			}
		}
	}

	return FALSE;
}

static Bool
resizeTerminate (BananaArgument   *arg,
                 int              nArg)
{
	RESIZE_DISPLAY (&display);

	if (rd->w)
	{
		CompWindow     *w = rd->w;
		XWindowChanges xwc;
		unsigned int   mask = 0;

		RESIZE_SCREEN (w->screen);

		if (rd->mode == RESIZE_MODE_NORMAL)
		{
			BananaValue *cancel = getArgNamed ("cancel", arg, nArg);

			if (cancel != NULL && cancel->b)
			{
				xwc.x      = rd->savedGeometry.x;
				xwc.y      = rd->savedGeometry.y;
				xwc.width  = rd->savedGeometry.width;
				xwc.height = rd->savedGeometry.height;

				mask = CWX | CWY | CWWidth | CWHeight;
			}
		}
		else
		{
			XRectangle geometry;

			BananaValue *cancel = getArgNamed ("cancel", arg, nArg);

			if (cancel != NULL && cancel->b)
				geometry = rd->savedGeometry;
			else
				geometry = rd->geometry;

			if (memcmp (&geometry, &rd->savedGeometry, sizeof (geometry)) == 0)
			{
				BoxRec box;

				if (rd->mode == RESIZE_MODE_STRETCH)
					resizeGetStretchRectangle (&display, &box);
				else
					resizeGetPaintRectangle (&display, &box);

				resizeDamageRectangle (w->screen, &box);
			}
			else
			{
				xwc.x      = geometry.x;
				xwc.y      = geometry.y;
				xwc.width  = geometry.width;
				xwc.height = geometry.height;

				mask = CWX | CWY | CWWidth | CWHeight;
			}
		}

		if ((mask & CWWidth) && xwc.width == w->serverWidth)
			mask &= ~CWWidth;

		if ((mask & CWHeight) && xwc.height == w->serverHeight)
			mask &= ~CWHeight;

		if (mask)
		{
			if (mask & (CWWidth | CWHeight))
				sendSyncRequest (w);

			configureXWindow (w, mask, &xwc);
		}

		if (!(mask & (CWWidth | CWHeight)))
			resizeFinishResizing (&display);

		if (rs->grabIndex)
		{
			removeScreenGrab (w->screen, rs->grabIndex, NULL);
			rs->grabIndex = 0;
		}

		rd->releaseButton = 0;
	}

	return FALSE;
}

static void
resizeUpdateWindowSize (CompDisplay *d)
{
	RESIZE_DISPLAY (d);

	if (rd->w->syncWait)
		return;

	if (rd->w->serverWidth  != rd->geometry.width ||
		rd->w->serverHeight != rd->geometry.height)
	{
		XWindowChanges xwc;

		xwc.x      = rd->geometry.x;
		xwc.y      = rd->geometry.y;
		xwc.width  = rd->geometry.width;
		xwc.height = rd->geometry.height;

		sendSyncRequest (rd->w);

		configureXWindow (rd->w,
		                  CWX | CWY | CWWidth | CWHeight,
		                  &xwc);
	}
}

static void
resizeHandleKeyEvent (CompScreen *s,
                      KeyCode    keycode)
{
	RESIZE_SCREEN (s);
	RESIZE_DISPLAY (&display);

	if (rs->grabIndex && rd->w)
	{
		CompWindow *w = rd->w;
		int        widthInc, heightInc, i;

		widthInc  = w->sizeHints.width_inc;
		heightInc = w->sizeHints.height_inc;

		if (widthInc < MIN_KEY_WIDTH_INC)
			widthInc = MIN_KEY_WIDTH_INC;

		if (heightInc < MIN_KEY_HEIGHT_INC)
			heightInc = MIN_KEY_HEIGHT_INC;

		for (i = 0; i < NUM_KEYS; i++)
		{
			if (keycode != rd->key[i])
				continue;

			if (rd->mask & rKeys[i].warpMask)
			{
				XWarpPointer (display.display, None, None, 0, 0, 0, 0,
				              rKeys[i].dx * widthInc,
				              rKeys[i].dy * heightInc);
			}
			else
			{
				int x, y, left, top, width, height;

				left   = w->serverX - w->input.left;
				top    = w->serverY - w->input.top;
				width  = w->input.left + w->serverWidth  + w->input.right;
				height = w->input.top  + w->serverHeight + w->input.bottom;

				x = left + width  * (rKeys[i].dx + 1) / 2;
				y = top  + height * (rKeys[i].dy + 1) / 2;

				warpPointer (s, x - pointerX, y - pointerY);

				rd->mask = rKeys[i].resizeMask;

				updateScreenGrab (s, rs->grabIndex, rs->cursor[i]);
			}
			break;
		}
	}
}

static void
resizeHandleMotionEvent (CompScreen *s,
                         int        xRoot,
                         int        yRoot)
{
	RESIZE_SCREEN (s);

	if (rs->grabIndex)
	{
		BoxRec box;
		int    w, h;                    /* size of window contents */
		int    wX, wY, wWidth, wHeight; /* rect. for window contents+borders */
		int    i;
		int    workAreaSnapDistance = 15;

		RESIZE_DISPLAY (&display);

		w = rd->savedGeometry.width;
		h = rd->savedGeometry.height;

		if (!rd->mask)
		{
			CompWindow *w = rd->w;
			int        xDist, yDist;
			int        minPointerOffsetX, minPointerOffsetY;

			xDist = xRoot - (w->serverX + (w->serverWidth / 2));
			yDist = yRoot - (w->serverY + (w->serverHeight / 2));

			/* decision threshold is 10% of window size */
			minPointerOffsetX = MIN (20, w->serverWidth / 10);
			minPointerOffsetY = MIN (20, w->serverHeight / 10);

			/* if we reached the threshold in one direction,
			   make the threshold in the other direction smaller
			   so there is a chance that this threshold also can
			   be reached (by diagonal movement) */
			if (abs (xDist) > minPointerOffsetX)
				minPointerOffsetY /= 2;
			else if (abs (yDist) > minPointerOffsetY)
				minPointerOffsetX /= 2;

			if (abs (xDist) > minPointerOffsetX)
			{
				if (xDist > 0)
					rd->mask |= ResizeRightMask;
				else
					rd->mask |= ResizeLeftMask;
			}

			if (abs (yDist) > minPointerOffsetY)
			{
				if (yDist > 0)
					rd->mask |= ResizeDownMask;
				else
					rd->mask |= ResizeUpMask;
			}

			/* if the pointer movement was enough to determine a
			   direction, warp the pointer to the appropriate edge
			   and set the right cursor */
			if (rd->mask)
			{
				Cursor     cursor;
				CompScreen *s = rd->w->screen;
				int        pointerAdjustX = 0;
				int        pointerAdjustY = 0;

				RESIZE_SCREEN (s);

				if (rd->mask & ResizeRightMask)
					pointerAdjustX = w->serverX + w->serverWidth +
					         w->input.right - xRoot;
				else if (rd->mask & ResizeLeftMask)
					pointerAdjustX = w->serverX - w->input.left - xRoot;

				if (rd->mask & ResizeDownMask)
					pointerAdjustY = w->serverY + w->serverHeight +
					         w->input.bottom - yRoot;
				else if (rd->mask & ResizeUpMask)
					pointerAdjustY = w->serverY - w->input.top - yRoot;

				warpPointer (s, pointerAdjustX, pointerAdjustY);

				cursor = resizeCursorFromResizeMask (s, rd->mask);
				updateScreenGrab (s, rs->grabIndex, cursor);
			}
		}
		else
		{
			/* only accumulate pointer movement if a mask is
			   already set as we don't have a use for the
			   difference information otherwise */
			rd->pointerDx += xRoot - lastPointerX;
			rd->pointerDy += yRoot - lastPointerY;
		}

		if (rd->mask & ResizeLeftMask)
			w -= rd->pointerDx;
		else if (rd->mask & ResizeRightMask)
			w += rd->pointerDx;

		if (rd->mask & ResizeUpMask)
			h -= rd->pointerDy;
		else if (rd->mask & ResizeDownMask)
			h += rd->pointerDy;

		if (rd->w->state & CompWindowStateMaximizedVertMask)
			h = rd->w->serverHeight;

		if (rd->w->state & CompWindowStateMaximizedHorzMask)
			w = rd->w->serverWidth;

		constrainNewWindowSize (rd->w, w, h, &w, &h);

		/* compute rect. for window + borders */
		wWidth  = w + rd->w->input.left + rd->w->input.right;
		wHeight = h + rd->w->input.top + rd->w->input.bottom;

		if (rd->mask & ResizeLeftMask)
			wX = rd->savedGeometry.x + rd->savedGeometry.width -
			     (w + rd->w->input.left);
		else
			wX = rd->savedGeometry.x - rd->w->input.left;

		if (rd->mask & ResizeUpMask)
			wY = rd->savedGeometry.y + rd->savedGeometry.height -
			     (h + rd->w->input.top);
		else
			wY = rd->savedGeometry.y - rd->w->input.top;

		/* Check if resized edge(s) are near a work-area boundary */
		for (i = 0; i < s->nOutputDev; i++)
		{
			const XRectangle *workArea = &s->outputDev[i].workArea;

			/* if window and work-area intersect in x axis */
			if (wX + wWidth > workArea->x &&
			    wX < workArea->x + workArea->width)
			{
				if (rd->mask & ResizeLeftMask)
				{
					int dw = workArea->x - wX;

					if (0 < dw && dw < workAreaSnapDistance)
					{
						w      -= dw;
						wWidth -= dw;
						wX     += dw;
					}
				}
				else if (rd->mask & ResizeRightMask)
				{
					int dw = wX + wWidth - (workArea->x + workArea->width);

					if (0 < dw && dw < workAreaSnapDistance)
					{
						w      -= dw;
						wWidth -= dw;
					}
				}
			}

			/* if window and work-area intersect in y axis */
			if (wY + wHeight > workArea->y &&
				wY < workArea->y + workArea->height)
			{
				if (rd->mask & ResizeUpMask)
				{
					int dh = workArea->y - wY;

					if (0 < dh && dh < workAreaSnapDistance)
					{
						h       -= dh;
						wHeight -= dh;
						wY      += dh;
					}
				}
				else if (rd->mask & ResizeDownMask)
				{
					int dh = wY + wHeight - (workArea->y + workArea->height);

					if (0 < dh && dh < workAreaSnapDistance)
					{
						h       -= dh;
						wHeight -= dh;
					}
				}
			}
		}

		if (rd->constraintRegion)
		{
			int minWidth  = 50;
			int minHeight = 50;

			/* rect. for a minimal height window + borders
			   (used for the constraining in X axis) */
			int minimalInputHeight = minHeight +
			                         rd->w->input.top + rd->w->input.bottom;

			/* small hot-spot square (on window's corner or edge) that is to be
			   constrained to the combined output work-area region */
			int x, y;
			int width = rd->w->input.top; /* square size = title bar height */
			int height = width;
			int status;

			/* compute x & y for constrained hot-spot rect */
			if (rd->mask & ResizeLeftMask)
				x = wX;
			else if (rd->mask & ResizeRightMask)
				x = wX + wWidth - width;
			else
				x = MIN (MAX (xRoot, wX), wX + wWidth - width);

			if (rd->mask & ResizeUpMask)
				y = wY;
			else if (rd->mask & ResizeDownMask)
				y = wY + wHeight - height;
			else
				y = MIN (MAX (yRoot, wY), wY + wHeight - height);

			status = XRectInRegion (rd->constraintRegion,
			                        x, y, width, height);

			/* only constrain movement if previous position was valid */
			if (rd->inRegionStatus == RectangleIn)
			{
				int xStatus, yForXResize;
				int nx = x;
				int nw = w;
				int nh = h;

				if (rd->mask & (ResizeLeftMask | ResizeRightMask))
				{
					xStatus = status;

					if (rd->mask & ResizeUpMask)
						yForXResize = wY + wHeight - minimalInputHeight;
					else if (rd->mask & ResizeDownMask)
						yForXResize = wY + minimalInputHeight - height;
					else
						yForXResize = y;

					if (XRectInRegion (rd->constraintRegion,
					                   x, yForXResize,
					                   width, height) != RectangleIn)
					{
						if (rd->lastGoodHotSpotY >= 0)
							yForXResize = rd->lastGoodHotSpotY;
						else
							yForXResize = y;
					}
				}
				if (rd->mask & ResizeLeftMask)
				{
					while ((nw > minWidth) && xStatus != RectangleIn)
					{
						xStatus = XRectInRegion (rd->constraintRegion,
						             nx, yForXResize, width, height);
						if (xStatus != RectangleIn)
						{
							nw--;
							nx++;
						}
					}
					if (nw > minWidth)
					{
						x = nx;
						w = nw;
					}
				}
				else if (rd->mask & ResizeRightMask)
				{
					while ((nw > minWidth) && xStatus != RectangleIn)
					{
						xStatus = XRectInRegion (rd->constraintRegion,
						             nx, yForXResize,
						             width, height);
						if (xStatus != RectangleIn)
						{
							nw--;
							nx--;
						}
					}
					if (nw > minWidth)
					{
						x = nx;
						w = nw;
					}
				}

				if (rd->mask & ResizeUpMask)
				{
					while ((nh > minHeight) && status != RectangleIn)
					{
						status = XRectInRegion (rd->constraintRegion,
						            x, y, width, height);
						if (status != RectangleIn)
						{
							nh--;
							y++;
						}
					}
					if (nh > minHeight)
						h = nh;
				}
				else if (rd->mask & ResizeDownMask)
				{
					while ((nh > minHeight) && status != RectangleIn)
					{
						status = XRectInRegion (rd->constraintRegion,
						            x, y, width, height);
						if (status != RectangleIn)
						{
							nh--;
							y--;
						}
					}
					if (nh > minHeight)
						h = nh;
				}

				if (((rd->mask & (ResizeLeftMask | ResizeRightMask)) &&
					 xStatus == RectangleIn) ||
					((rd->mask & (ResizeUpMask | ResizeDownMask)) &&
					 status == RectangleIn))
				{
					/* hot-spot inside work-area region, store good values */
					rd->lastGoodHotSpotY = y;
					rd->lastGoodWidth    = w;
					rd->lastGoodHeight   = h;
				}
				else
				{
					/* failed to find a good hot-spot position, restore size */
					w = rd->lastGoodWidth;
					h = rd->lastGoodHeight;
				}
			}
			else
			{
				rd->inRegionStatus = status;
			}
		}

		if (rd->mode != RESIZE_MODE_NORMAL)
		{
			if (rd->mode == RESIZE_MODE_STRETCH)
				resizeGetStretchRectangle (&display, &box);
			else
				resizeGetPaintRectangle (&display, &box);

			resizeDamageRectangle (s, &box);
		}

		if (rd->mask & ResizeLeftMask)
			rd->geometry.x -= w - rd->geometry.width;

		if (rd->mask & ResizeUpMask)
			rd->geometry.y -= h - rd->geometry.height;

		rd->geometry.width  = w;
		rd->geometry.height = h;

		if (rd->mode != RESIZE_MODE_NORMAL)
		{
			if (rd->mode == RESIZE_MODE_STRETCH)
				resizeGetStretchRectangle (&display, &box);
			else
				resizeGetPaintRectangle (&display, &box);

			resizeDamageRectangle (s, &box);
		}
		else
		{
			resizeUpdateWindowSize (&display);
		}

		resizeUpdateWindowProperty (&display);
		resizeSendResizeNotify (&display);
	}
}

static void
resizeHandleEvent (XEvent      *event)
{
	CompScreen *s;

	RESIZE_DISPLAY (&display);

	switch (event->type) {
	case ButtonPress:
		if (isButtonPressEvent (event, &initiate_button))
		{
			BananaArgument arg[4];

			arg[0].name = "window";
			arg[0].type = BananaInt;
			arg[0].value.i = display.activeWindow;

			arg[1].name = "modifiers";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xkey.state;

			arg[2].name = "x";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xkey.x_root;

			arg[3].name = "y";
			arg[3].type = BananaInt;
			arg[3].value.i = event->xkey.y_root;

			resizeInitiate (&arg[0], 4);
		}
		break;
	case KeyPress:
		if (event->xkey.keycode == display.escapeKeyCode)
		{
			BananaArgument arg;

			arg.name = "cancel";
			arg.type = BananaBool;
			arg.value.b = TRUE;

			resizeTerminate (&arg, 1);
		}
		else if (isKeyPressEvent (event, &initiate_key))
		{
			BananaArgument arg[5];

			arg[0].name = "window";
			arg[0].type = BananaInt;
			arg[0].value.i = display.activeWindow;

			arg[1].name = "modifiers";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xkey.state;

			arg[2].name = "x";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xkey.x_root;

			arg[3].name = "y";
			arg[3].type = BananaInt;
			arg[3].value.i = event->xkey.y_root;

			arg[4].name = "initiated_by_key";
			arg[4].type = BananaBool;
			arg[4].value.b = TRUE;

			resizeInitiate (&arg[0], 5);
		}
		s = findScreenAtDisplay (event->xkey.root);
		if (s)
			resizeHandleKeyEvent (s, event->xkey.keycode);
		break;
	case ButtonRelease:
		s = findScreenAtDisplay (event->xbutton.root);
		if (s)
		{
			RESIZE_SCREEN (s);

			if (rs->grabIndex)
			{
				if (rd->releaseButton     == -1 ||
				    event->xbutton.button == rd->releaseButton)
				{
					resizeTerminate (NULL, 0);
				}
			}
		}
		break;
	case MotionNotify:
		s = findScreenAtDisplay (event->xmotion.root);
		if (s)
			resizeHandleMotionEvent (s, pointerX, pointerY);
		break;
	case EnterNotify:
	case LeaveNotify:
		s = findScreenAtDisplay (event->xcrossing.root);
		if (s)
			resizeHandleMotionEvent (s, pointerX, pointerY);
		break;
	case ClientMessage:
		if (event->xclient.message_type == display.wmMoveResizeAtom)
		{
			CompWindow *w;

			if (event->xclient.data.l[2] <= WmMoveResizeSizeLeft ||
				event->xclient.data.l[2] == WmMoveResizeSizeKeyboard)
			{
				w = findWindowAtDisplay (event->xclient.window);
				if (w)
				{
					if (event->xclient.data.l[2] == WmMoveResizeSizeKeyboard)
					{
						BananaArgument arg[2];

						arg[0].name = "window";
						arg[0].type = BananaInt;
						arg[0].value.i = event->xclient.window;

						arg[1].name = "external";
						arg[1].type = BananaBool;
						arg[1].value.b = TRUE;

						resizeInitiate (&arg[0], 1);
					}
					else
					{
						static unsigned int mask[] = {
						    ResizeUpMask | ResizeLeftMask,
						    ResizeUpMask,
						    ResizeUpMask | ResizeRightMask,
						    ResizeRightMask,
						    ResizeDownMask | ResizeRightMask,
						    ResizeDownMask,
						    ResizeDownMask | ResizeLeftMask,
						    ResizeLeftMask,
						};
						unsigned int mods;
						Window       root, child;
						int          xRoot, yRoot, i;

						XQueryPointer (display.display, w->screen->root,
						           &root, &child, &xRoot, &yRoot,
						           &i, &i, &mods);

						/* TODO: not only button 1 */
						if (mods & Button1Mask)
						{
							BananaArgument arg[7];

							arg[0].name = "window";
							arg[0].type = BananaInt;
							arg[0].value.i = event->xclient.window;

							arg[1].name = "external";
							arg[1].type = BananaBool;
							arg[1].value.b = TRUE;

							arg[2].name    = "modifiers";
							arg[2].type    = BananaInt;
							arg[2].value.i = mods;

							arg[3].name    = "x";
							arg[3].type    = BananaInt;
							arg[3].value.i = event->xclient.data.l[0];

							arg[4].name    = "y";
							arg[4].type    = BananaInt;
							arg[4].value.i = event->xclient.data.l[1];

							arg[5].name    = "direction";
							arg[5].type    = BananaInt;
							arg[5].value.i = mask[event->xclient.data.l[2]];

							arg[6].name    = "button";
							arg[6].type    = BananaInt;
							arg[6].value.i = event->xclient.data.l[3] ?
							                event->xclient.data.l[3] : -1;

							resizeInitiate (&arg[0], 7);

							resizeHandleMotionEvent (w->screen, xRoot, yRoot);
						}
					}
				}
			}
			else if (rd->w && event->xclient.data.l[2] == WmMoveResizeCancel)
			{
				if (rd->w->id == event->xclient.window)
				{
					BananaArgument arg;

					arg.name = "cancel";
					arg.type = BananaBool;
					arg.value.b = TRUE;

					resizeTerminate (&arg, 1);
				}
			}
		}
		break;
	case DestroyNotify:
		if (rd->w && rd->w->id == event->xdestroywindow.window)
			resizeTerminate (NULL, 0);
		break;
	case UnmapNotify:
		if (rd->w && rd->w->id == event->xunmap.window)
			resizeTerminate (NULL, 0);
	default:
		break;
	}

	UNWRAP (rd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (rd, &display, handleEvent, resizeHandleEvent);

	if (event->type == display.syncEvent + XSyncAlarmNotify)
	{
		if (rd->w)
		{
			XSyncAlarmNotifyEvent *sa;

			sa = (XSyncAlarmNotifyEvent *) event;

			if (rd->w->syncAlarm == sa->alarm)
				resizeUpdateWindowSize (&display);
		}
	}
}

static void
resizeWindowResizeNotify (CompWindow *w,
                          int        dx,
                          int        dy,
                          int        dwidth,
                          int        dheight)
{
	RESIZE_DISPLAY (&display);
	RESIZE_SCREEN (w->screen);

	UNWRAP (rs, w->screen, windowResizeNotify);
	(*w->screen->windowResizeNotify) (w, dx, dy, dwidth, dheight);
	WRAP (rs, w->screen, windowResizeNotify, resizeWindowResizeNotify);

	if (rd->w == w && !rs->grabIndex)
		resizeFinishResizing (&display);
}

static void
resizePaintRectangle (CompScreen              *s,
                      const ScreenPaintAttrib *sa,
                      const CompTransform     *transform,
                      CompOutput              *output,
                      unsigned short          *borderColor,
                      unsigned short          *fillColor)
{
	BoxRec        box;
	CompTransform sTransform = *transform;

	resizeGetPaintRectangle (&display, &box);

	glPushMatrix ();

	transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

	glLoadMatrixf (sTransform.m);

	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	glEnable (GL_BLEND);

	/* fill rectangle */
	if (fillColor)
	{
		glColor4usv (fillColor);
		glRecti (box.x1, box.y2, box.x2, box.y1);
	}

	/* draw outline */
	glColor4usv (borderColor);
	glLineWidth (2.0);
	glBegin (GL_LINE_LOOP);
	glVertex2i (box.x1, box.y1);
	glVertex2i (box.x2, box.y1);
	glVertex2i (box.x2, box.y2);
	glVertex2i (box.x1, box.y2);
	glEnd ();

	/* clean up */
	glColor4usv (defaultColor);
	glDisable (GL_BLEND);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	glPopMatrix ();
}

static Bool
resizePaintOutput (CompScreen              *s,
                   const ScreenPaintAttrib *sAttrib,
                   const CompTransform     *transform,
                   Region                  region,
                   CompOutput              *output,
                   unsigned int            mask)
{
	Bool status;

	RESIZE_SCREEN (s);
	RESIZE_DISPLAY (&display);

	if (rd->w && (s == rd->w->screen))
	{
		if (rd->mode == RESIZE_MODE_STRETCH)
			mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;
	}

	UNWRAP (rs, s, paintOutput);
	status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
	WRAP (rs, s, paintOutput, resizePaintOutput);

	if (status && rd->w && (s == rd->w->screen))
	{
		unsigned short border[4], fill[4];

		const BananaValue *
		option_border_color = bananaGetOption (bananaIndex, "border_color", -1);

		const BananaValue *
		option_fill_color = bananaGetOption (bananaIndex, "fill_color", -1);

		stringToColor (option_border_color->s, border);
		stringToColor (option_fill_color->s, fill);

		switch (rd->mode) {
		case RESIZE_MODE_OUTLINE:
			resizePaintRectangle (s, sAttrib, transform, output, border, NULL);
			break;
		case RESIZE_MODE_RECTANGLE:
			resizePaintRectangle (s, sAttrib, transform, output, border, fill);
		default:
			break;
		}
	}

	return status;
}

static Bool
resizePaintWindow (CompWindow              *w,
                   const WindowPaintAttrib *attrib,
                   const CompTransform     *transform,
                   Region                  region,
                   unsigned int            mask)
{
	CompScreen *s = w->screen;
	Bool       status;

	RESIZE_SCREEN (s);
	RESIZE_DISPLAY (&display);

	if (w == rd->w && rd->mode == RESIZE_MODE_STRETCH)
	{
		FragmentAttrib fragment;
		CompTransform  wTransform = *transform;
		BoxRec         box;
		float          xOrigin, yOrigin;
		float          xScale, yScale;

		if (mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK)
			return FALSE;

		UNWRAP (rs, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region,
		                        mask | PAINT_WINDOW_NO_CORE_INSTANCE_MASK);
		WRAP (rs, s, paintWindow, resizePaintWindow);

		initFragmentAttrib (&fragment, &w->lastPaint);

		if (w->alpha || fragment.opacity != OPAQUE)
			mask |= PAINT_WINDOW_TRANSLUCENT_MASK;

		resizeGetPaintRectangle (&display, &box);
		resizeGetStretchScale (w, &box, &xScale, &yScale);

		xOrigin = w->attrib.x - w->input.left;
		yOrigin = w->attrib.y - w->input.top;

		matrixTranslate (&wTransform, xOrigin, yOrigin, 0.0f);
		matrixScale (&wTransform, xScale, yScale, 1.0f);
		matrixTranslate (&wTransform,
		                 (rd->geometry.x - w->attrib.x) / xScale - xOrigin,
		                 (rd->geometry.y - w->attrib.y) / yScale - yOrigin,
		                 0.0f);

		glPushMatrix ();
		glLoadMatrixf (wTransform.m);

		(*s->drawWindow) (w, &wTransform, &fragment, region,
		                  mask | PAINT_WINDOW_TRANSFORMED_MASK);

		glPopMatrix ();
	}
	else
	{
		UNWRAP (rs, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP (rs, s, paintWindow, resizePaintWindow);
	}

	return status;
}

static Bool
resizeDamageWindowRect (CompWindow *w,
                        Bool       initial,
                        BoxPtr     rect)
{
	Bool status = FALSE;

	RESIZE_SCREEN (w->screen);
	RESIZE_DISPLAY (&display);

	if (w == rd->w && rd->mode == RESIZE_MODE_STRETCH)
	{
		BoxRec box;

		resizeGetStretchRectangle (&display, &box);
		resizeDamageRectangle (w->screen, &box);

		status = TRUE;
	}

	UNWRAP (rs, w->screen, damageWindowRect);
	status |= (*w->screen->damageWindowRect) (w, initial, rect);
	WRAP (rs, w->screen, damageWindowRect, resizeDamageWindowRect);

	return status;
}

static Bool
resizeInitDisplay (CompPlugin  *p,
                   CompDisplay *d)
{
	ResizeDisplay *rd;
	int           i;

	rd = malloc (sizeof (ResizeDisplay));
	if (!rd)
		return FALSE;

	rd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (rd->screenPrivateIndex < 0)
	{
		free (rd);
		return FALSE;
	}

	rd->w = 0;

	rd->releaseButton = 0;

	rd->resizeNotifyAtom      = XInternAtom (d->display,
	                                 "_FUSILLI_RESIZE_NOTIFY", 0);
	rd->resizeInformationAtom = XInternAtom (d->display,
	                                 "_FUSILLI_RESIZE_INFORMATION", 0);

	for (i = 0; i < NUM_KEYS; i++)
		rd->key[i] = XKeysymToKeycode (d->display,
		                      XStringToKeysym (rKeys[i].name));

	rd->constraintRegion = NULL;

	WRAP (rd, d, handleEvent, resizeHandleEvent);

	d->base.privates[displayPrivateIndex].ptr = rd;

	return TRUE;
}

static void
resizeFiniDisplay (CompPlugin  *p,
                   CompDisplay *d)
{
	RESIZE_DISPLAY (d);

	freeScreenPrivateIndex (rd->screenPrivateIndex);

	UNWRAP (rd, d, handleEvent);

	if (rd->constraintRegion)
		XDestroyRegion (rd->constraintRegion);

	free (rd);
}

static Bool
resizeInitScreen (CompPlugin *p,
                  CompScreen *s)
{
	ResizeScreen *rs;

	RESIZE_DISPLAY (&display);

	rs = malloc (sizeof (ResizeScreen));
	if (!rs)
		return FALSE;

	rs->grabIndex = 0;

	Display *dpy = display.display;

	rs->leftCursor      = XCreateFontCursor (dpy, XC_left_side);
	rs->rightCursor     = XCreateFontCursor (dpy, XC_right_side);
	rs->upCursor        = XCreateFontCursor (dpy, XC_top_side);
	rs->upLeftCursor    = XCreateFontCursor (dpy, XC_top_left_corner);
	rs->upRightCursor   = XCreateFontCursor (dpy, XC_top_right_corner);
	rs->downCursor      = XCreateFontCursor (dpy, XC_bottom_side);
	rs->downLeftCursor  = XCreateFontCursor (dpy, XC_bottom_left_corner);
	rs->downRightCursor = XCreateFontCursor (dpy, XC_bottom_right_corner);
	rs->middleCursor    = XCreateFontCursor (dpy, XC_fleur);

	rs->cursor[0] = rs->leftCursor;
	rs->cursor[1] = rs->rightCursor;
	rs->cursor[2] = rs->upCursor;
	rs->cursor[3] = rs->downCursor;

	WRAP (rs, s, windowResizeNotify, resizeWindowResizeNotify);
	WRAP (rs, s, paintOutput, resizePaintOutput);
	WRAP (rs, s, paintWindow, resizePaintWindow);
	WRAP (rs, s, damageWindowRect, resizeDamageWindowRect);

	s->base.privates[rd->screenPrivateIndex].ptr = rs;

	return TRUE;
}

static void
resizeFiniScreen (CompPlugin *p,
                  CompScreen *s)
{
	RESIZE_SCREEN (s);

	Display *dpy = display.display;

	if (rs->leftCursor)
		XFreeCursor (dpy, rs->leftCursor);
	if (rs->rightCursor)
		XFreeCursor (dpy, rs->rightCursor);
	if (rs->upCursor)
		XFreeCursor (dpy, rs->upCursor);
	if (rs->downCursor)
		XFreeCursor (dpy, rs->downCursor);
	if (rs->middleCursor)
		XFreeCursor (dpy, rs->middleCursor);
	if (rs->upLeftCursor)
		XFreeCursor (dpy, rs->upLeftCursor);
	if (rs->upRightCursor)
		XFreeCursor (dpy, rs->upRightCursor);
	if (rs->downLeftCursor)
		XFreeCursor (dpy, rs->downLeftCursor);
	if (rs->downRightCursor)
		XFreeCursor (dpy, rs->downRightCursor);

	UNWRAP (rs, s, windowResizeNotify);
	UNWRAP (rs, s, paintOutput);
	UNWRAP (rs, s, paintWindow);
	UNWRAP (rs, s, damageWindowRect);

	free (rs);
}

static CompBool
resizeInitObject (CompPlugin *p,
                  CompObject *o)
{
	static InitPluginObjectProc dispTab[] = {
		(InitPluginObjectProc) 0, /* InitCore */
		(InitPluginObjectProc) resizeInitDisplay,
		(InitPluginObjectProc) resizeInitScreen
	};

	RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
resizeFiniObject (CompPlugin *p,
                  CompObject *o)
{
	static FiniPluginObjectProc dispTab[] = {
		(FiniPluginObjectProc) 0, /* FiniCore */
		(FiniPluginObjectProc) resizeFiniDisplay,
		(FiniPluginObjectProc) resizeFiniScreen
	};

	DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static void
resizeChangeNotify (const char        *optionName,
                    BananaType        optionType,
                    const BananaValue *optionValue,
                    int               screenNum)
{
	if (strcasecmp (optionName, "initiate_button") == 0)
		updateButton (optionValue->s, &initiate_button);

	else if (strcasecmp (optionName, "initiate_key") == 0)
		updateKey (optionValue->s, &initiate_key);
}

static Bool
resizeInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("resize", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("resize");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, resizeChangeNotify);

	const BananaValue *
	option_initiate_button = bananaGetOption (bananaIndex,
	                                          "initiate_button",
	                                          -1);

	const BananaValue *
	option_initiate_key = bananaGetOption (bananaIndex,
	                                       "initiate_key",
	                                       -1);

	registerKey (option_initiate_key->s, &initiate_key);
	registerButton (option_initiate_button->s, &initiate_button);

	return TRUE;
}

static void
resizeFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

CompPluginVTable resizeVTable = {
	"resize",
	resizeInit,
	resizeFini,
	resizeInitObject,
	resizeFiniObject
};

CompPluginVTable *
getCompPluginInfo20140724 (void)
{
	return &resizeVTable;
}
