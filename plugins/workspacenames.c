/*
 *
 * Compiz workspace name display plugin
 *
 * workspacenames.c
 *
 * Copyright : (C) 2008 by Danny Baumann
 * E-mail    : maniac@compiz-fusion.org
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <compiz-core.h>
#include <compiz-text.h>
#include "workspacenames_options.h"

#define PI 3.1415926

static int WSNamesDisplayPrivateIndex;

typedef struct _WSNamesDisplay {
    int screenPrivateIndex;

    HandleEventProc handleEvent;

    TextFunc *textFunc;
} WSNamesDisplay;

typedef struct _WSNamesScreen {
    PreparePaintScreenProc preparePaintScreen;
    DonePaintScreenProc    donePaintScreen;
    PaintOutputProc        paintOutput;

    CompTextData *textData;

    CompTimeoutHandle timeoutHandle;
    int               timer;
} WSNamesScreen;

#define WSNAMES_DISPLAY(d) PLUGIN_DISPLAY(d, WSNames, w)
#define WSNAMES_SCREEN(s) PLUGIN_SCREEN(s, WSNames, w)

static void
wsnamesFreeText (CompScreen *s)
{
    WSNAMES_SCREEN (s);
    WSNAMES_DISPLAY (s->display);

    if (!ws->textData)
	return;

    (wd->textFunc->finiTextData) (s, ws->textData);
    ws->textData = NULL;
}

static char *
wsnamesGetCurrentWSName (CompScreen *s)
{
    int           currentVp;
    int           listSize, i;
    CompListValue *names;
    CompListValue *vpNumbers;

    vpNumbers = workspacenamesGetViewports (s);
    names     = workspacenamesGetNames (s);

    currentVp = s->y * s->hsize + s->x + 1;
    listSize  = MIN (vpNumbers->nValue, names->nValue);

    for (i = 0; i < listSize; i++)
	if (vpNumbers->value[i].i == currentVp)
	    return names->value[i].s;

    return NULL;
}

static void
wsnamesRenderNameText (CompScreen *s)
{
    CompTextAttrib attrib;
    char           *name;
    int            ox1, ox2, oy1, oy2;

    WSNAMES_SCREEN (s);
    WSNAMES_DISPLAY (s->display);

    wsnamesFreeText (s);

    name = wsnamesGetCurrentWSName (s);
    if (!name)
	return;

    getCurrentOutputExtents (s, &ox1, &oy1, &ox2, &oy2);

    /* 75% of the output device as maximum width */
    attrib.maxWidth  = (ox2 - ox1) * 3 / 4;
    attrib.maxHeight = 100;

    attrib.family = "Sans";
    attrib.size = workspacenamesGetTextFontSize (s);

    attrib.color[0] = workspacenamesGetFontColorRed (s);
    attrib.color[1] = workspacenamesGetFontColorGreen (s);
    attrib.color[2] = workspacenamesGetFontColorBlue (s);
    attrib.color[3] = workspacenamesGetFontColorAlpha (s);

    attrib.flags = CompTextFlagWithBackground | CompTextFlagEllipsized;
    if (workspacenamesGetBoldText (s))
	attrib.flags |= CompTextFlagStyleBold;

    attrib.bgHMargin = 15;
    attrib.bgVMargin = 15;
    attrib.bgColor[0] = workspacenamesGetBackColorRed (s);
    attrib.bgColor[1] = workspacenamesGetBackColorGreen (s);
    attrib.bgColor[2] = workspacenamesGetBackColorBlue (s);
    attrib.bgColor[3] = workspacenamesGetBackColorAlpha (s);

    ws->textData = (wd->textFunc->renderText) (s, name, &attrib);
}

static void
wsnamesDrawText (CompScreen *s)
{
    GLfloat   alpha;
    int       ox1, ox2, oy1, oy2;
    float     x, y, border = 10.0f;

    WSNAMES_SCREEN (s);
    WSNAMES_DISPLAY (s->display);

    getCurrentOutputExtents (s, &ox1, &oy1, &ox2, &oy2);

    x = ox1 + ((ox2 - ox1) / 2) - (ws->textData->width / 2);

    /* assign y (for the lower corner!) according to the setting */
    switch (workspacenamesGetTextPlacement (s))
    {
	case TextPlacementCenteredOnScreen:
	    y = oy1 + ((oy2 - oy1) / 2) + (ws->textData->height / 2);
	    break;
	case TextPlacementTopOfScreen:
	case TextPlacementBottomOfScreen:
	    {
		XRectangle workArea;
		getWorkareaForOutput (s, s->currentOutputDev, &workArea);

	    	if (workspacenamesGetTextPlacement (s) ==
		    TextPlacementTopOfScreen)
    		    y = oy1 + workArea.y + (2 * border) + ws->textData->height;
		else
		    y = oy1 + workArea.y + workArea.height - (2 * border);
	    }
	    break;
	default:
	    return;
	    break;
    }

    if (ws->timer)
	alpha = ws->timer / (workspacenamesGetFadeTime (s) * 1000.0f);
    else
	alpha = 1.0f;

    (wd->textFunc->drawText) (s, ws->textData, floor (x), floor (y), alpha);
}

static Bool
wsnamesPaintOutput (CompScreen		    *s,
		    const ScreenPaintAttrib *sAttrib,
		    const CompTransform	    *transform,
		    Region		    region,
		    CompOutput		    *output,
		    unsigned int	    mask)
{
    Bool status;

    WSNAMES_SCREEN (s);

    UNWRAP (ws, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
    WRAP (ws, s, paintOutput, wsnamesPaintOutput);

    if (ws->textData)
    {
	CompTransform sTransform = *transform;

	transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);
	glPushMatrix ();
	glLoadMatrixf (sTransform.m);

	wsnamesDrawText (s);

	glPopMatrix ();
    }
	
    return status;
}

static void
wsnamesPreparePaintScreen (CompScreen *s,
			   int	      msSinceLastPaint)
{
    WSNAMES_SCREEN (s);

    if (ws->timer)
    {
	ws->timer -= msSinceLastPaint;
	ws->timer = MAX (ws->timer, 0);

	if (!ws->timer)
	    wsnamesFreeText (s);
    }

    UNWRAP (ws, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (ws, s, preparePaintScreen, wsnamesPreparePaintScreen);
}

static void
wsnamesDonePaintScreen (CompScreen *s)
{
    WSNAMES_SCREEN (s);

    /* FIXME: better only damage paint region */
    if (ws->timer)
	damageScreen (s);

    UNWRAP (ws, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (ws, s, donePaintScreen, wsnamesDonePaintScreen);
}

static Bool
wsnamesHideTimeout (void *closure)
{
    CompScreen *s = (CompScreen *) closure;

    WSNAMES_SCREEN (s);

    ws->timer = workspacenamesGetFadeTime (s) * 1000;
    if (!ws->timer)
	wsnamesFreeText (s);

    damageScreen (s);

    ws->timeoutHandle = 0;

    return FALSE;
}

static void
wsnamesHandleEvent (CompDisplay *d,
		    XEvent      *event)
{
    WSNAMES_DISPLAY (d);

    UNWRAP (wd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (wd, d, handleEvent, wsnamesHandleEvent);

    switch (event->type) {
    case PropertyNotify:
	if (event->xproperty.atom == d->desktopViewportAtom)
	{
	    CompScreen *s;
	    s = findScreenAtDisplay (d, event->xproperty.window);
	    if (s)
	    {
		int timeout;

		WSNAMES_SCREEN (s);

		ws->timer = 0;
		if (ws->timeoutHandle)
		    compRemoveTimeout (ws->timeoutHandle);

		wsnamesRenderNameText (s);
		timeout = workspacenamesGetDisplayTime (s) * 1000;
		ws->timeoutHandle = compAddTimeout (timeout, timeout + 200,
						    wsnamesHideTimeout, s);

		damageScreen (s);
	    }
	}
	break;
    }
}

static Bool
wsnamesInitDisplay (CompPlugin  *p,
		    CompDisplay *d)
{
    WSNamesDisplay *wd;
    int            index;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    if (!checkPluginABI ("text", TEXT_ABIVERSION) ||
	!getPluginDisplayIndex (d, "text", &index))
    {
	return FALSE;
    }

    wd = malloc (sizeof (WSNamesDisplay));
    if (!wd)
	return FALSE;

    wd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (wd->screenPrivateIndex < 0)
    {
	free (wd);
	return FALSE;
    }

    wd->textFunc = d->base.privates[index].ptr;

    WRAP (wd, d, handleEvent, wsnamesHandleEvent);

    d->base.privates[WSNamesDisplayPrivateIndex].ptr = wd;

    return TRUE;
}

static void
wsnamesFiniDisplay (CompPlugin  *p,
		    CompDisplay *d)
{
    WSNAMES_DISPLAY (d);

    freeScreenPrivateIndex (d, wd->screenPrivateIndex);

    UNWRAP (wd, d, handleEvent);

    free (wd);
}

static Bool
wsnamesInitScreen (CompPlugin *p,
		   CompScreen *s)
{
    WSNamesScreen *ws;

    WSNAMES_DISPLAY (s->display);

    ws = malloc (sizeof (WSNamesScreen));
    if (!ws)
	return FALSE;

    ws->textData = NULL;

    ws->timeoutHandle = 0;
    ws->timer         = 0;

    WRAP (ws, s, preparePaintScreen, wsnamesPreparePaintScreen);
    WRAP (ws, s, donePaintScreen, wsnamesDonePaintScreen);
    WRAP (ws, s, paintOutput, wsnamesPaintOutput);

    s->base.privates[wd->screenPrivateIndex].ptr = ws;

    return TRUE;
}

static void
wsnamesFiniScreen (CompPlugin *p,
		   CompScreen *s)
{
    WSNAMES_SCREEN (s);

    UNWRAP (ws, s, preparePaintScreen);
    UNWRAP (ws, s, donePaintScreen);
    UNWRAP (ws, s, paintOutput);

    wsnamesFreeText (s);

    free (ws);
}

static CompBool
wsnamesInitObject (CompPlugin *p,
		   CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) wsnamesInitDisplay,
	(InitPluginObjectProc) wsnamesInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
wsnamesFiniObject (CompPlugin *p,
		   CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) wsnamesFiniDisplay,
	(FiniPluginObjectProc) wsnamesFiniScreen
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
wsnamesInit (CompPlugin *p)
{
    WSNamesDisplayPrivateIndex = allocateDisplayPrivateIndex ();
    if (WSNamesDisplayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
wsnamesFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (WSNamesDisplayPrivateIndex);
}

CompPluginVTable wsnamesVTable = {
    "workspacenames",
    0,
    wsnamesInit,
    wsnamesFini,
    wsnamesInitObject,
    wsnamesFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &wsnamesVTable;
}
