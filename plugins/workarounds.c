/*
 * Copyright (C) 2007 Andrew Riedi <andrewriedi@gmail.com>
 *
 * Sticky window handling by Dennis Kasprzyk <onestone@opencompositing.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * This plug-in for Metacity-like workarounds.
 */

#include <string.h>
#include <limits.h>

#include <compiz-core.h>
#include <X11/Xatom.h>
#include "workarounds_options.h"

static int displayPrivateIndex;

typedef struct _WorkaroundsManagedFsWindow {
	Window id;
	struct _WorkaroundsManagedFsWindow *next;
} WorkaroundsManagedFsWindow;

typedef struct _WorkaroundsDisplay {
    int screenPrivateIndex;

    HandleEventProc handleEvent;

    Atom                       roleAtom;
    WorkaroundsManagedFsWindow *mfwList;
} WorkaroundsDisplay;

typedef void (*GLProgramParameter4dvProc) (GLenum         target,
					   GLuint         index,
					   const GLdouble *data);

typedef struct _WorkaroundsScreen {
    int windowPrivateIndex;

    WindowResizeNotifyProc         windowResizeNotify;
    GetAllowedActionsForWindowProc getAllowedActionsForWindow;
    PaintScreenProc                paintScreen;

    GLProgramParameter4fProc       origProgramEnvParameter4f;
    GLProgramParameter4dvProc      programEnvParameter4dv;

    GLXCopySubBufferProc           origCopySubBuffer;
} WorkaroundsScreen;

CompScreen *currentScreen = NULL;

typedef struct _WorkaroundsWindow {
    Bool adjustedWinType;
    Bool madeSticky;
    Bool madeFullscreen;
    Bool isFullscreen;
    Bool madeDemandAttention;
} WorkaroundsWindow;

#define GET_WORKAROUNDS_DISPLAY(d) \
    ((WorkaroundsDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define WORKAROUNDS_DISPLAY(d) \
    WorkaroundsDisplay *wd = GET_WORKAROUNDS_DISPLAY (d)

#define GET_WORKAROUNDS_SCREEN(s, wd) \
    ((WorkaroundsScreen *) (s)->base.privates[(wd)->screenPrivateIndex].ptr)

#define WORKAROUNDS_SCREEN(s) \
    WorkaroundsScreen *ws = GET_WORKAROUNDS_SCREEN (s, \
                            GET_WORKAROUNDS_DISPLAY (s->display))

#define GET_WORKAROUNDS_WINDOW(w, ns) \
    ((WorkaroundsWindow *) (w)->base.privates[(ns)->windowPrivateIndex].ptr)
#define WORKAROUNDS_WINDOW(w) \
    WorkaroundsWindow *ww = GET_WORKAROUNDS_WINDOW  (w, \
		    GET_WORKAROUNDS_SCREEN  (w->screen, \
		    GET_WORKAROUNDS_DISPLAY (w->screen->display)))

static void
workaroundsAddToFullscreenList (CompWindow *w)
{
    WorkaroundsManagedFsWindow *mfw, *nmfw;

    WORKAROUNDS_DISPLAY (w->screen->display);

    nmfw = malloc (sizeof (WorkaroundsManagedFsWindow));
    if (!nmfw)
	return;

    nmfw->id   = w->id;
    nmfw->next = NULL;

    if (!wd->mfwList)
	wd->mfwList = nmfw;
    else
    {
	for (mfw = wd->mfwList; mfw->next; mfw = mfw->next)
	    if (mfw->id == w->id)
	    {
		free (nmfw);
		return;
	    }

	mfw->next = nmfw;
    }
}

static void
workaroundsRemoveFromFullscreenList (CompWindow *w)
{
    WorkaroundsManagedFsWindow *mfw;
    WorkaroundsManagedFsWindow *temp;

    WORKAROUNDS_DISPLAY (w->screen->display);

    if (!wd->mfwList)
	return;

    if (wd->mfwList->id == w->id)
    {
	mfw = wd->mfwList;
	wd->mfwList = wd->mfwList->next;
	free (mfw);
	return;
    }

    for (mfw = wd->mfwList; mfw->next; mfw = mfw->next) 
    {
	if (mfw->next->id == w->id) 
	{
	    temp = mfw->next;
	    mfw->next = mfw->next->next;
	    free (temp);
	    break;
	}
    }
}

static void
workaroundsProgramEnvParameter4f (GLenum  target,
				  GLuint  index,
				  GLfloat x,
				  GLfloat y,
				  GLfloat z,
				  GLfloat w)
{
    WorkaroundsScreen *ws;
    GLdouble data[4];

    if (!currentScreen)
	return;

    ws = GET_WORKAROUNDS_SCREEN (currentScreen, GET_WORKAROUNDS_DISPLAY (
				 currentScreen->display));

    data[0] = x;
    data[1] = y;
    data[2] = z;
    data[3] = w;

    (*ws->programEnvParameter4dv) (target, index, data);
}

static void
workaroundsUpdateParameterFix (CompScreen *s)
{
    WORKAROUNDS_SCREEN (s);

    if (!s->programEnvParameter4f || !ws->programEnvParameter4dv)
	return;
    if (workaroundsGetAiglxFragmentFix (s->display))
	s->programEnvParameter4f = workaroundsProgramEnvParameter4f;
    else
	s->programEnvParameter4f = ws->origProgramEnvParameter4f;
}

static void
workaroundsPaintScreen (CompScreen   *s,
			CompOutput   *outputs,
			int          numOutputs,
			unsigned int mask)
{
    WORKAROUNDS_SCREEN (s);

    currentScreen = s;

    if (workaroundsGetForceGlxSync (s->display))
	glXWaitX ();

    UNWRAP (ws, s, paintScreen);
    (*s->paintScreen) (s, outputs, numOutputs, mask);
    WRAP (ws, s, paintScreen, workaroundsPaintScreen);
}

static char *
workaroundsGetWindowRoleAtom (CompWindow *w)
{
    CompDisplay *d = w->screen->display;
    Atom type;
    unsigned long nItems;
    unsigned long bytesAfter;
    unsigned char *str = NULL;
    int format, result;
    char *retval;

    WORKAROUNDS_DISPLAY (d);

    result = XGetWindowProperty (d->display, w->id, wd->roleAtom,
                                 0, LONG_MAX, FALSE, XA_STRING,
                                 &type, &format, &nItems, &bytesAfter,
                                 (unsigned char **) &str);

    if (result != Success)
        return NULL;

    if (type != XA_STRING)
    {
        XFree (str);
        return NULL;
    }

    retval = strdup ((char *) str);

    XFree (str);

    return retval;
}

static void
workaroundsRemoveSticky (CompWindow *w)
{
    WORKAROUNDS_WINDOW (w);

    if (w->state & CompWindowStateStickyMask && ww->madeSticky)
	changeWindowState (w, w->state & ~CompWindowStateStickyMask);
    ww->madeSticky = FALSE;
}

static void
workaroundsUpdateSticky (CompWindow *w)
{
    WORKAROUNDS_WINDOW (w);

    CompDisplay *d  = w->screen->display;
    Bool makeSticky = FALSE;

    if (workaroundsGetStickyAlldesktops (d) && w->desktop == 0xffffffff &&
	matchEval (workaroundsGetAlldesktopStickyMatch (d), w))
	makeSticky = TRUE;

    if (makeSticky)
    {
	if (!(w->state & CompWindowStateStickyMask))
	{
	    ww->madeSticky = TRUE;
	    changeWindowState (w, w->state | CompWindowStateStickyMask);
	}
    }
    else
	workaroundsRemoveSticky (w);
}

static void
updateUrgencyState (CompWindow *w)
{
    Bool urgent;

    WORKAROUNDS_WINDOW (w);

    urgent = (w->hints && (w->hints->flags & XUrgencyHint));

    if (urgent)
    {
	ww->madeDemandAttention = TRUE;
	changeWindowState (w, w->state | CompWindowStateDemandsAttentionMask);
    }
    else if (ww->madeDemandAttention)
    {
	ww->madeDemandAttention = FALSE;
	changeWindowState (w, w->state & ~CompWindowStateDemandsAttentionMask);
    }
}

static void
workaroundsGetAllowedActionsForWindow (CompWindow   *w,
				       unsigned int *setActions,
				       unsigned int *clearActions)
{
    CompScreen *s = w->screen;

    WORKAROUNDS_SCREEN (s);
    WORKAROUNDS_WINDOW (w);

    UNWRAP (ws, s, getAllowedActionsForWindow);
    (*s->getAllowedActionsForWindow) (w, setActions, clearActions);
    WRAP (ws, s, getAllowedActionsForWindow,
	  workaroundsGetAllowedActionsForWindow);

    if (ww->isFullscreen)
	*setActions |= CompWindowActionFullscreenMask;
}

static void
workaroundsFixupFullscreen (CompWindow *w)
{
    Bool   isFullSize;
    int    output;
    BoxPtr box;

    WORKAROUNDS_WINDOW (w);
    WORKAROUNDS_DISPLAY (w->screen->display);

    if (!workaroundsGetLegacyFullscreen (w->screen->display))
	return;

    if (w->wmType & CompWindowTypeDesktopMask)
    {
	/* desktop windows are implicitly fullscreen */
	isFullSize = FALSE;
    }
    else
    {
    	/* get output region for window */
	output = outputDeviceForWindow (w);
	box = &w->screen->outputDev[output].region.extents;

	/* does the size match the output rectangle? */
	isFullSize = (w->serverX == box->x1) && (w->serverY == box->y1) &&
	             (w->serverWidth == (box->x2 - box->x1)) &&
		     (w->serverHeight == (box->y2 - box->y1));

	/* if not, check if it matches the whole screen */
	if (!isFullSize)
	{
	    if ((w->serverX == 0) && (w->serverY == 0) &&
		(w->serverWidth == w->screen->width) &&
		(w->serverHeight == w->screen->height))
	    {
		isFullSize = TRUE;
	    }
	}
    }

    ww->isFullscreen = isFullSize;
    if (isFullSize && !(w->state & CompWindowStateFullscreenMask))
    {
	unsigned int state = w->state & ~CompWindowStateFullscreenMask;

	if (isFullSize)
	    state |= CompWindowStateFullscreenMask;
	ww->madeFullscreen = isFullSize;

	if (state != w->state)
	{
	    changeWindowState (w, state);
	    updateWindowAttributes (w, CompStackingUpdateModeNormal);

	    /* keep track of windows that we interact with */
	    workaroundsAddToFullscreenList (w);
	}
    }
    else if (!isFullSize && wd->mfwList &&
	     (w->state & CompWindowStateFullscreenMask))
    {
	/* did we set the flag? */
	WorkaroundsManagedFsWindow *mfw;

	for (mfw = wd->mfwList; mfw->next; mfw = mfw->next) 
	{
	    if (mfw->id == w->id) 
	    {
		unsigned int state = w->state & ~CompWindowStateFullscreenMask;

		if (isFullSize)
		    state |= CompWindowStateFullscreenMask;

		ww->madeFullscreen = isFullSize;

		if (state != w->state)
		{
		    changeWindowState (w, state);
		    updateWindowAttributes (w, CompStackingUpdateModeNormal);
		}

		workaroundsRemoveFromFullscreenList (w);
		break;
	    }
    	}
   }
}

static void
workaroundsDoFixes (CompWindow *w)
{
    CompDisplay  *d = w->screen->display;
    unsigned int newWmType;

    newWmType = getWindowType (d, w->id);

    /* FIXME: Is this the best way to detect a notification type window? */
    if (workaroundsGetNotificationDaemonFix (d))
    {
        if (newWmType == CompWindowTypeNormalMask &&
            w->attrib.override_redirect && w->resName &&
            strcmp (w->resName, "notification-daemon") == 0)
        {
            newWmType = CompWindowTypeNotificationMask;
	    goto AppliedFix;
        }
    }

    if (workaroundsGetFirefoxMenuFix (d))
    {
        if (newWmType == CompWindowTypeNormalMask &&
            w->attrib.override_redirect && w->resName)
	{
	    if ((strcasecmp (w->resName, "gecko") == 0) ||
		(strcasecmp (w->resName, "popup") == 0))
	    {
		newWmType = CompWindowTypeDropdownMenuMask;
		goto AppliedFix;
	    }
	}
    }

    if (workaroundsGetOooMenuFix (d))
    {
        if (newWmType == CompWindowTypeNormalMask &&
            w->attrib.override_redirect && w->resName)
	{
	    if (strcasecmp (w->resName, "VCLSalFrame") == 0)
	    {
		newWmType = CompWindowTypeDropdownMenuMask;
		goto AppliedFix;
	    }
	}
    }
    /* FIXME: Basic hack to get Java windows working correctly. */
    if (workaroundsGetJavaFix (d) && w->resName)
    {
        if ((strcmp (w->resName, "sun-awt-X11-XMenuWindow") == 0) ||
            (strcmp (w->resName, "sun-awt-X11-XWindowPeer") == 0))
        {
            newWmType = CompWindowTypeDropdownMenuMask;
	    goto AppliedFix;
        }
        else if (strcmp (w->resName, "sun-awt-X11-XDialogPeer") == 0)
        {
            newWmType = CompWindowTypeDialogMask;
	    goto AppliedFix;
        }
        else if (strcmp (w->resName, "sun-awt-X11-XFramePeer") == 0)
        {
            newWmType = CompWindowTypeNormalMask;
	    goto AppliedFix;
        }
    }

    if (workaroundsGetQtFix (d))
    {
        char *windowRole;

        /* fix tooltips */
        windowRole = workaroundsGetWindowRoleAtom (w);
        if (windowRole)
        {
            if ((strcmp (windowRole, "toolTipTip") == 0) ||
                (strcmp (windowRole, "qtooltip_label") == 0))
            {
		free (windowRole);
                newWmType = CompWindowTypeTooltipMask;
		goto AppliedFix;
            }
	    else
	    {
		free (windowRole);
	    }
        }

        /* fix Qt transients - FIXME: is there a better way to detect them? */
	if (!w->resName && w->attrib.override_redirect &&
	    (w->attrib.class == InputOutput) &&
	    (newWmType == CompWindowTypeUnknownMask))
	{
	    newWmType = CompWindowTypeDropdownMenuMask;
	    goto AppliedFix;
	}
    }

AppliedFix:
    if (newWmType != w->wmType)
    {
	WORKAROUNDS_WINDOW (w);

	ww->adjustedWinType = TRUE;
	w->wmType = newWmType;

	recalcWindowType (w);
	recalcWindowActions (w);

	(*d->matchPropertyChanged) (d, w);
    }
}

static void
workaroundsDisplayOptionChanged (CompDisplay               *d,
				 CompOption                *opt,
				 WorkaroundsDisplayOptions num)
{
    CompScreen *s;
    CompWindow *w;

    WorkaroundsScreen *ws;

    for (s = d->screens; s; s = s->next)
    {
	ws = GET_WORKAROUNDS_SCREEN (s, GET_WORKAROUNDS_DISPLAY (d));
	for (w = s->windows; w; w = w->next)
	    workaroundsUpdateSticky (w);
	workaroundsUpdateParameterFix (s);
	if (workaroundsGetFglrxXglFix (d))
	    s->copySubBuffer = NULL;
	else
	    s->copySubBuffer = ws->origCopySubBuffer;
    }
}

static void
workaroundsHandleEvent (CompDisplay *d,
			XEvent      *event)
{
    CompWindow *w;

    WORKAROUNDS_DISPLAY (d);

    switch (event->type) {
    case ConfigureRequest:
	w = findWindowAtDisplay (d, event->xconfigurerequest.window);
	if (w)
	{
	    WORKAROUNDS_WINDOW (w);

	    if (ww->madeFullscreen)
		w->state &= ~CompWindowStateFullscreenMask;
	}
	break;
    case MapRequest:
	w = findWindowAtDisplay (d, event->xmaprequest.window);
	if (w)
	{
	    workaroundsUpdateSticky (w);
	    workaroundsDoFixes (w);
	    workaroundsFixupFullscreen (w);
	}
	break;
    case MapNotify:
	w = findWindowAtDisplay (d, event->xmap.window);
	if (w && w->attrib.override_redirect)
	{
	    workaroundsDoFixes (w);
	    workaroundsFixupFullscreen (w);
	}
	break;
    case DestroyNotify:
	w = findWindowAtDisplay (d, event->xdestroywindow.window);
	if (w)
	    workaroundsRemoveFromFullscreenList (w);
	break;
    }

    UNWRAP (wd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (wd, d, handleEvent, workaroundsHandleEvent);

    switch (event->type) {
    case ConfigureRequest:
	w = findWindowAtDisplay (d, event->xconfigurerequest.window);
	if (w)
	{
	    WORKAROUNDS_WINDOW (w);

	    if (ww->madeFullscreen)
		w->state |= CompWindowStateFullscreenMask;
	}
	break;
    case ClientMessage:
	if (event->xclient.message_type == d->winDesktopAtom)
        {
            w = findWindowAtDisplay (d, event->xclient.window);
	    if (w)
	        workaroundsUpdateSticky (w);
        }
	break;
    case PropertyNotify:
	if ((event->xproperty.atom == XA_WM_CLASS) ||
	    (event->xproperty.atom == d->winTypeAtom))
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		workaroundsDoFixes (w);
	}
	else if (event->xproperty.atom == XA_WM_HINTS)
	{
	    if (workaroundsGetConvertUrgency (d))
	    {
		w = findWindowAtDisplay (d, event->xproperty.window);
		if (w)
		    updateUrgencyState (w);
	    }
	}
	else if (event->xproperty.atom == d->clientListAtom)
	{
	    if (workaroundsGetJavaTaskbarFix (d))
	    {
		CompScreen *s = findScreenAtDisplay (d,
						     event->xproperty.window);
		if (s)
		{
		    for (w = s->windows; w; w = w->next)
			if (w->managed)
			    setWindowState(d, w->state, w->id);
		}
	    }
	}
	break;
    default:
	break;
    }
}

static void
workaroundsWindowResizeNotify (CompWindow *w, int dx, int dy,
                               int dwidth, int dheight)
{
    WORKAROUNDS_SCREEN (w->screen);

    if (w->attrib.map_state == IsViewable)
	workaroundsFixupFullscreen (w);

    UNWRAP (ws, w->screen, windowResizeNotify);
    (*w->screen->windowResizeNotify) (w, dx, dy, dwidth, dheight);
    WRAP (ws, w->screen, windowResizeNotify, workaroundsWindowResizeNotify);
}

static Bool
workaroundsInitDisplay (CompPlugin *plugin, CompDisplay *d)
{
    WorkaroundsDisplay *wd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    wd = malloc (sizeof (WorkaroundsDisplay));
    if (!wd)
        return FALSE;

    wd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (wd->screenPrivateIndex < 0)
    {
        free (wd);
        return FALSE;
    }

    wd->roleAtom = XInternAtom (d->display, "WM_WINDOW_ROLE", 0);
    wd->mfwList  = NULL;

    workaroundsSetStickyAlldesktopsNotify (d, workaroundsDisplayOptionChanged);
    workaroundsSetAlldesktopStickyMatchNotify (d,
					       workaroundsDisplayOptionChanged);
    workaroundsSetAiglxFragmentFixNotify (d, workaroundsDisplayOptionChanged);
    workaroundsSetFglrxXglFixNotify (d, workaroundsDisplayOptionChanged);
    

    d->base.privates[displayPrivateIndex].ptr = wd;

    WRAP (wd, d, handleEvent, workaroundsHandleEvent);

    return TRUE;
}

static void
workaroundsFiniDisplay (CompPlugin *plugin, CompDisplay *d)
{
    WORKAROUNDS_DISPLAY (d);

    freeScreenPrivateIndex (d, wd->screenPrivateIndex);

    UNWRAP (wd, d, handleEvent);

    free (wd);
}

static Bool
workaroundsInitScreen (CompPlugin *plugin, CompScreen *s)
{
    WorkaroundsScreen *ws;

    WORKAROUNDS_DISPLAY (s->display);

    ws = malloc (sizeof (WorkaroundsScreen));
    if (!ws)
        return FALSE;

    ws->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (ws->windowPrivateIndex < 0)
    {
	free (ws);
	return FALSE;
    }

    ws->programEnvParameter4dv = (GLProgramParameter4dvProc)
	    (*s->getProcAddress) ((GLubyte *) "glProgramEnvParameter4dvARB");
    ws->origProgramEnvParameter4f = s->programEnvParameter4f;

    ws->origCopySubBuffer = s->copySubBuffer;

    WRAP (ws, s, windowResizeNotify, workaroundsWindowResizeNotify);
    WRAP (ws, s, getAllowedActionsForWindow,
	  workaroundsGetAllowedActionsForWindow);

    WRAP (ws, s, paintScreen, workaroundsPaintScreen);

    s->base.privates[wd->screenPrivateIndex].ptr = ws;

    workaroundsUpdateParameterFix (s);

    if (workaroundsGetFglrxXglFix (s->display))
	s->copySubBuffer = NULL;

    return TRUE;
}

static void
workaroundsFiniScreen (CompPlugin *plugin, CompScreen *s)
{
    WORKAROUNDS_SCREEN (s);

    freeWindowPrivateIndex (s, ws->windowPrivateIndex);

    UNWRAP (ws, s, windowResizeNotify);
    UNWRAP (ws, s, getAllowedActionsForWindow);

    UNWRAP (ws, s, paintScreen);

    s->copySubBuffer = ws->origCopySubBuffer;

    free (ws);
}

static Bool
workaroundsInitWindow (CompPlugin *plugin, CompWindow *w)
{
    WorkaroundsWindow *ww;

    WORKAROUNDS_SCREEN (w->screen);

    ww = malloc (sizeof (WorkaroundsWindow));
    if (!ww)
	return FALSE;

    ww->madeSticky = FALSE;
    ww->adjustedWinType = FALSE;
    ww->isFullscreen = FALSE;
    ww->madeFullscreen = FALSE;
    ww->madeDemandAttention = FALSE;

    w->base.privates[ws->windowPrivateIndex].ptr = ww;

    return TRUE;
}

static void
workaroundsFiniWindow (CompPlugin *plugin, CompWindow *w)
{
    WORKAROUNDS_WINDOW (w);

    if (!w->destroyed)
    {
	if (ww->adjustedWinType)
	{
	    w->wmType = getWindowType (w->screen->display, w->id);
	    recalcWindowType (w);
	    recalcWindowActions (w);
	}

	if (w->state & CompWindowStateStickyMask && ww->madeSticky)
	    setWindowState (w->screen->display,
			    w->state & ~CompWindowStateStickyMask, w->id);
    }

    free (ww);
}

static CompBool
workaroundsInitObject (CompPlugin *p,
		       CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) workaroundsInitDisplay,
	(InitPluginObjectProc) workaroundsInitScreen,
	(InitPluginObjectProc) workaroundsInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
workaroundsFiniObject (CompPlugin *p,
		       CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) workaroundsFiniDisplay,
	(FiniPluginObjectProc) workaroundsFiniScreen,
	(FiniPluginObjectProc) workaroundsFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
workaroundsInit (CompPlugin *plugin)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
        return FALSE;

    return TRUE;
}

static void
workaroundsFini (CompPlugin *plugin)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable workaroundsVTable =
{
    "workarounds",
    0,
    workaroundsInit,
    workaroundsFini,
    workaroundsInitObject,
    workaroundsFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &workaroundsVTable;
}

