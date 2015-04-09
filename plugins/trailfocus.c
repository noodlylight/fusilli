/**
 * Beryl Trailfocus - take three
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
 * This version is completly rewritten from scratch with opacify as a 
 * basic template. The original trailfocus was written by: 
 * François Ingelrest <Athropos@gmail.com> and rewritten by:
 * Dennis Kasprzyk <onestone@beryl-project.org>
 * 
 *
 * Trailfocus modifies the opacity, brightness and saturation on a window 
 * based on when it last had focus. 
 *
 */

#include <string.h>
#include <compiz-core.h>
#include "trailfocus_options.h"

static int displayPrivateIndex = 0;

typedef struct _TrailfocusDisplay
{
    int screenPrivateIndex;

    HandleEventProc handleEvent;
} TrailfocusDisplay;

typedef struct _TfWindowAttributes
{
    GLushort opacity;
    GLushort brightness;
    GLushort saturation;
} TfAttrib;

typedef struct _TrailfocusScreen
{
    int windowPrivateIndex;

    Window   *win;
    TfAttrib *inc;

    CompTimeoutHandle setupTimerHandle;

    PaintWindowProc paintWindow;
} TrailfocusScreen;

typedef struct _TrailfocusWindow
{
    Bool     isTfWindow;
    TfAttrib attribs;
}  TrailfocusWindow;

#define GET_TRAILFOCUS_DISPLAY(d)                            \
    ((TrailfocusDisplay *) (d)->base.privates[displayPrivateIndex].ptr)
#define TRAILFOCUS_DISPLAY(d)                                \
    TrailfocusDisplay *td = GET_TRAILFOCUS_DISPLAY (d)

#define GET_TRAILFOCUS_SCREEN(s, td)                         \
    ((TrailfocusScreen *) (s)->base.privates[(td)->screenPrivateIndex].ptr)
#define TRAILFOCUS_SCREEN(s)                                 \
    TrailfocusScreen *ts = GET_TRAILFOCUS_SCREEN (s,     \
			   GET_TRAILFOCUS_DISPLAY (s->display))

#define GET_TRAILFOCUS_WINDOW(w, ts)                         \
	((TrailfocusWindow *) (w)->base.privates[(ts)->windowPrivateIndex].ptr)
#define TRAILFOCUS_WINDOW(w)                                    \
	TrailfocusWindow *tw = GET_TRAILFOCUS_WINDOW (w,        \
			       GET_TRAILFOCUS_SCREEN(w->screen, \
			       GET_TRAILFOCUS_DISPLAY (w->screen->display)))

/* helper macros for getting the window's extents */
#define WIN_LEFT(w)   (w->attrib.x - w->input.left)
#define WIN_RIGHT(w)  (w->attrib.x + w->attrib.width + w->input.right)
#define WIN_TOP(w)    (w->attrib.y - w->input.top)
#define WIN_BOTTOM(w) (w->attrib.y + w->attrib.height + w->input.bottom)

/* Core trailfocus functions. These do the real work. ---------------*/

/* Determines if a window should be handled by trailfocus or not */
static Bool
isTrailfocusWindow (CompWindow *w)
{
    if (WIN_LEFT (w) >= w->screen->width || WIN_RIGHT (w) <= 0 ||
	WIN_TOP (w) >= w->screen->height || WIN_BOTTOM (w) <= 0)
    {
	return FALSE;
    }

    if (w->attrib.override_redirect)
	return FALSE;

    if (!w->mapNum || w->hidden || w->minimized || w->shaded)
	return FALSE;

    if (!matchEval (trailfocusGetWindowMatch (w->screen), w))
	return FALSE;

    return TRUE;
}

/* Walks through the window-list and sets the opacity-levels for
 * all windows. The inner loop will result in ts->win[i] either
 * representing a recently focused window, or the least
 * focused window.
 */
static void
setWindows (CompScreen *s)
{
    CompWindow *w;
    Bool       wasTfWindow;
    int        i = 0, winMax;

    TRAILFOCUS_SCREEN (s);

    winMax = trailfocusGetWindowsCount (s);

    for (w = s->windows; w; w = w->next)
    {
	TRAILFOCUS_WINDOW (w);

	wasTfWindow = tw->isTfWindow;
	tw->isTfWindow = isTrailfocusWindow (w);

	if (wasTfWindow && !tw->isTfWindow)
	    addWindowDamage (w);

	if (tw->isTfWindow)
	{
	    for (i = 0; i < winMax; i++)
		if (w->id == ts->win[i])
		    break;

	    if (!wasTfWindow ||
		(memcmp (&tw->attribs, &ts->inc[i], sizeof (TfAttrib)) != 0))
	    {
		addWindowDamage (w);
	    }

	    tw->attribs = ts->inc[i];
	}
    }
}

static Bool
setupTimeout (void *closure)
{
    CompScreen *s = (CompScreen *) closure;

    TRAILFOCUS_SCREEN (s);

    setWindows (s);
    ts->setupTimerHandle = 0;

    return FALSE;
}

/* Push a new window-id on the trailfocus window-stack (not to be
 * confused with the real window stack).  Only keep one copy of a
 * window on the stack. If the window allready exist on the stack,
 * move it to the top.
 */
static CompScreen*
pushWindow (CompDisplay *d,
	    Window      id)
{
    int        i, winMax;
    CompWindow *w;

    w = findWindowAtDisplay (d, id);
    if (!w)
	return NULL;

    TRAILFOCUS_SCREEN (w->screen);

    winMax = trailfocusGetWindowsCount (w->screen);
    if (!isTrailfocusWindow (w))
	return NULL;

    for (i = 0; i < winMax; i++)
	if (ts->win[i] == id)
	    break;

    if (i == 0)
	return NULL;

    for (; i > 0; i--)
	ts->win[i] = ts->win[i - 1];

    ts->win[0] = id;
    return w->screen;
}

/* Ppop a window-id from the trailfocus window-stack (not to be
 * confused with the real window stack).  Only keep one copy of a
 * window on the stack. Also fill the empty space with the next
 * window on the real window stack.
 */
static CompScreen*
popWindow (CompDisplay *d,
	   Window      id)
{
    int        i, winMax;
    CompWindow *w;
    CompScreen *s;

    w = findWindowAtDisplay (d, id);
    if (!w)
	return NULL;

    s = w->screen;

    TRAILFOCUS_SCREEN (s);

    winMax = trailfocusGetWindowsCount (s);
    for (i = 0; i < winMax; i++)
	if (ts->win[i] == id)
	    break;

    if (i == winMax)
	return NULL;

    for (i++ ; i < winMax; i++)
	ts->win[i - 1] = ts->win[i];

    ts->win[winMax - 1] = None;

    /* find window from the stacking order next to the last window
       in the stack to fill the empty space */
    for (w = NULL, i = winMax - 1; i >= 0; i--)
	if (ts->win[i])
	{
	    w = findWindowAtDisplay (d, ts->win[i]);
	    break;
	}

    if (w)
    {
	CompWindow *cw;

    	for (cw = w->prev; cw; cw = cw->prev)
	{
	    if (isTrailfocusWindow (w))
	    {
		ts->win[winMax - 1] = cw->id;
		break;
	    }
	}
    }

    return s;
}

/* Walks through the existing stack and removes windows that should
 * (no longer) be there. Used for option-change.
 */
static void
cleanList (CompScreen *s)
{
    CompWindow *w;
    int        i, j, length;
    int        winMax;

    TRAILFOCUS_SCREEN (s);

    winMax = trailfocusGetWindowsCount (s);

    for (i = 0; i < winMax; i++)
    {
	w = findWindowAtScreen (s, ts->win[i]);
	if (!w || !isTrailfocusWindow (w))
	{
	    ts->win[i] = 0;
	}
    }

    length = winMax;
    for (i = 0; i < length; i++)
    {
	if (!ts->win[i])
	{
	    for (j = i; j < length - 1; j++)
		ts->win[j] = ts->win[j + 1];
	    length--;
	}
    }
    for (; length < winMax; length++)
	ts->win[length] = 0;

    pushWindow (s->display, s->display->activeWindow);

    /* make sure that enough windows are in the list */
    for (i = 0; i < winMax; i++)
	if (!ts->win[i])
	    break;

    if (i < winMax)
    {
	w = s->windows;
	if (w)
	    w = w->next;

	for (; w && (i < winMax); w = w->next)
	{
	    if (!isTrailfocusWindow (w))
		continue;

	    for (j = 0; j < winMax; j++)
		if (w->id == ts->win[j])
		    break;

	    if (j < winMax)
		continue;

	    ts->win[i++] = w->id;
	}
    }
}

/* Handles the event if it was a FocusIn event.  */
static void
trailfocusHandleEvent (CompDisplay *d,
		       XEvent      *event)
{
    CompScreen *s;

    TRAILFOCUS_DISPLAY (d);

    switch (event->type) {
    case FocusIn:
	s = pushWindow (d, event->xfocus.window);
	if (s)
    	    setWindows (s);
	break;
    case DestroyNotify:
	s = popWindow (d, event->xdestroywindow.window);
	if (s)
	    setWindows (s);
	break;
    case PropertyNotify:
	if (event->xproperty.atom == d->desktopViewportAtom)
	{
	    s = findScreenAtDisplay (d, event->xproperty.window);
	    if (s)
	    {
	    	cleanList (s);
		setWindows (s);
	    }
	}
	break;
    default:
	break;
    }

    UNWRAP (td, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (td, d, handleEvent, trailfocusHandleEvent);
}

/* Settings changed. Reallocate rs->inc and re-populate it and the
 * rest of the TrailfocusScreen (-wMask).
 */
static void
recalculateAttributes (CompScreen *s)
{
    TfAttrib tmp, min, max;
    int      i;
    int      start;
    int      winMax;

    TRAILFOCUS_SCREEN (s);

    start = trailfocusGetWindowsStart (s) - 1;
    winMax = trailfocusGetWindowsCount (s);

    if (start >= winMax)
    {
	compLogMessage ("trailfocus", CompLogLevelWarn,
			"Attempting to define start higher than max windows.");
	start = winMax - 1;
    }

    min.opacity = trailfocusGetMinOpacity (s) * OPAQUE / 100;
    min.brightness = trailfocusGetMinBrightness (s) * BRIGHT / 100;
    min.saturation = trailfocusGetMinSaturation (s) * COLOR / 100;
    max.opacity = trailfocusGetMaxOpacity (s) * OPAQUE / 100;
    max.brightness = trailfocusGetMaxBrightness (s) * BRIGHT / 100;
    max.saturation = trailfocusGetMaxSaturation (s) * COLOR / 100;

    ts->win = realloc (ts->win, sizeof (Window) * (winMax + 1));
    ts->inc = realloc (ts->inc, sizeof (TfAttrib) * (winMax + 1));

    tmp.opacity = (max.opacity - min.opacity) / ((winMax - start));
    tmp.brightness = (max.brightness - min.brightness) / ((winMax - start));
    tmp.saturation = (max.saturation - min.saturation) / ((winMax - start));

    for (i = 0; i < start; ++i)
	ts->inc[i] = max;

    for (i = 0; i + start <= winMax; i++)
    {
	ts->inc[i + start].opacity = max.opacity - (tmp.opacity * i);
	ts->inc[i + start].brightness = max.brightness - (tmp.brightness * i);
	ts->inc[i + start].saturation = max.saturation - (tmp.saturation * i);
	ts->win[i + start] = 0;
    }
}

static Bool
trailfocusPaintWindow (CompWindow              *w,
		       const WindowPaintAttrib *attrib,
		       const CompTransform     *transform,
		       Region                  region,
		       unsigned int            mask)
{
    Bool status;

    TRAILFOCUS_WINDOW (w);
    TRAILFOCUS_SCREEN (w->screen);

    if (tw->isTfWindow)
    {
	WindowPaintAttrib wAttrib = *attrib;

	wAttrib.opacity = MIN (attrib->opacity, tw->attribs.opacity);
	wAttrib.brightness = MIN (attrib->brightness, tw->attribs.brightness);
	wAttrib.saturation = MIN (attrib->saturation, tw->attribs.saturation);

	UNWRAP (ts, w->screen, paintWindow);
	status = (*w->screen->paintWindow) (w, &wAttrib, transform,
					    region, mask);
	WRAP (ts, w->screen, paintWindow, trailfocusPaintWindow);
    }
    else
    {
	UNWRAP (ts, w->screen, paintWindow);
	status = (*w->screen->paintWindow) (w, attrib, transform, region, mask);
	WRAP (ts, w->screen, paintWindow, trailfocusPaintWindow);
    }

    return status;
}

/* Configuration, initialization, boring stuff. ----------------------- */

static void
trailfocusScreenOptionChanged (CompScreen              *s,
			       CompOption              *opt,
			       TrailfocusScreenOptions num)
{
    switch (num) {
    case TrailfocusScreenOptionMinOpacity:
    case TrailfocusScreenOptionMaxOpacity:
    case TrailfocusScreenOptionMinSaturation:
    case TrailfocusScreenOptionMaxSaturation:
    case TrailfocusScreenOptionMinBrightness:
    case TrailfocusScreenOptionMaxBrightness:
    case TrailfocusScreenOptionWindowsStart:
    case TrailfocusScreenOptionWindowsCount:
	recalculateAttributes (s);
	break;
    default:
	break;
    }

    cleanList (s);
    setWindows (s);
}

static Bool
trailfocusInitWindow (CompPlugin *p,
		      CompWindow *w)
{
    TrailfocusWindow *tw;

    TRAILFOCUS_SCREEN (w->screen);

    tw = calloc (1, sizeof (TrailfocusWindow));
    if (!tw)
	return FALSE;

    w->base.privates[ts->windowPrivateIndex].ptr = tw;

    tw->isTfWindow = FALSE;

    return TRUE;
}

static void
trailfocusFiniWindow (CompPlugin *p,
		      CompWindow *w)
{
    TRAILFOCUS_WINDOW (w);

    free (tw);
}

/* Remember to populate the TrailFocus screen properly, and push the
 * active window on the stack, then set windows.
 */
static Bool
trailfocusInitScreen (CompPlugin *p,
		      CompScreen *s)
{
    TrailfocusScreen *ts;
    int              i, start;

    TRAILFOCUS_DISPLAY(s->display);

    ts = calloc (1, sizeof (TrailfocusScreen));
    if (!ts)
	return FALSE;

    ts->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (ts->windowPrivateIndex < 0)
    {
	free (ts);
	return FALSE;
    }

    trailfocusSetWindowMatchNotify (s, trailfocusScreenOptionChanged);
    trailfocusSetWindowsCountNotify (s, trailfocusScreenOptionChanged);
    trailfocusSetWindowsStartNotify (s, trailfocusScreenOptionChanged);
    trailfocusSetMinOpacityNotify (s, trailfocusScreenOptionChanged);
    trailfocusSetMaxOpacityNotify (s, trailfocusScreenOptionChanged);
    trailfocusSetMinSaturationNotify (s, trailfocusScreenOptionChanged);
    trailfocusSetMaxSaturationNotify (s, trailfocusScreenOptionChanged);
    trailfocusSetMinBrightnessNotify (s, trailfocusScreenOptionChanged);
    trailfocusSetMaxBrightnessNotify (s, trailfocusScreenOptionChanged);

    s->base.privates[td->screenPrivateIndex].ptr = ts;

    WRAP (ts, s, paintWindow, trailfocusPaintWindow);

    recalculateAttributes (s);
    start = trailfocusGetWindowsStart (s) - 1;
    for (i = 0; i < start; i++)
	ts->win[i] = 0;

    pushWindow (s->display, s->display->activeWindow);
    ts->setupTimerHandle = compAddTimeout (0, 0, setupTimeout, s);

    return TRUE;
}

/* Remember to reset windows to some sane value when we unload */
static void
trailfocusFiniScreen (CompPlugin *p,
		      CompScreen *s)
{
    TRAILFOCUS_SCREEN (s);

    if (ts->setupTimerHandle)
	compRemoveTimeout (ts->setupTimerHandle);

    if (ts->win)
	free (ts->win);
    if (ts->inc)
	free (ts->inc);

    UNWRAP (ts, s, paintWindow);

    free (ts);
}

static Bool
trailfocusInitDisplay (CompPlugin  *p,
		       CompDisplay *d)
{
    TrailfocusDisplay *td;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    td = malloc (sizeof (TrailfocusDisplay));
    if (!td)
	return FALSE;

    td->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (td->screenPrivateIndex < 0)
    {
	free (td);
	return FALSE;
    }

    d->base.privates[displayPrivateIndex].ptr = td;

    WRAP (td, d, handleEvent, trailfocusHandleEvent);

    return TRUE;
}

static void
trailfocusFiniDisplay (CompPlugin  *p,
		       CompDisplay *d)
{
    TRAILFOCUS_DISPLAY (d);

    UNWRAP (td, d, handleEvent);

    freeScreenPrivateIndex (d, td->screenPrivateIndex);
    free (td);
}

static CompBool
trailfocusInitObject (CompPlugin *p,
		      CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) trailfocusInitDisplay,
	(InitPluginObjectProc) trailfocusInitScreen,
	(InitPluginObjectProc) trailfocusInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
trailfocusFiniObject (CompPlugin *p,
		      CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) trailfocusFiniDisplay,
	(FiniPluginObjectProc) trailfocusFiniScreen,
	(FiniPluginObjectProc) trailfocusFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}



static Bool
trailfocusInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
trailfocusFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable trailfocusVTable = {
    "trailfocus",
    0,
    trailfocusInit,
    trailfocusFini,
    trailfocusInitObject,
    trailfocusFiniObject,
    0,
    0
};

CompPluginVTable*
getCompPluginInfo (void)
{
    return &trailfocusVTable;
}
