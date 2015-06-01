/**
 *
 * Compiz group plugin
 *
 * init.c
 *
 * Copyright : (C) 2006-2007 by Patrick Niklaus, Roi Cohen, Danny Baumann
 * Authors: Patrick Niklaus <patrick.niklaus@googlemail.com>
 *          Roi Cohen       <roico.beryl@gmail.com>
 *          Danny Baumann   <maniac@opencompositing.org>
 * Copyright : (C) 2015 by Michail Bitzes
 *            Michail Bitzes <noodlylight@gmail.com>
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
 **/

#include "group-internal.h"
#include "group_glow.h"

int bananaIndex;

int displayPrivateIndex;

static const GlowTextureProperties glowTextureProperties[2] = {
	/* GlowTextureRectangular */
	{glowTexRect, 32, 21},
	/* GlowTextureRing */
	{glowTexRing, 32, 16}
};

/*
 * groupApplyInitialActions
 *
 * timer callback for stuff that needs to be called after all
 * screens and windows are initialized
 *
 */
static Bool
groupApplyInitialActions (void *closure)
{
	CompScreen *s = (CompScreen *) closure;
	CompWindow *w;

	GROUP_SCREEN (s);

	gs->initialActionsTimeoutHandle = 0;

	/* we need to do it from top to buttom of the stack to avoid problems
	   with a reload of Compiz and tabbed static groups. (topTab will always
	   be above the other windows in the group) */
	for (w = s->reverseWindows; w; w = w->prev)
	{
		Bool tabbed;
		long int id;
		GLushort color[3];

		GROUP_WINDOW (w);

		/* read window property to see if window was grouped
		   before - if it was, regroup */
		if (groupCheckWindowProperty (w, &id, &tabbed, color))
		{
			GroupSelection *group;

			for (group = gs->groups; group; group = group->next)
				if (group->identifier == id)
					break;

			groupAddWindowToGroup (w, group, id);
			if (tabbed)
				groupTabGroup (w);

			gw->group->color[0] = color[0];
			gw->group->color[1] = color[1];
			gw->group->color[2] = color[2];

			groupRenderTopTabHighlight (gw->group);
			damageScreen (w->screen);
		}

		const BananaValue *
		option_autotab_create = bananaGetOption (bananaIndex,
		                                         "autotab_create",
		                                         s->screenNum);

		if (option_autotab_create->b && groupIsGroupWindow (w))
		{
			if (!gw->group && (gw->windowState == WindowNormal))
			{
				groupAddWindowToGroup (w, NULL, 0);
				groupTabGroup (w);
			}
		}
	}

	return FALSE;
}

/*
 * groupInitDisplay
 *
 */
static Bool
groupInitDisplay (CompPlugin  *p,
                  CompDisplay *d)
{
	GroupDisplay *gd;

	gd = malloc (sizeof (GroupDisplay));
	if (!gd)
		return FALSE;

	gd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (gd->screenPrivateIndex < 0)
	{
		free (gd);
		return FALSE;
	}

	gd->glowTextureProperties =
	        (GlowTextureProperties*) glowTextureProperties;
	gd->ignoreMode = FALSE;
	gd->lastRestackedGroup = NULL;
	gd->resizeInfo = NULL;

	gd->groupWinPropertyAtom = XInternAtom (d->display,
	                                        "_COMPIZ_GROUP", 0);
	gd->resizeNotifyAtom     = XInternAtom (d->display,
	                                        "_COMPIZ_RESIZE_NOTIFY", 0);

	WRAP (gd, d, handleEvent, groupHandleEvent);

	const BananaValue *
	option_select_button = bananaGetOption (bananaIndex,
	                                        "select_button",
	                                        -1);

	registerButton (option_select_button->s, &gd->select_button);

	const BananaValue *
	option_select_single_key = bananaGetOption (bananaIndex,
	                                            "select_single_key",
	                                            -1);

	registerKey (option_select_single_key->s, &gd->select_single_key);

	const BananaValue *
	option_group_key = bananaGetOption (bananaIndex,
	                                    "group_key",
	                                    -1);

	registerKey (option_group_key->s, &gd->group_key);

	const BananaValue *
	option_ungroup_key = bananaGetOption (bananaIndex,
	                                      "ungroup_key",
	                                      -1);

	registerKey (option_ungroup_key->s, &gd->ungroup_key);

	const BananaValue *
	option_remove_key = bananaGetOption (bananaIndex,
	                                     "remove_key",
	                                     -1);

	registerKey (option_remove_key->s, &gd->remove_key);

	const BananaValue *
	option_close_key = bananaGetOption (bananaIndex,
	                                    "close_key",
	                                    -1);

	registerKey (option_close_key->s, &gd->close_key);

	const BananaValue *
	option_ignore_key = bananaGetOption (bananaIndex,
	                                     "ignore_key",
	                                     -1);

	registerKey (option_ignore_key->s, &gd->ignore_key);

	const BananaValue *
	option_tabmode_key = bananaGetOption (bananaIndex,
	                                      "tabmode_key",
	                                      -1);

	registerKey (option_tabmode_key->s, &gd->tabmode_key);

	const BananaValue *
	option_change_tab_left_key = bananaGetOption (bananaIndex,
	                                              "change_tab_left_key",
	                                              -1);

	registerKey (option_change_tab_left_key->s,
	             &gd->change_tab_left_key);

	const BananaValue *
	option_change_tab_right_key = bananaGetOption (bananaIndex,
	                                               "change_tab_right_key",
	                                               -1);

	registerKey (option_change_tab_right_key->s,
	             &gd->change_tab_right_key);

	const BananaValue *
	option_change_color_key = bananaGetOption (bananaIndex,
	                                           "change_color_key",
	                                           -1);

	registerKey (option_change_color_key->s,
	             &gd->change_color_key);

	d->privates[displayPrivateIndex].ptr = gd;

	srand (time (NULL));

	return TRUE;
}

/*
 * groupFiniDisplay
 *
 */
static void
groupFiniDisplay (CompPlugin  *p,
                  CompDisplay *d)
{
	GROUP_DISPLAY (d);

	freeScreenPrivateIndex (gd->screenPrivateIndex);

	UNWRAP (gd, d, handleEvent);

	free (gd);
}

/*
 * groupInitScreen
 *
 */
static Bool
groupInitScreen (CompPlugin *p,
                 CompScreen *s)
{
	GroupScreen       *gs;
	int glowType;

	GROUP_DISPLAY (&display);

	gs = malloc (sizeof (GroupScreen));
	if (!gs)
		return FALSE;

	gs->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (gs->windowPrivateIndex < 0)
	{
		free (gs);
		return FALSE;
	}

	WRAP (gs, s, windowMoveNotify, groupWindowMoveNotify);
	WRAP (gs, s, windowResizeNotify, groupWindowResizeNotify);
	WRAP (gs, s, getOutputExtentsForWindow, groupGetOutputExtentsForWindow);
	WRAP (gs, s, preparePaintScreen, groupPreparePaintScreen);
	WRAP (gs, s, paintOutput, groupPaintOutput);
	WRAP (gs, s, drawWindow, groupDrawWindow);
	WRAP (gs, s, paintWindow, groupPaintWindow);
	WRAP (gs, s, paintTransformedOutput, groupPaintTransformedOutput);
	WRAP (gs, s, donePaintScreen, groupDonePaintScreen);
	WRAP (gs, s, windowGrabNotify, groupWindowGrabNotify);
	WRAP (gs, s, windowUngrabNotify, groupWindowUngrabNotify);
	WRAP (gs, s, damageWindowRect, groupDamageWindowRect);
	WRAP (gs, s, windowStateChangeNotify, groupWindowStateChangeNotify);
	WRAP (gs, s, activateWindow, groupActivateWindow);

	s->privates[gd->screenPrivateIndex].ptr = gs;

	gs->groups = NULL;

	gs->tmpSel.windows = NULL;
	gs->tmpSel.nWins   = 0;

	gs->grabIndex = 0;
	gs->grabState = ScreenGrabNone;

	gs->lastHoveredGroup = NULL;

	gs->queued          = FALSE;
	gs->pendingMoves    = NULL;
	gs->pendingGrabs    = NULL;
	gs->pendingUngrabs  = NULL;

	gs->dequeueTimeoutHandle = 0;

	gs->draggedSlot            = NULL;
	gs->dragged                = FALSE;
	gs->dragHoverTimeoutHandle = 0;

	gs->prevX = 0;
	gs->prevY = 0;

	gs->showDelayTimeoutHandle = 0;

	/* one-shot timeout for stuff that needs to be initialized after
	   all screens and windows are initialized */
	gs->initialActionsTimeoutHandle =
	        compAddTimeout (0, 0, groupApplyInitialActions, (void *) s);

	initTexture (s, &gs->glowTexture);

	const BananaValue *
	option_glow_type = bananaGetOption (bananaIndex,
	                                    "glow_type",
	                                    s->screenNum);

	glowType = option_glow_type->i;
	imageDataToTexture (s, &gs->glowTexture,
	                    glowTextureProperties[glowType].textureData,
	                    glowTextureProperties[glowType].textureSize,
	                    glowTextureProperties[glowType].textureSize,
	                    GL_RGBA, GL_UNSIGNED_BYTE);

	const BananaValue *
	option_window_match = bananaGetOption (bananaIndex,
	                                       "window_match",
	                                       s->screenNum);

	matchInit (&gs->window_match);
	matchAddFromString (&gs->window_match, option_window_match->s);
	matchUpdate (&gs->window_match);

	const BananaValue *
	option_autotab_windows = bananaGetOption (bananaIndex,
	                                          "autotab_windows",
	                                          s->screenNum);

	gs->autotabCount = option_autotab_windows->list.nItem;
	gs->autotab = malloc (gs->autotabCount * sizeof (CompMatch));

	int i;
	for (i = 0; i <= gs->autotabCount - 1; i++)
	{
		matchInit (&gs->autotab[i]);
		matchAddFromString (&gs->autotab[i],
		                    option_autotab_windows->list.item[i].s);
		matchUpdate (&gs->autotab[i]);
	}

	return TRUE;
}

/*
 * groupFiniScreen
 *
 */
static void
groupFiniScreen (CompPlugin *p,
                 CompScreen *s)
{
	GROUP_SCREEN (s);

	if (gs->groups)
	{
		GroupSelection *group, *nextGroup;

		for (group = gs->groups; group; )
		{
			if (group->tabBar)
			{
				GroupTabBarSlot *slot, *nextSlot;

				for (slot = group->tabBar->slots; slot; )
				{
					if (slot->region)
						XDestroyRegion (slot->region);

					nextSlot = slot->next;
					free (slot);
					slot = nextSlot;
				}

				groupDestroyCairoLayer (s, group->tabBar->textLayer);
				groupDestroyCairoLayer (s, group->tabBar->bgLayer);
				groupDestroyCairoLayer (s, group->tabBar->selectionLayer);

				if (group->inputPrevention)
					XDestroyWindow (display.display,
					                group->inputPrevention);

				if (group->tabBar->region)
					XDestroyRegion (group->tabBar->region);

				if (group->tabBar->timeoutHandle)
					compRemoveTimeout (group->tabBar->timeoutHandle);

				free (group->tabBar);
			}

			nextGroup = group->next;
			free (group);
			group = nextGroup;
		}
	}

	if (gs->tmpSel.windows)
		free (gs->tmpSel.windows);

	if (gs->grabIndex)
		groupGrabScreen (s, ScreenGrabNone);

	if (gs->dragHoverTimeoutHandle)
		compRemoveTimeout (gs->dragHoverTimeoutHandle);

	if (gs->showDelayTimeoutHandle)
		compRemoveTimeout (gs->showDelayTimeoutHandle);

	if (gs->dequeueTimeoutHandle)
		compRemoveTimeout (gs->dequeueTimeoutHandle);

	if (gs->initialActionsTimeoutHandle)
		compRemoveTimeout (gs->initialActionsTimeoutHandle);

	freeWindowPrivateIndex (s, gs->windowPrivateIndex);

	matchFini (&gs->window_match);

	UNWRAP (gs, s, windowMoveNotify);
	UNWRAP (gs, s, windowResizeNotify);
	UNWRAP (gs, s, getOutputExtentsForWindow);
	UNWRAP (gs, s, preparePaintScreen);
	UNWRAP (gs, s, paintOutput);
	UNWRAP (gs, s, drawWindow);
	UNWRAP (gs, s, paintWindow);
	UNWRAP (gs, s, paintTransformedOutput);
	UNWRAP (gs, s, donePaintScreen);
	UNWRAP (gs, s, windowGrabNotify);
	UNWRAP (gs, s, windowUngrabNotify);
	UNWRAP (gs, s, damageWindowRect);
	UNWRAP (gs, s, windowStateChangeNotify);
	UNWRAP (gs, s, activateWindow);

	finiTexture (s, &gs->glowTexture);
	free (gs);
}

/*
 * groupInitWindow
 *
 */
static Bool
groupInitWindow (CompPlugin *p,
                 CompWindow *w)
{
	GroupWindow *gw;

	GROUP_SCREEN (w->screen);

	gw = malloc (sizeof (GroupWindow));
	if (!gw)
		return FALSE;

	gw->group        = NULL;
	gw->slot         = NULL;
	gw->glowQuads    = NULL;
	gw->inSelection  = FALSE;
	gw->needsPosSync = FALSE;
	gw->readOnlyProperty = FALSE;

	/* for tab */
	gw->animateState = 0;

	gw->tx        = 0.0f;
	gw->ty        = 0.0f;
	gw->xVelocity = 0.0f;
	gw->yVelocity = 0.0f;

	gw->orgPos.x        = 0;
	gw->orgPos.y        = 0;
	gw->mainTabOffset.x = 0;
	gw->mainTabOffset.y = 0;
	gw->destination.x   = 0;
	gw->destination.y   = 0;

	gw->windowHideInfo = NULL;
	gw->resizeGeometry = NULL;

	if (w->minimized)
		gw->windowState = WindowMinimized;
	else if (w->shaded)
		gw->windowState = WindowShaded;
	else
		gw->windowState = WindowNormal;

	w->privates[gs->windowPrivateIndex].ptr = gw;

	groupComputeGlowQuads (w, &gs->glowTexture.matrix);

	return TRUE;
}

/*
 * groupFiniWindow
 *
 */
static void
groupFiniWindow (CompPlugin *p,
                 CompWindow *w)
{
	GROUP_WINDOW (w);

	if (gw->windowHideInfo)
		groupSetWindowVisibility (w, TRUE);

	gw->readOnlyProperty = TRUE;

	/* FIXME: this implicitly calls the wrapped function activateWindow
	   (via groupDeleteTabBarSlot -> groupUnhookTabBarSlot -> groupChangeTab)
	   --> better wrap into removeObject and call it for removeWindow
	 */
	if (gw->group)
		groupDeleteGroupWindow (w);

	if (gw->glowQuads)
		free (gw->glowQuads);

	free (gw);
}

static void
groupChangeNotify (const char        *optionName,
                   BananaType        optionType,
                   const BananaValue *optionValue,
                   int               screenNum)
{
	GROUP_DISPLAY (&display);

	if (strcasecmp (optionName, "window_match") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);
		GROUP_SCREEN (s);

		matchFini (&gs->window_match);
		matchInit (&gs->window_match);
		matchAddFromString (&gs->window_match, optionValue->s);
		matchUpdate (&gs->window_match);
	}
	else if (strcasecmp (optionName, "tab_base_color") == 0 ||
	         strcasecmp (optionName, "tab_highlight_color") == 0 ||
	         strcasecmp (optionName, "tab_border_color") == 0 ||
	         strcasecmp (optionName, "tab_style") == 0 ||
	         strcasecmp (optionName, "border_radius") == 0 ||
	         strcasecmp (optionName, "border_width") == 0)
	{
		GroupSelection *group;

		CompScreen *s = getScreenFromScreenNum (screenNum);
		GROUP_SCREEN (s);

		for (group = gs->groups; group; group = group->next)
			if (group->tabBar)
				groupRenderTabBarBackground (group);
	}
	else if (strcasecmp (optionName, "tabbar_font_size") == 0 ||
	         strcasecmp (optionName, "tabbar_font_color") == 0)
	{
		GroupSelection *group;

		CompScreen *s = getScreenFromScreenNum (screenNum);
		GROUP_SCREEN (s);

		for (group = gs->groups; group; group = group->next)
			groupRenderWindowTitle (group);
	}
	else if (strcasecmp (optionName, "thumb_size") == 0 ||
	         strcasecmp (optionName, "thumb_space") == 0)
	{
		GroupSelection *group;

		CompScreen *s = getScreenFromScreenNum (screenNum);
		GROUP_SCREEN (s);

		for (group = gs->groups; group; group = group->next)
			if (group->tabBar)
			{
				BoxPtr box = &group->tabBar->region->extents;
				groupRecalcTabBarPos (group, (box->x1 + box->x2 ) / 2,
				                      box->x1, box->x2);
			}
	}
	else if (strcasecmp (optionName, "glow") == 0 ||
	         strcasecmp (optionName, "glow_size") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);
		GROUP_SCREEN (s);

		CompWindow *w;

		for (w = s->windows; w; w = w->next)
		{
			GROUP_WINDOW (w);

			groupComputeGlowQuads (w, &gs->glowTexture.matrix);
			if (gw->glowQuads)
			{
				damageWindowOutputExtents (w);
				updateWindowOutputExtents (w);
				damageWindowOutputExtents (w);
			}
		}
	}
	else if (strcasecmp (optionName, "glow_type") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);
		GROUP_SCREEN (s);

		int glowType;
		GlowTextureProperties *glowProperty;

		GROUP_DISPLAY (&display);

		const BananaValue *
		option_glow_type = bananaGetOption (bananaIndex,
		                                    "glow_type",
		                                    s->screenNum);

		glowType = option_glow_type->i;
		glowProperty = &gd->glowTextureProperties[glowType];

		finiTexture (s, &gs->glowTexture);
		initTexture (s, &gs->glowTexture);

		imageDataToTexture (s, &gs->glowTexture,
		                    glowProperty->textureData,
		                    glowProperty->textureSize,
		                    glowProperty->textureSize,
		                    GL_RGBA, GL_UNSIGNED_BYTE);

		const BananaValue *
		option_glow = bananaGetOption (bananaIndex,
		                               "glow",
		                               s->screenNum);

		if (option_glow->b && gs->groups)
		{
			CompWindow *w;

			for (w = s->windows; w; w = w->next)
				groupComputeGlowQuads (w, &gs->glowTexture.matrix);

			damageScreen (s);
		}
	}
	else if (strcasecmp (optionName, "select_button") == 0)
		updateButton (optionValue->s, &gd->select_button);
	else if (strcasecmp (optionName, "select_single_key") == 0)
		updateKey (optionValue->s, &gd->select_single_key);
	else if (strcasecmp (optionName, "group_key") == 0)
		updateKey (optionValue->s, &gd->group_key);
	else if (strcasecmp (optionName, "ungroup_key") == 0)
		updateKey (optionValue->s, &gd->ungroup_key);
	else if (strcasecmp (optionName, "remove_key") == 0)
		updateKey (optionValue->s, &gd->remove_key);
	else if (strcasecmp (optionName, "close_key") == 0)
		updateKey (optionValue->s, &gd->close_key);
	else if (strcasecmp (optionName, "ignore_key") == 0)
		updateKey (optionValue->s, &gd->ignore_key);
	else if (strcasecmp (optionName, "tabmode_key") == 0)
		updateKey (optionValue->s, &gd->tabmode_key);
	else if (strcasecmp (optionName, "change_tab_left_key") == 0)
		updateKey (optionValue->s, &gd->change_tab_left_key);
	else if (strcasecmp (optionName, "change_tab_right_key") == 0)
		updateKey (optionValue->s, &gd->change_tab_right_key);
	else if (strcasecmp (optionName, "change_color_key") == 0)
		updateKey (optionValue->s, &gd->change_color_key);
	else if (strcasecmp (optionName, "autotab_windows") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);

		GROUP_SCREEN (s);

		int i;
		if (gs->autotab && gs->autotabCount != 0)
		{
			for (i = 0; i <= gs->autotabCount - 1; i++)
				matchFini (&gs->autotab[i]);

			free (gs->autotab);
		}

		gs->autotabCount = optionValue->list.nItem;
		gs->autotab = malloc (gs->autotabCount * sizeof (CompMatch));

		for (i = 0; i <= gs->autotabCount - 1; i++)
		{
			matchInit (&gs->autotab[i]);
			matchAddFromString (&gs->autotab[i], optionValue->list.item[i].s);
			matchUpdate (&gs->autotab[i]);
		}
	}
}

static Bool
groupInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("group", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("group");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, groupChangeNotify);

	return TRUE;
}

static void
groupFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable groupVTable = {
	"group",
	groupInit,
	groupFini,
	groupInitDisplay,
	groupFiniDisplay,
	groupInitScreen,
	groupFiniScreen,
	groupInitWindow,
	groupFiniWindow
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &groupVTable;
}
