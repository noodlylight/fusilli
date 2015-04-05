/**
 * Compiz Opacify 
 *
 * Copyright (c) 2006 Kristian Lyngstøl <kristian@beryl-project.org>
 * Ported to Compiz and BCOP usage by Danny Baumann <maniac@beryl-project.org>
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
 *
 * Opacify increases opacity on targeted windows and reduces it on
 * blocking windows, making whatever window you are targeting easily
 * visible. 
 *
 */

#include <compiz-core.h>
#include "opacify_options.h"

#define GET_OPACIFY_DISPLAY(d)                            \
    ((OpacifyDisplay *) (d)->base.privates[displayPrivateIndex].ptr)
#define OPACIFY_DISPLAY(d)                                \
    OpacifyDisplay *od = GET_OPACIFY_DISPLAY (d)
#define GET_OPACIFY_SCREEN(s, od)                         \
    ((OpacifyScreen *) (s)->base.privates[(od)->screenPrivateIndex].ptr)
#define OPACIFY_SCREEN(s)                                 \
    OpacifyScreen *os = GET_OPACIFY_SCREEN (s, GET_OPACIFY_DISPLAY (s->display))
#define GET_OPACIFY_WINDOW(w, os)                         \
    ((OpacifyWindow *) (w)->base.privates[(os)->windowPrivateIndex].ptr)
#define OPACIFY_WINDOW(s)                                 \
    OpacifyWindow *ow = GET_OPACIFY_WINDOW (w,            \
			GET_OPACIFY_SCREEN (w->screen,    \
			GET_OPACIFY_DISPLAY (w->screen->display)))

/* Size of the Window array storing passive windows. */
#define MAX_WINDOWS 64

static int displayPrivateIndex;

typedef struct _OpacifyDisplay
{
    int screenPrivateIndex;

    HandleEventProc handleEvent;

    Bool toggle;
    int  activeScreen;

    CompTimeoutHandle timeoutHandle;
} OpacifyDisplay;

typedef struct _OpacifyScreen
{
    int windowPrivateIndex;

    PaintWindowProc paintWindow;

    CompWindow *newActive;

    Window             active;
    Window             passive[MAX_WINDOWS];
    Region             intersect;
    unsigned short int passiveNum;

    Bool justMoved;
} OpacifyScreen;

typedef struct _OpacifyWindow
{
    Bool opacified;
    int  opacity;
} OpacifyWindow;

/* Core opacify functions. These do the real work. ---------------------*/

/* Sets the real opacity and damages the window if actual opacity and 
 * requested opacity differs. */
static void
setOpacity (CompWindow *w,
	    int        opacity)
{
    OPACIFY_WINDOW (w);

    if (!ow->opacified || (w->paint.opacity != opacity))
	addWindowDamage (w);

    ow->opacified = TRUE;
    ow->opacity = opacity;
}

/* Resets the Window to the original opacity if it still exists.
*/
static void
resetOpacity (CompScreen *s,
	      Window     id)
{
    CompWindow *w;

    w = findWindowAtScreen (s, id);
    if (!w)
	return;

    OPACIFY_WINDOW (w);

    ow->opacified = FALSE;
    addWindowDamage (w);
}

/* Resets the opacity of windows on the passive list.
*/
static void
clearPassive (CompScreen *s)
{
    int i;

    OPACIFY_SCREEN (s);

    for (i = 0; i < os->passiveNum; i++)
	resetOpacity (s, os->passive[i]);

    os->passiveNum = 0;
}

/* Dim an (inactive) window. Place it on the passive list and
 * update passiveNum. Then change the opacity.
 */
static void
dimWindow (CompWindow *w)
{
    OPACIFY_SCREEN (w->screen);

    if (os->passiveNum >= MAX_WINDOWS - 1)
    {
	compLogMessage ("opacify", CompLogLevelWarn,
			"Trying to store information "
			"about too many windows, or you hit a bug.\nIf "
			"you don't have around %d windows blocking the "
			"currently targeted window, please report this.",
			MAX_WINDOWS);
	return;
    }

    os->passive[os->passiveNum++] = w->id;
    setOpacity (w, MIN (OPAQUE * opacifyGetPassiveOpacity (w->screen) / 100,
			w->paint.opacity));
}

/* Walk through all windows, skip until we've passed the active
 * window, skip if it's invisible, hidden or minimized, skip if
 * it's not a window type we're looking for. 
 * Dim it if it intersects. 
 *
 * Returns number of changed windows.
 */
static int
passiveWindows (CompScreen *s,
		Region     region)
{
    CompWindow *w;
    Bool       flag = FALSE;
    int        i = 0;

    OPACIFY_SCREEN (s);

    for (w = s->windows; w; w = w->next)
    {
	if (w->id == os->active)
	{
	    flag = TRUE;
	    continue;
	}
	if (!flag)
	    continue;
	if (!matchEval (opacifyGetWindowMatch (s), w))
	    continue;
	if (w->invisible || w->hidden || w->minimized)
	    continue;

	XIntersectRegion (w->region, region, os->intersect);
	if (!XEmptyRegion (os->intersect))
	{
	    dimWindow (w);
	    i++;
	}
    }

    return i;
}

/* Check if we switched active window, reset the old passive windows
 * if we did. If we have an active window and switched: reset that too.
 * If we have a window (w is true), update the active id and
 * passive list. justMoved is to make sure we recalculate opacity after
 * moving. We can't reset before moving because if we're using a delay
 * and the window being moved is not the active but overlapping, it will
 * be reset, which would conflict with move's opacity change. 
 */
static void
opacifyHandleEnter (CompScreen *s,
		    CompWindow *w)
{
    OPACIFY_SCREEN (s);

    if (otherScreenGrabExist (s, NULL))
    {
	if (!otherScreenGrabExist (s, "move", NULL))
	{
	    os->justMoved = TRUE;
	    return;
	}

	clearPassive (s);
	resetOpacity (s, os->active);
	os->active = 0;
	return;
    }

    if (!w || os->active != w->id || os->justMoved)
    {
	os->justMoved = FALSE;
	clearPassive (s);
	resetOpacity (s, os->active);
	os->active = 0;
    }

    if (!w)
	return;

    if (w->id != os->active && !w->shaded &&
	matchEval (opacifyGetWindowMatch (s), w))
    {
	int num;

	os->active = w->id;
	num = passiveWindows (s, w->region);

	if (num || opacifyGetOnlyIfBlock (s))
	    setOpacity (w, MAX (OPAQUE * opacifyGetActiveOpacity (s) / 100,
				w->paint.opacity));
    }
}

/* Check if we are on the same screen. We only want opacify active on
 * one screen, so if we are on a diffrent screen, we reset the old one.
 * Returns True if the screen has switched.
 */
static Bool
checkScreenSwitch (CompScreen *s)
{
    CompScreen *tmp;

    OPACIFY_DISPLAY (s->display);

    if (od->activeScreen == s->screenNum)
	return FALSE;

    for (tmp = s->display->screens; tmp; tmp = tmp->next)
    {
	OPACIFY_SCREEN (tmp);
	clearPassive (tmp);
	resetOpacity (tmp, os->active);
	os->active = 0;
    }

    od->activeScreen = s->screenNum;
    return TRUE;
}

/* Timeout-time! Unset the timeout handler, make sure we're on the same
 * screen, handle the event.
 */
static Bool
handleTimeout (void *data)
{
    CompScreen *s = (CompScreen *) data;

    OPACIFY_SCREEN (s);
    OPACIFY_DISPLAY (s->display);

    od->timeoutHandle = 0;
    checkScreenSwitch (s);

    opacifyHandleEnter (s, os->newActive);

    return FALSE;
}

/* Checks whether we should delay or not.
 * Returns true if immediate execution.
 */
static inline Bool
checkDelay (CompScreen *s)
{
    CompDisplay *d = s->display;

    OPACIFY_SCREEN (s);

    if (opacifyGetFocusInstant (s) && os->newActive && 
	(os->newActive->id == d->activeWindow))
	return TRUE;
    if (!opacifyGetTimeout (d))
	return TRUE;
    if (!os->newActive || (os->newActive->id == s->root))
	return FALSE;
    if (os->newActive->type & (CompWindowTypeDesktopMask |
			       CompWindowTypeDockMask))
    {
	return FALSE;
    }
    if (opacifyGetNoDelayChange (s) && os->passiveNum)
	return TRUE;

    return FALSE;
}

static Bool
opacifyPaintWindow (CompWindow              *w,
		    const WindowPaintAttrib *attrib,
    		    const CompTransform     *transform,
		    Region                  region,
    		    unsigned int            mask)
{
    Bool       status;
    CompScreen *s = w->screen;

    OPACIFY_SCREEN (s);
    OPACIFY_WINDOW (w);

    if (ow->opacified)
    {
	WindowPaintAttrib wAttrib = *attrib;

	wAttrib.opacity = ow->opacity;

	UNWRAP (os, s, paintWindow);
	status = (*s->paintWindow) (w, &wAttrib, transform, region, mask);
	WRAP (os, s, paintWindow, opacifyPaintWindow);
    }
    else
    {
	UNWRAP (os, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP (os, s, paintWindow, opacifyPaintWindow);
    }

    return status;
}

/* Takes the inital event. 
 * If we were configured, recalculate the opacify-windows if 
 * it was our window. 
 * If a window was entered: call upon handle_timeout after od->timeout 
 * micro seconds, or directly if od->timeout is 0 (no delay).
 *
 */
static void
opacifyHandleEvent (CompDisplay *d,
		    XEvent      *event)
{
    CompScreen *s;

    OPACIFY_DISPLAY (d);

    UNWRAP (od, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (od, d, handleEvent, opacifyHandleEvent);

    if (!od->toggle)
	return;

    switch (event->type) {
    case EnterNotify:
	s = findScreenAtDisplay (d, event->xcrossing.root);
	if (s)
	{
	    Window id;

    	    OPACIFY_SCREEN (s);

	    id = event->xcrossing.window;
	    os->newActive = findTopLevelWindowAtScreen (s, id);

    	    if (od->timeoutHandle)
		compRemoveTimeout (od->timeoutHandle);

    	    if (checkDelay (s))
		handleTimeout (s);
	    else
	    	od->timeoutHandle = compAddTimeout (opacifyGetTimeout (d),
						    (float)
						    opacifyGetTimeout (d) * 1.2,
						    handleTimeout, s);
	}
     	break;
    case ConfigureNotify:
	s = findScreenAtDisplay (d, event->xconfigure.event);
	if (s)
	{
	    OPACIFY_SCREEN (s);

    	    if (os->active != event->xconfigure.window)
		break;

	    clearPassive (s);
	    if (os->active)
	    {
		CompWindow *w;

		w = findWindowAtScreen (s, os->active);
		if (w)
		    passiveWindows (s, w->region);
	    }
	}
     	break;
    default:
	break;
    }
}

/* Toggle opacify on/off. We are in Display-context, make sure we handle all
 * screens. 
 */
static Bool
opacifyToggle (CompDisplay     *d,
	       CompAction      *action,
	       CompActionState state,
	       CompOption      *option,
    	       int             nOption)
{
    OPACIFY_DISPLAY (d);

    od->toggle = !od->toggle;
    if (!od->toggle && opacifyGetToggleReset (d))
    {
	CompScreen *s;
	for (s = d->screens; s; s = s->next)
	{
	    OPACIFY_SCREEN (s);
	    if (os->active)
	    {
		clearPassive (s);
		resetOpacity (s, os->active);
		os->active = 0;
	    }
	}
    }

    return TRUE;
}

/* Configuration, initialization, boring stuff. ----------------------- */

static void
opacifyDisplayOptionChanged (CompDisplay           *d,
			     CompOption            *opt,
			     OpacifyDisplayOptions num)
{
    OPACIFY_DISPLAY (d);

    switch (num) {
    case OpacifyDisplayOptionInitToggle:
	od->toggle = opt->value.b;
	break;
    default:
	break;
    }
}

static Bool
opacifyInitWindow (CompPlugin *p,
		   CompWindow *w)
{
    OpacifyWindow *ow;

    OPACIFY_SCREEN (w->screen);

    ow = malloc (sizeof (OpacifyWindow));
    if (!ow)
	return FALSE;

    ow->opacified = FALSE;

    w->base.privates[os->windowPrivateIndex].ptr = ow;

    return TRUE;
}

static void
opacifyFiniWindow (CompPlugin *p,
		   CompWindow *w)
{
    OPACIFY_WINDOW (w);

    free (ow);
}

static Bool
opacifyInitScreen (CompPlugin *p,
		   CompScreen *s)
{
    OpacifyScreen *os;

    OPACIFY_DISPLAY (s->display);

    os = calloc (1, sizeof (OpacifyScreen));
    if (!os)
	return FALSE;

    os->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (os->windowPrivateIndex < 0)
    {
	free (os);
	return FALSE;
    }

    WRAP (os, s, paintWindow, opacifyPaintWindow);

    s->base.privates[od->screenPrivateIndex].ptr = os;
    os->intersect = XCreateRegion();
    os->justMoved = FALSE;

    return TRUE;
}

static void
opacifyFiniScreen (CompPlugin *p,
		   CompScreen *s)
{
    OPACIFY_SCREEN (s);

    UNWRAP (os, s, paintWindow);

    XDestroyRegion (os->intersect);
    free (os);
}

static Bool
opacifyInitDisplay (CompPlugin  *p,
		    CompDisplay *d)
{
    OpacifyDisplay *od;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    od = malloc (sizeof (OpacifyDisplay));
    if (!od)
	return FALSE;

    od->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (od->screenPrivateIndex < 0)
    {
	free (od);
	return FALSE;
    }
    d->base.privates[displayPrivateIndex].ptr = od;
    od->timeoutHandle = 0;
    od->activeScreen = d->screens->screenNum;
    od->toggle = TRUE;

    opacifySetToggleKeyInitiate (d, opacifyToggle);	
    opacifySetInitToggleNotify (d, opacifyDisplayOptionChanged);

    WRAP (od, d, handleEvent, opacifyHandleEvent);
    return TRUE;
}

static void
opacifyFiniDisplay (CompPlugin  *p,
		    CompDisplay *d)
{
    OPACIFY_DISPLAY (d);

    UNWRAP (od, d, handleEvent);

    if (od->timeoutHandle)
	compRemoveTimeout(od->timeoutHandle);

    freeScreenPrivateIndex (d, od->screenPrivateIndex);
    free (od);
}

static CompBool
opacifyInitObject (CompPlugin *p,
		   CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) opacifyInitDisplay,
	(InitPluginObjectProc) opacifyInitScreen,
	(InitPluginObjectProc) opacifyInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
opacifyFiniObject (CompPlugin *p,
		   CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) opacifyFiniDisplay,
	(FiniPluginObjectProc) opacifyFiniScreen,
	(FiniPluginObjectProc) opacifyFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
opacifyInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
opacifyFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable opacifyVTable = {
    "opacify",
    0,
    opacifyInit,
    opacifyFini,
    opacifyInitObject,
    opacifyFiniObject,
    0,
    0
};

CompPluginVTable*
getCompPluginInfo (void)
{
    return &opacifyVTable;
}
