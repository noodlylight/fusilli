/**
 * Compiz ADD Helper. Makes it easier to concentrate.
 * *
 * Copyright (c) 2007 Kristian Lyngstøl <kristian@beryl-project.org>
 * Ported and highly modified by Patrick Niklaus <marex@beryl-project.org>
 *
 * Copyright (c) 2015 Michail Bitzes <noodlylight@gmail.com>
 * Port to fusilli
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
 * This plugin provides a toggle-feature that dims all but the active
 * window. This makes it easier for people with lousy concentration
 * to focus. Like me.
 * 
 * Please note any major changes to the code in this header with who you
 * are and what you did. 
 *
 */

#include <string.h>
#include <fusilli-core.h>

#define GET_ADD_DISPLAY(d) \
        ((AddHelperDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define ADD_DISPLAY(d) \
        AddHelperDisplay *ad = GET_ADD_DISPLAY (d)

#define GET_ADD_SCREEN(s, ad) \
        ((AddHelperScreen *) (s)->privates[(ad)->screenPrivateIndex].ptr)

#define ADD_SCREEN(s) \
        AddHelperScreen *as = GET_ADD_SCREEN (s, GET_ADD_DISPLAY (&display))

#define GET_ADD_WINDOW(w, as) \
        ((AddHelperWindow *) (w)->privates[(as)->windowPrivateIndex].ptr)

#define ADD_WINDOW(w) \
        AddHelperWindow *aw = GET_ADD_WINDOW (w,          \
                              GET_ADD_SCREEN  (w->screen, \
                              GET_ADD_DISPLAY (&display)))

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _AddHelperDisplay
{
	int screenPrivateIndex;

	GLushort opacity;
	GLushort brightness;
	GLushort saturation;

	Bool   toggle;

	HandleEventProc handleEvent;

	CompKeyBinding toggle_key;

	CompMatch window_types;
} AddHelperDisplay;

typedef struct _AddHelperScreen
{
	int windowPrivateIndex;

	PaintWindowProc paintWindow;
} AddHelperScreen;

typedef struct _AddHelperWindow
{
	Bool dim;
} AddHelperWindow;

/* Walk through all windows of the screen and adjust them if they
 * are not the active window. If reset is true, this will reset
 * the windows, including the active. Otherwise, it will dim 
 * and reset the active. 
 */
static void
walkWindows (void)
{
	CompScreen *s;
	CompWindow *w;

	ADD_DISPLAY (&display);

	for (s = display.screens; s; s = s->next)
	{
		for (w = s->windows; w; w = w->next)
		{
			ADD_WINDOW (w);

			aw->dim = FALSE;

			if (!ad->toggle)
				continue;

			if (w->id == display.activeWindow)
				continue;

			if (w->invisible || w->destroyed || w->hidden || w->minimized)
				continue;

			if (!matchEval (&ad->window_types, w))
				continue;

			aw->dim = TRUE;
		}

		damageScreen (s);
	}
}

/* Checks if the window is dimmed and, if so, paints it with the modified
 * paint attributes.
 */
static Bool
addhelperPaintWindow (CompWindow              *w,
                      const WindowPaintAttrib *attrib,
                      const CompTransform     *transform,
                      Region                  region,
                      unsigned int            mask)
{
	Bool       status;
	CompScreen *s = w->screen;

	ADD_DISPLAY (&display);
	ADD_SCREEN (s);
	ADD_WINDOW (w);

	if (aw->dim)
	{
		/* copy the paint attribute */
		WindowPaintAttrib wAttrib = *attrib;

		/* applies the lowest value */
		wAttrib.opacity = MIN (attrib->opacity, ad->opacity);
		wAttrib.brightness = MIN (attrib->brightness, ad->brightness);
		wAttrib.saturation = MIN (attrib->saturation, ad->saturation);

		/* continue painting with the modified attribute */
		UNWRAP (as, s, paintWindow);
		status = (*s->paintWindow) (w, &wAttrib, transform, region, mask);
		WRAP (as, s, paintWindow, addhelperPaintWindow);
	}
	else
	{
		/* the window is not dimmed, so it's painted normal */
		UNWRAP (as, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP (as, s, paintWindow, addhelperPaintWindow);
	}

	return status;
}

static void
addhelperHandleEvent (XEvent      *event)
{
	Window activeWindow = display.activeWindow;

	ADD_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &ad->toggle_key))
		{
			ad->toggle = !ad->toggle;
			walkWindows ();
		}
		break;
	default:
		break;
	}

	UNWRAP(ad, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP(ad, &display, handleEvent, addhelperHandleEvent);

	if (!ad->toggle)
		return;

	if (activeWindow != display.activeWindow)
		walkWindows ();
}

static Bool
addhelperInitWindow (CompPlugin *p,
                     CompWindow *w)
{
	AddHelperWindow *aw;

	ADD_SCREEN (w->screen);
	ADD_DISPLAY (&display);

	aw = malloc (sizeof (AddHelperWindow));
	if (!aw)
		return FALSE;

	w->privates[as->windowPrivateIndex].ptr = aw;

	if (ad->toggle && 
	         w->id != display.activeWindow &&
	         !w->attrib.override_redirect)
		aw->dim = TRUE;
	else
		aw->dim = FALSE;

	return TRUE;
}

static void
addhelperFiniWindow (CompPlugin *p,
                     CompWindow *w)
{
	ADD_WINDOW (w);

	free (aw);
}

static Bool
addhelperInitScreen (CompPlugin *p,
                     CompScreen *s)
{
	AddHelperScreen *as;

	ADD_DISPLAY (&display);

	as = malloc (sizeof (AddHelperScreen));
	if (!as)
		return FALSE;

	as->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (as->windowPrivateIndex < 0)
	{
		free (as);
		return FALSE;
	}

	WRAP (as, s, paintWindow, addhelperPaintWindow);

	s->privates[ad->screenPrivateIndex].ptr = as;

	return TRUE;
}

static void
addhelperFiniScreen (CompPlugin *p,
                     CompScreen *s)
{
	ADD_SCREEN (s);

	UNWRAP (as, s, paintWindow);

	free (as);
}

static Bool
addhelperInitDisplay (CompPlugin  *p,
                      CompDisplay *d)
{
	AddHelperDisplay *ad;

	ad = malloc (sizeof (AddHelperDisplay));
	if (!ad)
		return FALSE;

	ad->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (ad->screenPrivateIndex < 0)
	{
		free (ad);
		return FALSE;
	}

	d->privates[displayPrivateIndex].ptr = ad;

	const BananaValue *
	option_window_types = bananaGetOption (bananaIndex,
	                                       "window_types",
	                                       -1);

	matchInit (&ad->window_types);
	matchAddFromString (&ad->window_types, option_window_types->s);
	matchUpdate (&ad->window_types);

	const BananaValue *
	option_brightness = bananaGetOption (bananaIndex,
	                                     "brightness",
	                                     -1);

	const BananaValue *
	option_opacity = bananaGetOption (bananaIndex,
	                                  "opacity",
	                                  -1);

	const BananaValue *
	option_saturation = bananaGetOption (bananaIndex,
	                                     "saturation",
	                                     -1);

	const BananaValue *
	option_ononinit = bananaGetOption (bananaIndex,
	                                   "ononinit",
	                                   -1);

	ad->brightness = (option_brightness->i * BRIGHT) / 100;
	ad->opacity = (option_opacity->i * OPAQUE) / 100;
	ad->saturation = (option_saturation->i * COLOR) / 100;
	ad->toggle = option_ononinit->b;

	const BananaValue *
	option_toggle_key = bananaGetOption (bananaIndex,
	                                     "toggle_key",
	                                     -1);

	registerKey (option_toggle_key->s, &ad->toggle_key);

	WRAP (ad, d, handleEvent, addhelperHandleEvent);

	return TRUE;
}

static void
addhelperFiniDisplay (CompPlugin  *p,
                      CompDisplay *d)
{
	ADD_DISPLAY (d);

	UNWRAP (ad, d, handleEvent);

	freeScreenPrivateIndex (ad->screenPrivateIndex);

	free (ad);
}

static void
addhelperChangeNotify (const char        *optionName,
                       BananaType        optionType,
                       const BananaValue *optionValue,
                       int               screenNum)
{
	ADD_DISPLAY (&display);

	if (strcasecmp (optionName, "toggle_key") == 0)
		updateKey (optionValue->s, &ad->toggle_key);

	else if (strcasecmp (optionName, "brightness") == 0)
		ad->brightness = (optionValue->i * 0xffff) / 100;

	else if (strcasecmp (optionName, "saturation") == 0)
		ad->saturation = (optionValue->i * 0xffff) / 100;

	else if (strcasecmp (optionName, "opacity") == 0)
		ad->opacity = (optionValue->i * 0xffff) / 100;

	else if (strcasecmp (optionName, "ononinit") == 0)
		ad->toggle = optionValue->b;

	else if (strcasecmp (optionName, "window_types") == 0)
	{
		matchFini (&ad->window_types);
		matchInit (&ad->window_types);
		matchAddFromString (&ad->window_types, optionValue->s);
		matchUpdate (&ad->window_types);
	}
}

static Bool
addhelperInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("addhelper", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("addhelper");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, addhelperChangeNotify);

	return TRUE;
}

static void
addhelperFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable addhelperVTable = {
	"addhelper",
	addhelperInit,
	addhelperFini,
	addhelperInitDisplay,
	addhelperFiniDisplay,
	addhelperInitScreen,
	addhelperFiniScreen,
	addhelperInitWindow,
	addhelperFiniWindow
};

CompPluginVTable*
getCompPluginInfo20141205 (void)
{
	return &addhelperVTable;
}
