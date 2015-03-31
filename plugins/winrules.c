/*
 * winrules plugin for compiz
 *
 * Copyright (C) 2007 Bellegarde Cedric (gnumdk (at) gmail.com)
 *
 * Copyright (C) 2015 Michail Bitzes (noodlylight@gmail.com)
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
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <string.h>
#include <fusilli-core.h>

#include <X11/Xatom.h>

static int bananaIndex;

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

	HandleEventProc handleEvent;
	MatchPropertyChangedProc matchPropertyChanged;
} WinrulesDisplay;

#define MAX_MATCHES 100

typedef struct _WinrulesScreen {
	int windowPrivateIndex;
	GetAllowedActionsForWindowProc getAllowedActionsForWindow;

	CompMatch skiptaskbar_match, skippager_match,
	          above_match, below_match, sticky_match,
	          fullscreen_match, maximize_match, no_argb_match,
	          no_move_match, no_resize_match, no_minimize_match,
	          no_maximize_match, no_close_match, no_focus_match;

	CompMatch size_matches[MAX_MATCHES];
	int       size_matches_count;
} WinrulesScreen;

#define GET_WINRULES_DISPLAY(d) \
	((WinrulesDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define WINRULES_DISPLAY(d) \
	WinrulesDisplay *wd = GET_WINRULES_DISPLAY (d)

#define GET_WINRULES_SCREEN(s, wd) \
	((WinrulesScreen *) (s)->privates[(wd)->screenPrivateIndex].ptr)

#define WINRULES_SCREEN(s) \
	WinrulesScreen *ws = GET_WINRULES_SCREEN (s, \
	                                          GET_WINRULES_DISPLAY (&display))

#define GET_WINRULES_WINDOW(w, ws) \
	((WinrulesWindow *) (w)->privates[(ws)->windowPrivateIndex].ptr)

#define WINRULES_WINDOW(w)                                      \
	WinrulesWindow *ww = GET_WINRULES_WINDOW  (w,               \
	                                           GET_WINRULES_SCREEN  (w->screen,       \
	                                                                 GET_WINRULES_DISPLAY (&display)))

static void
winrulesSetProtocols (unsigned int protocols,
                      Window       id)
{
	Atom protocol[4];
	int count = 0;

	if (protocols & CompWindowProtocolDeleteMask)
		protocol[count++] = display.wmDeleteWindowAtom;
	if (protocols & CompWindowProtocolTakeFocusMask)
		protocol[count++] = display.wmTakeFocusAtom;
	if (protocols & CompWindowProtocolPingMask)
		protocol[count++] = display.wmPingAtom;
	if (protocols & CompWindowProtocolSyncRequestMask)
		protocol[count++] = display.wmSyncRequestAtom;

	XSetWMProtocols (display.display, id, protocol, count);
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
                    CompMatch  *match)
{
	unsigned int newProtocol = w->protocols;

	WINRULES_WINDOW (w);

	if (!isWinrulesWindow (w))
		return;

	if (matchEval (match, w))
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
		winrulesSetProtocols (newProtocol, w->id);
		w->protocols = newProtocol;
	}
}

static void
winrulesSetNoAlpha (CompWindow *w,
                    CompMatch  *match)
{
	WINRULES_WINDOW (w);

	if (!isWinrulesWindow (w))
		return;

	if (matchEval (match, w))
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
                     CompMatch  *match,
                     int        mask)
{
	unsigned int newState = w->state;

	WINRULES_WINDOW (w);

	if (!isWinrulesWindow (w))
		return;

	if (matchEval (match, w))
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
                           CompMatch  *match,
                           int        action)
{
	WINRULES_WINDOW (w);

	if (!isWinrulesWindow (w))
		return;

	if (matchEval (match, w))
		ww->allowedActions &= ~action;
	else if (!(ww->allowedActions & action))
		ww->allowedActions |= action;

	recalcWindowActions (w);
}

static Bool
winrulesMatchSize (CompWindow *w,
                   int        *width,
                   int        *height)
{
	CompScreen *s = w->screen;
	WINRULES_SCREEN (s);
	int i, min;

	if (!isWinrulesWindow (w))
		return FALSE;

	if (w->type & CompWindowTypeDesktopMask)
		return FALSE;

	const BananaValue *
	option_size_matches = bananaGetOption (bananaIndex,
	                                       "size_matches",
	                                       s->screenNum);

	const BananaValue *
	option_size_width_values = bananaGetOption (bananaIndex,
	                                            "size_width_values",
	                                            s->screenNum);

	const BananaValue *
	option_size_height_values = bananaGetOption (bananaIndex,
	                                             "size_height_values",
	                                             s->screenNum);

	min = MIN (option_size_matches->list.nItem,
	           option_size_width_values->list.nItem);

	min = MIN (min, option_size_height_values->list.nItem);

	for (i = 0; i < min; i++)
	{
		if (matchEval (&ws->size_matches[i], w))
		{
			*width = option_size_width_values->list.item[i].i;
			*height = option_size_height_values->list.item[i].i;

			return TRUE;
		}
	}

	return FALSE;
}

static void
winrulesUpdateWindowSize (CompWindow *w,
                          int        width,
                          int        height)
{
	XWindowChanges xwc;
	unsigned int xwcm = 0;

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

static void
winrulesChangeNotify (const char        *optionName,
                      BananaType        optionType,
                      const BananaValue *optionValue,
                      int               screenNum)
{
	unsigned int updateStateMask = 0, updateActionsMask = 0;

	CompScreen *screen = getScreenFromScreenNum (screenNum);

	WINRULES_SCREEN (screen);

	CompMatch *match;

	if (strcasecmp (optionName, "skiptaskbar_match") == 0)
	{
		match = &ws->skiptaskbar_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateStateMask = CompWindowStateSkipTaskbarMask;
	}
	else if (strcasecmp (optionName, "skippager_match") == 0)
	{
		match = &ws->skippager_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateStateMask = CompWindowStateSkipPagerMask;
	}
	else if (strcasecmp (optionName, "above_match") == 0)
	{
		match = &ws->above_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateStateMask = CompWindowStateAboveMask;
	}
	else if (strcasecmp (optionName, "below_match") == 0)
	{
		match = &ws->below_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateStateMask = CompWindowStateBelowMask;
	}
	else if (strcasecmp (optionName, "sticky_match") == 0)
	{
		match = &ws->sticky_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateStateMask = CompWindowStateStickyMask;
	}
	else if (strcasecmp (optionName, "fullscreen_match") == 0)
	{
		match = &ws->fullscreen_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateStateMask = CompWindowStateFullscreenMask;
	}
	else if (strcasecmp (optionName, "maximize_match") == 0)
	{
		match = &ws->maximize_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateStateMask = CompWindowStateMaximizedHorzMask |
		                  CompWindowStateMaximizedVertMask;
	}
	else if (strcasecmp (optionName, "no_move_match") == 0)
	{
		match = &ws->no_move_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateActionsMask = CompWindowActionMoveMask;
	}
	else if (strcasecmp (optionName, "no_resize_match") == 0)
	{
		match = &ws->no_resize_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateActionsMask = CompWindowActionResizeMask;
	}
	else if (strcasecmp (optionName, "no_minimize_match") == 0)
	{
		match = &ws->no_minimize_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateActionsMask = CompWindowActionMinimizeMask;
	}
	else if (strcasecmp (optionName, "no_maximize_match") == 0)
	{
		match = &ws->no_maximize_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateActionsMask = CompWindowActionMaximizeVertMask |
		                    CompWindowActionMaximizeHorzMask;
	}
	else if (strcasecmp (optionName, "no_close_match") == 0)
	{
		match = &ws->no_close_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		updateActionsMask = CompWindowActionCloseMask;
	}
	else if (strcasecmp (optionName, "no_argb_match") == 0)
	{
		match = &ws->no_argb_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		CompWindow *w;

		for (w = screen->windows; w; w = w->next)
			winrulesSetNoAlpha (w, match);
	}
	else if (strcasecmp (optionName, "no_focus_match") == 0)
	{
		match = &ws->no_focus_match;

		matchFini (match);
		matchInit (match);
		matchAddFromString (match, optionValue->s);
		matchUpdate (match);

		CompWindow *w;

		for (w = screen->windows; w; w = w->next)
			winrulesSetNoFocus (w, match);
	}
	else if (strcasecmp (optionName, "size_matches") == 0)
	{
		int i;
		for (i = 0; i < ws->size_matches_count; i++)
			matchFini (&ws->size_matches[i]);

		ws->size_matches_count = optionValue->list.nItem;
		for (i = 0; i < ws->size_matches_count; i++)
		{
			matchInit (&ws->size_matches[i]);
			matchAddFromString (&ws->size_matches[i],
			                    optionValue->list.item[i].s);
			matchUpdate (&ws->size_matches[i]);
		}
	}

	if (strcasecmp (optionName, "size_matches") == 0 ||
	    strcasecmp (optionName, "size_width_values") == 0 ||
	    strcasecmp (optionName, "size_height_values") == 0)
	{
		CompWindow *w;

		for (w = screen->windows; w; w = w->next)
		{
			int width, height;

			if (winrulesMatchSize (w, &width, &height))
				winrulesUpdateWindowSize (w, width, height);
		}
	}

	if (updateStateMask)
	{
		CompWindow *w;

		for (w = screen->windows; w; w = w->next)
			winrulesUpdateState (w, match, updateStateMask);
	}

	if (updateActionsMask)
	{
		CompWindow *w;

		for (w = screen->windows; w; w = w->next)
			winrulesSetAllowedActions (w, match, updateActionsMask);
	}
}

static Bool
winrulesApplyRules (CompWindow *w)
{
	int width, height;

	WINRULES_SCREEN (w->screen);

	winrulesUpdateState (w,
	                     &ws->skiptaskbar_match,
	                     CompWindowStateSkipTaskbarMask);

	winrulesUpdateState (w,
	                     &ws->skippager_match,
	                     CompWindowStateSkipPagerMask);

	winrulesUpdateState (w,
	                     &ws->above_match,
	                     CompWindowStateAboveMask);

	winrulesUpdateState (w,
	                     &ws->below_match,
	                     CompWindowStateBelowMask);

	winrulesUpdateState (w,
	                     &ws->sticky_match,
	                     CompWindowStateStickyMask);

	winrulesUpdateState (w,
	                     &ws->fullscreen_match,
	                     CompWindowStateFullscreenMask);

	winrulesUpdateState (w,
	                     &ws->maximize_match,
	                     CompWindowStateMaximizedHorzMask |
	                     CompWindowStateMaximizedVertMask);

	winrulesSetAllowedActions (w,
	                           &ws->no_move_match,
	                           CompWindowActionMoveMask);

	winrulesSetAllowedActions (w,
	                           &ws->no_resize_match,
	                           CompWindowActionResizeMask);

	winrulesSetAllowedActions (w,
	                           &ws->no_minimize_match,
	                           CompWindowActionMinimizeMask);

	winrulesSetAllowedActions (w,
	                           &ws->no_maximize_match,
	                           CompWindowActionMaximizeVertMask |
	                           CompWindowActionMaximizeHorzMask);

	winrulesSetAllowedActions (w,
	                           &ws->no_close_match,
	                           CompWindowActionCloseMask);

	winrulesSetNoAlpha (w, &ws->no_argb_match);

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
winrulesHandleEvent (XEvent      *event)
{
	CompWindow *w;

	WINRULES_DISPLAY (&display);

	if (event->type == MapRequest)
	{
		w = findWindowAtDisplay (event->xmap.window);
		if (w)
		{
			WINRULES_SCREEN (w->screen);

			winrulesSetNoFocus (w, &ws->no_focus_match);
			winrulesApplyRules (w);
		}
	}

	UNWRAP (wd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (wd, &display, handleEvent, winrulesHandleEvent);
}

static void
winrulesGetAllowedActionsForWindow (CompWindow   *w,
                                    unsigned int *setActions,
                                    unsigned int *clearActions)
{
	WINRULES_SCREEN (w->screen);
	WINRULES_WINDOW (w);

	UNWRAP (ws, w->screen, getAllowedActionsForWindow);
	(*w->screen->getAllowedActionsForWindow)(w, setActions, clearActions);
	WRAP (ws, w->screen, getAllowedActionsForWindow,
	      winrulesGetAllowedActionsForWindow);

	if (ww)
		*clearActions |= ~ww->allowedActions;
}

static void
winrulesMatchPropertyChanged (CompWindow  *w)
{
	WINRULES_DISPLAY (&display);

	winrulesApplyRules (w);

	UNWRAP (wd, &display, matchPropertyChanged);
	(display.matchPropertyChanged) (w);
	WRAP (wd, &display, matchPropertyChanged, winrulesMatchPropertyChanged);
}

static Bool
winrulesInitDisplay (CompPlugin  *p,
                     CompDisplay *d)
{
	WinrulesDisplay *wd;

	wd = malloc (sizeof (WinrulesDisplay));
	if (!wd)
		return FALSE;

	wd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (wd->screenPrivateIndex < 0)
	{
		free (wd);
		return FALSE;
	}

	WRAP (wd, d, handleEvent, winrulesHandleEvent);
	WRAP (wd, d, matchPropertyChanged, winrulesMatchPropertyChanged);

	d->privates[displayPrivateIndex].ptr = wd;

	return TRUE;
}

static void
winrulesFiniDisplay (CompPlugin  *p,
                     CompDisplay *d)
{
	WINRULES_DISPLAY (d);

	freeScreenPrivateIndex (wd->screenPrivateIndex);

	UNWRAP (wd, d, handleEvent);
	UNWRAP (wd, d, matchPropertyChanged);

	free (wd);
}

static Bool
winrulesInitScreen (CompPlugin *p,
                    CompScreen *s)
{
	WinrulesScreen *ws;

	WINRULES_DISPLAY (&display);

	ws = malloc (sizeof (WinrulesScreen));
	if (!ws)
		return FALSE;

	ws->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (ws->windowPrivateIndex < 0)
	{
		free (ws);
		return FALSE;
	}

	WRAP (ws, s, getAllowedActionsForWindow,
	      winrulesGetAllowedActionsForWindow);

	const BananaValue *
	option_skiptaskbar_match = bananaGetOption (bananaIndex,
	                                            "skiptaskbar_match",
	                                            s->screenNum);

	matchInit (&ws->skiptaskbar_match);
	matchAddFromString (&ws->skiptaskbar_match, option_skiptaskbar_match->s);
	matchUpdate (&ws->skiptaskbar_match);

	const BananaValue *
	option_skippager_match = bananaGetOption (bananaIndex,
	                                          "skippager_match",
	                                          s->screenNum);

	matchInit (&ws->skippager_match);
	matchAddFromString (&ws->skippager_match, option_skippager_match->s);
	matchUpdate (&ws->skippager_match);

	const BananaValue *
	option_above_match = bananaGetOption (bananaIndex,
	                                      "above_match",
	                                      s->screenNum);

	matchInit (&ws->above_match);
	matchAddFromString (&ws->above_match, option_above_match->s);
	matchUpdate (&ws->above_match);

	const BananaValue *
	option_below_match = bananaGetOption (bananaIndex,
	                                      "below_match",
	                                      s->screenNum);

	matchInit (&ws->below_match);
	matchAddFromString (&ws->below_match, option_below_match->s);
	matchUpdate (&ws->below_match);

	const BananaValue *
	option_sticky_match = bananaGetOption (bananaIndex,
	                                       "sticky_match",
	                                       s->screenNum);

	matchInit (&ws->sticky_match);
	matchAddFromString (&ws->sticky_match, option_sticky_match->s);
	matchUpdate (&ws->sticky_match);

	const BananaValue *
	option_fullscreen_match = bananaGetOption (bananaIndex,
	                                           "fullscreen_match",
	                                           s->screenNum);

	matchInit (&ws->fullscreen_match);
	matchAddFromString (&ws->fullscreen_match, option_fullscreen_match->s);
	matchUpdate (&ws->fullscreen_match);

	const BananaValue *
	option_maximize_match = bananaGetOption (bananaIndex,
	                                         "maximize_match",
	                                         s->screenNum);

	matchInit (&ws->maximize_match);
	matchAddFromString (&ws->maximize_match, option_maximize_match->s);
	matchUpdate (&ws->maximize_match);

	const BananaValue *
	option_no_argb_match = bananaGetOption (bananaIndex,
	                                        "no_argb_match",
	                                         s->screenNum);

	matchInit (&ws->no_argb_match);
	matchAddFromString (&ws->no_argb_match, option_no_argb_match->s);
	matchUpdate (&ws->no_argb_match);

	const BananaValue *
	option_no_move_match = bananaGetOption (bananaIndex,
	                                        "no_move_match",
	                                        s->screenNum);

	matchInit (&ws->no_move_match);
	matchAddFromString (&ws->no_move_match, option_no_move_match->s);
	matchUpdate (&ws->no_move_match);

	const BananaValue *
	option_no_resize_match = bananaGetOption (bananaIndex,
	                                          "no_resize_match",
	                                          s->screenNum);

	matchInit (&ws->no_resize_match);
	matchAddFromString (&ws->no_resize_match, option_no_resize_match->s);
	matchUpdate (&ws->no_resize_match);

	const BananaValue *
	option_no_minimize_match = bananaGetOption (bananaIndex,
	                                            "no_minimize_match",
	                                            s->screenNum);

	matchInit (&ws->no_minimize_match);
	matchAddFromString (&ws->no_minimize_match, option_no_minimize_match->s);
	matchUpdate (&ws->no_minimize_match);

	const BananaValue *
	option_no_maximize_match = bananaGetOption (bananaIndex,
	                                            "no_maximize_match",
	                                            s->screenNum);

	matchInit (&ws->no_maximize_match);
	matchAddFromString (&ws->no_maximize_match, option_no_maximize_match->s);
	matchUpdate (&ws->no_maximize_match);

	const BananaValue *
	option_no_close_match = bananaGetOption (bananaIndex,
	                                         "no_close_match",
	                                         s->screenNum);

	matchInit (&ws->no_close_match);
	matchAddFromString (&ws->no_close_match, option_no_close_match->s);
	matchUpdate (&ws->no_close_match);

	const BananaValue *
	option_no_focus_match = bananaGetOption (bananaIndex,
	                                         "no_focus_match",
	                                         s->screenNum);

	matchInit (&ws->no_focus_match);
	matchAddFromString (&ws->no_focus_match, option_no_focus_match->s);
	matchUpdate (&ws->no_focus_match);

	const BananaValue *
	option_size_matches = bananaGetOption (bananaIndex,
	                                       "size_matches",
	                                       s->screenNum);

	ws->size_matches_count = option_size_matches->list.nItem;
	int i;
	for (i = 0; i < option_size_matches->list.nItem; i++)
	{
		matchInit (&ws->size_matches[i]);
		matchAddFromString (&ws->size_matches[i],
		                    option_size_matches->list.item[i].s);
		matchUpdate (&ws->size_matches[i]);
	}

	s->privates[wd->screenPrivateIndex].ptr = ws;

	return TRUE;
}

static void
winrulesFiniScreen (CompPlugin *p,
                    CompScreen *s)
{
	WINRULES_SCREEN (s);

	UNWRAP (ws, s, getAllowedActionsForWindow);

	matchFini (&ws->skiptaskbar_match);
	matchFini (&ws->skippager_match);
	matchFini (&ws->above_match);
	matchFini (&ws->below_match);
	matchFini (&ws->sticky_match);
	matchFini (&ws->fullscreen_match);
	matchFini (&ws->maximize_match);
	matchFini (&ws->no_argb_match);
	matchFini (&ws->no_move_match);
	matchFini (&ws->no_resize_match);
	matchFini (&ws->no_minimize_match);
	matchFini (&ws->no_maximize_match);
	matchFini (&ws->no_close_match);
	matchFini (&ws->no_focus_match);

	int i;
	for (i = 0; i < ws->size_matches_count; i++)
		matchFini (&ws->size_matches[i]);

	freeWindowPrivateIndex (s, ws->windowPrivateIndex);

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

	w->privates[ws->windowPrivateIndex].ptr = ww;

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

	w->privates[ws->windowPrivateIndex].ptr = NULL;
}

static Bool
winrulesInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("winrules", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("winrules");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, winrulesChangeNotify);

	return TRUE;
}

static void
winrulesFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable winrulesVTable = {
	"winrules",
	winrulesInit,
	winrulesFini,
	winrulesInitDisplay,
	winrulesFiniDisplay,
	winrulesInitScreen,
	winrulesFiniScreen,
	winrulesInitWindow,
	winrulesFiniWindow
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &winrulesVTable;
}
