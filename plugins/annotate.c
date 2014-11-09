/*
 * Copyright © 2006 Novell, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Novell, Inc. not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Novell, Inc. makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * NOVELL, INC. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NOVELL, INC. BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: David Reveman <davidr@novell.com>
 */

// TODO:
// Actions 
// Allow different colors per screen

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <cairo-xlib-xrender.h>

#include <fusilli-core.h>

static int bananaIndex;

static int displayPrivateIndex;

static CompKeyBinding    clear_key;
static CompButtonBinding initiate_button, erase_button, clear_button;

static int annoLastPointerX = 0;
static int annoLastPointerY = 0;

typedef struct _AnnoDisplay {
	int             screenPrivateIndex;
	HandleEventProc handleEvent;
} AnnoDisplay;

typedef struct _AnnoScreen {
	PaintOutputProc paintOutput;
	int             grabIndex;

	Pixmap           pixmap;
	CompTexture      texture;
	cairo_surface_t  *surface;
	cairo_t          *cairo;
	Bool             content;

	Bool eraseMode;
} AnnoScreen;

#define GET_ANNO_DISPLAY(d) \
        ((AnnoDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define ANNO_DISPLAY(d) \
        AnnoDisplay *ad = GET_ANNO_DISPLAY (d)

#define GET_ANNO_SCREEN(s, ad) \
        ((AnnoScreen *) (s)->base.privates[(ad)->screenPrivateIndex].ptr)

#define ANNO_SCREEN(s) \
        AnnoScreen *as = GET_ANNO_SCREEN (s, GET_ANNO_DISPLAY (&display))

static void
annoCairoClear (CompScreen *s,
                cairo_t    *cr)
{
	ANNO_SCREEN (s);

	cairo_save (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint (cr);
	cairo_restore (cr);

	as->content = FALSE;
}

static cairo_t *
annoCairoContext (CompScreen *s)
{
	ANNO_SCREEN (s);

	if (!as->cairo)
	{
		XRenderPictFormat *format;
		Screen            *screen;
		int               w, h;

		screen = ScreenOfDisplay (display.display, s->screenNum);

		w = s->width;
		h = s->height;

		format = XRenderFindStandardFormat (display.display,
		                                    PictStandardARGB32);

		as->pixmap = XCreatePixmap (display.display, s->root, w, h, 32);

		if (!bindPixmapToTexture (s, &as->texture, as->pixmap, w, h, 32))
		{
			compLogMessage ("annotate", CompLogLevelError,
			                "Couldn't bind pixmap 0x%x to texture",
			                (int) as->pixmap);

			XFreePixmap (display.display, as->pixmap);

			return NULL;
		}

		as->surface =
		    cairo_xlib_surface_create_with_xrender_format (display.display,
		                                                   as->pixmap, screen,
		                                                   format, w, h);

		as->cairo = cairo_create (as->surface);

		annoCairoClear (s, as->cairo);
	}

	return as->cairo;
}

static void
annoSetSourceColor (cairo_t	       *cr,
                    unsigned short *color)
{
	cairo_set_source_rgba (cr,
	                       (double) color[0] / 0xffff,
	                       (double) color[1] / 0xffff,
	                       (double) color[2] / 0xffff,
	                       (double) color[3] / 0xffff);
}
/*
static void
annoDrawCircle (CompScreen     *s,
                double         xc,
                double         yc,
                double         radius,
                unsigned short *fillColor,
                unsigned short *strokeColor,
                double         strokeWidth)
{
	REGION  reg;
	cairo_t *cr;

	ANNO_SCREEN (s);

	cr = annoCairoContext (s);
	if (cr)
	{
		double  ex1, ey1, ex2, ey2;

		annoSetSourceColor (cr, fillColor);
		cairo_arc (cr, xc, yc, radius, 0, 2 * M_PI);
		cairo_fill_preserve (cr);
		cairo_set_line_width (cr, strokeWidth);
		cairo_stroke_extents (cr, &ex1, &ey1, &ex2, &ey2);
		annoSetSourceColor (cr, strokeColor);
		cairo_stroke (cr);

		reg.rects    = &reg.extents;
		reg.numRects = 1;

		reg.extents.x1 = ex1;
		reg.extents.y1 = ey1;
		reg.extents.x2 = ex2;
		reg.extents.y2 = ey2;

		as->content = TRUE;
		damageScreenRegion (s, &reg);
	}
}*/
/*
static void
annoDrawRectangle (CompScreen *s,
                   double     x,
                   double     y,
                   double     w,
                   double     h,
                   unsigned short *fillColor,
                   unsigned short *strokeColor,
                   double     strokeWidth)
{
	REGION reg;
	cairo_t *cr;

	ANNO_SCREEN (s);

	cr = annoCairoContext (s);
	if (cr)
	{
		double  ex1, ey1, ex2, ey2;

		annoSetSourceColor (cr, fillColor);
		cairo_rectangle (cr, x, y, w, h);
		cairo_fill_preserve (cr);
		cairo_set_line_width (cr, strokeWidth);
		cairo_stroke_extents (cr, &ex1, &ey1, &ex2, &ey2);
		annoSetSourceColor (cr, strokeColor);
		cairo_stroke (cr);

		reg.rects    = &reg.extents;
		reg.numRects = 1;

		reg.extents.x1 = ex1;
		reg.extents.y1 = ey1;
		reg.extents.x2 = ex2 + 2.0;
		reg.extents.y2 = ey2 + 2.0;

		as->content = TRUE;
		damageScreenRegion (s, &reg);
	}
}*/

static void
annoDrawLine (CompScreen     *s,
              double         x1,
              double         y1,
              double         x2,
              double         y2,
              double         width,
              unsigned short *color)
{
	REGION reg;
	cairo_t *cr;

	ANNO_SCREEN (s);

	cr = annoCairoContext (s);
	if (cr)
	{
		double ex1, ey1, ex2, ey2;

		cairo_set_line_width (cr, width);
		cairo_move_to (cr, x1, y1);
		cairo_line_to (cr, x2, y2);
		cairo_stroke_extents (cr, &ex1, &ey1, &ex2, &ey2);
		cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
		annoSetSourceColor (cr, color);
		cairo_stroke (cr);

		reg.rects    = &reg.extents;
		reg.numRects = 1;

		reg.extents.x1 = ex1;
		reg.extents.y1 = ey1;
		reg.extents.x2 = ex2;
		reg.extents.y2 = ey2;

		as->content = TRUE;
		damageScreenRegion (s, &reg);
	}
}
/*
static void
annoDrawText (CompScreen     *s,
              double         x,
              double         y,
              char           *text,
              char           *fontFamily,
              double         fontSize,
              int            fontSlant,
              int            fontWeight,
              unsigned short *fillColor,
              unsigned short *strokeColor,
              double         strokeWidth)
{
	REGION  reg;
	cairo_t *cr;

	ANNO_SCREEN (s);

	cr = annoCairoContext (s);
	if (cr)
	{
		cairo_text_extents_t extents;

		cairo_set_line_width (cr, strokeWidth);
		annoSetSourceColor (cr, fillColor);
		cairo_select_font_face (cr, fontFamily, fontSlant, fontWeight);
		cairo_set_font_size (cr, fontSize);
		cairo_text_extents (cr, text, &extents);
		cairo_save (cr);
		cairo_move_to (cr, x, y);
		cairo_text_path (cr, text);
		cairo_fill_preserve (cr);
		annoSetSourceColor (cr, strokeColor);
		cairo_stroke (cr);
		cairo_restore (cr);

		reg.rects    = &reg.extents;
		reg.numRects = 1;

		reg.extents.x1 = x;
		reg.extents.y1 = y + extents.y_bearing - 2.0;
		reg.extents.x2 = x + extents.width + 20.0;
		reg.extents.y2 = y + extents.height;

		as->content = TRUE;
		damageScreenRegion (s, &reg);
	}
}
*/
/*
static Bool
annoDraw (CompDisplay     *d,
          CompAction      *action,
          CompActionState state,
          CompOption      *option,
          int             nOption)
{
	CompScreen *s;
	Window     xid;

	xid  = getIntOptionNamed (option, nOption, "root", 0);

	s = findScreenAtDisplay (d, xid);
	if (s)
	{
		cairo_t *cr;

		cr = annoCairoContext (s);
		if (cr)
		{
			char           *tool;
			unsigned short *fillColor, *strokeColor;
			double         lineWidth, strokeWidth;

			ANNO_DISPLAY (d);

			tool = getStringOptionNamed (option, nOption, "tool", "line");

			cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
			cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);

			//fillColor = ad->opt[ANNO_DISPLAY_OPTION_FILL_COLOR].value.c;
			fillColor = getColorOptionNamed (option, nOption, "fill_color",
			                                 fillColor);

			//strokeColor = ad->opt[ANNO_DISPLAY_OPTION_STROKE_COLOR].value.c;
			strokeColor = getColorOptionNamed (option, nOption,
			                                   "stroke_color", strokeColor);

			//strokeWidth = ad->opt[ANNO_DISPLAY_OPTION_STROKE_WIDTH].value.f;
			strokeWidth = getFloatOptionNamed (option, nOption, "stroke_width",
			                                   strokeWidth);

			//lineWidth = ad->opt[ANNO_DISPLAY_OPTION_LINE_WIDTH].value.f;
			lineWidth = getFloatOptionNamed (option, nOption, "line_width",
			                                 lineWidth);

			if (strcasecmp (tool, "rectangle") == 0)
			{
				double x, y, w, h;

				x = getFloatOptionNamed (option, nOption, "x", 0);
				y = getFloatOptionNamed (option, nOption, "y", 0);
				w = getFloatOptionNamed (option, nOption, "w", 100);
				h = getFloatOptionNamed (option, nOption, "h", 100);

				annoDrawRectangle (s, x, y, w, h, fillColor, strokeColor,
				                   strokeWidth);
			}
			else if (strcasecmp (tool, "circle") == 0)
			{
				double xc, yc, r;

				xc = getFloatOptionNamed (option, nOption, "xc", 0);
				yc = getFloatOptionNamed (option, nOption, "yc", 0);
				r  = getFloatOptionNamed (option, nOption, "radius", 100);

				annoDrawCircle (s, xc, yc, r, fillColor, strokeColor,
				                strokeWidth);
			}
			else if (strcasecmp (tool, "line") == 0)
			{
				double x1, y1, x2, y2;

				x1 = getFloatOptionNamed (option, nOption, "x1", 0);
				y1 = getFloatOptionNamed (option, nOption, "y1", 0);
				x2 = getFloatOptionNamed (option, nOption, "x2", 100);
				y2 = getFloatOptionNamed (option, nOption, "y2", 100);

				annoDrawLine (s, x1, y1, x2, y2, lineWidth, fillColor);
			}
			else if (strcasecmp (tool, "text") == 0)
			{
				double       x, y, size;
				char         *text, *family;
				unsigned int slant, weight;
				char         *str;

				str = getStringOptionNamed (option, nOption, "slant", "");
				if (strcasecmp (str, "oblique") == 0)
					slant = CAIRO_FONT_SLANT_OBLIQUE;
				else if (strcasecmp (str, "italic") == 0)
					slant = CAIRO_FONT_SLANT_ITALIC;
				else
					slant = CAIRO_FONT_SLANT_NORMAL;

				str = getStringOptionNamed (option, nOption, "weight", "");
				if (strcasecmp (str, "bold") == 0)
					weight = CAIRO_FONT_WEIGHT_BOLD;
				else
					weight = CAIRO_FONT_WEIGHT_NORMAL;

				x      = getFloatOptionNamed (option, nOption, "x", 0);
				y      = getFloatOptionNamed (option, nOption, "y", 0);
				text   = getStringOptionNamed (option, nOption, "text", "");
				family = getStringOptionNamed (option, nOption, "family",
				                              "Sans");
				size   = getFloatOptionNamed (option, nOption, "size", 36.0);

				annoDrawText (s, x, y, text, family, size, slant, weight,
				              fillColor, strokeColor, strokeWidth);
			}
		}
	}

	return FALSE;
}
*/


static Bool
annoInitiate (BananaArgument     *arg,
              int                nArg)
{
	CompScreen *s;
	Window     xid;

	BananaValue *root = getArgNamed ("root", arg, nArg);

	if (root != NULL)
		xid = root->i;
	else
		xid = 0;

	s = findScreenAtDisplay (xid);
	if (s)
	{
		ANNO_SCREEN (s);

		if (otherScreenGrabExist (s, NULL))
			return FALSE;

		if (!as->grabIndex)
			as->grabIndex = pushScreenGrab (s, None, "annotate");

		annoLastPointerX = pointerX;
		annoLastPointerY = pointerY;

		as->eraseMode = FALSE;
	}

	return TRUE;
}

static Bool
annoTerminate (BananaArgument     *arg,
               int                nArg)
{
	CompScreen *s;
	Window     xid;

	BananaValue *root = getArgNamed ("root", arg, nArg);

	if (root != NULL)
		xid = root->i;
	else
		xid = 0;

	for (s = display.screens; s; s = s->next)
	{
		ANNO_SCREEN (s);

		if (xid && s->root != xid)
			continue;

		if (as->grabIndex)
		{
			removeScreenGrab (s, as->grabIndex, NULL);
			as->grabIndex = 0;
		}
	}

	return FALSE;
}

static Bool
annoEraseInitiate (BananaArgument     *arg,
                   int                nArg)
{
	CompScreen *s;
	Window     xid;

	BananaValue *root = getArgNamed ("root", arg, nArg);

	if (root != NULL)
		xid = root->i;
	else
		xid = 0;

	s = findScreenAtDisplay (xid);
	if (s)
	{
		ANNO_SCREEN (s);

		if (otherScreenGrabExist (s, NULL))
			return FALSE;

		if (!as->grabIndex)
			as->grabIndex = pushScreenGrab (s, None, "annotate");

		annoLastPointerX = pointerX;
		annoLastPointerY = pointerY;

		as->eraseMode = TRUE;
	}

	return FALSE;
}

static Bool
annoClear (BananaArgument     *arg,
           int                nArg)
{
	CompScreen *s;
	Window     xid;

	BananaValue *root = getArgNamed ("root", arg, nArg);

	if (root != NULL)
		xid = root->i;
	else
		xid = 0;

	s = findScreenAtDisplay (xid);
	if (s)
	{
		ANNO_SCREEN (s);

		if (as->content)
		{
			cairo_t *cr;

			cr = annoCairoContext (s);
			if (cr)
				annoCairoClear (s, as->cairo);

			damageScreen (s);
		}

		return TRUE;
	}

	return FALSE;
}

static Bool
annoPaintOutput (CompScreen              *s,
                 const ScreenPaintAttrib *sAttrib,
                 const CompTransform     *transform,
                 Region                  region,
                 CompOutput              *output,
                 unsigned int            mask)
{
	Bool status;

	ANNO_SCREEN (s);

	UNWRAP (as, s, paintOutput);
	status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
	WRAP (as, s, paintOutput, annoPaintOutput);

	if (status && as->content && region->numRects)
	{
		BoxPtr pBox;
		int    nBox;

		glPushMatrix ();

		prepareXCoords (s, output, -DEFAULT_Z_CAMERA);

		glDisableClientState (GL_TEXTURE_COORD_ARRAY);
		glEnable (GL_BLEND);

		enableTexture (s, &as->texture, COMP_TEXTURE_FILTER_FAST);

		pBox = region->rects;
		nBox = region->numRects;

		glBegin (GL_QUADS);

		while (nBox--)
		{
			glTexCoord2f (COMP_TEX_COORD_X (&as->texture.matrix, pBox->x1),
			              COMP_TEX_COORD_Y (&as->texture.matrix, pBox->y2));
			glVertex2i (pBox->x1, pBox->y2);
			glTexCoord2f (COMP_TEX_COORD_X (&as->texture.matrix, pBox->x2),
			              COMP_TEX_COORD_Y (&as->texture.matrix, pBox->y2));
			glVertex2i (pBox->x2, pBox->y2);
			glTexCoord2f (COMP_TEX_COORD_X (&as->texture.matrix, pBox->x2),
			              COMP_TEX_COORD_Y (&as->texture.matrix, pBox->y1));
			glVertex2i (pBox->x2, pBox->y1);
			glTexCoord2f (COMP_TEX_COORD_X (&as->texture.matrix, pBox->x1),
			              COMP_TEX_COORD_Y (&as->texture.matrix, pBox->y1));
			glVertex2i (pBox->x1, pBox->y1);

			pBox++;
		}

		glEnd ();

		disableTexture (s, &as->texture);

		glDisable (GL_BLEND);
		glEnableClientState (GL_TEXTURE_COORD_ARRAY);

		glPopMatrix ();
	}

	return status;
}

static void
annoHandleMotionEvent (CompScreen *s,
                       int        xRoot,
                       int        yRoot)
{
	ANNO_SCREEN (s);

	if (as->grabIndex)
	{
		if (as->eraseMode)
		{
			static unsigned short color[] = { 0, 0, 0, 0 };

			annoDrawLine (s,
			              annoLastPointerX, annoLastPointerY,
			              xRoot, yRoot,
			              20.0, color);
		}
		else
		{
			const BananaValue *
			option_line_width = bananaGetOption (bananaIndex,
			                                     "line_width",
			                                     -1);

			const BananaValue *
			option_fill_color = bananaGetOption (bananaIndex,
			                                     "fill_color",
			                                     -1);

			static unsigned short color[] = { 0, 0, 0, 0 };

			stringToColor (option_fill_color->s, color);

			annoDrawLine (s,
			              annoLastPointerX, annoLastPointerY,
			              xRoot, yRoot,
			              option_line_width->f,
			              color);
		}

		annoLastPointerX = xRoot;
		annoLastPointerY = yRoot;
	}
}

static void
annoHandleEvent (XEvent      *event)
{
	CompScreen *s;

	ANNO_DISPLAY (&display);

	switch (event->type) {
	case MotionNotify:
		s = findScreenAtDisplay (event->xmotion.root);
		if (s)
			annoHandleMotionEvent (s, pointerX, pointerY);
		break;
	case EnterNotify:
	case LeaveNotify:
		s = findScreenAtDisplay (event->xcrossing.root);
		if (s)
			annoHandleMotionEvent (s, pointerX, pointerY);
	case KeyPress:
		if (isKeyPressEvent (event, &clear_key))
		{
			BananaArgument arg;
			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xkey.root;
			annoClear (&arg, 1);
		}
		break;
	case KeyRelease:
		break;
	case ButtonPress:
		if (isButtonPressEvent (event, &initiate_button))
		{
			BananaArgument arg;
			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;
			annoInitiate (&arg, 1);
		}

		if (isButtonPressEvent (event, &erase_button))
		{
			BananaArgument arg;
			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;
			annoEraseInitiate (&arg, 1);
		}
		break;
	case ButtonRelease:
		if (initiate_button.button == event->xbutton.button ||
		    erase_button.button == event->xbutton.button)
		{
			BananaArgument arg;
			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;
			annoTerminate (&arg, 1);
		}
		break;

	default:
		break;
	}

	UNWRAP (ad, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (ad, &display, handleEvent, annoHandleEvent);
}

static Bool
annoInitDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	AnnoDisplay *ad;

	ad = malloc (sizeof (AnnoDisplay));
	if (!ad)
		return FALSE;

	ad->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (ad->screenPrivateIndex < 0)
	{
		free (ad);
		return FALSE;
	}

	WRAP (ad, d, handleEvent, annoHandleEvent);

	d->base.privates[displayPrivateIndex].ptr = ad;

	return TRUE;
}

static void
annoFiniDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	ANNO_DISPLAY (d);

	freeScreenPrivateIndex (ad->screenPrivateIndex);

	UNWRAP (ad, d, handleEvent);

	free (ad);
}

static Bool
annoInitScreen (CompPlugin *p,
                CompScreen *s)
{
	AnnoScreen *as;

	ANNO_DISPLAY (&display);

	as = malloc (sizeof (AnnoScreen));
	if (!as)
		return FALSE;

	as->grabIndex = 0;
	as->surface   = NULL;
	as->pixmap    = None;
	as->cairo     = NULL;
	as->content   = FALSE;

	initTexture (s, &as->texture);

	WRAP (as, s, paintOutput, annoPaintOutput);

	s->base.privates[ad->screenPrivateIndex].ptr = as;

	return TRUE;
}

static void
annoFiniScreen (CompPlugin *p,
                CompScreen *s)
{
	ANNO_SCREEN (s);

	if (as->cairo)
		cairo_destroy (as->cairo);

	if (as->surface)
		cairo_surface_destroy (as->surface);

	finiTexture (s, &as->texture);

	if (as->pixmap)
		XFreePixmap (display.display, as->pixmap);

	UNWRAP (as, s, paintOutput);

	free (as);
}

static CompBool
annoInitObject (CompPlugin *p,
                CompObject *o)
{
	static InitPluginObjectProc dispTab[] = {
		(InitPluginObjectProc) 0, /* InitCore */
		(InitPluginObjectProc) annoInitDisplay,
		(InitPluginObjectProc) annoInitScreen
	};

	RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
annoFiniObject (CompPlugin *p,
                CompObject *o)
{
	static FiniPluginObjectProc dispTab[] = {
		(FiniPluginObjectProc) 0, /* FiniCore */
		(FiniPluginObjectProc) annoFiniDisplay,
		(FiniPluginObjectProc) annoFiniScreen
	};

	DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static void
annoChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	if (strcasecmp (optionName, "initiate_button") == 0)
		updateButton (optionValue->s, &initiate_button);

	else if (strcasecmp (optionName, "erase_button") == 0)
		updateButton (optionValue->s, &erase_button);

	else if (strcasecmp (optionName, "clear_button") == 0)
		updateButton (optionValue->s, &clear_button);

	else if (strcasecmp (optionName, "clear_key") == 0)
		updateKey (optionValue->s, &clear_key);
}

static Bool
annoInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("annotate", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("annotate");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, annoChangeNotify);

	const BananaValue *
	option_initiate_button = bananaGetOption (bananaIndex,
	                                          "initiate_button", -1);

	const BananaValue *
	option_erase_button = bananaGetOption (bananaIndex,
	                                       "erase_button", -1);

	const BananaValue *
	option_clear_button = bananaGetOption (bananaIndex,
	                                       "clear_button", -1);

	const BananaValue *
	option_clear_key = bananaGetOption (bananaIndex,
	                                    "clear_key", -1);

	registerButton (option_initiate_button->s, &initiate_button);
	registerButton (option_erase_button->s, &erase_button);
	registerButton (option_clear_button->s, &clear_button);

	registerKey (option_clear_key->s, &clear_key);

	return TRUE;
}

static void
annoFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable annoVTable = {
	"annotate",
	annoInit,
	annoFini,
	annoInitObject,
	annoFiniObject
};

CompPluginVTable *
getCompPluginInfo20140724 (void)
{
	return &annoVTable;
}
