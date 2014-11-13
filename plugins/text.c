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

#include <compiz-core.h>
#include "compiz-text.h"

#define PI 3.14159265359f

static CompMetadata textMetadata;

static int displayPrivateIndex;
static int functionsPrivateIndex;

#define TEXT_DISPLAY_OPTION_ABI    0
#define TEXT_DISPLAY_OPTION_INDEX  1
#define TEXT_DISPLAY_OPTION_NUM    2

typedef struct _TextDisplay {
    Atom visibleNameAtom;

    CompOption opt[TEXT_DISPLAY_OPTION_NUM];
} TextDisplay;

#define GET_TEXT_DISPLAY(d)				    \
    ((TextDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define TEXT_DISPLAY(d)			 \
    TextDisplay *td = GET_TEXT_DISPLAY (d)

#define NUM_OPTIONS(d) (sizeof ((d)->opt) / sizeof (CompOption))

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

static char *
textGetUtf8Property (CompDisplay *d,
		     Window      id,
		     Atom        atom)
{
    Atom          type;
    int           result, format;
    unsigned long nItems, bytesAfter;
    char          *val, *retval = NULL;

    result = XGetWindowProperty (d->display, id, atom, 0L, 65536, False,
				 d->utf8StringAtom, &type, &format, &nItems,
				 &bytesAfter, (unsigned char **) &val);

    if (result != Success)
	return NULL;

    if (type == d->utf8StringAtom && format == 8 && val && nItems > 0)
    {
	retval = malloc (sizeof (char) * (nItems + 1));
	if (retval)
	{
	    strncpy (retval, val, nItems);
	    retval[nItems] = 0;
	}
    }

    if (val)
	XFree (val);

    return retval;
}

static char *
textGetTextProperty (CompDisplay *d,
		     Window      id,
		     Atom        atom)
{
    XTextProperty text;
    char          *retval = NULL;

    text.nitems = 0;
    if (XGetTextProperty (d->display, id, &text, atom))
    {
        if (text.value)
	{
	    retval = malloc (sizeof (char) * (text.nitems + 1));
	    if (retval)
	    {
		strncpy (retval, (char *) text.value, text.nitems);
		retval[text.nitems] = 0;
	    }

	    XFree (text.value);
	}
    }

    return retval;
}

static char *
textGetWindowName (CompDisplay *d,
		   Window      id)
{
    char *name;

    TEXT_DISPLAY (d);

    name = textGetUtf8Property (d, id, td->visibleNameAtom);

    if (!name)
	name = textGetUtf8Property (d, id, d->wmNameAtom);

    if (!name)
	name = textGetTextProperty (d, id, XA_WM_NAME);

    return name;
}

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
    Display *dpy = s->display->display;

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
    Display *dpy = s->display->display;

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
    Display *dpy = s->display->display;

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

static CompTextData *
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
	XFreePixmap (s->display->display, surface.pixmap);

    textCleanupSurface (&surface);

    return retval;
}

static CompTextData *
textRenderWindowTitle (CompScreen           *s,
		       Window               window,
		       Bool                 withViewportNumber,
		       const CompTextAttrib *attrib)
{
    char         *text = NULL;
    CompTextData *retval;

    if (withViewportNumber)
    {
	char *title;
	
	title = textGetWindowName (s->display, window);
	if (title)
	{
	    CompWindow *w;

	    w = findWindowAtDisplay (s->display, window);
	    if (w)
	    {
		int vx, vy, viewport;

		defaultViewportForWindow (w, &vx, &vy);
		viewport = vy * w->screen->hsize + vx + 1;
		asprintf (&text, "%s -[%d]-", title, viewport);
		free (title);
	    }
	    else
	    {
		text = title;
	    }
	}
    }
    else
    {
	text = textGetWindowName (s->display, window);
    }

    retval = textRenderText (s, text, attrib);

    if (text)
	free (text);

    return retval;
}

static void
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

    if (!data->texture)
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

static void
textFiniTextData (CompScreen   *s,
		  CompTextData *data)
{
    if (data->texture)
    {
	finiTexture (s, data->texture);
	free (data->texture);
    }

    XFreePixmap (s->display->display, data->pixmap);

    free (data);
}

static TextFunc textFunctions =
{
    .renderText        = textRenderText,
    .renderWindowTitle = textRenderWindowTitle,
    .drawText          = textDrawText,
    .finiTextData      = textFiniTextData
};
static const CompMetadataOptionInfo textDisplayOptionInfo[] = {
    { "abi", "int", 0, 0, 0 },
    { "index", "int", 0, 0, 0 }
};

static CompOption *
textGetDisplayOptions (CompPlugin  *plugin,
		       CompDisplay *display,
		       int         *count)
{
    TEXT_DISPLAY (display);

    *count = NUM_OPTIONS (td);
    return td->opt;
}

static Bool
textInitDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    TextDisplay *td;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    td = malloc (sizeof (TextDisplay));
    if (!td)
	return FALSE;

    if (!compInitDisplayOptionsFromMetadata (d,
					     &textMetadata,
					     textDisplayOptionInfo,
					     td->opt,
					     TEXT_DISPLAY_OPTION_NUM))
    {
	free (td);
	return FALSE;
    }

    td->visibleNameAtom = XInternAtom (d->display,
				       "_NET_WM_VISIBLE_NAME", 0);

    td->opt[TEXT_DISPLAY_OPTION_ABI].value.i   = TEXT_ABIVERSION;
    td->opt[TEXT_DISPLAY_OPTION_INDEX].value.i = functionsPrivateIndex;

    d->base.privates[displayPrivateIndex].ptr   = td;
    d->base.privates[functionsPrivateIndex].ptr = &textFunctions;

    return TRUE;
}

static void
textFiniDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    TEXT_DISPLAY (d);

    compFiniDisplayOptions (d, td->opt, TEXT_DISPLAY_OPTION_NUM);

    free (td);
}

static CompBool
textInitObject (CompPlugin *p,
		CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) textInitDisplay
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
textFiniObject (CompPlugin *p,
		CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) textFiniDisplay
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static CompOption *
textGetObjectOptions (CompPlugin *plugin,
		      CompObject *object,
		      int        *count)
{
    static GetPluginObjectOptionsProc dispTab[] = {
	(GetPluginObjectOptionsProc) 0, /* GetCoreOptions */
	(GetPluginObjectOptionsProc) textGetDisplayOptions
    };
    
    *count = 0;
    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab),
		     NULL, (plugin, object, count));
}


static Bool
textInit (CompPlugin *p)
{
    if (!compInitPluginMetadataFromInfo (&textMetadata,
					 p->vTable->name,
					 textDisplayOptionInfo,
					 TEXT_DISPLAY_OPTION_NUM,
					 NULL, 0))
	return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
	compFiniMetadata (&textMetadata);
	return FALSE;
    }

    functionsPrivateIndex = allocateDisplayPrivateIndex ();
    if (functionsPrivateIndex < 0)
    {
	freeDisplayPrivateIndex (displayPrivateIndex);
	compFiniMetadata (&textMetadata);
	return FALSE;
    }

    compAddMetadataFromFile (&textMetadata, p->vTable->name);

    return TRUE;
}

static void
textFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
    freeDisplayPrivateIndex (functionsPrivateIndex);
    compFiniMetadata (&textMetadata);
}

static CompMetadata *
textGetMetadata (CompPlugin *p)
{
    return &textMetadata;
}

CompPluginVTable textVTable = {
    "text",
    textGetMetadata,
    textInit,
    textFini,
    textInitObject,
    textFiniObject,
    textGetObjectOptions,
    0
};

CompPluginVTable *
getCompPluginInfo20070830 (void)
{
    return &textVTable;
}

