/*
 *
 * Compiz workspace name display plugin
 *
 * wsnames.c
 *
 * Copyright : (C) 2008 by Danny Baumann
 * E-mail    : maniac@compiz-fusion.org
 *
 * Port for fusilli:
 * Copyright : (C) 2014 by Michail Bitzes
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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <fusilli-core.h>
#include <fusilli-text.h>

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _WSNamesDisplay {
	int screenPrivateIndex;

	HandleEventProc handleEvent;
} WSNamesDisplay;

typedef struct _WSNamesScreen {
	PreparePaintScreenProc preparePaintScreen;
	DonePaintScreenProc    donePaintScreen;
	PaintOutputProc        paintOutput;

	CompTextData *textData;

	CompTimeoutHandle timeoutHandle;
	int               timer;
} WSNamesScreen;

#define GET_WSNAMES_DISPLAY(d) \
        ((WSNamesDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define WSNAMES_DISPLAY(d) \
        WSNamesDisplay *wd = GET_WSNAMES_DISPLAY (d)

#define GET_WSNAMES_SCREEN(s, ad) \
        ((WSNamesScreen *) (s)->privates[(ad)->screenPrivateIndex].ptr)

#define WSNAMES_SCREEN(s) \
        WSNamesScreen *ws = GET_WSNAMES_SCREEN (s, GET_WSNAMES_DISPLAY (&display))

static void
dbusComposeNamesList (CompScreen        *s,
                      DBusMessage       *message)
{
	DBusMessageIter iter;
	DBusMessageIter listIter;

	char sig[2];

	sig[0] = DBUS_TYPE_STRING;
	sig[1] = '\0';

	dbus_message_iter_init_append (message, &iter);

	if (dbus_message_iter_open_container (&iter, DBUS_TYPE_ARRAY,
	                                             sig, &listIter))
	{
		int i, j, listSize;

		const BananaValue *
		option_workspace_number = bananaGetOption (bananaIndex,
		                                           "workspace_number",
		                                           s->screenNum);

		const BananaValue *
		option_workspace_name = bananaGetOption (bananaIndex,
		                                         "workspace_name",
		                                         s->screenNum);

		listSize  = MIN (option_workspace_name->list.nItem,
		                 option_workspace_number->list.nItem);

		for (i = 1; i <= s->hsize; i++)
		{
			Bool found = FALSE;

			for (j = 0; j < listSize; j++)
				if (option_workspace_number->list.item[j].i == i)
				{
					found = TRUE;
					dbus_message_iter_append_basic (&listIter, sig[0],
					            &option_workspace_name->list.item[j].s);
					break;
				}

			if (!found)
			{
				char *tmp = malloc(50);
				sprintf (tmp, "Workspace %d", i);
				dbus_message_iter_append_basic (&listIter, sig[0], &tmp);
				free (tmp);
			}
		}
	}

	dbus_message_iter_close_container (&iter, &listIter);
}

static void
sendChangeSignal (CompScreen *s)
{
	DBusMessage *signal;
	DBusMessageIter iter;

	if (core.dbusConnection == NULL)
		return;

	signal = dbus_message_new_signal ("/org/fusilli/wsnames",
	                                  "org.fusilli",
	                                  "namesChanged");

	dbus_message_iter_init_append (signal, &iter);
	dbus_message_iter_append_basic (&iter, DBUS_TYPE_INT32, &s->screenNum);

	dbus_connection_send (core.dbusConnection, signal, NULL);
	dbus_connection_flush (core.dbusConnection);

	dbus_message_unref (signal);
}

static DBusHandlerResult
wsnamesDbusHandleMessage (DBusConnection *connection,
                          DBusMessage    *message,
                          void           *userData)
{
	char **path;

	if (!dbus_message_get_path_decomposed (message, &path))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (!path[0] || !path[1] || !path[2])
	{
		dbus_free_string_array (path);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (!path[3]) //message to /org/fusilli/wsnames
	{
		if (dbus_message_is_method_call (message,
		                                 DBUS_INTERFACE_INTROSPECTABLE,
		                                 "Introspect"))
		{
			dbus_free_string_array (path);
			return DBUS_HANDLER_RESULT_HANDLED;
		}
		else if (dbus_message_is_method_call (message,
		                                      "org.fusilli",
		                                      "getNames"))
		{
			DBusMessage *reply = NULL;

			DBusMessageIter param_iter;
			CompScreen *s;
			int screenNum = -1;

			//read the parameter
			if (dbus_message_iter_init (message, &param_iter))
				if (dbus_message_iter_get_arg_type (&param_iter) == 
				                                            DBUS_TYPE_INT32)
					dbus_message_iter_get_basic (&param_iter, &screenNum);

			s = getScreenFromScreenNum (screenNum);

			if (!s)
			{
				reply = dbus_message_new_error (message,
				        DBUS_ERROR_FAILED,
				        "Invalid or missing parameter");

				dbus_connection_send (connection, reply, NULL);
				dbus_connection_flush (connection);
				dbus_message_unref (reply);
				dbus_free_string_array (path);

				return DBUS_HANDLER_RESULT_HANDLED;
			}

			//give the reply
			reply = dbus_message_new_method_return (message);

			dbusComposeNamesList (s, reply);

			dbus_connection_send (connection, reply, NULL);
			dbus_connection_flush (connection);
			dbus_message_unref (reply);
			dbus_free_string_array (path);

			return DBUS_HANDLER_RESULT_HANDLED;
		}
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusObjectPathVTable wsnamesDbusMessagesVTable = {
	NULL, wsnamesDbusHandleMessage, NULL, NULL, NULL, NULL
};

static void
wsnamesFreeText (CompScreen *s)
{
	WSNAMES_SCREEN (s);

	if (!ws->textData)
		return;

	textFiniTextData (s, ws->textData);
	ws->textData = NULL;
}

static char *
wsnamesGetCurrentWSName (CompScreen *s)
{
	int           currentVp;
	int           listSize, i;

	const BananaValue *
	option_workspace_number = bananaGetOption (bananaIndex,
	                                           "workspace_number",
	                                           s->screenNum);

	const BananaValue *
	option_workspace_name = bananaGetOption (bananaIndex,
	                                         "workspace_name",
	                                         s->screenNum);

	currentVp = s->y * s->hsize + s->x + 1;
	listSize  = MIN (option_workspace_name->list.nItem,
	                 option_workspace_number->list.nItem);

	for (i = 0; i < listSize; i++)
		if (option_workspace_number->list.item[i].i == currentVp)
			return option_workspace_name->list.item[i].s;

	return NULL;
}

static void
wsnamesRenderNameText (CompScreen *s)
{
	CompTextAttrib attrib;
	char           *name;
	int            ox1, ox2, oy1, oy2;

	WSNAMES_SCREEN (s);

	wsnamesFreeText (s);

	name = wsnamesGetCurrentWSName (s);
	if (!name)
		return;

	getCurrentOutputExtents (s, &ox1, &oy1, &ox2, &oy2);

	/* 75% of the output device as maximum width */
	attrib.maxWidth  = (ox2 - ox1) * 3 / 4;
	attrib.maxHeight = 100;

	const BananaValue *
	option_font_size = bananaGetOption (bananaIndex,
	                                    "font_size",
	                                    s->screenNum);

	const BananaValue *
	option_font_family = bananaGetOption(bananaIndex,
	                                     "font_family",
	                                     s->screenNum);

	attrib.family = option_font_family->s;
	attrib.size = option_font_size->i;

	const BananaValue *
	option_font_color = bananaGetOption (bananaIndex,
	                                     "font_color",
	                                     s->screenNum);

	unsigned short color[] = { 0, 0, 0, 0 };

	stringToColor (option_font_color->s, color);

	attrib.color[0] = color[0];
	attrib.color[1] = color[1];
	attrib.color[2] = color[2];
	attrib.color[3] = color[3];

	const BananaValue *
	option_bold_text = bananaGetOption (bananaIndex,
	                                    "bold_text",
	                                    s->screenNum);

	attrib.flags = CompTextFlagWithBackground | CompTextFlagEllipsized;
	if (option_bold_text->b)
		attrib.flags |= CompTextFlagStyleBold;

	attrib.bgHMargin = 15;
	attrib.bgVMargin = 15;

	const BananaValue *
	option_back_color = bananaGetOption (bananaIndex,
	                                     "back_color",
	                                     s->screenNum);

	unsigned short back_color[] = { 0, 0, 0, 0 };

	stringToColor (option_back_color->s, back_color);

	attrib.bgColor[0] = back_color[0];
	attrib.bgColor[1] = back_color[1];
	attrib.bgColor[2] = back_color[2];
	attrib.bgColor[3] = back_color[3];

	ws->textData = textRenderText (s, name, &attrib);
}

#define TEXT_PLACEMENT_CENTERED_ON_SCREEN 0
#define TEXT_PLACEMENT_TOP_OF_SCREEN      1
#define TEXT_PLACEMENET_BOTTOM_OF_SCREEN  2

static void
wsnamesDrawText (CompScreen *s)
{
	GLfloat   alpha;
	int       ox1, ox2, oy1, oy2;
	float     x, y, border = 10.0f;

	WSNAMES_SCREEN (s);

	getCurrentOutputExtents (s, &ox1, &oy1, &ox2, &oy2);

	x = ox1 + ((ox2 - ox1) / 2) - (ws->textData->width / 2);

	const BananaValue *
	option_text_placement = bananaGetOption (bananaIndex,
	                                         "text_placement",
	                                         s->screenNum);

	/* assign y (for the lower corner!) according to the setting */
	switch (option_text_placement->i) {
	case TEXT_PLACEMENT_CENTERED_ON_SCREEN:
		y = oy1 + ((oy2 - oy1) / 2) + (ws->textData->height / 2);
		break;
	case TEXT_PLACEMENT_TOP_OF_SCREEN:
	case TEXT_PLACEMENET_BOTTOM_OF_SCREEN:
		{
		XRectangle workArea;
		getWorkareaForOutput (s, s->currentOutputDev, &workArea);

		if (option_text_placement->i == TEXT_PLACEMENT_TOP_OF_SCREEN)
			y = oy1 + workArea.y + (2 * border) + ws->textData->height;
		else
			y = oy1 + workArea.y + workArea.height - (2 * border);
		}
		break;
	default:
		return;
		break;
	}

	const BananaValue *
	option_fade_time = bananaGetOption (bananaIndex,
	                                    "fade_time",
	                                    s->screenNum);

	if (ws->timer)
		alpha = ws->timer / (option_fade_time->f * 1000.0f);
	else
		alpha = 1.0f;

	textDrawText (s, ws->textData, floor (x), floor (y), alpha);
}

static Bool
wsnamesPaintOutput (CompScreen              *s,
                    const ScreenPaintAttrib *sAttrib,
                    const CompTransform     *transform,
                    Region                  region,
                    CompOutput              *output,
                    unsigned int            mask)
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
                           int        msSinceLastPaint)
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

	const BananaValue *
	option_fade_time = bananaGetOption (bananaIndex,
	                                    "fade_time",
	                                    s->screenNum);

	ws->timer = option_fade_time->f * 1000;
	if (!ws->timer)
		wsnamesFreeText (s);

	damageScreen (s);

	ws->timeoutHandle = 0;

	return FALSE;
}

static void
wsnamesHandleEvent (XEvent      *event)
{
	WSNAMES_DISPLAY (&display);

	UNWRAP (wd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (wd, &display, handleEvent, wsnamesHandleEvent);

	switch (event->type) {
	case PropertyNotify:
		if (event->xproperty.atom == display.desktopViewportAtom)
		{
			CompScreen *s;
			s = findScreenAtDisplay (event->xproperty.window);
			if (s)
			{
				int timeout;

				WSNAMES_SCREEN (s);

				ws->timer = 0;
				if (ws->timeoutHandle)
					compRemoveTimeout (ws->timeoutHandle);

				const BananaValue *
				option_display_time = bananaGetOption (bananaIndex,
				                                       "display_time",
				                                       s->screenNum);

				wsnamesRenderNameText (s);
				timeout = option_display_time->f * 1000;
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

	wd = malloc (sizeof (WSNamesDisplay));
	if (!wd)
		return FALSE;

	wd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (wd->screenPrivateIndex < 0)
	{
		free (wd);
		return FALSE;
	}

	WRAP (wd, d, handleEvent, wsnamesHandleEvent);

	d->privates[displayPrivateIndex].ptr = wd;

	return TRUE;
}

static void
wsnamesFiniDisplay (CompPlugin  *p,
                    CompDisplay *d)
{
	WSNAMES_DISPLAY (d);

	freeScreenPrivateIndex (wd->screenPrivateIndex);

	UNWRAP (wd, d, handleEvent);

	free (wd);
}

static Bool
wsnamesInitScreen (CompPlugin *p,
                   CompScreen *s)
{
	WSNamesScreen *ws;

	WSNAMES_DISPLAY (&display);

	ws = malloc (sizeof (WSNamesScreen));
	if (!ws)
		return FALSE;

	ws->textData = NULL;

	ws->timeoutHandle = 0;
	ws->timer         = 0;

	WRAP (ws, s, preparePaintScreen, wsnamesPreparePaintScreen);
	WRAP (ws, s, donePaintScreen, wsnamesDonePaintScreen);
	WRAP (ws, s, paintOutput, wsnamesPaintOutput);

	s->privates[wd->screenPrivateIndex].ptr = ws;

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

static void
wsnamesChangeNotify (const char        *optionName,
                     BananaType        optionType,
                     const BananaValue *optionValue,
                     int               screenNum)
{
	CompScreen *s;

	if (strcasecmp (optionName, "workspace_name") == 0)
	{
		s = getScreenFromScreenNum (screenNum);
		sendChangeSignal (s);
	}
}

static Bool
wsnamesInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("wsnames", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("wsnames");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, wsnamesChangeNotify);

	if (core.dbusConnection != NULL)
	{
		dbus_connection_register_object_path (core.dbusConnection,
		                                      "/org/fusilli/wsnames",
		                                      &wsnamesDbusMessagesVTable, 0);
	}

	return TRUE;
}

static void
wsnamesFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);

	if (core.dbusConnection != NULL)
	{
		dbus_connection_unregister_object_path (core.dbusConnection,
		                                        "/org/fusilli/wsnames");
	}
}

CompPluginVTable wsnamesVTable = {
	"wsnames",
	wsnamesInit,
	wsnamesFini,
	wsnamesInitDisplay,
	wsnamesFiniDisplay,
	wsnamesInitScreen,
	wsnamesFiniScreen,
	NULL, /* wsnamesInitDisplay */
	NULL  /* wsnamesFiniWindow  */
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &wsnamesVTable;
}
