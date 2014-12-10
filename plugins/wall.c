/**
 *
 * Compiz wall plugin
 *
 * wall.c
 *
 * Copyright (c) 2006 Robert Carr <racarr@beryl-project.org>
 *
 * Authors:
 * Robert Carr <racarr@beryl-project.org>
 * Dennis Kasprzyk <onestone@opencompositing.org>
 * Michail Bitzes <noodlylight@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <fusilli-core.h>

#include <GL/glu.h>

#include <cairo-xlib-xrender.h>
#include <cairo.h>

#define MMMODE_SWITCH_ALL        0
#define MMMODE_SWITCH_SEPARATELY 1

#define PI 3.14159265359f
#define VIEWPORT_SWITCHER_SIZE 100
#define ARROW_SIZE 33

#define WIN_X(w) ((w)->attrib.x - (w)->input.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->input.top)
#define WIN_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define WIN_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

static int bananaIndex;

static int displayPrivateIndex;

/* Enums */
typedef enum
{
	Up = 0,
	Left,
	Down,
	Right
} Direction;

typedef enum
{
	NoTransformation,
	MiniScreen,
	Sliding
} ScreenTransformation;

typedef struct _WallCairoContext
{
	Pixmap      pixmap;
	CompTexture texture;

	cairo_surface_t *surface;
	cairo_t         *cr;

	int width;
	int height;
} WallCairoContext;

typedef struct _WallDisplay
{
	CompMatch no_slide_match;

	CompKeyBinding left_key,
	               right_key,
	               up_key,
	               down_key,
	               prev_key,
	               next_key;

	CompButtonBinding left_button,
	                  right_button,
	                  up_button,
	                  down_button,
	                  prev_button,
	                  next_button;

	CompKeyBinding left_window_key,
	               right_window_key,
	               up_window_key,
	               down_window_key;

	int screenPrivateIndex;

	HandleEventProc            handleEvent;
	MatchPropertyChangedProc   matchPropertyChanged;
} WallDisplay;

typedef struct _WallScreen
{
	int windowPrivateIndex;

	DonePaintScreenProc          donePaintScreen;
	PaintOutputProc              paintOutput;
	PaintScreenProc              paintScreen;
	PreparePaintScreenProc       preparePaintScreen;
	PaintTransformedOutputProc   paintTransformedOutput;
	PaintWindowProc              paintWindow;

	WindowAddNotifyProc          windowAddNotify;
	WindowGrabNotifyProc         windowGrabNotify;
	WindowUngrabNotifyProc       windowUngrabNotify;
	ActivateWindowProc           activateWindow;

	Bool moving; /* Used to track miniview movement */
	Bool showPreview;

	float curPosX;
	float curPosY;
	int   gotoX;
	int   gotoY;
	int   direction; /* >= 0 : direction arrow angle, < 0 : no direction */

	int boxTimeout;
	int boxOutputDevice;

	int grabIndex;
	int timer;

	Window moveWindow;

	CompWindow *grabWindow;

	Bool focusDefault;

	ScreenTransformation transform;
	CompOutput           *currOutput;

	WindowPaintAttrib mSAttribs;
	float             mSzCamera;

	int firstViewportX;
	int firstViewportY;
	int viewportWidth;
	int viewportHeight;
	int viewportBorder;

	int moveWindowX;
	int moveWindowY;

	WallCairoContext switcherContext;
	WallCairoContext thumbContext;
	WallCairoContext highlightContext;
	WallCairoContext arrowContext;
} WallScreen;

typedef struct _WallWindow
{
	Bool isSliding;
} WallWindow;

/* Helpers */
#define GET_WALL_DISPLAY(d) \
        ((WallDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define WALL_DISPLAY(d) \
        WallDisplay *wd = GET_WALL_DISPLAY (d)

#define GET_WALL_SCREEN(s, wd) \
        ((WallScreen *) (s)->privates[(wd)->screenPrivateIndex].ptr)

#define WALL_SCREEN(s) \
        WallScreen *ws = GET_WALL_SCREEN (s, GET_WALL_DISPLAY (&display))

#define GET_WALL_WINDOW(w, ws) \
        ((WallWindow *) (w)->privates[(ws)->windowPrivateIndex].ptr)

#define WALL_WINDOW(w) \
        WallWindow *ww = GET_WALL_WINDOW  (w, \
                         GET_WALL_SCREEN  (w->screen, \
                         GET_WALL_DISPLAY (&display)))

#define sigmoid(x) (1.0f / (1.0f + exp (-5.5f * 2 * ((x) - 0.5))))
#define sigmoidProgress(x) ((sigmoid (x) - sigmoid (0)) / \
                            (sigmoid (1) - sigmoid (0)))


static void
getColorRGBA (const char *optionName,
              float      *r,
              float      *g,
              float      *b,
              float      *a)
{
	unsigned short color[4];

	const BananaValue *
	option_color = bananaGetOption (bananaIndex,
	                                optionName,
	                                -1);

	stringToColor (option_color->s, color);

	*r = color[0] / 65535.0f;
	*g = color[1] / 65535.0f;
	*b = color[2] / 65535.0f;
	*a = color[3] / 65535.0f;;
}

static void
wallClearCairoLayer (cairo_t *cr)
{
	cairo_save (cr);
	cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
	cairo_paint (cr);
	cairo_restore (cr);
}

static void
wallDrawSwitcherBackground (CompScreen *s)
{
	cairo_t         *cr;
	cairo_pattern_t *pattern;
	float           outline = 2.0f;
	int             width, height, radius;
	float           r, g, b, a;
	int             i, j;

	WALL_SCREEN (s);

	cr = ws->switcherContext.cr;
	wallClearCairoLayer (cr);

	width = ws->switcherContext.width - outline;
	height = ws->switcherContext.height - outline;

	cairo_save (cr);
	cairo_translate (cr, outline / 2.0f, outline / 2.0f);

	/* set the pattern for the switcher's background */
	pattern = cairo_pattern_create_linear (0, 0, width, height);
	getColorRGBA ("background_gradient_base_color", &r, &g, &b, &a);
	cairo_pattern_add_color_stop_rgba (pattern, 0.00f, r, g, b, a);
	getColorRGBA ("background_gradient_highlight_color", &r, &g, &b, &a);
	cairo_pattern_add_color_stop_rgba (pattern, 0.65f, r, g, b, a);
	getColorRGBA ("background_gradient_shadow_color", &r, &g, &b, &a);
	cairo_pattern_add_color_stop_rgba (pattern, 0.85f, r, g, b, a);
	cairo_set_source (cr, pattern);

	/* draw the border's shape */
	const BananaValue *
	option_edge_radius = bananaGetOption (bananaIndex,
	                                      "edge_radius",
	                                      -1);

	radius = option_edge_radius->i;
	if (radius)
	{
		cairo_arc (cr, radius, radius, radius, PI, 1.5f * PI);
		cairo_arc (cr, radius + width - 2 * radius,
		           radius, radius, 1.5f * PI, 2.0 * PI);
		cairo_arc (cr, width - radius, height - radius, radius, 0,  PI / 2.0f);
		cairo_arc (cr, radius, height - radius, radius,  PI / 2.0f, PI);
	}
	else
		cairo_rectangle (cr, 0, 0, width, height);

	cairo_close_path (cr);

	/* apply pattern to background... */
	cairo_fill_preserve (cr);

	/* ... and draw an outline */
	cairo_set_line_width (cr, outline);
	getColorRGBA ("outline_color", &r, &g, &b, &a);
	cairo_set_source_rgba (cr, r, g, b, a);
	cairo_stroke(cr);

	cairo_pattern_destroy (pattern);
	cairo_restore (cr);

	cairo_save (cr);
	for (i = 0; i < s->vsize; i++)
	{
		cairo_translate (cr, 0.0, ws->viewportBorder);
		cairo_save (cr);
		for (j = 0; j < s->hsize; j++)
		{
			cairo_translate (cr, ws->viewportBorder, 0.0);

			/* this cuts a hole into our background */
			cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
			cairo_set_source_rgba (cr, 0.0, 0.0, 0.0, 1.0);
			cairo_rectangle (cr, 0, 0, ws->viewportWidth, ws->viewportHeight);

			cairo_fill_preserve (cr);
			cairo_set_operator (cr, CAIRO_OPERATOR_XOR);
			cairo_fill (cr);

			cairo_translate (cr, ws->viewportWidth, 0.0);
		}
		cairo_restore(cr);

		cairo_translate (cr, 0.0, ws->viewportHeight);
	}
	cairo_restore (cr);
}

static void
wallDrawThumb (CompScreen *s)
{
	cairo_t         *cr;
	cairo_pattern_t *pattern;
	float           r, g, b, a;
	float           outline = 2.0f;
	int             width, height;

	WALL_SCREEN(s);

	cr = ws->thumbContext.cr;
	wallClearCairoLayer (cr);

	width  = ws->thumbContext.width - outline;
	height = ws->thumbContext.height - outline;

	cairo_translate (cr, outline / 2.0f, outline / 2.0f);

	pattern = cairo_pattern_create_linear (0, 0, width, height);
	getColorRGBA ("thumb_gradient_base_color", &r, &g, &b, &a);
	cairo_pattern_add_color_stop_rgba (pattern, 0.0f, r, g, b, a);
	getColorRGBA ("thumb_gradient_highlight_color", &r, &g, &b, &a);
	cairo_pattern_add_color_stop_rgba (pattern, 1.0f, r, g, b, a);

	/* apply the pattern for thumb background */
	cairo_set_source (cr, pattern);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_fill_preserve (cr);

	cairo_set_line_width (cr, outline);
	getColorRGBA ("outline_color", &r, &g, &b, &a);
	cairo_set_source_rgba (cr, r, g, b, a);
	cairo_stroke (cr);

	cairo_pattern_destroy (pattern);

	cairo_restore (cr);
}

static void
wallDrawHighlight(CompScreen *s)
{
	cairo_t         *cr;
	cairo_pattern_t *pattern;
	int             width, height;
	float           r, g, b, a;
	float           outline = 2.0f;

	WALL_SCREEN(s);

	cr = ws->highlightContext.cr;
	wallClearCairoLayer (cr);

	width  = ws->highlightContext.width - outline;
	height = ws->highlightContext.height - outline;

	cairo_translate (cr, outline / 2.0f, outline / 2.0f);

	pattern = cairo_pattern_create_linear (0, 0, width, height);
	getColorRGBA ("thumb_highlight_gradient_base_color", &r, &g, &b, &a);
	cairo_pattern_add_color_stop_rgba (pattern, 0.0f, r, g, b, a);
	getColorRGBA ("thumb_highlight_gradient_shadow_color", &r, &g, &b, &a);
	cairo_pattern_add_color_stop_rgba (pattern, 1.0f, r, g, b, a);

	/* apply the pattern for thumb background */
	cairo_set_source (cr, pattern);
	cairo_rectangle (cr, 0, 0, width, height);
	cairo_fill_preserve (cr);

	cairo_set_line_width (cr, outline);
	getColorRGBA ("outline_color", &r, &g, &b, &a);
	cairo_set_source_rgba (cr, r, g, b, a);
	cairo_stroke (cr);

	cairo_pattern_destroy (pattern);

	cairo_restore (cr);
}

static void
wallDrawArrow (CompScreen *s)
{
	cairo_t *cr;
	float   outline = 2.0f;
	float   r, g, b, a;

	WALL_SCREEN (s);

	cr = ws->arrowContext.cr;
	wallClearCairoLayer (cr);

	cairo_translate (cr, outline / 2.0f, outline / 2.0f);

	/* apply the pattern for thumb background */
	cairo_set_line_width (cr, outline);

	/* draw top part of the arrow */
	getColorRGBA ("arrow_base_color", &r, &g, &b, &a);
	cairo_set_source_rgba (cr, r, g, b, a);
	cairo_move_to (cr, 15, 0);
	cairo_line_to (cr, 30, 30);
	cairo_line_to (cr, 15, 24.5);
	cairo_line_to (cr, 15, 0);
	cairo_fill (cr);

	/* draw bottom part of the arrow */
	getColorRGBA ("arrow_shadow_color", &r, &g, &b, &a);
	cairo_set_source_rgba (cr, r, g, b, a);
	cairo_move_to (cr, 15, 0);
	cairo_line_to (cr, 0, 30);
	cairo_line_to (cr, 15, 24.5);
	cairo_line_to (cr, 15, 0);
	cairo_fill (cr);

	/* draw the arrow outline */
	getColorRGBA ("outline_color", &r, &g, &b, &a);
	cairo_set_source_rgba (cr, r, g, b, a);
	cairo_move_to (cr, 15, 0);
	cairo_line_to (cr, 30, 30);
	cairo_line_to (cr, 15, 24.5);
	cairo_line_to (cr, 0, 30);
	cairo_line_to (cr, 15, 0);
	cairo_stroke (cr);

	cairo_restore (cr);
}

static void
wallSetupCairoContext (CompScreen       *s,
                       WallCairoContext *context)
{
	XRenderPictFormat *format;
	Screen            *screen;
	int               width, height;

	screen = ScreenOfDisplay (display.display, s->screenNum);

	width = context->width;
	height = context->height;

	initTexture (s, &context->texture);

	format = XRenderFindStandardFormat (display.display,
	                                    PictStandardARGB32);

	context->pixmap = XCreatePixmap (display.display, s->root,
	                                 width, height, 32);

	if (!bindPixmapToTexture(s, &context->texture, context->pixmap,
	                         width, height, 32))
	{
		compLogMessage ("wall", CompLogLevelError,
		                "Couldn't create cairo context for switcher");
	}

	context->surface =
	   cairo_xlib_surface_create_with_xrender_format (display.display,
	                                                  context->pixmap,
	                                                  screen, format,
	                                                  width, height);

	context->cr = cairo_create (context->surface);
	wallClearCairoLayer (context->cr);
}

static void
wallDestroyCairoContext (CompScreen       *s,
                         WallCairoContext *context)
{
	if (context->cr)
		cairo_destroy (context->cr);

	if (context->surface)
		cairo_surface_destroy (context->surface);

	finiTexture (s, &context->texture);

	if (context->pixmap)
		XFreePixmap (display.display, context->pixmap);
}

static Bool
wallCheckDestination (CompScreen *s,
                      int        destX,
                      int        destY)
{
	if (s->x - destX < 0)
		return FALSE;

	if (s->x - destX >= s->hsize)
		return FALSE;

	if (s->y - destY >= s->vsize)
		return FALSE;

	if (s->y - destY < 0)
		return FALSE;

	return TRUE;
}

static void
wallReleaseMoveWindow (CompScreen *s)
{
	CompWindow *w;
	WALL_SCREEN (s);

	w = findWindowAtScreen (s, ws->moveWindow);
	if (w)
		syncWindowPosition (w);

	ws->moveWindow = 0;
}

static void
wallComputeTranslation (CompScreen *s,
                        float      *x,
                        float      *y)
{
	float dx, dy, elapsed, duration;

	WALL_SCREEN (s);

	const BananaValue *
	option_slide_duration = bananaGetOption (bananaIndex,
	                                         "slide_duration",
	                                         -1);

	duration = option_slide_duration->f * 1000.0;
	if (duration != 0.0)
		elapsed = 1.0 - (ws->timer / duration);
	else
		elapsed = 1.0;

	if (elapsed < 0.0)
		elapsed = 0.0;
	if (elapsed > 1.0)
		elapsed = 1.0;

	/* Use temporary variables to you can pass in &ps->cur_x */
	dx = (ws->gotoX - ws->curPosX) * elapsed + ws->curPosX;
	dy = (ws->gotoY - ws->curPosY) * elapsed + ws->curPosY;

	*x = dx;
	*y = dy;
}

/* movement remainder that gets ignored for direction calculation */
#define IGNORE_REMAINDER 0.05

static void
wallDetermineMovementAngle (CompScreen *s)
{
	int angle;
	float dx, dy;

	WALL_SCREEN (s);

	dx = ws->gotoX - ws->curPosX;
	dy = ws->gotoY - ws->curPosY;

	if (dy > IGNORE_REMAINDER)
		angle = (dx > IGNORE_REMAINDER) ? 135 :
		        (dx < -IGNORE_REMAINDER) ? 225 : 180;
	else if (dy < -IGNORE_REMAINDER)
		angle = (dx > IGNORE_REMAINDER) ? 45 :
		        (dx < -IGNORE_REMAINDER) ? 315 : 0;
	else
		angle = (dx > IGNORE_REMAINDER) ? 90 :
		        (dx < -IGNORE_REMAINDER) ? 270 : -1;

	ws->direction = angle;
}

static void
wallInitiate (CompScreen *s)
{
	WALL_SCREEN (s);

	const BananaValue *
	option_show_switcher = bananaGetOption (bananaIndex,
	                                        "show_switcher",
	                                        -1);

	ws->showPreview = option_show_switcher->b;
}

static void
wallTerminate (void)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
	{
		WALL_SCREEN (s);

		if (ws->showPreview)
		{
			ws->showPreview = FALSE;
			damageScreen (s);
		}
	}
}

static Bool
wallMoveViewport (CompScreen *s,
                  int        x,
                  int        y,
                  Window     moveWindow)
{
	WALL_SCREEN (s);

	if (!x && !y)
		return FALSE;

	if (otherScreenGrabExist (s, "move", "switcher", "group-drag", "wall", NULL))
		return FALSE;

	if (!wallCheckDestination (s, x, y))
		return FALSE;

	if (ws->moveWindow != moveWindow)
	{
		CompWindow *w;

		wallReleaseMoveWindow (s);
		w = findWindowAtScreen (s, moveWindow);
		if (w)
		{
			if (!(w->type & (CompWindowTypeDesktopMask |
			                 CompWindowTypeDockMask)))
			{
				if (!(w->state & CompWindowStateStickyMask))
				{
					ws->moveWindow = w->id;
					ws->moveWindowX = w->attrib.x;
					ws->moveWindowY = w->attrib.y;
					raiseWindow (w);
				}
			}
		}
	}

	if (!ws->moving)
	{
		ws->curPosX = s->x;
		ws->curPosY = s->y;
	}
	ws->gotoX = s->x - x;
	ws->gotoY = s->y - y;

	wallDetermineMovementAngle (s);

	if (!ws->grabIndex)
		ws->grabIndex = pushScreenGrab (s, s->invisibleCursor, "wall");

	moveScreenViewport (s, x, y, TRUE);

	ws->moving          = TRUE;
	ws->focusDefault    = TRUE;
	ws->boxOutputDevice = outputDeviceForPoint (s, pointerX, pointerY);

	const BananaValue *
	option_show_switcher = bananaGetOption (bananaIndex,
	                                        "show_switcher",
	                                        -1);

	const BananaValue *
	option_preview_timeout = bananaGetOption (bananaIndex,
	                                          "preview_timeout",
	                                          -1);

	const BananaValue *
	option_slide_duration = bananaGetOption (bananaIndex,
	                                         "slide_duration",
	                                         -1);

	if (option_show_switcher->b)
		ws->boxTimeout = option_preview_timeout->f * 1000;
	else
		ws->boxTimeout = 0;

	ws->timer = option_slide_duration->f * 1000;

	damageScreen (s);

	return TRUE;
}

static void
wallCheckAmount (CompScreen *s,
                 int        dx,
                 int        dy,
                 int        *amountX,
                 int        *amountY)
{
	*amountX = -dx;
	*amountY = -dy;

	const BananaValue *
	option_allow_wraparound = bananaGetOption (bananaIndex,
	                                           "allow_wraparound",
	                                           -1);

	if (option_allow_wraparound->b)
	{
		if ((s->x + dx) < 0)
			*amountX = -(s->hsize + dx);
		else if ((s->x + dx) >= s->hsize)
			*amountX = s->hsize - dx;

		if ((s->y + dy) < 0)
			*amountY = -(s->vsize + dy);
		else if ((s->y + dy) >= s->vsize)
			*amountY = s->vsize - dy;
	}
}

static void
wallHandleEvent (XEvent      *event)
{
	CompScreen *s;

	WALL_DISPLAY (&display);

	int amountX, amountY;

	switch (event->type) {
	case KeyPress:
		s = findScreenAtDisplay (event->xkey.root);
		if (isKeyPressEvent (event, &wd->left_key))
		{
			wallCheckAmount (s, -1, 0, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isKeyPressEvent (event, &wd->right_key))
		{
			wallCheckAmount (s, 1, 0, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isKeyPressEvent (event, &wd->up_key))
		{
			wallCheckAmount (s, 0, -1, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isKeyPressEvent (event, &wd->down_key))
		{
			wallCheckAmount (s, 0, 1, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isKeyPressEvent (event, &wd->prev_key))
		{
			if ((s->x == 0) && (s->y == 0))
			{
				amountX = s->hsize - 1;
				amountY = s->vsize - 1;
			}
			else if (s->x == 0)
			{
				amountX = s->hsize - 1;
				amountY = -1;
			}
			else
			{
				amountX = -1;
				amountY = 0;
			}

			wallCheckAmount (s, amountX, amountY, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isKeyPressEvent (event, &wd->next_key))
		{
			if ((s->x == s->hsize - 1) && (s->y == s->vsize - 1))
			{
				amountX = -(s->hsize - 1);
				amountY = -(s->vsize - 1);
			}
			else if (s->x == s->hsize - 1)
			{
				amountX = -(s->hsize - 1);
				amountY = 1;
			}
			else
			{
				amountX = 1;
				amountY = 0;
			}

			wallCheckAmount (s, amountX, amountY, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isKeyPressEvent (event, &wd->left_window_key))
		{
			wallCheckAmount (s, -1, 0, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, display.activeWindow);
			wallInitiate (s);
		}
		else if (isKeyPressEvent (event, &wd->right_window_key))
		{
			wallCheckAmount (s, 1, 0, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, display.activeWindow);
			wallInitiate (s);
		}
		else if (isKeyPressEvent (event, &wd->up_window_key))
		{
			wallCheckAmount (s, 0, -1, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, display.activeWindow);
			wallInitiate (s);
		}
		else if (isKeyPressEvent (event, &wd->down_window_key))
		{
			wallCheckAmount (s, 0, 1, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, display.activeWindow);
			wallInitiate (s);
		}
		break;
	case ButtonPress:
		s = findScreenAtDisplay (event->xkey.root);
		if (isButtonPressEvent (event, &wd->left_button))
		{
			wallCheckAmount (s, -1, 0, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isButtonPressEvent (event, &wd->right_button))
		{
			wallCheckAmount (s, 1, 0, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isButtonPressEvent (event, &wd->up_button))
		{
			wallCheckAmount (s, 0, -1, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isButtonPressEvent (event, &wd->down_button))
		{
			wallCheckAmount (s, 0, 1, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isButtonPressEvent (event, &wd->prev_button))
		{
			if ((s->x == 0) && (s->y == 0))
			{
				amountX = s->hsize - 1;
				amountY = s->vsize - 1;
			}
			else if (s->x == 0)
			{
				amountX = s->hsize - 1;
				amountY = -1;
			}
			else
			{
				amountX = -1;
				amountY = 0;
			}

			wallCheckAmount (s, amountX, amountY, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		else if (isButtonPressEvent (event, &wd->next_button))
		{
			if ((s->x == s->hsize - 1) && (s->y == s->vsize - 1))
			{
				amountX = -(s->hsize - 1);
				amountY = -(s->vsize - 1);
			}
			else if (s->x == s->hsize - 1)
			{
				amountX = -(s->hsize - 1);
				amountY = 1;
			}
			else
			{
				amountX = 1;
				amountY = 0;
			}

			wallCheckAmount (s, amountX, amountY, &amountX, &amountY);
			wallMoveViewport (s, amountX, amountY, None);
			wallInitiate (s);
		}
		break;
	case ClientMessage:
		if (event->xclient.message_type == display.desktopViewportAtom)
		{
			int        dx, dy;
			CompScreen *s;

			s = findScreenAtDisplay (event->xclient.window);
			if (!s)
				break;

			if (otherScreenGrabExist (s, "switcher", "wall", NULL))
				break;

			dx = event->xclient.data.l[0] / s->width - s->x;
			dy = event->xclient.data.l[1] / s->height - s->y;

			if (!dx && !dy)
				break;

			wallMoveViewport (s, -dx, -dy, None);
		}
		break;
	default:
		if (event->type == display.xkbEvent)
		{
			XkbAnyEvent *xkbEvent = (XkbAnyEvent *) event;

			if (xkbEvent->xkb_type == XkbStateNotify)
			{
				XkbStateNotifyEvent *stateEvent = (XkbStateNotifyEvent *) event;
				if (stateEvent->event_type == KeyRelease)
				{
					wallTerminate ();
				}
			}
		}
	}

	UNWRAP (wd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (wd, &display, handleEvent, wallHandleEvent);
}

static void
wallActivateWindow (CompWindow *w)
{
	CompScreen *s = w->screen;

	WALL_SCREEN (s);

	if (w->placed && !otherScreenGrabExist (s, "wall", "switcher", NULL))
	{
		int dx, dy;

		defaultViewportForWindow (w, &dx, &dy);
		dx -= s->x;
		dy -= s->y;

		if (dx || dy)
		{
			wallMoveViewport (s, -dx, -dy, None);
			ws->focusDefault = FALSE;
		}
	}

	UNWRAP (ws, s, activateWindow);
	(*s->activateWindow) (w);
	WRAP (ws, s, activateWindow, wallActivateWindow);
}



static inline void
wallDrawQuad (CompMatrix *matrix, BOX *box)
{
	glTexCoord2f (COMP_TEX_COORD_X (matrix, box->x1),
	              COMP_TEX_COORD_Y (matrix, box->y2));
	glVertex2i (box->x1, box->y2);
	glTexCoord2f (COMP_TEX_COORD_X (matrix, box->x2),
	              COMP_TEX_COORD_Y (matrix, box->y2));
	glVertex2i (box->x2, box->y2);
	glTexCoord2f (COMP_TEX_COORD_X (matrix, box->x2),
	              COMP_TEX_COORD_Y (matrix, box->y1));
	glVertex2i (box->x2, box->y1);
	glTexCoord2f (COMP_TEX_COORD_X (matrix, box->x1),
	              COMP_TEX_COORD_Y (matrix, box->y1));
	glVertex2i (box->x1, box->y1);
}

static void
wallDrawCairoTextureOnScreen (CompScreen *s)
{
	float      centerX, centerY;
	float      width, height;
	float      topLeftX, topLeftY;
	float      border;
	int        i, j;
	CompMatrix matrix;
	BOX        box;

	WALL_SCREEN(s);

	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	glEnable (GL_BLEND);

	centerX = s->outputDev[ws->boxOutputDevice].region.extents.x1 +
	                   (s->outputDev[ws->boxOutputDevice].width / 2.0f);
	centerY = s->outputDev[ws->boxOutputDevice].region.extents.y1 +
	                   (s->outputDev[ws->boxOutputDevice].height / 2.0f);

	border = (float) ws->viewportBorder;
	width  = (float) ws->switcherContext.width;
	height = (float) ws->switcherContext.height;

	topLeftX = centerX - floor (width / 2.0f);
	topLeftY = centerY - floor (height / 2.0f);

	ws->firstViewportX = topLeftX + border;
	ws->firstViewportY = topLeftY + border;

	if (!ws->moving)
	{
		double left, timeout;

		const BananaValue *
		option_preview_timeout = bananaGetOption (bananaIndex,
		                                          "preview_timeout",
		                                          -1);

		timeout = option_preview_timeout->f * 1000.0f;
		left    = (timeout > 0) ? (float) ws->boxTimeout / timeout : 1.0f;

		if (left < 0)
			left = 0.0f;
		else if (left > 0.5)
			left = 1.0f;
		else
			left = 2 * left;

		screenTexEnvMode (s, GL_MODULATE);

		glColor4f (left, left, left, left);
		glTranslatef (0.0f,0.0f, -(1 - left));

		ws->mSzCamera = -(1 - left);
	}
	else
		ws->mSzCamera = 0.0f;

	/* draw background */

	matrix = ws->switcherContext.texture.matrix;
	matrix.x0 -= topLeftX * matrix.xx;
	matrix.y0 -= topLeftY * matrix.yy;

	box.x1 = topLeftX;
	box.x2 = box.x1 + width;
	box.y1 = topLeftY;
	box.y2 = box.y1 + height;

	enableTexture (s, &ws->switcherContext.texture, COMP_TEXTURE_FILTER_FAST);
	glBegin (GL_QUADS);
	wallDrawQuad (&matrix, &box);
	glEnd ();
	disableTexture (s, &ws->switcherContext.texture);

	/* draw thumb */
	width = (float) ws->thumbContext.width;
	height = (float) ws->thumbContext.height;

	enableTexture (s, &ws->thumbContext.texture, COMP_TEXTURE_FILTER_FAST);
	glBegin (GL_QUADS);
	for (i = 0; i < s->hsize; i++)
	{
		for (j = 0; j < s->vsize; j++)
		{
			if (i == ws->gotoX && j == ws->gotoY && ws->moving)
			continue;

			box.x1 = i * (width + border);
			box.x1 += topLeftX + border;
			box.x2 = box.x1 + width;
			box.y1 = j * (height + border);
			box.y1 += topLeftY + border;
			box.y2 = box.y1 + height;

			matrix = ws->thumbContext.texture.matrix;
			matrix.x0 -= box.x1 * matrix.xx;
			matrix.y0 -= box.y1 * matrix.yy;

			wallDrawQuad (&matrix, &box);
		}
	}
	glEnd ();
	disableTexture (s, &ws->thumbContext.texture);

	if (ws->moving || ws->showPreview)
	{
		/* draw highlight */
		int   aW, aH;

		box.x1 = s->x * (width + border) + topLeftX + border;
		box.x2 = box.x1 + width;
		box.y1 = s->y * (height + border) + topLeftY + border;
		box.y2 = box.y1 + height;

		matrix = ws->highlightContext.texture.matrix;
		matrix.x0 -= box.x1 * matrix.xx;
		matrix.y0 -= box.y1 * matrix.yy;

		enableTexture (s, &ws->highlightContext.texture,
		                  COMP_TEXTURE_FILTER_FAST);
		glBegin (GL_QUADS);
		wallDrawQuad (&matrix, &box);
		glEnd ();
		disableTexture (s, &ws->highlightContext.texture);

		/* draw arrow */
		if (ws->direction >= 0)
		{
			enableTexture (s, &ws->arrowContext.texture,
			                   COMP_TEXTURE_FILTER_GOOD);

			aW = ws->arrowContext.width;
			aH = ws->arrowContext.height;

			/* if we have a viewport preview we just paint the
			   arrow outside the switcher */

			const BananaValue *
			option_miniscreen = bananaGetOption (bananaIndex,
			                                     "miniscreen",
			                                     -1);

			if (option_miniscreen->b)
			{
				width  = (float) ws->switcherContext.width;
				height = (float) ws->switcherContext.height;

				switch (ws->direction) {
				/* top left */
				case 315:
					box.x1 = topLeftX - aW - border;
					box.y1 = topLeftY - aH - border;
					break;
				/* up */
				case 0:
					box.x1 = topLeftX + width / 2.0f - aW / 2.0f;
					box.y1 = topLeftY - aH - border;
					break;
				/* top right */
				case 45:
					box.x1 = topLeftX + width + border;
					box.y1 = topLeftY - aH - border;
					break;
				/* right */
				case 90:
					box.x1 = topLeftX + width + border;
					box.y1 = topLeftY + height / 2.0f - aH / 2.0f;
					break;
				/* bottom right */
				case 135:
					box.x1 = topLeftX + width + border;
					box.y1 = topLeftY + height + border;
					break;
				/* down */
				case 180:
					box.x1 = topLeftX + width / 2.0f - aW / 2.0f;
					box.y1 = topLeftY + height + border;
					break;
				/* bottom left */
				case 225:
					box.x1 = topLeftX - aW - border;
					box.y1 = topLeftY + height + border;
					break;
				/* left */
				case 270:
					box.x1 = topLeftX - aW - border;
					box.y1 = topLeftY + height / 2.0f - aH / 2.0f;
					break;
				default:
					break;
				}
			}
			else
			{
				/* arrow is visible (no preview is painted over it) */
				box.x1 = s->x * (width + border) + topLeftX + border;
				box.x1 += width / 2 - aW / 2;
				box.y1 = s->y * (height + border) + topLeftY + border;
				box.y1 += height / 2 - aH / 2;
			}

			box.x2 = box.x1 + aW;
			box.y2 = box.y1 + aH;

			glTranslatef (box.x1 + aW / 2, box.y1 + aH / 2, 0.0f);
			glRotatef (ws->direction, 0.0f, 0.0f, 1.0f);
			glTranslatef (-box.x1 - aW / 2, -box.y1 - aH / 2, 0.0f);

			matrix = ws->arrowContext.texture.matrix;
			matrix.x0 -= box.x1 * matrix.xx;
			matrix.y0 -= box.y1 * matrix.yy;

			glBegin (GL_QUADS);
			wallDrawQuad (&matrix, &box);
			glEnd ();

			disableTexture (s, &ws->arrowContext.texture);
		}
	}

	glDisable (GL_BLEND);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);
	screenTexEnvMode (s, GL_REPLACE);
	glColor4usv (defaultColor);
}

static void
wallPaintScreen (CompScreen   *s,
                 CompOutput   *outputs,
                 int          numOutputs,
                 unsigned int mask)
{
	WALL_SCREEN (s);

	const BananaValue *
	option_mmmode = bananaGetOption (bananaIndex,
	                                 "mmmode",
	                                 s->screenNum);

	if (ws->moving && numOutputs > 1 && option_mmmode == MMMODE_SWITCH_ALL)
	{
		outputs = &s->fullscreenOutput;
		numOutputs = 1;
	}

	UNWRAP (ws, s, paintScreen);
	(*s->paintScreen) (s, outputs, numOutputs, mask);
	WRAP (ws, s, paintScreen, wallPaintScreen);
}

static Bool
wallPaintOutput (CompScreen              *s,
                 const ScreenPaintAttrib *sAttrib,
                 const CompTransform     *transform,
                 Region                  region,
                 CompOutput              *output,
                 unsigned int            mask)
{
	Bool status;

	WALL_SCREEN (s);

	ws->transform = NoTransformation;
	if (ws->moving)
		mask |= PAINT_SCREEN_TRANSFORMED_MASK |
		        PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;

	UNWRAP (ws, s, paintOutput);
	status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
	WRAP (ws, s, paintOutput, wallPaintOutput);

	const BananaValue *
	option_show_switcher = bananaGetOption (bananaIndex,
	                                        "show_switcher",
	                                        -1);

	if (option_show_switcher->b &&
	      (ws->moving || ws->showPreview || ws->boxTimeout) &&
	      (output->id == ws->boxOutputDevice || output == &s->fullscreenOutput))
	{
		CompTransform sTransform = *transform;

		transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

		glPushMatrix ();
		glLoadMatrixf (sTransform.m);

		wallDrawCairoTextureOnScreen (s);

		glPopMatrix ();

		const BananaValue *
		option_miniscreen = bananaGetOption (bananaIndex,
		                                     "miniscreen",
		                                     -1);

		if (option_miniscreen->b)
		{
			int  i, j;
			float mw, mh;

			mw = ws->viewportWidth;
			mh = ws->viewportHeight;

			ws->transform = MiniScreen;
			ws->mSAttribs.xScale = mw / s->width;
			ws->mSAttribs.yScale = mh / s->height;
			ws->mSAttribs.opacity = OPAQUE * (1.0 + ws->mSzCamera);
			ws->mSAttribs.saturation = COLOR;

			for (j = 0; j < s->vsize; j++)
			{
				for (i = 0; i < s->hsize; i++)
				{
					float        mx, my;
					unsigned int msMask;

					mx = ws->firstViewportX +
					      (i * (ws->viewportWidth + ws->viewportBorder));
					my = ws->firstViewportY + 
					      (j * (ws->viewportHeight + ws->viewportBorder));

					ws->mSAttribs.xTranslate = mx / output->width;
					ws->mSAttribs.yTranslate = -my / output->height;

					ws->mSAttribs.brightness = 0.4f * BRIGHT;

					if (i == s->x && j == s->y && ws->moving)
						ws->mSAttribs.brightness = BRIGHT;

					if ((ws->boxTimeout || ws->showPreview) &&
					       !ws->moving && i == s->x && j == s->y)
					{
						ws->mSAttribs.brightness = BRIGHT;
					}

					setWindowPaintOffset (s, (s->x - i) * s->width,
					                         (s->y - j) * s->height);

					msMask = mask | PAINT_SCREEN_TRANSFORMED_MASK;
					(*s->paintTransformedOutput) (s, sAttrib, transform,
					                              region, output, msMask);


				}
			}
			ws->transform = NoTransformation;
			setWindowPaintOffset (s, 0, 0);
		}
	}

	return status;
}

static void
wallPreparePaintScreen (CompScreen *s,
                        int        msSinceLastPaint)
{
	WALL_SCREEN (s);

	if (!ws->moving && !ws->showPreview && ws->boxTimeout)
		ws->boxTimeout -= msSinceLastPaint;

	if (ws->timer)
		ws->timer -= msSinceLastPaint;

	if (ws->moving)
	{
		wallComputeTranslation (s, &ws->curPosX, &ws->curPosY);

		if (ws->moveWindow)
		{
			CompWindow *w;

			w = findWindowAtScreen (s, ws->moveWindow);
			if (w)
			{
				float dx, dy;

				dx = ws->gotoX - ws->curPosX;
				dy = ws->gotoY - ws->curPosY;

				moveWindowToViewportPosition (w,
				      ws->moveWindowX - s->width * dx,
				      ws->moveWindowY - s->height * dy,
				      TRUE);
			}
		}
	}

	if (ws->moving && ws->curPosX == ws->gotoX && ws->curPosY == ws->gotoY)
	{
		ws->moving = FALSE;
		ws->timer  = 0;

		if (ws->moveWindow)
			wallReleaseMoveWindow (s);
		else if (ws->focusDefault)
		{
			int i;
			for (i = 0; i < s->maxGrab; i++)
			if (s->grabs[i].active)
				if (strcmp(s->grabs[i].name, "switcher") == 0)
					break;

			/* only focus default window if switcher is not active */
			if (i == s->maxGrab)
				focusDefaultWindow (s);
		}
	}

	UNWRAP (ws, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, msSinceLastPaint);
	WRAP (ws, s, preparePaintScreen, wallPreparePaintScreen);
}

static void
wallPaintTransformedOutput (CompScreen              *s,
                            const ScreenPaintAttrib *sAttrib,
                            const CompTransform     *transform,
                            Region                  region,
                            CompOutput              *output,
                            unsigned int            mask)
{
	WALL_SCREEN (s);
	Bool clear = (mask & PAINT_SCREEN_CLEAR_MASK);

	if (ws->transform == MiniScreen)
	{
		CompTransform sTransform = *transform;

		mask &= ~PAINT_SCREEN_CLEAR_MASK;

		/* move each screen to the correct output position */

		matrixTranslate (&sTransform,
		         -(float) output->region.extents.x1 /
		          (float) output->width,
		          (float) output->region.extents.y1 /
		          (float) output->height, 0.0f);
		matrixTranslate (&sTransform, 0.0f, 0.0f, -DEFAULT_Z_CAMERA);

		matrixTranslate (&sTransform,
		                 ws->mSAttribs.xTranslate,
		                 ws->mSAttribs.yTranslate,
		                 ws->mSzCamera);

		/* move origin to top left */
		matrixTranslate (&sTransform, -0.5f, 0.5f, 0.0f);
		matrixScale (&sTransform,
		     ws->mSAttribs.xScale, ws->mSAttribs.yScale, 1.0);

		/* revert prepareXCoords region shift.
		   Now all screens display the same */
		matrixTranslate (&sTransform, 0.5f, 0.5f, DEFAULT_Z_CAMERA);
		matrixTranslate (&sTransform,
		         (float) output->region.extents.x1 /
		         (float) output->width,
		        -(float) output->region.extents.y2 /
		         (float) output->height, 0.0f);

		UNWRAP (ws, s, paintTransformedOutput);
		(*s->paintTransformedOutput) (s, sAttrib, &sTransform,
		                      &s->region, output, mask);
		WRAP (ws, s, paintTransformedOutput, wallPaintTransformedOutput);
		return;
	}

	UNWRAP (ws, s, paintTransformedOutput);

	if (!ws->moving)
		(*s->paintTransformedOutput) (s, sAttrib, transform,
		                              region, output, mask);

	mask &= ~PAINT_SCREEN_CLEAR_MASK;

	if (ws->moving)
	{
		ScreenTransformation oldTransform = ws->transform;
		CompTransform        sTransform = *transform;
		float                xTranslate, yTranslate;
		float                px, py;
		//int                  tx, ty; //TODO: set but not used
		Bool                 movingX, movingY;

		if (clear)
			clearTargetOutput (GL_COLOR_BUFFER_BIT);

		ws->transform  = Sliding;
		ws->currOutput = output;

		px = ws->curPosX;
		py = ws->curPosY;

		movingX = ((int) floor (px)) != ((int) ceil (px));
		movingY = ((int) floor (py)) != ((int) ceil (py));

		if (movingY)
		{
			//ty = ceil (py) - s->y;
			yTranslate = fmod (py, 1) - 1;

			matrixTranslate (&sTransform, 0.0f, yTranslate, 0.0f);

			if (movingX)
			{
				//tx = ceil (px) - s->x;
				xTranslate = 1 - fmod (px, 1);

				setWindowPaintOffset (s, (s->x - ceil(px)) * s->width,
				                      (s->y - ceil(py)) * s->height);

				matrixTranslate (&sTransform, xTranslate, 0.0f, 0.0f);

				(*s->paintTransformedOutput) (s, sAttrib, &sTransform,
				                              &output->region, output, mask);

				matrixTranslate (&sTransform, -xTranslate, 0.0f, 0.0f);
			}

			//tx = floor (px) - s->x;
			xTranslate = -fmod (px, 1);

			setWindowPaintOffset (s, (s->x - floor(px)) * s->width,
			                          (s->y - ceil(py)) * s->height);

			matrixTranslate (&sTransform, xTranslate, 0.0f, 0.0f);

			(*s->paintTransformedOutput) (s, sAttrib, &sTransform,
			                              &output->region, output, mask);
			matrixTranslate (&sTransform, -xTranslate, -yTranslate, 0.0f);
		}

		//ty = floor (py) - s->y;
		yTranslate = fmod (py, 1);

		matrixTranslate (&sTransform, 0.0f, yTranslate, 0.0f);

		if (movingX)
		{
			//tx = ceil (px) - s->x;
			xTranslate = 1 - fmod (px, 1);

			setWindowPaintOffset (s, (s->x - ceil(px)) * s->width,
			                         (s->y - floor(py)) * s->height);

			matrixTranslate (&sTransform, xTranslate, 0.0f, 0.0f);

			(*s->paintTransformedOutput) (s, sAttrib, &sTransform,
			                              &output->region, output, mask);

			matrixTranslate (&sTransform, -xTranslate, 0.0f, 0.0f);
		}

		//tx = floor (px) - s->x;
		xTranslate = -fmod (px, 1);

		setWindowPaintOffset (s, (s->x - floor(px)) * s->width,
		                      (s->y - floor(py)) * s->height);

		matrixTranslate (&sTransform, xTranslate, 0.0f, 0.0f);
		(*s->paintTransformedOutput) (s, sAttrib, &sTransform,
		                              &output->region, output, mask);

		setWindowPaintOffset (s, 0, 0);
		ws->transform = oldTransform;
	}

	WRAP (ws, s, paintTransformedOutput, wallPaintTransformedOutput);
}

static Bool
wallPaintWindow (CompWindow              *w,
                 const WindowPaintAttrib *attrib,
                 const CompTransform     *transform,
                 Region                  region,
                 unsigned int            mask)
{
	Bool       status;
	CompScreen *s = w->screen;

	WALL_SCREEN (s);

	if (ws->transform == MiniScreen)
	{
		WindowPaintAttrib pA = *attrib;

		pA.opacity    = attrib->opacity *
		               ((float) ws->mSAttribs.opacity / OPAQUE);
		pA.brightness = attrib->brightness *
		               ((float) ws->mSAttribs.brightness / BRIGHT);
		pA.saturation = attrib->saturation *
		               ((float) ws->mSAttribs.saturation / COLOR);

		if (!pA.opacity || !pA.brightness)
			mask |= PAINT_WINDOW_NO_CORE_INSTANCE_MASK;

		UNWRAP (ws, s, paintWindow);
		status = (*s->paintWindow) (w, &pA, transform, region, mask);
		WRAP (ws, s, paintWindow, wallPaintWindow);
	}
	else if (ws->transform == Sliding)
	{
		CompTransform wTransform;

		WALL_WINDOW (w);

		if (!ww->isSliding)
		{
			matrixGetIdentity (&wTransform);
			transformToScreenSpace (s, ws->currOutput, -DEFAULT_Z_CAMERA,
			                        &wTransform);
			mask |= PAINT_WINDOW_TRANSFORMED_MASK;
		}
		else
		{
			wTransform = *transform;
		}

		UNWRAP (ws, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, &wTransform, region, mask);
		WRAP (ws, s, paintWindow, wallPaintWindow);
	}
	else
	{
		UNWRAP (ws, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP (ws, s, paintWindow, wallPaintWindow);
	}

	return status;
}

static void
wallDonePaintScreen (CompScreen *s)
{
	WALL_SCREEN (s);

	if (ws->moving || ws->showPreview || ws->boxTimeout)
	{
		ws->boxTimeout = MAX (0, ws->boxTimeout);
		damageScreen (s);
	}

	if (!ws->moving && !ws->showPreview && ws->grabIndex)
	{
		removeScreenGrab (s, ws->grabIndex, NULL);
		ws->grabIndex = 0;
	}

	UNWRAP (ws, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP (ws, s, donePaintScreen, wallDonePaintScreen);
}

static void
wallCreateCairoContexts (CompScreen *s,
                         Bool       initial)
{
	int width, height;

	WALL_SCREEN (s);

	const BananaValue *
	option_preview_scale = bananaGetOption (bananaIndex,
	                                        "preview_scale",
	                                        -1);

	const BananaValue *
	option_border_width = bananaGetOption (bananaIndex,
	                                       "border_width",
	                                       -1);

	ws->viewportWidth = VIEWPORT_SWITCHER_SIZE *
	                    (float) option_preview_scale->i / 100.0f;
	ws->viewportHeight = ws->viewportWidth *
	                    (float) s->height / (float) s->width;
	ws->viewportBorder = option_border_width->i;

	width  = s->hsize * (ws->viewportWidth + ws->viewportBorder) +
	                     ws->viewportBorder;
	height = s->vsize * (ws->viewportHeight + ws->viewportBorder) +
	                     ws->viewportBorder;

	wallDestroyCairoContext (s, &ws->switcherContext);
	ws->switcherContext.width = width;
	ws->switcherContext.height = height;
	wallSetupCairoContext (s, &ws->switcherContext);
	wallDrawSwitcherBackground (s);

	wallDestroyCairoContext (s, &ws->thumbContext);
	ws->thumbContext.width = ws->viewportWidth;
	ws->thumbContext.height = ws->viewportHeight;
	wallSetupCairoContext (s, &ws->thumbContext);
	wallDrawThumb (s);

	wallDestroyCairoContext (s, &ws->highlightContext);
	ws->highlightContext.width = ws->viewportWidth;
	ws->highlightContext.height = ws->viewportHeight;
	wallSetupCairoContext (s, &ws->highlightContext);
	wallDrawHighlight (s);

	if (initial)
	{
		ws->arrowContext.width = ARROW_SIZE;
		ws->arrowContext.height = ARROW_SIZE;
		wallSetupCairoContext (s, &ws->arrowContext);
		wallDrawArrow (s);
	}
}

static void
wallMatchPropertyChanged (CompWindow  *w)
{
	WALL_DISPLAY (&display);
	WALL_WINDOW (w);

	UNWRAP (wd, &display, matchPropertyChanged);
	(*display.matchPropertyChanged) (w);
	WRAP (wd, &display, matchPropertyChanged, wallMatchPropertyChanged);

	ww->isSliding = !matchEval (&wd->no_slide_match, w);
}

static void
wallWindowGrabNotify (CompWindow   *w,
                      int          x,
                      int          y,
                      unsigned int state,
                      unsigned int mask)
{
	WALL_SCREEN (w->screen);

	if (!ws->grabWindow)
		ws->grabWindow = w;

	UNWRAP (ws, w->screen, windowGrabNotify);
	(*w->screen->windowGrabNotify) (w, x, y, state, mask);
	WRAP (ws, w->screen, windowGrabNotify, wallWindowGrabNotify);
}

static void
wallWindowUngrabNotify (CompWindow *w)
{
	WALL_SCREEN (w->screen);

	if (w == ws->grabWindow)
		ws->grabWindow = NULL;

	UNWRAP (ws, w->screen, windowUngrabNotify);
	(*w->screen->windowUngrabNotify) (w);
	WRAP (ws, w->screen, windowUngrabNotify, wallWindowUngrabNotify);
}

static void
wallWindowAdd (CompScreen *s,
               CompWindow *w)
{
	WALL_DISPLAY (&display);
	WALL_WINDOW (w);

	ww->isSliding = !matchEval (&wd->no_slide_match, w);
}

static void
wallWindowAddNotify (CompWindow *w)
{
	WALL_SCREEN (w->screen);

	wallWindowAdd (w->screen, w);

	UNWRAP (ws, w->screen, windowAddNotify);
	(*w->screen->windowAddNotify) (w);
	WRAP (ws, w->screen, windowAddNotify, wallWindowAddNotify);
}

static void
wallChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	WALL_DISPLAY (&display);
	CompScreen *s;

	if (strcasecmp (optionName, "no_slide_match") == 0)
	{
		matchFini (&wd->no_slide_match);
		matchInit (&wd->no_slide_match);
		matchAddFromString (&wd->no_slide_match, optionValue->s);
		matchUpdate (&wd->no_slide_match);

		for (s = display.screens; s; s = s->next)
		{
			CompWindow *w;

			for (w = s->windows; w; w = w->next)
			{
				WALL_WINDOW (w);
				ww->isSliding = !matchEval (&wd->no_slide_match, w);
			}
		}
	}
	else if (strcasecmp (optionName, "outline_color") == 0)
	{
		for (s = display.screens; s; s = s->next)
		{
			wallDrawSwitcherBackground (s);
			wallDrawHighlight (s);
			wallDrawThumb (s);
		}
	}
	else if (strcasecmp (optionName, "edge_radius") == 0 ||
	         strcasecmp (optionName, "background_gradient_base_color") == 0 ||
	         strcasecmp (optionName, "background_gradient_highlight_color") == 0 ||
	         strcasecmp (optionName, "background_gradient_shadow_color") == 0)
	{
		for (s = display.screens; s; s = s->next)
			wallDrawSwitcherBackground (s);
	}
	else if (strcasecmp (optionName, "border_width") == 0 ||
	         strcasecmp (optionName, "preview_scale") == 0)
	{
		for (s = display.screens; s; s = s->next)
			wallCreateCairoContexts (s, FALSE);
	}
	else if (strcasecmp (optionName, "thumb_gradient_base_color") == 0 ||
	         strcasecmp (optionName, "thumb_gradient_highlight_color") == 0)
	{
		for (s = display.screens; s; s = s->next)
			wallDrawThumb (s);
	}
	else if (strcasecmp (optionName, "thumb_highlight_gradient_base_color") == 0 ||
	         strcasecmp (optionName, "thumb_highlight_gradient_shadow_color") == 0)
	{
		for (s = display.screens; s; s = s->next)
			wallDrawHighlight (s);
	}
	else if (strcasecmp (optionName, "arrow_base_color") == 0 ||
	         strcasecmp (optionName, "arrow_shadow_color") == 0)
	{
		for (s = display.screens; s; s = s->next)
			wallDrawArrow (s);
	}
	else if (strcasecmp (optionName, "left_key") == 0)
	{
		updateKey (optionValue->s, &wd->left_key);
	}
	else if (strcasecmp (optionName, "right_key") == 0)
	{
		updateKey (optionValue->s, &wd->right_key);
	}
	else if (strcasecmp (optionName, "up_key") == 0)
	{
		updateKey (optionValue->s, &wd->up_key);
	}
	else if (strcasecmp (optionName, "down_key") == 0)
	{
		updateKey (optionValue->s, &wd->down_key);
	}
	else if (strcasecmp (optionName, "prev_key") == 0)
	{
		updateKey (optionValue->s, &wd->prev_key);
	}
	else if (strcasecmp (optionName, "next_key") == 0)
	{
		updateKey (optionValue->s, &wd->next_key);
	}
	else if (strcasecmp (optionName, "left_button") == 0)
	{
		updateButton (optionValue->s, &wd->left_button);
	}
	else if (strcasecmp (optionName, "right_button") == 0)
	{
		updateButton (optionValue->s, &wd->right_button);
	}
	else if (strcasecmp (optionName, "up_button") == 0)
	{
		updateButton (optionValue->s, &wd->up_button);
	}
	else if (strcasecmp (optionName, "down_button") == 0)
	{
		updateButton (optionValue->s, &wd->down_button);
	}
	else if (strcasecmp (optionName, "prev_button") == 0)
	{
		updateButton (optionValue->s, &wd->prev_button);
	}
	else if (strcasecmp (optionName, "next_button") == 0)
	{
		updateButton (optionValue->s, &wd->next_button);
	}
	else if (strcasecmp (optionName, "left_window_key") == 0)
	{
		updateKey (optionValue->s, &wd->left_window_key);
	}
	else if (strcasecmp (optionName, "right_window_key") == 0)
	{
		updateKey (optionValue->s, &wd->right_window_key);
	}
	else if (strcasecmp (optionName, "up_window_key") == 0)
	{
		updateKey (optionValue->s, &wd->up_window_key);
	}
	else if (strcasecmp (optionName, "down_window_key") == 0)
	{
		updateKey (optionValue->s, &wd->down_window_key);
	}
}

static void
coreChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	if (strcasecmp (optionName, "hsize") == 0 ||
	    strcasecmp (optionName, "vsize") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);

		wallCreateCairoContexts (s, FALSE);
	}
}

static Bool
wallInitDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	WallDisplay *wd;

	wd = malloc (sizeof (WallDisplay));
	if (!wd)
		return FALSE;

	wd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (wd->screenPrivateIndex < 0)
	{
		free (wd);
		return FALSE;
	}

	WRAP (wd, d, handleEvent, wallHandleEvent);
	WRAP (wd, d, matchPropertyChanged, wallMatchPropertyChanged);

	const BananaValue *
	option_no_slide_match = bananaGetOption (bananaIndex,
	                                         "no_slide_match",
	                                         -1);

	matchInit (&wd->no_slide_match);
	matchAddFromString (&wd->no_slide_match, option_no_slide_match->s);
	matchUpdate (&wd->no_slide_match);

	//bindings
	//left
	const BananaValue *
	option_left_key = bananaGetOption (
	       bananaIndex, "left_key", -1);

	registerKey (option_left_key->s, &wd->left_key);

	const BananaValue *
	option_left_button = bananaGetOption (
	       bananaIndex, "left_button", -1);

	registerButton (option_left_button->s, &wd->left_button);

	//right
	const BananaValue *
	option_right_key = bananaGetOption (
	       bananaIndex, "right_key", -1);

	registerKey (option_right_key->s, &wd->right_key);

	const BananaValue *
	option_right_button = bananaGetOption (
	       bananaIndex, "right_button", -1);

	registerButton (option_right_button->s, &wd->right_button);

	//up
	const BananaValue *
	option_up_key = bananaGetOption (
	       bananaIndex, "up_key", -1);

	registerKey (option_up_key->s, &wd->up_key);

	const BananaValue *
	option_up_button = bananaGetOption (
	       bananaIndex, "up_button", -1);

	registerButton (option_up_button->s, &wd->up_button);

	//down
	const BananaValue *
	option_down_key = bananaGetOption (
	       bananaIndex, "down_key", -1);

	registerKey (option_down_key->s, &wd->down_key);

	const BananaValue *
	option_down_button = bananaGetOption (
	       bananaIndex, "down_button", -1);

	registerButton (option_down_button->s, &wd->down_button);

	//prev
	const BananaValue *
	option_prev_key = bananaGetOption (
	       bananaIndex, "prev_key", -1);

	registerKey (option_prev_key->s, &wd->prev_key);

	const BananaValue *
	option_prev_button = bananaGetOption (
	       bananaIndex, "prev_button", -1);

	registerButton (option_prev_button->s, &wd->prev_button);

	//next
	const BananaValue *
	option_next_key = bananaGetOption (
	       bananaIndex, "next_key", -1);

	registerKey (option_next_key->s, &wd->next_key);

	const BananaValue *
	option_next_button = bananaGetOption (
	       bananaIndex, "next_button", -1);

	registerButton (option_next_button->s, &wd->next_button);

	//bindings with windows
	const BananaValue *
	option_left_window_key = bananaGetOption (
	       bananaIndex, "left_window_key", -1);

	registerKey (option_left_window_key->s, &wd->left_window_key);

	const BananaValue *
	option_right_window_key = bananaGetOption (
	       bananaIndex, "right_window_key", -1);

	registerKey (option_right_window_key->s, &wd->right_window_key);

	const BananaValue *
	option_up_window_key = bananaGetOption (
	       bananaIndex, "up_window_key", -1);

	registerKey (option_up_window_key->s, &wd->up_window_key);

	const BananaValue *
	option_down_window_key = bananaGetOption (
	       bananaIndex, "down_window_key", -1);

	registerKey (option_down_window_key->s, &wd->down_window_key);

	d->privates[displayPrivateIndex].ptr = wd;

	return TRUE;
}

static void
wallFiniDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	WALL_DISPLAY (d);

	matchFini (&wd->no_slide_match);

	UNWRAP (wd, d, handleEvent);
	UNWRAP (wd, d, matchPropertyChanged);

	freeScreenPrivateIndex (wd->screenPrivateIndex);
	free (wd);
}

static Bool
wallInitScreen (CompPlugin *p,
                CompScreen *s)
{
	WallScreen *ws;

	WALL_DISPLAY (&display);

	ws = malloc (sizeof (WallScreen));
	if (!ws)
		return FALSE;

	ws->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (ws->windowPrivateIndex < 0)
	{
		free (ws);
		return FALSE;
	}

	ws->timer      = 0;
	ws->boxTimeout = 0;
	ws->grabIndex  = 0;

	ws->moving       = FALSE;
	ws->showPreview  = FALSE;
	ws->focusDefault = TRUE;
	ws->moveWindow   = None;
	ws->grabWindow   = NULL;

	ws->transform  = NoTransformation;
	ws->direction  = -1;

	memset (&ws->switcherContext, 0, sizeof (WallCairoContext));
	memset (&ws->thumbContext, 0, sizeof (WallCairoContext));
	memset (&ws->highlightContext, 0, sizeof (WallCairoContext));
	memset (&ws->arrowContext, 0, sizeof (WallCairoContext));

	WRAP (ws, s, paintScreen, wallPaintScreen);
	WRAP (ws, s, paintOutput, wallPaintOutput);
	WRAP (ws, s, donePaintScreen, wallDonePaintScreen);
	WRAP (ws, s, paintTransformedOutput, wallPaintTransformedOutput);
	WRAP (ws, s, preparePaintScreen, wallPreparePaintScreen);
	WRAP (ws, s, paintWindow, wallPaintWindow);
	WRAP (ws, s, windowAddNotify, wallWindowAddNotify);
	WRAP (ws, s, windowGrabNotify, wallWindowGrabNotify);
	WRAP (ws, s, windowUngrabNotify, wallWindowUngrabNotify);
	WRAP (ws, s, activateWindow, wallActivateWindow);

	s->privates[wd->screenPrivateIndex].ptr = ws;

	wallCreateCairoContexts (s, TRUE);

	return TRUE;
}

static void
wallFiniScreen (CompPlugin *p,
                CompScreen *s)
{
	WALL_SCREEN (s);

	if (ws->grabIndex)
		removeScreenGrab (s, ws->grabIndex, NULL);

	wallDestroyCairoContext (s, &ws->switcherContext);
	wallDestroyCairoContext (s, &ws->thumbContext);
	wallDestroyCairoContext (s, &ws->highlightContext);
	wallDestroyCairoContext (s, &ws->arrowContext);

	UNWRAP (ws, s, paintScreen);
	UNWRAP (ws, s, paintOutput);
	UNWRAP (ws, s, donePaintScreen);
	UNWRAP (ws, s, paintTransformedOutput);
	UNWRAP (ws, s, preparePaintScreen);
	UNWRAP (ws, s, paintWindow);
	UNWRAP (ws, s, windowAddNotify);
	UNWRAP (ws, s, windowGrabNotify);
	UNWRAP (ws, s, windowUngrabNotify);
	UNWRAP (ws, s, activateWindow);

	freeWindowPrivateIndex (s, ws->windowPrivateIndex);

	free (ws);
}

static CompBool
wallInitWindow (CompPlugin *p,
                CompWindow *w)
{
	WallWindow *ww;

	WALL_SCREEN (w->screen);

	ww = malloc (sizeof (WallWindow));
	if (!ww)
		return FALSE;

	ww->isSliding = TRUE;

	w->privates[ws->windowPrivateIndex].ptr = ww;

	if (w->added)
		wallWindowAdd (w->screen, w);

	return TRUE;
}

static void
wallFiniWindow (CompPlugin *p,
                CompWindow *w)
{
	WALL_WINDOW (w);

	free (ww);
}

static Bool
wallInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("wall", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("wall");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, wallChangeNotify);

	bananaAddChangeNotifyCallBack (coreBananaIndex, coreChangeNotify);

	return TRUE;
}

static void
wallFini (CompPlugin *p)
{
	bananaRemoveChangeNotifyCallBack (coreBananaIndex, coreChangeNotify);

	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

CompPluginVTable wallVTable = {
	"wall",
	wallInit,
	wallFini,
	wallInitDisplay,
	wallFiniDisplay,
	wallInitScreen,
	wallFiniScreen,
	wallInitWindow,
	wallFiniWindow
};

CompPluginVTable*
getCompPluginInfo20141205 (void)
{
	return &wallVTable;
}
