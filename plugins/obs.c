/*
 * Copyright © 2008 Danny Baumann
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Danny Baumann not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Danny Baumann makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * DANNY BAUMANN DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL DENNIS KASPRZYK BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: Danny Baumann <dannybaumann@web.de>
 *         Michail Bitzes <noodlylight@gmail.com>
 */
#include <string.h>

#include <fusilli-core.h>

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _ObsDisplay
{
	int screenPrivateIndex;

	HandleEventProc            handleEvent;
	MatchPropertyChangedProc   matchPropertyChanged;
} ObsDisplay;


#define MODIFIER_OPACITY     0
#define MODIFIER_BRIGHTNESS  1
#define MODIFIER_SATURATION  2
#define MODIFIER_COUNT       3

char *modifierNames[MODIFIER_COUNT] = { "opacity", "brightness", "saturation" };

static CompKeyBinding increase_key[MODIFIER_COUNT];
static CompButtonBinding increase_button[MODIFIER_COUNT];

static CompKeyBinding decrease_key[MODIFIER_COUNT];
static CompButtonBinding decrease_button[MODIFIER_COUNT];

#define MAX_LIST_LENGTH 100

typedef struct _ObsScreen
{
	int windowPrivateIndex;

	PaintWindowProc paintWindow;
	DrawWindowProc  drawWindow;

	int step[MODIFIER_COUNT];

	CompMatch match[MODIFIER_COUNT][MAX_LIST_LENGTH];
	int value[MODIFIER_COUNT][MAX_LIST_LENGTH];
	int nValue[MODIFIER_COUNT];
} ObsScreen;

typedef struct _ObsWindow
{
	int customFactor[MODIFIER_COUNT];
	int matchFactor[MODIFIER_COUNT];

	CompTimeoutHandle updateHandle;
} ObsWindow;

#define GET_OBS_DISPLAY(d) \
        ((ObsDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define OBS_DISPLAY(d) \
        ObsDisplay *od = GET_OBS_DISPLAY (d)

#define GET_OBS_SCREEN(s, od) \
        ((ObsScreen *) (s)->privates[(od)->screenPrivateIndex].ptr)

#define OBS_SCREEN(s) \
        ObsScreen *os = GET_OBS_SCREEN (s, GET_OBS_DISPLAY (&display))

#define GET_OBS_WINDOW(w, os) \
        ((ObsWindow *) (w)->privates[(os)->windowPrivateIndex].ptr)

#define OBS_WINDOW(w) \
        ObsWindow *ow = GET_OBS_WINDOW  (w, \
                        GET_OBS_SCREEN  (w->screen, \
                        GET_OBS_DISPLAY (&display)))

static void
changePaintModifier (CompWindow *w,
                     int        modifier,
                     int        direction)
{
	int value;

	OBS_SCREEN (w->screen);
	OBS_WINDOW (w);

	if (w->attrib.override_redirect)
		return;

	if (modifier == MODIFIER_OPACITY && (w->type & CompWindowTypeDesktopMask))
		return;

	value = ow->customFactor[modifier];
	value += os->step[modifier] * direction;

	value = MIN (value, 100);
	value = MAX (value, os->step[modifier]);

	if (value != ow->customFactor[modifier])
	{
		ow->customFactor[modifier] = value;
		addWindowDamage (w);
	}
}


static void
updatePaintModifier (CompWindow *w,
                     int        modifier)
{
	int lastFactor;

	OBS_WINDOW (w);
	OBS_SCREEN (w->screen);

	lastFactor = ow->customFactor[modifier];

	if ((w->type & CompWindowTypeDesktopMask) && (modifier == MODIFIER_OPACITY))
	{
		ow->customFactor[modifier] = 100;
		ow->matchFactor[modifier]  = 100;
	}
	else
	{
		int        i, lastMatchFactor;

		lastMatchFactor           = ow->matchFactor[modifier];
		ow->matchFactor[modifier] = 100;

		for (i = 0; i < os->nValue[modifier]; i++)
		{
			if (matchEval (&os->match[modifier][i], w))
			{
				ow->matchFactor[modifier] = os->value[modifier][i];
				break;
			}
		}

		if (ow->customFactor[modifier] == lastMatchFactor)
			ow->customFactor[modifier] = ow->matchFactor[modifier];
	}

	if (ow->customFactor[modifier] != lastFactor)
		addWindowDamage (w);
}

static Bool
alterPaintModifier (BananaArgument   *arg,
                    int              nArg)
{
	CompWindow *w;
	Window     xid;

	BananaValue *window = getArgNamed ("window", arg, nArg);

	if (window != NULL)
		xid = window->i;
	else
		xid = 0;

	w   = findTopLevelWindowAtDisplay (xid);

	if (w)
	{
		BananaValue *modifier = getArgNamed ("modifier", arg, nArg);
		if (modifier == NULL)
			return FALSE;

		BananaValue *direction = getArgNamed ("direction", arg, nArg);
		if (direction == NULL)
			return FALSE;

		changePaintModifier (w, modifier->i, direction->i);
	}

	return TRUE;
}

static void
obsHandleEvent (XEvent      *event)
{
	OBS_DISPLAY (&display);

	int i;

	switch (event->type) {
	case KeyPress:
		for (i = 0; i < MODIFIER_COUNT; i++)
		{
			if (isKeyPressEvent (event, &increase_key[i]))
			{
				BananaArgument arg[3];

				arg[0].name = "window";
				arg[0].type = BananaInt;
				arg[0].value.i = display.activeWindow;

				arg[1].name = "modifier";
				arg[1].type = BananaInt;
				arg[1].value.i = i;

				arg[2].name = "direction";
				arg[2].type = BananaInt;
				arg[2].value.i = 1;

				alterPaintModifier (arg, 3);
				break;
			}
			else if (isKeyPressEvent (event, &decrease_key[i]))
			{
				BananaArgument arg[3];

				arg[0].name = "window";
				arg[0].type = BananaInt;
				arg[0].value.i = display.activeWindow;

				arg[1].name = "modifier";
				arg[1].type = BananaInt;
				arg[1].value.i = i;

				arg[2].name = "direction";
				arg[2].type = BananaInt;
				arg[2].value.i = -1;

				alterPaintModifier (arg, 3);
				break;
			}
		}
		break;
	case ButtonPress:
		for (i = 0; i < MODIFIER_COUNT; i++)
		{
			if (isButtonPressEvent (event, &(increase_button[i])))
			{
				BananaArgument arg[3];

				arg[0].name = "window";
				arg[0].type = BananaInt;
				arg[0].value.i = event->xbutton.window;

				arg[1].name = "modifier";
				arg[1].type = BananaInt;
				arg[1].value.i = i;

				arg[2].name = "direction";
				arg[2].type = BananaInt;
				arg[2].value.i = 1;

				alterPaintModifier (arg, 3);
				break;
			}
			else if (isButtonPressEvent (event, &decrease_button[i]))
			{
				BananaArgument arg[3];

				arg[0].name = "window";
				arg[0].type = BananaInt;
				arg[0].value.i = event->xbutton.window;

				arg[1].name = "modifier";
				arg[1].type = BananaInt;
				arg[1].value.i = i;

				arg[2].name = "direction";
				arg[2].type = BananaInt;
				arg[2].value.i = -1;

				alterPaintModifier (arg, 3);
				break;
			}
		}
		break;
	}

	UNWRAP (od, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (od, &display, handleEvent, obsHandleEvent);
}

static Bool
obsPaintWindow (CompWindow              *w,
                const WindowPaintAttrib *attrib,
                const CompTransform     *transform,
                Region                  region,
                unsigned int            mask)
{
	CompScreen *s = w->screen;
	Bool       status;

	OBS_SCREEN (s);
	OBS_WINDOW (w);

	if (ow->customFactor[MODIFIER_OPACITY] != 100)
		mask |= PAINT_WINDOW_TRANSLUCENT_MASK;

	UNWRAP (os, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP (os, s, paintWindow, obsPaintWindow);

	return status;
}

/* Note: Normally plugins should wrap into PaintWindow to modify opacity,
         brightness and saturation. As some plugins bypass paintWindow when
         they draw windows and our custom values always need to be applied,
         we wrap into DrawWindow here */

static Bool
obsDrawWindow (CompWindow           *w,
               const CompTransform  *transform,
               const FragmentAttrib *attrib,
               Region               region,
               unsigned int         mask)
{
	CompScreen *s = w->screen;
	Bool       hasCustomFactor = FALSE;
	Bool       status;
	int        i;

	OBS_SCREEN (s);
	OBS_WINDOW (w);

	for (i = 0; i < MODIFIER_COUNT; i++)
		if (ow->customFactor[i] != 100)
		{
			hasCustomFactor = TRUE;
			break;
		}

	if (hasCustomFactor)
	{
		FragmentAttrib fragment = *attrib;
		int            factor;

		factor = ow->customFactor[MODIFIER_OPACITY];
		if (factor != 100)
		{
			fragment.opacity = (int) fragment.opacity * factor / 100;
			mask |= PAINT_WINDOW_TRANSLUCENT_MASK;
		}

		factor = ow->customFactor[MODIFIER_BRIGHTNESS];
		if (factor != 100)
			fragment.brightness = (int) fragment.brightness * factor / 100;

		factor = ow->customFactor[MODIFIER_SATURATION];
		if (factor != 100)
			fragment.saturation = (int) fragment.saturation * factor / 100;

		UNWRAP (os, s, drawWindow);
		status = (*s->drawWindow) (w, transform, &fragment, region, mask);
		WRAP (os, s, drawWindow, obsDrawWindow);
	}
	else
	{
		UNWRAP (os, s, drawWindow);
		status = (*s->drawWindow) (w, transform, attrib, region, mask);
		WRAP (os, s, drawWindow, obsDrawWindow);
	}

	return status;
}

static void
obsMatchPropertyChanged (CompWindow  *w)
{
	int i;

	OBS_DISPLAY (&display);

	for (i = 0; i < MODIFIER_COUNT; i++)
		updatePaintModifier (w, i);

	UNWRAP (od, &display, matchPropertyChanged);
	(*display.matchPropertyChanged) (w);
	WRAP (od, &display, matchPropertyChanged, obsMatchPropertyChanged);
}

static Bool
obsUpdateWindow (void *closure)
{
	CompWindow *w = (CompWindow *) closure;
	int        i;

	OBS_WINDOW (w);

	for (i = 0; i < MODIFIER_COUNT; i++)
		updatePaintModifier (w, i);

	ow->updateHandle = 0;

	return FALSE;
}

static CompBool
obsInitDisplay (CompPlugin  *p,
                CompDisplay *d)
{
	ObsDisplay *od;

	od = malloc (sizeof (ObsDisplay));
	if (!od)
		return FALSE;

	od->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (od->screenPrivateIndex < 0)
	{
		free (od);
		return FALSE;
	}

	WRAP (od, d, matchPropertyChanged, obsMatchPropertyChanged);
	WRAP (od, d, handleEvent, obsHandleEvent);

	d->privates[displayPrivateIndex].ptr = od;

	return TRUE;
}

static void
obsFiniDisplay (CompPlugin  *p,
                CompDisplay *d)
{
	OBS_DISPLAY (d);

	UNWRAP (od, d, matchPropertyChanged);

	freeScreenPrivateIndex (od->screenPrivateIndex);

	free (od);
}

static CompBool
obsInitScreen (CompPlugin *p,
               CompScreen *s)
{
	ObsScreen  *os;

	OBS_DISPLAY (&display);

	os = malloc (sizeof (ObsScreen));
	if (!os)
		return FALSE;

	os->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (os->windowPrivateIndex < 0)
	{
		free (os);
		return FALSE;
	}

	s->privates[od->screenPrivateIndex].ptr = os;

	int i;

	for (i = 0; i < MODIFIER_COUNT; i++)
	{
		char name[50];
		const BananaValue *opt, *opt2;

		sprintf (name, "%s_step", modifierNames[i]);
		opt = bananaGetOption (bananaIndex, name, s->screenNum);
		os->step[i] = opt->i;

		sprintf (name, "%s_matches", modifierNames[i]);
		opt = bananaGetOption (bananaIndex, name, s->screenNum);

		sprintf (name, "%s_values", modifierNames[i]);
		opt2 = bananaGetOption (bananaIndex, name, s->screenNum);

		os->nValue[i] = MIN (opt->list.nItem, opt2->list.nItem);

		int j;

		for (j = 0; j < os->nValue[i]; j++)
		{
			matchInit (&os->match[i][j]);
			matchAddFromString (&os->match[i][j], opt->list.item[j].s);
			matchUpdate (&os->match[i][j]);

			os->value[i][j] = opt2->list.item[j].i;
		}
	}

	WRAP (os, s, paintWindow, obsPaintWindow);
	WRAP (os, s, drawWindow, obsDrawWindow);

	return TRUE;
}

static void
obsFiniScreen (CompPlugin *p,
               CompScreen *s)
{
	OBS_SCREEN (s);

	UNWRAP (os, s, paintWindow);
	UNWRAP (os, s, drawWindow);

	damageScreen (s);

	int i, j;
	for (i = 0; i < MODIFIER_COUNT; i++)
		for (j = 0; j < os->nValue[i]; j++)
			matchFini (&os->match[i][j]);

	free (os);
}

static CompBool
obsInitWindow (CompPlugin *p,
               CompWindow *w)
{
	ObsWindow *ow;
	int       i;

	OBS_SCREEN (w->screen);

	ow = malloc (sizeof (ObsWindow));
	if (!ow)
		return FALSE;

	for (i = 0; i < MODIFIER_COUNT; i++)
	{
		ow->customFactor[i] = 100;
		ow->matchFactor[i]  = 100;
	}

	/* defer initializing the factors from window matches as match evalution
	   means wrapped function calls */
	ow->updateHandle = compAddTimeout (0, 0, obsUpdateWindow, w);

	w->privates[os->windowPrivateIndex].ptr = ow;

	return TRUE;
}

static void
obsFiniWindow (CompPlugin *p,
               CompWindow *w)
{
	OBS_WINDOW (w);

	if (ow->updateHandle)
		compRemoveTimeout (ow->updateHandle);

	free (ow);
}

static void
obsChangeNotify (const char        *optionName,
                 BananaType        optionType,
                 const BananaValue *optionValue,
                 int               screenNum)
{
	int i;

	for (i = 0; i < MODIFIER_COUNT; i++)
	{
		char name[50];

		sprintf (name, "%s_increase_key", modifierNames[i]);
		if (strcasecmp (optionName, name) == 0)
		{
			updateKey (optionValue->s, &increase_key[i]);
			break;
		}

		sprintf (name, "%s_increase_button", modifierNames[i]);
		if (strcasecmp (optionName, name) == 0)
		{
			updateButton (optionValue->s, &increase_button[i]);
			break;
		}

		sprintf (name, "%s_decrease_key", modifierNames[i]);
		if (strcasecmp (optionName, name) == 0)
		{
			updateKey (optionValue->s, &decrease_key[i]);
			break;
		}

		sprintf (name, "%s_decrease_button", modifierNames[i]);
		if (strcasecmp (optionName, name) == 0)
		{
			updateButton (optionValue->s, &decrease_button[i]);
			break;
		}

		sprintf (name, "%s_step", modifierNames[i]);
		if (strcasecmp (optionName, name) == 0)
		{
			CompScreen *screen = getScreenFromScreenNum (screenNum);

			OBS_SCREEN (screen);

			os->step[i] = optionValue->i;
		}

		sprintf (name, "%s_values", modifierNames[i]);
		char name2[50];
		sprintf (name2, "%s_matches", modifierNames[i]);
		if (strcasecmp (optionName, name) == 0 || 
		    strcasecmp (optionName, name2) == 0)
		{
			CompScreen *screen = getScreenFromScreenNum (screenNum);

			OBS_SCREEN (screen);

			int j;

			for (j = 0; j < os->nValue[i]; j++)
				matchFini (&os->match[i][j]);

			const BananaValue *
			option_matches = bananaGetOption (bananaIndex, name2, screenNum);

			const BananaValue *
			option_values = bananaGetOption (bananaIndex, name, screenNum);

			os->nValue[i] = MIN (option_matches->list.nItem,
			                     option_values->list.nItem);

			for (j = 0; j < os->nValue[i]; j++)
			{
				matchInit (&os->match[i][j]);
				matchAddFromString (&os->match[i][j], option_matches->list.item[j].s);
				matchUpdate (&os->match[i][j]);
				os->value[i][j] = option_values->list.item[j].i;
			}

			CompWindow *w;

			for (w = screen->windows; w; w = w->next)
				updatePaintModifier (w, i);
		}
	}
}

static CompBool
obsInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("obs", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("obs");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, obsChangeNotify);

	char name[50];

	int i;

	for (i = 0; i < MODIFIER_COUNT; i++)
	{
		const BananaValue *opt;

		sprintf (name, "%s_increase_key", modifierNames[i]);
		opt = bananaGetOption (bananaIndex, name, -1);
		registerKey (opt->s, &increase_key[i]);

		sprintf (name, "%s_increase_button", modifierNames[i]);
		opt = bananaGetOption (bananaIndex, name, -1);
		registerButton (opt->s, &increase_button[i]);

		sprintf (name, "%s_decrease_key", modifierNames[i]);
		opt = bananaGetOption (bananaIndex, name, -1);
		registerKey (opt->s, &decrease_key[i]);

		sprintf (name, "%s_decrease_button", modifierNames[i]);
		opt = bananaGetOption (bananaIndex, name, -1);
		registerButton (opt->s, &decrease_button[i]);
	}

	return TRUE;
}

static void
obsFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

CompPluginVTable obsVTable = {
	"obs",
	obsInit,
	obsFini,
	NULL, /* obsInitCore */
	NULL, /* obsFiniCore */
	obsInitDisplay,
	obsFiniDisplay,
	obsInitScreen,
	obsFiniScreen,
	obsInitWindow,
	obsFiniWindow
};

CompPluginVTable *
getCompPluginInfo20141130 (void)
{
	return &obsVTable;
}

