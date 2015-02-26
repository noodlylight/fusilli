/*
 * Compiz/Fusion color filtering plugin
 *
 * Author : Guillaume Seguin
 * Email : guillaume@segu.in
 *
 * Copyright (c) 2007 Guillaume Seguin <guillaume@segu.in>
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

#include <compiz-core.h>
#include "parser.h"
#include "colorfilter_options.h"

static int displayPrivateIndex;
static int corePrivateIndex;

typedef struct _ColorFilterCore {
    ObjectAddProc objectAdd;
} ColorFilterCore;

typedef struct _ColorFilterDisplay
{
    int		    screenPrivateIndex;
} ColorFilterDisplay;

typedef struct _ColorFilterScreen
{
    int			    windowPrivateIndex;

    DrawWindowTextureProc   drawWindowTexture;

    Bool		    isFiltered;
    int			    currentFilter; /* 0 : cumulative mode
					      0 < c <= count : single mode */

    /* The plugin can not immediately load the filters because it needs to
     * know what texture target it will use : when required, this boolean
     * is set to TRUE and filters will be loaded on next filtered window
     * texture painting */
    Bool		    filtersLoaded;
    int			    *filtersFunctions;
    int			    filtersCount;
} ColorFilterScreen;

typedef struct _ColorFilterWindow
{
    Bool    isFiltered;
} ColorFilterWindow;

#define GET_FILTER_CORE(c) \
    ((ColorFilterCore *) (c)->base.privates[corePrivateIndex].ptr)
#define FILTER_CORE(c) \
    ColorFilterCore *fc = GET_FILTER_CORE (c)
#define GET_FILTER_DISPLAY(d)					    \
    ((ColorFilterDisplay *) (d)->base.privates[displayPrivateIndex].ptr)
#define FILTER_DISPLAY(d)			    \
    ColorFilterDisplay *cfd = GET_FILTER_DISPLAY (d)
#define GET_FILTER_SCREEN(s, cfd)					\
    ((ColorFilterScreen *) (s)->base.privates[(cfd)->screenPrivateIndex].ptr)
#define FILTER_SCREEN(s)					\
    ColorFilterScreen *cfs = GET_FILTER_SCREEN (s,		\
			     GET_FILTER_DISPLAY ((s)->display))
#define GET_FILTER_WINDOW(w, cfs)					\
    ((ColorFilterWindow *) (w)->base.privates[(cfs)->windowPrivateIndex].ptr)
#define FILTER_WINDOW(w)						\
    ColorFilterWindow *cfw = GET_FILTER_WINDOW  (w,			\
			     GET_FILTER_SCREEN  ((w)->screen,		\
			     GET_FILTER_DISPLAY ((w)->screen->display)))

/* Compiz-core imports ------------------------------------------------------ */

/* _CompFunction struct definition (from compiz-core/src/fragment.c)
 * I just keep the beginning of the struct, since I just want the name
 * and importing the remainder would mean importing several other definitions.
 * I guess this is a bit risky though.. maybe should I bring this issue on
 * the mailing list? */
struct _CompFunction {
    struct _CompFunction *next;

    int			 id;
    char		 *name;
};

/*
 * Find fragment function by id (imported from compiz-core/src/fragment.c)
 */
static CompFunction *
findFragmentFunction (CompScreen *s, int id)
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
colorFilterToggleWindow (CompWindow * w)
{
    FILTER_WINDOW (w);

    /* Toggle window filtering flag */
    cfw->isFiltered = !cfw->isFiltered;

    /* Check exclude list */
    if (matchEval (colorfilterGetExcludeMatch (w->screen), w))
	cfw->isFiltered = FALSE;

    /* Ensure window is going to be repainted */
    addWindowDamage (w);
}

/*
 * Toggle filtering for the whole screen
 */
static void
colorFilterToggleScreen (CompScreen * s)
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
colorFilterSwitchFilter (CompScreen * s)
{
    int id;
    CompFunction *function;
    CompWindow *w;
    FILTER_SCREEN (s);

    /* % (count + 1) because of the cumulative filters mode */
    cfs->currentFilter = ++cfs->currentFilter % (cfs->filtersCount + 1);
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

/*
 * Window filtering toggle action
 */
static Bool
colorFilterToggle (CompDisplay * d, CompAction * action,
		   CompActionState state, CompOption * option, int nOption)
{
    CompWindow *w;
    Window xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);

    w = findWindowAtDisplay (d, xid);

    if (w && w->screen->fragmentProgram)
	colorFilterToggleWindow (w);

    return TRUE;
}

/*
 * Screen filtering toggle action
 */
static Bool
colorFilterToggleAll (CompDisplay * d, CompAction * action,
		      CompActionState state, CompOption * option, int nOption)
{
    CompScreen *s;
    Window xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    s = findScreenAtDisplay(d, xid);

    if (s && s->fragmentProgram)
	colorFilterToggleScreen (s);

    return TRUE;
}

/*
 * Filter switching action
 */
static Bool
colorFilterSwitch (CompDisplay * d, CompAction * action,
		   CompActionState state, CompOption * option, int nOption)
{
    CompScreen *s;
    Window xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    s = findScreenAtDisplay(d, xid);

    if (s && s->fragmentProgram)
	colorFilterSwitchFilter (s);

    return TRUE;
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
loadFilters (CompScreen *s, CompTexture *texture)
{
    int i, target, loaded, function, count;
    char *name;
    CompListValue *filters;
    CompWindow *w;

    FILTER_SCREEN (s);

    cfs->filtersLoaded = TRUE;

    /* Fetch filters filenames */
    filters = colorfilterGetFilters (s);
    count = filters->nValue;

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
	name = base_name (filters->value[i].s);
	if (!name || !strlen (name))
	{
	    if (name)
		free (name);

	    cfs->filtersFunctions[i] = 0;
	    continue;
	}

	compLogMessage ("colorfilter", CompLogLevelInfo,
			"Loading filter %s (item %s).", name,
			filters->value[i].s);
	function = loadFragmentProgram (filters->value[i].s, name, s, target);
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
colorFilterDrawWindowTexture (CompWindow *w, CompTexture *texture,
			      const FragmentAttrib *attrib, unsigned int mask)
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
    if (cfs->filtersCount && cfw->isFiltered &&
	(colorfilterGetFilterDecorations (w->screen) ||
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
	WRAP(cfs, w->screen, drawWindowTexture, colorFilterDrawWindowTexture);
    }
    else /* Not filtering */
    {
	UNWRAP (cfs, w->screen, drawWindowTexture);
	(*w->screen->drawWindowTexture) (w, texture, attrib, mask);
	WRAP(cfs, w->screen, drawWindowTexture, colorFilterDrawWindowTexture);
    }
}

/*
 * Filter windows when they are open if they match the filtering rules
 */
static void
colorFilterWindowAdd (CompScreen *s,
		      CompWindow *w)
{
    FILTER_SCREEN (s);

    /* cfw->isFiltered is initialized to FALSE in InitWindow, so we only
       have to toggle it to TRUE if necessary */
    if (cfs->isFiltered && matchEval (colorfilterGetFilterMatch (s), w))
	colorFilterToggleWindow (w);
}

/* Internal stuff ----------------------------------------------------------- */

/*
 * Filtering match settings update callback
 */
static void
colorFilterMatchsChanged (CompScreen *s, CompOption *opt,
			  ColorfilterScreenOptions num)
{
    CompWindow *w;

    FILTER_SCREEN (s);

    /* Re-check every window against new match settings */
    for (w = s->windows; w; w = w->next)
    {
	FILTER_WINDOW (w);
	if (matchEval (colorfilterGetFilterMatch (s), w) &&
	    cfs->isFiltered && !cfw->isFiltered)
	{
	    colorFilterToggleWindow (w);
	}
    }
}

/*
 * Exclude match settings update callback
 */
static void
colorFilterExcludeMatchsChanged (CompScreen *s, CompOption *opt,
				 ColorfilterScreenOptions num)
{
    CompWindow *w;

    FILTER_SCREEN (s);

    /* Re-check every window against new match settings */
    for (w = s->windows; w; w = w->next)
    {
	Bool isExcluded;

	FILTER_WINDOW (w);

	isExcluded = matchEval (colorfilterGetExcludeMatch (s), w);
	if (isExcluded && cfw->isFiltered)
	    colorFilterToggleWindow (w);
	else if (!isExcluded && cfs->isFiltered && !cfw->isFiltered)
	    colorFilterToggleWindow (w);
    }
}

/*
 * Filters list setting update callback
 */
static void
colorFiltersChanged (CompScreen *s, CompOption *opt,
		     ColorfilterScreenOptions num)
{
    FILTER_SCREEN (s);
    /* Just set the filtersLoaded boolean to FALSE, unloadFilters will be
     * called in loadFilters */
    cfs->filtersLoaded = FALSE;
}

/*
 * Damage decorations after the "Filter Decorations" setting got changed
 */
static void
colorFilterDamageDecorations (CompScreen *s, CompOption *opt,
			      ColorfilterScreenOptions num)
{
    damageScreen (s);
}

static void
colorFilterObjectAdd (CompObject *parent,
		      CompObject *object)
{
    static ObjectAddProc dispTab[] = {
	(ObjectAddProc) 0, /* CoreAdd */
        (ObjectAddProc) 0, /* DisplayAdd */
        (ObjectAddProc) 0, /* ScreenAdd */
        (ObjectAddProc) colorFilterWindowAdd
    };

    FILTER_CORE (&core);

    UNWRAP (fc, &core, objectAdd);
    (*core.objectAdd) (parent, object);
    WRAP (fc, &core, objectAdd, colorFilterObjectAdd);

    DISPATCH (object, dispTab, ARRAY_SIZE (dispTab), (parent, object));
}

static Bool
colorFilterInitCore (CompPlugin *p,
		     CompCore   *c)
{
    ColorFilterCore *fc;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
        return FALSE;

    fc = malloc (sizeof (ColorFilterCore));
    if (!fc)
        return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
        free (fc);
        return FALSE;
    }

    WRAP (fc, c, objectAdd, colorFilterObjectAdd);

    c->base.privates[corePrivateIndex].ptr = fc;

    return TRUE;
}

static void
colorFilterFiniCore (CompPlugin *p,
		     CompCore   *c)
{
    FILTER_CORE (c);

    freeDisplayPrivateIndex (displayPrivateIndex);

    UNWRAP (fc, c, objectAdd);

    free (fc);
}

static Bool
colorFilterInitDisplay (CompPlugin * p, CompDisplay * d)
{
    ColorFilterDisplay *cfd;

    cfd = malloc (sizeof (ColorFilterDisplay));
    if (!cfd)
	return FALSE;

    cfd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (cfd->screenPrivateIndex < 0)
    {
	free (cfd);
	return FALSE;
    }

    colorfilterSetToggleWindowKeyInitiate (d, colorFilterToggle);
    colorfilterSetToggleScreenKeyInitiate (d, colorFilterToggleAll);
    colorfilterSetSwitchFilterKeyInitiate (d, colorFilterSwitch);

    d->base.privates[displayPrivateIndex].ptr = cfd;

    return TRUE;
}

static void
colorFilterFiniDisplay (CompPlugin * p, CompDisplay * d)
{
    FILTER_DISPLAY (d);
    freeScreenPrivateIndex (d, cfd->screenPrivateIndex);
    free (cfd);
}

static Bool
colorFilterInitScreen (CompPlugin * p, CompScreen * s)
{
    ColorFilterScreen *cfs;

    FILTER_DISPLAY (s->display);

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

    colorfilterSetFilterMatchNotify (s, colorFilterMatchsChanged);
    colorfilterSetExcludeMatchNotify (s, colorFilterExcludeMatchsChanged);
    colorfilterSetFiltersNotify (s, colorFiltersChanged);
    colorfilterSetFilterDecorationsNotify (s, colorFilterDamageDecorations);

    WRAP (cfs, s, drawWindowTexture, colorFilterDrawWindowTexture);

    s->base.privates[cfd->screenPrivateIndex].ptr = cfs;

    return TRUE;
}

static void
colorFilterFiniScreen (CompPlugin * p, CompScreen * s)
{
    FILTER_SCREEN (s);

    freeWindowPrivateIndex (s, cfs->windowPrivateIndex);
    UNWRAP (cfs, s, drawWindowTexture);

    unloadFilters (s);

    free (cfs);
}

static Bool
colorFilterInitWindow (CompPlugin * p, CompWindow * w)
{
    ColorFilterWindow *cfw;

    if (!w->screen->fragmentProgram)
	return TRUE;

    FILTER_SCREEN (w->screen);

    cfw = malloc (sizeof (ColorFilterWindow));
    if (!cfw)
	return FALSE;

    cfw->isFiltered = FALSE;

    w->base.privates[cfs->windowPrivateIndex].ptr = cfw;

    return TRUE;
}

static void
colorFilterFiniWindow (CompPlugin * p, CompWindow * w)
{
    if (!w->screen->fragmentProgram)
	return;

    FILTER_WINDOW (w);
    free (cfw);
}

static CompBool
colorFilterInitObject (CompPlugin *p,
		       CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) colorFilterInitCore,
	(InitPluginObjectProc) colorFilterInitDisplay,
	(InitPluginObjectProc) colorFilterInitScreen,
	(InitPluginObjectProc) colorFilterInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
colorFilterFiniObject (CompPlugin *p,
		       CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) colorFilterFiniCore,
	(FiniPluginObjectProc) colorFilterFiniDisplay,
	(FiniPluginObjectProc) colorFilterFiniScreen,
	(FiniPluginObjectProc) colorFilterFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
colorFilterInit (CompPlugin * p)
{
    corePrivateIndex = allocateCorePrivateIndex ();
    if (corePrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
colorFilterFini (CompPlugin * p)
{
    freeCorePrivateIndex (corePrivateIndex);
}

CompPluginVTable colorFilterVTable = {
    "colorfilter",
    0,
    colorFilterInit,
    colorFilterFini,
    colorFilterInitObject,
    colorFilterFiniObject,
    NULL,
    NULL
};

CompPluginVTable *getCompPluginInfo (void)
{
    return &colorFilterVTable;
}
