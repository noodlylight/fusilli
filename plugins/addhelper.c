/**
 * Compiz ADD Helper. Makes it easier to concentrate.
 *
 * Copyright (c) 2007 Kristian Lyngstøl <kristian@beryl-project.org>
 * Ported and highly modified by Patrick Niklaus <marex@beryl-project.org>
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

#include <compiz-core.h>
#include "addhelper_options.h"

#define GET_ADD_DISPLAY(d)                            \
    ((AddHelperDisplay *) (d)->base.privates[displayPrivateIndex].ptr)
#define ADD_DISPLAY(d)                                \
    AddHelperDisplay *ad = GET_ADD_DISPLAY (d)
#define GET_ADD_SCREEN(s, ad)                         \
    ((AddHelperScreen *) (s)->base.privates[(ad)->screenPrivateIndex].ptr)
#define ADD_SCREEN(s)                                 \
    AddHelperScreen *as = GET_ADD_SCREEN (s, GET_ADD_DISPLAY (s->display))
#define GET_ADD_WINDOW(w, as) \
    ((AddHelperWindow *) (w)->base.privates[ (as)->windowPrivateIndex].ptr)
#define ADD_WINDOW(w) \
    AddHelperWindow *aw = GET_ADD_WINDOW (w,          \
			  GET_ADD_SCREEN  (w->screen, \
			  GET_ADD_DISPLAY (w->screen->display)))

static int displayPrivateIndex;

typedef struct _AddHelperDisplay
{
    int screenPrivateIndex;

    GLushort opacity;
    GLushort brightness;
    GLushort saturation;

    Bool   toggle;

    HandleEventProc handleEvent;
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
walkWindows (CompDisplay *d)
{
    CompScreen *s;
    CompWindow *w;

    ADD_DISPLAY (d);

    for (s = d->screens; s; s = s->next)
    {
	for (w = s->windows; w; w = w->next)
	{
	    ADD_WINDOW (w);

	    aw->dim = FALSE;

	    if (!ad->toggle)
		continue;

	    if (w->id == d->activeWindow)
		continue;

	    if (w->invisible || w->destroyed || w->hidden || w->minimized)
		continue;

	    if (!matchEval (addhelperGetWindowTypes (d), w))
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

    ADD_DISPLAY (s->display);
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

/* Takes the inital event. 
 * This checks for focus change and acts on it.
 */
static void
addhelperHandleEvent (CompDisplay *d,
		      XEvent      *event)
{
    Window activeWindow = d->activeWindow;

    ADD_DISPLAY (d);

    UNWRAP(ad, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP(ad, d, handleEvent, addhelperHandleEvent);

    if (!ad->toggle)
	return;

    if (activeWindow != d->activeWindow)
	walkWindows (d);
}

/* Configuration, initialization, boring stuff. ----------------------- */

/* Takes the action and toggles us.
*/
static Bool
addhelperToggle (CompDisplay     *d,
		 CompAction      *action,
		 CompActionState state,
		 CompOption      *option,
		 int             nOption)
{
    ADD_DISPLAY (d);
    ad->toggle = !ad->toggle;
    walkWindows (d);

    return TRUE;
}

/* Change notify for bcop */
static void
addhelperDisplayOptionChanged (CompDisplay             *d,
			       CompOption              *opt,
			       AddhelperDisplayOptions num)
{
    ADD_DISPLAY (d);

    switch (num) {
    case AddhelperDisplayOptionBrightness:
	ad->brightness = (addhelperGetBrightness(d) * 0xffff) / 100;
	break;
    case AddhelperDisplayOptionSaturation:
	ad->saturation = (addhelperGetSaturation(d) * 0xffff) / 100;
	break;
    case AddhelperDisplayOptionOpacity:
	ad->opacity = (addhelperGetOpacity(d) * 0xffff) / 100;
	break;
    case AddhelperDisplayOptionOnoninit:
	ad->toggle = addhelperGetOnoninit (d);
	break;
    default:
	break;
    }
}

static Bool
addhelperInitWindow (CompPlugin *p,
		     CompWindow *w)
{
    AddHelperWindow *aw;

    ADD_SCREEN (w->screen);
    ADD_DISPLAY (w->screen->display);

    aw = malloc (sizeof (AddHelperWindow));
    if (!aw)
	return FALSE;

    w->base.privates[as->windowPrivateIndex].ptr = aw;

    if (ad->toggle && 
	w->id != w->screen->display->activeWindow &&
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

    ADD_DISPLAY (s->display);

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

    s->base.privates[ad->screenPrivateIndex].ptr = as;

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

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    ad = malloc (sizeof (AddHelperDisplay));
    if (!ad)
	return FALSE;

    ad->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (ad->screenPrivateIndex < 0)
    {
	free (ad);
	return FALSE;
    }

    d->base.privates[displayPrivateIndex].ptr = ad;

    addhelperSetToggleKeyInitiate (d, addhelperToggle);
    addhelperSetBrightnessNotify (d, addhelperDisplayOptionChanged);
    addhelperSetOpacityNotify (d, addhelperDisplayOptionChanged);
    addhelperSetSaturationNotify (d, addhelperDisplayOptionChanged);
    addhelperSetOnoninitNotify (d, addhelperDisplayOptionChanged);

    ad->brightness = (addhelperGetBrightness (d) * BRIGHT) / 100;
    ad->opacity = (addhelperGetOpacity (d) * OPAQUE) / 100;
    ad->saturation = (addhelperGetSaturation (d) * COLOR) / 100;
    ad->toggle = addhelperGetOnoninit (d);

    WRAP (ad, d, handleEvent, addhelperHandleEvent);

    return TRUE;
}

static void
addhelperFiniDisplay (CompPlugin  *p,
		      CompDisplay *d)
{
    ADD_DISPLAY (d);

    UNWRAP (ad, d, handleEvent);

    freeScreenPrivateIndex (d, ad->screenPrivateIndex);
    free (ad);
}

static CompBool
addhelperInitObject (CompPlugin *p,
		     CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) addhelperInitDisplay,
	(InitPluginObjectProc) addhelperInitScreen,
	(InitPluginObjectProc) addhelperInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
addhelperFiniObject (CompPlugin *p,
		     CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) addhelperFiniDisplay,
	(FiniPluginObjectProc) addhelperFiniScreen,
	(FiniPluginObjectProc) addhelperFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
addhelperInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
	return FALSE;
    return TRUE;
}

static void
addhelperFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

CompPluginVTable addhelperVTable = {
    "addhelper",
    0,
    addhelperInit,
    addhelperFini,
    addhelperInitObject,
    addhelperFiniObject,
    0,
    0
};

CompPluginVTable*
getCompPluginInfo (void)
{
    return &addhelperVTable;
}
