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
 *
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

int groupDisplayPrivateIndex;

static const GlowTextureProperties glowTextureProperties[2] = {
    /* GlowTextureRectangular */
    {glowTexRect, 32, 21},
    /* GlowTextureRing */
    {glowTexRing, 32, 16}
};

static void
groupScreenOptionChanged (CompScreen         *s,
			  CompOption         *opt,
			  GroupScreenOptions num)
{
    GroupSelection *group;

    GROUP_SCREEN (s);

    switch (num)
    {
	case GroupScreenOptionTabBaseColor:
	case GroupScreenOptionTabHighlightColor:
	case GroupScreenOptionTabBorderColor:
	case GroupScreenOptionTabStyle:
	case GroupScreenOptionBorderRadius:
	case GroupScreenOptionBorderWidth:
	    for (group = gs->groups; group; group = group->next)
		if (group->tabBar)
		    groupRenderTabBarBackground (group);
	    break;
	case GroupScreenOptionTabbarFontSize:
	case GroupScreenOptionTabbarFontColor:
	    for (group = gs->groups; group; group = group->next)
		groupRenderWindowTitle (group);
	    break;
	case GroupScreenOptionThumbSize:
	case GroupScreenOptionThumbSpace:
	    for (group = gs->groups; group; group = group->next)
		if (group->tabBar)
		{
		    BoxPtr box = &group->tabBar->region->extents;
		    groupRecalcTabBarPos (group, (box->x1 + box->x2 ) / 2,
					  box->x1, box->x2);
		}
	    break;
	case GroupScreenOptionGlow:
	case GroupScreenOptionGlowSize:
	    {
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
		break;
	    }
	case GroupScreenOptionGlowType:
	    {
		GroupGlowTypeEnum     glowType;
		GlowTextureProperties *glowProperty;

		GROUP_DISPLAY (s->display);
		glowType = groupGetGlowType (s);
		glowProperty = &gd->glowTextureProperties[glowType];

		finiTexture (s, &gs->glowTexture);
		initTexture (s, &gs->glowTexture);

		imageDataToTexture (s, &gs->glowTexture,
				    glowProperty->textureData,
				    glowProperty->textureSize,
				    glowProperty->textureSize,
				    GL_RGBA, GL_UNSIGNED_BYTE);

		if (groupGetGlow (s) && gs->groups)
		{
		    CompWindow *w;

		    for (w = s->windows; w; w = w->next)
			groupComputeGlowQuads (w, &gs->glowTexture.matrix);

		    damageScreen (s);
		}
		break;
	    }

	default:
	    break;
    }
}

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
	Bool     tabbed;
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

	if (groupGetAutotabCreate (s) && groupIsGroupWindow (w))
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
    int          index;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    gd = malloc (sizeof (GroupDisplay));
    if (!gd)
	return FALSE;

    gd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (gd->screenPrivateIndex < 0)
    {
	free (gd);
	return FALSE;
    }

    if (checkPluginABI ("text", TEXT_ABIVERSION) &&
	getPluginDisplayIndex (d, "text", &index))
    {
	gd->textFunc = d->base.privates[index].ptr;
    }
    else
    {
	compLogMessage ("group", CompLogLevelWarn,
			"No compatible text plugin loaded.");
	gd->textFunc = NULL;
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

    groupSetSelectButtonInitiate (d, groupSelect);
    groupSetSelectButtonTerminate (d, groupSelectTerminate);
    groupSetSelectSingleKeyInitiate (d, groupSelectSingle);
    groupSetGroupKeyInitiate (d, groupGroupWindows);
    groupSetUngroupKeyInitiate (d, groupUnGroupWindows);
    groupSetTabmodeKeyInitiate (d, groupInitTab);
    groupSetChangeTabLeftKeyInitiate (d, groupChangeTabLeft);
    groupSetChangeTabRightKeyInitiate (d, groupChangeTabRight);
    groupSetRemoveKeyInitiate (d, groupRemoveWindow);
    groupSetCloseKeyInitiate (d, groupCloseWindows);
    groupSetIgnoreKeyInitiate (d, groupSetIgnore);
    groupSetIgnoreKeyTerminate (d, groupUnsetIgnore);
    groupSetChangeColorKeyInitiate (d, groupChangeColor);

    d->base.privates[groupDisplayPrivateIndex].ptr = gd;

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

    freeScreenPrivateIndex (d, gd->screenPrivateIndex);

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
    GroupGlowTypeEnum glowType;

    GROUP_DISPLAY (s->display);

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

    s->base.privates[gd->screenPrivateIndex].ptr = gs;

    groupSetTabHighlightColorNotify (s, groupScreenOptionChanged);
    groupSetTabBaseColorNotify (s, groupScreenOptionChanged);
    groupSetTabBorderColorNotify (s, groupScreenOptionChanged);
    groupSetTabbarFontSizeNotify (s, groupScreenOptionChanged);
    groupSetTabbarFontColorNotify (s, groupScreenOptionChanged);
    groupSetGlowNotify (s, groupScreenOptionChanged);
    groupSetGlowTypeNotify (s, groupScreenOptionChanged);
    groupSetGlowSizeNotify (s, groupScreenOptionChanged);
    groupSetTabStyleNotify (s, groupScreenOptionChanged);
    groupSetThumbSizeNotify (s, groupScreenOptionChanged);
    groupSetThumbSpaceNotify (s, groupScreenOptionChanged);
    groupSetBorderWidthNotify (s, groupScreenOptionChanged);
    groupSetBorderRadiusNotify (s, groupScreenOptionChanged);

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

    glowType = groupGetGlowType (s);
    imageDataToTexture (s, &gs->glowTexture,
			glowTextureProperties[glowType].textureData,
			glowTextureProperties[glowType].textureSize,
			glowTextureProperties[glowType].textureSize,
			GL_RGBA, GL_UNSIGNED_BYTE);

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

	for (group = gs->groups; group;)
	{
	    if (group->tabBar)
	    {
		GroupTabBarSlot *slot, *nextSlot;

		for (slot = group->tabBar->slots; slot;)
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
		    XDestroyWindow (s->display->display,
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

    w->base.privates[gs->windowPrivateIndex].ptr = gw;

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

/*
 * groupInit
 *
 */
static Bool
groupInit (CompPlugin *p)
{
    groupDisplayPrivateIndex = allocateDisplayPrivateIndex ();
    if (groupDisplayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

/*
 * groupFini
 *
 */
static void
groupFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (groupDisplayPrivateIndex);
}

static CompBool
groupInitObject (CompPlugin *p,
		 CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) groupInitDisplay,
	(InitPluginObjectProc) groupInitScreen,
	(InitPluginObjectProc) groupInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
groupFiniObject (CompPlugin *p,
		 CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) groupFiniDisplay,
	(FiniPluginObjectProc) groupFiniScreen,
	(FiniPluginObjectProc) groupFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

/*
 * groupVTable
 *
 */
CompPluginVTable groupVTable = {
    "group",
    0,
    groupInit,
    groupFini,
    groupInitObject,
    groupFiniObject,
    0,
    0
};

/*
 * getCompPluginInfo
 *
 */
CompPluginVTable*
getCompPluginInfo (void)
{
    return &groupVTable;
}
