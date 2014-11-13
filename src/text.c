/*
 * Compiz text plugin
 * Description: Adds text to pixmap support to Compiz.
 *
 * text.c
 *
 * Copyright: (C) 2006-2007 Patrick Niklaus, Danny Baumann, Dennis Kasprzyk
 * Authors: Patrick Niklaus <marex@opencompsiting.org>
 *	    Danny Baumann   <maniac@opencompositing.org>
 *	    Dennis Kasprzyk <onestone@opencompositing.org>
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
 *
 */

#define _GNU_SOURCE

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <X11/Xatom.h>

#include <cairo-xlib-xrender.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

#include <fusilli-core.h>
#include <fusilli-text.h>

#define PI 3.14159265359f

typedef struct _TextSurfaceData {
	int                  width;
	int                  height;

	cairo_t              *cr;
	cairo_surface_t      *surface;
	PangoLayout          *layout;
	Pixmap               pixmap;
	XRenderPictFormat    *format;
	PangoFontDescription *font;
	Screen               *screen;
} TextSurfaceData;

/*
 * Draw a rounded rectangle path
 */
static void
textDrawTextBackground (cairo_t *cr,
                        int     x,
                        int     y,
                        int     width,
                        int     height,
                        int     radius)
{
	int x0, y0, x1, y1;

	x0 = x;
	y0 = y;
	x1 = x + width;
	y1 = y + height;

	cairo_new_path (cr);
	cairo_arc (cr, x0 + radius, y1 - radius, radius, PI / 2, PI);
	cairo_line_to (cr, x0, y0 + radius);
	cairo_arc (cr, x0 + radius, y0 + radius, radius, PI, 3 * PI / 2);
	cairo_line_to (cr, x1 - radius, y0);
	cairo_arc (cr, x1 - radius, y0 + radius, radius, 3 * PI / 2, 2 * PI);
	cairo_line_to (cr, x1, y1 - radius);
	cairo_arc (cr, x1 - radius, y1 - radius, radius, 0, PI / 2);
	cairo_close_path (cr);
}

static Bool
textInitCairo (CompScreen      *s,
               TextSurfaceData *data,
               int             width,
               int             height)
{
	Display *dpy = display.display;

	data->pixmap = None;
	if (width > 0 && height > 0)
		data->pixmap = XCreatePixmap (dpy, s->root, width, height, 32);

	data->width  = width;
	data->height = height;

	if (!data->pixmap)
	{
		compLogMessage ("text", CompLogLevelError,
		                "Couldn't create %d x %d pixmap.", width, height);
		return FALSE;
	}

	data->surface = cairo_xlib_surface_create_with_xrender_format (dpy,
	                                                               data->pixmap,
	                                                               data->screen,
	                                                               data->format,
	                                                               width,
	                                                               height);

	if (cairo_surface_status (data->surface) != CAIRO_STATUS_SUCCESS)
	{
		compLogMessage ("text", CompLogLevelError, "Couldn't create surface.");
		return FALSE;
	}

	data->cr = cairo_create (data->surface);
	if (cairo_status (data->cr) != CAIRO_STATUS_SUCCESS)
	{
		compLogMessage ("text", CompLogLevelError,
		                "Couldn't create cairo context.");
		return FALSE;
	}

	return TRUE;
}

static Bool
textInitSurface (CompScreen      *s,
                 TextSurfaceData *data)
{
	Display *dpy = display.display;

	data->screen = ScreenOfDisplay (dpy, s->screenNum);
	if (!data->screen)
	{
		compLogMessage ("text", CompLogLevelError,
		                "Couldn't get screen for %d.", s->screenNum);
		return FALSE;
	}

	data->format = XRenderFindStandardFormat (dpy, PictStandardARGB32);
	if (!data->format)
	{
		compLogMessage ("text", CompLogLevelError, "Couldn't get format.");
		return FALSE;
	}

	if (!textInitCairo (s, data, 1, 1))
		return FALSE;

	/* init pango */
	data->layout = pango_cairo_create_layout (data->cr);
	if (!data->layout)
	{
		compLogMessage ("text", CompLogLevelError,
		                "Couldn't create pango layout.");
		return FALSE;
	}

	data->font = pango_font_description_new ();
	if (!data->font)
	{
		compLogMessage ("text", CompLogLevelError,
		                "Couldn't create font description.");
		return FALSE;
	}

	return TRUE;
}

static Bool
textUpdateSurface (CompScreen      *s,
                   TextSurfaceData *data,
                   int             width,
                   int             height)
{
	Display *dpy = display.display;

	cairo_surface_destroy (data->surface);
	data->surface = NULL;

	cairo_destroy (data->cr);
	data->cr = NULL;

	XFreePixmap (dpy, data->pixmap);
	data->pixmap = None;

	return textInitCairo (s, data, width, height);
}

static Bool
textRenderTextToSurface (CompScreen           *s,
                         const char           *text,
                         TextSurfaceData      *data,
                         const CompTextAttrib *attrib)
{
	int width, height, layoutWidth;

	pango_font_description_set_family (data->font, attrib->family);
	pango_font_description_set_absolute_size (data->font,
	                                          attrib->size * PANGO_SCALE);
	pango_font_description_set_style (data->font, PANGO_STYLE_NORMAL);

	if (attrib->flags & CompTextFlagStyleBold)
		pango_font_description_set_weight (data->font, PANGO_WEIGHT_BOLD);

	if (attrib->flags & CompTextFlagStyleItalic)
		pango_font_description_set_style (data->font, PANGO_STYLE_ITALIC);

	pango_layout_set_font_description (data->layout, data->font);

	if (attrib->flags & CompTextFlagEllipsized)
		pango_layout_set_ellipsize (data->layout, PANGO_ELLIPSIZE_END);

	pango_layout_set_auto_dir (data->layout, FALSE);
	pango_layout_set_text (data->layout, text, -1);

	pango_layout_get_pixel_size (data->layout, &width, &height);

	if (attrib->flags & CompTextFlagWithBackground)
	{
		width  += 2 * attrib->bgHMargin;
		height += 2 * attrib->bgVMargin;
	}

	width  = MIN (attrib->maxWidth, width);
	height = MIN (attrib->maxHeight, height);

	/* update the size of the pango layout */
	layoutWidth = attrib->maxWidth;
	if (attrib->flags & CompTextFlagWithBackground)
		layoutWidth -= 2 * attrib->bgHMargin;

	pango_layout_set_width (data->layout, layoutWidth * PANGO_SCALE);

	if (!textUpdateSurface (s, data, width, height))
		return FALSE;

	pango_cairo_update_layout (data->cr, data->layout);

	cairo_save (data->cr);
	cairo_set_operator (data->cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint (data->cr);
	cairo_restore (data->cr);

	cairo_set_operator (data->cr, CAIRO_OPERATOR_OVER);

	if (attrib->flags & CompTextFlagWithBackground)
	{
		textDrawTextBackground (data->cr, 0, 0, width, height,
		                        MIN (attrib->bgHMargin, attrib->bgVMargin));
		cairo_set_source_rgba (data->cr,
		                       attrib->bgColor[0] / 65535.0,
		                       attrib->bgColor[1] / 65535.0,
		                       attrib->bgColor[2] / 65535.0,
		                       attrib->bgColor[3] / 65535.0);
		cairo_fill (data->cr);
		cairo_move_to (data->cr, attrib->bgHMargin, attrib->bgVMargin);
	}

	cairo_set_source_rgba (data->cr,
	                       attrib->color[0] / 65535.0,
	                       attrib->color[1] / 65535.0,
	                       attrib->color[2] / 65535.0,
	                       attrib->color[3] / 65535.0);

	pango_cairo_show_layout (data->cr, data->layout);

	return TRUE;
}

static void
textCleanupSurface (TextSurfaceData *data)
{
	if (data->layout)
		g_object_unref (data->layout);
	if (data->surface)
		cairo_surface_destroy (data->surface);
	if (data->cr)
		cairo_destroy (data->cr);
	if (data->font)
		pango_font_description_free (data->font);
}

CompTextData *
textRenderText (CompScreen           *s,
                const char           *text,
                const CompTextAttrib *attrib)
{
	TextSurfaceData surface;
	CompTextData    *retval = NULL;

	if (!text || !strlen (text))
		return NULL;

	memset (&surface, 0, sizeof (TextSurfaceData));

	if (textInitSurface (s, &surface) &&
	    textRenderTextToSurface (s, text, &surface, attrib))
	{
		retval = calloc (1, sizeof (CompTextData));
		if (retval && !(attrib->flags & CompTextFlagNoAutoBinding))
		{
			retval->texture = malloc (sizeof (CompTexture));
			if (!retval->texture)
			{
				free (retval);
				retval = NULL;
			}
		}

		if (retval)
		{
			retval->pixmap = surface.pixmap;
			retval->width  = surface.width;
			retval->height = surface.height;

			if (retval->texture)
			{
				initTexture (s, retval->texture);
				if (!bindPixmapToTexture (s, retval->texture, retval->pixmap,
				    retval->width, retval->height, 32))
				{
					compLogMessage ("text", CompLogLevelError,
				                    "Failed to bind text pixmap to texture.");
					free (retval->texture);
					free (retval);
					retval = NULL;
				}
			}
		}
	}

	if (!retval && surface.pixmap)
		XFreePixmap (display.display, surface.pixmap);

	textCleanupSurface (&surface);

	return retval;
}

CompTextData *
textRenderWindowTitle (CompScreen           *s,
                       CompWindow           *w,
                       Bool                 withViewportNumber,
                       const CompTextAttrib *attrib)
{
	char         *text = NULL;
	CompTextData *retval;

	if (withViewportNumber)
	{
		char *title;

		title = getWindowTitle (w);
		if (title)
		{
			int vx, vy, viewport;
			int bytes;

			defaultViewportForWindow (w, &vx, &vy);
			viewport = vy * w->screen->hsize + vx + 1;
			bytes = asprintf (&text, "%s -[%d]-", title, viewport);
			free (title);
		}
	}
	else
	{
		text = getWindowTitle (w);

	}

	retval = textRenderText (s, text, attrib);

	if (text)
		free (text);

	return retval;
}

void
textDrawText (CompScreen         *s,
              const CompTextData *data,
              float              x,
              float              y,
              float              alpha)
{
	GLboolean  wasBlend;
	GLint      oldBlendSrc, oldBlendDst;
	CompMatrix *m;
	float      width, height;

	if (!data || !data->texture)
		return;

	glGetIntegerv (GL_BLEND_SRC, &oldBlendSrc);
	glGetIntegerv (GL_BLEND_DST, &oldBlendDst);

	wasBlend = glIsEnabled (GL_BLEND);
	if (!wasBlend)
		glEnable (GL_BLEND);

	glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glColor4f (alpha, alpha, alpha, alpha);

	enableTexture (s, data->texture, COMP_TEXTURE_FILTER_GOOD);

	m      = &data->texture->matrix;
	width  = data->width;
	height = data->height;

	glBegin (GL_QUADS);

	glTexCoord2f (COMP_TEX_COORD_X (m, 0), COMP_TEX_COORD_Y (m, 0));
	glVertex2f (x, y - height);
	glTexCoord2f (COMP_TEX_COORD_X (m, 0), COMP_TEX_COORD_Y (m, height));
	glVertex2f (x, y);
	glTexCoord2f (COMP_TEX_COORD_X (m, width), COMP_TEX_COORD_Y (m, height));
	glVertex2f (x + width, y);
	glTexCoord2f (COMP_TEX_COORD_X (m, width), COMP_TEX_COORD_Y (m, 0));
	glVertex2f (x + width, y - height);

	glEnd ();

	disableTexture (s, data->texture);
	glColor4usv (defaultColor);

	if (!wasBlend)
		glDisable (GL_BLEND);
	glBlendFunc (oldBlendSrc, oldBlendDst);
}

void
textFiniTextData (CompScreen   *s,
                  CompTextData *data)
{
	if (data->texture)
	{
		finiTexture (s, data->texture);
		free (data->texture);
	}

	XFreePixmap (display.display, data->pixmap);

	free (data);
}








