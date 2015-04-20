/**
 *
 * Compiz group plugin
 *
 * cairo.c
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
 * groupRebuildCairoLayer
 *
 */
GroupCairoLayer*
groupRebuildCairoLayer (CompScreen      *s,
			GroupCairoLayer *layer,
			int             width,
			int             height)
{
    int        timeBuf = layer->animationTime;
    PaintState stateBuf = layer->state;

    groupDestroyCairoLayer (s, layer);
    layer = groupCreateCairoLayer (s, width, height);
    if (!layer)
	return NULL;

    layer->animationTime = timeBuf;
    layer->state = stateBuf;

    return layer;
}

/*
 * groupClearCairoLayer
 *
 */
void
groupClearCairoLayer (GroupCairoLayer *layer)
{
    cairo_t *cr = layer->cairo;

    cairo_save (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint (cr);
    cairo_restore (cr);
}

/*
 * groupDestroyCairoLayer
 *
 */
void
groupDestroyCairoLayer (CompScreen      *s,
			GroupCairoLayer *layer)
{
    if (!layer)
	return;

    if (layer->cairo)
	cairo_destroy (layer->cairo);

    if (layer->surface)
	cairo_surface_destroy (layer->surface);

    finiTexture (s, &layer->texture);

    if (layer->pixmap)
	XFreePixmap (s->display->display, layer->pixmap);

    if (layer->buffer)
	free (layer->buffer);

    free (layer);
}

/*
 * groupCreateCairoLayer
 *
 */
GroupCairoLayer*
groupCreateCairoLayer (CompScreen *s,
		       int        width,
		       int        height)
{
    GroupCairoLayer *layer;


    layer = malloc (sizeof (GroupCairoLayer));
    if (!layer)
        return NULL;

    layer->surface = NULL;
    layer->cairo   = NULL;
    layer->buffer  = NULL;
    layer->pixmap  = None;

    layer->animationTime = 0;
    layer->state         = PaintOff;

    layer->texWidth  = width;
    layer->texHeight = height;

    initTexture (s, &layer->texture);

    layer->buffer = calloc (4 * width * height, sizeof (unsigned char));
    if (!layer->buffer)
    {
	compLogMessage ("group", CompLogLevelError,
			"Failed to allocate cairo layer buffer.");
	groupDestroyCairoLayer (s, layer);
	return NULL;
    }

    layer->surface = cairo_image_surface_create_for_data (layer->buffer,
							  CAIRO_FORMAT_ARGB32,
							  width, height,
							  4 * width);
    if (cairo_surface_status (layer->surface) != CAIRO_STATUS_SUCCESS)
    {
	compLogMessage ("group", CompLogLevelError,
			"Failed to create cairo layer surface.");
	groupDestroyCairoLayer (s, layer);
	return NULL;
    }

    layer->cairo = cairo_create (layer->surface);
    if (cairo_status (layer->cairo) != CAIRO_STATUS_SUCCESS)
    {
	compLogMessage ("group", CompLogLevelError,
			"Failed to create cairo layer context.");
	groupDestroyCairoLayer (s, layer);
	return NULL;
    }

    groupClearCairoLayer (layer);

    return layer;
}

/*
 * groupRenderTopTabHighlight
 *
 */
void
groupRenderTopTabHighlight (GroupSelection *group)
{
    GroupTabBar     *bar = group->tabBar;
    GroupCairoLayer *layer;
    cairo_t         *cr;
    int             width, height;

    if (!bar || !HAS_TOP_WIN (group) ||
	!bar->selectionLayer || !bar->selectionLayer->cairo)
    {
	return;
    }

    width = group->topTab->region->extents.x2 -
	    group->topTab->region->extents.x1;
    height = group->topTab->region->extents.y2 -
	     group->topTab->region->extents.y1;

    bar->selectionLayer = groupRebuildCairoLayer (group->screen,
						  bar->selectionLayer,
						  width, height);
    if (!bar->selectionLayer)
	return;

    layer = bar->selectionLayer;
    cr = bar->selectionLayer->cairo;

    /* fill */
    cairo_set_line_width (cr, 2);
    cairo_set_source_rgba (cr,
			   (group->color[0] / 65535.0f),
			   (group->color[1] / 65535.0f),
			   (group->color[2] / 65535.0f),
			   (group->color[3] / (65535.0f * 2)));

    cairo_move_to (cr, 0, 0);
    cairo_rectangle (cr, 0, 0, width, height);

    cairo_fill_preserve (cr);

    /* outline */
    cairo_set_source_rgba (cr,
			   (group->color[0] / 65535.0f),
			   (group->color[1] / 65535.0f),
			   (group->color[2] / 65535.0f),
			   (group->color[3] / 65535.0f));
    cairo_stroke (cr);

    imageBufferToTexture (group->screen,
			  &layer->texture, (char*) layer->buffer,
			  layer->texWidth, layer->texHeight);
}

/*
 * groupRenderTabBarBackground
 *
 */
void
groupRenderTabBarBackground(GroupSelection *group)
{
    GroupCairoLayer *layer;
    cairo_t         *cr;
    int             width, height, radius;
    int             borderWidth;
    float           r, g, b, a;
    double          x0, y0, x1, y1;
    CompScreen      *s = group->screen;
    GroupTabBar     *bar = group->tabBar;

    if (!bar || !HAS_TOP_WIN (group) || !bar->bgLayer || !bar->bgLayer->cairo)
	return;

    width = bar->region->extents.x2 - bar->region->extents.x1;
    height = bar->region->extents.y2 - bar->region->extents.y1;
    radius = groupGetBorderRadius (s);

    if (width > bar->bgLayer->texWidth)
	width = bar->bgLayer->texWidth;

    if (radius > width / 2)
	radius = width / 2;

    layer = bar->bgLayer;
    cr = layer->cairo;

    groupClearCairoLayer (layer);

    borderWidth = groupGetBorderWidth (s);
    cairo_set_line_width (cr, borderWidth);

    cairo_save (cr);

    x0 = borderWidth / 2.0f;
    y0 = borderWidth / 2.0f;
    x1 = width  - borderWidth / 2.0f;
    y1 = height - borderWidth / 2.0f;
    cairo_move_to (cr, x0 + radius, y0);
    cairo_arc (cr, x1 - radius, y0 + radius, radius, M_PI * 1.5, M_PI * 2.0);
    cairo_arc (cr, x1 - radius, y1 - radius, radius, 0.0, M_PI * 0.5);
    cairo_arc (cr, x0 + radius, y1 - radius, radius, M_PI * 0.5, M_PI);
    cairo_arc (cr, x0 + radius, y0 + radius, radius, M_PI, M_PI * 1.5);

    cairo_close_path  (cr);

    switch (groupGetTabStyle (s)) {
    case TabStyleSimple:
	{
	    /* base color */
	    r = groupGetTabBaseColorRed (s) / 65535.0f;
	    g = groupGetTabBaseColorGreen (s) / 65535.0f;
	    b = groupGetTabBaseColorBlue (s) / 65535.0f;
	    a = groupGetTabBaseColorAlpha (s) / 65535.0f;
	    cairo_set_source_rgba (cr, r, g, b, a);

    	    cairo_fill_preserve (cr);
	    break;
	}

    case TabStyleGradient:
	{
	    /* fill */
	    cairo_pattern_t *pattern;
	    pattern = cairo_pattern_create_linear (0, 0, width, height);

	    /* highlight color */
	    r = groupGetTabHighlightColorRed (s) / 65535.0f;
	    g = groupGetTabHighlightColorGreen (s) / 65535.0f;
	    b = groupGetTabHighlightColorBlue (s) / 65535.0f;
	    a = groupGetTabHighlightColorAlpha (s) / 65535.0f;
	    cairo_pattern_add_color_stop_rgba (pattern, 0.0f, r, g, b, a);

	    /* base color */
	    r = groupGetTabBaseColorRed (s) / 65535.0f;
	    g = groupGetTabBaseColorGreen (s) / 65535.0f;
	    b = groupGetTabBaseColorBlue (s) / 65535.0f;
	    a = groupGetTabBaseColorAlpha (s) / 65535.0f;
	    cairo_pattern_add_color_stop_rgba (pattern, 1.0f, r, g, b, a);

	    cairo_set_source (cr, pattern);
	    cairo_fill_preserve (cr);
	    cairo_pattern_destroy (pattern);
	    break;
	}

    case TabStyleGlass:
	{
	    cairo_pattern_t *pattern;

	    cairo_save (cr);

	    /* clip width rounded rectangle */
	    cairo_clip (cr);

	    /* ===== HIGHLIGHT ===== */

	    /* make draw the shape for the highlight and
	       create a pattern for it */
	    cairo_rectangle (cr, 0, 0, width, height / 2);
	    pattern = cairo_pattern_create_linear (0, 0, 0, height);

	    /* highlight color */
	    r = groupGetTabHighlightColorRed (s) / 65535.0f;
	    g = groupGetTabHighlightColorGreen (s) / 65535.0f;
	    b = groupGetTabHighlightColorBlue (s) / 65535.0f;
	    a = groupGetTabHighlightColorAlpha (s) / 65535.0f;
	    cairo_pattern_add_color_stop_rgba (pattern, 0.0f, r, g, b, a);

	    /* base color */
	    r = groupGetTabBaseColorRed (s) / 65535.0f;
	    g = groupGetTabBaseColorGreen (s) / 65535.0f;
	    b = groupGetTabBaseColorBlue (s) / 65535.0f;
	    a = groupGetTabBaseColorAlpha (s) / 65535.0f;
	    cairo_pattern_add_color_stop_rgba (pattern, 0.6f, r, g, b, a);

	    cairo_set_source (cr, pattern);
	    cairo_fill (cr);
	    cairo_pattern_destroy (pattern);

	    /* ==== SHADOW ===== */

	    /* make draw the shape for the show and create a pattern for it */
	    cairo_rectangle (cr, 0, height / 2, width, height);
	    pattern = cairo_pattern_create_linear (0, 0, 0, height);

	    /* we don't want to use a full highlight here
	       so we mix the colors */
	    r = (groupGetTabHighlightColorRed (s) +
		 groupGetTabBaseColorRed (s)) / (2 * 65535.0f);
	    g = (groupGetTabHighlightColorGreen (s) +
		 groupGetTabBaseColorGreen (s)) / (2 * 65535.0f);
	    b = (groupGetTabHighlightColorBlue (s) +
		 groupGetTabBaseColorBlue (s)) / (2 * 65535.0f);
	    a = (groupGetTabHighlightColorAlpha (s) +
		 groupGetTabBaseColorAlpha (s)) / (2 * 65535.0f);
	    cairo_pattern_add_color_stop_rgba (pattern, 1.0f, r, g, b, a);

	    /* base color */
	    r = groupGetTabBaseColorRed (s) / 65535.0f;
	    g = groupGetTabBaseColorGreen (s) / 65535.0f;
	    b = groupGetTabBaseColorBlue (s) / 65535.0f;
	    a = groupGetTabBaseColorAlpha (s) / 65535.0f;
	    cairo_pattern_add_color_stop_rgba (pattern, 0.5f, r, g, b, a);

	    cairo_set_source (cr, pattern);
	    cairo_fill (cr);
	    cairo_pattern_destroy (pattern);

	    cairo_restore (cr);

	    /* draw shape again for the outline */
	    cairo_move_to (cr, x0 + radius, y0);
	    cairo_arc (cr, x1 - radius, y0 + radius,
		       radius, M_PI * 1.5, M_PI * 2.0);
	    cairo_arc (cr, x1 - radius, y1 - radius,
		       radius, 0.0, M_PI * 0.5);
	    cairo_arc (cr, x0 + radius, y1 - radius,
		       radius, M_PI * 0.5, M_PI);
	    cairo_arc (cr, x0 + radius, y0 + radius,
		       radius, M_PI, M_PI * 1.5);

	    break;
	}

    case TabStyleMetal:
	{
	    /* fill */
	    cairo_pattern_t *pattern;
	    pattern = cairo_pattern_create_linear (0, 0, 0, height);

	    /* base color #1 */
	    r = groupGetTabBaseColorRed (s) / 65535.0f;
	    g = groupGetTabBaseColorGreen (s) / 65535.0f;
	    b = groupGetTabBaseColorBlue (s) / 65535.0f;
	    a = groupGetTabBaseColorAlpha (s) / 65535.0f;
	    cairo_pattern_add_color_stop_rgba (pattern, 0.0f, r, g, b, a);

	    /* highlight color */
	    r = groupGetTabHighlightColorRed (s) / 65535.0f;
	    g = groupGetTabHighlightColorGreen (s) / 65535.0f;
	    b = groupGetTabHighlightColorBlue (s) / 65535.0f;
	    a = groupGetTabHighlightColorAlpha (s) / 65535.0f;
	    cairo_pattern_add_color_stop_rgba (pattern, 0.55f, r, g, b, a);

	    /* base color #2 */
	    r = groupGetTabBaseColorRed (s) / 65535.0f;
	    g = groupGetTabBaseColorGreen (s) / 65535.0f;
	    b = groupGetTabBaseColorBlue (s) / 65535.0f;
	    a = groupGetTabBaseColorAlpha (s) / 65535.0f;
	    cairo_pattern_add_color_stop_rgba (pattern, 1.0f, r, g, b, a);

	    cairo_set_source (cr, pattern);
	    cairo_fill_preserve (cr);
	    cairo_pattern_destroy (pattern);
	    break;
	}

    case TabStyleMurrina:
	{
	    double          ratio, transX;
	    cairo_pattern_t *pattern;

	    cairo_save (cr);

	    /* clip width rounded rectangle */
	    cairo_clip_preserve (cr);

	    /* ==== TOP ==== */

	    x0 = borderWidth / 2.0;
	    y0 = borderWidth / 2.0;
	    x1 = width  - borderWidth / 2.0;
	    y1 = height - borderWidth / 2.0;
	    radius = (y1 - y0) / 2;

	    /* setup pattern */
	    pattern = cairo_pattern_create_linear (0, 0, 0, height);

	    /* we don't want to use a full highlight here
	       so we mix the colors */
	    r = (groupGetTabHighlightColorRed (s) +
		 groupGetTabBaseColorRed (s)) / (2 * 65535.0f);
	    g = (groupGetTabHighlightColorGreen (s) +
		 groupGetTabBaseColorGreen (s)) / (2 * 65535.0f);
	    b = (groupGetTabHighlightColorBlue (s) +
		 groupGetTabBaseColorBlue (s)) / (2 * 65535.0f);
	    a = (groupGetTabHighlightColorAlpha (s) +
		 groupGetTabBaseColorAlpha (s)) / (2 * 65535.0f);
	    cairo_pattern_add_color_stop_rgba (pattern, 0.0f, r, g, b, a);

	    /* highlight color */
	    r = groupGetTabHighlightColorRed (s) / 65535.0f;
	    g = groupGetTabHighlightColorGreen (s) / 65535.0f;
	    b = groupGetTabHighlightColorBlue (s) / 65535.0f;
	    a = groupGetTabHighlightColorAlpha (s) / 65535.0f;
	    cairo_pattern_add_color_stop_rgba (pattern, 1.0f, r, g, b, a);

	    cairo_set_source (cr, pattern);

	    cairo_fill (cr);
	    cairo_pattern_destroy (pattern);

	    /* ==== BOTTOM ===== */

	    x0 = borderWidth / 2.0;
	    y0 = borderWidth / 2.0;
	    x1 = width  - borderWidth / 2.0;
	    y1 = height - borderWidth / 2.0;
	    radius = (y1 - y0) / 2;

	    ratio = (double)width / (double)height;
	    transX = width - (width * ratio);

	    cairo_move_to (cr, x1, y1);
	    cairo_line_to (cr, x1, y0);
	    if (width < height)
	    {
		cairo_translate (cr, transX, 0);
		cairo_scale (cr, ratio, 1.0);
	    }
	    cairo_arc (cr, x1 - radius, y0, radius, 0.0, M_PI * 0.5);
	    if (width < height)
	    {
		cairo_scale (cr, 1.0 / ratio, 1.0);
		cairo_translate (cr, -transX, 0);
		cairo_scale (cr, ratio, 1.0);
	    }
	    cairo_arc_negative (cr, x0 + radius, y1,
				radius, M_PI * 1.5, M_PI);
	    cairo_close_path (cr);

	    /* setup pattern */
	    pattern = cairo_pattern_create_linear (0, 0, 0, height);

	    /* base color */
	    r = groupGetTabBaseColorRed (s) / 65535.0f;
	    g = groupGetTabBaseColorGreen (s) / 65535.0f;
	    b = groupGetTabBaseColorBlue (s) / 65535.0f;
	    a = groupGetTabBaseColorAlpha (s) / 65535.0f;
	    cairo_pattern_add_color_stop_rgba (pattern, 0.0f, r, g, b, a);

	    /* we don't want to use a full highlight here
	       so we mix the colors */
	    r = (groupGetTabHighlightColorRed (s) +
		 groupGetTabBaseColorRed (s)) / (2 * 65535.0f);
	    g = (groupGetTabHighlightColorGreen (s) +
		 groupGetTabBaseColorGreen (s)) / (2 * 65535.0f);
	    b = (groupGetTabHighlightColorBlue (s) +
		 groupGetTabBaseColorBlue (s)) / (2 * 65535.0f);
	    a = (groupGetTabHighlightColorAlpha (s) +
		 groupGetTabBaseColorAlpha (s)) / (2 * 65535.0f);
	    cairo_pattern_add_color_stop_rgba (pattern, 1.0f, r, g, b, a);

	    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
	    cairo_set_source (cr, pattern);
	    cairo_fill (cr);
	    cairo_pattern_destroy (pattern);
	    cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

	    cairo_restore (cr);

	    /* draw shape again for the outline */
	    x0 = borderWidth / 2.0;
	    y0 = borderWidth / 2.0;
	    x1 = width  - borderWidth / 2.0;
	    y1 = height - borderWidth / 2.0;
	    radius = groupGetBorderRadius (s);

	    cairo_move_to (cr, x0 + radius, y0);
	    cairo_arc (cr, x1 - radius, y0 + radius,
		       radius, M_PI * 1.5, M_PI * 2.0);
	    cairo_arc (cr, x1 - radius, y1 - radius,
		       radius, 0.0, M_PI * 0.5);
	    cairo_arc (cr, x0 + radius, y1 - radius,
		       radius, M_PI * 0.5, M_PI);
	    cairo_arc (cr, x0 + radius, y0 + radius,
		       radius, M_PI, M_PI * 1.5);

    	    break;
	}

    default:
	break;
    }

    /* outline */
    r = groupGetTabBorderColorRed (s) / 65535.0f;
    g = groupGetTabBorderColorGreen (s) / 65535.0f;
    b = groupGetTabBorderColorBlue (s) / 65535.0f;
    a = groupGetTabBorderColorAlpha (s) / 65535.0f;
    cairo_set_source_rgba (cr, r, g, b, a);

    if (bar->bgAnimation != AnimationNone)
	cairo_stroke_preserve (cr);
    else
	cairo_stroke (cr);

    switch (bar->bgAnimation) {
    case AnimationPulse:
	{
	    double animationProgress;
	    double alpha;

	    animationProgress = bar->bgAnimationTime /
		                (groupGetPulseTime (s) * 1000.0);
	    alpha = sin ((2 * PI * animationProgress) - 1.55)*0.5 + 0.5;
	    if (alpha <= 0)
		break;

	    cairo_save (cr);
	    cairo_clip (cr);
	    cairo_set_operator (cr, CAIRO_OPERATOR_XOR);
	    cairo_rectangle (cr, 0.0, 0.0, width, height);
	    cairo_set_source_rgba (cr, 1.0, 1.0, 1.0, alpha);
	    cairo_fill (cr);
	    cairo_restore (cr);
	    break;
	}

    case AnimationReflex:
	{
	    double          animationProgress;
	    double          reflexWidth;
	    double          posX, alpha;
	    cairo_pattern_t *pattern;

	    animationProgress = bar->bgAnimationTime /
		                (groupGetReflexTime (s) * 1000.0);
	    reflexWidth = (bar->nSlots / 2.0) * 30;
	    posX = (width + reflexWidth * 2.0) * animationProgress;
	    alpha = sin (PI * animationProgress) * 0.55;
	    if (alpha <= 0)
		break;

	    cairo_save (cr);
	    cairo_clip (cr);
	    pattern = cairo_pattern_create_linear (posX - reflexWidth,
						   0.0, posX, height);
	    cairo_pattern_add_color_stop_rgba (pattern,
					       0.0f, 1.0, 1.0, 1.0, 0.0);
	    cairo_pattern_add_color_stop_rgba (pattern,
					       0.5f, 1.0, 1.0, 1.0, alpha);
	    cairo_pattern_add_color_stop_rgba (pattern,
					       1.0f, 1.0, 1.0, 1.0, 0.0);
	    cairo_rectangle (cr, 0.0, 0.0, width, height);
	    cairo_set_source (cr, pattern);
	    cairo_fill (cr);
	    cairo_restore (cr);
	    cairo_pattern_destroy (pattern);
	    break;
	}

    case AnimationNone:
    default:
	break;
    }

    /* draw inner outline */
    cairo_move_to (cr, x0 + radius + 1.0, y0 + 1.0);
    cairo_arc (cr, x1 - radius - 1.0, y0 + radius + 1.0,
		radius, M_PI * 1.5, M_PI * 2.0);
    cairo_arc (cr, x1 - radius - 1.0, y1 - radius - 1.0,
		radius, 0.0, M_PI * 0.5);
    cairo_arc (cr, x0 + radius + 1.0, y1 - radius - 1.0,
		radius, M_PI * 0.5, M_PI);
    cairo_arc (cr, x0 + radius + 1.0, y0 + radius + 1.0,
		radius, M_PI, M_PI * 1.5);

    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.3);
    cairo_stroke(cr);

    cairo_restore (cr);
    imageBufferToTexture (s, &layer->texture, (char*) layer->buffer,
			  layer->texWidth, layer->texHeight);
}

/*
 * groupRenderWindowTitle
 *
 */
void
groupRenderWindowTitle (GroupSelection *group)
{
    GroupCairoLayer *layer;
    int             width, height;
    Pixmap          pixmap = None;
    CompScreen      *s = group->screen;
    CompDisplay     *d = s->display;
    GroupTabBar     *bar = group->tabBar;

    GROUP_DISPLAY (d);

    if (!bar || !HAS_TOP_WIN (group) || !bar->textLayer)
	return;

    width = bar->region->extents.x2 - bar->region->extents.x1;
    height = bar->region->extents.y2 - bar->region->extents.y1;

    bar->textLayer = groupRebuildCairoLayer (s, bar->textLayer, width, height);
    layer = bar->textLayer;
    if (!layer)
	return;

    if (bar->textSlot && bar->textSlot->window && gd->textFunc)
    {
	CompTextData    *data;
	CompTextAttrib  textAttrib;

	textAttrib.family = "Sans";
	textAttrib.size   = groupGetTabbarFontSize (s);

	textAttrib.flags = CompTextFlagStyleBold | CompTextFlagEllipsized |
	                   CompTextFlagNoAutoBinding;

	textAttrib.color[0] = groupGetTabbarFontColorRed (s);
	textAttrib.color[1] = groupGetTabbarFontColorGreen (s);
	textAttrib.color[2] = groupGetTabbarFontColorBlue (s);
	textAttrib.color[3] = groupGetTabbarFontColorAlpha (s);

	textAttrib.maxWidth = width;
	textAttrib.maxHeight = height;

	data = (gd->textFunc->renderWindowTitle) (s, bar->textSlot->window->id,
						  FALSE, &textAttrib);
	if (data)
	{
	    pixmap = data->pixmap;
	    width = data->width;
	    height = data->height;
	    free (data);
	}
    }

    if (!pixmap)
    {
	/* getting the pixmap failed, so create an empty one */
	pixmap = XCreatePixmap (d->display, s->root, width, height, 32);

	if (pixmap)
	{
	    XGCValues gcv;
	    GC        gc;

	    gcv.foreground = 0x00000000;
	    gcv.plane_mask = 0xffffffff;

	    gc = XCreateGC (d->display, pixmap, GCForeground, &gcv);
	    XFillRectangle (d->display, pixmap, gc, 0, 0, width, height);
	    XFreeGC (d->display, gc);
	}
    }

    layer->texWidth = width;
    layer->texHeight = height;

    if (pixmap)
    {
	layer->pixmap = pixmap;
	bindPixmapToTexture (s, &layer->texture, layer->pixmap,
			     layer->texWidth, layer->texHeight, 32);
    }
}

