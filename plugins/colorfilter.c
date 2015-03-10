/*
 * Compiz/Fusion color filtering plugin
 *
 * Author : Guillaume Seguin
 * Email : guillaume@segu.in
 *
 * Copyright (c) 2007 Guillaume Seguin <guillaume@segu.in>
 *
 * Copyright (c) 2015 Michail Bitzes <noodlylight@gmail.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <fusilli-core.h>
#include "colorfilter-parser.h"

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _ColorFilterDisplay {
	int             screenPrivateIndex;
	HandleEventProc handleEvent;

	CompKeyBinding toggle_window_key, toggle_screen_key, switch_filter_key;
} ColorFilterDisplay;

typedef struct _ColorFilterScreen {
	int                     windowPrivateIndex;

	DrawWindowTextureProc   drawWindowTexture;
	WindowAddNotifyProc     windowAddNotify;

	Bool            isFiltered;
	int             currentFilter; /* 0 : cumulative mode
	                                  0 < c <= count : single mode */

	/* The plugin can not immediately load the filters because it needs to
	 * know what texture target it will use : when required, this boolean
	 * is set to TRUE and filters will be loaded on next filtered window
	 * texture painting */
	Bool            filtersLoaded;
	int             *filtersFunctions;
	int             filtersCount;

	CompMatch filter_match, exclude_match;
} ColorFilterScreen;

typedef struct _ColorFilterWindow {
	Bool    isFiltered;
} ColorFilterWindow;

#define GET_FILTER_CORE(c) \
        ((ColorFilterCore *) (c)->privates[corePrivateIndex].ptr)

#define FILTER_CORE(c) \
        ColorFilterCore *fc = GET_FILTER_CORE (c)

#define GET_FILTER_DISPLAY(d) \
        ((ColorFilterDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define FILTER_DISPLAY(d) \
        ColorFilterDisplay *cfd = GET_FILTER_DISPLAY (d)

#define GET_FILTER_SCREEN(s, cfd) \
        ((ColorFilterScreen *) (s)->privates[(cfd)->screenPrivateIndex].ptr)

#define FILTER_SCREEN(s) \
        ColorFilterScreen *cfs = GET_FILTER_SCREEN (s, \
                                 GET_FILTER_DISPLAY (&display))

#define GET_FILTER_WINDOW(w, cfs) \
        ((ColorFilterWindow *) (w)->privates[(cfs)->windowPrivateIndex].ptr)

#define FILTER_WINDOW(w) \
        ColorFilterWindow *cfw = GET_FILTER_WINDOW  (w, \
                                 GET_FILTER_SCREEN  ((w)->screen, \
                                 GET_FILTER_DISPLAY (&display)))

/* Compiz-core imports ------------------------------------------------------ */

/* _CompFunction struct definition (from compiz-core/src/fragment.c)
 * I just keep the beginning of the struct, since I just want the name
 * and importing the remainder would mean importing several other definitions.
 * I guess this is a bit risky though.. maybe should I bring this issue on
 * the mailing list? */
struct _CompFunction {
	struct _CompFunction *next;

	int                  id;
	char                 *name;
};

/*
 * Find fragment function by id (imported from compiz-core/src/fragment.c)
 */
static CompFunction *
findFragmentFunction (CompScreen *s,
                      int        id)
{
	CompFunction *function;

	for (function = s->fragmentFunctions; function; function = function->next)
	{
		if (function->id == id)
			return function;
	}

	return NULL;
}

/* Actions handling functions ----------------------------------------------- */

/*
 * Toggle filtering for a specific window
 */
static void
colorFilterToggleWindow (CompWindow *w)
{
	FILTER_WINDOW (w);
	FILTER_SCREEN (w->screen);

	/* Toggle window filtering flag */
	cfw->isFiltered = !cfw->isFiltered;

	/* Check exclude list */
	if (matchEval (&cfs->exclude_match, w))
		cfw->isFiltered = FALSE;

	/* Ensure window is going to be repainted */
	addWindowDamage (w);
}

/*
 * Toggle filtering for the whole screen
 */
static void
colorFilterToggleScreen (CompScreen *s)
{
	CompWindow *w;

	FILTER_SCREEN (s);

	/* Toggle screen filtering flag */
	cfs->isFiltered = !cfs->isFiltered;

	/* Toggle filtering for every window */
	for (w = s->windows; w; w = w->next)
		if (w)
			colorFilterToggleWindow (w);
}

/*
 * Switch current filter
 */
static void
colorFilterSwitchFilter (CompScreen *s)
{
	int id;
	CompFunction *function;
	CompWindow *w;

	FILTER_SCREEN (s);

	/* % (count + 1) because of the cumulative filters mode */
	cfs->currentFilter++;
	if (cfs->currentFilter >= cfs->filtersCount + 1)
		cfs->currentFilter = 0;

	if (cfs->currentFilter == 0)
		compLogMessage ("colorfilter", CompLogLevelInfo,
		                "Cumulative filters mode");
	else
	{
		id = cfs->filtersFunctions[cfs->currentFilter - 1];

		if (id)
		{
			function = findFragmentFunction (s, id);
			compLogMessage ("colorfilter", CompLogLevelInfo,
			                "Single filter mode (using %s filter)",
			                function->name);
		}
		else
		{
			compLogMessage ("colorfilter", CompLogLevelInfo,
			                "Single filter mode (filter loading failure)");
		}
	}

	/* Damage currently filtered windows */
	for (w = s->windows; w; w = w->next)
	{
		FILTER_WINDOW (w);

		if (cfw->isFiltered)
			addWindowDamage (w);
	}
}

/* Filters handling functions ----------------------------------------------- */

/*
 * Free filters resources if any
 */
static void
unloadFilters (CompScreen *s)
{
	int i;

	FILTER_SCREEN (s);

	if (cfs->filtersFunctions)
	{
		/* Destroy loaded filters one by one */
		for (i = 0; i < cfs->filtersCount; i++)
		{
			if (cfs->filtersFunctions[i])
				destroyFragmentFunction (s, cfs->filtersFunctions[i]);
		}

		free (cfs->filtersFunctions);
		cfs->filtersFunctions = NULL;
		cfs->filtersCount = 0;

		/* Reset current filter */
		cfs->currentFilter = 0;
	}
}

/*
 * Load filters from a list of files for current screen
 */
static int
loadFilters (CompScreen  *s,
             CompTexture *texture)
{
	int i, target, loaded, function, count;
	char *name;
	CompWindow *w;

	FILTER_SCREEN (s);

	cfs->filtersLoaded = TRUE;

	/* Fetch filters filenames */
	const BananaValue *
	option_filters = bananaGetOption (bananaIndex,
	                                  "filters",
	                                  s->screenNum);

	count = option_filters->list.nItem;

	/* The texture target that will be used for some ops */
	if (texture->target == GL_TEXTURE_2D)
		target = COMP_FETCH_TARGET_2D;
	else
		target = COMP_FETCH_TARGET_RECT;

	/* Free previously loaded filters and malloc */
	unloadFilters (s);
	cfs->filtersFunctions = malloc (sizeof (int) * count);

	if (!cfs->filtersFunctions)
		return 0;

	cfs->filtersCount = count;

	/* Load each filter one by one */
	loaded = 0;

	for (i = 0; i < count; i++)
	{
		name = base_name (option_filters->list.item[i].s);
		if (!name || !strlen (name))
		{
			if (name)
				free (name);

			cfs->filtersFunctions[i] = 0;
			continue;
		}

		compLogMessage ("colorfilter", CompLogLevelInfo,
		                "Loading filter %s (item %s).", name,
		                option_filters->list.item[i].s);

		function = loadFragmentProgram (option_filters->list.item[i].s, name, s, target);
		free (name);

		cfs->filtersFunctions[i] = function;

		if (function)
			loaded++;
	}

	/* Warn if there was at least one loading failure */
	if (loaded < count)
		compLogMessage ("colorfilter", CompLogLevelWarn,
		                "Tried to load %d filter(s), %d succeeded.",
		                count, loaded);

	if (!loaded)
		cfs->filtersCount = 0;

	/* Damage currently filtered windows */
	for (w = s->windows; w; w = w->next)
	{
		FILTER_WINDOW (w);

		if (cfw->isFiltered)
			addWindowDamage (w);
	}

	return loaded;
}

/*
 * Wrapper that enables filters if the window is filtered
 */
static void
colorFilterDrawWindowTexture (CompWindow           *w,
                              CompTexture          *texture,
                              const FragmentAttrib *attrib,
                              unsigned int         mask)
{
	int i, function;

	FILTER_SCREEN (w->screen);
	FILTER_WINDOW (w);

	/* Check if filters have to be loaded and load them if so
	 * Maybe should this check be done only if a filter is going to be applied
	 * for this texture? */
	if (!cfs->filtersLoaded)
		loadFilters (w->screen, texture);

	/* Filter texture if :
	 *   o GL_ARB_fragment_program available
	 *   o Filters are loaded
	 *   o Texture's window is filtered */
	/* Note : if required, filter window contents only and not decorations
	 * (use that w->texture->name != texture->name for decorations) */
	const BananaValue *
	option_filter_decorations = bananaGetOption (bananaIndex,
	                                             "filter_decorations",
	                                             w->screen->screenNum);

	if (cfs->filtersCount && cfw->isFiltered &&
	       (option_filter_decorations->b ||
	        (texture->name == w->texture->name)))
	{
		FragmentAttrib fa = *attrib;
		if (cfs->currentFilter == 0) /* Cumulative filters mode */
		{
			/* Enable each filter one by one */
			for (i = 0; i < cfs->filtersCount; i++)
			{
				function = cfs->filtersFunctions[i];

				if (function)
					addFragmentFunction (&fa, function);
			}
		}
		/* Single filter mode */
		else if (cfs->currentFilter <= cfs->filtersCount)
		{
			/* Enable the currently selected filter if possible (i.e. if it
			 * was successfully loaded) */
			function = cfs->filtersFunctions[cfs->currentFilter - 1];

			if (function)
				addFragmentFunction (&fa, function);
		}

		UNWRAP (cfs, w->screen, drawWindowTexture);
		(*w->screen->drawWindowTexture) (w, texture, &fa, mask);
		WRAP (cfs, w->screen, drawWindowTexture, colorFilterDrawWindowTexture);
	}
	else /* Not filtering */
	{
		UNWRAP (cfs, w->screen, drawWindowTexture);
		(*w->screen->drawWindowTexture) (w, texture, attrib, mask);
		WRAP (cfs, w->screen, drawWindowTexture, colorFilterDrawWindowTexture);
	}
}

/*
 * Filter windows when they are open if they match the filtering rules
 */
static void
colorFilterWindowAddNotify (CompWindow *w)
{
	FILTER_SCREEN (w->screen);

	/* cfw->isFiltered is initialized to FALSE in InitWindow, so we only
	   have to toggle it to TRUE if necessary */
	if (cfs->isFiltered && matchEval (&cfs->filter_match, w))
		colorFilterToggleWindow (w);

	UNWRAP (cfs, w->screen, windowAddNotify);
	(*w->screen->windowAddNotify) (w);
	WRAP (cfs, w->screen, windowAddNotify, colorFilterWindowAddNotify);
}

static void
colorFilterHandleEvent (XEvent      *event)
{
	FILTER_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &cfd->toggle_window_key))
		{
			CompWindow *w = findWindowAtDisplay (display.activeWindow);

			if (w && w->screen->fragmentProgram)
				colorFilterToggleWindow (w);
		}
		else if (isKeyPressEvent (event, &cfd->toggle_screen_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (s && s->fragmentProgram)
				colorFilterToggleScreen (s);
		}
		else if (isKeyPressEvent (event, &cfd->switch_filter_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (s && s->fragmentProgram)
				colorFilterSwitchFilter (s);
		}
		break;
	default:
		break;
	}

	UNWRAP (cfd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (cfd, &display, handleEvent, colorFilterHandleEvent);
}

static void
colorFilterChangeNotify (const char        *optionName,
                         BananaType        optionType,
                         const BananaValue *optionValue,
                         int               screenNum)
{
	FILTER_DISPLAY (&display);

	if (strcasecmp (optionName, "toggle_window_key") == 0)
		updateKey (optionValue->s, &cfd->toggle_window_key);

	else if (strcasecmp (optionName, "toggle_screen_key") == 0)
		updateKey (optionValue->s, &cfd->toggle_screen_key);

	else if (strcasecmp (optionName, "switch_filter_key") == 0)
		updateKey (optionValue->s, &cfd->switch_filter_key);

	else if (strcasecmp (optionName, "filter_match") == 0)
	{
		CompWindow *w;
		CompScreen *s = getScreenFromScreenNum (screenNum);

		FILTER_SCREEN (s);

		//Re-check every window against new match settings
		for (w = s->windows; w; w = w->next)
		{
			FILTER_WINDOW (w);

			if (matchEval (&cfs->filter_match, w) &&
			    cfs->isFiltered && !cfw->isFiltered)
			{
				colorFilterToggleWindow (w);
			}
		}
	}

	else if (strcasecmp (optionName, "exclude_match") == 0)
	{
		CompWindow *w;
		CompScreen *s = getScreenFromScreenNum (screenNum);

		FILTER_SCREEN (s);

		// Re-check every window against new match settings
		for (w = s->windows; w; w = w->next)
		{
			Bool isExcluded;

			FILTER_WINDOW (w);

			isExcluded = matchEval (&cfs->exclude_match, w);

			if (isExcluded && cfw->isFiltered)
				colorFilterToggleWindow (w);
			else if (!isExcluded && cfs->isFiltered && !cfw->isFiltered)
				colorFilterToggleWindow (w);
		}
	}

	else if (strcasecmp (optionName, "filters") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);

		FILTER_SCREEN (s);

		/* Just set the filtersLoaded boolean to FALSE, unloadFilters will be
		 * called in loadFilters */

		cfs->filtersLoaded = FALSE;
	}

	else if (strcasecmp (optionName, "filter_decorations") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);

		damageScreen (s);
	}
}

static Bool
colorFilterInitDisplay (CompPlugin  *p,
                        CompDisplay *d)
{
	ColorFilterDisplay *cfd;

	cfd = malloc (sizeof (ColorFilterDisplay));
	if (!cfd)
		return FALSE;

	cfd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (cfd->screenPrivateIndex < 0)
	{
		free (cfd);
		return FALSE;
	}

	WRAP (cfd, d, handleEvent, colorFilterHandleEvent);

	const BananaValue *
	option_toggle_window_key = bananaGetOption (bananaIndex,
	                                            "toggle_window_key",
	                                            -1);

	registerKey (option_toggle_window_key->s, &cfd->toggle_window_key);

	const BananaValue *
	option_toggle_screen_key = bananaGetOption (bananaIndex,
	                                            "toggle_screen_key",
	                                            -1);

	registerKey (option_toggle_screen_key->s, &cfd->toggle_screen_key);

	const BananaValue *
	option_switch_filter_key = bananaGetOption (bananaIndex,
	                                            "switch_filter_key",
	                                            -1);

	registerKey (option_switch_filter_key->s, &cfd->switch_filter_key);

	d->privates[displayPrivateIndex].ptr = cfd;

	return TRUE;
}

static void
colorFilterFiniDisplay (CompPlugin  *p,
                        CompDisplay *d)
{
	FILTER_DISPLAY (d);

	UNWRAP (cfd, d, handleEvent);

	freeScreenPrivateIndex (cfd->screenPrivateIndex);

	free (cfd);
}

static Bool
colorFilterInitScreen (CompPlugin *p,
                       CompScreen *s)
{
	ColorFilterScreen *cfs;

	FILTER_DISPLAY (&display);

	if (!s->fragmentProgram)
	{
		compLogMessage ("colorfilter", CompLogLevelFatal,
		                "Fragment program support missing.");
		return TRUE;
	}

	cfs = malloc (sizeof (ColorFilterScreen));
	if (!cfs)
		return FALSE;

	cfs->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (cfs->windowPrivateIndex < 0)
	{
		free (cfs);
		return FALSE;
	}

	cfs->isFiltered = FALSE;
	cfs->currentFilter = 0;

	cfs->filtersLoaded = FALSE;
	cfs->filtersFunctions = NULL;
	cfs->filtersCount = 0;	

	WRAP (cfs, s, drawWindowTexture, colorFilterDrawWindowTexture);
	WRAP (cfs, s, windowAddNotify, colorFilterWindowAddNotify);

	s->privates[cfd->screenPrivateIndex].ptr = cfs;

	const BananaValue *
	option_filter_match = bananaGetOption (bananaIndex,
	                                       "filter_match",
	                                       s->screenNum);

	matchInit (&cfs->filter_match);
	matchAddFromString (&cfs->filter_match, option_filter_match->s);
	matchUpdate (&cfs->filter_match);

	const BananaValue *
	option_exclude_match = bananaGetOption (bananaIndex,
	                                        "exclude_match",
	                                        s->screenNum);

	matchInit (&cfs->exclude_match);
	matchAddFromString (&cfs->exclude_match, option_exclude_match->s);
	matchUpdate (&cfs->exclude_match);

	return TRUE;
}

static void
colorFilterFiniScreen (CompPlugin *p,
                       CompScreen *s)
{
	FILTER_SCREEN (s);

	freeWindowPrivateIndex (s, cfs->windowPrivateIndex);

	UNWRAP (cfs, s, drawWindowTexture);
	UNWRAP (cfs, s, windowAddNotify);

	unloadFilters (s);

	free (cfs);
}

static Bool
colorFilterInitWindow (CompPlugin *p,
                       CompWindow *w)
{
	ColorFilterWindow *cfw;

	if (!w->screen->fragmentProgram)
		return TRUE;

	FILTER_SCREEN (w->screen);

	cfw = malloc (sizeof (ColorFilterWindow));

	if (!cfw)
		return FALSE;

	cfw->isFiltered = FALSE;

	w->privates[cfs->windowPrivateIndex].ptr = cfw;

	return TRUE;
}

static void
colorFilterFiniWindow (CompPlugin *p,
                       CompWindow *w)
{
	if (!w->screen->fragmentProgram)
		return;

	FILTER_WINDOW (w);
	free (cfw);
}

static Bool
colorFilterInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("colorfilter", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("colorfilter");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, colorFilterChangeNotify);

	return TRUE;
}

static void
colorFilterFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable colorFilterVTable = {
	"colorfilter",
	colorFilterInit,
	colorFilterFini,
	colorFilterInitDisplay,
	colorFilterFiniDisplay,
	colorFilterInitScreen,
	colorFilterFiniScreen,
	colorFilterInitWindow,
	colorFilterFiniWindow
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &colorFilterVTable;
}
