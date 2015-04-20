/**
 *
 * Compiz group plugin
 *
 * paint.c
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

/*
 * groupPaintThumb - taken from switcher and modified for tab bar
 *
 */
static void
groupPaintThumb (GroupSelection      *group,
		 GroupTabBarSlot     *slot,
		 const CompTransform *transform,
		 int                 targetOpacity)
{
    CompWindow            *w = slot->window;
    CompScreen            *s = w->screen;
    AddWindowGeometryProc oldAddWindowGeometry;
    WindowPaintAttrib     wAttrib = w->paint;
    int                   tw, th;

    tw = slot->region->extents.x2 - slot->region->extents.x1;
    th = slot->region->extents.y2 - slot->region->extents.y1;

    /* Wrap drawWindowGeometry to make sure the general
       drawWindowGeometry function is used */
    oldAddWindowGeometry = s->addWindowGeometry;
    s->addWindowGeometry = addWindowGeometry;

    /* animate fade */
    if (group && group->tabBar->state == PaintFadeIn)
    {
	wAttrib.opacity -= wAttrib.opacity * group->tabBar->animationTime /
	                   (groupGetFadeTime (s) * 1000);
    }
    else if (group && group->tabBar->state == PaintFadeOut)
    {
	wAttrib.opacity = wAttrib.opacity * group->tabBar->animationTime /
	                  (groupGetFadeTime (s) * 1000);
    }

    wAttrib.opacity = wAttrib.opacity * targetOpacity / OPAQUE;

    if (w->mapNum)
    {
	FragmentAttrib fragment;
	CompTransform  wTransform = *transform;
	int            width, height;
	int            vx, vy;

	width = w->width + w->output.left + w->output.right;
	height = w->height + w->output.top + w->output.bottom;

	if (width > tw)
	    wAttrib.xScale = (float) tw / width;
	else
	    wAttrib.xScale = 1.0f;
	if (height > th)
	    wAttrib.yScale = (float) tw / height;
	else
	    wAttrib.yScale = 1.0f;

	if (wAttrib.xScale < wAttrib.yScale)
	    wAttrib.yScale = wAttrib.xScale;
	else
	    wAttrib.xScale = wAttrib.yScale;

	/* FIXME: do some more work on the highlight on hover feature
	// Highlight on hover
	if (group && group->tabBar && group->tabBar->hoveredSlot == slot) {
	wAttrib.saturation = 0;
	wAttrib.brightness /= 1.25f;
	}*/

	groupGetDrawOffsetForSlot (slot, &vx, &vy);

	wAttrib.xTranslate = (slot->region->extents.x1 +
			      slot->region->extents.x2) / 2 + vx;
	wAttrib.yTranslate = slot->region->extents.y1 + vy;

	initFragmentAttrib (&fragment, &wAttrib);

	matrixTranslate (&wTransform,
			 wAttrib.xTranslate, wAttrib.yTranslate, 0.0f);
	matrixScale (&wTransform, wAttrib.xScale, wAttrib.yScale, 1.0f);
	matrixTranslate (&wTransform, -(WIN_X (w) + WIN_WIDTH (w) / 2),
			 -(WIN_Y (w) - w->output.top), 0.0f);

	glPushMatrix ();
	glLoadMatrixf (wTransform.m);

	(*s->drawWindow) (w, &wTransform, &fragment, &infiniteRegion,
			  PAINT_WINDOW_TRANSFORMED_MASK |
			  PAINT_WINDOW_TRANSLUCENT_MASK);

	glPopMatrix ();
    }

    s->addWindowGeometry = oldAddWindowGeometry;
}

/*
 * groupPaintTabBar
 *
 */
static void
groupPaintTabBar (GroupSelection          *group,
		  const WindowPaintAttrib *wAttrib,
		  const CompTransform     *transform,
		  unsigned int            mask,
		  Region                  clipRegion)
{
    CompWindow      *topTab;
    CompScreen      *s = group->screen;
    GroupTabBar     *bar = group->tabBar;
    int             count;
    REGION          box;

    GROUP_SCREEN (s);

    if (HAS_TOP_WIN (group))
	topTab = TOP_TAB (group);
    else
	topTab = PREV_TOP_TAB (group);

#define PAINT_BG     0
#define PAINT_SEL    1
#define PAINT_THUMBS 2
#define PAINT_TEXT   3
#define PAINT_MAX    4

    box.rects = &box.extents;
    box.numRects = 1;

    for (count = 0; count < PAINT_MAX; count++)
    {
	int             alpha = OPAQUE;
	float           wScale = 1.0f, hScale = 1.0f;
	GroupCairoLayer *layer = NULL;

	if (bar->state == PaintFadeIn)
	    alpha -= alpha * bar->animationTime / (groupGetFadeTime (s) * 1000);
	else if (bar->state == PaintFadeOut)
	    alpha = alpha * bar->animationTime / (groupGetFadeTime (s) * 1000);

	switch (count) {
	case PAINT_BG:
	    {
		int newWidth;

		layer = bar->bgLayer;

		/* handle the repaint of the background */
		newWidth = bar->region->extents.x2 - bar->region->extents.x1;
		if (layer && (newWidth > layer->texWidth))
		    newWidth = layer->texWidth;

		wScale = (double) (bar->region->extents.x2 -
				   bar->region->extents.x1) / (double) newWidth;

		/* FIXME: maybe move this over to groupResizeTabBarRegion -
		   the only problem is that we would have 2 redraws if
		   there is an animation */
		if (newWidth != bar->oldWidth || bar->bgAnimation)
		    groupRenderTabBarBackground (group);

		bar->oldWidth = newWidth;
		box.extents = bar->region->extents;
	    }
	    break;

	case PAINT_SEL:
	    if (group->topTab != gs->draggedSlot)
	    {
		layer = bar->selectionLayer;
		box.extents = group->topTab->region->extents;
	    }
	    break;

	case PAINT_THUMBS:
	    {
		GLenum          oldTextureFilter;
		GroupTabBarSlot *slot;

		oldTextureFilter = s->display->textureFilter;

		if (groupGetMipmaps (s))
		    s->display->textureFilter = GL_LINEAR_MIPMAP_LINEAR;

		for (slot = bar->slots; slot; slot = slot->next)
		{
		    if (slot != gs->draggedSlot || !gs->dragged)
			groupPaintThumb (group, slot, transform,
					 wAttrib->opacity);
		}

		s->display->textureFilter = oldTextureFilter;
	    }
	    break;

	case PAINT_TEXT:
	    if (bar->textLayer && (bar->textLayer->state != PaintOff))
	    {
		layer = bar->textLayer;

		box.extents.x1 = bar->region->extents.x1 + 5;
		box.extents.x2 = bar->region->extents.x1 +
		                 bar->textLayer->texWidth + 5;
		box.extents.y1 = bar->region->extents.y2 -
		                 bar->textLayer->texHeight - 5;
		box.extents.y2 = bar->region->extents.y2 - 5;

		if (box.extents.x2 > bar->region->extents.x2)
		    box.extents.x2 = bar->region->extents.x2;

		/* recalculate the alpha again for text fade... */
		if (layer->state == PaintFadeIn)
		    alpha -= alpha * layer->animationTime /
			     (groupGetFadeTextTime(s) * 1000);
		else if (layer->state == PaintFadeOut)
		    alpha = alpha * layer->animationTime /
			    (groupGetFadeTextTime(s) * 1000);
	    }
	    break;
	}

	if (layer)
	{
	    CompMatrix matrix = layer->texture.matrix;

	    /* remove the old x1 and y1 so we have a relative value */
	    box.extents.x2 -= box.extents.x1;
	    box.extents.y2 -= box.extents.y1;
	    box.extents.x1 = (box.extents.x1 - topTab->attrib.x) / wScale +
		             topTab->attrib.x;
	    box.extents.y1 = (box.extents.y1 - topTab->attrib.y) / hScale +
		             topTab->attrib.y;

	    /* now add the new x1 and y1 so we have a absolute value again,
	       also we don't want to stretch the texture... */
	    if (box.extents.x2 * wScale < layer->texWidth)
		box.extents.x2 += box.extents.x1;
	    else
		box.extents.x2 = box.extents.x1 + layer->texWidth;

	    if (box.extents.y2 * hScale < layer->texHeight)
		box.extents.y2 += box.extents.y1;
	    else
		box.extents.y2 = box.extents.y1 + layer->texHeight;

	    matrix.x0 -= box.extents.x1 * matrix.xx;
	    matrix.y0 -= box.extents.y1 * matrix.yy;
	    topTab->vCount = topTab->indexCount = 0;

	    addWindowGeometry (topTab, &matrix, 1, &box, clipRegion);

	    if (topTab->vCount)
	    {
		FragmentAttrib fragment;
		CompTransform  wTransform = *transform;

		matrixTranslate (&wTransform,
				 WIN_X (topTab), WIN_Y (topTab), 0.0f);
		matrixScale (&wTransform, wScale, hScale, 1.0f);
		matrixTranslate (&wTransform,
				 wAttrib->xTranslate / wScale - WIN_X (topTab),
				 wAttrib->yTranslate / hScale - WIN_Y (topTab),
				 0.0f);

		glPushMatrix ();
		glLoadMatrixf (wTransform.m);

		alpha = alpha * ((float)wAttrib->opacity / OPAQUE);

		initFragmentAttrib (&fragment, wAttrib);
		fragment.opacity = alpha;

		(*s->drawWindowTexture) (topTab, &layer->texture,
					 &fragment, mask |
					 PAINT_WINDOW_BLEND_MASK |
					 PAINT_WINDOW_TRANSFORMED_MASK |
					 PAINT_WINDOW_TRANSLUCENT_MASK);

		glPopMatrix ();
	    }
	}
    }
}

/*
 * groupPaintSelectionOutline
 *
 */
static void
groupPaintSelectionOutline (CompScreen              *s,
			    const ScreenPaintAttrib *sa,
			    const CompTransform     *transform,
			    CompOutput              *output,
			    Bool                    transformed)
{
    int x1, x2, y1, y2;

    GROUP_SCREEN (s);

    x1 = MIN (gs->x1, gs->x2);
    y1 = MIN (gs->y1, gs->y2);
    x2 = MAX (gs->x1, gs->x2);
    y2 = MAX (gs->y1, gs->y2);

    if (gs->grabState == ScreenGrabSelect)
    {
	CompTransform sTransform = *transform;

	if (transformed)
	{
	    (*s->applyScreenTransform) (s, sa, output, &sTransform);
	    transformToScreenSpace (s, output, -sa->zTranslate, &sTransform);
	} else
	    transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

	glPushMatrix ();
	glLoadMatrixf (sTransform.m);

	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	glEnable (GL_BLEND);

	glColor4usv (groupGetFillColorOption (s)->value.c);
	glRecti (x1, y2, x2, y1);

	glColor4usv (groupGetLineColorOption (s)->value.c);
	glBegin (GL_LINE_LOOP);
	glVertex2i (x1, y1);
	glVertex2i (x2, y1);
	glVertex2i (x2, y2);
	glVertex2i (x1, y2);
	glEnd ();

	glColor4usv (defaultColor);
	glDisable (GL_BLEND);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	glPopMatrix ();
    }
}

/*
 * groupPreparePaintScreen
 *
 */
void
groupPreparePaintScreen (CompScreen *s,
			 int        msSinceLastPaint)
{
    GroupSelection *group, *next;

    GROUP_SCREEN (s);

    UNWRAP (gs, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP (gs, s, preparePaintScreen, groupPreparePaintScreen);

    group = gs->groups;
    while (group)
    {
	GroupTabBar *bar = group->tabBar;

	if (bar)
	{
	    groupApplyForces (s, bar, (gs->dragged) ? gs->draggedSlot : NULL);
	    groupApplySpeeds (s, group, msSinceLastPaint);

	    if ((bar->state != PaintOff) && HAS_TOP_WIN (group))
		groupHandleHoverDetection (group);

	    if (bar->state == PaintFadeIn || bar->state == PaintFadeOut)
		groupHandleTabBarFade (group, msSinceLastPaint);

	    if (bar->textLayer)
		groupHandleTextFade (group, msSinceLastPaint);

	    if (bar->bgAnimation)
		groupHandleTabBarAnimation (group, msSinceLastPaint);
	}

	if (group->changeState != NoTabChange)
	{
	    group->changeAnimationTime -= msSinceLastPaint;
	    if (group->changeAnimationTime <= 0)
		groupHandleAnimation (group);
	}

	/* groupDrawTabAnimation may delete the group, so better
	   save the pointer to the next chain element */
	next = group->next;

	if (group->tabbingState != NoTabbing)
	    groupDrawTabAnimation (group, msSinceLastPaint);

	group = next;
    }
}

/*
 * groupPaintOutput
 *
 */
Bool
groupPaintOutput (CompScreen              *s,
		  const ScreenPaintAttrib *sAttrib,
		  const CompTransform     *transform,
		  Region                  region,
		  CompOutput              *output,
		  unsigned int            mask)
{
    GroupSelection *group;
    Bool           status;

    GROUP_SCREEN (s);
    GROUP_DISPLAY (s->display);

    gs->painted = FALSE;
    gs->vpX = s->x;
    gs->vpY = s->y;

    if (gd->resizeInfo)
    {
	mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;
    }
    else
    {
	for (group = gs->groups; group; group = group->next)
	{
	    if (group->changeState != NoTabChange ||
		group->tabbingState != NoTabbing)
	    {
		mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;
	    }
	    else if (group->tabBar && (group->tabBar->state != PaintOff))
	    {
		mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;
	    }
	}
    }

    UNWRAP (gs, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
    WRAP (gs, s, paintOutput, groupPaintOutput);

    if (status && !gs->painted)
    {
	if ((gs->grabState == ScreenGrabTabDrag) && gs->draggedSlot)
	{
	    CompTransform wTransform = *transform;
	    PaintState    state;

	    GROUP_WINDOW (gs->draggedSlot->window);

	    transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &wTransform);

	    glPushMatrix ();
	    glLoadMatrixf (wTransform.m);

	    /* prevent tab bar drawing.. */
	    state = gw->group->tabBar->state;
	    gw->group->tabBar->state = PaintOff;
	    groupPaintThumb (NULL, gs->draggedSlot, &wTransform, OPAQUE);
	    gw->group->tabBar->state = state;

	    glPopMatrix ();
	}
	else  if (gs->grabState == ScreenGrabSelect)
	{
	    groupPaintSelectionOutline (s, sAttrib, transform, output, FALSE);
	}
    }

    return status;
}

/*
 * groupaintTransformedOutput
 *
 */
void
groupPaintTransformedOutput (CompScreen              *s,
			     const ScreenPaintAttrib *sa,
			     const CompTransform     *transform,
			     Region                  region,
			     CompOutput              *output,
			     unsigned int            mask)
{
    GROUP_SCREEN (s);

    UNWRAP (gs, s, paintTransformedOutput);
    (*s->paintTransformedOutput) (s, sa, transform, region, output, mask);
    WRAP (gs, s, paintTransformedOutput, groupPaintTransformedOutput);

    if ((gs->vpX == s->x) && (gs->vpY == s->y))
    {
	gs->painted = TRUE;

	if ((gs->grabState == ScreenGrabTabDrag) &&
	    gs->draggedSlot && gs->dragged)
	{
	    CompTransform wTransform = *transform;

	    (*s->applyScreenTransform) (s, sa, output, &wTransform);
	    transformToScreenSpace (s, output, -sa->zTranslate, &wTransform);
	    glPushMatrix ();
	    glLoadMatrixf (wTransform.m);

	    groupPaintThumb (NULL, gs->draggedSlot, &wTransform, OPAQUE);

	    glPopMatrix ();
	}
	else if (gs->grabState == ScreenGrabSelect)
	{
	    groupPaintSelectionOutline (s, sa, transform, output, TRUE);
	}
    }
}

/*
 * groupDonePaintScreen
 *
 */
void
groupDonePaintScreen (CompScreen *s)
{
    GroupSelection *group;

    GROUP_SCREEN (s);

    UNWRAP (gs, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (gs, s, donePaintScreen, groupDonePaintScreen);

    for (group = gs->groups; group; group = group->next)
    {
	if (group->tabbingState != NoTabbing)
	    damageScreen (s);
	else if (group->changeState != NoTabChange)
	    damageScreen (s);
	else if (group->tabBar)
	{
	    Bool needDamage = FALSE;

	    if ((group->tabBar->state == PaintFadeIn) ||
		(group->tabBar->state == PaintFadeOut))
	    {
		needDamage = TRUE;
	    }

	    if (group->tabBar->textLayer)
	    {
		if ((group->tabBar->textLayer->state == PaintFadeIn) ||
		    (group->tabBar->textLayer->state == PaintFadeOut))
		{
		    needDamage = TRUE;
		}
	    }

	    if (group->tabBar->bgAnimation)
		needDamage = TRUE;

	    if (gs->draggedSlot)
		needDamage = TRUE;

	    if (needDamage)
		groupDamageTabBarRegion (group);
	}
    }
}

void
groupComputeGlowQuads (CompWindow *w,
		       CompMatrix *matrix)
{
    BoxRec            *box;
    CompMatrix        *quadMatrix;
    int               glowSize, glowOffset;
    GroupGlowTypeEnum glowType;

    GROUP_WINDOW (w);

    if (groupGetGlow (w->screen) && matrix)
    {
	if (!gw->glowQuads)
	    gw->glowQuads = malloc (NUM_GLOWQUADS * sizeof (GlowQuad));
	if (!gw->glowQuads)
	    return;
    }
    else
    {
	if (gw->glowQuads)
	{
	    free (gw->glowQuads);
	    gw->glowQuads = NULL;
	}
	return;
    }

    GROUP_DISPLAY (w->screen->display);

    glowSize = groupGetGlowSize (w->screen);
    glowType = groupGetGlowType (w->screen);
    glowOffset = (glowSize * gd->glowTextureProperties[glowType].glowOffset /
		  gd->glowTextureProperties[glowType].textureSize) + 1;

    /* Top left corner */
    box = &gw->glowQuads[GLOWQUAD_TOPLEFT].box;
    gw->glowQuads[GLOWQUAD_TOPLEFT].matrix = *matrix;
    quadMatrix = &gw->glowQuads[GLOWQUAD_TOPLEFT].matrix;

    box->x1 = WIN_REAL_X (w) - glowSize + glowOffset;
    box->y1 = WIN_REAL_Y (w) - glowSize + glowOffset;
    box->x2 = WIN_REAL_X (w) + glowOffset;
    box->y2 = WIN_REAL_Y (w) + glowOffset;

    quadMatrix->xx = 1.0f / glowSize;
    quadMatrix->yy = -1.0f / glowSize;
    quadMatrix->x0 = -(box->x1 * quadMatrix->xx);
    quadMatrix->y0 = 1.0 -(box->y1 * quadMatrix->yy);

    box->x2 = MIN (WIN_REAL_X (w) + glowOffset,
		   WIN_REAL_X (w) + (WIN_REAL_WIDTH (w) / 2));
    box->y2 = MIN (WIN_REAL_Y (w) + glowOffset,
		   WIN_REAL_Y (w) + (WIN_REAL_HEIGHT (w) / 2));

    /* Top right corner */
    box = &gw->glowQuads[GLOWQUAD_TOPRIGHT].box;
    gw->glowQuads[GLOWQUAD_TOPRIGHT].matrix = *matrix;
    quadMatrix = &gw->glowQuads[GLOWQUAD_TOPRIGHT].matrix;

    box->x1 = WIN_REAL_X (w) + WIN_REAL_WIDTH (w) - glowOffset;
    box->y1 = WIN_REAL_Y (w) - glowSize + glowOffset;
    box->x2 = WIN_REAL_X (w) + WIN_REAL_WIDTH (w) + glowSize - glowOffset;
    box->y2 = WIN_REAL_Y (w) + glowOffset;

    quadMatrix->xx = -1.0f / glowSize;
    quadMatrix->yy = -1.0f / glowSize;
    quadMatrix->x0 = 1.0 - (box->x1 * quadMatrix->xx);
    quadMatrix->y0 = 1.0 - (box->y1 * quadMatrix->yy);

    box->x1 = MAX (WIN_REAL_X (w) + WIN_REAL_WIDTH (w) - glowOffset,
		   WIN_REAL_X (w) + (WIN_REAL_WIDTH (w) / 2));
    box->y2 = MIN (WIN_REAL_Y (w) + glowOffset,
		   WIN_REAL_Y (w) + (WIN_REAL_HEIGHT (w) / 2));

    /* Bottom left corner */
    box = &gw->glowQuads[GLOWQUAD_BOTTOMLEFT].box;
    gw->glowQuads[GLOWQUAD_BOTTOMLEFT].matrix = *matrix;
    quadMatrix = &gw->glowQuads[GLOWQUAD_BOTTOMLEFT].matrix;

    box->x1 = WIN_REAL_X (w) - glowSize + glowOffset;
    box->y1 = WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) - glowOffset;
    box->x2 = WIN_REAL_X (w) + glowOffset;
    box->y2 = WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) + glowSize - glowOffset;

    quadMatrix->xx = 1.0f / glowSize;
    quadMatrix->yy = 1.0f / glowSize;
    quadMatrix->x0 = -(box->x1 * quadMatrix->xx);
    quadMatrix->y0 = -(box->y1 * quadMatrix->yy);

    box->y1 = MAX (WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) - glowOffset,
		   WIN_REAL_Y (w) + (WIN_REAL_HEIGHT (w) / 2));
    box->x2 = MIN (WIN_REAL_X (w) + glowOffset,
		   WIN_REAL_X (w) + (WIN_REAL_WIDTH (w) / 2));

    /* Bottom right corner */
    box = &gw->glowQuads[GLOWQUAD_BOTTOMRIGHT].box;
    gw->glowQuads[GLOWQUAD_BOTTOMRIGHT].matrix = *matrix;
    quadMatrix = &gw->glowQuads[GLOWQUAD_BOTTOMRIGHT].matrix;

    box->x1 = WIN_REAL_X (w) + WIN_REAL_WIDTH (w) - glowOffset;
    box->y1 = WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) - glowOffset;
    box->x2 = WIN_REAL_X (w) + WIN_REAL_WIDTH (w) + glowSize - glowOffset;
    box->y2 = WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) + glowSize - glowOffset;

    quadMatrix->xx = -1.0f / glowSize;
    quadMatrix->yy = 1.0f / glowSize;
    quadMatrix->x0 = 1.0 - (box->x1 * quadMatrix->xx);
    quadMatrix->y0 = -(box->y1 * quadMatrix->yy);

    box->x1 = MAX (WIN_REAL_X (w) + WIN_REAL_WIDTH (w) - glowOffset,
		   WIN_REAL_X (w) + (WIN_REAL_WIDTH (w) / 2));
    box->y1 = MAX (WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) - glowOffset,
		   WIN_REAL_Y (w) + (WIN_REAL_HEIGHT (w) / 2));

    /* Top edge */
    box = &gw->glowQuads[GLOWQUAD_TOP].box;
    gw->glowQuads[GLOWQUAD_TOP].matrix = *matrix;
    quadMatrix = &gw->glowQuads[GLOWQUAD_TOP].matrix;

    box->x1 = WIN_REAL_X (w) + glowOffset;
    box->y1 = WIN_REAL_Y (w) - glowSize + glowOffset;
    box->x2 = WIN_REAL_X (w) + WIN_REAL_WIDTH (w) - glowOffset;
    box->y2 = WIN_REAL_Y (w) + glowOffset;

    quadMatrix->xx = 0.0f;
    quadMatrix->yy = -1.0f / glowSize;
    quadMatrix->x0 = 1.0;
    quadMatrix->y0 = 1.0 - (box->y1 * quadMatrix->yy);

    /* Bottom edge */
    box = &gw->glowQuads[GLOWQUAD_BOTTOM].box;
    gw->glowQuads[GLOWQUAD_BOTTOM].matrix = *matrix;
    quadMatrix = &gw->glowQuads[GLOWQUAD_BOTTOM].matrix;

    box->x1 = WIN_REAL_X (w) + glowOffset;
    box->y1 = WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) - glowOffset;
    box->x2 = WIN_REAL_X (w) + WIN_REAL_WIDTH (w) - glowOffset;
    box->y2 = WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) + glowSize - glowOffset;

    quadMatrix->xx = 0.0f;
    quadMatrix->yy = 1.0f / glowSize;
    quadMatrix->x0 = 1.0;
    quadMatrix->y0 = -(box->y1 * quadMatrix->yy);

    /* Left edge */
    box = &gw->glowQuads[GLOWQUAD_LEFT].box;
    gw->glowQuads[GLOWQUAD_LEFT].matrix = *matrix;
    quadMatrix = &gw->glowQuads[GLOWQUAD_LEFT].matrix;

    box->x1 = WIN_REAL_X (w) - glowSize + glowOffset;
    box->y1 = WIN_REAL_Y (w) + glowOffset;
    box->x2 = WIN_REAL_X (w) + glowOffset;
    box->y2 = WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) - glowOffset;

    quadMatrix->xx = 1.0f / glowSize;
    quadMatrix->yy = 0.0f;
    quadMatrix->x0 = -(box->x1 * quadMatrix->xx);
    quadMatrix->y0 = 0.0;

    /* Right edge */
    box = &gw->glowQuads[GLOWQUAD_RIGHT].box;
    gw->glowQuads[GLOWQUAD_RIGHT].matrix = *matrix;
    quadMatrix = &gw->glowQuads[GLOWQUAD_RIGHT].matrix;

    box->x1 = WIN_REAL_X (w) + WIN_REAL_WIDTH (w) - glowOffset;
    box->y1 = WIN_REAL_Y (w) + glowOffset;
    box->x2 = WIN_REAL_X (w) + WIN_REAL_WIDTH (w) + glowSize - glowOffset;
    box->y2 = WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) - glowOffset;

    quadMatrix->xx = -1.0f / glowSize;
    quadMatrix->yy = 0.0f;
    quadMatrix->x0 = 1.0 - (box->x1 * quadMatrix->xx);
    quadMatrix->y0 = 0.0;
}

/*
 * groupDrawWindow
 *
 */
Bool
groupDrawWindow (CompWindow           *w,
		 const CompTransform  *transform,
		 const FragmentAttrib *attrib,
		 Region               region,
		 unsigned int         mask)
{
    Bool       status;
    CompScreen *s = w->screen;

    GROUP_WINDOW (w);
    GROUP_SCREEN (s);

    if (gw->group && (gw->group->nWins > 1) && gw->glowQuads)
    {
	if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
	    region = &infiniteRegion;

	if (region->numRects)
	{
	    REGION box;
	    int    i;

	    box.rects = &box.extents;
	    box.numRects = 1;

	    w->vCount = w->indexCount = 0;

	    for (i = 0; i < NUM_GLOWQUADS; i++)
	    {
		box.extents = gw->glowQuads[i].box;

		if (box.extents.x1 < box.extents.x2 &&
		    box.extents.y1 < box.extents.y2)
		{
		    (*s->addWindowGeometry) (w, &gw->glowQuads[i].matrix,
					     1, &box, region);
		}
	    }

	    if (w->vCount)
	    {
		FragmentAttrib fAttrib = *attrib;
		GLushort       average;
		GLushort       color[3] = {gw->group->color[0],
		                           gw->group->color[1],
		                           gw->group->color[2]};

		/* Apply brightness to color. */
		color[0] *= (float)attrib->brightness / BRIGHT;
		color[1] *= (float)attrib->brightness / BRIGHT;
		color[2] *= (float)attrib->brightness / BRIGHT;

		/* Apply saturation to color. */
		average = (color[0] + color[1] + color[2]) / 3;
		color[0] = average + (color[0] - average) *
		           attrib->saturation / COLOR;
		color[1] = average + (color[1] - average) *
		           attrib->saturation / COLOR;
		color[2] = average + (color[2] - average) *
		           attrib->saturation / COLOR;

		fAttrib.opacity = OPAQUE;
		fAttrib.saturation = COLOR;
		fAttrib.brightness = BRIGHT;

		screenTexEnvMode (s, GL_MODULATE);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4us (color[0], color[1], color[2], attrib->opacity);

		/* we use PAINT_WINDOW_TRANSFORMED_MASK here to force
		   the usage of a good texture filter */
		(*s->drawWindowTexture) (w, &gs->glowTexture, &fAttrib,
					 mask | PAINT_WINDOW_BLEND_MASK |
					 PAINT_WINDOW_TRANSLUCENT_MASK |
					 PAINT_WINDOW_TRANSFORMED_MASK);

		glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		screenTexEnvMode (s, GL_REPLACE);
		glColor4usv (defaultColor);
	    }
	}
    }

    UNWRAP (gs, s, drawWindow);
    status = (*s->drawWindow) (w, transform, attrib, region, mask);
    WRAP (gs, s, drawWindow, groupDrawWindow);

    return status;
}

void
groupGetStretchRectangle (CompWindow *w,
			  BoxPtr     pBox,
			  float      *xScaleRet,
			  float      *yScaleRet)
{
    BoxRec box;
    int    width, height;
    float  xScale, yScale;

    GROUP_WINDOW (w);

    box.x1 = gw->resizeGeometry->x - w->input.left;
    box.y1 = gw->resizeGeometry->y - w->input.top;
    box.x2 = gw->resizeGeometry->x + gw->resizeGeometry->width +
	     w->serverBorderWidth * 2 + w->input.right;

    if (w->shaded)
    {
	box.y2 = gw->resizeGeometry->y + w->height + w->input.bottom;
    }
    else
    {
	box.y2 = gw->resizeGeometry->y + gw->resizeGeometry->height +
	         w->serverBorderWidth * 2 + w->input.bottom;
    }

    width  = w->width  + w->input.left + w->input.right;
    height = w->height + w->input.top  + w->input.bottom;

    xScale = (width)  ? (box.x2 - box.x1) / (float) width  : 1.0f;
    yScale = (height) ? (box.y2 - box.y1) / (float) height : 1.0f;

    pBox->x1 = box.x1 - (w->output.left - w->input.left) * xScale;
    pBox->y1 = box.y1 - (w->output.top - w->input.top) * yScale;
    pBox->x2 = box.x2 + w->output.right * xScale;
    pBox->y2 = box.y2 + w->output.bottom * yScale;

    if (xScaleRet)
	*xScaleRet = xScale;
    if (yScaleRet)
	*yScaleRet = yScale;
}

void
groupDamagePaintRectangle (CompScreen *s,
			   BoxPtr     pBox)
{
    REGION reg;

    reg.rects    = &reg.extents;
    reg.numRects = 1;

    reg.extents = *pBox;

    reg.extents.x1 -= 1;
    reg.extents.y1 -= 1;
    reg.extents.x2 += 1;
    reg.extents.y2 += 1;

    damageScreenRegion (s, &reg);
}

/*
 * groupPaintWindow
 *
 */
Bool
groupPaintWindow (CompWindow              *w,
		  const WindowPaintAttrib *attrib,
		  const CompTransform     *transform,
		  Region                  region,
		  unsigned int            mask)
{
    Bool       status;
    Bool       doRotate, doTabbing, showTabbar;
    CompScreen *s = w->screen;

    GROUP_SCREEN (s);
    GROUP_WINDOW (w);

    if (gw->group)
    {
	GroupSelection *group = gw->group;

	doRotate = (group->changeState != NoTabChange) &&
	           HAS_TOP_WIN (group) && HAS_PREV_TOP_WIN (group) &&
	           (IS_TOP_TAB (w, group) || IS_PREV_TOP_TAB (w, group));

	doTabbing = (gw->animateState & (IS_ANIMATED | FINISHED_ANIMATION)) &&
	            !(IS_TOP_TAB (w, group) &&
		      (group->tabbingState == Tabbing));

	showTabbar = group->tabBar && (group->tabBar->state != PaintOff) &&
	             (((IS_TOP_TAB (w, group)) &&
		       ((group->changeState == NoTabChange) ||
			(group->changeState == TabChangeNewIn))) ||
		      (IS_PREV_TOP_TAB (w, group) &&
		       (group->changeState == TabChangeOldOut)));
    }
    else
    {
	doRotate   = FALSE;
	doTabbing  = FALSE;
	showTabbar = FALSE;
    }

    if (gw->windowHideInfo)
	mask |= PAINT_WINDOW_NO_CORE_INSTANCE_MASK;

    if (gw->inSelection || gw->resizeGeometry || doRotate ||
	doTabbing || showTabbar)
    {
	WindowPaintAttrib wAttrib = *attrib;
	CompTransform     wTransform = *transform;
	float             animProgress = 0.0f;
	int               drawnPosX = 0, drawnPosY = 0;

	if (gw->inSelection)
	{
	    wAttrib.opacity    = OPAQUE * groupGetSelectOpacity (s) / 100;
	    wAttrib.saturation = COLOR * groupGetSelectSaturation (s) / 100;
	    wAttrib.brightness = BRIGHT * groupGetSelectBrightness (s) / 100;
	}

	if (doTabbing)
	{
	    /* fade the window out */
	    float progress;
	    int   distanceX, distanceY;
	    float origDistance, distance;

	    if (gw->animateState & FINISHED_ANIMATION)
	    {
		drawnPosX = gw->destination.x;
		drawnPosY = gw->destination.y;
	    }
	    else
	    {
		drawnPosX = gw->orgPos.x + gw->tx;
		drawnPosY = gw->orgPos.y + gw->ty;
	    }

	    distanceX = drawnPosX - gw->destination.x;
	    distanceY = drawnPosY - gw->destination.y;
	    distance = sqrt (pow (distanceX, 2) + pow (distanceY, 2));

	    distanceX = (gw->orgPos.x - gw->destination.x);
	    distanceY = (gw->orgPos.y - gw->destination.y);
	    origDistance = sqrt (pow (distanceX, 2) + pow (distanceY, 2));

	    if (!distanceX && !distanceY)
		progress = 1.0f;
	    else
		progress = 1.0f - (distance / origDistance);

	    animProgress = progress;

	    progress = MAX (progress, 0.0f);
	    if (gw->group->tabbingState == Tabbing)
		progress = 1.0f - progress;

	    wAttrib.opacity = (float)wAttrib.opacity * progress;
	}

	if (doRotate)
	{
	    float timeLeft = gw->group->changeAnimationTime;
	    int   animTime = groupGetChangeAnimationTime (s) * 500;

	    if (gw->group->changeState == TabChangeOldOut)
		timeLeft += animTime;

	    /* 0 at the beginning, 1 at the end */
	    animProgress = 1 - (timeLeft / (2 * animTime));
	}

	if (gw->resizeGeometry)
	{
	    int    xOrigin, yOrigin;
	    float  xScale, yScale;
	    BoxRec box;

	    groupGetStretchRectangle (w, &box, &xScale, &yScale);

	    xOrigin = w->attrib.x - w->input.left;
	    yOrigin = w->attrib.y - w->input.top;

	    matrixTranslate (&wTransform, xOrigin, yOrigin, 0.0f);
	    matrixScale (&wTransform, xScale, yScale, 1.0f);
	    matrixTranslate (&wTransform,
			     (gw->resizeGeometry->x - w->attrib.x) /
			     xScale - xOrigin,
			     (gw->resizeGeometry->y - w->attrib.y) /
			     yScale - yOrigin,
			     0.0f);

	    mask |= PAINT_WINDOW_TRANSFORMED_MASK;
	}
	else if (doRotate || doTabbing)
	{
	    float      animWidth, animHeight;
	    float      animScaleX, animScaleY;
	    CompWindow *morphBase, *morphTarget;

	    if (doTabbing)
	    {
		if (gw->group->tabbingState == Tabbing)
		{
		    morphBase   = w;
		    morphTarget = TOP_TAB (gw->group);
		}
		else
		{
		    morphTarget = w;
		    if (HAS_TOP_WIN (gw->group))
			morphBase = TOP_TAB (gw->group);
		    else
			morphBase = gw->group->lastTopTab;
		}
	    }
	    else
	    {
		morphBase   = PREV_TOP_TAB (gw->group);
		morphTarget = TOP_TAB (gw->group);
	    }

	    animWidth = (1 - animProgress) * WIN_REAL_WIDTH (morphBase) +
		        animProgress * WIN_REAL_WIDTH (morphTarget);
	    animHeight = (1 - animProgress) * WIN_REAL_HEIGHT (morphBase) +
		         animProgress * WIN_REAL_HEIGHT (morphTarget);

	    animWidth = MAX (1.0f, animWidth);
	    animHeight = MAX (1.0f, animHeight);
	    animScaleX = animWidth / WIN_REAL_WIDTH (w);
	    animScaleY = animHeight / WIN_REAL_HEIGHT (w);

	    if (doRotate)
		matrixScale (&wTransform, 1.0f, 1.0f, 1.0f / s->width);

	    matrixTranslate (&wTransform,
			     WIN_REAL_X (w) + WIN_REAL_WIDTH (w) / 2.0f,
			     WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) / 2.0f,
			     0.0f);

	    if (doRotate)
	    {
		float rotateAngle = animProgress * 180.0f;
		if (IS_TOP_TAB (w, gw->group))
		    rotateAngle += 180.0f;

		if (gw->group->changeAnimationDirection < 0)
		    rotateAngle *= -1.0f;

		matrixRotate (&wTransform, rotateAngle, 0.0f, 1.0f, 0.0f);
	    }

	    if (doTabbing)
		matrixTranslate (&wTransform,
				 drawnPosX - WIN_X (w),
				 drawnPosY - WIN_Y (w), 0.0f);

	    matrixScale (&wTransform, animScaleX, animScaleY, 1.0f);

	    matrixTranslate (&wTransform,
			     -(WIN_REAL_X (w) + WIN_REAL_WIDTH (w) / 2.0f),
			     -(WIN_REAL_Y (w) + WIN_REAL_HEIGHT (w) / 2.0f),
			     0.0f);

	    mask |= PAINT_WINDOW_TRANSFORMED_MASK;
	}

	UNWRAP (gs, s, paintWindow);
	status = (*s->paintWindow) (w, &wAttrib, &wTransform, region, mask);

	if (showTabbar)
	    groupPaintTabBar (gw->group, &wAttrib, &wTransform, mask, region);

	WRAP (gs, s, paintWindow, groupPaintWindow);
    }
    else
    {
	UNWRAP (gs, s, paintWindow);
	status = (*s->paintWindow) (w, attrib, transform, region, mask);
	WRAP (gs, s, paintWindow, groupPaintWindow);
    }

    return status;
}
