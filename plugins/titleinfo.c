/*
 *
 * Compiz title bar information extension plugin
 *
 * titleinfo.c
 *
 * Copyright : (C) 2009 by Danny Baumann
 * E-mail    : maniac@compiz.org
 *
 * Copyright : (C) 2015 by Michail Bitzes
 * E-mail    : noodlylight@gmail.com
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

#include <fusilli-core.h>

static int bananaIndex;

static int displayPrivateIndex;

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
	int owner;
} TitleinfoWindow;

#define GET_TITLEINFO_DISPLAY(d) \
	((TitleinfoDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define TITLEINFO_DISPLAY(d) \
	TitleinfoDisplay *td = GET_TITLEINFO_DISPLAY (d)

#define GET_TITLEINFO_SCREEN(s, td) \
	((TitleinfoScreen *) (s)->privates[(td)->screenPrivateIndex].ptr)

#define TITLEINFO_SCREEN(s) \
	TitleinfoScreen *ts = GET_TITLEINFO_SCREEN (s, \
	                                          GET_TITLEINFO_DISPLAY (&display))

#define GET_TITLEINFO_WINDOW(w, ts) \
	((TitleinfoWindow *) (w)->privates[(ts)->windowPrivateIndex].ptr)

#define TITLEINFO_WINDOW(w) \
	TitleinfoWindow *tw = GET_TITLEINFO_WINDOW  (w, \
	                                             GET_TITLEINFO_SCREEN  (w->screen, \
	                                                                 GET_TITLEINFO_DISPLAY (&display)))

static void
titleinfoUpdateVisibleName (CompWindow *w)
{
	CompDisplay *d = &display;
	char        *text = NULL, *machine = NULL;
	const char  *root = "", *title;

	TITLEINFO_DISPLAY (d);
	TITLEINFO_WINDOW (w);

	title = tw->title ? tw->title : "";

	const BananaValue *
	option_show_root = bananaGetOption (bananaIndex,
	                                    "show_root",
	                                    w->screen->screenNum);

	const BananaValue *
	option_show_remote_machine = bananaGetOption (bananaIndex,
	                                              "show_remote_machine",
	                                              w->screen->screenNum);

	if (option_show_root->b && tw->owner == 0)
		root = "ROOT: ";

	if (option_show_remote_machine->b && tw->remoteMachine)
	{
		char hostname[256];

		if (gethostname (hostname, 256) || strcmp (hostname, tw->remoteMachine))
			machine = tw->remoteMachine;
	}

	if (machine)
	{
#pragma GCC diagnostic ignored "-Wunused-variable"
		int retval = asprintf (&text, "%s%s (@%s)", root, title, machine);
	}
	else if (root[0])
	{
#pragma GCC diagnostic ignored "-Wunused-variable"
		int retval = asprintf (&text, "%s%s", root, title);
	}

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
	CompDisplay   *d = &display;
	int pid = -1;
	Atom type;
	int result, format;
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
		char path[512];
		struct stat fileStat;

		snprintf (path, 512, "/proc/%d", pid);
		if (!lstat (path, &fileStat))
			tw->owner = fileStat.st_uid;
	}

	const BananaValue *
	option_show_root = bananaGetOption (bananaIndex,
	                                    "show_root",
	                                    w->screen->screenNum);

	if (option_show_root->b)
		titleinfoUpdateVisibleName (w);
}

static char *
titleinfoGetUtf8Property (CompDisplay *d,
                          Window      id,
                          Atom        atom)
{
	Atom type;
	int result, format;
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
	CompDisplay *d = &display;
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

	tw->remoteMachine = titleinfoGetTextProperty (&display, w->id,
	                                              XA_WM_CLIENT_MACHINE);

	const BananaValue *
	option_show_remote_machine = bananaGetOption (bananaIndex,
	                                              "show_remote_machine",
	                                              w->screen->screenNum);

	if (option_show_remote_machine->b)
		titleinfoUpdateVisibleName (w);
}

static unsigned int
titleinfoAddSupportedAtoms (CompScreen   *s,
                            Atom         *atoms,
                            unsigned int size)
{
	unsigned int count;

	TITLEINFO_DISPLAY (&display);
	TITLEINFO_SCREEN (s);

	UNWRAP (ts, s, addSupportedAtoms);
	count = (*s->addSupportedAtoms)(s, atoms, size);
	WRAP (ts, s, addSupportedAtoms, titleinfoAddSupportedAtoms);

	if ((size - count) >= 2)
	{
		atoms[count++] = td->visibleNameAtom;
		atoms[count++] = td->wmPidAtom;
	}

	return count;
}

static void
titleinfoHandleEvent (XEvent      *event)
{
	TITLEINFO_DISPLAY (&display);

	UNWRAP (td, &display, handleEvent);
	(display.handleEvent) (event);
	WRAP (td, &display, handleEvent, titleinfoHandleEvent);

	if (event->type == PropertyNotify)
	{
		CompWindow *w;

		if (event->xproperty.atom == XA_WM_CLIENT_MACHINE)
		{
			w = findWindowAtDisplay (event->xproperty.window);

			if (w)
				titleinfoUpdateMachine (w);
		}
		else if (event->xproperty.atom == td->wmPidAtom)
		{
			w = findWindowAtDisplay (event->xproperty.window);

			if (w)
				titleinfoUpdatePid (w);
		}
		else if (event->xproperty.atom == display.wmNameAtom ||
		         event->xproperty.atom == XA_WM_NAME)
		{
			w = findWindowAtDisplay (event->xproperty.window);

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

	td = malloc (sizeof (TitleinfoDisplay));
	if (!td)
		return FALSE;

	td->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (td->screenPrivateIndex < 0)
	{
		free (td);
		return FALSE;
	}

	td->visibleNameAtom = XInternAtom (d->display, "_NET_WM_VISIBLE_NAME", 0);
	td->wmPidAtom       = XInternAtom (d->display, "_NET_WM_PID", 0);

	WRAP (td, d, handleEvent, titleinfoHandleEvent);

	d->privates[displayPrivateIndex].ptr = td;

	return TRUE;
}

static void
titleinfoFiniDisplay (CompPlugin  *p,
                      CompDisplay *d)
{
	TITLEINFO_DISPLAY (d);

	freeScreenPrivateIndex (td->screenPrivateIndex);

	UNWRAP (td, d, handleEvent);

	free (td);
}

static Bool
titleinfoInitScreen (CompPlugin *p,
                     CompScreen *s)
{
	TitleinfoScreen *ts;

	TITLEINFO_DISPLAY (&display);

	ts = malloc (sizeof (TitleinfoScreen));
	if (!ts)
		return FALSE;

	ts->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (ts->windowPrivateIndex < 0)
	{
		free (ts);
		return FALSE;
	}

	s->privates[td->screenPrivateIndex].ptr = ts;

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

	w->privates[ts->windowPrivateIndex].ptr = tw;

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

static Bool
titleinfoInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("titleinfo", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("titleinfo");

	if (bananaIndex == -1)
		return FALSE;

	return TRUE;
}

static void
titleinfoFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable titleinfoVTable = {
	"titleinfo",
	titleinfoInit,
	titleinfoFini,
	titleinfoInitDisplay,
	titleinfoFiniDisplay,
	titleinfoInitScreen,
	titleinfoFiniScreen,
	titleinfoInitWindow,
	titleinfoFiniWindow
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &titleinfoVTable;
}

