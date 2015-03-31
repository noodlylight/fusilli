/*
 * winrules plugin for compiz
 *
 * Copyright (C) 2007 Bellegarde Cedric (gnumdk (at) gmail.com)
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
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <compiz-core.h>

#include <X11/Xatom.h>

#define WINRULES_SCREEN_OPTION_SKIPTASKBAR_MATCH  0
#define WINRULES_SCREEN_OPTION_SKIPPAGER_MATCH	  1
#define WINRULES_SCREEN_OPTION_ABOVE_MATCH	  2
#define WINRULES_SCREEN_OPTION_BELOW_MATCH        3
#define WINRULES_SCREEN_OPTION_STICKY_MATCH       4
#define WINRULES_SCREEN_OPTION_FULLSCREEN_MATCH   5
#define WINRULES_SCREEN_OPTION_MAXIMIZE_MATCH     6
#define WINRULES_SCREEN_OPTION_NOARGB_MATCH       7
#define WINRULES_SCREEN_OPTION_NOMOVE_MATCH       8
#define WINRULES_SCREEN_OPTION_NORESIZE_MATCH     9
#define WINRULES_SCREEN_OPTION_NOMINIMIZE_MATCH   10
#define WINRULES_SCREEN_OPTION_NOMAXIMIZE_MATCH   11
#define WINRULES_SCREEN_OPTION_NOCLOSE_MATCH      12
#define WINRULES_SCREEN_OPTION_NOFOCUS_MATCH      13
#define WINRULES_SCREEN_OPTION_SIZE_MATCHES	  14
#define WINRULES_SCREEN_OPTION_SIZE_WIDTH_VALUES  15
#define WINRULES_SCREEN_OPTION_SIZE_HEIGHT_VALUES 16
#define WINRULES_SCREEN_OPTION_NUM		  17

static CompMetadata winrulesMetadata;

static int displayPrivateIndex;

typedef struct _WinrulesWindow {
    unsigned int allowedActions;
    unsigned int stateSetMask;
    unsigned int protocolSetMask;

    Bool oldInputHint;
    Bool hasAlpha;

    CompTimeoutHandle handle;
} WinrulesWindow;

typedef struct _WinrulesDisplay {
    int screenPrivateIndex;

    HandleEventProc            handleEvent;
    MatchExpHandlerChangedProc matchExpHandlerChanged;
    MatchPropertyChangedProc   matchPropertyChanged;
} WinrulesDisplay;

typedef struct _WinrulesScreen {
    int windowPrivateIndex;
    GetAllowedActionsForWindowProc getAllowedActionsForWindow;
    CompOption opt[WINRULES_SCREEN_OPTION_NUM];
} WinrulesScreen;

#define GET_WINRULES_DISPLAY(d)				\
    ((WinrulesDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define WINRULES_DISPLAY(d)			   	\
    WinrulesDisplay *wd = GET_WINRULES_DISPLAY (d)

#define GET_WINRULES_SCREEN(s, wd)				    \
    ((WinrulesScreen *) (s)->base.privates[(wd)->screenPrivateIndex].ptr)

#define WINRULES_SCREEN(s)			    \
    WinrulesScreen *ws = GET_WINRULES_SCREEN (s,    \
			 GET_WINRULES_DISPLAY (s->display))

#define GET_WINRULES_WINDOW(w, ws)                                  \
    ((WinrulesWindow *) (w)->base.privates[(ws)->windowPrivateIndex].ptr)

#define WINRULES_WINDOW(w)					\
    WinrulesWindow *ww = GET_WINRULES_WINDOW  (w,		\
   			 GET_WINRULES_SCREEN  (w->screen,	\
			 GET_WINRULES_DISPLAY (w->screen->display)))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static void
winrulesSetProtocols (CompDisplay  *display,
		      unsigned int protocols,
		      Window       id)
{
    Atom protocol[4];
    int  count = 0;

    if (protocols & CompWindowProtocolDeleteMask)
	protocol[count++] = display->wmDeleteWindowAtom;
    if (protocols & CompWindowProtocolTakeFocusMask)
	protocol[count++] = display->wmTakeFocusAtom;
    if (protocols & CompWindowProtocolPingMask)
	protocol[count++] = display->wmPingAtom;
    if (protocols & CompWindowProtocolSyncRequestMask)
	protocol[count++] = display->wmSyncRequestAtom;

    XSetWMProtocols (display->display, id, protocol, count);
}

static Bool
isWinrulesWindow (CompWindow *w)
{
    if (w->attrib.override_redirect)
	return FALSE;

    if (w->wmType & CompWindowTypeDesktopMask)
	return FALSE;

    return TRUE;
}

/* FIXME? Directly set inputHint, not a problem for now */
static void
winrulesSetNoFocus (CompWindow *w,
		    int        optNum)
{
    unsigned int newProtocol = w->protocols;

    WINRULES_SCREEN (w->screen);
    WINRULES_WINDOW (w);

    if (!isWinrulesWindow (w))
	return;

    if (matchEval (&ws->opt[optNum].value.match, w))
    {
	if (w->protocols & CompWindowProtocolTakeFocusMask)
	{
	    ww->protocolSetMask |= (w->protocols &
				    CompWindowProtocolTakeFocusMask);
	    newProtocol = w->protocols & ~CompWindowProtocolTakeFocusMask;
	}
	ww->oldInputHint = w->inputHint;
	w->inputHint = FALSE;
    }
    else if (ww->oldInputHint ||
	     (ww->protocolSetMask & CompWindowProtocolTakeFocusMask))
    {
	newProtocol = w->protocols |
	              (ww->protocolSetMask & CompWindowProtocolTakeFocusMask);
	ww->protocolSetMask &= ~CompWindowProtocolTakeFocusMask;
	w->inputHint = ww->oldInputHint;
    }

    if (newProtocol != w->protocols)
    {
	winrulesSetProtocols (w->screen->display, newProtocol, w->id);
	w->protocols = newProtocol;
    }
}

static void
winrulesSetNoAlpha (CompWindow *w,
	  	    int        optNum)
{
    WINRULES_SCREEN (w->screen);
    WINRULES_WINDOW (w);

    if (!isWinrulesWindow (w))
	return;

    if (matchEval (&ws->opt[optNum].value.match, w))
    {
	ww->hasAlpha = w->alpha;
	w->alpha = FALSE;
    }
    else
    {
	w->alpha = ww->hasAlpha;
    }
}

static void
winrulesUpdateState (CompWindow *w,
		     int        optNum,
		     int        mask)
{
    unsigned int newState = w->state;

    WINRULES_SCREEN (w->screen);
    WINRULES_WINDOW (w);

    if (!isWinrulesWindow (w))
	return;

    if (matchEval (&ws->opt[optNum].value.match, w))
    {
	newState |= mask;
	newState = constrainWindowState (newState, w->actions);
	ww->stateSetMask |= (newState & mask);
    }
    else if (ww->stateSetMask & mask)
    {
	newState &= ~mask;
	ww->stateSetMask &= ~mask;
    }

    if (newState != w->state)
    {
	changeWindowState (w, newState);

	if (mask & (CompWindowStateFullscreenMask |
		    CompWindowStateAboveMask      |
		    CompWindowStateBelowMask       ))
	    updateWindowAttributes (w, CompStackingUpdateModeNormal);
	else
	    updateWindowAttributes (w, CompStackingUpdateModeNone);
    }
}

static void
winrulesSetAllowedActions (CompWindow *w,
			   int        optNum,
			   int        action)
{
    WINRULES_SCREEN (w->screen);
    WINRULES_WINDOW (w);

    if (!isWinrulesWindow (w))
	return;

    if (matchEval (&ws->opt[optNum].value.match, w))
	ww->allowedActions &= ~action;
    else if (!(ww->allowedActions & action))
	ww->allowedActions |= action;

    recalcWindowActions (w);
}

static Bool
winrulesMatchSizeValue (CompWindow *w,
			CompOption *matches,
			CompOption *widthValues,
			CompOption *heightValues,
			int	   *width,
			int	   *height)
{
    int i, min;

    if (!isWinrulesWindow (w))
	return FALSE;

    if (w->type & CompWindowTypeDesktopMask)
	return FALSE;

    min = MIN (matches->value.list.nValue, widthValues->value.list.nValue);
    min = MIN (min, heightValues->value.list.nValue);

    for (i = 0; i < min; i++)
    {
	if (matchEval (&matches->value.list.value[i].match, w))
	{
	    *width = widthValues->value.list.value[i].i;
	    *height = heightValues->value.list.value[i].i;
	
	    return TRUE;
	}
    }

    return FALSE;
}

static Bool
winrulesMatchSize (CompWindow *w,
		   int	      *width,
		   int	      *height)
{
    WINRULES_SCREEN (w->screen);

    return winrulesMatchSizeValue (w,
	&ws->opt[WINRULES_SCREEN_OPTION_SIZE_MATCHES],
	&ws->opt[WINRULES_SCREEN_OPTION_SIZE_WIDTH_VALUES],
	&ws->opt[WINRULES_SCREEN_OPTION_SIZE_HEIGHT_VALUES],
	width, height);
}

static void
winrulesUpdateWindowSize (CompWindow *w,
			  int        width,
			  int        height)
{
    XWindowChanges xwc;
    unsigned int   xwcm = 0;

    if (width != w->serverWidth)
	xwcm |= CWWidth;
    if (height != w->serverHeight)
	xwcm |= CWHeight;

    xwc.width = width;
    xwc.height = height;

    if (w->mapNum && xwcm)
	sendSyncRequest (w);

    configureXWindow (w, xwcm, &xwc);
}

static CompOption *
winrulesGetScreenOptions (CompPlugin *plugin,
			  CompScreen *screen,
                          int        *count)
{
    WINRULES_SCREEN (screen);

    *count = NUM_OPTIONS (ws);
    return ws->opt;
}

static Bool
winrulesSetScreenOption (CompPlugin *plugin,
			 CompScreen      *screen,
                         const char      *name,
                         CompOptionValue *value)
{
    CompOption   *o;
    int          index;
    unsigned int updateStateMask = 0, updateActionsMask = 0;

    WINRULES_SCREEN (screen);

    o = compFindOption (ws->opt, NUM_OPTIONS (ws), name, &index);
    if (!o)
        return FALSE;

    switch (index)
    {
    case WINRULES_SCREEN_OPTION_SKIPTASKBAR_MATCH:
	if (compSetMatchOption (o, value))
	    updateStateMask = CompWindowStateSkipTaskbarMask;
	break;

    case WINRULES_SCREEN_OPTION_SKIPPAGER_MATCH:
	if (compSetMatchOption (o, value))
	    updateStateMask = CompWindowStateSkipPagerMask;
	break;

    case WINRULES_SCREEN_OPTION_ABOVE_MATCH:
	if (compSetMatchOption (o, value))
	    updateStateMask = CompWindowStateAboveMask;
	break;
	
    case WINRULES_SCREEN_OPTION_BELOW_MATCH:
	if (compSetMatchOption (o, value))
	    updateStateMask = CompWindowStateBelowMask;
	break;
	
    case WINRULES_SCREEN_OPTION_STICKY_MATCH:
	if (compSetMatchOption (o, value))
	    updateStateMask = CompWindowStateStickyMask;
	break;
	
    case WINRULES_SCREEN_OPTION_FULLSCREEN_MATCH:
	if (compSetMatchOption (o, value))
	    updateStateMask = CompWindowStateFullscreenMask;
	break;

    case WINRULES_SCREEN_OPTION_MAXIMIZE_MATCH:
	if (compSetMatchOption (o, value))
	    updateStateMask = CompWindowStateMaximizedHorzMask |
			      CompWindowStateMaximizedVertMask;
	break;

    case WINRULES_SCREEN_OPTION_NOMOVE_MATCH:
	if (compSetMatchOption (o, value))
	    updateActionsMask = CompWindowActionMoveMask;
	break;

    case WINRULES_SCREEN_OPTION_NORESIZE_MATCH:
	if (compSetMatchOption (o, value))
	    updateActionsMask = CompWindowActionResizeMask;
	break;

    case WINRULES_SCREEN_OPTION_NOMINIMIZE_MATCH:
	if (compSetMatchOption (o, value))
	    updateActionsMask = CompWindowActionMinimizeMask;
	break;

    case WINRULES_SCREEN_OPTION_NOMAXIMIZE_MATCH:
	if (compSetMatchOption (o, value))
	    updateActionsMask = CompWindowActionMaximizeVertMask |
		                CompWindowActionMaximizeHorzMask;
	break;

    case WINRULES_SCREEN_OPTION_NOCLOSE_MATCH:
	if (compSetMatchOption (o, value))
	    updateActionsMask = CompWindowActionCloseMask;
	break;
    case WINRULES_SCREEN_OPTION_NOARGB_MATCH:
	if (compSetMatchOption (o, value))
	{
	    CompWindow *w;

	    for (w = screen->windows; w; w = w->next)
		winrulesSetNoAlpha (w, WINRULES_SCREEN_OPTION_NOARGB_MATCH);

	    return TRUE;
	}
	break;
    case WINRULES_SCREEN_OPTION_NOFOCUS_MATCH:
	if (compSetMatchOption (o, value))
	{
	    CompWindow *w;

	    for (w = screen->windows; w; w = w->next)
		winrulesSetNoFocus (w, WINRULES_SCREEN_OPTION_NOFOCUS_MATCH);

	    return TRUE;
	}
	break;
    case WINRULES_SCREEN_OPTION_SIZE_MATCHES:
	if (compSetOptionList (o, value))
	{
	    int i;

	    for (i = 0; i < o->value.list.nValue; i++)
		matchUpdate (screen->display, &o->value.list.value[i].match);

	    return TRUE;
	}
	break;
    default:
	if (compSetOption (o, value))
	    return TRUE;
        break;
    }

    if (updateStateMask)
    {
	CompWindow *w;

	for (w = screen->windows; w; w = w->next)
	    winrulesUpdateState (w, index, updateStateMask);

	return TRUE;
    }

    if (updateActionsMask)
    {
	CompWindow *w;

	for (w = screen->windows; w; w = w->next)
	    winrulesSetAllowedActions (w, index, updateActionsMask);

	return TRUE;
    }

    return FALSE;
}

static Bool
winrulesApplyRules (CompWindow *w)
{
    int        width, height;

    winrulesUpdateState (w,
			 WINRULES_SCREEN_OPTION_SKIPTASKBAR_MATCH,
			 CompWindowStateSkipTaskbarMask);

    winrulesUpdateState (w,
			 WINRULES_SCREEN_OPTION_SKIPPAGER_MATCH,
			 CompWindowStateSkipPagerMask);

    winrulesUpdateState (w,
			 WINRULES_SCREEN_OPTION_ABOVE_MATCH,
			 CompWindowStateAboveMask);

    winrulesUpdateState (w,
			 WINRULES_SCREEN_OPTION_BELOW_MATCH,
			 CompWindowStateBelowMask);

    winrulesUpdateState (w,
			 WINRULES_SCREEN_OPTION_STICKY_MATCH,
			 CompWindowStateStickyMask);

    winrulesUpdateState (w,
			 WINRULES_SCREEN_OPTION_FULLSCREEN_MATCH,
			 CompWindowStateFullscreenMask);

    winrulesUpdateState (w,
			 WINRULES_SCREEN_OPTION_MAXIMIZE_MATCH,
			 CompWindowStateMaximizedHorzMask |
			 CompWindowStateMaximizedVertMask);

    winrulesSetAllowedActions (w,
			       WINRULES_SCREEN_OPTION_NOMOVE_MATCH,
			       CompWindowActionMoveMask);

    winrulesSetAllowedActions (w,
			       WINRULES_SCREEN_OPTION_NORESIZE_MATCH,
			       CompWindowActionResizeMask);

    winrulesSetAllowedActions (w,
			       WINRULES_SCREEN_OPTION_NOMINIMIZE_MATCH,
			       CompWindowActionMinimizeMask);

    winrulesSetAllowedActions (w,
			       WINRULES_SCREEN_OPTION_NOMAXIMIZE_MATCH,
			       CompWindowActionMaximizeVertMask |
			       CompWindowActionMaximizeHorzMask);

    winrulesSetAllowedActions (w,
			       WINRULES_SCREEN_OPTION_NOCLOSE_MATCH,
			       CompWindowActionCloseMask);

    winrulesSetNoAlpha (w, WINRULES_SCREEN_OPTION_NOARGB_MATCH);

    if (winrulesMatchSize (w, &width, &height))
	winrulesUpdateWindowSize (w, width, height);

    return FALSE;
}

static Bool
winrulesApplyRulesTimeout (void *closure)
{
    CompWindow *w = (CompWindow *) closure;

    WINRULES_WINDOW (w);
    ww->handle = 0;

    return winrulesApplyRules (w);
}

static void
winrulesHandleEvent (CompDisplay *d,
                     XEvent      *event)
{
    CompWindow *w;

    WINRULES_DISPLAY (d);

    if (event->type == MapRequest)
    {
	w = findWindowAtDisplay (d, event->xmap.window);
	if (w)
	{
	    winrulesSetNoFocus (w, WINRULES_SCREEN_OPTION_NOFOCUS_MATCH);
	    winrulesApplyRules (w);
	}
    }

    UNWRAP (wd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (wd, d, handleEvent, winrulesHandleEvent);
}

static void
winrulesGetAllowedActionsForWindow (CompWindow   *w,
				    unsigned int *setActions,
				    unsigned int *clearActions)
{
    WINRULES_SCREEN (w->screen);
    WINRULES_WINDOW (w);

    UNWRAP (ws, w->screen, getAllowedActionsForWindow);
    (*w->screen->getAllowedActionsForWindow) (w, setActions, clearActions);
    WRAP (ws, w->screen, getAllowedActionsForWindow,
          winrulesGetAllowedActionsForWindow);

    if (ww)
	*clearActions |= ~ww->allowedActions;
}

static void
winrulesMatchExpHandlerChanged (CompDisplay *d)
{
    CompScreen *s;
    CompWindow *w;

    WINRULES_DISPLAY (d);

    UNWRAP (wd, d, matchExpHandlerChanged);
    (*d->matchExpHandlerChanged) (d);
    WRAP (wd, d, matchExpHandlerChanged, winrulesMatchExpHandlerChanged);

    /* match options are up to date after the call to matchExpHandlerChanged */
    for (s = d->screens; s; s = s->next)
	for (w = s->windows; w; w = w->next)
	    winrulesApplyRules (w);
}

static void
winrulesMatchPropertyChanged (CompDisplay *d,
	    		      CompWindow  *w)
{
    WINRULES_DISPLAY (d);

    winrulesApplyRules (w);

    UNWRAP (wd, d, matchPropertyChanged);
    (*d->matchPropertyChanged) (d, w);
    WRAP (wd, d, matchPropertyChanged, winrulesMatchPropertyChanged);
}

static Bool
winrulesInitDisplay (CompPlugin  *p,
		     CompDisplay *d)
{
    WinrulesDisplay *wd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    wd = malloc (sizeof (WinrulesDisplay));
    if (!wd)
        return FALSE;

    wd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (wd->screenPrivateIndex < 0)
    {
        free (wd);
        return FALSE;
    }

    WRAP (wd, d, handleEvent, winrulesHandleEvent);
    WRAP (wd, d, matchExpHandlerChanged, winrulesMatchExpHandlerChanged);
    WRAP (wd, d, matchPropertyChanged, winrulesMatchPropertyChanged);

    d->base.privates[displayPrivateIndex].ptr = wd;

    return TRUE;
}

static void
winrulesFiniDisplay (CompPlugin  *p,
		     CompDisplay *d)
{
    WINRULES_DISPLAY (d);

    freeScreenPrivateIndex (d, wd->screenPrivateIndex);

    UNWRAP (wd, d, handleEvent);
    UNWRAP (wd, d, matchExpHandlerChanged);
    UNWRAP (wd, d, matchPropertyChanged);

    free (wd);
}

static const CompMetadataOptionInfo winrulesScreenOptionInfo[] = {
    { "skiptaskbar_match", "match", 0, 0, 0 },
    { "skippager_match", "match", 0, 0, 0 },
    { "above_match", "match", 0, 0, 0 },
    { "below_match", "match", 0, 0, 0 },
    { "sticky_match", "match", 0, 0, 0 },
    { "fullscreen_match", "match", 0, 0, 0 },
    { "maximize_match", "match", 0, 0, 0 },
    { "no_argb_match", "match", 0, 0, 0 },
    { "no_move_match", "match", 0, 0, 0 },
    { "no_resize_match", "match", 0, 0, 0 },
    { "no_minimize_match", "match", 0, 0, 0 },
    { "no_maximize_match", "match", 0, 0, 0 },
    { "no_close_match", "match", 0, 0, 0 },
    { "no_focus_match", "match", 0, 0, 0 },
    { "size_matches", "list", "<type>match</type>", 0, 0 },
    { "size_width_values", "list", "<type>int</type>", 0, 0 },
    { "size_height_values", "list", "<type>int</type>", 0, 0 }
};

static Bool
winrulesInitScreen (CompPlugin *p,
		    CompScreen *s)
{
    WinrulesScreen *ws;

    WINRULES_DISPLAY (s->display);

    ws = malloc (sizeof (WinrulesScreen));
    if (!ws)
        return FALSE;

    if (!compInitScreenOptionsFromMetadata (s,
					    &winrulesMetadata,
					    winrulesScreenOptionInfo,
					    ws->opt,
					    WINRULES_SCREEN_OPTION_NUM))
    {
	free (ws);
	return FALSE;
    }

    ws->windowPrivateIndex = allocateWindowPrivateIndex(s);
    if (ws->windowPrivateIndex < 0)
    {
	compFiniScreenOptions (s, ws->opt, WINRULES_SCREEN_OPTION_NUM);
	free (ws);
	return FALSE;
    }

    WRAP (ws, s, getAllowedActionsForWindow,
	  winrulesGetAllowedActionsForWindow);

    s->base.privates[wd->screenPrivateIndex].ptr = ws;

    return TRUE;
}

static void
winrulesFiniScreen (CompPlugin *p,
                    CompScreen *s)
{
    WINRULES_SCREEN (s);

    UNWRAP (ws, s, getAllowedActionsForWindow);

    freeWindowPrivateIndex(s, ws->windowPrivateIndex);

    compFiniScreenOptions (s, ws->opt, WINRULES_SCREEN_OPTION_NUM);

    free (ws);
}

static Bool
winrulesInitWindow (CompPlugin *p,
		    CompWindow *w)
{
    WinrulesWindow *ww;

    WINRULES_SCREEN (w->screen);

    ww = malloc (sizeof (WinrulesWindow));
    if (!ww)
        return FALSE;

    ww->stateSetMask    = 0;
    ww->protocolSetMask = 0;

    ww->allowedActions = ~0;

    ww->hasAlpha     = w->alpha;
    ww->oldInputHint = w->inputHint;

    w->base.privates[ws->windowPrivateIndex].ptr = ww;

    ww->handle = compAddTimeout (0, 0, winrulesApplyRulesTimeout, w);

    return TRUE;
}

static void
winrulesFiniWindow (CompPlugin *p,
                    CompWindow *w)
{
    WINRULES_WINDOW (w);

    if (ww->handle)
	compRemoveTimeout (ww->handle);

    free (ww);

    WINRULES_SCREEN (w->screen);
    w->base.privates[ws->windowPrivateIndex].ptr = NULL;
}

static CompBool
winrulesInitObject (CompPlugin *p,
		    CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) winrulesInitDisplay,
	(InitPluginObjectProc) winrulesInitScreen,
	(InitPluginObjectProc) winrulesInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
winrulesFiniObject (CompPlugin *p,
		    CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) winrulesFiniDisplay,
	(FiniPluginObjectProc) winrulesFiniScreen,
	(FiniPluginObjectProc) winrulesFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static CompOption *
winrulesGetObjectOptions (CompPlugin *plugin,
	  		  CompObject *object,
	  		  int	     *count)
{
    static GetPluginObjectOptionsProc dispTab[] = {
	(GetPluginObjectOptionsProc) 0, /* GetCoreOptions */
	(GetPluginObjectOptionsProc) 0, /* GetDisplayOptions */
	(GetPluginObjectOptionsProc) winrulesGetScreenOptions
    };

    *count = 0;
    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab),
		     NULL, (plugin, object, count));
}

static CompBool
winrulesSetObjectOption (CompPlugin      *plugin,
	  		 CompObject      *object,
	  		 const char      *name,
	  		 CompOptionValue *value)
{
    static SetPluginObjectOptionProc dispTab[] = {
	(SetPluginObjectOptionProc) 0, /* SetCoreOption */
	(SetPluginObjectOptionProc) 0, /* SetDisplayOption */
	(SetPluginObjectOptionProc) winrulesSetScreenOption
    };

    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab), FALSE,
		     (plugin, object, name, value));
}

static Bool
winrulesInit (CompPlugin *p)
{
    if (!compInitPluginMetadataFromInfo (&winrulesMetadata,
					 p->vTable->name,
					 0, 0,
					 winrulesScreenOptionInfo,
					 WINRULES_SCREEN_OPTION_NUM))
	return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
	compFiniMetadata (&winrulesMetadata);
        return FALSE;
    }

    compAddMetadataFromFile (&winrulesMetadata, p->vTable->name);

    return TRUE;
}

static void
winrulesFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);

    compFiniMetadata (&winrulesMetadata);
}

static CompMetadata *
winrulesGetMetadata (CompPlugin *plugin)
{
    return &winrulesMetadata;
}

static CompPluginVTable winrulesVTable = {
    "winrules",
    winrulesGetMetadata,
    winrulesInit,
    winrulesFini,
    winrulesInitObject,
    winrulesFiniObject,
    winrulesGetObjectOptions,
    winrulesSetObjectOption,
};

CompPluginVTable *
getCompPluginInfo20070830 (void)
{
    return &winrulesVTable;
}
