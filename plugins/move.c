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

#include <X11/cursorfont.h>

#include <fusilli-core.h>

static int bananaIndex;

static CompKeyBinding initiate_key;
static CompButtonBinding initiate_button;

struct _MoveKeys {
	char *name;
	int  dx;
	int  dy;
} mKeys[] = {
	{ "Left",  -1,  0 },
	{ "Right",  1,  0 },
	{ "Up",     0, -1 },
	{ "Down",   0,  1 }
};

#define NUM_KEYS (sizeof (mKeys) / sizeof (mKeys[0]))

#define KEY_MOVE_INC 24

#define SNAP_BACK 20
#define SNAP_OFF  100

static int displayPrivateIndex;

typedef struct _MoveDisplay {
	int             screenPrivateIndex;
	HandleEventProc handleEvent;

	CompWindow *w;
	int	       savedX;
	int	       savedY;
	int	       x;
	int	       y;
	Region     region;
	int        status;
	Bool       constrainY;
	KeyCode    key[NUM_KEYS];

	int releaseButton;

	GLushort moveOpacity;
} MoveDisplay;

typedef struct _MoveScreen {
	PaintWindowProc paintWindow;

	int grabIndex;

	Cursor moveCursor;

	unsigned int origState;

	int	snapOffY;
	int	snapBackY;
} MoveScreen;

#define GET_MOVE_DISPLAY(d) \
        ((MoveDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define MOVE_DISPLAY(d) \
        MoveDisplay *md = GET_MOVE_DISPLAY (d)

#define GET_MOVE_SCREEN(s, md) \
        ((MoveScreen *) (s)->base.privates[(md)->screenPrivateIndex].ptr)

#define MOVE_SCREEN(s) \
        MoveScreen *ms = GET_MOVE_SCREEN (s, GET_MOVE_DISPLAY (&display))

static Bool
moveInitiate (BananaArgument     *arg,
              int                nArg)
{
	CompWindow *w;
	Window     xid;

	MOVE_DISPLAY (&display);

	BananaValue *window = getArgNamed ("window", arg, nArg);

	if (window != NULL)
		xid = window->i;
	else
		xid = 0;

	w = findWindowAtDisplay (xid);
	if (w && (w->actions & CompWindowActionMoveMask))
	{
		XRectangle   workArea;
		unsigned int mods;
		int          x, y, button;
		Bool         sourceExternalApp;

		MOVE_SCREEN (w->screen);

		BananaValue *modifiers = getArgNamed ("modifiers", arg, nArg);
		if (modifiers != NULL)
			mods = modifiers->i;
		else
			mods = 0;

		BananaValue *arg_x = getArgNamed ("x", arg, nArg);
		if (arg_x != NULL)
			x = arg_x->i;
		else
			x = w->attrib.x + (w->width / 2);

		BananaValue *arg_y = getArgNamed ("y", arg, nArg);
		if (arg_y != NULL)
			y = arg_y->i;
		else
			y = w->attrib.y + (w->height / 2);

		BananaValue *arg_button = getArgNamed ("button", arg, nArg);
		if (arg_button != NULL)
			button = arg_button->i;
		else
			button = -1;

		if (otherScreenGrabExist (w->screen, "move", NULL))
			return FALSE;

		if (md->w)
			return FALSE;

		if (w->type & (CompWindowTypeDesktopMask |
		               CompWindowTypeDockMask    |
		               CompWindowTypeFullscreenMask))
			return FALSE;

		if (w->attrib.override_redirect)
			return FALSE;

		if (md->region)
		{
			XDestroyRegion (md->region);
			md->region = NULL;
		}

		md->status = RectangleOut;

		md->savedX = w->serverX;
		md->savedY = w->serverY;

		md->x = 0;
		md->y = 0;

		BananaValue *arg_external = getArgNamed ("external", arg, nArg);

		if (arg_external != NULL)
			sourceExternalApp = arg_external->b;
		else
			sourceExternalApp = FALSE;

		const BananaValue *
		option_constrain_y = bananaGetOption (bananaIndex, "constrain_y", -1);

		md->constrainY = sourceExternalApp && option_constrain_y->b;

		lastPointerX = x;
		lastPointerY = y;

		ms->origState = w->state;

		getWorkareaForOutput (w->screen,
		                      outputDeviceForWindow (w),
		                      &workArea);

		ms->snapBackY = w->serverY - workArea.y;
		ms->snapOffY  = y - workArea.y;

		if (!ms->grabIndex)
			ms->grabIndex = pushScreenGrab (w->screen, ms->moveCursor, "move");

		if (ms->grabIndex)
		{
			unsigned int grabMask = CompWindowGrabMoveMask |
			                    CompWindowGrabButtonMask;

			if (sourceExternalApp)
				grabMask |= CompWindowGrabExternalAppMask;

			md->w = w;

			md->releaseButton = button;

			(w->screen->windowGrabNotify) (w, x, y, mods, grabMask);

			const BananaValue *option_raise_on_click = bananaGetOption (
			    coreBananaIndex, "raise_on_click", -1);

			if (option_raise_on_click->b)
				updateWindowAttributes (w,
				                  CompStackingUpdateModeAboveFullscreen);

			BananaValue *arg_cursor_at_center = getArgNamed ("cursor_at_center",
			                                                 arg, nArg);

			if (arg_cursor_at_center != NULL && arg_cursor_at_center->b)
			{
				int xRoot, yRoot;

				xRoot = w->attrib.x + (w->width  / 2);
				yRoot = w->attrib.y + (w->height / 2);

				warpPointer (w->screen, xRoot - pointerX, yRoot - pointerY);
			}

			if (md->moveOpacity != OPAQUE)
				addWindowDamage (w);
		}
	}

	return FALSE;
}

static Bool
moveTerminate (BananaArgument     *arg,
              int                 nArg)
{
	MOVE_DISPLAY (&display);

	if (md->w)
	{
		MOVE_SCREEN (md->w->screen);

		BananaValue *cancel = getArgNamed ("cancel", arg, nArg);

		if (cancel != NULL && cancel->b)
			moveWindow (md->w,
			            md->savedX - md->w->attrib.x,
			            md->savedY - md->w->attrib.y,
			            TRUE, FALSE);

		syncWindowPosition (md->w);

		/* update window size as window constraints may have
		   changed - needed e.g. if a maximized window was moved
		   to another output device */
		updateWindowSize (md->w);

		(md->w->screen->windowUngrabNotify) (md->w);

		if (ms->grabIndex)
		{
			removeScreenGrab (md->w->screen, ms->grabIndex, NULL);
			ms->grabIndex = 0;
		}

		if (md->moveOpacity != OPAQUE)
			addWindowDamage (md->w);

		md->w             = 0;
		md->releaseButton = 0;
	}

	return FALSE;
}

/* creates a region containing top and bottom struts. only struts that are
   outside the screen workarea are considered. */
static Region
moveGetYConstrainRegion (CompScreen *s)
{
	CompWindow *w;
	Region     region;
	REGION     r;
	XRectangle workArea;
	BoxRec     extents;
	int        i;

	region = XCreateRegion ();
	if (!region)
		return NULL;

	r.rects    = &r.extents;
	r.numRects = r.size = 1;

	r.extents.x1 = MINSHORT;
	r.extents.y1 = 0;
	r.extents.x2 = 0;
	r.extents.y2 = s->height;

	XUnionRegion (&r, region, region);

	r.extents.x1 = s->width;
	r.extents.x2 = MAXSHORT;

	XUnionRegion (&r, region, region);

	for (i = 0; i < s->nOutputDev; i++)
	{
		XUnionRegion (&s->outputDev[i].region, region, region);

		getWorkareaForOutput (s, i, &workArea);
		extents = s->outputDev[i].region.extents;

		for (w = s->windows; w; w = w->next)
		{
			if (!w->mapNum)
				continue;

			if (w->struts)
			{
				r.extents.x1 = w->struts->top.x;
				r.extents.y1 = w->struts->top.y;
				r.extents.x2 = r.extents.x1 + w->struts->top.width;
				r.extents.y2 = r.extents.y1 + w->struts->top.height;

				if (r.extents.x1 < extents.x1)
					r.extents.x1 = extents.x1;
				if (r.extents.x2 > extents.x2)
					r.extents.x2 = extents.x2;
				if (r.extents.y1 < extents.y1)
					r.extents.y1 = extents.y1;
				if (r.extents.y2 > extents.y2)
					r.extents.y2 = extents.y2;

				if (r.extents.x1 < r.extents.x2 && r.extents.y1 < r.extents.y2)
				{
					if (r.extents.y2 <= workArea.y)
						XSubtractRegion (region, &r, region);
				}

				r.extents.x1 = w->struts->bottom.x;
				r.extents.y1 = w->struts->bottom.y;
				r.extents.x2 = r.extents.x1 + w->struts->bottom.width;
				r.extents.y2 = r.extents.y1 + w->struts->bottom.height;

				if (r.extents.x1 < extents.x1)
					r.extents.x1 = extents.x1;
				if (r.extents.x2 > extents.x2)
					r.extents.x2 = extents.x2;
				if (r.extents.y1 < extents.y1)
					r.extents.y1 = extents.y1;
				if (r.extents.y2 > extents.y2)
					r.extents.y2 = extents.y2;

				if (r.extents.x1 < r.extents.x2 && r.extents.y1 < r.extents.y2)
				{
					if (r.extents.y1 >= (workArea.y + workArea.height))
						XSubtractRegion (region, &r, region);
				}
			}
		}
	}

	return region;
}

static void
moveHandleMotionEvent (CompScreen *s,
                       int        xRoot,
                       int        yRoot)
{
	MOVE_SCREEN (s);

	if (ms->grabIndex)
	{
		CompWindow *w;
		int        dx, dy;
		int        wX, wY;
		int        wWidth, wHeight;

		MOVE_DISPLAY (&display);

		w = md->w;

		wX      = w->serverX;
		wY      = w->serverY;
		wWidth  = w->serverWidth  + w->serverBorderWidth * 2;
		wHeight = w->serverHeight + w->serverBorderWidth * 2;

		md->x += xRoot - lastPointerX;
		md->y += yRoot - lastPointerY;

		if (w->type & CompWindowTypeFullscreenMask)
		{
			dx = dy = 0;
		}
		else
		{
			XRectangle workArea;
			int        min, max;

			dx = md->x;
			dy = md->y;

			getWorkareaForOutput (s,
			                  outputDeviceForWindow (w),
			                  &workArea);

			if (md->constrainY)
			{
				if (!md->region)
					md->region = moveGetYConstrainRegion (s);

				/* make sure that the top frame extents or the top row of
				   pixels are within what is currently our valid screen
				   region */
				if (md->region)
				{
					int x, y, width, height;
					int status;

					x      = wX + dx - w->input.left;
					y      = wY + dy - w->input.top;
					width  = wWidth + w->input.left + w->input.right;
					height = w->input.top ? w->input.top : 1;

					status = XRectInRegion (md->region, x, y, width, height);

					/* only constrain movement if previous position was valid */
					if (md->status == RectangleIn)
					{
						int xStatus = status;

						while (dx && xStatus != RectangleIn)
						{
							xStatus = XRectInRegion (md->region,
							             x, y - dy,
							             width, height);

							if (xStatus != RectangleIn)
							dx += (dx < 0) ? 1 : -1;

							x = wX + dx - w->input.left;
						}

						while (dy && status != RectangleIn)
						{
							status = XRectInRegion (md->region,
							            x, y,
							            width, height);

							if (status != RectangleIn)
								dy += (dy < 0) ? 1 : -1;

							y = wY + dy - w->input.top;
						}
					}
					else
					{
						md->status = status;
					}
				}
			}

			const BananaValue *option_snapoff_maximized = bananaGetOption (
			    bananaIndex, "snapoff_maximized", -1);

			if (option_snapoff_maximized->b)
			{
				if (w->state & CompWindowStateMaximizedVertMask)
				{
					if (abs ((yRoot - workArea.y) - ms->snapOffY) >= SNAP_OFF)
					{
						if (!otherScreenGrabExist (s, "move", NULL))
						{
							int width = w->serverWidth;

							w->saveMask |= CWX | CWY;

							if (w->saveMask & CWWidth)
								width = w->saveWc.width;

							w->saveWc.x = xRoot - (width >> 1);
							w->saveWc.y = yRoot + (w->input.top >> 1);

							md->x = md->y = 0;

							maximizeWindow (w, 0);

							ms->snapOffY = ms->snapBackY;

							return;
						}
					}
				}
				else if (ms->origState & CompWindowStateMaximizedVertMask)
				{
					if (abs ((yRoot - workArea.y) - ms->snapBackY) < SNAP_BACK)
					{
						if (!otherScreenGrabExist (s, "move", NULL))
						{
							int wy;

							/* update server position before maximizing
							   window again so that it is maximized on
							   correct output */
							syncWindowPosition (w);

							maximizeWindow (w, ms->origState);

							wy  = workArea.y + (w->input.top >> 1);
							wy += w->sizeHints.height_inc >> 1;

							warpPointer (s, 0, wy - pointerY);

							return;
						}
					}
				}
			}

			if (w->state & CompWindowStateMaximizedVertMask)
			{
				min = workArea.y + w->input.top;
				max = workArea.y + workArea.height - w->input.bottom - wHeight;

				if (wY + dy < min)
					dy = min - wY;
				else if (wY + dy > max)
					dy = max - wY;
			}

			if (w->state & CompWindowStateMaximizedHorzMask)
			{
				if (wX > s->width || wX + w->width < 0)
					return;

				if (wX + wWidth < 0)
					return;

				min = workArea.x + w->input.left;
				max = workArea.x + workArea.width - w->input.right - wWidth;

				if (wX + dx < min)
					dx = min - wX;
				else if (wX + dx > max)
					dx = max - wX;
			}
		}

		if (dx || dy)
		{
			moveWindow (w,
			            wX + dx - w->attrib.x,
			            wY + dy - w->attrib.y,
			            TRUE, FALSE);

			const BananaValue *option_lazy_positioning = bananaGetOption (
			        bananaIndex, "lazy_positioning", -1);

			if (option_lazy_positioning->b)
			{
				/* FIXME: This form of lazy positioning is broken and should
				   be replaced asap. Current code exists just to avoid a
				   major performance regression in the 0.5.2 release. */
				w->serverX = w->attrib.x;
				w->serverY = w->attrib.y;
			}
			else
			{
				syncWindowPosition (w);
			}

			md->x -= dx;
			md->y -= dy;
		}
	}
}

static void
moveHandleEvent (XEvent      *event)
{
	CompScreen *s;

	MOVE_DISPLAY (&display);

	switch (event->type) {
	case ButtonPress:
		if (isButtonPressEvent (event, &initiate_button))
		{
			BananaArgument arg[5];

			arg[0].name = "window";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xbutton.window;

			arg[1].name = "modifiers";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xbutton.state;

			arg[2].name = "x";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xbutton.x_root;

			arg[3].name = "y";
			arg[3].type = BananaInt;
			arg[3].value.i = event->xbutton.y_root;

			arg[4].name = "button";
			arg[4].type = BananaInt;
			arg[4].value.i = event->xbutton.button;

			moveInitiate (&arg[0], 5);
		}
		break;
	case ButtonRelease:
		s = findScreenAtDisplay (event->xbutton.root);
		if (s)
		{
			MOVE_SCREEN (s);

			if (ms->grabIndex)
			{
				if (md->releaseButton == -1 ||
				    initiate_button.button == event->xbutton.button)
				{
					BananaArgument arg;

					arg.name = "cancel";
					arg.type = BananaBool;
					arg.value.b = FALSE;

					moveTerminate (&arg, 1);
				}
			}
		}
		break;
	case KeyPress:
		if (isKeyPressEvent (event, &initiate_key))
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

			arg[4].name = "cursor_at_center";
			arg[4].type = BananaBool;
			arg[4].value.b = TRUE;

			moveInitiate (&arg[0], 5);
		}
		s = findScreenAtDisplay (event->xkey.root);
		if (s)
		{
			MOVE_SCREEN (s);

			if (ms->grabIndex)
			{
				int i;

				for (i = 0; i < NUM_KEYS; i++)
				{
					if (event->xkey.keycode == md->key[i])
					{
						XWarpPointer (display.display, None, None, 0, 0, 0, 0,
						          mKeys[i].dx * KEY_MOVE_INC,
						          mKeys[i].dy * KEY_MOVE_INC);
						break;
					}
				}
				if (event->xkey.keycode == display.escapeKeyCode)
				{
					BananaArgument arg;

					arg.name = "cancel";
					arg.type = BananaBool;
					arg.value.b = TRUE;

					moveTerminate (&arg, 1);
				}
			}
		}
		break;
	case MotionNotify:
		s = findScreenAtDisplay (event->xmotion.root);
		if (s)
			moveHandleMotionEvent (s, pointerX, pointerY);
		break;
	case EnterNotify:
	case LeaveNotify:
		s = findScreenAtDisplay (event->xcrossing.root);
		if (s)
			moveHandleMotionEvent (s, pointerX, pointerY);
		break;
	case ClientMessage:
		if (event->xclient.message_type == display.wmMoveResizeAtom)
		{
			CompWindow *w;

			if (event->xclient.data.l[2] == WmMoveResizeMove ||
			    event->xclient.data.l[2] == WmMoveResizeMoveKeyboard)
			{
				w = findWindowAtDisplay (event->xclient.window);
				if (w)
				{
					if (event->xclient.data.l[2] == WmMoveResizeMoveKeyboard)
					{
						BananaArgument arg[2];

						arg[0].name = "window";
						arg[0].type = BananaInt;
						arg[0].value.i = event->xclient.window;

						arg[1].name = "external";
						arg[1].type = BananaBool;
						arg[1].value.b = TRUE;

						moveInitiate (&arg[0], 1);
					}
					else
					{
						unsigned int mods;
						Window       root, child;
						int          i;
						int        xRoot, yRoot;

						XQueryPointer (display.display, w->screen->root,
						           &root, &child, &xRoot, &yRoot,
						           &i, &i, &mods);

						/* TODO: not only button 1 */

						if (mods & Button1Mask)
						{
							BananaArgument arg[6];

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

							//arg[5].name    = "button";
							//arg[5].type    = BananaInt;
							//arg[5].value.i = event->xclient.data.l[3] ?
							//           event->xclient.data.l[3] : -1;

							moveInitiate (&arg[0], 5);

							moveHandleMotionEvent (w->screen, xRoot, yRoot);
						}
					}
				}
			}
			else if (md->w && event->xclient.data.l[2] == WmMoveResizeCancel)
			{
				if (md->w->id == event->xclient.window)
				{
					BananaArgument arg;

					arg.name = "cancel";
					arg.type = BananaBool;
					arg.value.b = TRUE;

					moveTerminate (&arg, 1);
				}
			}
		}
		break;
	case DestroyNotify:
		if (md->w && md->w->id == event->xdestroywindow.window)
			moveTerminate (NULL, 0);
		break;
	case UnmapNotify:
		if (md->w && md->w->id == event->xunmap.window)
			moveTerminate (NULL, 0);
		break;
	default:
		break;
	}

	UNWRAP (md, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (md, &display, handleEvent, moveHandleEvent);
}

static Bool
movePaintWindow (CompWindow              *w,
                 const WindowPaintAttrib *attrib,
                 const CompTransform     *transform,
                 Region                  region,
                 unsigned int            mask)
{
	WindowPaintAttrib sAttrib;
	CompScreen        *s = w->screen;
	Bool              status;

	MOVE_SCREEN (s);

	if (ms->grabIndex)
	{
		MOVE_DISPLAY (&display);

		if (md->w == w && md->moveOpacity != OPAQUE)
		{
			/* modify opacity of windows that are not active */
			sAttrib = *attrib;
			attrib  = &sAttrib;

			sAttrib.opacity = (sAttrib.opacity * md->moveOpacity) >> 16;
		}
	}

	UNWRAP (ms, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP (ms, s, paintWindow, movePaintWindow);

	return status;
}

static void
moveChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	MOVE_DISPLAY (&display);

	if (strcasecmp (optionName, "opacity") == 0)
		md->moveOpacity = (optionValue->i * OPAQUE) / 100;
	else if (strcasecmp (optionName, "initiate_button") == 0)
		updateButton (optionValue->s, &initiate_button);
	else if (strcasecmp (optionName, "initiate_key") == 0)
		updateKey (optionValue->s, &initiate_key);
}

static Bool
moveInitDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	MoveDisplay *md;
	int         i;

	md = malloc (sizeof (MoveDisplay));
	if (!md)
		return FALSE;

	md->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (md->screenPrivateIndex < 0)
	{
		free (md);
		return FALSE;
	}

	const BananaValue *
	option_opacity = bananaGetOption (bananaIndex, "opacity", -1);

	md->moveOpacity = (option_opacity->i * OPAQUE) / 100;

	md->w             = 0;
	md->region        = NULL;
	md->status        = RectangleOut;
	md->releaseButton = 0;
	md->constrainY    = FALSE;

	for (i = 0; i < NUM_KEYS; i++)
		md->key[i] = XKeysymToKeycode (d->display,
		                           XStringToKeysym (mKeys[i].name));

	WRAP (md, d, handleEvent, moveHandleEvent);

	d->base.privates[displayPrivateIndex].ptr = md;

	return TRUE;
}

static void
moveFiniDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	MOVE_DISPLAY (d);

	freeScreenPrivateIndex (md->screenPrivateIndex);

	UNWRAP (md, d, handleEvent);

	if (md->region)
		XDestroyRegion (md->region);

	free (md);
}

static Bool
moveInitScreen (CompPlugin *p,
                CompScreen *s)
{
	MoveScreen *ms;

	MOVE_DISPLAY (&display);

	ms = malloc (sizeof (MoveScreen));
	if (!ms)
		return FALSE;

	ms->grabIndex = 0;

	ms->moveCursor = XCreateFontCursor (display.display, XC_fleur);

	WRAP (ms, s, paintWindow, movePaintWindow);

	s->base.privates[md->screenPrivateIndex].ptr = ms;

	return TRUE;
}

static void
moveFiniScreen (CompPlugin *p,
                CompScreen *s)
{
	MOVE_SCREEN (s);

	UNWRAP (ms, s, paintWindow);

	if (ms->moveCursor)
		XFreeCursor (display.display, ms->moveCursor);

	free (ms);
}

static CompBool
moveInitObject (CompPlugin *p,
                CompObject *o)
{
	static InitPluginObjectProc dispTab[] = {
		(InitPluginObjectProc) 0, /* InitCore */
		(InitPluginObjectProc) moveInitDisplay,
		(InitPluginObjectProc) moveInitScreen
	};

	RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
moveFiniObject (CompPlugin *p,
                CompObject *o)
{
	static FiniPluginObjectProc dispTab[] = {
		(FiniPluginObjectProc) 0, /* FiniCore */
		(FiniPluginObjectProc) moveFiniDisplay,
		(FiniPluginObjectProc) moveFiniScreen
	};

	DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
moveInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("move", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("move");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, moveChangeNotify);

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
moveFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

CompPluginVTable moveVTable = {
	"move",
	moveInit,
	moveFini,
	moveInitObject,
	moveFiniObject
};

CompPluginVTable *
getCompPluginInfo20140724 (void)
{
	return &moveVTable;
}
