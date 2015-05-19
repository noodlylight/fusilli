/*
 *
 * Compiz application switcher plugin
 *
 * staticswitcher.c
 *
 * Copyright : (C) 2014 by Michail Bitzes
 * E-mail    : noodlylight@gmail.com
 *
 * Copyright : (C) 2008 by Danny Baumann
 * E-mail    : maniac@compiz-fusion.org
 *
 * Based on switcher.c:
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
#include <sys/types.h>
#include <unistd.h>
#include <X11/Xatom.h>

#include <fusilli-core.h>
#include <decoration.h>

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _SwitchDisplay {
	int    screenPrivateIndex;
	HandleEventProc handleEvent;

	Window lastActiveWindow;

	Atom selectWinAtom;
	Atom selectFgColorAtom;

	CompKeyBinding next_key, prev_key, next_all_key, prev_all_key;
} SwitchDisplay;

typedef enum {
	CurrentViewport = 0,
	AllViewports,
	Group,
	Panels
} SwitchWindowSelection;

typedef struct _SwitchScreen {
	PreparePaintScreenProc preparePaintScreen;
	DonePaintScreenProc    donePaintScreen;
	PaintOutputProc        paintOutput;
	PaintWindowProc        paintWindow;
	DamageWindowRectProc   damageWindowRect;

	Window            popupWindow;
	CompTimeoutHandle popupDelayHandle;

	CompWindow *selectedWindow;

	Window clientLeader;

	unsigned int previewWidth;
	unsigned int previewHeight;
	unsigned int previewBorder;
	unsigned int xCount;

	int  grabIndex;
	Bool switching;

	int     moreAdjust;
	GLfloat mVelocity;

	CompWindow **windows;
	int        windowsSize;
	int        nWindows;

	float pos;
	float move;

	SwitchWindowSelection selection;

	Bool mouseSelect;

	unsigned int fgColor[4];

	CompMatch window_match;
} SwitchScreen;

#define ICON_SIZE     48
#define MAX_ICON_SIZE 256

#define PREVIEWSIZE   150
#define BORDER        10

#define GET_SWITCH_DISPLAY(d) \
        ((SwitchDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define SWITCH_DISPLAY(d) \
        SwitchDisplay *sd = GET_SWITCH_DISPLAY (d)

#define GET_SWITCH_SCREEN(s, sd) \
        ((SwitchScreen *) (s)->privates[(sd)->screenPrivateIndex].ptr)

#define SWITCH_SCREEN(s) \
        SwitchScreen *ss = GET_SWITCH_SCREEN (s, GET_SWITCH_DISPLAY (&display))

static void
setSelectedWindowHint (CompScreen *s)
{
	Window selectedWindowId = None;

	SWITCH_DISPLAY (&display);
	SWITCH_SCREEN (s);

	if (ss->selectedWindow && !ss->selectedWindow->destroyed)
		selectedWindowId = ss->selectedWindow->id;

	XChangeProperty (display.display, ss->popupWindow, sd->selectWinAtom,
	         XA_WINDOW, 32, PropModeReplace,
	         (unsigned char *) &selectedWindowId, 1);
}

static Bool
isSwitchWin (CompWindow *w)
{
	CompScreen *s = w->screen;

	SWITCH_SCREEN (s);

	if (w->destroyed)
		return FALSE;

	if (!w->mapNum || w->attrib.map_state != IsViewable)
	{
		const BananaValue *
		option_minimized = bananaGetOption (bananaIndex,
		                                    "minimized",
		                                    s->screenNum);

		if (option_minimized->b)
		{
			if (!w->minimized && !w->inShowDesktopMode && !w->shaded)
				return FALSE;
		}
		else
		{
			return FALSE;
		}
	}

	if (!(w->inputHint || (w->protocols & CompWindowProtocolTakeFocusMask)))
		return FALSE;

	if (w->attrib.override_redirect)
		return FALSE;

	if (ss->selection == Panels)
	{
		if (!(w->type & (CompWindowTypeDockMask | CompWindowTypeDesktopMask)))
			return FALSE;
	}
	else
	{
		if (w->wmType & (CompWindowTypeDockMask | CompWindowTypeDesktopMask))
			return FALSE;

		if (w->state & CompWindowStateSkipTaskbarMask)
			return FALSE;

		if (!matchEval (&ss->window_match, w))
			return FALSE;
	}

	if (ss->selection == CurrentViewport)
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
	else if (ss->selection == Group)
	{
		if (ss->clientLeader != w->clientLeader &&
		    ss->clientLeader != w->id)
			return FALSE;
	}

	return TRUE;
}

static void
switchActivateEvent (CompScreen *s,
                     Bool       activating)
{
	BananaArgument arg[2];

	arg[0].type = BananaInt;
	arg[0].name = "root";
	arg[0].value.i = s->root;

	arg[1].type = BananaBool;
	arg[1].name = "active";
	arg[1].value.b = activating;

	(*display.handleFusilliEvent) ("staticswitcher", "activate", arg, 2);
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

static void
switchAddWindowToList (CompScreen *s,
                       CompWindow *w)
{
	SWITCH_SCREEN (s);

	if (ss->windowsSize <= ss->nWindows)
	{
		ss->windows = realloc (ss->windows,
		                       sizeof (CompWindow *) * (ss->nWindows + 32));

		if (!ss->windows)
			return;

		ss->windowsSize = ss->nWindows + 32;
	}

	ss->windows[ss->nWindows++] = w;
}

static void
switchUpdatePopupWindow (CompScreen *s,
                         int        count)
{
	unsigned int winWidth, winHeight;
	unsigned int xCount, yCount;
	float        aspect;
	double       dCount = count;
	unsigned int w = PREVIEWSIZE, h = PREVIEWSIZE, b = BORDER;
	XSizeHints xsh;
	int x, y;

	SWITCH_SCREEN (s);

	/* maximum window size is 2/3 of the current output */
	winWidth  = s->outputDev[s->currentOutputDev].width * 2 / 3;
	winHeight = s->outputDev[s->currentOutputDev].height * 2 / 3;

	if (count <= 4)
	{
		/* don't put 4 or less windows in multiple rows */
		xCount = count;
		yCount = 1;
	}
	else
	{
		aspect = (float) winWidth / winHeight;
		/* round is available in C99 only, so use a replacement for that */
		yCount = floor (sqrt (dCount / aspect) + 0.5f);
		xCount = ceil (dCount / yCount);
	}

	while ((w + b) * xCount > winWidth || (h + b) * yCount > winHeight)
	{
		/* shrink by 10% until all windows fit */
		w = w * 9 / 10;
		h = h * 9 / 10;
		b = b * 9 / 10;
	}

	winWidth = MIN (count, xCount);
	winHeight = (count + xCount - 1) / xCount;

	winWidth = winWidth * w + (winWidth + 1) * b;
	winHeight = winHeight * h + (winHeight + 1) * b;
	ss->xCount = MIN (xCount, count);

	ss->previewWidth = w;
	ss->previewHeight = h;
	ss->previewBorder = b;

	x = s->outputDev[s->currentOutputDev].region.extents.x1 +
	    s->outputDev[s->currentOutputDev].width / 2;
	y = s->outputDev[s->currentOutputDev].region.extents.y1 +
	    s->outputDev[s->currentOutputDev].height / 2;

	xsh.flags       = PSize | PPosition | PWinGravity;
	xsh.x           = x;
	xsh.y           = y;
	xsh.width       = winWidth;
	xsh.height      = winHeight;
	xsh.win_gravity = StaticGravity;

	XSetWMNormalHints (display.display, ss->popupWindow, &xsh);

	XMoveResizeWindow (display.display, ss->popupWindow,
	                   x - winWidth / 2, y - winHeight / 2,
	                   winWidth, winHeight);
}

static void
switchUpdateWindowList (CompScreen *s,
                        int        count)
{
	SWITCH_SCREEN (s);

	ss->pos  = 0.0;
	ss->move = 0.0;

	ss->selectedWindow = ss->windows[0];

	if (ss->popupWindow)
		switchUpdatePopupWindow (s, count);
}

static void
switchCreateWindowList (CompScreen *s,
                        int        count)
{
	CompWindow *w;

	SWITCH_SCREEN (s);

	ss->nWindows = 0;

	for (w = s->windows; w; w = w->next)
	{
		if (isSwitchWin (w))
			switchAddWindowToList (s, w);
	}

	qsort (ss->windows, ss->nWindows, sizeof (CompWindow *), compareWindows);

	switchUpdateWindowList (s, count);
}

static Bool
switchGetPaintRectangle (CompWindow *w,
                         BoxPtr     rect,
                         int        *opacity)
{
	int mode;

	const BananaValue *
	option_highlight_rect_hidden = bananaGetOption (bananaIndex,
	                                                "highlight_rect_hidden",
	                                                w->screen->screenNum);

	mode = option_highlight_rect_hidden->i;

	if (w->attrib.map_state == IsViewable || w->shaded)
	{
		rect->x1 = w->attrib.x - w->input.left;
		rect->y1 = w->attrib.y - w->input.top;
		rect->x2 = w->attrib.x + w->width + w->input.right;
		rect->y2 = w->attrib.y + w->height + w->input.bottom;
		return TRUE;
	}
	else if (mode == 1 && w->iconGeometrySet)
	{
		rect->x1 = w->iconGeometry.x;
		rect->y1 = w->iconGeometry.y;
		rect->x2 = rect->x1 + w->iconGeometry.width;
		rect->y2 = rect->y1 + w->iconGeometry.height;
		return TRUE;
	}
	else if (mode == 2)
	{
		rect->x1 = w->serverX - w->input.left;
		rect->y1 = w->serverY - w->input.top;
		rect->x2 = w->serverX + w->serverWidth + w->input.right;
		rect->y2 = w->serverY + w->serverHeight + w->input.bottom;

		if (opacity)
			*opacity /= 4;

		return TRUE;
	}

	return FALSE;
}

static void
switchDoWindowDamage (CompWindow *w)
{
	if (w->attrib.map_state == IsViewable || w->shaded)
		addWindowDamage (w);
	else
	{
		BoxRec box;
		if (switchGetPaintRectangle (w, &box, NULL))
		{
			REGION reg;

			reg.rects    = &reg.extents;
			reg.numRects = 1;

			reg.extents.x1 = box.x1 - 2;
			reg.extents.y1 = box.y1 - 2;
			reg.extents.x2 = box.x2 + 2;
			reg.extents.y2 = box.y2 + 2;

			damageScreenRegion (w->screen, &reg);
		}
	}
}

static void
switchToWindow (CompScreen *s,
                Bool       toNext)
{
	CompWindow *w;
	int        cur, nextIdx;

	SWITCH_SCREEN (s);

	if (!ss->grabIndex)
		return;

	for (cur = 0; cur < ss->nWindows; cur++)
	{
		if (ss->windows[cur] == ss->selectedWindow)
			break;
	}

	if (cur == ss->nWindows)
		return;

	if (toNext)
		nextIdx = (cur + 1) % ss->nWindows;
	else
		nextIdx = (cur + ss->nWindows - 1) % ss->nWindows;

	w = ss->windows[nextIdx];

	if (w)
	{
		CompWindow *old = ss->selectedWindow;

		const BananaValue *
		option_auto_change_vp = bananaGetOption (bananaIndex,
		                                         "auto_change_vp",
		                                         s->screenNum);

		if (ss->selection == AllViewports && option_auto_change_vp->b)
		{
			XEvent xev;
			int    x, y;

			defaultViewportForWindow (w, &x, &y);

			xev.xclient.type = ClientMessage;
			xev.xclient.display = display.display;
			xev.xclient.format = 32;

			xev.xclient.message_type = display.desktopViewportAtom;
			xev.xclient.window = s->root;

			xev.xclient.data.l[0] = x * s->width;
			xev.xclient.data.l[1] = y * s->height;
			xev.xclient.data.l[2] = 0;
			xev.xclient.data.l[3] = 0;
			xev.xclient.data.l[4] = 0;

			XSendEvent (display.display, s->root, FALSE,
			            SubstructureRedirectMask | SubstructureNotifyMask,
			            &xev);
		}

		ss->selectedWindow = w;
		display.activeWindow = w->id;

		if (old != w)
		{
			ss->move = nextIdx;

			ss->moreAdjust = 1;
		}

		if (ss->popupWindow)
		{
			CompWindow *popup;

			popup = findWindowAtScreen (s, ss->popupWindow);
			if (popup)
				addWindowDamage (popup);

			setSelectedWindowHint (s);
		}

		switchDoWindowDamage (w);

		if (old && !old->destroyed)
			switchDoWindowDamage (old);
	}
}

static int
switchCountWindows (CompScreen *s)
{
	CompWindow *w;
	int        count = 0;

	for (w = s->windows; w; w = w->next)
		if (isSwitchWin (w))
			count++;

	return count;
}

static Visual *
findArgbVisual (Display *dpy, int scr)
{
	XVisualInfo       *xvi;
	XVisualInfo       template;
	int               nvi;
	int               i;
	XRenderPictFormat *format;
	Visual            *visual;

	template.screen = scr;
	template.depth  = 32;
	template.class  = TrueColor;

	xvi = XGetVisualInfo (dpy,
	          VisualScreenMask |
	          VisualDepthMask  |
	          VisualClassMask,
	          &template,
	          &nvi);

	if (!xvi)
		return 0;

	visual = 0;
	for (i = 0; i < nvi; i++)
	{
		format = XRenderFindVisualFormat (dpy, xvi[i].visual);
		if (format->type == PictTypeDirect && format->direct.alphaMask)
		{
			visual = xvi[i].visual;
			break;
		}
	}

	XFree (xvi);

	return visual;
}

static Bool
switchShowPopup (void *closure)
{
	CompScreen *s = (CompScreen *) closure;
	CompWindow *w;

	SWITCH_SCREEN (s);

	w = findWindowAtScreen (s, ss->popupWindow);
	if (w && (w->state & CompWindowStateHiddenMask))
	{
		w->hidden = FALSE;
		showWindow (w);
	}
	else
	{
		XMapWindow (display.display, ss->popupWindow);
	}

	damageScreen (s);

	ss->popupDelayHandle = 0;

	return FALSE;
}

static inline Cursor
switchGetCursor (CompScreen *s,
                 Bool       mouseSelect)
{
	if (mouseSelect)
		return s->normalCursor;

	return s->invisibleCursor;
}

static void
switchInitiate (CompScreen            *s,
                SwitchWindowSelection selection,
                Bool                  showPopup)
{
	CompDisplay *d = &display;
	int         count;
	Bool        mouseSelect;

	SWITCH_SCREEN (s);
	SWITCH_DISPLAY (d);

	if (otherScreenGrabExist (s, "cube", NULL))
		return;

	ss->selection      = selection;
	ss->selectedWindow = NULL;

	count = switchCountWindows (s);
	if (count < 1)
		return;

	if (!ss->popupWindow && showPopup)
	{
		Display              *dpy = display.display;
		XWMHints             xwmh;
		XClassHint           xch;
		Atom                 state[4];
		int                  nState = 0;
		XSetWindowAttributes attr;
		Visual               *visual;

		visual = findArgbVisual (dpy, s->screenNum);
		if (!visual)
			return;

		xwmh.flags = InputHint;
		xwmh.input = 0;

		xch.res_name  = "fusilli";
		xch.res_class = "switcher-window";

		attr.background_pixel = 0;
		attr.border_pixel     = 0;
		attr.colormap         = XCreateColormap (dpy, s->root, visual,
		                        AllocNone);

		ss->popupWindow =
		    XCreateWindow (dpy, s->root, -1, -1, 1, 1, 0,
		    32, InputOutput, visual,
		    CWBackPixel | CWBorderPixel | CWColormap, &attr);

		XSetWMProperties (dpy, ss->popupWindow, NULL, NULL,
		                  programArgv, programArgc,
		                  NULL, &xwmh, &xch);

		state[nState++] = display.winStateAboveAtom;
		state[nState++] = display.winStateStickyAtom;
		state[nState++] = display.winStateSkipTaskbarAtom;
		state[nState++] = display.winStateSkipPagerAtom;

		XChangeProperty (dpy, ss->popupWindow,
		     display.winStateAtom,
		     XA_ATOM, 32, PropModeReplace,
		     (unsigned char *) state, nState);

		XChangeProperty (dpy, ss->popupWindow,
		     display.winTypeAtom,
		     XA_ATOM, 32, PropModeReplace,
		     (unsigned char *) &display.winTypeUtilAtom, 1);

		setWindowProp (ss->popupWindow,
		       display.winDesktopAtom,
		       0xffffffff);

		setSelectedWindowHint (s);
	}

	const BananaValue *
	option_mouse_select = bananaGetOption (bananaIndex,
	                                       "mouse_select",
	                                       s->screenNum);

	mouseSelect = option_mouse_select->b &&
	              selection != Panels && showPopup;

	if (!ss->grabIndex)
		ss->grabIndex = pushScreenGrab (s, switchGetCursor (s, mouseSelect),
		                                "switcher");
	else if (mouseSelect != ss->mouseSelect)
		updateScreenGrab (s, ss->grabIndex, switchGetCursor (s, mouseSelect));

	ss->mouseSelect = mouseSelect;

	if (ss->grabIndex)
	{
		if (!ss->switching)
		{
			switchCreateWindowList (s, count);

			if (ss->popupWindow && showPopup)
			{
				unsigned int delay;

				const BananaValue *
				option_popup_delay = bananaGetOption (bananaIndex,
				                                      "popup_delay",
				                                      s->screenNum);

				delay = option_popup_delay->f * 1000;
				if (delay)
				{
					if (ss->popupDelayHandle)
						compRemoveTimeout (ss->popupDelayHandle);

					ss->popupDelayHandle = compAddTimeout (delay,
					                   (float) delay * 1.2,
					                   switchShowPopup, s);
				}
				else
				{
					switchShowPopup (s);
				}
			}

			sd->lastActiveWindow = d->activeWindow;
			switchActivateEvent (s, TRUE);
		}

		damageScreen (s);

		ss->switching  = TRUE;
		ss->moreAdjust = 1;
	}
}

static Bool
switchTerminate (Window xid,
                 Bool   cancel)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
	{
		SWITCH_SCREEN (s);
		SWITCH_DISPLAY (&display);

		if (xid && s->root != xid)
			continue;

		if (ss->grabIndex)
		{
			CompWindow *w;

			if (ss->popupDelayHandle)
			{
				compRemoveTimeout (ss->popupDelayHandle);
				ss->popupDelayHandle = 0;
			}

			if (ss->popupWindow)
			{
				w = findWindowAtScreen (s, ss->popupWindow);
				if (w && w->managed && w->mapNum)
				{
					w->hidden = TRUE;
					hideWindow (w);
				}
				else
				{
					XUnmapWindow (display.display, ss->popupWindow);
				}
			}

			ss->switching = FALSE;
			display.activeWindow = sd->lastActiveWindow;

			//check if ESC was pressed
			if (cancel)
				ss->selectedWindow = NULL;

			if (ss->selectedWindow && !ss->selectedWindow->destroyed)
				sendWindowActivationRequest (s, ss->selectedWindow->id);

			removeScreenGrab (s, ss->grabIndex, 0);
			ss->grabIndex = 0;

			ss->selectedWindow = NULL;

			switchActivateEvent (s, FALSE);
			setSelectedWindowHint (s);

			damageScreen (s);
		}
	}

	return FALSE;
}

static Bool
switchInitiateCommon (Window                xid,
                      SwitchWindowSelection selection,
                      Bool                  showPopup,
                      Bool                  nextWindow)
{
	CompScreen *s;

	s = findScreenAtDisplay (xid);

	if (s)
	{
		SWITCH_SCREEN (s);

		if (!ss->switching)
			switchInitiate (s, selection, showPopup);

		switchToWindow (s, nextWindow);
	}

	return FALSE;
}

static void
switchWindowRemove (CompWindow  *w)
{
	if (w)
	{
		Bool   inList = FALSE;
		int    count, j, i = 0;
		CompWindow *selected;
		CompWindow *old;
		CompScreen *s = w->screen;

		SWITCH_SCREEN (s);

		if (isSwitchWin (w))
			return;

		old = selected = ss->selectedWindow;

		while (i < ss->nWindows)
		{
			if (ss->windows[i] == w)
			{
				inList = TRUE;

				if (w == selected)
				{
					if (i + 1 < ss->nWindows)
						selected = ss->windows[i + 1];
					else
						selected = ss->windows[0];
				}

				ss->nWindows--;
				for (j = i; j < ss->nWindows; j++)
					ss->windows[j] = ss->windows[j + 1];
			}
			else
			{
				i++;
			}
		}

		if (!inList)
			return;

		count = ss->nWindows;

		if (ss->nWindows == 0)
		{
			switchTerminate (w->screen->root, FALSE);
			return;
		}

		if (!ss->grabIndex)
			return;

		switchUpdateWindowList (w->screen, count);

		for (i = 0; i < ss->nWindows; i++)
		{
			ss->selectedWindow = ss->windows[i];
			ss->move = ss->pos = i;

			if (ss->selectedWindow == selected)
				break;
		}

		if (ss->popupWindow)
		{
			CompWindow *popup;

			popup = findWindowAtScreen (w->screen, ss->popupWindow);
			if (popup)
				addWindowDamage (popup);

			setSelectedWindowHint (w->screen);
		}

		if (old != ss->selectedWindow)
		{
			switchDoWindowDamage (ss->selectedWindow);
			switchDoWindowDamage (w);

			if (old && !old->destroyed)
				switchDoWindowDamage (old);

			ss->moreAdjust = 1;
		}
	}
}

static void
updateForegroundColor (CompScreen *s)
{
	Atom          actual;
	int           result, format;
	unsigned long n, left;
	unsigned char *propData;

	SWITCH_SCREEN (s);
	SWITCH_DISPLAY (&display);

	if (!ss->popupWindow)
		return;

	result = XGetWindowProperty (display.display, ss->popupWindow,
	             sd->selectFgColorAtom, 0L, 4L, FALSE,
	             XA_INTEGER, &actual, &format,
	             &n, &left, &propData);

	if (result == Success && propData)
	{
		if (n == 3 || n == 4)
		{
			long *data = (long *) propData;

			ss->fgColor[0] = MIN (0xffff, data[0]);
			ss->fgColor[1] = MIN (0xffff, data[1]);
			ss->fgColor[2] = MIN (0xffff, data[2]);

			if (n == 4)
				ss->fgColor[3] = MIN (0xffff, data[3]);
		}

		XFree (propData);
	}
	else
	{
		ss->fgColor[0] = 0;
		ss->fgColor[1] = 0;
		ss->fgColor[2] = 0;
		ss->fgColor[3] = 0xffff;
	}
}

static inline int
switchGetRowXOffset (CompScreen   *s,
                     SwitchScreen *ss,
                     int          y)
{
	int retval = 0;

	if (ss->nWindows - (y * ss->xCount) >= ss->xCount)
		return 0;

	const BananaValue *
	option_row_align = bananaGetOption (bananaIndex,
	                                    "row_align",
	                                    s->screenNum);

	switch (option_row_align->i) {
	case 0: //Left
		break;
	case 1: //Centered
		retval = (ss->xCount - ss->nWindows + (y * ss->xCount)) *
		         (ss->previewWidth + ss->previewBorder) / 2;
		break;
	case 2: //Right
		retval = (ss->xCount - ss->nWindows + (y * ss->xCount)) *
		         (ss->previewWidth + ss->previewBorder);
		break;
	}

	return retval;
}

static void
switchGetWindowPosition (CompScreen   *s,
                         unsigned int index,
                         int          *x,
                         int          *y)
{
	int row, column;

	SWITCH_SCREEN (s);

	if (index >= ss->nWindows)
		return;

	column = index % ss->xCount;
	row    = index / ss->xCount;

	*x = column * ss->previewWidth + (column + 1) * ss->previewBorder;
	*x += switchGetRowXOffset (s, ss, row);

	*y = row * ss->previewHeight + (row + 1) * ss->previewBorder;
}

static CompWindow *
switchFindWindowAt (CompScreen *s,
                    int        x,
                    int        y)
{
	CompWindow *popup;

	SWITCH_SCREEN (s);

	popup = findWindowAtScreen (s, ss->popupWindow);
	if (popup)
	{
		int   i;

		for (i = 0; i < ss->nWindows; i++)
		{
			int x1, x2, y1, y2;

			switchGetWindowPosition (s, i, &x1, &y1);

			x1 += popup->attrib.x;
			y1 += popup->attrib.y;

			x2 = x1 + ss->previewWidth;
			y2 = y1 + ss->previewHeight;

			if (x >= x1 && x < x2 && y >= y1 && y < y2)
				return ss->windows[i];
		}
	}

	return NULL;
}

static void
switchHandleEvent (XEvent *event)
{
	CompWindow *w = NULL;
	CompScreen *s;

	SWITCH_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &sd->next_key))
		{
			const BananaValue *
			option_show_popup = bananaGetOption (bananaIndex,
			                                     "show_popup",
			                                     -1);

			switchInitiateCommon (event->xkey.root, CurrentViewport,
			                      option_show_popup->b, TRUE);
		}
		else if (isKeyPressEvent (event, &sd->prev_key))
		{
			const BananaValue *
			option_show_popup = bananaGetOption (bananaIndex, "show_popup", -1);

			switchInitiateCommon (event->xkey.root, CurrentViewport,
			                      option_show_popup->b, FALSE);
		}
		else if (isKeyPressEvent (event, &sd->next_all_key))
		{
			const BananaValue *
			option_show_popup = bananaGetOption (bananaIndex, "show_popup", -1);


			switchInitiateCommon (event->xkey.root, AllViewports,
			                      option_show_popup->b, TRUE);
		}
		else if (isKeyPressEvent (event, &sd->prev_all_key))
		{
			const BananaValue *
			option_show_popup = bananaGetOption (bananaIndex, "show_popup", -1);

			switchInitiateCommon (event->xkey.root, AllViewports,
			                      option_show_popup->b, FALSE);
		}
		if (event->xkey.keycode == display.escapeKeyCode)
		{
			switchTerminate (w->screen->root, TRUE);
		}
		break;
	case MapNotify:
		w = findWindowAtDisplay (event->xmap.window);
		if (w)
		{
			SWITCH_SCREEN (w->screen);

			if (w->id == ss->popupWindow)
			{
				/* we don't get a MapRequest for internal window creations,
				   so we need to set the internals ourselves */
				w->managed = TRUE;
				w->wmType = getWindowType (w->id);
				recalcWindowType (w);
				recalcWindowActions (w);
				updateWindowClassHints (w);
			}
		}
		break;
	case DestroyNotify:
		/* We need to get the CompWindow * for event->xdestroywindow.window
		   here because in the (*d->handleEvent) call below, that CompWindow's
		   id will become 1, so findWindowAtDisplay won't be able to find the
		   CompWindow after that. */
		w = findWindowAtDisplay (event->xdestroywindow.window);
		break;
	default:
		if (event->type == display.xkbEvent)
		{
			XkbAnyEvent *xkbEvent = (XkbAnyEvent *) event;

			if (xkbEvent->xkb_type == XkbStateNotify)
			{
				XkbStateNotifyEvent *stateEvent = (XkbStateNotifyEvent *) event;
				if (stateEvent->event_type == KeyRelease)
				{
					//unsigned int modMask = REAL_MOD_MASK & ~d->ignoredModMask;
					//unsigned int bindMods = virtualToRealModMask (d, next_key.modifiers);
					//if ((stateEvent->mods & modMask & bindMods) != bindMods)
						switchTerminate (0, FALSE);

				}
			}
		}
		break;
	}

	UNWRAP (sd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (sd, &display, handleEvent, switchHandleEvent);

	switch (event->type) {
	case UnmapNotify:
		w = findWindowAtDisplay (event->xunmap.window);
		switchWindowRemove (w);
		break;
	case DestroyNotify:
		switchWindowRemove (w);
		break;
	case PropertyNotify:
		if (event->xproperty.atom == sd->selectFgColorAtom)
		{
			w = findWindowAtDisplay (event->xproperty.window);
			if (w)
			{
				SWITCH_SCREEN (w->screen);

			if (event->xproperty.window == ss->popupWindow)
				updateForegroundColor (w->screen);
			}
		}
		break;
	case ButtonPress:
		s = findScreenAtDisplay (event->xbutton.root);
		if (s)
		{
			SWITCH_SCREEN (s);

			if (ss->grabIndex && ss->mouseSelect)
			{
				CompWindow *selected;

				selected = switchFindWindowAt (s,
				                               event->xbutton.x_root,
				                               event->xbutton.y_root);

				if (selected)
				{
					ss->selectedWindow = selected;

					switchTerminate (s->root, FALSE);
				}
			}
		}
		break;
	default:
		break;
	}
}

static int
adjustSwitchVelocity (CompScreen *s)
{
	float dx, adjust, amount;

	SWITCH_SCREEN (s);

	dx = ss->move - ss->pos;
	if (abs (dx) > abs (dx + ss->nWindows))
		dx += ss->nWindows;
	if (abs (dx) > abs (dx - ss->nWindows))
		dx -= ss->nWindows;

	adjust = dx * 0.15f;
	amount = fabs (dx) * 1.5f;
	if (amount < 0.2f)
		amount = 0.2f;
	else if (amount > 2.0f)
		amount = 2.0f;

	ss->mVelocity = (amount * ss->mVelocity + adjust) / (amount + 1.0f);

	if (fabs (dx) < 0.001f && fabs (ss->mVelocity) < 0.001f)
	{
		ss->mVelocity = 0.0f;
		return 0;
	}

	return 1;
}

static void
switchPreparePaintScreen (CompScreen *s,
                          int        msSinceLastPaint)
{
	SWITCH_SCREEN (s);

	if (ss->moreAdjust)
	{
		int   steps;
		float amount, chunk;

		const BananaValue *
		option_speed = bananaGetOption (bananaIndex,
		                                "speed",
		                                s->screenNum);

		const BananaValue *
		option_timestep = bananaGetOption (bananaIndex,
		                                   "timestep",
		                                   s->screenNum);

		amount = msSinceLastPaint * 0.05f * option_speed->f;
		steps  = amount / (0.5f * option_timestep->f);
		if (!steps)
			steps = 1;
		chunk  = amount / (float) steps;

		while (steps--)
		{
			ss->moreAdjust = adjustSwitchVelocity (s);
			if (!ss->moreAdjust)
			{
				ss->pos = ss->move;
				break;
			}

			ss->pos += ss->mVelocity * chunk;
			ss->pos = fmod (ss->pos, ss->nWindows);
			if (ss->pos < 0.0)
				ss->pos += ss->nWindows;
		}
	}

	UNWRAP (ss, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, msSinceLastPaint);
	WRAP (ss, s, preparePaintScreen, switchPreparePaintScreen);
}

static inline void
switchPaintRect (BoxRec *box,
                 unsigned int offset,
                 unsigned short *color,
                 int opacity)
{
	glColor4us (color[0], color[1], color[2], color[3] * opacity / 100);
	glBegin (GL_LINE_LOOP);
	glVertex2i (box->x1 + offset, box->y1 + offset);
	glVertex2i (box->x2 - offset, box->y1 + offset);
	glVertex2i (box->x2 - offset, box->y2 - offset);
	glVertex2i (box->x1 + offset, box->y2 - offset);
	glEnd ();
}

static Bool
switchPaintOutput (CompScreen              *s,
                   const ScreenPaintAttrib *sAttrib,
                   const CompTransform     *transform,
                   Region                  region,
                   CompOutput              *output,
                   unsigned int            mask)
{
	Bool status;

	SWITCH_SCREEN (s);

	if (ss->grabIndex)
	{
		int                             mode;
		CompWindow                      *switcher, *zoomed;
		Window                          zoomedAbove = None;
		Bool                            saveDestroyed = FALSE;

		switcher = findWindowAtScreen (s, ss->popupWindow);
		if (switcher)
		{
			saveDestroyed = switcher->destroyed;
			switcher->destroyed = TRUE;
		}

		if (!ss->popupDelayHandle)
		{
			const BananaValue *
			option_highlight_mode = bananaGetOption (bananaIndex,
			                                         "highlight_mode",
			                                         s->screenNum);

			mode = option_highlight_mode->i;
		}
		else
			mode = 0; //highlight mode: None

		if (mode == 1) //highlight mode: bring selected to front
		{
			zoomed = ss->selectedWindow;
			if (zoomed && !zoomed->destroyed)
			{
				CompWindow *w;

				for (w = zoomed->prev; w && w->id <= 1; w = w->prev)
				     ;
				zoomedAbove = (w) ? w->id : None;

				unhookWindowFromScreen (s, zoomed);
				insertWindowIntoScreen (s, zoomed, s->reverseWindows->id);
			}
		}
		else
		{
			zoomed = NULL;
		}

		UNWRAP (ss, s, paintOutput);
		status = (*s->paintOutput) (s, sAttrib, transform,
		                            region, output, mask);
		WRAP (ss, s, paintOutput, switchPaintOutput);

		if (zoomed)
		{
			unhookWindowFromScreen (s, zoomed);
			insertWindowIntoScreen (s, zoomed, zoomedAbove);
		}

		if (switcher || mode == 2) //high light mode: show rectangle
		{
			CompTransform sTransform = *transform;

			transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

			glPushMatrix ();
			glLoadMatrixf (sTransform.m);

			if (mode == 2) //high light mode show rectangle
			{
				CompWindow *w;

				if (zoomed)
					w = zoomed;
				else
					w = ss->selectedWindow;

				if (w)
				{
					BoxRec box;
					int    opacity = 100;

					if (switchGetPaintRectangle (w, &box, &opacity))
					{
						unsigned short color[] = { 0, 0, 0, 0 };
						GLushort       r, g, b, a;

						glEnable (GL_BLEND);

						/* fill rectangle */
						const BananaValue *
						option_highlight_color = bananaGetOption (bananaIndex,
						                                  "highlight_color",
						                                  s->screenNum);

						stringToColor (option_highlight_color->s, color);

						r = color[0];
						g = color[1];
						b = color[2];
						a = color[3];
						a = a * opacity / 100;

						glColor4us (r, g, b, a);
						glRecti (box.x1, box.y2, box.x2, box.y1);

						/* draw outline */
						glLineWidth (1.0);
						glDisable (GL_LINE_SMOOTH);

						const BananaValue *
						option_highlight_border_color = bananaGetOption (
						                               bananaIndex,
						                              "highlight_border_color",
						                              s->screenNum);

						stringToColor (option_highlight_border_color->s, color);

						switchPaintRect (&box, 0, color, opacity);
						switchPaintRect (&box, 2, color, opacity);

						const BananaValue *
						option_highlight_border_inlay_color = bananaGetOption (
						                        bananaIndex,
						                        "highlight_border_inlay_color",
						                         s->screenNum);

						stringToColor (option_highlight_border_inlay_color->s, 
						               color);

						switchPaintRect (&box, 1, color, opacity);

						/* clean up */
						glColor4usv (defaultColor);
						glDisable (GL_BLEND);
					}
				}
			}

			if (switcher)
			{
				switcher->destroyed = saveDestroyed;

				if (!switcher->destroyed                     &&
				    switcher->attrib.map_state == IsViewable &&
				    switcher->damaged)
				{
					(*s->paintWindow) (switcher, &switcher->paint, &sTransform,
					       &infiniteRegion, 0);
				}
			}

			glPopMatrix ();
		}
	}
	else
	{
		UNWRAP (ss, s, paintOutput);
		status = (*s->paintOutput) (s, sAttrib, transform, region, output,
		                            mask);
		WRAP (ss, s, paintOutput, switchPaintOutput);
	}

	return status;
}

static void
switchDonePaintScreen (CompScreen *s)
{
	SWITCH_SCREEN (s);

	if (ss->grabIndex && ss->moreAdjust)
	{
		CompWindow *w;

		w = findWindowAtScreen (s, ss->popupWindow);
		if (w)
			addWindowDamage (w);
	}

	UNWRAP (ss, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP (ss, s, donePaintScreen, switchDonePaintScreen);
}

static void
switchPaintThumb (CompWindow              *w,
                  const WindowPaintAttrib *attrib,
                  const CompTransform     *transform,
                  unsigned int             mask,
                  int                      x,
                  int                      y)
{
	CompScreen *s = w->screen;
	WindowPaintAttrib sAttrib = *attrib;
	int		      wx, wy;
	float	      width, height;
	CompIcon	      *icon = NULL;

	SWITCH_SCREEN (s);

	mask |= PAINT_WINDOW_TRANSFORMED_MASK;

	if (w->mapNum)
	{
		if (!w->texture->pixmap && !w->bindFailed)
			bindWindow (w);
	}

	if (w->texture->pixmap)
	{
		AddWindowGeometryProc oldAddWindowGeometry;
		FragmentAttrib	      fragment;
		CompTransform	      wTransform = *transform;
		int		      ww, wh;
		GLenum                filter;

		width  = ss->previewWidth;
		height = ss->previewHeight;

		ww = w->width  + w->input.left + w->input.right;
		wh = w->height + w->input.top  + w->input.bottom;

		if (ww > width)
			sAttrib.xScale = width / ww;
		else
			sAttrib.xScale = 1.0f;

		if (wh > height)
			sAttrib.yScale = height / wh;
		else
			sAttrib.yScale = 1.0f;

		if (sAttrib.xScale < sAttrib.yScale)
			sAttrib.yScale = sAttrib.xScale;
		else
			sAttrib.xScale = sAttrib.yScale;

		width  = ww * sAttrib.xScale;
		height = wh * sAttrib.yScale;

		wx = x + (ss->previewWidth / 2) - (width / 2);
		wy = y + (ss->previewHeight / 2) - (height / 2);

#if 0
	if (w != ss->selectedWindow)
	{
	    sAttrib.brightness /= 2;
	    sAttrib.saturation /= 2;
	}
#endif

		sAttrib.xTranslate = wx - w->attrib.x + w->input.left * sAttrib.xScale;
		sAttrib.yTranslate = wy - w->attrib.y + w->input.top  * sAttrib.yScale;

		initFragmentAttrib (&fragment, &sAttrib);

		if (w->alpha || fragment.opacity != OPAQUE)
			mask |= PAINT_WINDOW_TRANSLUCENT_MASK;

		matrixTranslate (&wTransform, w->attrib.x, w->attrib.y, 0.0f);
		matrixScale (&wTransform, sAttrib.xScale, sAttrib.yScale, 1.0f);
		matrixTranslate (&wTransform,
		         sAttrib.xTranslate / sAttrib.xScale - w->attrib.x,
		         sAttrib.yTranslate / sAttrib.yScale - w->attrib.y,
		         0.0f);

		glPushMatrix ();
		glLoadMatrixf (wTransform.m);

		filter = display.textureFilter;

		const BananaValue *
		option_mipmap = bananaGetOption (bananaIndex,
		                                 "mipmap",
		                                 s->screenNum);

		if (option_mipmap->b)
			display.textureFilter = GL_LINEAR_MIPMAP_LINEAR;

		/* XXX: replacing the addWindowGeometry function like this is
		   very ugly but necessary until the vertex stage has been made
		   fully pluggable. */
		oldAddWindowGeometry = w->screen->addWindowGeometry;
		w->screen->addWindowGeometry = addWindowGeometry;
		(w->screen->drawWindow) (w, &wTransform, &fragment, &infiniteRegion,
		                         mask);
		w->screen->addWindowGeometry = oldAddWindowGeometry;

		display.textureFilter = filter;

		glPopMatrix ();

		const BananaValue *
		option_icon = bananaGetOption (bananaIndex,
		                               "icon",
		                               s->screenNum);

		if (option_icon->b)
		{
			icon = getWindowIcon (w, MAX_ICON_SIZE, MAX_ICON_SIZE);
			if (icon)
			{
				float xScale, yScale;

				xScale = (float) ICON_SIZE / icon->width;
				yScale = (float) ICON_SIZE / icon->height;

				if (xScale < yScale)
					yScale = xScale;
				else
					xScale = yScale;

				sAttrib.xScale = (float) ss->previewWidth * xScale / PREVIEWSIZE;
				sAttrib.yScale = (float) ss->previewWidth * yScale / PREVIEWSIZE;

				wx = x + ss->previewWidth - (sAttrib.xScale * icon->width);
				wy = y + ss->previewHeight - (sAttrib.yScale * icon->height);
			}
		}
	}
	else
	{
		width  = ss->previewWidth * 3 / 4;
		height = ss->previewHeight * 3 / 4;

		/* try to get the largest icon */
		icon = getWindowIcon (w, MAX_ICON_SIZE, MAX_ICON_SIZE);
		if (!icon)
			icon = w->screen->defaultIcon;

		if (icon)
		{
			sAttrib.xScale = (width / icon->width);
			sAttrib.yScale = (height / icon->height);

			if (sAttrib.xScale < sAttrib.yScale)
				sAttrib.yScale = sAttrib.xScale;
			else
				sAttrib.xScale = sAttrib.yScale;

			width  = icon->width  * sAttrib.xScale;
			height = icon->height * sAttrib.yScale;

			wx = x + (ss->previewWidth / 2) - (width / 2);
			wy = y + (ss->previewHeight / 2) - (height / 2);
		}
	}

	if (icon && (icon->texture.name || iconToTexture (w->screen, icon)))
	{
		REGION     iconReg;
		CompMatrix matrix;

		mask |= PAINT_WINDOW_BLEND_MASK;

		iconReg.rects    = &iconReg.extents;
		iconReg.numRects = 1;

		iconReg.extents.x1 = w->attrib.x;
		iconReg.extents.y1 = w->attrib.y;
		iconReg.extents.x2 = w->attrib.x + icon->width;
		iconReg.extents.y2 = w->attrib.y + icon->height;

		matrix = icon->texture.matrix;
		matrix.x0 -= (w->attrib.x * icon->texture.matrix.xx);
		matrix.y0 -= (w->attrib.y * icon->texture.matrix.yy);

		sAttrib.xTranslate = wx - w->attrib.x;
		sAttrib.yTranslate = wy - w->attrib.y;

		w->vCount = w->indexCount = 0;
		addWindowGeometry (w, &matrix, 1, &iconReg, &infiniteRegion);

		if (w->vCount)
		{
			FragmentAttrib fragment;
			CompTransform  wTransform = *transform;

			initFragmentAttrib (&fragment, &sAttrib);

			matrixTranslate (&wTransform, w->attrib.x, w->attrib.y, 0.0f);
			matrixScale (&wTransform, sAttrib.xScale, sAttrib.yScale, 1.0f);
			matrixTranslate (&wTransform,
			         sAttrib.xTranslate / sAttrib.xScale - w->attrib.x,
			         sAttrib.yTranslate / sAttrib.yScale - w->attrib.y,
			         0.0f);

			glPushMatrix ();
			glLoadMatrixf (wTransform.m);

			(*w->screen->drawWindowTexture) (w,
			                 &icon->texture, &fragment,
			                 mask);
			glPopMatrix ();
		}
	}
}

static void
switchPaintSelectionRect (SwitchScreen *ss,
                          int          x,
                          int          y,
                          float        dx,
                          float        dy,
                          unsigned int opacity)
{
	int            i;
	float          color[4], op;
	unsigned int   w, h;

	w = ss->previewWidth + ss->previewBorder;
	h = ss->previewHeight + ss->previewBorder;

	glEnable (GL_BLEND);

	if (dx > ss->xCount - 1)
		op = 1.0 - MIN (1.0, dx - (ss->xCount - 1));
	else if (dx + (dy * ss->xCount) > ss->nWindows - 1)
		op = 1.0 - MIN (1.0, dx - (ss->nWindows - 1 - (dy * ss->xCount)));
	else if (dx < 0.0)
		op = 1.0 + MAX (-1.0, dx);
	else
		op = 1.0;

	for (i = 0; i < 4; i++)
		color[i] = (float)ss->fgColor[i] * opacity * op / 0xffffffff;

	glColor4fv (color);
	glPushMatrix ();
	glTranslatef (x + ss->previewBorder / 2 + (dx * w),
	              y + ss->previewBorder / 2 + (dy * h), 0.0f);

	glBegin (GL_QUADS);
	glVertex2i (-1, -1);
	glVertex2i (-1, 1);
	glVertex2i (w + 1, 1);
	glVertex2i (w + 1, -1);
	glVertex2i (-1, h - 1);
	glVertex2i (-1, h + 1);
	glVertex2i (w + 1, h + 1);
	glVertex2i (w + 1, h - 1);
	glVertex2i (-1, 1);
	glVertex2i (-1, h - 1);
	glVertex2i (1, h - 1);
	glVertex2i (1, 1);
	glVertex2i (w - 1, 1);
	glVertex2i (w - 1, h - 1);
	glVertex2i (w + 1, h - 1);
	glVertex2i (w + 1, 1);
	glEnd ();

	glPopMatrix ();
	glColor4usv (defaultColor);
	glDisable (GL_BLEND);
}

static Bool
switchPaintWindow (CompWindow              *w,
                   const WindowPaintAttrib *attrib,
                   const CompTransform     *transform,
                   Region                  region,
                   unsigned int            mask)
{
	CompScreen *s = w->screen;
	Bool       status;

	SWITCH_SCREEN (s);

	if (w->id == ss->popupWindow)
	{
		int            x = 0, y = 0, offX, i;
		float          px, py, pos;

		if (mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK)
			return FALSE;

		UNWRAP (ss, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP (ss, s, paintWindow, switchPaintWindow);

		if (!(mask & PAINT_WINDOW_TRANSFORMED_MASK) && region->numRects == 0)
			return TRUE;

		glPushAttrib (GL_SCISSOR_BIT);

		glEnable (GL_SCISSOR_TEST);
		glScissor (w->attrib.x, 0, w->width, w->screen->height);

		for (i = 0; i < ss->nWindows; i++)
		{
			switchGetWindowPosition (s, i, &x, &y);
			switchPaintThumb (ss->windows[i], &w->lastPaint, transform,
			      mask, x + w->attrib.x, y + w->attrib.y);
		}

		pos = fmod (ss->pos, ss->nWindows);
		px  = fmod (pos, ss->xCount);
		py  = floor (pos / ss->xCount);

		offX = switchGetRowXOffset (s, ss, py);

		if (pos > ss->nWindows - 1)
		{
			px = fmod (pos - ss->nWindows, ss->xCount);
			switchPaintSelectionRect (ss, w->attrib.x, w->attrib.y, px, 0.0,
			                          w->lastPaint.opacity);

			px = fmod (pos, ss->xCount);
			switchPaintSelectionRect (ss, w->attrib.x + offX, w->attrib.y,
			                          px, py, w->lastPaint.opacity);
		}

		if (px > ss->xCount - 1)
		{
			switchPaintSelectionRect (ss, w->attrib.x, w->attrib.y, px, py,
			                          w->lastPaint.opacity);

			py = fmod (py + 1, ceil ((double) ss->nWindows / ss->xCount));
			offX = switchGetRowXOffset (s, ss, py);

			switchPaintSelectionRect (ss, w->attrib.x + offX, w->attrib.y,
			                          px - ss->xCount, py,
			                          w->lastPaint.opacity);
		}
		else
		{
			switchPaintSelectionRect (ss, w->attrib.x + offX, w->attrib.y,
			                          px, py, w->lastPaint.opacity);
		}

		glDisable (GL_SCISSOR_TEST);
		glPopAttrib ();
	}
	else if (ss->switching && !ss->popupDelayHandle &&
	     (w != ss->selectedWindow))
	{
		WindowPaintAttrib sAttrib = *attrib;
		GLuint            value;

		const BananaValue *
		option_saturation = bananaGetOption (bananaIndex,
		                                     "saturation",
		                                     s->screenNum);

		value = option_saturation->i;
		if (value != 100)
			sAttrib.saturation = sAttrib.saturation * value / 100;

		const BananaValue *
		option_brightness = bananaGetOption (bananaIndex,
		                                     "brightness",
		                                     s->screenNum);

		value = option_brightness->i;
		if (value != 100)
			sAttrib.brightness = sAttrib.brightness * value / 100;

		if (w->wmType & ~(CompWindowTypeDockMask | CompWindowTypeDesktopMask))
		{
			const BananaValue *
			option_opacity = bananaGetOption (bananaIndex,
			                                  "opacity",
			                                  s->screenNum);

			value = option_opacity->i;
			if (value != 100)
				sAttrib.opacity = sAttrib.opacity * value / 100;
		}

		UNWRAP (ss, s, paintWindow);
		status = (*s->paintWindow) (w, &sAttrib, transform, region, mask);
		WRAP (ss, s, paintWindow, switchPaintWindow);
	}
	else
	{
		UNWRAP (ss, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP (ss, s, paintWindow, switchPaintWindow);
	}

	return status;
}

static Bool
switchDamageWindowRect (CompWindow *w,
                        Bool       initial,
                        BoxPtr     rect)
{
	CompScreen *s = w->screen;
	Bool status;

	SWITCH_SCREEN (s);

	if (ss->grabIndex)
	{
		CompWindow *popup;
		int        i;

		for (i = 0; i < ss->nWindows; i++)
		{
			if (ss->windows[i] == w)
			{
				popup = findWindowAtScreen (s, ss->popupWindow);
				if (popup)
					addWindowDamage (popup);

				break;
			}
		}
	}

	UNWRAP (ss, s, damageWindowRect);
	status = (*s->damageWindowRect) (w, initial, rect);
	WRAP (ss, s, damageWindowRect, switchDamageWindowRect);

	return status;
}

static Bool
switchInitDisplay (CompPlugin  *p,
                   CompDisplay *d)
{
	SwitchDisplay *sd;

	sd = malloc (sizeof (SwitchDisplay));
	if (!sd)
		return FALSE;

	sd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (sd->screenPrivateIndex < 0)
	{
		free (sd);
		return FALSE;
	}

	sd->selectWinAtom     = XInternAtom (display.display,
	                                     DECOR_SWITCH_WINDOW_ATOM_NAME, 0);
	sd->selectFgColorAtom =
	     XInternAtom (display.display,
	                  DECOR_SWITCH_FOREGROUND_COLOR_ATOM_NAME, 0);

	sd->lastActiveWindow = None;

	const BananaValue *
	option_next_key = bananaGetOption (bananaIndex, "next_key", -1);

	const BananaValue *
	option_prev_key = bananaGetOption (bananaIndex, "prev_key", -1);

	const BananaValue *
	option_next_all_key = bananaGetOption (bananaIndex, "next_all_key", -1);

	const BananaValue *
	option_prev_all_key = bananaGetOption (bananaIndex, "prev_all_key", -1);

	registerKey (option_next_key->s, &sd->next_key);
	registerKey (option_prev_key->s, &sd->prev_key);
	registerKey (option_next_all_key->s, &sd->next_all_key);
	registerKey (option_prev_all_key->s, &sd->prev_all_key);

	WRAP (sd, d, handleEvent, switchHandleEvent);

	d->privates[displayPrivateIndex].ptr = sd;

	return TRUE;
}

static void
switchFiniDisplay (CompPlugin  *p,
                   CompDisplay *d)
{
	SWITCH_DISPLAY (d);

	freeScreenPrivateIndex (sd->screenPrivateIndex);

	UNWRAP (sd, d, handleEvent);

	free (sd);
}

static Bool
switchInitScreen (CompPlugin *p,
                  CompScreen *s)
{
	SwitchScreen *ss;

	SWITCH_DISPLAY (&display);

	ss = malloc (sizeof (SwitchScreen));
	if (!ss)
		return FALSE;

	ss->popupWindow      = None;
	ss->popupDelayHandle = 0;

	ss->selectedWindow = NULL;
	ss->clientLeader   = None;

	ss->windows     = 0;
	ss->nWindows    = 0;
	ss->windowsSize = 0;

	ss->pos  = 0;
	ss->move = 0;

	ss->switching = FALSE;
	ss->grabIndex = 0;

	ss->moreAdjust = 0;
	ss->mVelocity  = 0.0f;

	ss->selection   = CurrentViewport;
	ss->mouseSelect = FALSE;

	ss->fgColor[0] = 0;
	ss->fgColor[1] = 0;
	ss->fgColor[2] = 0;
	ss->fgColor[3] = 0xffff;

	const BananaValue *
	option_window_match = bananaGetOption (bananaIndex,
	                                       "window_match",
	                                       s->screenNum);

	matchInit (&ss->window_match);
	matchAddFromString (&ss->window_match, option_window_match->s);
	matchUpdate (&ss->window_match);

	WRAP (ss, s, preparePaintScreen, switchPreparePaintScreen);
	WRAP (ss, s, donePaintScreen, switchDonePaintScreen);
	WRAP (ss, s, paintOutput, switchPaintOutput);
	WRAP (ss, s, paintWindow, switchPaintWindow);
	WRAP (ss, s, damageWindowRect, switchDamageWindowRect);

	s->privates[sd->screenPrivateIndex].ptr = ss;

	return TRUE;
}

static void
switchFiniScreen (CompPlugin *p,
                  CompScreen *s)
{
	SWITCH_SCREEN (s);

	UNWRAP (ss, s, preparePaintScreen);
	UNWRAP (ss, s, donePaintScreen);
	UNWRAP (ss, s, paintOutput);
	UNWRAP (ss, s, paintWindow);
	UNWRAP (ss, s, damageWindowRect);

	if (ss->popupDelayHandle)
		compRemoveTimeout (ss->popupDelayHandle);

	if (ss->popupWindow)
		XDestroyWindow (display.display, ss->popupWindow);

	if (ss->windows)
		free (ss->windows);

	matchFini (&ss->window_match);
	free (ss);
}

static void
switchChangeNotify (const char        *optionName,
                    BananaType        optionType,
                    const BananaValue *optionValue,
                    int               screenNum)
{
	SWITCH_DISPLAY (&display);

	if (strcasecmp (optionName, "window_match") == 0)
	{
		CompScreen *screen;

		screen = getScreenFromScreenNum (screenNum);

		SWITCH_SCREEN (screen);

		matchFini (&ss->window_match);
		matchInit (&ss->window_match);
		matchAddFromString (&ss->window_match, optionValue->s);
		matchUpdate (&ss->window_match);
	}
	else if (strcasecmp (optionName, "next_key") == 0)
		updateKey (optionValue->s, &sd->next_key);

	else if (strcasecmp (optionName, "prev_key") == 0)
		updateKey (optionValue->s, &sd->prev_key);

	else if (strcasecmp (optionName, "next_all_key") == 0)
		updateKey (optionValue->s, &sd->next_all_key);

	else if (strcasecmp (optionName, "prev_all_key") == 0)
		updateKey (optionValue->s, &sd->prev_all_key);

}

static Bool
switchInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("staticswitcher", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("staticswitcher");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, switchChangeNotify);

	return TRUE;
}

static void
switchFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable switchVTable = {
	"staticswitcher",
	switchInit,
	switchFini,
	switchInitDisplay,
	switchFiniDisplay,
	switchInitScreen,
	switchFiniScreen,
	NULL, /* switchInitWindow */
	NULL  /* switchFiniWindow */
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &switchVTable;
}