/*
 * extrawm.c
 * Compiz extra WM actions plugins
 * Copyright: (C) 2007 Danny Baumann <maniac@beryl-project.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <X11/Xatom.h>

#include <compiz-core.h>
#include "extrawm_options.h"

static int ExtraWMDisplayPrivateIndex;

typedef struct _DemandsAttentionWindow {
    CompWindow                     *w;
    struct _DemandsAttentionWindow *next;
} DemandsAttentionWindow;

typedef struct _ExtraWMDisplay {
    int screenPrivateIndex;

    HandleEventProc handleEvent;
} ExtraWMDisplay;

typedef struct _ExtraWMScreen {
    WindowStateChangeNotifyProc windowStateChangeNotify;

    DemandsAttentionWindow *attentionWindows;
} ExtraWMScreen;

#define EXTRAWM_DISPLAY(d) PLUGIN_DISPLAY(d, ExtraWM, e)
#define EXTRAWM_SCREEN(s) PLUGIN_SCREEN(s, ExtraWM, e)

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
    Bool     urgent = FALSE;

    hints = XGetWMHints (w->screen->display->display, w->id);
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
activateDemandsAttention (CompDisplay     *d,
			  CompAction      *action,
			  CompActionState state,
			  CompOption      *option,
			  int             nOption)
{
    Window     xid;
    CompScreen *s;

    xid = getIntOptionNamed (option, nOption, "root", None);
    s   = findScreenAtDisplay (d, xid);

    if (s)
    {
	EXTRAWM_SCREEN (s);

	if (es->attentionWindows)
	{
	    CompWindow *w = es->attentionWindows->w;

	    removeAttentionWindow (w);
	    (*w->screen->activateWindow) (w);
	}
    }

    return FALSE;
}

static Bool
sendToNextOutput (CompDisplay     *d,
		  CompAction      *action,
		  CompActionState state,
		  CompOption      *option,
		  int             nOption)
{
    CompWindow *w;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w   = findWindowAtDisplay (d, xid);

    if (w)
    {
	CompScreen *s = w->screen;
	int        outputNum, currentNum;
	CompOutput *currentOutput, *newOutput;
	int        dx, dy;

	currentNum = outputDeviceForWindow (w);
	outputNum  = getIntOptionNamed (option, nOption, "output",
					(currentNum + 1) % s->nOutputDev);

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
	    XWindowChanges    xwc;
	    int               width, height;
	    unsigned int      mask = 0;

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
	    if (d->activeWindow == w->id)
		sendWindowActivationRequest (s, w->id);
	}

	return TRUE;
    }

    return FALSE;
}

static Bool
activateWin (CompDisplay     *d,
	     CompAction      *action,
	     CompActionState state,
	     CompOption      *option,
	     int             nOption)
{
    CompWindow *w;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w = findWindowAtDisplay (d, xid);
    if (w)
  	sendWindowActivationRequest (w->screen, w->id);

    return TRUE;
}

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
toggleFullscreen (CompDisplay     *d,
		  CompAction      *action,
		  CompActionState state,
		  CompOption      *option,
		  int             nOption)
{
    CompWindow *w;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);

    w = findTopLevelWindowAtDisplay (d, xid);
    if (w && (w->actions & CompWindowActionFullscreenMask))
	fullscreenWindow (w, w->state ^ CompWindowStateFullscreenMask);

    return TRUE;
}

static Bool
toggleRedirect (CompDisplay     *d,
		CompAction      *action,
		CompActionState state,
		CompOption      *option,
		int             nOption)
{
    CompWindow *w;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w = findTopLevelWindowAtDisplay (d, xid);
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
toggleAlwaysOnTop (CompDisplay     *d,
		   CompAction      *action,
		   CompActionState state,
		   CompOption      *option,
		   int             nOption)
{
    CompWindow *w;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w = findTopLevelWindowAtDisplay (d, xid);
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
toggleSticky (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    CompWindow *w;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w = findTopLevelWindowAtDisplay (d, xid);
    if (w && (w->actions & CompWindowActionStickMask))
    {
	unsigned int newState;
	newState = w->state ^ CompWindowStateStickyMask;
	changeWindowState (w, newState);
    }

    return TRUE;
}

static void
extraWMHandleEvent (CompDisplay *d,
		    XEvent      *event)
{
    EXTRAWM_DISPLAY (d);

    UNWRAP (ed, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (ed, d, handleEvent, extraWMHandleEvent);

    switch (event->type) {
    case PropertyNotify:
	if (event->xproperty.atom == XA_WM_HINTS)
	{
	    CompWindow *w;

	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		updateAttentionWindow (w);
	}
	break;
    default:
	break;
    }
}

static void
extraWMWindowStateChangeNotify (CompWindow *w,
				unsigned int lastState)
{
    CompScreen *s = w->screen;

    EXTRAWM_SCREEN (s);

    UNWRAP (es, s, windowStateChangeNotify);
    (*s->windowStateChangeNotify) (w, lastState);
    WRAP (es, s, windowStateChangeNotify, extraWMWindowStateChangeNotify);

    if ((w->state ^ lastState) & CompWindowStateDemandsAttentionMask)
	updateAttentionWindow (w);
}

static Bool
extraWMInit (CompPlugin *p)
{
    ExtraWMDisplayPrivateIndex = allocateDisplayPrivateIndex ();
    if (ExtraWMDisplayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
extraWMFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (ExtraWMDisplayPrivateIndex);
}

static Bool
extraWMInitDisplay (CompPlugin  *p,
		    CompDisplay *d)
{
    ExtraWMDisplay *ed;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    ed = malloc (sizeof (ExtraWMDisplay));
    if (!ed)
	return FALSE;

    ed->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (ed->screenPrivateIndex < 0)
    {
	free (ed);
	return FALSE;
    }

    extrawmSetToggleRedirectKeyInitiate (d, toggleRedirect);
    extrawmSetToggleAlwaysOnTopKeyInitiate (d, toggleAlwaysOnTop);
    extrawmSetToggleStickyKeyInitiate (d, toggleSticky);
    extrawmSetToggleFullscreenKeyInitiate (d, toggleFullscreen);
    extrawmSetActivateInitiate (d, activateWin);
    extrawmSetActivateDemandsAttentionKeyInitiate (d, activateDemandsAttention);
    extrawmSetToNextOutputKeyInitiate (d, sendToNextOutput);

    WRAP (ed, d, handleEvent, extraWMHandleEvent);

    d->base.privates[ExtraWMDisplayPrivateIndex].ptr = ed;

    return TRUE;
}

static void
extraWMFiniDisplay (CompPlugin  *p,
		    CompDisplay *d)
{
    EXTRAWM_DISPLAY (d);

    freeScreenPrivateIndex (d, ed->screenPrivateIndex);

    UNWRAP (ed, d, handleEvent);

    free (ed);
}

static Bool
extraWMInitScreen (CompPlugin *p,
		   CompScreen *s)
{
    ExtraWMScreen *es;

    EXTRAWM_DISPLAY (s->display);

    es = malloc (sizeof (ExtraWMScreen));
    if (!es)
	return FALSE;

    es->attentionWindows = NULL;

    WRAP (es, s, windowStateChangeNotify, extraWMWindowStateChangeNotify);

    s->base.privates[ed->screenPrivateIndex].ptr = es;

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

static CompBool
extraWMInitObject (CompPlugin *p,
		   CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) extraWMInitDisplay,
	(InitPluginObjectProc) extraWMInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
extraWMFiniObject (CompPlugin *p,
		   CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) extraWMFiniDisplay,
	(InitPluginObjectProc) extraWMFiniScreen,
	(InitPluginObjectProc) extraWMFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

CompPluginVTable extraWMVTable = {
    "extrawm",
    0,
    extraWMInit,
    extraWMFini,
    extraWMInitObject,
    extraWMFiniObject,
    0,
    0
};

CompPluginVTable*
getCompPluginInfo (void)
{
    return &extraWMVTable;
}
