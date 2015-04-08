/*
 *
 * Compiz title bar information extension plugin
 *
 * titleinfo.c
 *
 * Copyright : (C) 2009 by Danny Baumann
 * E-mail    : maniac@compiz.org
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

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <X11/Xatom.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <compiz-core.h>
#include "titleinfo_options.h"

static int TitleinfoDisplayPrivateIndex;

typedef struct _TitleinfoDisplay {
    int screenPrivateIndex;

    Atom visibleNameAtom;
    Atom wmPidAtom;

    HandleEventProc handleEvent;
} TitleinfoDisplay;

typedef struct _TitleinfoScreen {
    int windowPrivateIndex;

    AddSupportedAtomsProc addSupportedAtoms;
} TitleinfoScreen;

typedef struct _TitleinfoWindow {
    char *title;
    char *remoteMachine;
    int  owner;
} TitleinfoWindow;

#define TITLEINFO_DISPLAY(display) PLUGIN_DISPLAY(display, Titleinfo, t)
#define TITLEINFO_SCREEN(screen) PLUGIN_SCREEN(screen, Titleinfo, t)
#define TITLEINFO_WINDOW(window) PLUGIN_WINDOW(window, Titleinfo, t)

static void
titleinfoUpdateVisibleName (CompWindow *w)
{
    CompDisplay *d = w->screen->display;
    char        *text = NULL, *machine = NULL;
    const char  *root = "", *title;

    TITLEINFO_DISPLAY (d);
    TITLEINFO_WINDOW (w);

    title = tw->title ? tw->title : "";

    if (titleinfoGetShowRoot (w->screen) && tw->owner == 0)
	root = "ROOT: ";

    if (titleinfoGetShowRemoteMachine (w->screen) && tw->remoteMachine)
    {
	char hostname[256];

	if (gethostname (hostname, 256) || strcmp (hostname, tw->remoteMachine))
	    machine = tw->remoteMachine;
    }

    if (machine)
	asprintf (&text, "%s%s (@%s)", root, title, machine);
    else if (root[0])
	asprintf (&text, "%s%s", root, title);

    if (text)
    {
	XChangeProperty (d->display, w->id, td->visibleNameAtom,
			 d->utf8StringAtom, 8, PropModeReplace,
			 (unsigned char *) text, strlen (text));
	free (text);
    }
    else
    {
	XDeleteProperty (d->display, w->id, td->visibleNameAtom);
    }
}

static void
titleinfoUpdatePid (CompWindow *w)
{
    CompDisplay   *d = w->screen->display;
    int           pid = -1;
    Atom          type;
    int           result, format;
    unsigned long nItems, bytesAfter;
    unsigned char *propVal;

    TITLEINFO_DISPLAY (d);
    TITLEINFO_WINDOW (w);

    tw->owner = -1;

    result = XGetWindowProperty (d->display, w->id, td->wmPidAtom,
				 0L, 1L, False, XA_CARDINAL, &type,
				 &format, &nItems, &bytesAfter, &propVal);

    if (result == Success && propVal)
    {
	if (nItems)
	{
	    unsigned long value;

	    memcpy (&value, propVal, sizeof (unsigned long));
	    pid = value;
	}

	XFree (propVal);
    }

    if (pid >= 0)
    {
	char        path[512];
	struct stat fileStat;

	snprintf (path, 512, "/proc/%d", pid);
	if (!lstat (path, &fileStat))
	    tw->owner = fileStat.st_uid;
    }

    if (titleinfoGetShowRoot (w->screen))
	titleinfoUpdateVisibleName (w);
}

static char *
titleinfoGetUtf8Property (CompDisplay *d,
			  Window      id,
			  Atom        atom)
{
    Atom          type;
    int           result, format;
    unsigned long nItems, bytesAfter;
    char          *val, *retval = NULL;

    result = XGetWindowProperty (d->display, id, atom, 0L, 65536, False,
				 d->utf8StringAtom, &type, &format, &nItems,
				 &bytesAfter, (unsigned char **) &val);

    if (result != Success)
	return NULL;

    if (type == d->utf8StringAtom && format == 8 && val && nItems > 0)
    {
	retval = malloc (sizeof (char) * (nItems + 1));
	if (retval)
	{
	    strncpy (retval, val, nItems);
	    retval[nItems] = 0;
	}
    }

    if (val)
	XFree (val);

    return retval;
}

static char *
titleinfoGetTextProperty (CompDisplay *d,
			  Window      id,
			  Atom        atom)
{
    XTextProperty text;
    char          *retval = NULL;

    text.nitems = 0;
    if (XGetTextProperty (d->display, id, &text, atom))
    {
        if (text.value)
	{
	    retval = malloc (sizeof (char) * (text.nitems + 1));
	    if (retval)
	    {
		strncpy (retval, (char *) text.value, text.nitems);
		retval[text.nitems] = 0;
	    }

	    XFree (text.value);
	}
    }

    return retval;
}

static void
titleinfoUpdateTitle (CompWindow *w)
{
    CompDisplay *d = w->screen->display;
    char        *title;

    TITLEINFO_WINDOW (w);

    title = titleinfoGetUtf8Property (d, w->id, d->wmNameAtom);

    if (!title)
	title = titleinfoGetTextProperty (d, w->id, XA_WM_NAME);

    if (tw->title)
	free (tw->title);

    tw->title = title;
    titleinfoUpdateVisibleName (w);
}


static void
titleinfoUpdateMachine (CompWindow *w)
{
    TITLEINFO_WINDOW (w);

    if (tw->remoteMachine)
	free (tw->remoteMachine);

    tw->remoteMachine = titleinfoGetTextProperty (w->screen->display, w->id,
						  XA_WM_CLIENT_MACHINE);

    if (titleinfoGetShowRemoteMachine (w->screen))
	titleinfoUpdateVisibleName (w);
}

static unsigned int
titleinfoAddSupportedAtoms (CompScreen   *s,
			    Atom         *atoms,
			    unsigned int size)
{
    unsigned int count;

    TITLEINFO_DISPLAY (s->display);
    TITLEINFO_SCREEN (s);

    UNWRAP (ts, s, addSupportedAtoms);
    count = (*s->addSupportedAtoms) (s, atoms, size);
    WRAP (ts, s, addSupportedAtoms, titleinfoAddSupportedAtoms);

    if ((size - count) >= 2)
    {
	atoms[count++] = td->visibleNameAtom;
	atoms[count++] = td->wmPidAtom;
    }

    return count;
}

static void
titleinfoHandleEvent (CompDisplay *d,
		      XEvent      *event)
{
    TITLEINFO_DISPLAY (d);

    UNWRAP (td, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (td, d, handleEvent, titleinfoHandleEvent);

    if (event->type == PropertyNotify)
    {
	CompWindow *w;

	if (event->xproperty.atom == XA_WM_CLIENT_MACHINE)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		titleinfoUpdateMachine (w);
	}
	else if (event->xproperty.atom == td->wmPidAtom)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		titleinfoUpdatePid (w);
	}
	else if (event->xproperty.atom == d->wmNameAtom ||
		 event->xproperty.atom == XA_WM_NAME)
	{
	    w = findWindowAtDisplay (d, event->xproperty.window);
	    if (w)
		titleinfoUpdateTitle (w);
	}
    }
}

static Bool
titleinfoInitDisplay (CompPlugin  *p,
		      CompDisplay *d)
{
    TitleinfoDisplay *td;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    td = malloc (sizeof (TitleinfoDisplay));
    if (!td)
	return FALSE;

    td->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (td->screenPrivateIndex < 0)
    {
	free (td);
	return FALSE;
    }

    td->visibleNameAtom = XInternAtom (d->display, "_NET_WM_VISIBLE_NAME", 0);
    td->wmPidAtom       = XInternAtom (d->display, "_NET_WM_PID", 0);

    WRAP (td, d, handleEvent, titleinfoHandleEvent);

    d->base.privates[TitleinfoDisplayPrivateIndex].ptr = td;

    return TRUE;
}

static void
titleinfoFiniDisplay (CompPlugin  *p,
		      CompDisplay *d)
{
    TITLEINFO_DISPLAY (d);

    freeScreenPrivateIndex (d, td->screenPrivateIndex);

    UNWRAP (td, d, handleEvent);

    free (td);
}

static Bool
titleinfoInitScreen (CompPlugin *p,
		     CompScreen *s)
{
    TitleinfoScreen *ts;

    TITLEINFO_DISPLAY (s->display);

    ts = malloc (sizeof (TitleinfoScreen));
    if (!ts)
	return FALSE;

    ts->windowPrivateIndex = allocateWindowPrivateIndex (s);
    if (ts->windowPrivateIndex < 0) 
    {
	free (ts);
	return FALSE;
    }

    s->base.privates[td->screenPrivateIndex].ptr = ts;

    WRAP (ts, s, addSupportedAtoms, titleinfoAddSupportedAtoms);

    return TRUE;
}

static void
titleinfoFiniScreen (CompPlugin *p,
		     CompScreen *s)
{
    TITLEINFO_SCREEN (s);

    UNWRAP (ts, s, addSupportedAtoms);

    freeWindowPrivateIndex (s, ts->windowPrivateIndex);

    free (ts);
}

static Bool
titleinfoInitWindow (CompPlugin *p,
		     CompWindow *w)
{
    TitleinfoWindow *tw;

    TITLEINFO_SCREEN (w->screen);

    tw = malloc (sizeof (TitleinfoWindow));
    if (!tw)
	return FALSE;

    tw->remoteMachine = NULL;
    tw->title         = NULL;
    tw->owner         = -1;

    w->base.privates[ts->windowPrivateIndex].ptr = tw;

    titleinfoUpdateTitle (w);
    titleinfoUpdateMachine (w);
    titleinfoUpdatePid (w);
    titleinfoUpdateVisibleName (w);

    return TRUE;
}

static void
titleinfoFiniWindow (CompPlugin *p,
		     CompWindow *w)
{
    TITLEINFO_WINDOW (w);

    if (tw->title)
	free (tw->title);

    if (tw->remoteMachine)
	free (tw->remoteMachine);

    tw->remoteMachine = NULL;
    titleinfoUpdateVisibleName (w);

    free (tw);
}

static CompBool
titleinfoInitObject (CompPlugin *p,
		     CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) titleinfoInitDisplay,
	(InitPluginObjectProc) titleinfoInitScreen,
	(InitPluginObjectProc) titleinfoInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
titleinfoFiniObject (CompPlugin *p,
		     CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) titleinfoFiniDisplay,
	(FiniPluginObjectProc) titleinfoFiniScreen,
	(FiniPluginObjectProc) titleinfoFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
titleinfoInit (CompPlugin *p)
{
    TitleinfoDisplayPrivateIndex = allocateDisplayPrivateIndex ();
    if (TitleinfoDisplayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
titleinfoFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (TitleinfoDisplayPrivateIndex);
}

CompPluginVTable titleinfoVTable = {
    "titleinfo",
    0,
    titleinfoInit,
    titleinfoFini,
    titleinfoInitObject,
    titleinfoFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &titleinfoVTable;
}

