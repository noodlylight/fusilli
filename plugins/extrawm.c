/*
 * extrawm.c
 * Compiz extra WM actions plugins
 * Copyright: (C) 2007 Danny Baumann <maniac@beryl-project.org>
 *
 * Port to fusilli: 2015 Michail Bitzes <noodlylight@gmail.com>
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
 *
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/Xatom.h>

#include <fusilli-core.h>

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _DemandsAttentionWindow {
	CompWindow                     *w;
	struct _DemandsAttentionWindow *next;
} DemandsAttentionWindow;

typedef struct _ExtraWMDisplay {
	int screenPrivateIndex;

	HandleEventProc handleEvent;

	CompKeyBinding toggle_redirect_key,
	               toggle_fullscreen_key,
	               toggle_always_on_top_key,
	               toggle_sticky_key,
	               activate_demands_attention_key,
	               to_next_output_key;
} ExtraWMDisplay;

typedef struct _ExtraWMScreen {
	WindowStateChangeNotifyProc windowStateChangeNotify;

	DemandsAttentionWindow *attentionWindows;
} ExtraWMScreen;

#define GET_EXTRAWM_DISPLAY(d) \
        ((ExtraWMDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define EXTRAWM_DISPLAY(d) \
        ExtraWMDisplay *ed = GET_EXTRAWM_DISPLAY (d)

#define GET_EXTRAWM_SCREEN(s, ed) \
        ((ExtraWMScreen *) (s)->privates[(ed)->screenPrivateIndex].ptr)

#define EXTRAWM_SCREEN(s) \
        ExtraWMScreen *es = GET_EXTRAWM_SCREEN (s, GET_EXTRAWM_DISPLAY (&display))

static void
addAttentionWindow (CompWindow *w)
{
	DemandsAttentionWindow *dw;

	EXTRAWM_SCREEN (w->screen);

	/* check if the window is already there */
	for (dw = es->attentionWindows; dw; dw = dw->next)
		if (dw->w == w)
			return;

	dw = malloc (sizeof (DemandsAttentionWindow));
	if (!dw)
		return;

	dw->w = w;
	dw->next = es->attentionWindows;
	es->attentionWindows = dw;
}

static void
removeAttentionWindow (CompWindow *w)
{
	DemandsAttentionWindow *dw, *ldw;

	EXTRAWM_SCREEN (w->screen);

	for (dw = es->attentionWindows, ldw = NULL; dw; dw = dw->next)
	{
		if (w == dw->w)
		{
			if (ldw)
				ldw->next = dw->next;
			else
				es->attentionWindows = dw->next;

			free (dw);
			break;
		}

		ldw = dw;
	}
}

static void
updateAttentionWindow (CompWindow *w)
{
	XWMHints *hints;
	Bool urgent = FALSE;

	hints = XGetWMHints (display.display, w->id);
	if (hints)
	{
		if (hints->flags & XUrgencyHint)
			urgent = TRUE;

		XFree (hints);
	}

	if (urgent || (w->state & CompWindowStateDemandsAttentionMask))
		addAttentionWindow (w);
	else
		removeAttentionWindow (w);
}

static Bool
activateDemandsAttention (Window xid)
{
	CompScreen *s;

	s   = findScreenAtDisplay (xid);

	if (s)
	{
		EXTRAWM_SCREEN (s);

		if (es->attentionWindows)
		{
			CompWindow *w = es->attentionWindows->w;

			removeAttentionWindow (w);
			(*w->screen->activateWindow)(w);
		}
	}

	return FALSE;
}

static Bool
sendToNextOutput (Window xid)
{
	CompWindow *w;

	w   = findWindowAtDisplay (xid);

	if (w)
	{
		CompScreen *s = w->screen;
		int outputNum, currentNum;
		CompOutput *currentOutput, *newOutput;
		int dx, dy;

		currentNum = outputDeviceForWindow (w);
		outputNum  = (currentNum + 1) % s->nOutputDev;

		if (outputNum >= s->nOutputDev)
			return FALSE;

		currentOutput = &s->outputDev[currentNum];
		newOutput     = &s->outputDev[outputNum];

		/* move by the distance of the output center points */
		dx = (newOutput->region.extents.x1 + newOutput->width / 2) -
		     (currentOutput->region.extents.x1 + currentOutput->width / 2);
		dy = (newOutput->region.extents.y1 + newOutput->height / 2) -
		     (currentOutput->region.extents.y1 + currentOutput->height / 2);

		if (dx || dy)
		{
			/* constrain to work area of new output and move */
			CompWindowExtents newExtents;
			XRectangle        *workArea = &newOutput->workArea;
			XWindowChanges xwc;
			int width, height;
			unsigned int mask = 0;

			newExtents.left   = w->serverX + dx - w->input.left;
			newExtents.right  = w->serverX + dx +
			                    w->serverWidth + w->input.right;
			newExtents.top    = w->serverY + dy - w->input.top;
			newExtents.bottom = w->serverY + dy +
			                    w->serverHeight + w->input.bottom;

			width  = newExtents.right - newExtents.left;
			height = newExtents.bottom - newExtents.top;

			if (newExtents.left < workArea->x)
			{
				dx += workArea->x - newExtents.left;
			}
			else if (width <= workArea->width &&
			         newExtents.right > workArea->x + workArea->width)
			{
				dx += workArea->x + workArea->width - newExtents.right;
			}

			if (newExtents.top < workArea->y)
			{
				dy += workArea->y - newExtents.top;
			}
			else if (height <= workArea->height &&
			         newExtents.bottom > workArea->y + workArea->height)
			{
				dy += workArea->y + workArea->width - newExtents.right;
			}

			if (dx)
			{
				xwc.x = w->serverX + dx;
				mask  |= CWX;
			}

			if (dy)
			{
				xwc.y = w->serverY + dy;
				mask  |= CWY;
			}

			if (mask)
				configureXWindow (w, mask, &xwc);

			if (w->state & (MAXIMIZE_STATE | CompWindowStateFullscreenMask))
				updateWindowAttributes (w, CompStackingUpdateModeNone);

			/* make sure the window keeps focus */
			if (display.activeWindow == w->id)
				sendWindowActivationRequest (s, w->id);
		}

		return TRUE;
	}

	return FALSE;
}

#if 0
use for action!
static Bool
activateWin (CompDisplay     *d,
             CompAction      *action,
             CompActionState state,
             CompOption      *option,
             int nOption)
{
	CompWindow *w;
	Window xid;

	xid = getIntOptionNamed (option, nOption, "window", 0);
	w = findWindowAtDisplay (d, xid);
	if (w)
		sendWindowActivationRequest (w->screen, w->id);

	return TRUE;
}
#endif

static void
fullscreenWindow (CompWindow *w,
                  int        state)
{
	unsigned int newState = w->state;

	if (w->attrib.override_redirect)
		return;

	/* It would be a bug, to put a shaded window to fullscreen. */
	if (w->shaded)
		return;

	state = constrainWindowState (state, w->actions);
	state &= CompWindowStateFullscreenMask;

	if (state == (w->state & CompWindowStateFullscreenMask))
		return;

	newState &= ~CompWindowStateFullscreenMask;
	newState |= state;

	changeWindowState (w, newState);
	updateWindowAttributes (w, CompStackingUpdateModeNormal);
}

static Bool
toggleFullscreen (Window xid)
{
	CompWindow *w;

	w = findTopLevelWindowAtDisplay (xid);

	if (w && (w->actions & CompWindowActionFullscreenMask))
		fullscreenWindow (w, w->state ^ CompWindowStateFullscreenMask);

	return TRUE;
}

static Bool
toggleRedirect (Window xid)
{
	CompWindow *w;

	w = findTopLevelWindowAtDisplay (xid);
	if (w)
	{
		if (w->redirected)
			unredirectWindow (w);
		else
			redirectWindow (w);
	}

	return TRUE;
}

static Bool
toggleAlwaysOnTop (Window xid)
{
	CompWindow *w;

	w = findTopLevelWindowAtDisplay (xid);
	if (w)
	{
		unsigned int newState;

		newState = w->state ^ CompWindowStateAboveMask;
		changeWindowState (w, newState);
		updateWindowAttributes (w, CompStackingUpdateModeNormal);
	}

	return TRUE;
}

static Bool
toggleSticky (Window xid)
{
	CompWindow *w;

	w = findTopLevelWindowAtDisplay (xid);
	if (w && (w->actions & CompWindowActionStickMask))
	{
		unsigned int newState;
		newState = w->state ^ CompWindowStateStickyMask;
		changeWindowState (w, newState);
	}

	return TRUE;
}

static void
extraWMHandleEvent (XEvent *event)
{
	EXTRAWM_DISPLAY (&display);

	UNWRAP (ed, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (ed, &display, handleEvent, extraWMHandleEvent);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &ed->toggle_redirect_key))
			toggleRedirect (display.activeWindow);
		else if (isKeyPressEvent (event, &ed->toggle_fullscreen_key))
			toggleFullscreen (display.activeWindow);
		else if (isKeyPressEvent (event, &ed->toggle_always_on_top_key))
			toggleAlwaysOnTop (display.activeWindow);
		else if (isKeyPressEvent (event, &ed->toggle_sticky_key))
			toggleSticky (display.activeWindow);
		else if (isKeyPressEvent (event, &ed->activate_demands_attention_key))
			activateDemandsAttention (display.activeWindow);
		else if (isKeyPressEvent (event, &ed->to_next_output_key))
			sendToNextOutput (display.activeWindow);
		break;
	case PropertyNotify:
		if (event->xproperty.atom == XA_WM_HINTS)
		{
			CompWindow *w;

			w = findWindowAtDisplay (event->xproperty.window);
			if (w)
				updateAttentionWindow (w);
		}
		break;
	default:
		break;
	}
}

static void
extraWMWindowStateChangeNotify (CompWindow   *w,
                                unsigned int lastState)
{
	CompScreen *s = w->screen;

	EXTRAWM_SCREEN (s);

	UNWRAP (es, s, windowStateChangeNotify);
	(*s->windowStateChangeNotify)(w, lastState);
	WRAP (es, s, windowStateChangeNotify, extraWMWindowStateChangeNotify);

	if ((w->state ^ lastState) & CompWindowStateDemandsAttentionMask)
		updateAttentionWindow (w);
}

static void
extrawmChangeNotify (const char        *optionName,
                     BananaType        optionType,
                     const BananaValue *optionValue,
                     int               screenNum)
{
	EXTRAWM_DISPLAY (&display);

	if (strcasecmp (optionName, "toggle_redirect_key") == 0)
		updateKey (optionValue->s, &ed->toggle_redirect_key);

	else if (strcasecmp (optionName, "toggle_fullscreen_key") == 0)
		updateKey (optionValue->s, &ed->toggle_fullscreen_key);

	else if (strcasecmp (optionName, "toggle_always_on_top_key") == 0)
		updateKey (optionValue->s, &ed->toggle_always_on_top_key);

	else if (strcasecmp (optionName, "toggle_sticky_key") == 0)
		updateKey (optionValue->s, &ed->toggle_sticky_key);

	else if (strcasecmp (optionName, "activate_demands_attention_key") == 0)
		updateKey (optionValue->s, &ed->activate_demands_attention_key);

	else if (strcasecmp (optionName, "to_next_output_key") == 0)
		updateKey (optionValue->s, &ed->to_next_output_key);
}

static Bool
extraWMInitDisplay (CompPlugin  *p,
                    CompDisplay *d)
{
	ExtraWMDisplay *ed;

	ed = malloc (sizeof (ExtraWMDisplay));
	if (!ed)
		return FALSE;

	ed->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (ed->screenPrivateIndex < 0)
	{
		free (ed);
		return FALSE;
	}

	const BananaValue *
	option_toggle_redirect_key = bananaGetOption (bananaIndex,
	                                              "toggle_redirect_key",
	                                              -1);

	registerKey (option_toggle_redirect_key->s, &ed->toggle_redirect_key);

	const BananaValue *
	option_toggle_fullscreen_key = bananaGetOption (bananaIndex,
	                                                "toggle_fullscreen_key",
	                                                -1);

	registerKey (option_toggle_fullscreen_key->s, &ed->toggle_fullscreen_key);

	const BananaValue *
	option_toggle_always_on_top_key = bananaGetOption (bananaIndex,
	                                                 "toggle_always_on_top_key",
	                                                 -1);

	registerKey (option_toggle_always_on_top_key->s,
	             &ed->toggle_always_on_top_key);

	const BananaValue *
	option_toggle_sticky_key = bananaGetOption (bananaIndex,
	                                            "toggle_sticky_key",
	                                            -1);

	registerKey (option_toggle_sticky_key->s, &ed->toggle_sticky_key);

	const BananaValue *
	option_toggle_activate_demands_attention_key = bananaGetOption (bananaIndex,
	                                           "activate_demands_attention_key",
	                                           -1);

	registerKey (option_toggle_activate_demands_attention_key->s,
	             &ed->activate_demands_attention_key);

	const BananaValue *
	option_to_next_output_key = bananaGetOption (bananaIndex,
	                                             "to_next_output_key",
	                                             -1);

	registerKey (option_to_next_output_key->s,
	             &ed->to_next_output_key);

	WRAP (ed, d, handleEvent, extraWMHandleEvent);

	d->privates[displayPrivateIndex].ptr = ed;

	return TRUE;
}

static void
extraWMFiniDisplay (CompPlugin  *p,
                    CompDisplay *d)
{
	EXTRAWM_DISPLAY (d);

	freeScreenPrivateIndex (ed->screenPrivateIndex);

	UNWRAP (ed, d, handleEvent);

	free (ed);
}

static Bool
extraWMInitScreen (CompPlugin *p,
                   CompScreen *s)
{
	ExtraWMScreen *es;

	EXTRAWM_DISPLAY (&display);

	es = malloc (sizeof (ExtraWMScreen));
	if (!es)
		return FALSE;

	es->attentionWindows = NULL;

	WRAP (es, s, windowStateChangeNotify, extraWMWindowStateChangeNotify);

	s->privates[ed->screenPrivateIndex].ptr = es;

	return TRUE;
}

static void
extraWMFiniScreen (CompPlugin *p,
                   CompScreen *s)
{
	EXTRAWM_SCREEN (s);

	UNWRAP (es, s, windowStateChangeNotify);

	while (es->attentionWindows)
		removeAttentionWindow (es->attentionWindows->w);

	free (es);
}

static void
extraWMFiniWindow (CompPlugin *p,
                   CompWindow *w)
{
	removeAttentionWindow (w);
}

static Bool
extraWMInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("extrawm", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("extrawm");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, extrawmChangeNotify);

	return TRUE;
}

static void
extraWMFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable extraWMVTable = {
	"extrawm",
	extraWMInit,
	extraWMFini,
	extraWMInitDisplay,
	extraWMFiniDisplay,
	extraWMInitScreen,
	extraWMFiniScreen,
	NULL, /* extraWMInitWindow */
	extraWMFiniWindow
};

CompPluginVTable*
getCompPluginInfo20141205 (void)
{
	return &extraWMVTable;
}
