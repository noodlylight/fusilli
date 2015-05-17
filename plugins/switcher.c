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
#include <math.h>
#include <sys/types.h>
#include <unistd.h>

#include <fusilli-core.h>

#include <decoration.h>

#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>

#define ZOOMED_WINDOW_MASK (1 << 0)
#define NORMAL_WINDOW_MASK (1 << 1)

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _SwitchDisplay {
	int             screenPrivateIndex;
	HandleEventProc handleEvent;

	Atom selectWinAtom;
	Atom selectFgColorAtom;

	CompKeyBinding next_key, prev_key, next_all_key, prev_all_key;
} SwitchDisplay;

typedef enum {
	CurrentViewport = 0,
	AllViewports
} SwitchWindowSelection;

typedef struct _SwitchScreen {
	PreparePaintScreenProc preparePaintScreen;
	DonePaintScreenProc    donePaintScreen;
	PaintOutputProc        paintOutput;
	PaintWindowProc        paintWindow;
	DamageWindowRectProc   damageWindowRect;

	Window popupWindow;

	CompWindow     *selectedWindow;
	CompWindow     *zoomedWindow;
	unsigned int lastActiveNum;

	float zoom;

	int grabIndex;

	Bool switching;
	Bool zooming;
	int  zoomMask;

	int moreAdjust;

	GLfloat mVelocity;
	GLfloat tVelocity;
	GLfloat sVelocity;

	CompWindow **windows;
	int        windowsSize;
	int        nWindows;

	int pos;
	int move;

	float translate;
	float sTranslate;

	SwitchWindowSelection selection;

	unsigned int fgColor[4];

	CompMatch window_match;
} SwitchScreen;

#define MwmHintsDecorations (1L << 1)

typedef struct {
	unsigned long flags;
	unsigned long functions;
	unsigned long decorations;
} MwmHints;

#define WIDTH  212
#define HEIGHT 192
#define SPACE  10

#define SWITCH_ZOOM 0.1f

#define BOX_WIDTH 3

#define ICON_SIZE 48
#define MAX_ICON_SIZE 256

static float _boxVertices[] =
{
	-(WIDTH >> 1), 0,
	-(WIDTH >> 1), BOX_WIDTH,
	 (WIDTH >> 1), BOX_WIDTH,
	 (WIDTH >> 1), 0,

	-(WIDTH >> 1),         BOX_WIDTH,
	-(WIDTH >> 1),         HEIGHT - BOX_WIDTH,
	-(WIDTH >> 1) + BOX_WIDTH, HEIGHT - BOX_WIDTH,
	-(WIDTH >> 1) + BOX_WIDTH, 0,

	 (WIDTH >> 1) - BOX_WIDTH, BOX_WIDTH,
	 (WIDTH >> 1) - BOX_WIDTH, HEIGHT - BOX_WIDTH,
	 (WIDTH >> 1),         HEIGHT - BOX_WIDTH,
	 (WIDTH >> 1),         0,

	-(WIDTH >> 1), HEIGHT - BOX_WIDTH,
	-(WIDTH >> 1), HEIGHT,
	 (WIDTH >> 1), HEIGHT,
	 (WIDTH >> 1), HEIGHT - BOX_WIDTH
};

#define WINDOW_WIDTH(count) (WIDTH * (count) + (SPACE << 1))
#define WINDOW_HEIGHT (HEIGHT + (SPACE << 1))

#define GET_SWITCH_DISPLAY(d) \
        ((SwitchDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define SWITCH_DISPLAY(d) \
        SwitchDisplay *sd = GET_SWITCH_DISPLAY (d)

#define GET_SWITCH_SCREEN(s, sd) \
        ((SwitchScreen *) (s)->privates[(sd)->screenPrivateIndex].ptr)

#define SWITCH_SCREEN(s) \
        SwitchScreen *ss = GET_SWITCH_SCREEN (s, GET_SWITCH_DISPLAY (&display))

static void
switchChangeNotify (const char        *optionName,
                    BananaType        optionType,
                    const BananaValue *optionValue,
                    int               screenNum)
{
	SWITCH_DISPLAY (&display);

	if (strcasecmp (optionName, "zoom") == 0)
	{
		CompScreen *screen;

		screen = getScreenFromScreenNum (screenNum);

		SWITCH_SCREEN (screen);

		if (optionValue->f < 0.05f)
		{
			ss->zooming = FALSE;
			ss->zoom    = 0.0f;
		}
		else
		{
			ss->zooming = TRUE;
			ss->zoom    = optionValue->f / 30.0f;
		}
	}
	else if (strcasecmp (optionName, "next_key") == 0)
		updateKey (optionValue->s, &sd->next_key);

	else if (strcasecmp (optionName, "prev_key") == 0)
		updateKey (optionValue->s, &sd->prev_key);

	else if (strcasecmp (optionName, "next_all_key") == 0)
		updateKey (optionValue->s, &sd->next_all_key);

	else if (strcasecmp (optionName, "prev_all_key") == 0)
		updateKey (optionValue->s, &sd->prev_all_key);

	else if (strcasecmp (optionName, "window_match") == 0)
	{
		CompScreen *screen;

		screen = getScreenFromScreenNum (screenNum);

		SWITCH_SCREEN (screen);

		matchFini (&ss->window_match);
		matchInit (&ss->window_match);
		matchAddFromString (&ss->window_match, optionValue->s);
		matchUpdate (&ss->window_match);
	}
}

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
	SWITCH_SCREEN (w->screen);

	if (w->destroyed)
		return FALSE;

	if (!w->mapNum || w->attrib.map_state != IsViewable)
	{
		const BananaValue *
		option_minimized = bananaGetOption (bananaIndex,
		                                    "minimized",
		                                    w->screen->screenNum);

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

	if (w->wmType & (CompWindowTypeDockMask | CompWindowTypeDesktopMask))
		return FALSE;

	if (w->state & CompWindowStateSkipTaskbarMask)
		return FALSE;

	if (!matchEval (&ss->window_match, w))
		return FALSE;

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

	(*display.handleFusilliEvent) ("switcher", "activate", arg, 2);
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
switchUpdateWindowList (CompScreen *s,
                        int        count)
{
	int x, y;

	SWITCH_SCREEN (s);

	if (count > 1)
	{
		count -= (count + 1) & 1;
		if (count < 3)
			count = 3;
	}

	ss->pos  = ((count >> 1) - ss->nWindows) * WIDTH;
	ss->move = 0;

	ss->selectedWindow = ss->windows[0];

	x = s->outputDev[s->currentOutputDev].region.extents.x1 +
		s->outputDev[s->currentOutputDev].width / 2;
	y = s->outputDev[s->currentOutputDev].region.extents.y1 +
		s->outputDev[s->currentOutputDev].height / 2;

	if (ss->popupWindow)
		XMoveResizeWindow (display.display, ss->popupWindow,
		                   x - WINDOW_WIDTH (count) / 2,
		                   y - WINDOW_HEIGHT / 2,
		                   WINDOW_WIDTH (count),
		                   WINDOW_HEIGHT);
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

	if (ss->nWindows == 2)
	{
		switchAddWindowToList (s, ss->windows[0]);
		switchAddWindowToList (s, ss->windows[1]);
	}

	switchUpdateWindowList (s, count);
}

static void
switchToWindow (CompScreen *s,
                Bool       toNext)
{
	CompWindow *w;
	int        cur;

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
		w = ss->windows[(cur + 1) % ss->nWindows];
	else
		w = ss->windows[(cur + ss->nWindows - 1) % ss->nWindows];

	if (w)
	{
		CompWindow *old = ss->selectedWindow;

		const BananaValue *
		option_auto_rotate = bananaGetOption (bananaIndex,
		                                      "auto_rotate",
		                                      s->screenNum);

		if (ss->selection == AllViewports &&
		    option_auto_rotate->b)
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

		ss->lastActiveNum  = w->activeNum;
		ss->selectedWindow = w;

		if (!ss->zoomedWindow)
			ss->zoomedWindow = ss->selectedWindow;

		if (old != w)
		{
			if (toNext)
				ss->move -= WIDTH;
			else
				ss->move += WIDTH;

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

		addWindowDamage (w);

		if (old && !old->destroyed)
			addWindowDamage (old);
	}
}

static int
switchCountWindows (CompScreen *s)
{
	CompWindow *w;
	int        count = 0;

	for (w = s->windows; w && count < 5; w = w->next)
		if (isSwitchWin (w))
			count++;

	if (count == 5 && s->width <= WINDOW_WIDTH (5))
		count = 3;

	return count;
}

static Visual *
findArgbVisual (Display *dpy, int scr)
{
	XVisualInfo     *xvi;
	XVisualInfo     template;
	int             nvi;
	int             i;
	XRenderPictFormat *format;
	Visual          *visual;

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

static void
switchInitiate (CompScreen            *s,
                SwitchWindowSelection selection,
                Bool                  showPopup)
{
	int count;

	SWITCH_SCREEN (s);

	if (otherScreenGrabExist (s, "cube", NULL))
		return;

	ss->selection      = selection;
	ss->selectedWindow = NULL;

	count = switchCountWindows (s);
	if (count < 1)
		return;

	if (!ss->popupWindow && showPopup)
	{
		Display          *dpy = display.display;
		XSizeHints       xsh;
		XWMHints         xwmh;
		XClassHint       xch;
		Atom             state[4];
		int              nState = 0;
		XSetWindowAttributes attr;
		Visual           *visual;

		visual = findArgbVisual (dpy, s->screenNum);
		if (!visual)
			return;

		if (count > 1)
		{
			count -= (count + 1) & 1;
			if (count < 3)
				count = 3;
		}

		xsh.flags       = PSize | PWinGravity;
		xsh.width       = WINDOW_WIDTH (count);
		xsh.height      = WINDOW_HEIGHT;
		xsh.win_gravity = StaticGravity;

		xwmh.flags = InputHint;
		xwmh.input = 0;

		xch.res_name  = "fusilli";
		xch.res_class = "switcher-window";

		attr.background_pixel = 0;
		attr.border_pixel     = 0;
		attr.colormap         = XCreateColormap (dpy, s->root, visual,
		                             AllocNone);

		ss->popupWindow =
		    XCreateWindow (dpy, s->root,
		                   s->width  / 2 - xsh.width / 2,
		                   s->height / 2 - xsh.height / 2,
		                   xsh.width, xsh.height, 0,
		                   32, InputOutput, visual,
		                   CWBackPixel | CWBorderPixel | CWColormap, &attr);

		XSetWMProperties (dpy, ss->popupWindow, NULL, NULL,
		                  programArgv, programArgc,
		                  &xsh, &xwmh, &xch);

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

	if (!ss->grabIndex)
		ss->grabIndex = pushScreenGrab (s, s->invisibleCursor, "switcher");

	if (ss->grabIndex)
	{
		if (!ss->switching)
		{
			ss->lastActiveNum = s->activeNum;

			switchCreateWindowList (s, count);

			ss->sTranslate = ss->zoom;

			if (ss->popupWindow && showPopup)
			{
				CompWindow *w;

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
			}

			switchActivateEvent (s, TRUE);
		}

		damageScreen (s);

		ss->switching  = TRUE;
		ss->moreAdjust = 1;
	}
}

static Bool
switchTerminate (Window     xid,
                 Bool       cancel)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
	{
		SWITCH_SCREEN (s);

		if (xid && s->root != xid)
			continue;

		if (ss->grabIndex)
		{
			CompWindow *w;

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

			//check if ESC was pressed
			if (cancel)
			{
				ss->selectedWindow = NULL;
				ss->zoomedWindow   = NULL;
			}

			if (ss->selectedWindow && !ss->selectedWindow->destroyed)
				sendWindowActivationRequest (s, ss->selectedWindow->id);

			removeScreenGrab (s, ss->grabIndex, 0);
			ss->grabIndex = 0;

			if (!ss->zooming)
			{
				ss->selectedWindow = NULL;
				ss->zoomedWindow   = NULL;

				switchActivateEvent (s, FALSE);
			}
			else
			{
				ss->moreAdjust = 1;
			}

			ss->selectedWindow = NULL;
			setSelectedWindowHint (s);

			ss->lastActiveNum = 0;

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
switchWindowRemove (CompDisplay *d,
                    CompWindow  *w)
{
	if (w)
	{
		Bool   inList = FALSE;
		int    count, j, i = 0;
		CompWindow *selected;
		CompWindow *old;

		SWITCH_SCREEN (w->screen);

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

		if (ss->nWindows == 2)
		{
			if (ss->windows[0] == ss->windows[1])
			{
				ss->nWindows--;
				count = 1;
			}
			else
			{
				switchAddWindowToList (w->screen, ss->windows[0]);
				switchAddWindowToList (w->screen, ss->windows[1]);
			}
		}

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

			if (ss->selectedWindow == selected)
				break;

			ss->pos -= WIDTH;
			if (ss->pos < -ss->nWindows * WIDTH)
				ss->pos += ss->nWindows * WIDTH;
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
			ss->zoomedWindow = ss->selectedWindow;

			addWindowDamage (ss->selectedWindow);
			addWindowDamage (w);

			if (old && !old->destroyed)
				addWindowDamage (old);
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

static void
switchHandleEvent (XEvent *event)
{
	CompWindow *w = NULL;

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
			switchTerminate (0, TRUE);

		break;
	case MapNotify:
		w = findWindowAtDisplay (event->xmap.window);
		if (w)
		{
			SWITCH_SCREEN (w->screen);

			if (w->id == ss->popupWindow)
			{
				/* we don't get a MapRequest for internal window
				   creations, so we need to update the internals
				   ourselves */
				w->wmType  = getWindowType (w->id);
				w->managed = TRUE;

				recalcWindowType (w);
				recalcWindowActions (w);
				updateWindowClassHints (w);
			}
		}
		break;
	case DestroyNotify:
		/* We need to get the CompWindow * for event->xdestroywindow.window
		   here because in the (*display.handleEvent) call below, that CompWindow's
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
	}

	UNWRAP (sd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (sd, &display, handleEvent, switchHandleEvent);

	switch (event->type) {
	case UnmapNotify:
		w = findWindowAtDisplay (event->xunmap.window);
		switchWindowRemove (&display, w);
		break;
	case DestroyNotify:
		switchWindowRemove (&display, w);
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

	default:
		break;
	}
}

static int
adjustSwitchVelocity (CompScreen *s)
{
	float dx, adjust, amount;

	SWITCH_SCREEN (s);

	dx = ss->move;

	adjust = dx * 0.15f;
	amount = fabs (dx) * 1.5f;
	if (amount < 0.2f)
		amount = 0.2f;
	else if (amount > 2.0f)
		amount = 2.0f;

	ss->mVelocity = (amount * ss->mVelocity + adjust) / (amount + 1.0f);

	if (ss->zooming)
	{
		float dt, ds;

		if (ss->switching)
			dt = ss->zoom - ss->translate;
		else
			dt = 0.0f - ss->translate;

		adjust = dt * 0.15f;
		amount = fabs (dt) * 1.5f;
		if (amount < 0.2f)
			amount = 0.2f;
		else if (amount > 2.0f)
			amount = 2.0f;

		ss->tVelocity = (amount * ss->tVelocity + adjust) / (amount + 1.0f);

		if (ss->selectedWindow == ss->zoomedWindow)
			ds = ss->zoom - ss->sTranslate;
		else
			ds = 0.0f - ss->sTranslate;

		adjust = ds * 0.5f;
		amount = fabs (ds) * 5.0f;
		if (amount < 1.0f)
			amount = 1.0f;
		else if (amount > 6.0f)
			amount = 6.0f;

		ss->sVelocity = (amount * ss->sVelocity + adjust) / (amount + 1.0f);

		if (ss->selectedWindow == ss->zoomedWindow)
		{
			if (fabs (dx) < 0.1f   && fabs (ss->mVelocity) < 0.2f   &&
			    fabs (dt) < 0.001f && fabs (ss->tVelocity) < 0.001f &&
			    fabs (ds) < 0.001f && fabs (ss->sVelocity) < 0.001f)
			{
				ss->mVelocity = ss->tVelocity = ss->sVelocity = 0.0f;
				return 0;
			}
		}
	}
	else
	{
		if (fabs (dx) < 0.1f  && fabs (ss->mVelocity) < 0.2f)
		{
			ss->mVelocity = 0.0f;
			return 0;
		}
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
		int   steps, m;
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

		if (!steps) steps = 1;
		chunk  = amount / (float) steps;

		while (steps--)
		{
			ss->moreAdjust = adjustSwitchVelocity (s);
			if (!ss->moreAdjust)
			{
				ss->pos += ss->move;
				ss->move = 0;

				if (ss->zooming)
				{
					if (ss->switching)
					{
						ss->translate  = ss->zoom;
						ss->sTranslate = ss->zoom;
					}
					else
					{
						ss->translate  = 0.0f;
						ss->sTranslate = ss->zoom;

						ss->selectedWindow = NULL;
						ss->zoomedWindow   = NULL;

						if (ss->grabIndex)
						{
							removeScreenGrab (s, ss->grabIndex, 0);
							ss->grabIndex = 0;
						}

						switchActivateEvent (s, FALSE);
					}
				}
				break;
			}

			m = ss->mVelocity * chunk;
			if (!m)
			{
				if (ss->mVelocity)
					m = (ss->move > 0) ? 1 : -1;
			}

			ss->move -= m;
			ss->pos  += m;
			if (ss->pos < -ss->nWindows * WIDTH)
				ss->pos += ss->nWindows * WIDTH;
			else if (ss->pos > 0)
				ss->pos -= ss->nWindows * WIDTH;

			ss->translate  += ss->tVelocity * chunk;
			ss->sTranslate += ss->sVelocity * chunk;

			if (ss->selectedWindow != ss->zoomedWindow)
			{
				if (ss->sTranslate < 0.01f)
					ss->zoomedWindow = ss->selectedWindow;
			}
		}
	}

	UNWRAP (ss, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, msSinceLastPaint);
	WRAP (ss, s, preparePaintScreen, switchPreparePaintScreen);
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

	ss->zoomMask = ZOOMED_WINDOW_MASK | NORMAL_WINDOW_MASK;

	if (ss->grabIndex || (ss->zooming && ss->translate > 0.001f))
	{
		CompTransform sTransform = *transform;
		CompWindow    *zoomed;
		CompWindow    *switcher;
		Window        zoomedAbove = None;
		Bool          saveDestroyed = FALSE;

		if (ss->zooming)
		{
			mask &= ~PAINT_SCREEN_REGION_MASK;
			mask |= PAINT_SCREEN_TRANSFORMED_MASK | PAINT_SCREEN_CLEAR_MASK;

			matrixTranslate (&sTransform, 0.0f, 0.0f, -ss->translate);

			ss->zoomMask = NORMAL_WINDOW_MASK;
		}

		switcher = findWindowAtScreen (s, ss->popupWindow);
		if (switcher)
		{
			saveDestroyed = switcher->destroyed;
			switcher->destroyed = TRUE;
		}

		const BananaValue *
		option_bring_to_front = bananaGetOption (bananaIndex,
		                                         "bring_to_front",
		                                         s->screenNum);

		if (option_bring_to_front->b)
		{
			zoomed = ss->zoomedWindow;
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
		status = (*s->paintOutput) (s, sAttrib, &sTransform,
		                        region, output, mask);
		WRAP (ss, s, paintOutput, switchPaintOutput);

		if (ss->zooming)
		{
			float zTranslate;

			mask &= ~PAINT_SCREEN_CLEAR_MASK;
			mask |= PAINT_SCREEN_NO_BACKGROUND_MASK;

			ss->zoomMask = ZOOMED_WINDOW_MASK;

			zTranslate = MIN (ss->sTranslate, ss->translate);
			matrixTranslate (&sTransform, 0.0f, 0.0f, zTranslate);

			UNWRAP (ss, s, paintOutput);
			status = (*s->paintOutput) (s, sAttrib, &sTransform, region,
			                    output, mask);
			WRAP (ss, s, paintOutput, switchPaintOutput);
		}

		if (zoomed)
		{
			unhookWindowFromScreen (s, zoomed);
			insertWindowIntoScreen (s, zoomed, zoomedAbove);
		}

		if (switcher)
		{
			sTransform = *transform;

			switcher->destroyed = saveDestroyed;

			transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

			glPushMatrix ();
			glLoadMatrixf (sTransform.m);

			if (!switcher->destroyed                     &&
			    switcher->attrib.map_state == IsViewable &&
			    switcher->damaged)
			{
				(*s->paintWindow) (switcher, &switcher->paint, &sTransform,
				               &infiniteRegion, 0);
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

	if ((ss->grabIndex || ss->zooming) && ss->moreAdjust)
	{
		if (ss->zooming)
		{
			damageScreen (s);
		}
		else
		{
			CompWindow *w;

			w = findWindowAtScreen (s, ss->popupWindow);
			if (w)
				addWindowDamage (w);
		}
	}

	UNWRAP (ss, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP (ss, s, donePaintScreen, switchDonePaintScreen);
}

static void
switchPaintThumb (CompWindow              *w,
                  const WindowPaintAttrib *attrib,
                  const CompTransform     *transform,
                  unsigned int            mask,
                  int                     x,
                  int                     y,
                  int                     x1,
                  int                     x2)
{
	WindowPaintAttrib sAttrib = *attrib;
	int                         wx, wy;
	float                       width, height;
	CompIcon                    *icon = NULL;

	mask |= PAINT_WINDOW_TRANSFORMED_MASK;

	if (w->mapNum)
	{
		if (!w->texture->pixmap && !w->bindFailed)
			bindWindow (w);
	}

	if (w->texture->pixmap)
	{
		AddWindowGeometryProc oldAddWindowGeometry;
		FragmentAttrib        fragment;
		CompTransform         wTransform = *transform;
		int                   ww, wh;
		GLenum                filter;

		width  = WIDTH  - (SPACE << 1);
		height = HEIGHT - (SPACE << 1);

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

		wx = x + SPACE + ((WIDTH  - (SPACE << 1)) - width)  / 2;
		wy = y + SPACE + ((HEIGHT - (SPACE << 1)) - height) / 2;

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
		                                 w->screen->screenNum);

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
		                               w->screen->screenNum);

		if (option_icon->b)
		{
			icon = getWindowIcon (w, MAX_ICON_SIZE, MAX_ICON_SIZE);
			if (icon)
			{
				sAttrib.xScale = (float) ICON_SIZE / icon->width;
				sAttrib.yScale = (float) ICON_SIZE / icon->height;
				/*
				sAttrib.xScale =
					((WIDTH - (SPACE << 1)) / 2.5f) / icon->width;
				sAttrib.yScale =
					((HEIGHT - (SPACE << 1)) / 2.5f) / icon->height;
				*/
				if (sAttrib.xScale < sAttrib.yScale)
					sAttrib.yScale = sAttrib.xScale;
				else
					sAttrib.xScale = sAttrib.yScale;

				wx = x + WIDTH  - icon->width  * sAttrib.xScale - SPACE;
				wy = y + HEIGHT - icon->height * sAttrib.yScale - SPACE;
			}
		}
	}
	else
	{
		width  = WIDTH  - (WIDTH  >> 2);
		height = HEIGHT - (HEIGHT >> 2);

		icon = getWindowIcon (w, MAX_ICON_SIZE, MAX_ICON_SIZE);
		if (!icon)
			icon = w->screen->defaultIcon;

		if (icon)
		{
			float iw, ih;

			iw = width  - SPACE;
			ih = height - SPACE;

			sAttrib.xScale = (iw / icon->width);
			sAttrib.yScale = (ih / icon->height);

			if (sAttrib.xScale < sAttrib.yScale)
				sAttrib.yScale = sAttrib.xScale;
			else
				sAttrib.xScale = sAttrib.yScale;

			width  = icon->width  * sAttrib.xScale;
			height = icon->height * sAttrib.yScale;

			wx = x + SPACE + ((WIDTH  - (SPACE << 1)) - width)  / 2;
			wy = y + SPACE + ((HEIGHT - (SPACE << 1)) - height) / 2;
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

static Bool
switchPaintWindow (CompWindow              *w,
                   const WindowPaintAttrib *attrib,
                   const CompTransform     *transform,
                   Region                  region,
                   unsigned int            mask)
{
	CompScreen *s = w->screen;
	int	       zoomType = NORMAL_WINDOW_MASK;
	Bool       status;

	SWITCH_SCREEN (s);

	if (w->id == ss->popupWindow)
	{
		int            x, y, x1, x2, cx, i;
		unsigned short color[4];

		if (mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK)
			return FALSE;

		UNWRAP (ss, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP (ss, s, paintWindow, switchPaintWindow);

		if (!(mask & PAINT_WINDOW_TRANSFORMED_MASK) && region->numRects == 0)
			return TRUE;

		x1 = w->attrib.x + SPACE;
		x2 = w->attrib.x + w->width - SPACE;

		x = x1 + ss->pos;
		y = w->attrib.y + SPACE;

		glPushAttrib (GL_SCISSOR_BIT);

		glEnable (GL_SCISSOR_TEST);
		glScissor (x1, 0, x2 - x1, w->screen->height);

		for (i = 0; i < ss->nWindows; i++)
		{
			if (x + WIDTH > x1)
				switchPaintThumb (ss->windows[i], &w->lastPaint, transform,
				                  mask, x, y, x1, x2);

			x += WIDTH;
		}

		for (i = 0; i < ss->nWindows; i++)
		{
			if (x > x2)
				break;

			switchPaintThumb (ss->windows[i], &w->lastPaint, transform, mask,
			                  x, y, x1, x2);

			x += WIDTH;
		}

		glPopAttrib ();

		cx = w->attrib.x + (w->width >> 1);

		glDisableClientState (GL_TEXTURE_COORD_ARRAY);
		glEnable (GL_BLEND);
		for (i = 0; i < 4; i++)
			color[i] = (unsigned int)ss->fgColor[i] * w->lastPaint.opacity /
			           0xffff;
		glColor4usv (color);
		glPushMatrix ();
		glTranslatef (cx, y, 0.0f);
		glVertexPointer (2, GL_FLOAT, 0, _boxVertices);
		glDrawArrays (GL_QUADS, 0, 16);
		glPopMatrix ();
		glColor4usv (defaultColor);
		glDisable (GL_BLEND);
		glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	}
	else if (w == ss->selectedWindow)
	{
		const BananaValue *
		option_bring_to_front = bananaGetOption (bananaIndex,
		                                         "bring_to_front",
		                                         w->screen->screenNum);

		if (option_bring_to_front->b &&
			ss->selectedWindow == ss->zoomedWindow)
			zoomType = ZOOMED_WINDOW_MASK;

		if (!(ss->zoomMask & zoomType))
			return (mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK) ?
			    FALSE : TRUE;

		UNWRAP (ss, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP (ss, s, paintWindow, switchPaintWindow);
	}
	else if (ss->switching)
	{
		WindowPaintAttrib sAttrib = *attrib;
		GLuint            value;

		const BananaValue *
		option_saturation = bananaGetOption (bananaIndex,
		                                     "saturation",
		                                     w->screen->screenNum);

		value = option_saturation->i;
		if (value != 100)
			sAttrib.saturation = sAttrib.saturation * value / 100;

		const BananaValue *
		option_brightness = bananaGetOption (bananaIndex,
		                                     "brightness",
		                                     w->screen->screenNum);

		value = option_brightness->i;
		if (value != 100)
			sAttrib.brightness = sAttrib.brightness * value / 100;

		if (w->wmType & ~(CompWindowTypeDockMask | CompWindowTypeDesktopMask))
		{
			const BananaValue *
			option_opacity = bananaGetOption (bananaIndex,
			                                  "opacity",
			                                  w->screen->screenNum);

			value = option_opacity->i;
			if (value != 100)
				sAttrib.opacity = sAttrib.opacity * value / 100;
		}

		const BananaValue *
		option_bring_to_front = bananaGetOption (bananaIndex,
		                                         "bring_to_front",
		                                         w->screen->screenNum);

		if (option_bring_to_front->b &&
			w == ss->zoomedWindow)
			zoomType = ZOOMED_WINDOW_MASK;

		if (!(ss->zoomMask & zoomType))
			return (mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK) ?
				FALSE : TRUE;

		UNWRAP (ss, s, paintWindow);
		status = (*s->paintWindow) (w, &sAttrib, transform, region, mask);
		WRAP (ss, s, paintWindow, switchPaintWindow);
	}
	else
	{
		if (!(ss->zoomMask & zoomType))
			return (mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK) ?
				FALSE : TRUE;

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
	Bool status;

	SWITCH_SCREEN (w->screen);

	if (ss->grabIndex)
	{
		CompWindow *popup;
		int        i;

		for (i = 0; i < ss->nWindows; i++)
		{
			if (ss->windows[i] == w)
			{
				popup = findWindowAtScreen (w->screen, ss->popupWindow);
				if (popup)
					addWindowDamage (popup);

				break;
			}
		}
	}

	UNWRAP (ss, w->screen, damageWindowRect);
	status = (*w->screen->damageWindowRect) (w, initial, rect);
	WRAP (ss, w->screen, damageWindowRect, switchDamageWindowRect);

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

	sd->selectWinAtom     = XInternAtom (d->display,
	                             DECOR_SWITCH_WINDOW_ATOM_NAME, 0);
	sd->selectFgColorAtom =
		XInternAtom (d->display, DECOR_SWITCH_FOREGROUND_COLOR_ATOM_NAME, 0);

	WRAP (sd, d, handleEvent, switchHandleEvent);

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

	ss->popupWindow = None;

	ss->selectedWindow = NULL;
	ss->zoomedWindow   = NULL;

	ss->lastActiveNum  = 0;

	ss->windows     = 0;
	ss->nWindows    = 0;
	ss->windowsSize = 0;

	ss->pos = ss->move = 0;

	ss->switching = FALSE;

	ss->grabIndex = 0;

	const BananaValue *
	option_zoom = bananaGetOption (bananaIndex,
	                               "zoom",
	                               s->screenNum);

	ss->zoom = option_zoom->f / 30.0f;

	ss->zooming = (option_zoom->f > 0.05f);

	ss->zoomMask = ~0;

	ss->moreAdjust = 0;

	ss->mVelocity = 0.0f;
	ss->tVelocity = 0.0f;
	ss->sVelocity = 0.0f;

	ss->translate  = 0.0f;
	ss->sTranslate = 0.0f;

	ss->selection = CurrentViewport;

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

	if (ss->popupWindow)
		XDestroyWindow (display.display, ss->popupWindow);

	if (ss->windows)
		free (ss->windows);

	matchFini (&ss->window_match);
	free (ss);
}

static Bool
switchInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("switcher", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("switcher");

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

CompPluginVTable switchVTable = {
	"switcher",
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
