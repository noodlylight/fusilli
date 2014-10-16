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
 */

#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/Xfixes.h>

#include <fusilli-core.h>

static void
handleWindowDamageRect (CompWindow *w,
                        int        x,
                        int        y,
                        int        width,
                        int        height)
{
	REGION region;
	Bool   initial = FALSE;

	if (!w->redirected || w->bindFailed)
		return;

	if (!w->damaged)
	{
		w->damaged   = initial = TRUE;
		w->invisible = WINDOW_INVISIBLE (w);
	}

	region.extents.x1 = x;
	region.extents.y1 = y;
	region.extents.x2 = region.extents.x1 + width;
	region.extents.y2 = region.extents.y1 + height;

	if (!(*w->screen->damageWindowRect) (w, initial, &region.extents))
	{
		region.extents.x1 += w->attrib.x + w->attrib.border_width;
		region.extents.y1 += w->attrib.y + w->attrib.border_width;
		region.extents.x2 += w->attrib.x + w->attrib.border_width;
		region.extents.y2 += w->attrib.y + w->attrib.border_width;

		region.rects = &region.extents;
		region.numRects = region.size = 1;

		damageScreenRegion (w->screen, &region);
	}

	if (initial)
		damageWindowOutputExtents (w);
}

void
handleSyncAlarm (CompWindow *w)
{
	if (w->syncWait)
	{
		if (w->syncWaitHandle)
		{
			compRemoveTimeout (w->syncWaitHandle);
			w->syncWaitHandle = 0;
		}

		w->syncWait = FALSE;

		if (resizeWindow (w,
		                  w->syncX, w->syncY,
		                  w->syncWidth, w->syncHeight,
		                  w->syncBorderWidth))
		{
			XRectangle *rects;
			int        nDamage;

			nDamage = w->nDamage;
			rects   = w->damageRects;
			while (nDamage--)
			{
				handleWindowDamageRect (w,
				                        rects[nDamage].x,
				                        rects[nDamage].y,
				                        rects[nDamage].width,
				                        rects[nDamage].height);
			}

			w->nDamage = 0;
		}
		else
		{
			/* resizeWindow failing means that there is another pending
			   resize and we must send a new sync request to the client */
			sendSyncRequest (w);
		}
	}
}

static void
moveInputFocusToOtherWindow (CompWindow *w)
{
	CompScreen  *s = w->screen;
	CompDisplay *d = s->display;
	Bool        focussedAny = FALSE;

	if (w->id != d->activeWindow && w->id != d->nextActiveWindow)
		if (d->activeWindow != None)
			return;

	if (w->transientFor && w->transientFor != s->root)
	{
		CompWindow *ancestor = findWindowAtDisplay (d, w->transientFor);
		if (ancestor && !(ancestor->type & (CompWindowTypeDesktopMask |
		                                    CompWindowTypeDockMask)))
		{
			moveInputFocusToWindow (ancestor);
			focussedAny = TRUE;
		}
	}
	else if (w->type & (CompWindowTypeDialogMask |
	                    CompWindowTypeModalDialogMask))
	{
		CompWindow *a, *focus = NULL;

		for (a = s->reverseWindows; a; a = a->prev)
		{
			if (a->clientLeader != w->clientLeader)
				continue;

			if (!(*s->focusWindow) (a))
				continue;

			if (!focus)
			{
				focus = a;
				continue;
			}

			if (a->type & (CompWindowTypeNormalMask |
			               CompWindowTypeDialogMask |
			               CompWindowTypeModalDialogMask))
			{
				if (compareWindowActiveness (focus, a) < 0)
					focus = a;
			}
		}

		if (focus && !(focus->type & (CompWindowTypeDesktopMask |
		                              CompWindowTypeDockMask)))
		{
			moveInputFocusToWindow (focus);
			focussedAny = TRUE;
		}
	}

	if (!focussedAny)
		focusDefaultWindow (s);
}

static Bool
autoRaiseTimeout (void *closure)
{
	CompDisplay *display = closure;
	CompWindow  *w = findWindowAtDisplay (display, display->activeWindow);

	if (display->autoRaiseWindow == display->activeWindow ||
		(w && (display->autoRaiseWindow == w->transientFor)))
	{
		w = findWindowAtDisplay (display, display->autoRaiseWindow);
		if (w)
			updateWindowAttributes (w, CompStackingUpdateModeNormal);
	}

	return FALSE;
}

static Bool
closeWin (BananaArgument   *arg,
          int              nArg)
{
	CompWindow   *w;
	Window       xid;
	unsigned int time;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	BananaValue *arg_time = getArgNamed ("time", arg, nArg);

	if (arg_time != NULL)
		time = arg_time->i;
	else
		time = CurrentTime;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w && (w->actions & CompWindowActionCloseMask))
		closeWindow (w, time);

	return TRUE;
}

static Bool
unmaximize (BananaArgument   *arg,
            int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w)
		maximizeWindow (w, 0);

	return TRUE;
}

static Bool
minimize (BananaArgument   *arg,
          int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w && (w->actions & CompWindowActionMinimizeMask))
		minimizeWindow (w);

	return TRUE;
}

static Bool
maximize (BananaArgument   *arg,
          int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w)
		maximizeWindow (w, MAXIMIZE_STATE);

	return TRUE;
}

static Bool
maximizeHorizontally (BananaArgument   *arg,
                      int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w)
		maximizeWindow (w, w->state | CompWindowStateMaximizedHorzMask);

	return TRUE;
}

static Bool
maximizeVertically (BananaArgument   *arg,
                    int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w)
		maximizeWindow (w, w->state | CompWindowStateMaximizedVertMask);

	return TRUE;
}

static Bool
showDesktop (BananaArgument   *arg,
             int              nArg)
{
	CompScreen *s;
	Window     xid;

	BananaValue *arg_root = getArgNamed ("root", arg, nArg);

	if (arg_root != NULL)
		xid = arg_root->i;
	else
		xid = 0;

	s = findScreenAtDisplay (core.displays, xid);
	if (s)
	{
		if (s->showingDesktopMask == 0)
			(*s->enterShowDesktopMode) (s);
		else
			(*s->leaveShowDesktopMode) (s, NULL);
	}

	return TRUE;
}

static Bool
toggleSlowAnimations (BananaArgument   *arg,
                      int              nArg)
{
	CompScreen *s;
	Window     xid;

	BananaValue *arg_root = getArgNamed ("root", arg, nArg);

	if (arg_root != NULL)
		xid = arg_root->i;
	else
		xid = 0;

	s = findScreenAtDisplay (core.displays, xid);
	if (s)
		s->slowAnimations = !s->slowAnimations;

	return TRUE;
}

static Bool
raiseInitiate (BananaArgument   *arg,
               int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w)
		raiseWindow (w);

	return TRUE;
}

static Bool
lowerInitiate (BananaArgument   *arg,
               int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w)
		lowerWindow (w);

	return TRUE;
}

static Bool
windowMenu (BananaArgument   *arg,
            int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w && !w->screen->maxGrab)
	{
		int  x, y, button;
		Time time;

		BananaValue *arg_time = getArgNamed ("time", arg, nArg);

		if (arg_time != NULL)
			time = arg_time->i;
		else
			time = CurrentTime;

		BananaValue *arg_button = getArgNamed ("button", arg, nArg);

		if (arg_button != NULL)
			button = arg_button->i;
		else
			button = 0;

		BananaValue *arg_x = getArgNamed ("x", arg, nArg);

		if (arg_x != NULL)
			x = arg_x->i;
		else
			x = w->attrib.x;

		BananaValue *arg_y = getArgNamed ("y", arg, nArg);

		if (arg_y != NULL)
			y = arg_y->i;
		else
			y = w->attrib.y;

		toolkitAction (w->screen,
		               w->screen->display->toolkitActionWindowMenuAtom,
		               time,
		               w->id,
		               button,
		               x,
		               y);
	}

	return TRUE;
}

static Bool
toggleMaximized (BananaArgument   *arg,
                 int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w)
	{
		if ((w->state & MAXIMIZE_STATE) == MAXIMIZE_STATE)
			maximizeWindow (w, 0);
		else
			maximizeWindow (w, MAXIMIZE_STATE);
	}

	return TRUE;
}

static Bool
toggleMaximizedHorizontally (BananaArgument   *arg,
                             int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w)
		maximizeWindow (w, w->state ^ CompWindowStateMaximizedHorzMask);

	return TRUE;
}

static Bool
toggleMaximizedVertically (BananaArgument   *arg,
                           int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w)
		maximizeWindow (w, w->state ^ CompWindowStateMaximizedVertMask);

	return TRUE;
}

static Bool
shade (BananaArgument   *arg,
       int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *arg_window = getArgNamed ("window", arg, nArg);

	if (arg_window != NULL)
		xid = arg_window->i;
	else
		xid = 0;

	w = findTopLevelWindowAtDisplay (core.displays, xid);
	if (w && (w->actions & CompWindowActionShadeMask))
	{
		w->state ^= CompWindowStateShadedMask;
		updateWindowAttributes (w, CompStackingUpdateModeNone);
	}

	return TRUE;
}

void
handleFusilliEvent (CompDisplay    *d,
                   const char      *pluginName,
                   const char      *eventName,
                   BananaArgument  *arg,
                   int             nArg)
{
}

void
handleEvent (CompDisplay *d,
             XEvent      *event)
{
	CompScreen *s;
	CompWindow *w;

	switch (event->type) {
	case ButtonPress:
		s = findScreenAtDisplay (d, event->xbutton.root);
		if (s)
			setCurrentOutput (s, outputDeviceForPoint (s,
			                  event->xbutton.x_root,
			                  event->xbutton.y_root));
		break;
	case MotionNotify:
		s = findScreenAtDisplay (d, event->xmotion.root);
		if (s)
			setCurrentOutput (s, outputDeviceForPoint (s,
			                  event->xmotion.x_root,
			                  event->xmotion.y_root));
		break;
	case KeyPress:
		w = findWindowAtDisplay (d, d->activeWindow);
		if (w)
			setCurrentOutput (w->screen, outputDeviceForWindow (w));
	default:
		break;
	}

	/* handle Core bindings */
	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &d->close_window_key))
		{
			BananaArgument arg[2];

			arg[0].name = "window";
			arg[0].type = BananaInt;
			arg[0].value.i = d->activeWindow;

			arg[1].name = "time";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xkey.time;

			closeWin (arg, 2);
		}
		else if (isKeyPressEvent (event, &d->raise_window_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			raiseInitiate (&arg, 1);
		}
		else if (isKeyPressEvent (event, &d->lower_window_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			lowerInitiate (&arg, 1);
		}
		else if (isKeyPressEvent (event, &d->unmaximize_window_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			unmaximize (&arg, 1);
		}
		else if (isKeyPressEvent (event, &d->minimize_window_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			minimize (&arg, 1);
		}
		else if (isKeyPressEvent (event, &d->maximize_window_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			maximize (&arg, 1);
		}
		else if (isKeyPressEvent (event, &d->maximize_window_horizontally_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			maximizeHorizontally (&arg, 1);
		}
		else if (isKeyPressEvent (event, &d->maximize_window_horizontally_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			maximizeHorizontally (&arg, 1);
		}
		else if (isKeyPressEvent (event, &d->maximize_window_vertically_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			maximizeVertically (&arg, 1);
		}
		else if (isKeyPressEvent (event, &d->window_menu_key))
		{
			BananaArgument arg[4];

			arg[0].name = "window";
			arg[0].type = BananaInt;
			arg[0].value.i = d->activeWindow;

			arg[1].name = "time";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xkey.time;

			arg[2].name = "x";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xkey.x_root;

			arg[3].name = "y";
			arg[3].type = BananaInt;
			arg[3].value.i = event->xkey.y_root;

			windowMenu (arg, 4);
		}
		else if (isKeyPressEvent (event, &d->show_desktop_key))
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xkey.root;

			showDesktop (&arg, 1);
		}
		else if (isKeyPressEvent (event, &d->toggle_window_maximized_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			toggleMaximized (&arg, 1);
		}
		else if (isKeyPressEvent (event,
		         &d->toggle_window_maximized_horizontally_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			toggleMaximizedHorizontally (&arg, 1);
		}
		else if (isKeyPressEvent (event,
		         &d->toggle_window_maximized_vertically_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			toggleMaximizedVertically (&arg, 1);
		}
		else if (isKeyPressEvent (event,
		         &d->toggle_window_shaded_key))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			shade (&arg, 1);
		}
		else if (isKeyPressEvent (event, &d->slow_animations_key))
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xkey.root;

			toggleSlowAnimations (&arg, 1);
		}

		break;
	case ButtonPress:
		if (isButtonPressEvent (event, &d->close_window_button))
		{
			BananaArgument arg[2];

			arg[0].name = "window";
			arg[0].type = BananaInt;
			arg[0].value.i = d->activeWindow;

			arg[1].name = "time";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xbutton.time;

			closeWin (arg, 2);
		}
		else if (isButtonPressEvent (event, &d->raise_window_button))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			raiseInitiate (&arg, 1);
		}
		else if (isButtonPressEvent (event, &d->lower_window_button))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			lowerInitiate (&arg, 1);
		}
		else if (isButtonPressEvent (event, &d->minimize_window_button))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			minimize (&arg, 1);
		}
		else if (isButtonPressEvent (event, &d->window_menu_button))
		{
			BananaArgument arg[5];

			arg[0].name = "window";
			arg[0].type = BananaInt;
			arg[0].value.i = d->activeWindow;

			arg[1].name = "time";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xbutton.time;

			arg[2].name = "x";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xbutton.x_root;

			arg[3].name = "y";
			arg[3].type = BananaInt;
			arg[3].value.i = event->xbutton.y_root;

			arg[4].name = "button";
			arg[4].type = BananaInt;
			arg[4].value.i = event->xbutton.button;

			windowMenu (arg, 5);
		}
		else if (isButtonPressEvent (event, &d->toggle_window_maximized_button))
		{
			BananaArgument arg;

			arg.name = "window";
			arg.type = BananaInt;
			arg.value.i = d->activeWindow;

			toggleMaximized (&arg, 1);
		}
		break;
	default:
		break;
	}

	switch (event->type) {
	case Expose:
		for (s = d->screens; s; s = s->next)
			if (s->output == event->xexpose.window)
				break;

		if (s)
		{
			int more = event->xexpose.count + 1;

			if (s->nExpose == s->sizeExpose)
			{
				s->exposeRects = realloc (s->exposeRects,
				                          (s->sizeExpose + more) *
				                          sizeof (XRectangle));
				s->sizeExpose += more;
			}

			s->exposeRects[s->nExpose].x      = event->xexpose.x;
			s->exposeRects[s->nExpose].y      = event->xexpose.y;
			s->exposeRects[s->nExpose].width  = event->xexpose.width;
			s->exposeRects[s->nExpose].height = event->xexpose.height;
			s->nExpose++;

			if (event->xexpose.count == 0)
			{
				REGION rect;

				rect.rects = &rect.extents;
				rect.numRects = rect.size = 1;

				while (s->nExpose--)
				{
					rect.extents.x1 = s->exposeRects[s->nExpose].x;
					rect.extents.y1 = s->exposeRects[s->nExpose].y;
					rect.extents.x2 = rect.extents.x1 +
					    s->exposeRects[s->nExpose].width;
					rect.extents.y2 = rect.extents.y1 +
					    s->exposeRects[s->nExpose].height;

					damageScreenRegion (s, &rect);
				}
				s->nExpose = 0;
			}
		}
		break;
	case SelectionRequest:
		handleSelectionRequest (d, event);
		break;
	case SelectionClear:
		handleSelectionClear (d, event);
		break;
	case ConfigureNotify:
		w = findWindowAtDisplay (d, event->xconfigure.window);
		if (w)
		{
			configureWindow (w, &event->xconfigure);
		}
		else
		{
			s = findScreenAtDisplay (d, event->xconfigure.window);
			if (s)
				configureScreen (s, &event->xconfigure);
		}
		break;
	case CreateNotify:
		s = findScreenAtDisplay (d, event->xcreatewindow.parent);
		if (s)
		{
			/* The first time some client asks for the composite
			 * overlay window, the X server creates it, which causes
			 * an errorneous CreateNotify event.  We catch it and
			 * ignore it. */
			if (s->overlay != event->xcreatewindow.window)
				addWindow (s, event->xcreatewindow.window, getTopWindow (s));
		}
		break;
	case DestroyNotify:
		w = findWindowAtDisplay (d, event->xdestroywindow.window);
		if (w)
		{
			moveInputFocusToOtherWindow (w);
			destroyWindow (w);
		}
		break;
	case MapNotify:
		w = findWindowAtDisplay (d, event->xmap.window);
		if (w)
		{
			if (w->pendingMaps)
				w->managed = TRUE;

			/* been shaded */
			if (w->height == 0)
			{
				if (w->id == d->activeWindow)
					moveInputFocusToWindow (w);
			}

			mapWindow (w);
		}
		break;
	case UnmapNotify:
		w = findWindowAtDisplay (d, event->xunmap.window);
		if (w)
		{
			/* Normal -> Iconic */
			if (w->pendingUnmaps)
			{
				setWmState (d, IconicState, w->id);
				w->pendingUnmaps--;
			}
			else /* X -> Withdrawn */
			{
				/* Iconic -> Withdrawn */
				if (w->state & CompWindowStateHiddenMask)
				{
					w->minimized = FALSE;

					changeWindowState (w,
					                   w->state & ~CompWindowStateHiddenMask);

					updateClientListForScreen (w->screen);
				}

				if (!w->attrib.override_redirect)
					setWmState (d, WithdrawnState, w->id);

				w->placed     = FALSE;
				w->unmanaging = w->managed;
				w->managed    = FALSE;
			}

			unmapWindow (w);

			if (!w->shaded)
				moveInputFocusToOtherWindow (w);
		}
		break;
	case ReparentNotify:
		w = findWindowAtDisplay (d, event->xreparent.window);
		s = findScreenAtDisplay (d, event->xreparent.parent);
		if (s && !w)
		{
			addWindow (s, event->xreparent.window, getTopWindow (s));
		}
		else if (w)
		{
			/* This is the only case where a window is removed but not
			   destroyed. We must remove our event mask and all passive
			   grabs. */
			XSelectInput (d->display, w->id, NoEventMask);
			XShapeSelectInput (d->display, w->id, NoEventMask);
			XUngrabButton (d->display, AnyButton, AnyModifier, w->id);

			moveInputFocusToOtherWindow (w);

			destroyWindow (w);
		}
		break;
	case CirculateNotify:
		w = findWindowAtDisplay (d, event->xcirculate.window);
		if (w)
			circulateWindow (w, &event->xcirculate);
		break;
	case ButtonPress:
		s = findScreenAtDisplay (d, event->xbutton.root);
		if (s)
		{
			if (event->xbutton.button == Button1 ||
			    event->xbutton.button == Button2 ||
			    event->xbutton.button == Button3)
			{
				w = findTopLevelWindowAtScreen (s, event->xbutton.window);
				if (w)
				{
					const BananaValue *
					option_raise_on_click = bananaGetOption (
					        coreBananaIndex, "raise_on_click", -1);

					if (option_raise_on_click->b)
						updateWindowAttributes (w,
						          CompStackingUpdateModeAboveFullscreen);

					if (w->id != d->activeWindow)
						if (!(w->type & CompWindowTypeDockMask))
							if ((*s->focusWindow) (w))
								moveInputFocusToWindow (w);
				}
			}

			if (!s->maxGrab)
				XAllowEvents (d->display, ReplayPointer, event->xbutton.time);
		}
		break;
	case PropertyNotify:
		if (event->xproperty.atom == d->winTypeAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
			{
				unsigned int type;

				type = getWindowType (d, w->id);

				if (type != w->wmType)
				{
					if (w->attrib.map_state == IsViewable)
					{
						if (w->type == CompWindowTypeDesktopMask)
							w->screen->desktopWindowCount--;
						else if (type == CompWindowTypeDesktopMask)
							w->screen->desktopWindowCount++;
					}

					w->wmType = type;

					recalcWindowType (w);
					recalcWindowActions (w);

					if (w->type & CompWindowTypeDesktopMask)
						w->paint.opacity = OPAQUE;

					if (type & (CompWindowTypeDockMask |
							CompWindowTypeDesktopMask))
						setDesktopForWindow (w, 0xffffffff);

					updateClientListForScreen (w->screen);

					(*d->matchPropertyChanged) (d, w);
				}
			}
		}
		else if (event->xproperty.atom == d->winStateAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w && !w->managed)
			{
				unsigned int state;

				state = getWindowState (d, w->id);
				state = constrainWindowState (state, w->actions);

				/* EWMH suggests that we ignore changes
				   to _NET_WM_STATE_HIDDEN */
				if (w->state & CompWindowStateHiddenMask)
					state |= CompWindowStateHiddenMask;
				else
					state &= ~CompWindowStateHiddenMask;

				if (state != w->state)
				{
					if (w->type & CompWindowTypeDesktopMask)
						w->paint.opacity = OPAQUE;

					changeWindowState (w, state);
				}
			}
		}
		else if (event->xproperty.atom == XA_WM_NORMAL_HINTS)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
			{
				updateNormalHints (w);
				recalcWindowActions (w);
			}
		}
		else if (event->xproperty.atom == XA_WM_HINTS)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
				updateWmHints (w);
		}
		else if (event->xproperty.atom == XA_WM_TRANSIENT_FOR)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
			{
				updateTransientHint (w);
				recalcWindowActions (w);
			}
		}
		else if (event->xproperty.atom == d->wmClientLeaderAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
				w->clientLeader = getClientLeader (w);
		}
		else if (event->xproperty.atom == d->wmIconGeometryAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
				updateIconGeometry (w);
		}
		else if (event->xproperty.atom == d->winOpacityAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w && !(w->type & CompWindowTypeDesktopMask))
			{
				int opacity;

				opacity = getWindowProp32 (d, w->id, d->winOpacityAtom, OPAQUE);
				if (opacity != w->paint.opacity)
				{
					w->paint.opacity = opacity;
					addWindowDamage (w);
				}
			}
		}
		else if (event->xproperty.atom == d->winBrightnessAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
			{
				int brightness;

				brightness = getWindowProp32 (d, w->id,
			 	                              d->winBrightnessAtom, BRIGHT);
				if (brightness != w->paint.brightness)
				{
					w->paint.brightness = brightness;
					addWindowDamage (w);
				}
			}
		}
		else if (event->xproperty.atom == d->winSaturationAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w && w->screen->canDoSaturated)
			{
				int saturation;

				saturation = getWindowProp32 (d, w->id,
				                              d->winSaturationAtom, COLOR);
				if (saturation != w->paint.saturation)
				{
					w->paint.saturation = saturation;
					addWindowDamage (w);
				}
			}
		}
		else if (event->xproperty.atom == d->xBackgroundAtom[0] ||
		         event->xproperty.atom == d->xBackgroundAtom[1])
		{
			s = findScreenAtDisplay (d, event->xproperty.window);
			if (s)
			{
				finiTexture (s, &s->backgroundTexture);
				initTexture (s, &s->backgroundTexture);

				if (s->backgroundLoaded)
				{
					s->backgroundLoaded = FALSE;
					damageScreen (s);
				}
			}
		}
		else if (event->xproperty.atom == d->wmStrutAtom ||
		         event->xproperty.atom == d->wmStrutPartialAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
			{
				if (updateWindowStruts (w))
					updateWorkareaForScreen (w->screen);
			}
		}
		else if (event->xproperty.atom == d->mwmHintsAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
			{
				getMwmHints (d, w->id, &w->mwmFunc, &w->mwmDecor);

				recalcWindowActions (w);
			}
		}
		else if (event->xproperty.atom == d->wmProtocolsAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
				w->protocols = getProtocols (d, w->id);
		}
		else if (event->xproperty.atom == d->wmIconAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
				freeWindowIcons (w);
		}
		else if (event->xproperty.atom == d->startupIdAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
			{
				s = w->screen;

				if (w->startupId)
					free (w->startupId);
				
				w->startupId = getStartupId (w);

				if (w->managed && w->startupId)
				{
					Time            timestamp = 0;
					int             vx, vy, x, y;
					CompFocusResult focus;

					w->initialTimestampSet = FALSE;
					applyStartupProperties (s, w);

					if (w->initialTimestampSet)
						timestamp = w->initialTimestamp;

					/* as the viewport can't be transmitted via startup
					   notification, assume the client changing the ID
					   wanted to activate the window on the current viewport */

					defaultViewportForWindow (w, &vx, &vy);
					x = w->attrib.x + (s->x - vx) * s->width;
					y = w->attrib.y + (s->y - vy) * s->height;
					moveWindowToViewportPosition (w, x, y, TRUE);

					focus = allowWindowFocus (w, 0,
					                          w->initialViewportX,
					                          w->initialViewportY,
					                          timestamp);

					if (focus == CompFocusAllowed)
						(*s->activateWindow) (w);
				}
			}
		}
		else if (event->xproperty.atom == XA_WM_CLASS)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
				updateWindowClassHints (w);
				(*d->matchPropertyChanged) (d, w);
		}
		else if (event->xproperty.atom == XA_WM_NAME)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
			{
				if (w->title)
					free (w->title);

				w->title = getWindowTitle (w);

				(*d->matchPropertyChanged) (d, w);
			}
		}
		else if (event->xproperty.atom == d->roleAtom)
		{
			w = findWindowAtDisplay (d, event->xproperty.window);
			if (w)
			{
				if (w->role)
					free (w->role);

				w->role = getWindowStringProperty (w, d->roleAtom, XA_STRING);

				(*d->matchPropertyChanged) (d, w);
			}
		}
		break;
	case MotionNotify:
		break;
	case ClientMessage:
		if (event->xclient.message_type == d->winActiveAtom)
		{
			w = findTopLevelWindowAtDisplay (d, event->xclient.window);
			if (w)
			{
				CompFocusResult focus = CompFocusAllowed;

				/* use focus stealing prevention if request came
				   from an application */
				if (event->xclient.data.l[0] == ClientTypeApplication)
					focus = allowWindowFocus (w, 0,
					                          w->screen->x,
					                          w->screen->y,
					                          event->xclient.data.l[1]);

				if (focus == CompFocusAllowed)
					(*w->screen->activateWindow) (w);
			}
		}
		else if (event->xclient.message_type == d->winOpacityAtom)
		{
			w = findWindowAtDisplay (d, event->xclient.window);
			if (w && !(w->type & CompWindowTypeDesktopMask))
			{
				GLushort opacity = event->xclient.data.l[0] >> 16;

				setWindowProp32 (d, w->id, d->winOpacityAtom, opacity);
			}
		}
		else if (event->xclient.message_type == d->winBrightnessAtom)
		{
			w = findWindowAtDisplay (d, event->xclient.window);
			if (w)
			{
				GLushort brightness = event->xclient.data.l[0] >> 16;

				setWindowProp32 (d, w->id, d->winBrightnessAtom, brightness);
			}
		}
		else if (event->xclient.message_type == d->winSaturationAtom)
		{
			w = findWindowAtDisplay (d, event->xclient.window);
			if (w && w->screen->canDoSaturated)
			{
				GLushort saturation = event->xclient.data.l[0] >> 16;

				setWindowProp32 (d, w->id, d->winSaturationAtom, saturation);
			}
		}
		else if (event->xclient.message_type == d->winStateAtom)
		{
			w = findWindowAtDisplay (d, event->xclient.window);
			if (w)
			{
				unsigned long wState, state;
				int           i;

				wState = w->state;

				for (i = 1; i < 3; i++)
				{
					state = windowStateMask (d, event->xclient.data.l[i]);
					if (state & ~CompWindowStateHiddenMask)
					{

#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD    1
#define _NET_WM_STATE_TOGGLE 2

						switch (event->xclient.data.l[0]) {
						case _NET_WM_STATE_REMOVE:
							wState &= ~state;
							break;
						case _NET_WM_STATE_ADD:
							wState |= state;
							break;
						case _NET_WM_STATE_TOGGLE:
							wState ^= state;
							break;
						}
					}
				}

				wState = constrainWindowState (wState, w->actions);
				if (w->id == d->activeWindow)
					wState &= ~CompWindowStateDemandsAttentionMask;

				if (wState != w->state)
				{
					CompStackingUpdateMode stackingUpdateMode;
					unsigned long          dState = wState ^ w->state;

					stackingUpdateMode = CompStackingUpdateModeNone;

					/* raise the window whenever its fullscreen state,
					   above/below state or maximization state changed */
					if (dState & CompWindowStateFullscreenMask)
						stackingUpdateMode = CompStackingUpdateModeAboveFullscreen;
					else if (dState & (CompWindowStateAboveMask         |
					                   CompWindowStateBelowMask         |
					                   CompWindowStateMaximizedHorzMask |
					                   CompWindowStateMaximizedVertMask))
						stackingUpdateMode = CompStackingUpdateModeNormal;

					changeWindowState (w, wState);

					updateWindowAttributes (w, stackingUpdateMode);
				}
			}
		}
		else if (event->xclient.message_type == d->wmProtocolsAtom)
		{
			if (event->xclient.data.l[0] == d->wmPingAtom)
			{
				w = findWindowAtDisplay (d, event->xclient.data.l[2]);
				if (w)
				{
					if (!w->alive)
					{
						w->alive = TRUE;

						if (w->lastCloseRequestTime)
						{
							toolkitAction (w->screen,
							               d->toolkitActionForceQuitDialogAtom,
							               w->lastCloseRequestTime,
							               w->id,
							               FALSE,
							               0,
							               0);

							w->lastCloseRequestTime = 0;
						}
					}
					w->lastPong = d->lastPing;
				}
			}
		}
		else if (event->xclient.message_type == d->closeWindowAtom)
		{
			w = findWindowAtDisplay (d, event->xclient.window);
			if (w)
				closeWindow (w, event->xclient.data.l[0]);
		}
		else if (event->xclient.message_type == d->desktopGeometryAtom)
		{
			s = findScreenAtDisplay (d, event->xclient.window);
			if (s)
			{
				BananaValue value;

				value.i = event->xclient.data.l[0] / s->width;

				bananaSetOption (coreBananaIndex,
				                 "hsize",
				                 s->screenNum,
				                 &value);

				value.i = event->xclient.data.l[1] / s->height;

				bananaSetOption (coreBananaIndex,
				                 "vsize",
				                 s->screenNum,
				                 &value);
			}
		}
		else if (event->xclient.message_type == d->moveResizeWindowAtom)
		{
			w = findWindowAtDisplay (d, event->xclient.window);
			if (w)
			{
				unsigned int   xwcm = 0;
				XWindowChanges xwc;
				int            gravity;
				unsigned int   source;

				memset (&xwc, 0, sizeof (xwc));

				if (event->xclient.data.l[0] & (1 << 8))
				{
					xwcm |= CWX;
					xwc.x = event->xclient.data.l[1];
				}

				if (event->xclient.data.l[0] & (1 << 9))
				{
					xwcm |= CWY;
					xwc.y = event->xclient.data.l[2];
				}

				if (event->xclient.data.l[0] & (1 << 10))
				{
					xwcm |= CWWidth;
					xwc.width = event->xclient.data.l[3];
				}

				if (event->xclient.data.l[0] & (1 << 11))
				{
					xwcm |= CWHeight;
					xwc.height = event->xclient.data.l[4];
				}

				gravity = event->xclient.data.l[0] & 0xFF;
				source  = (event->xclient.data.l[0] >> 12) & 0xF;

				moveResizeWindow (w, &xwc, xwcm, gravity, source);
			}
		}
		else if (event->xclient.message_type == d->restackWindowAtom)
		{
			w = findWindowAtDisplay (d, event->xclient.window);
			if (w)
			{
				/* TODO: other stack modes than Above and Below */
				if (event->xclient.data.l[1])
				{
					CompWindow *sibling;

					sibling = findWindowAtDisplay (d, event->xclient.data.l[1]);
					if (sibling)
					{
						if (event->xclient.data.l[2] == Above)
							restackWindowAbove (w, sibling);
						else if (event->xclient.data.l[2] == Below)
							restackWindowBelow (w, sibling);
					}
				}
				else
				{
					if (event->xclient.data.l[2] == Above)
						raiseWindow (w);
					else if (event->xclient.data.l[2] == Below)
						lowerWindow (w);
				}
			}
		}
		else if (event->xclient.message_type == d->wmChangeStateAtom)
		{
			w = findWindowAtDisplay (d, event->xclient.window);
			if (w)
			{
				if (event->xclient.data.l[0] == IconicState)
				{
					if (w->actions & CompWindowActionMinimizeMask)
						minimizeWindow (w);
				}
				else if (event->xclient.data.l[0] == NormalState)
					unminimizeWindow (w);
			}
		}
		else if (event->xclient.message_type == d->showingDesktopAtom)
		{
			for (s = d->screens; s; s = s->next)
			{
				if (event->xclient.window == s->root ||
					event->xclient.window == None)
				{
					if (event->xclient.data.l[0])
						(*s->enterShowDesktopMode) (s);
					else
						(*s->leaveShowDesktopMode) (s, NULL);
				}
			}
		}
		else if (event->xclient.message_type == d->numberOfDesktopsAtom)
		{
			s = findScreenAtDisplay (d, event->xclient.window);
			if (s)
			{
				BananaValue value;

				value.i = event->xclient.data.l[0];

				bananaSetOption (coreBananaIndex,
				                 "number_of_desktops",
				                 s->screenNum,
				                 &value);
			}
		}
		else if (event->xclient.message_type == d->currentDesktopAtom)
		{
			s = findScreenAtDisplay (d, event->xclient.window);
			if (s)
				setCurrentDesktop (s, event->xclient.data.l[0]);
		}
		else if (event->xclient.message_type == d->winDesktopAtom)
		{
			w = findWindowAtDisplay (d, event->xclient.window);
			if (w)
				setDesktopForWindow (w, event->xclient.data.l[0]);
		}
		else if (event->xclient.message_type == d->wmFullscreenMonitorsAtom)
		{
			w = findWindowAtDisplay (d, event->xclient.window);
			if (w)
			{
				CompFullscreenMonitorSet monitors;

				monitors.top    = event->xclient.data.l[0];
				monitors.bottom = event->xclient.data.l[1];
				monitors.left   = event->xclient.data.l[2];
				monitors.right  = event->xclient.data.l[3];

				setWindowFullscreenMonitors (w, &monitors);
			}
		}
		break;
	case MappingNotify:
		updateModifierMappings (d);
		break;
	case MapRequest:
		w = findWindowAtDisplay (d, event->xmaprequest.window);
		if (w)
		{
			XWindowAttributes attr;
			Bool              doMapProcessing = TRUE;

			/* We should check the override_redirect flag here, because the
			   client might have changed it while being unmapped. */
			if (XGetWindowAttributes (d->display, w->id, &attr))
			{
				if (w->attrib.override_redirect != attr.override_redirect)
				{
					w->attrib.override_redirect = attr.override_redirect;
					recalcWindowType (w);
					recalcWindowActions (w);

					(*d->matchPropertyChanged) (d, w);
				}
			}

			if (w->state & CompWindowStateHiddenMask)
				if (!w->minimized && !w->inShowDesktopMode)
					doMapProcessing = FALSE;

			if (doMapProcessing)
			{
				w->initialViewportX = w->screen->x;
				w->initialViewportY = w->screen->y;

				w->initialTimestampSet = FALSE;

				applyStartupProperties (w->screen, w);
			}

			w->managed = TRUE;

			if (doMapProcessing)
			{
				CompFocusResult        focus;
				CompStackingUpdateMode stackingMode;
				Bool                   initiallyMinimized;

				if (!w->placed)
				{
					int            newX, newY;
					int            gravity = w->sizeHints.win_gravity;
					XWindowChanges xwc;
					unsigned int   xwcm, source;

					/* adjust for gravity, but only for frame size */
					xwc.x      = w->serverX;
					xwc.y      = w->serverY;
					xwc.width  = 0;
					xwc.height = 0;

					xwcm = adjustConfigureRequestForGravity (w, &xwc,
					                                         CWX | CWY,
					                                         gravity, 1);

					source = ClientTypeApplication;
					(*w->screen->validateWindowResizeRequest) (w, &xwcm, &xwc,
					                                           source);

					if (xwcm)
						configureXWindow (w, xwcm, &xwc);

					if ((*w->screen->placeWindow) (w, xwc.x, xwc.y,
					                               &newX, &newY))
					{
						xwc.x = newX;
						xwc.y = newY;
						configureXWindow (w, CWX | CWY, &xwc);
					}

					w->placed   = TRUE;
				}

				focus = allowWindowFocus (w, NO_FOCUS_MASK,
				                          w->screen->x, w->screen->y, 0);

				if (focus == CompFocusDenied)
					stackingMode = CompStackingUpdateModeInitialMapDeniedFocus;
				else
					stackingMode = CompStackingUpdateModeInitialMap;

				updateWindowAttributes (w, stackingMode);

				initiallyMinimized = w->hints &&
				                     w->hints->initial_state == IconicState;

				if (w->minimized && !initiallyMinimized)
					unminimizeWindow (w);

				(*w->screen->leaveShowDesktopMode) (w->screen, w);

				if (!initiallyMinimized)
				{
					if (focus == CompFocusAllowed && !onCurrentDesktop (w))
						setCurrentDesktop (w->screen, w->desktop);

					if (!(w->state & CompWindowStateHiddenMask))
						showWindow (w);

					if (focus == CompFocusAllowed)
						moveInputFocusToWindow (w);
				}
				else
				{
					minimizeWindow (w);
					changeWindowState (w, w->state | CompWindowStateHiddenMask);
					updateClientListForScreen (w->screen);
				}
			}

			setWindowProp (d, w->id, d->winDesktopAtom, w->desktop);
		}
		else
		{
			XMapWindow (d->display, event->xmaprequest.window);
		}
		break;
	case ConfigureRequest:
		w = findWindowAtDisplay (d, event->xconfigurerequest.window);
		if (w && w->managed)
		{
			XWindowChanges xwc;

			memset (&xwc, 0, sizeof (xwc));

			xwc.x            = event->xconfigurerequest.x;
			xwc.y            = event->xconfigurerequest.y;
			xwc.width        = event->xconfigurerequest.width;
			xwc.height       = event->xconfigurerequest.height;
			xwc.border_width = event->xconfigurerequest.border_width;

			moveResizeWindow (w, &xwc, event->xconfigurerequest.value_mask,
			                 0, ClientTypeUnknown);

			if (event->xconfigurerequest.value_mask & CWStackMode)
			{
				Window          above    = None;
				CompWindow      *sibling = NULL;
				CompFocusResult focus;

				if (event->xconfigurerequest.value_mask & CWSibling)
				{
					above   = event->xconfigurerequest.above;
					sibling = findTopLevelWindowAtDisplay (d, above);
				}

				switch (event->xconfigurerequest.detail) {
				case Above:
					focus = allowWindowFocus (w, NO_FOCUS_MASK,
					                          w->screen->x, w->screen->y, 0);
					if (focus == CompFocusAllowed)
					{
						if (above)
						{
							if (sibling)
								restackWindowAbove (w, sibling);
						}
						else
							raiseWindow (w);
					}
					break;
				case Below:
					if (above)
					{
						if (sibling)
							restackWindowBelow (w, sibling);
					}
					else
						lowerWindow (w);
					break;
				default:
					/* no handling of the TopIf, BottomIf, Opposite cases -
					   there will hardly be any client using that */
					break;
				}
			}
		}
		else
		{
			XWindowChanges xwc;
			unsigned int   xwcm;

			xwcm = event->xconfigurerequest.value_mask &
			       (CWX | CWY | CWWidth | CWHeight | CWBorderWidth);

			xwc.x            = event->xconfigurerequest.x;
			xwc.y            = event->xconfigurerequest.y;
			xwc.width        = event->xconfigurerequest.width;
			xwc.height       = event->xconfigurerequest.height;
			xwc.border_width = event->xconfigurerequest.border_width;

			if (w)
				configureXWindow (w, xwcm, &xwc);
			else
				XConfigureWindow (d->display, event->xconfigurerequest.window,
				                  xwcm, &xwc);
		}
		break;
	case CirculateRequest:
		break;
	case FocusIn:
		if (event->xfocus.mode != NotifyGrab)
		{
			w = findTopLevelWindowAtDisplay (d, event->xfocus.window);
			if (w && w->managed)
			{
				unsigned int state = w->state;

				if (w->id != d->activeWindow)
				{
					d->activeWindow = w->id;
					w->activeNum = w->screen->activeNum++;

					addToCurrentActiveWindowHistory (w->screen, w->id);

					XChangeProperty (d->display, w->screen->root,
					                 d->winActiveAtom,
					                 XA_WINDOW, 32, PropModeReplace,
					                 (unsigned char *) &w->id, 1);
				}

				state &= ~CompWindowStateDemandsAttentionMask;
				changeWindowState (w, state);
			}
			else
			{
				d->activeWindow = None;

				s = findScreenAtDisplay (d, event->xfocus.window);
				if (s)
				{
					if (event->xfocus.detail == NotifyDetailNone ||
					    (event->xfocus.mode == NotifyNormal &&
					     event->xfocus.detail == NotifyInferior))
					{
						/* we don't want the root window to get focus */
						focusDefaultWindow (s);
					}
				}
			}

			if (d->nextActiveWindow == event->xfocus.window)
				d->nextActiveWindow = None;
		}
		break;
	case EnterNotify:
		s = findScreenAtDisplay (d, event->xcrossing.root);
		if (s)
			w = findTopLevelWindowAtScreen (s, event->xcrossing.window);
		else
			w = NULL;

		if (w && w->id != d->below)
		{
			d->below = w->id;

			const BananaValue *
			option_click_to_focus = bananaGetOption (
			    coreBananaIndex, "click_to_focus", -1);

			if (!option_click_to_focus->b &&
			    !s->maxGrab                                     &&
			    event->xcrossing.mode   != NotifyGrab           &&
			    event->xcrossing.detail != NotifyInferior)
			{
				Bool raise, focus;
				int  delay;

				const BananaValue *
				option_autoraise = bananaGetOption (
				    coreBananaIndex, "autoraise", -1);

				raise = option_autoraise->b;

				const BananaValue *
				option_autoraise_delay = bananaGetOption (
				    coreBananaIndex, "autoraise_delay", -1);

				delay = option_autoraise_delay->i;

				if (d->autoRaiseHandle && d->autoRaiseWindow != w->id)
				{
					compRemoveTimeout (d->autoRaiseHandle);
					d->autoRaiseHandle = 0;
				}

				if (w->type & NO_FOCUS_MASK)
					focus = FALSE;
				else
					focus = (*w->screen->focusWindow) (w);

				if (focus)
				{
					moveInputFocusToWindow (w);

					if (raise)
					{
						if (delay > 0)
						{
							d->autoRaiseWindow = w->id;
							d->autoRaiseHandle =
							      compAddTimeout (delay, (float) delay * 1.2,
							      autoRaiseTimeout, d);
						}
						else
						{
							CompStackingUpdateMode mode =
							       CompStackingUpdateModeNormal;

							updateWindowAttributes (w, mode);
						}
					}
				}
			}
		}
		break;
	case LeaveNotify:
		if (event->xcrossing.detail != NotifyInferior)
		{
			if (event->xcrossing.window == d->below)
				d->below = None;
		}
		break;
	default:
		if (event->type == d->damageEvent + XDamageNotify)
		{
			XDamageNotifyEvent *de = (XDamageNotifyEvent *) event;

			if (lastDamagedWindow && de->drawable == lastDamagedWindow->id)
			{
				w = lastDamagedWindow;
			}
			else
			{
				w = findWindowAtDisplay (d, de->drawable);
				if (w)
					lastDamagedWindow = w;
			}

			if (w)
			{
				w->texture->oldMipmaps = TRUE;

				if (w->syncWait)
				{
					if (w->nDamage == w->sizeDamage)
					{
						w->damageRects = realloc (w->damageRects,
						                         (w->sizeDamage + 1) *
						                         sizeof (XRectangle));
						w->sizeDamage += 1;
					}

					w->damageRects[w->nDamage].x      = de->area.x;
					w->damageRects[w->nDamage].y      = de->area.y;
					w->damageRects[w->nDamage].width  = de->area.width;
					w->damageRects[w->nDamage].height = de->area.height;
					w->nDamage++;
				}
				else
				{
					handleWindowDamageRect (w,
					                        de->area.x,
					                        de->area.y,
					                        de->area.width,
					                        de->area.height);
				}
			}
		}
		else if (d->shapeExtension &&
		         event->type == d->shapeEvent + ShapeNotify)
		{
			w = findWindowAtDisplay (d, ((XShapeEvent *) event)->window);
			if (w)
			{
				if (w->mapNum)
				{
					addWindowDamage (w);
					updateWindowRegion (w);
					addWindowDamage (w);
				}
			}
		}
		else if (d->randrExtension &&
		         event->type == d->randrEvent + RRScreenChangeNotify)
		{
			XRRScreenChangeNotifyEvent *rre;

			rre = (XRRScreenChangeNotifyEvent *) event;

			s = findScreenAtDisplay (d, rre->root);
			if (s)
				detectRefreshRateOfScreen (s);
		}
		else if (event->type == d->syncEvent + XSyncAlarmNotify)
		{
			XSyncAlarmNotifyEvent *sa;

			sa = (XSyncAlarmNotifyEvent *) event;

			for (s = d->screens; s; s = s->next)
			{
				for (w = s->windows; w; w = w->next)
				{
					if (w->syncAlarm == sa->alarm)
						break;
				}

				if (w)
				{
					handleSyncAlarm (w);
					/* it makes no sense to search for the already
					   found window on other screens, so leave screen loop */
					break;
				}
			}
		}
		break;
	}
}
