/*
 * Copyright © 2005 Novell, Inc.
 * Copyright (C) 2007, 2008 Kristian Lyngstøl
 * Copyright (C) 2015 Michail Bitzes
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
 *
 * Author(s):
 *	- Original zoom plug-in; David Reveman <davidr@novell.com>
 *	- Most features beyond basic zoom;
 *	  Kristian Lyngstol <kristian@bohemians.org>
 *    Fusilli port: Michail Bitzes <noodlylight@gmail.com>
 * Description:
 *
 * This plug-in offers zoom functionality with focus tracking,
 * fit-to-window actions, mouse panning, zoom area locking. Without
 * disabling input.
 *
 * Note on actual zoom process
 *
 * The animation is done in preparePaintScreen, while instant movements
 * are done by calling updateActualTranslate () after updating the
 * translations. This causes [xyz]trans to be re-calculated. We keep track
 * of each head separately.
 *
 * Note on input
 *
 * We can not redirect input yet, but this plug-in offers two fundamentally
 * different approaches to achieve input enabled zoom:
 *
 * 1.
 * Always have the zoomed area be in sync with the mouse cursor. This binds
 * the zoom area to the mouse position at any given time. It allows using
 * the original mouse cursor drawn by X, and is technically very safe.
 * First used in Beryl's inputzoom.
 *
 * 2.
 * Hide the real cursor and draw our own where it would be when zoomed in.
 * This allows us to navigate with the mouse without constantly moving the
 * zoom area. This is fairly close to what we want in the end when input
 * redirection is available.
 *
 * This second method has one huge issue, which is bugged XFixes. After
 * hiding the cursor once with XFixes, some mouse cursors will simply be
 * invisible. The Firefox loading cursor being one of them.
 *
 * An other minor annoyance is that mouse sensitivity seems to increase as
 * you zoom in, since the mouse isn't really zoomed at all.
 *
 * Todo:
 *  - Different multi head modes
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>

#include <fusilli-core.h>
#include <fusilli-mousepoll.h>

static int bananaIndex;

static int displayPrivateIndex;

typedef enum {
	NORTHEAST,
	NORTHWEST,
	SOUTHEAST,
	SOUTHWEST,
	CENTER
} ZoomGravity;

typedef enum {
	NORTH,
	SOUTH,
	EAST,
	WEST
} ZoomEdge;

typedef struct _CursorTexture
{
	Bool isSet;
	GLuint texture;
	CompScreen *screen;
	int width;
	int height;
	int hotX;
	int hotY;
} CursorTexture;

typedef struct _ZoomDisplay {
	HandleEventProc handleEvent;

	int screenPrivateIndex;

	Bool fixesSupported;
	int fixesEventBase;
	int fixesErrorBase;
	Bool canHideCursor;

	CompKeyBinding zoom_in_key, zoom_out_key,
	               zoom_specific_1_key,
	               zoom_specific_2_key,
	               zoom_specific_3_key,
	               pan_left_key, pan_right_key, pan_up_key, pan_down_key,
	               fit_to_window_key, center_mouse_key,
	               fit_to_zoom_key, lock_zoom_key;

	CompButtonBinding zoom_in_button, zoom_out_button, zoom_box_button;
} ZoomDisplay;

/* Stores an actual zoom-setup. This can later be used to store/restore
 * zoom areas on the fly.
 *
 * [xy]Translate and newZoom are target values, and [xy]Translate always
 * ranges from -0.5 to 0.5.
 *
 * currentZoom is actual zoomed value
 *
 * real[XY]Translate are the currently used values in the same range as
 * [xy]Translate, and [xy]trans is adjusted for the zoom level in place.
 * [xyz]trans should never be modified except in updateActualTranslates()
 *
 * viewport is a mask of the viewport, or ~0 for "any".
 */
typedef struct _ZoomArea {
	int output;
	unsigned long int viewport;
	GLfloat currentZoom;
	GLfloat newZoom;
	GLfloat xVelocity;
	GLfloat yVelocity;
	GLfloat zVelocity;
	GLfloat xTranslate;
	GLfloat yTranslate;
	GLfloat realXTranslate;
	GLfloat realYTranslate;
	GLfloat xtrans;
	GLfloat ytrans;
	Bool locked;
} ZoomArea;

typedef struct _ZoomScreen {
	PreparePaintScreenProc preparePaintScreen;
	DonePaintScreenProc donePaintScreen;
	PaintOutputProc paintOutput;

	PositionPollingHandle pollHandle;
	ZoomArea               *zooms;
	int nZooms;
	int mouseX;
	int mouseY;
	unsigned long int grabbed;
	int grabIndex;                // for zoomBox
	time_t lastChange;
	CursorTexture cursor;
	Bool cursorInfoSelected;
	Bool cursorHidden;
	Box box;
} ZoomScreen;

static void syncCenterToMouse (CompScreen *s);

static void updateMouseInterval (CompScreen *s,
                                 int        x,
                                 int        y);

static void cursorZoomActive (CompScreen *s);
static void cursorZoomInactive (CompScreen *s);
static void restrainCursor (CompScreen *s,
                            int        out);

static void drawCursor (CompScreen          *s,
                        CompOutput          *output,
                        const CompTransform *transform);

static void convertToZoomedTarget (CompScreen *s,
                                   int        out,
                                   int        x,
                                   int        y,
                                   int        *resultX,
                                   int        *resultY);

static void
convertToZoomed (CompScreen *s,
                 int        out,
                 int        x,
                 int        y,
                 int        *resultX,
                 int        *resultY);

#define GET_ZOOM_DISPLAY(d)                                   \
	((ZoomDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define ZOOM_DISPLAY(d)                    \
	ZoomDisplay *zd = GET_ZOOM_DISPLAY (d)

#define GET_ZOOM_SCREEN(s, zd)                                   \
	((ZoomScreen *) (s)->privates[(zd)->screenPrivateIndex].ptr)

#define ZOOM_SCREEN(s)                                                  \
	ZoomScreen *zs = GET_ZOOM_SCREEN (s, GET_ZOOM_DISPLAY (&display))

/* Checks if a specific screen grab exist. DO NOT USE THIS.
 * This is a temporary fix that SHOULD be removed asap.
 * See comments in drawCursor.
 */

static inline Bool
dontuseScreengrabExist (CompScreen * s, char * grab)
{
	int i;

	for (i = 0; i < s->maxGrab; i++)
		if (s->grabs[i].active && !strcmp (s->grabs[i].name, grab))
			return TRUE;

	return FALSE;
}

/* Check if the output is valid */
static inline Bool
outputIsZoomArea (CompScreen *s,
                  int        out)
{
	ZOOM_SCREEN (s);

	if (out < 0 || out >= zs->nZooms)
		return FALSE;

	return TRUE;
}

/* Check if zoom is active on the output specified */
static inline Bool
isActive (CompScreen *s, int out)
{
	ZOOM_SCREEN (s);

	if (!outputIsZoomArea (s, out))
		return FALSE;

	if (zs->grabbed & (1 << zs->zooms[out].output))
		return TRUE;

	return FALSE;
}

/* Check if we are zoomed out and not going anywhere
 * (similar to isActive but based on actual zoom, not grab)
 */
static inline Bool
isZoomed (CompScreen *s,
          int        out)
{
	ZOOM_SCREEN (s);

	if (!outputIsZoomArea (s, out))
		return FALSE;

	if (zs->zooms[out].currentZoom != 1.0f || zs->zooms[out].newZoom != 1.0f)
		return TRUE;

	if (zs->zooms[out].zVelocity != 0.0f)
		return TRUE;

	return FALSE;
}

/* Returns the distance to the defined edge in zoomed pixels.  */
static int
distanceToEdge (CompScreen *s,
                int        out,
                ZoomEdge   edge)
{
	int x1, y1, x2, y2;
	CompOutput *o = &s->outputDev[out];

	if (!isActive (s, out))
		return 0;

	convertToZoomedTarget (s, out, o->region.extents.x2,
	                       o->region.extents.y2, &x2, &y2);
	convertToZoomedTarget (s, out, o->region.extents.x1,
	                       o->region.extents.y1, &x1, &y1);

	switch (edge) {
	case NORTH: 
		return o->region.extents.y1 - y1;
	case SOUTH: 
		return y2 - o->region.extents.y2;
	case EAST: 
		return x2 - o->region.extents.x2;
	case WEST: 
		return o->region.extents.x1 - x1;
	}

	return 0; // Never reached.
}

/* Update/set translations based on zoom level and real translate.  */
static void
updateActualTranslates (ZoomArea *za)
{
	za->xtrans = -za->realXTranslate * (1.0f - za->currentZoom);
	za->ytrans = za->realYTranslate * (1.0f - za->currentZoom);
}

/* Returns true if the head in question is currently moving.
 * Since we don't always bother resetting everything when
 * canceling zoom, we check for the condition of being completely
 * zoomed out and not zooming in/out first.
 */
static Bool
isInMovement (CompScreen *s,
              int        out)
{
	ZOOM_SCREEN (s);

	if (zs->zooms[out].currentZoom == 1.0f &&
	    zs->zooms[out].newZoom == 1.0f &&
	    zs->zooms[out].zVelocity == 0.0f)
		return FALSE;

	if (zs->zooms[out].currentZoom != zs->zooms[out].newZoom ||
	    zs->zooms[out].xVelocity || zs->zooms[out].yVelocity ||
	    zs->zooms[out].zVelocity)
		return TRUE;

	if (zs->zooms[out].xTranslate != zs->zooms[out].realXTranslate ||
	    zs->zooms[out].yTranslate != zs->zooms[out].realYTranslate)
		return TRUE;

	return FALSE;
}

/* Set the initial values of a zoom area.  */
static void
initialiseZoomArea (ZoomArea *za,
                    int      out)
{
	za->output = out;
	za->currentZoom = 1.0f;
	za->newZoom = 1.0f;
	za->xVelocity = 0.0f;
	za->yVelocity = 0.0f;
	za->zVelocity = 0.0f;
	za->xTranslate = 0.0f;
	za->yTranslate = 0.0f;
	za->realXTranslate = 0.0f;
	za->realYTranslate = 0.0f;
	za->viewport = ~0;
	za->locked = FALSE;
	updateActualTranslates (za);
}

/* Adjust the velocity in the z-direction.  */
static void
adjustZoomVelocity (CompScreen *s,
                    int        out,
                    float      chunk)
{
	float d, adjust, amount;
	ZOOM_SCREEN (s);

	d = (zs->zooms[out].newZoom - zs->zooms[out].currentZoom) * 75.0f;

	adjust = d * 0.002f;
	amount = fabs (d);
	if (amount < 1.0f)
		amount = 1.0f;
	else if (amount > 5.0f)
		amount = 5.0f;

	zs->zooms[out].zVelocity =
	        (amount * zs->zooms[out].zVelocity + adjust) / (amount + 1.0f);

	if (fabs (d) < 0.1f && fabs (zs->zooms[out].zVelocity) < 0.005f)
	{
		zs->zooms[out].currentZoom = zs->zooms[out].newZoom;
		zs->zooms[out].zVelocity = 0.0f;
	}
	else
	{
		zs->zooms[out].currentZoom += (zs->zooms[out].zVelocity * chunk) /
		                              s->redrawTime;
	}
}

/* Adjust the X/Y velocity based on target translation and real
 * translation. */
static void
adjustXYVelocity (CompScreen *s,
                  int        out,
                  float      chunk)
{
	float xdiff, ydiff;
	float xadjust, yadjust;
	float xamount, yamount;
	ZOOM_SCREEN (s);

	zs->zooms[out].xVelocity /= 1.25f;
	zs->zooms[out].yVelocity /= 1.25f;
	xdiff =
	        (zs->zooms[out].xTranslate - zs->zooms[out].realXTranslate) *
	        75.0f;

	ydiff =
	        (zs->zooms[out].yTranslate - zs->zooms[out].realYTranslate) *
	        75.0f;

	xadjust = xdiff * 0.002f;
	yadjust = ydiff * 0.002f;
	xamount = fabs (xdiff);
	yamount = fabs (ydiff);

	if (xamount < 1.0f)
		xamount = 1.0f;
	else if (xamount > 5.0)
		xamount = 5.0f;

	if (yamount < 1.0f)
		yamount = 1.0f;
	else if (yamount > 5.0)
		yamount = 5.0f;

	zs->zooms[out].xVelocity =
	        (xamount * zs->zooms[out].xVelocity + xadjust) / (xamount + 1.0f);

	zs->zooms[out].yVelocity =
	        (yamount * zs->zooms[out].yVelocity + yadjust) / (yamount + 1.0f);

	if ((fabs (xdiff) < 0.1f && fabs (zs->zooms[out].xVelocity) < 0.005f) &&
	    (fabs (ydiff) < 0.1f && fabs (zs->zooms[out].yVelocity) < 0.005f))
	{
		zs->zooms[out].realXTranslate = zs->zooms[out].xTranslate;
		zs->zooms[out].realYTranslate = zs->zooms[out].yTranslate;
		zs->zooms[out].xVelocity = 0.0f;
		zs->zooms[out].yVelocity = 0.0f;

		return;
	}

	zs->zooms[out].realXTranslate +=
	        (zs->zooms[out].xVelocity * chunk) / s->redrawTime;
	zs->zooms[out].realYTranslate +=
	        (zs->zooms[out].yVelocity * chunk) / s->redrawTime;
}

/* Animate the movement (if any) in preparation of a paint screen.  */
static void
zoomPreparePaintScreen (CompScreen *s,
                        int        msSinceLastPaint)
{
	ZOOM_SCREEN (s);

	if (zs->grabbed)
	{
		int steps;
		float amount, chunk;

		const BananaValue *
		option_speed = bananaGetOption (bananaIndex,
		                                "speed",
		                                s->screenNum);

		const BananaValue *
		option_timestep = bananaGetOption (bananaIndex,
		                                   "timestep",
		                                   s->screenNum);

		amount = msSinceLastPaint * 0.05f * option_speed->f;
		steps  = amount / (0.5f * option_timestep->f);

		if (!steps)
			steps = 1;

		chunk  = amount / (float) steps;

		while (steps--)
		{
			int out;

			for (out = 0; out < zs->nZooms; out++)
			{
				if (!isInMovement (s, out) || !isActive (s, out))
					continue;

				adjustXYVelocity (s, out, chunk);
				adjustZoomVelocity (s, out, chunk);
				updateActualTranslates (&zs->zooms[out]);

				if (!isZoomed (s, out))
				{
					zs->zooms[out].xVelocity = zs->zooms[out].yVelocity =
					                                   0.0f;
					zs->grabbed &= ~(1 << zs->zooms[out].output);
				}
			}
		}

		const BananaValue *
		option_sync_mouse = bananaGetOption (bananaIndex,
		                                     "sync_mouse",
		                                     s->screenNum);

		if (option_sync_mouse->b)
			syncCenterToMouse (s);
	}

	UNWRAP (zs, s, preparePaintScreen);
	(*s->preparePaintScreen)(s, msSinceLastPaint);
	WRAP (zs, s, preparePaintScreen, zoomPreparePaintScreen);
}

/* Damage screen if we're still moving.  */
static void
zoomDonePaintScreen (CompScreen *s)
{
	ZOOM_SCREEN (s);

	if (zs->grabbed)
	{
		int out;

		for (out = 0; out < zs->nZooms; out++)
		{
			if (isInMovement (s, out) && isActive (s, out))
			{
				damageScreen (s);
				break;
			}
		}
	}

	UNWRAP (zs, s, donePaintScreen);
	(*s->donePaintScreen)(s);
	WRAP (zs, s, donePaintScreen, zoomDonePaintScreen);
}

/* Draws a box from the screen coordinates inx1,iny1 to inx2,iny2 */
static void
drawBox (CompScreen          *s,
         const CompTransform *transform,
         CompOutput          *output,
         Box                 box)
{
	CompTransform zTransform = *transform;
	int x1, x2, y1, y2;
	int inx1, inx2, iny1, iny2;
	int out = output->id;

	transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &zTransform);
	convertToZoomed (s, out, box.x1, box.y1, &inx1, &iny1);
	convertToZoomed (s, out, box.x2, box.y2, &inx2, &iny2);

	x1 = MIN (inx1, inx2);
	y1 = MIN (iny1, iny2);
	x2 = MAX (inx1, inx2);
	y2 = MAX (iny1, iny2);
	glPushMatrix ();
	glLoadMatrixf (zTransform.m);
	glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	glEnable (GL_BLEND);
	glColor4us (0x2fff, 0x2fff, 0x4fff, 0x4fff);
	glRecti (x1, y2, x2, y1);
	glColor4us (0x2fff, 0x2fff, 0x4fff, 0x9fff);
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

/* Apply the zoom if we are grabbed.
 * Make sure to use the correct filter.
 */
static Bool
zoomPaintOutput (CompScreen              *s,
                 const ScreenPaintAttrib *sAttrib,
                 const CompTransform     *transform,
                 Region                  region,
                 CompOutput              *output,
                 unsigned int            mask)
{
	Bool status;
	int out = output->id;
	ZOOM_SCREEN (s);

	if (isActive (s, out))
	{
		ScreenPaintAttrib sa = *sAttrib;
		int saveFilter;
		CompTransform zTransform = *transform;

		mask &= ~PAINT_SCREEN_REGION_MASK;
		mask |= PAINT_SCREEN_CLEAR_MASK;

		matrixScale (&zTransform,
		             1.0f / zs->zooms[out].currentZoom,
		             1.0f / zs->zooms[out].currentZoom,
		             1.0f);

		matrixTranslate (&zTransform,
		                 zs->zooms[out].xtrans,
		                 zs->zooms[out].ytrans,
		                 0);

		mask |= PAINT_SCREEN_TRANSFORMED_MASK;
		saveFilter = s->filter[SCREEN_TRANS_FILTER];

		const BananaValue *
		option_filter_linear = bananaGetOption (bananaIndex,
		                                        "filter_linear",
		                                        s->screenNum);

		if (option_filter_linear->b)
			s->filter[SCREEN_TRANS_FILTER] = COMP_TEXTURE_FILTER_GOOD;
		else
			s->filter[SCREEN_TRANS_FILTER] = COMP_TEXTURE_FILTER_FAST;

		UNWRAP (zs, s, paintOutput);
		status =
		        (*s->paintOutput)(s, &sa, &zTransform, region, output, mask);
		WRAP (zs, s, paintOutput, zoomPaintOutput);
		drawCursor (s, output, transform);

		s->filter[SCREEN_TRANS_FILTER] = saveFilter;
	}
	else
	{
		UNWRAP (zs, s, paintOutput);
		status = (*s->paintOutput)(s,
		                           sAttrib,
		                           transform,
		                           region,
		                           output,
		                           mask);
		WRAP (zs, s, paintOutput, zoomPaintOutput);
	}

	if (zs->grabIndex)
		drawBox (s, transform, output, zs->box);

	return status;
}

/* Makes sure we're not attempting to translate too far.
 * We are restricted to 0.5 to not go beyond the end
 * of the screen/head.  */
static inline void
constrainZoomTranslate (CompScreen *s)
{
	int out;
	ZOOM_SCREEN (s);

	for (out = 0; out < zs->nZooms; out++)
	{
		if (zs->zooms[out].xTranslate > 0.5f)
			zs->zooms[out].xTranslate = 0.5f;
		else if (zs->zooms[out].xTranslate < -0.5f)
			zs->zooms[out].xTranslate = -0.5f;

		if (zs->zooms[out].yTranslate > 0.5f)
			zs->zooms[out].yTranslate = 0.5f;
		else if (zs->zooms[out].yTranslate < -0.5f)
			zs->zooms[out].yTranslate = -0.5f;
	}
}

/* Functions for adjusting the zoomed area.
 * These are the core of the zoom plug-in; Anything wanting
 * to adjust the zoomed area must use setCenter or setZoomArea
 * and setScale or front ends to them.  */

/* Sets the center of the zoom area to X,Y.
 * We have to be able to warp the pointer here: If we are moved by
 * anything except mouse movement, we have to sync the
 * mouse pointer. This is to allow input, and is NOT necessary
 * when input redirection is available to us or if we're cheating
 * and using a scaled mouse cursor to imitate IR.
 * The center is not the center of the screen. This is the target-center;
 * that is, it's the point that's the same regardless of zoom level.
 */
static void
setCenter (CompScreen *s,
           int        x,
           int        y,
           Bool       instant)
{
	int out = outputDeviceForPoint (s, x, y);
	CompOutput  *o = &s->outputDev[out];
	ZOOM_SCREEN (s);

	if (zs->zooms[out].locked)
		return;

	zs->zooms[out].xTranslate = (float)
	                            ((x - o->region.extents.x1) - o->width  / 2) / (o->width);
	zs->zooms[out].yTranslate = (float)
	                            ((y - o->region.extents.y1) - o->height / 2) / (o->height);

	if (instant)
	{
		zs->zooms[out].realXTranslate = zs->zooms[out].xTranslate;
		zs->zooms[out].realYTranslate = zs->zooms[out].yTranslate;
		zs->zooms[out].yVelocity = 0.0f;
		zs->zooms[out].xVelocity = 0.0f;

		updateActualTranslates (&zs->zooms[out]);
	}

	const BananaValue *
	option_mouse_pan = bananaGetOption (bananaIndex,
	                                    "mouse_pan",
	                                    s->screenNum);

	if (option_mouse_pan->b)
		restrainCursor (s, out);
}

/* Zooms the area described.
 * The math could probably be cleaned up, but should be correct now. */
static void
setZoomArea (CompScreen *s,
             int        x,
             int        y,
             int        width,
             int        height,
             Bool       instant)
{
	int out = outputDeviceForGeometry (s, x, y, width, height, 0);
	CompOutput  *o = &s->outputDev[out];
	ZOOM_SCREEN (s);

	if (zs->zooms[out].newZoom == 1.0f)
		return;

	if (zs->zooms[out].locked)
		return;

	zs->zooms[out].xTranslate =
	        (float) -((o->width/2) - (x + (width/2) - o->region.extents.x1))
	        / (o->width);

	zs->zooms[out].xTranslate /= (1.0f - zs->zooms[out].newZoom);
	zs->zooms[out].yTranslate =
	        (float) -((o->height/2) - (y + (height/2) - o->region.extents.y1))
	        / (o->height);

	zs->zooms[out].yTranslate /= (1.0f - zs->zooms[out].newZoom);
	constrainZoomTranslate (s);

	if (instant)
	{
		zs->zooms[out].realXTranslate = zs->zooms[out].xTranslate;
		zs->zooms[out].realYTranslate = zs->zooms[out].yTranslate;
		updateActualTranslates (&zs->zooms[out]);
	}

	const BananaValue *
	option_mouse_pan = bananaGetOption (bananaIndex,
	                                    "mouse_pan",
	                                    s->screenNum);

	if (option_mouse_pan->b)
		restrainCursor (s, out);
}

/* Moves the zoom area to the window specified */
static void
zoomAreaToWindow (CompWindow *w)
{
	int left = w->serverX - w->input.left;
	int width = w->width + w->input.left + w->input.right;
	int top = w->serverY - w->input.top;
	int height = w->height + w->input.top + w->input.bottom;

	setZoomArea (w->screen, left, top, width, height, FALSE);
}

/* Pans the zoomed area vertically/horizontally by * value * zs->panFactor
 * TODO: Fix output. */
static void
panZoom (CompScreen *s,
         int        xvalue,
         int        yvalue)
{
	int out;
	ZOOM_SCREEN (s);

	const BananaValue *
	option_pan_factor = bananaGetOption (bananaIndex,
	                                     "pan_factor",
	                                     s->screenNum);

	for (out = 0; out < zs->nZooms; out++)
	{
		zs->zooms[out].xTranslate +=
		        option_pan_factor->f * xvalue *
		        zs->zooms[out].currentZoom;

		zs->zooms[out].yTranslate +=
		        option_pan_factor->f * yvalue *
		        zs->zooms[out].currentZoom;
	}

	constrainZoomTranslate (s);
}

/* Enables polling of mouse position, and refreshes currently
 * stored values.
 */
static void
enableMousePolling (CompScreen *s)
{
	ZOOM_SCREEN (s);

	zs->pollHandle = addPositionPollingCallback (s, updateMouseInterval);

	zs->lastChange = time (NULL);

	getCurrentMousePosition (s, &zs->mouseX, &zs->mouseY);
}

/* Sets the zoom (or scale) level.
 * Cleans up if we are suddenly zoomed out.
 */
static void
setScale (CompScreen *s,
          int        out,
          float      value)
{
	ZOOM_SCREEN (s);

	if (zs->zooms[out].locked)
		return;

	if (value >= 1.0f)
		value = 1.0f;
	else
	{
		if (!zs->pollHandle)
			enableMousePolling (s);

		zs->grabbed |= (1 << zs->zooms[out].output);
		cursorZoomActive (s);
	}

	if (value == 1.0f)
	{
		zs->zooms[out].xTranslate = 0.0f;
		zs->zooms[out].yTranslate = 0.0f;
		cursorZoomInactive (s);
	}

	const BananaValue *
	option_minimum_zoom = bananaGetOption (bananaIndex,
	                                       "minimum_zoom",
	                                       s->screenNum);

	if (value < option_minimum_zoom->f)
		value = option_minimum_zoom->f;

	zs->zooms[out].newZoom = value;

	damageScreen (s);
}

/* Sets the zoom factor to the bigger of the two floats supplied.
 * Convenience function for setting the scale factor for an area.
 */
static inline void
setScaleBigger (CompScreen *s,
                int        out,
                float      x,
                float      y)
{
	setScale (s, out, x > y ? x : y);
}

/* Mouse code...
 * This takes care of keeping the mouse in sync with the zoomed area and
 * vice versa.
 * See heading for description.
 */

/* Syncs the center, based on translations, back to the mouse.
 * This should be called when doing non-IR zooming and moving the zoom
 * area based on events other than mouse movement.
 */
static void
syncCenterToMouse (CompScreen *s)
{
	int x, y;
	int out;
	CompOutput  *o;
	ZOOM_SCREEN (s);

	out = outputDeviceForPoint (s, zs->mouseX, zs->mouseY);
	o = &s->outputDev[out];

	if (!isInMovement (s, out))
		return;

	x = (int) ((zs->zooms[out].realXTranslate * o->width) +
	           (o->width / 2) + o->region.extents.x1);

	y = (int) ((zs->zooms[out].realYTranslate * o->height) +
	           (o->height / 2) + o->region.extents.y1);

	if ((x != zs->mouseX || y != zs->mouseY)
	    && zs->grabbed && zs->zooms[out].newZoom != 1.0f)
	{
		warpPointer (s, x - pointerX, y - pointerY );
		zs->mouseX = x;
		zs->mouseY = y;
	}
}

/* Convert the point X,Y to where it would be when zoomed.  */
static void
convertToZoomed (CompScreen *s,
                 int        out,
                 int        x,
                 int        y,
                 int        *resultX,
                 int        *resultY)
{
	CompOutput  *o = &s->outputDev[out];
	ZoomArea    *za;
	ZOOM_SCREEN (s);

	za = &zs->zooms[out];
	x -= o->region.extents.x1;
	y -= o->region.extents.y1;

	*resultX = x - (za->realXTranslate *
	                (1.0f - za->currentZoom) * o->width) - o->width / 2;

	*resultX /= za->currentZoom;
	*resultX += o->width/2;
	*resultX += o->region.extents.x1;

	*resultY = y - (za->realYTranslate *
	                (1.0f - za->currentZoom) * o->height) - o->height / 2;

	*resultY /= za->currentZoom;
	*resultY += o->height/2;
	*resultY += o->region.extents.y1;
}

/* Same but use targeted translation, not real */
static void
convertToZoomedTarget (CompScreen *s,
                       int        out,
                       int        x,
                       int        y,
                       int        *resultX,
                       int        *resultY)
{
	CompOutput  *o = &s->outputDev[out];
	ZoomArea    *za;
	ZOOM_SCREEN (s);

	za = &zs->zooms[out];
	x -= o->region.extents.x1;
	y -= o->region.extents.y1;

	*resultX = x - (za->xTranslate *
	                (1.0f - za->newZoom) * o->width) - o->width / 2;

	*resultX /= za->newZoom;
	*resultX += o->width/2;
	*resultX += o->region.extents.x1;

	*resultY = y - (za->yTranslate *
	                (1.0f - za->newZoom) * o->height) - o->height / 2;
	*resultY /= za->newZoom;
	*resultY += o->height / 2;
	*resultY += o->region.extents.y1;
}

/* Make sure the given point + margin is visible;
 * Translate to make it visible if necessary.
 * Returns false if the point isn't on a actively zoomed head
 * or the area is locked. */
static Bool
ensureVisibility (CompScreen *s,
                  int        x,
                  int        y,
                  int        margin)
{
	int zoomX, zoomY;
	int out;
	CompOutput  *o;
	ZOOM_SCREEN (s);

	out = outputDeviceForPoint (s, x, y);
	if (!isActive (s, out))
		return FALSE;

	o = &s->outputDev[out];
	convertToZoomedTarget (s, out, x, y, &zoomX, &zoomY);
	ZoomArea *za = &zs->zooms[out];
	if (za->locked)
		return FALSE;

#define FACTOR (za->newZoom / (1.0f - za->newZoom))
	if (zoomX + margin > o->region.extents.x2)
		za->xTranslate +=
		        (FACTOR * (float) (zoomX + margin - o->region.extents.x2)) /
		        (float) o->width;
	else if (zoomX - margin < o->region.extents.x1)
		za->xTranslate +=
		        (FACTOR * (float) (zoomX - margin - o->region.extents.x1)) /
		        (float) o->width;

	if (zoomY + margin > o->region.extents.y2)
		za->yTranslate +=
		        (FACTOR * (float) (zoomY + margin - o->region.extents.y2)) /
		        (float) o->height;
	else if (zoomY - margin < o->region.extents.y1)
		za->yTranslate +=
		        (FACTOR * (float) (zoomY - margin - o->region.extents.y1)) /
		        (float) o->height;
#undef FACTOR
	constrainZoomTranslate (s);
	return TRUE;
}

/* Attempt to ensure the visibility of an area defined by x1/y1 and x2/y2.
 * See ensureVisibility () for details.
 *
 * This attempts to find the translations that leaves the biggest part of
 * the area visible.
 *
 * gravity defines what part of the window that should get
 * priority if it isn't possible to fit all of it.
 */
static void
ensureVisibilityArea (CompScreen  *s,
                      int         x1,
                      int         y1,
                      int         x2,
                      int         y2,
                      int         margin,
                      ZoomGravity gravity)
{
	int targetX, targetY, targetW, targetH;
	int out;
	CompOutput *o;
	ZOOM_SCREEN (s);

	out = outputDeviceForPoint (s, x1 + (x2-x1/2), y1 + (y2-y1/2));
	o = &s->outputDev[out];

#define WIDTHOK (float)(x2-x1) / (float)o->width < zs->zooms[out].newZoom
#define HEIGHTOK (float)(y2-y1) / (float)o->height < zs->zooms[out].newZoom

	if (WIDTHOK &&
	    HEIGHTOK) {
		ensureVisibility (s, x1, y1, margin);
		ensureVisibility (s, x2, y2, margin);
		return;
	}

	switch (gravity) {
	case NORTHWEST:
		targetX = x1;
		targetY = y1;
		if (WIDTHOK)
			targetW = x2 - x1;
		else
			targetW = o->width * zs->zooms[out].newZoom;
		if (HEIGHTOK)
			targetH = y2 - y1;
		else
			targetH = o->height * zs->zooms[out].newZoom;
		break;
	case NORTHEAST:
		targetY = y1;
		if (WIDTHOK)
		{
			targetX = x1;
			targetW = x2-x1;
		}
		else
		{
			targetX = x2 - o->width * zs->zooms[out].newZoom;
			targetW = o->width * zs->zooms[out].newZoom;
		}

		if (HEIGHTOK)
			targetH = y2-y1;
		else
			targetH = o->height * zs->zooms[out].newZoom;
		break;
	case SOUTHWEST:
		targetX = x1;
		if (WIDTHOK)
			targetW = x2-x1;
		else
			targetW = o->width * zs->zooms[out].newZoom;
		if (HEIGHTOK)
		{
			targetY = y1;
			targetH = y2-y1;
		}
		else
		{
			targetY = y2 - (o->width * zs->zooms[out].newZoom);
			targetH = o->width * zs->zooms[out].newZoom;
		}
		break;
	case SOUTHEAST:
		if (WIDTHOK)
		{
			targetX = x1;
			targetW = x2-x1;
		}
		else
		{
			targetW = o->width * zs->zooms[out].newZoom;
			targetX = x2 - targetW;
		}

		if (HEIGHTOK)
		{
			targetY = y1;
			targetH = y2 - y1;
		}
		else
		{
			targetH = o->height * zs->zooms[out].newZoom;
			targetY = y2 - targetH;
		}
		break;
	case CENTER:
		setCenter (s, x1 + (x2 - x1 / 2), y1 + (y2 - y1 / 2), FALSE);
		return;
		break;
	}

	setZoomArea (s, targetX, targetY, targetW, targetH, FALSE);
	return;
}

/* Ensures that the cursor is visible on the given head.
 * Note that we check if currentZoom is 1.0f, because that often means that
 * mouseX and mouseY is not up-to-date (since the polling timer just
 * started).
 */
static void
restrainCursor (CompScreen *s,
                int        out)
{
	int x1, y1, x2, y2, margin;
	int diffX = 0, diffY = 0;
	int north, south, east, west;
	float z;
	CompOutput  *o = &s->outputDev[out];
	ZOOM_SCREEN (s);

	z = zs->zooms[out].newZoom;

	const BananaValue *
	option_restrain_margin = bananaGetOption (bananaIndex,
	                                          "restrain_margin",
	                                          s->screenNum);

	margin = option_restrain_margin->i;
	north = distanceToEdge (s, out, NORTH);
	south = distanceToEdge (s, out, SOUTH);
	east = distanceToEdge (s, out, EAST);
	west = distanceToEdge (s, out, WEST);

	if (zs->zooms[out].currentZoom == 1.0f)
	{
		zs->lastChange = time (NULL);
		getCurrentMousePosition (s, &zs->mouseX, &zs->mouseY);
	}

	convertToZoomedTarget (s, out, zs->mouseX - zs->cursor.hotX,
	                       zs->mouseY - zs->cursor.hotY, &x1, &y1);

	convertToZoomedTarget
	        (s, out,
	        zs->mouseX - zs->cursor.hotX + zs->cursor.width,
	        zs->mouseY - zs->cursor.hotY + zs->cursor.height,
	        &x2, &y2);

	if ((x2 - x1 > o->region.extents.x2 - o->region.extents.x1) ||
	    (y2 - y1 > o->region.extents.y2 - o->region.extents.y1))
		return;

	if (x2 > o->region.extents.x2 - margin && east > 0)
		diffX = x2 - o->region.extents.x2 + margin;
	else if (x1 < o->region.extents.x1 + margin && west > 0)
		diffX = x1 - o->region.extents.x1 - margin;

	if (y2 > o->region.extents.y2 - margin && south > 0)
		diffY = y2 - o->region.extents.y2 + margin;
	else if (y1 < o->region.extents.y1 + margin && north > 0)
		diffY = y1 - o->region.extents.y1 - margin;

	if (abs (diffX)*z > 0  || abs (diffY)*z > 0)
		warpPointer (s,
		             (int) (zs->mouseX - pointerX) -  (int) ((float)diffX * z),
		             (int) (zs->mouseY - pointerY) -  (int) ((float)diffY * z));
}

/* Check if the cursor is still visible.
 * We also make sure to activate/deactivate cursor scaling here
 * so we turn on/off the pointer if it moves from one head to another.
 * FIXME: Detect an actual output change instead of spamming.
 * FIXME: The second ensureVisibility (sync with restrain).
 */
static void
cursorMoved (CompScreen *s)
{
	int out;
	ZOOM_SCREEN (s);

	out = outputDeviceForPoint (s, zs->mouseX, zs->mouseY);
	if (isActive (s, out))
	{
		const BananaValue *
		option_restrain_mouse = bananaGetOption (bananaIndex,
		                                         "restrain_mouse",
		                                         s->screenNum);

		if (option_restrain_mouse->b)
			restrainCursor (s, out);

		const BananaValue *
		option_mouse_pan = bananaGetOption (bananaIndex,
		                                    "mouse_pan",
		                                    s->screenNum);

		const BananaValue *
		option_restrain_margin = bananaGetOption (bananaIndex,
		                                          "restrain_margin",
		                                          s->screenNum);

		if (option_mouse_pan->b)
		{
			ensureVisibilityArea (s,
			                      zs->mouseX - zs->cursor.hotX,
			                      zs->mouseY - zs->cursor.hotY,
			                      zs->mouseX + zs->cursor.width -
			                      zs->cursor.hotX,
			                      zs->mouseY + zs->cursor.height -
			                      zs->cursor.hotY,
			                      option_restrain_margin->i,
			                      NORTHWEST);
		}

		cursorZoomActive (s);
	}
	else
	{
		cursorZoomInactive (s);
	}
}

/* Update the mouse position.
 * Based on the zoom engine in use, we will have to move the zoom area.
 * This might have to be added to a timer.
 */
static void
updateMousePosition (CompScreen *s,
                     int        x,
                     int        y)
{
	ZOOM_SCREEN (s);
	int out;

	zs->mouseX = x;
	zs->mouseY = y;
	out = outputDeviceForPoint (s, zs->mouseX, zs->mouseY);
	zs->lastChange = time (NULL);

	const BananaValue *
	option_sync_mouse = bananaGetOption (bananaIndex,
	                                     "sync_mouse",
	                                     s->screenNum);

	if (option_sync_mouse->b && !isInMovement (s, out))
		setCenter (s, zs->mouseX, zs->mouseY, TRUE);

	cursorMoved (s);
	damageScreen (s);
}

/* Timeout handler to poll the mouse. Returns false (and thereby does not
 * get re-added to the queue) when zoom is not active. */
static void
updateMouseInterval (CompScreen *s,
                     int        x,
                     int        y)
{
	ZOOM_SCREEN (s);

	updateMousePosition (s, x, y);

	if (!zs->grabbed)
	{
		if (zs->pollHandle)
			removePositionPollingCallback (s, zs->pollHandle);

		zs->pollHandle = 0;

		cursorMoved (s);
	}
}

/* Free a cursor */
static void
freeCursor (CursorTexture * cursor)
{
	if (!cursor->isSet)
		return;

	makeScreenCurrent (cursor->screen);
	cursor->isSet = FALSE;
	glDeleteTextures (1, &cursor->texture);
	cursor->texture = 0;
}

/* Translate into place and draw the scaled cursor.  */
static void
drawCursor (CompScreen          *s,
            CompOutput          *output,
            const CompTransform *transform)
{
	int out = output->id;
	ZOOM_SCREEN (s);

	if (zs->cursor.isSet)
	{
		CompTransform sTransform = *transform;
		float scaleFactor;
		int ax, ay, x, y;

		/* This is a hack because these transformations are wrong when
		 * we're working exposed. Expo is capable of telling where the
		 * real mouse is despite zoom, so we don't have to disable the
		 * zoom. We do, however, have to show the original pointer.
		 */
		if (dontuseScreengrabExist (s, "expo"))
		{
			cursorZoomInactive (s);
			return;
		}

		transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);
		convertToZoomed (s, out, zs->mouseX, zs->mouseY, &ax, &ay);
		glPushMatrix ();
		glLoadMatrixf (sTransform.m);
		glTranslatef ((float) ax, (float) ay, 0.0f);

		const BananaValue *
		option_scale_mouse_dynamic = bananaGetOption (bananaIndex,
		                                              "scale_mouse_dynamic",
		                                              s->screenNum);

		const BananaValue *
		option_scale_mouse_static = bananaGetOption (bananaIndex,
		                                             "scale_mouse_static",
		                                             s->screenNum);

		if (option_scale_mouse_dynamic->b)
			scaleFactor = 1.0f / zs->zooms[out].currentZoom;
		else
			scaleFactor = 1.0f / option_scale_mouse_static->f;

		glScalef (scaleFactor,
		          scaleFactor,
		          1.0f);
		x = -zs->cursor.hotX;
		y = -zs->cursor.hotY;

		glEnable (GL_BLEND);
		glBindTexture (GL_TEXTURE_RECTANGLE_ARB, zs->cursor.texture);
		glEnable (GL_TEXTURE_RECTANGLE_ARB);

		glBegin (GL_QUADS);
		glTexCoord2d (0, 0);
		glVertex2f (x, y);
		glTexCoord2d (0, zs->cursor.height);
		glVertex2f (x, y + zs->cursor.height);
		glTexCoord2d (zs->cursor.width, zs->cursor.height);
		glVertex2f (x + zs->cursor.width, y + zs->cursor.height);
		glTexCoord2d (zs->cursor.width, 0);
		glVertex2f (x + zs->cursor.width, y);
		glEnd ();
		glDisable (GL_BLEND);
		glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
		glDisable (GL_TEXTURE_RECTANGLE_ARB);
		glPopMatrix ();
	}
}

/* Create (if necessary) a texture to store the cursor,
 * fetch the cursor with XFixes. Store it.  */
static void
zoomUpdateCursor (CompScreen    *s,
                  CursorTexture *cursor)
{
	unsigned char *pixels;
	int i;
	Display       *dpy = display.display;

	if (!cursor->isSet)
	{
		cursor->isSet = TRUE;
		cursor->screen = s;
		makeScreenCurrent (s);
		glEnable (GL_TEXTURE_RECTANGLE_ARB);
		glGenTextures (1, &cursor->texture);
		glBindTexture (GL_TEXTURE_RECTANGLE_ARB, cursor->texture);

		const BananaValue *
		option_filter_linear = bananaGetOption (bananaIndex,
		                                        "filter_linear",
		                                        s->screenNum);

		if (option_filter_linear->b &&
		    display.textureFilter != GL_NEAREST)
		{
			glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
			                 GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
			                 GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		}
		else
		{
			glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
			                 GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
			                 GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
		glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
		                 GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri (GL_TEXTURE_RECTANGLE_ARB,
		                 GL_TEXTURE_WRAP_T, GL_CLAMP);
	}
	else
	{
		makeScreenCurrent (cursor->screen);
		glEnable (GL_TEXTURE_RECTANGLE_ARB);
	}

	XFixesCursorImage *ci = XFixesGetCursorImage (dpy);
	/* Hack to avoid changing to an invisible (bugged)cursor image.
	 * Example: The animated Firefox cursors.
	 */
	if (ci->width <= 1 && ci->height <= 1)
	{
		XFree (ci);
		return;
	}

	cursor->width = ci->width;
	cursor->height = ci->height;
	cursor->hotX = ci->xhot;
	cursor->hotY = ci->yhot;
	pixels = malloc (ci->width * ci->height * 4);

	if (!pixels)
	{
		XFree (ci);
		return;
	}

	for (i = 0; i < ci->width * ci->height; i++)
	{
		unsigned long pix = ci->pixels[i];
		pixels[i * 4] = pix & 0xff;
		pixels[(i * 4) + 1] = (pix >> 8) & 0xff;
		pixels[(i * 4) + 2] = (pix >> 16) & 0xff;
		pixels[(i * 4) + 3] = (pix >> 24) & 0xff;
	}

	glBindTexture (GL_TEXTURE_RECTANGLE_ARB, cursor->texture);
	glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA, cursor->width,
	              cursor->height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
	glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
	glDisable (GL_TEXTURE_RECTANGLE_ARB);
	XFree (ci);
	free (pixels);
}

/* We are no longer zooming the cursor, so display it.  */
static void
cursorZoomInactive (CompScreen *s)
{
	ZOOM_DISPLAY (&display);
	ZOOM_SCREEN (s);

	if (!zd->fixesSupported)
		return;

	if (zs->cursorInfoSelected)
	{
		zs->cursorInfoSelected = FALSE;
		XFixesSelectCursorInput (display.display, s->root, 0);
	}

	if (zs->cursor.isSet)
	{
		freeCursor (&zs->cursor);
	}

	if (zs->cursorHidden)
	{
		zs->cursorHidden = FALSE;
		XFixesShowCursor (display.display, s->root);
	}
}

/* Cursor zoom is active: We need to hide the original,
 * register for Cursor notifies and display the new one.
 * This can be called multiple times, not just on initial
 * activation.
 */
static void
cursorZoomActive (CompScreen *s)
{
	ZOOM_DISPLAY (&display);
	ZOOM_SCREEN (s);

	if (!zd->fixesSupported)
		return;

	const BananaValue *
	option_scale_mouse = bananaGetOption (bananaIndex,
	                                      "scale_mouse",
	                                      s->screenNum);

	if (!option_scale_mouse->b)
		return;

	if (!zs->cursorInfoSelected)
	{
		zs->cursorInfoSelected = TRUE;

		XFixesSelectCursorInput (display.display, s->root,
		                         XFixesDisplayCursorNotifyMask);

		zoomUpdateCursor (s, &zs->cursor);
	}

	const BananaValue *
	option_hide_original_mouse = bananaGetOption (bananaIndex,
	                                              "hide_original_mouse",
	                                              s->screenNum);

	if (zd->canHideCursor && !zs->cursorHidden &&
	    option_hide_original_mouse->b)
	{
		zs->cursorHidden = TRUE;

		XFixesHideCursor (display.display, s->root);
	}
}

/* Focus-track related event handling.
 * The lastMapped is a hack to ensure that newly mapped windows are
 * caught even if the grab that (possibly) triggered them affected
 * the mode. Windows created by a key binding (like creating a terminal
 * on a key binding) tends to trigger FocusIn events with mode other than
 * Normal. This works around this problem.
 * FIXME: Cleanup.
 * TODO: Avoid maximized windows.
 */
static void
focusTrack (XEvent      *event)
{
	int out;
	static Window lastMapped = 0;
	CompWindow    *w;
	ZoomScreen    *zs;

	if (event->type == MapNotify)
	{
		lastMapped = event->xmap.window;
		return;
	}
	else if (event->type != FocusIn)
		return;

	if ((event->xfocus.mode != NotifyNormal)
	    && (lastMapped != event->xfocus.window))
		return;

	lastMapped = 0;
	w = findWindowAtDisplay (event->xfocus.window);
	if (w == NULL || w->id == display.activeWindow)
		return;

	zs = GET_ZOOM_SCREEN (w->screen, GET_ZOOM_DISPLAY (&display));

	const BananaValue *
	option_follow_focus = bananaGetOption (bananaIndex,
	                                       "follow_focus",
	                                       w->screen->screenNum);

	const BananaValue *
	option_follow_focus_delay = bananaGetOption (bananaIndex,
	                                             "follow_focus_delay",
	                                             w->screen->screenNum);

	const BananaValue *
	option_always_focus_fit_window = bananaGetOption (bananaIndex,
	                                                  "always_focus_fit_window",
	                                                  w->screen->screenNum);

	const BananaValue *
	option_focus_fit_window = bananaGetOption (bananaIndex,
	                                           "focus_fit_window",
	                                           w->screen->screenNum);

	if (time (NULL) - zs->lastChange < option_follow_focus_delay->i ||
	    !option_follow_focus->b)
		return;

	out = outputDeviceForWindow (w);

	if (!isActive (w->screen, out) &&
	    !option_always_focus_fit_window->b)
		return;

	if (option_focus_fit_window->b)
	{
		int width = w->width + w->input.left + w->input.right;
		int height = w->height + w->input.top + w->input.bottom;
		float scale = MAX ((float) width/w->screen->outputDev[out].width,
		                   (float) height/w->screen->outputDev[out].height);

		const BananaValue *
		option_autoscale_min = bananaGetOption (bananaIndex,
		                                        "autoscale_min",
		                                        w->screen->screenNum);

		if (scale > option_autoscale_min->f)
			setScale (w->screen, out, scale);
	}

	zoomAreaToWindow (w);
}

/* Zoom in to the area pointed to by the mouse.
 */
static void
zoomIn (CompScreen *s)
{
	if (s)
	{
		int out = outputDeviceForPoint (s, pointerX, pointerY);
		ZOOM_SCREEN (s);

		const BananaValue *
		option_sync_mouse = bananaGetOption (bananaIndex,
		                                     "sync_mouse",
		                                     s->screenNum);

		if (option_sync_mouse->b && !isInMovement (s, out))
			setCenter (s, pointerX, pointerY, TRUE);

		const BananaValue *
		option_zoom_factor = bananaGetOption (bananaIndex,
		                                      "zoom_factor",
		                                      s->screenNum);

		setScale (s, out,
		          zs->zooms[out].newZoom /
		          option_zoom_factor->f);
	}
}

static void
zoomOut (CompScreen *s)
{
	if (s)
	{
		int out = outputDeviceForPoint (s, pointerX, pointerY);
		ZOOM_SCREEN (s);

		const BananaValue *
		option_zoom_factor = bananaGetOption (bananaIndex,
		                                      "zoom_factor",
		                                      s->screenNum);

		setScale (s, out,
		          zs->zooms[out].newZoom *
		          option_zoom_factor->f);
	}
}

/* Zoom to a specific level.
 * target defines the target zoom level.
 * First set the scale level and mark the display as grabbed internally (to
 * catch the FocusIn event). Either target the focused window or the mouse,
 * depending on settings.
 * FIXME: A bit of a mess...
 */
static Bool
zoomSpecific (CompScreen *s,
              int        x,
              int        y,
              float      target)
{
	if (s)
	{
		int out = outputDeviceForPoint (s, pointerX, pointerY);

		CompWindow   *w;

		ZOOM_SCREEN  (s);

		if (target == 1.0f && zs->zooms[out].newZoom == 1.0f)
			return FALSE;

		if (otherScreenGrabExist (s, NULL))
			return FALSE;

		setScale (s, out, target);

		w = findWindowAtDisplay (display.activeWindow);

		const BananaValue *
		option_spec_target_focus = bananaGetOption (bananaIndex,
		                                            "spec_target_focus",
		                                            -1);

		if (option_spec_target_focus->b
		    && w && w->screen->root == s->root)
		{
			zoomAreaToWindow (w);
		}
		else
		{
			setCenter (s, x, y, FALSE);
		}
	}

	return TRUE;
}

static void
zoomBoxActivate (CompScreen *s)
{
	if (s)
	{
		ZOOM_SCREEN (s);

		zs->grabIndex = pushScreenGrab (s, None, "ezoom");
		zs->box.x1 = pointerX;
		zs->box.y1 = pointerY;
		zs->box.x2 = pointerX;
		zs->box.y2 = pointerY;
	}
}

static void
zoomBoxDeactivate (void)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
	{
		int x, y, width, height;
		ZOOM_SCREEN (s);

		if (zs->grabIndex)
		{
			int out;
			CompOutput *o;

			removeScreenGrab (s, zs->grabIndex, NULL);
			zs->grabIndex = 0;

			zs->box.x2 = pointerX;
			zs->box.y2 = pointerY;

			x = MIN (zs->box.x1, zs->box.x2);
			y = MIN (zs->box.y1, zs->box.y2);
			width = MAX (zs->box.x1, zs->box.x2) - x;
			height = MAX (zs->box.y1, zs->box.y2) - y;

			out = outputDeviceForGeometry (s, x, y, width, height, 0);
			o = &s->outputDev[out];

			setScaleBigger (s, out, (float) width/o->width, (float)
			                height/o->height);

			setZoomArea (s, x, y, width, height, FALSE);
		}
	}
}

/* Centers the mouse based on zoom level and translation.
 */
static void
zoomCenterMouse (CompScreen *s)
{
	if (s)
	{
		ZoomScreen *zs = GET_ZOOM_SCREEN (s, GET_ZOOM_DISPLAY (&display));

		int out = outputDeviceForPoint (s, pointerX, pointerY);

		warpPointer (s,
		             (int) (s->outputDev[out].width/2 +
		                    s->outputDev[out].region.extents.x1 - pointerX)
		             + ((float) s->outputDev[out].width *
		                -zs->zooms[out].xtrans),
		             (int) (s->outputDev[out].height/2 +
		                    s->outputDev[out].region.extents.y1 - pointerY)
		             + ((float) s->outputDev[out].height *
		                zs->zooms[out].ytrans));
	}
}

/* 
 * Locks down the current zoom area
 */
static void
lockZoomAction (CompScreen *s)
{
	if (s)
	{
		int out = outputDeviceForPoint (s, pointerX, pointerY);

		ZOOM_SCREEN (s);

		zs->zooms[out].locked = !zs->zooms[out].locked;
	}
}

/* Resize a window to fit the zoomed area.
 * This could probably do with some moving-stuff too.
 * IE: Move the zoom area afterwards. And ensure
 * the window isn't resized off-screen.
 */
static void
zoomFitWindowToZoom (CompWindow *w)
{
	if (w)
	{
		int out = outputDeviceForWindow (w);
		unsigned int mask = CWWidth | CWHeight;
		CompScreen *s = w->screen;
		XWindowChanges xwc;
		ZoomScreen     *zs = GET_ZOOM_SCREEN (s, GET_ZOOM_DISPLAY (&display));

		xwc.x = w->serverX;
		xwc.y = w->serverY;

		xwc.width = (int) (s->outputDev[out].width *
		                   zs->zooms[out].currentZoom -
		                   (int) ((w->input.left + w->input.right)));

		xwc.height = (int) (s->outputDev[out].height *
		                    zs->zooms[out].currentZoom -
		                    (int) ((w->input.top + w->input.bottom)));

		constrainNewWindowSize (w,
		                        xwc.width,
		                        xwc.height,
		                        &xwc.width,
		                        &xwc.height);

		if (xwc.width == w->serverWidth)
			mask &= ~CWWidth;

		if (xwc.height == w->serverHeight)
			mask &= ~CWHeight;

		if (w->mapNum && (mask & (CWWidth | CWHeight)))
			sendSyncRequest (w);

		configureXWindow (w, mask, &xwc);
	}
}

/* Zooms to fit the active window to the screen without cutting
 * it off and targets it.
 */
static void
zoomToWindow (CompScreen *s,
              CompWindow *w)
{
	int width, height, out;

	CompOutput *o;

	if (!s)
		return;

	if (!w || w->screen->root != s->root)
		return;

	width = w->width + w->input.left + w->input.right;
	height = w->height + w->input.top + w->input.bottom;

	out = outputDeviceForWindow (w);

	o = &s->outputDev[out];

	setScaleBigger (s, out, (float) width/o->width,
	                (float) height/o->height);

	zoomAreaToWindow (w);
}

/* Event handler. Pass focus-related events on and handle XFixes events. 
 * Also handle keys and buttons. */
static void
zoomHandleEvent (XEvent      *event)
{
	CompScreen *s;
	XMotionEvent *mev;
	ZOOM_DISPLAY (&display);

	switch (event->type) {
	case MotionNotify:
		mev =  (XMotionEvent *) event;
		s = findScreenAtDisplay (mev->root);
		if (s)
		{
			ZOOM_SCREEN (s);
			if (zs->grabIndex)
			{
				zs->box.x2 = pointerX;
				zs->box.y2 = pointerY;
				damageScreen (s);
			}
		}
		break;
	case FocusIn:
	case MapNotify:
		focusTrack (event);
		break;
	case KeyPress:
		if (isKeyPressEvent (event, &zd->zoom_in_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			zoomIn (s);
		}
		else if (isKeyPressEvent (event, &zd->zoom_out_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			zoomOut (s);
		}
		else if (isKeyPressEvent (event, &zd->pan_left_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (!s)
				break;

			panZoom (s, -1, 0);
		}
		else if (isKeyPressEvent (event, &zd->pan_right_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (!s)
				break;

			panZoom (s, 1, 0);
		}
		else if (isKeyPressEvent (event, &zd->pan_up_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (!s)
				break;

			panZoom (s, 0, -1);
		}
		else if (isKeyPressEvent (event, &zd->pan_down_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (!s)
				break;

			panZoom (s, 0, 1);
		}
		else if (isKeyPressEvent (event, &zd->zoom_specific_1_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (!s)
				break;

			const BananaValue *
			option_zoom_spec1 = bananaGetOption (bananaIndex,
			                                     "zoom_spec1",
			                                     -1);

			zoomSpecific (s, event->xkey.x_root,
			                 event->xkey.y_root,
			                 option_zoom_spec1->f);
		}
		else if (isKeyPressEvent (event, &zd->zoom_specific_2_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (!s)
				break;

			const BananaValue *
			option_zoom_spec2 = bananaGetOption (bananaIndex,
			                                     "zoom_spec2",
			                                     -1);

			zoomSpecific (s, event->xkey.x_root,
			                 event->xkey.y_root,
			                 option_zoom_spec2->f);
		}
		else if (isKeyPressEvent (event, &zd->zoom_specific_3_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (!s)
				break;

			const BananaValue *
			option_zoom_spec3 = bananaGetOption (bananaIndex,
			                                     "zoom_spec3",
			                                     -1);

			zoomSpecific (s, event->xkey.x_root,
			                 event->xkey.y_root,
			                 option_zoom_spec3->f);
		}
		else if (isKeyPressEvent (event, &zd->center_mouse_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (!s)
				break;

			zoomCenterMouse (s);
		}
		else if (isKeyPressEvent (event, &zd->lock_zoom_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (!s)
				break;

			lockZoomAction (s);
		}
		else if (isKeyPressEvent (event, &zd->fit_to_zoom_key))
		{
			CompWindow *w = findWindowAtDisplay (display.activeWindow);

			zoomFitWindowToZoom (w);
		}
		else if (isKeyPressEvent (event, &zd->fit_to_window_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			CompWindow *w = findWindowAtDisplay (display.activeWindow);

			zoomToWindow (s, w);
		}
		break;
	case ButtonPress:
		if (isButtonPressEvent (event, &zd->zoom_in_button))
		{
			CompScreen *s = findScreenAtDisplay (event->xbutton.root);

			zoomIn (s);
		}
		else if (isButtonPressEvent (event, &zd->zoom_out_button))
		{
			CompScreen *s = findScreenAtDisplay (event->xbutton.root);

			zoomOut (s);
		}
		else if (isButtonPressEvent (event, &zd->zoom_box_button))
		{
			CompScreen *s = findScreenAtDisplay (event->xbutton.root);

			zoomBoxActivate (s);
		}
		break;
	case ButtonRelease:
		if (zd->zoom_box_button.button == event->xbutton.button)
		{
			zoomBoxDeactivate ();
		}
		break;
	default:
		if (event->type == zd->fixesEventBase + XFixesCursorNotify)
		{
			XFixesCursorNotifyEvent *cev = (XFixesCursorNotifyEvent *) event;
			s = findScreenAtDisplay (cev->window);

			if (s)
			{
				ZOOM_SCREEN (s);

				if (zs->cursor.isSet)
					zoomUpdateCursor (s, &zs->cursor);
			}
		}
		break;
	}

	UNWRAP (zd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (zd, &display, handleEvent, zoomHandleEvent);
}

static Bool
zoomInitDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	int minor, major;

	ZoomDisplay *zd;

	zd = malloc (sizeof (ZoomDisplay));
	if (!zd)
		return FALSE;

	zd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (zd->screenPrivateIndex < 0)
	{
		free (zd);
		return FALSE;
	}

	zd->fixesSupported =
	        XFixesQueryExtension (d->display,
	                              &zd->fixesEventBase,
	                              &zd->fixesErrorBase);

	XFixesQueryVersion (d->display, &major, &minor);

	if (major >= 4)
		zd->canHideCursor = TRUE;
	else
		zd->canHideCursor = FALSE;

	WRAP (zd, d, handleEvent, zoomHandleEvent);

	const BananaValue *
	option_zoom_in_key = bananaGetOption (bananaIndex, "zoom_in_key", -1);

	registerKey (option_zoom_in_key->s, &zd->zoom_in_key);

	const BananaValue *
	option_zoom_out_key = bananaGetOption (bananaIndex, "zoom_out_key", -1);

	registerKey (option_zoom_out_key->s, &zd->zoom_out_key);

	const BananaValue *
	option_zoom_specific_1_key = bananaGetOption (bananaIndex,
	                                              "zoom_specific_1_key", -1);

	registerKey (option_zoom_specific_1_key->s, &zd->zoom_specific_1_key);

	const BananaValue *
	option_zoom_specific_2_key = bananaGetOption (bananaIndex,
	                                              "zoom_specific_2_key", -1);

	registerKey (option_zoom_specific_2_key->s, &zd->zoom_specific_2_key);

	const BananaValue *
	option_zoom_specific_3_key = bananaGetOption (bananaIndex,
	                                              "zoom_specific_3_key", -1);

	registerKey (option_zoom_specific_3_key->s, &zd->zoom_specific_3_key);

	const BananaValue *
	option_pan_left_key = bananaGetOption (bananaIndex,
	                                       "pan_left_key", -1);

	registerKey (option_pan_left_key->s, &zd->pan_left_key);

	const BananaValue *
	option_pan_right_key = bananaGetOption (bananaIndex,
	                                       "pan_right_key", -1);

	registerKey (option_pan_right_key->s, &zd->pan_right_key);

	const BananaValue *
	option_pan_up_key = bananaGetOption (bananaIndex,
	                                       "pan_up_key", -1);

	registerKey (option_pan_up_key->s, &zd->pan_up_key);

	const BananaValue *
	option_pan_down_key = bananaGetOption (bananaIndex,
	                                       "pan_down_key", -1);

	registerKey (option_pan_down_key->s, &zd->pan_down_key);

	const BananaValue *
	option_fit_to_window_key = bananaGetOption (bananaIndex,
	                                            "fit_to_window_key", -1);

	registerKey (option_fit_to_window_key->s, &zd->fit_to_window_key);

	const BananaValue *
	option_center_mouse_key = bananaGetOption (bananaIndex,
	                                           "center_mouse_key", -1);

	registerKey (option_center_mouse_key->s, &zd->center_mouse_key);

	const BananaValue *
	option_fit_to_zoom_key = bananaGetOption (bananaIndex,
	                                          "fit_to_zoom_key", -1);

	registerKey (option_fit_to_zoom_key->s, &zd->fit_to_zoom_key);

	const BananaValue *
	option_lock_zoom_key = bananaGetOption (bananaIndex,
	                                        "lock_zoom_key", -1);

	registerKey (option_lock_zoom_key->s, &zd->lock_zoom_key);

	const BananaValue *
	option_zoom_in_button = bananaGetOption (bananaIndex,
	                                         "zoom_in_button", -1);

	registerButton (option_zoom_in_button->s, &zd->zoom_in_button);

	const BananaValue *
	option_zoom_out_button = bananaGetOption (bananaIndex,
	                                          "zoom_out_button", -1);

	registerButton (option_zoom_out_button->s, &zd->zoom_out_button);

	const BananaValue *
	option_zoom_box_button = bananaGetOption (bananaIndex,
	                                          "zoom_box_button", -1);

	registerButton (option_zoom_box_button->s, &zd->zoom_box_button);

	d->privates[displayPrivateIndex].ptr = zd;

	return TRUE;
}

static void
zoomFiniDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	ZOOM_DISPLAY (d);

	freeScreenPrivateIndex (zd->screenPrivateIndex);

	UNWRAP (zd, d, handleEvent);

	free (zd);
}

static Bool
zoomInitScreen (CompPlugin *p,
                CompScreen *s)
{
	int i;
	ZoomScreen   *zs;
	ZOOM_DISPLAY (&display);

	zs = malloc (sizeof (ZoomScreen));
	if (!zs)
		return FALSE;

	zs->grabIndex = 0;
	zs->nZooms = s->nOutputDev;
	zs->zooms = malloc (sizeof (ZoomArea) * zs->nZooms);

	for (i = 0; i < zs->nZooms; i++)
	{
		/* zs->grabbed is a mask ... Thus this limit */
		if (i > sizeof (long int) * 8)
			break;

		initialiseZoomArea (&zs->zooms[i], i);
	}

	zs->lastChange = 0;
	zs->grabbed = 0;
	zs->mouseX = -1;
	zs->mouseY = -1;
	zs->cursorInfoSelected = FALSE;
	zs->cursor.isSet = FALSE;
	zs->cursorHidden = FALSE;
	zs->pollHandle = 0;

	WRAP (zs, s, preparePaintScreen, zoomPreparePaintScreen);
	WRAP (zs, s, donePaintScreen, zoomDonePaintScreen);
	WRAP (zs, s, paintOutput, zoomPaintOutput);

	s->privates[zd->screenPrivateIndex].ptr = zs;

	return TRUE;
}

static void
zoomFiniScreen (CompPlugin *p,
                CompScreen *s)
{
	ZOOM_SCREEN (s);

	UNWRAP (zs, s, preparePaintScreen);
	UNWRAP (zs, s, donePaintScreen);
	UNWRAP (zs, s, paintOutput);

	if (zs->pollHandle)
		removePositionPollingCallback (s, zs->pollHandle);

	if (zs->zooms)
		free (zs->zooms);

	damageScreen (s); // If we are unloaded and zoomed in.
	cursorZoomInactive (s);

	free (zs);
}

static void
ezoomChangeNotify (const char        *optionName,
                   BananaType        optionType,
                   const BananaValue *optionValue,
                   int               screenNum)
{
	ZOOM_DISPLAY (&display);

	if (strcasecmp (optionName, "zoom_in_key") == 0)
		updateKey (optionValue->s, &zd->zoom_in_key);

	else if (strcasecmp (optionName, "zoom_out_key") == 0)
		updateKey (optionValue->s, &zd->zoom_out_key);

	else if (strcasecmp (optionName, "zoom_specific_1_key") == 0)
		updateKey (optionValue->s, &zd->zoom_specific_1_key);

	else if (strcasecmp (optionName, "zoom_specific_2_key") == 0)
		updateKey (optionValue->s, &zd->zoom_specific_1_key);

	else if (strcasecmp (optionName, "zoom_specific_3_key") == 0)
		updateKey (optionValue->s, &zd->zoom_specific_1_key);

	else if (strcasecmp (optionName, "pan_left_key") == 0)
		updateKey (optionValue->s, &zd->pan_left_key);

	else if (strcasecmp (optionName, "pan_right_key") == 0)
		updateKey (optionValue->s, &zd->pan_right_key);

	else if (strcasecmp (optionName, "pan_up_key") == 0)
		updateKey (optionValue->s, &zd->pan_up_key);

	else if (strcasecmp (optionName, "pan_down_key") == 0)
		updateKey (optionValue->s, &zd->pan_down_key);

	else if (strcasecmp (optionName, "fit_to_window_key") == 0)
		updateKey (optionValue->s, &zd->fit_to_window_key);

	else if (strcasecmp (optionName, "center_mouse_key") == 0)
		updateKey (optionValue->s, &zd->center_mouse_key);

	else if (strcasecmp (optionName, "fit_to_zoom_key") == 0)
		updateKey (optionValue->s, &zd->fit_to_zoom_key);

	else if (strcasecmp (optionName, "lock_zoom_key") == 0)
		updateKey (optionValue->s, &zd->lock_zoom_key);

	else if (strcasecmp (optionName, "zoom_in_button") == 0)
		updateButton (optionValue->s, &zd->zoom_in_button);

	else if (strcasecmp (optionName, "zoom_out_button") == 0)
		updateButton (optionValue->s, &zd->zoom_out_button);

	else if (strcasecmp (optionName, "zoom_box_button") == 0)
		updateButton (optionValue->s, &zd->zoom_box_button);
}

static Bool
zoomInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("ezoom", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("ezoom");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, ezoomChangeNotify);

	return TRUE;
}

static void
zoomFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

CompPluginVTable zoomVTable = {
	"ezoom",
	zoomInit,
	zoomFini,
	zoomInitDisplay,
	zoomFiniDisplay,
	zoomInitScreen,
	zoomFiniScreen,
	NULL, /* zoomInitWindow */
	NULL  /* zoomFiniWindow */
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &zoomVTable;
}

#if 0
/* Ensure visibility of an area defined by x1->x2/y1->y2
 * int:x1: left X coordinate
 * int:x2: right X Coordinate
 * int:y1: top Y coordinate
 * int:y2: bottom Y coordinate
 * bool:scale: zoom out if necessary to ensure visibility
 * bool:restrain: Restrain the mouse cursor
 * int:margin: The margin to use (default: 0)
 * if x2/y2 is omitted, it is ignored.
 */
static Bool
ensureVisibilityAction (CompDisplay     *d,
                        CompAction      *action,
                        CompActionState state,
                        CompOption      *option,
                        int nOption)
{
	CompScreen *s;
	Window xid;

	xid = getIntOptionNamed (option, nOption, "root", 0);
	s = findScreenAtDisplay (d, xid);

	if (s)
	{
		int x1, y1, x2, y2, margin, out;
		Bool scale, restrain;
		CompOutput *o;

		x1 = getIntOptionNamed (option, nOption, "x1", -1);
		y1 = getIntOptionNamed (option, nOption, "y1", -1);
		x2 = getIntOptionNamed (option, nOption, "x2", -1);
		y2 = getIntOptionNamed (option, nOption, "y2", -1);
		margin = getBoolOptionNamed (option, nOption, "margin", 0);
		scale = getBoolOptionNamed (option, nOption, "scale", FALSE);
		restrain = getBoolOptionNamed (option, nOption, "restrain", FALSE);
		if (x1 < 0 || y1 < 0)
			return FALSE;
		if (x2 < 0)
			y2 = y1 + 1;
		out = outputDeviceForPoint (s, x1, y1);
		ensureVisibility (s, x1, y1, margin);
		if (x2 >= 0 && y2 >= 0)
			ensureVisibility (s, x2, y2, margin);
		o = &s->outputDev[out];
#define WIDTH (x2 - x1)
#define HEIGHT (y2 - y1)
		if (scale && WIDTH && HEIGHT)
			setScaleBigger (s, out, (float) WIDTH/o->width,
			                (float) HEIGHT/o->height);
#undef WIDTH
#undef HEIGHT
		if (restrain)
			restrainCursor (s, out);
	}
	return TRUE;
}

/* Set the zoom area
 * This is an interface for scripting.
 * int32:x1: left x coordinate
 * int32:y1: top y coordinate
 * int32:x2: right x
 * int32:y2: bottom y
 * x2 and y2 can be omitted to assume x1==x2+1 y1==y2+1
 * boolean:scale: True if we should modify the zoom level, false to just
 *                adjust the movement/translation.
 * boolean:restrain: True to warp the pointer so it's visible.
 */
static Bool
setZoomAreaAction (CompDisplay     *d,
                   CompAction      *action,
                   CompActionState state,
                   CompOption      *option,
                   int nOption)
{
	CompScreen *s;
	Window xid;

	xid = getIntOptionNamed (option, nOption, "root", 0);
	s = findScreenAtDisplay (d, xid);

	if (s)
	{
		int x1, y1, x2, y2, out;
		Bool scale, restrain;
		CompOutput *o;

		x1 = getIntOptionNamed (option, nOption, "x1", -1);
		y1 = getIntOptionNamed (option, nOption, "y1", -1);
		x2 = getIntOptionNamed (option, nOption, "x2", -1);
		y2 = getIntOptionNamed (option, nOption, "y2", -1);
		scale = getBoolOptionNamed (option, nOption, "scale", FALSE);
		restrain = getBoolOptionNamed (option, nOption, "restrain", FALSE);

		if (x1 < 0 || y1 < 0)
			return FALSE;

		if (x2 < 0)
			x2 = x1 + 1;

		if (y2 < 0)
			y2 = y1 + 1;

		out = outputDeviceForPoint (s, x1, y1);
#define WIDTH (x2 - x1)
#define HEIGHT (y2 - y1)
		setZoomArea (s, x1, y1, WIDTH, HEIGHT, FALSE);
		o = &s->outputDev[out];
		if (scale && WIDTH && HEIGHT)
			setScaleBigger (s, out, (float) WIDTH/o->width,
			                (float) HEIGHT/o->height);
#undef WIDTH
#undef HEIGHT
		if (restrain)
			restrainCursor (s, out);
	}
	return TRUE;
}

/* Settings etc., boring stuff */
static const CompMetadataOptionInfo zoomDisplayOptionInfo[] = {
	{ "initiate", "key", 0, zoomInitiate, zoomTerminate },
	{ "zoom_in_button", "button", 0, zoomIn, 0 },
	{ "zoom_out_button", "button", 0, zoomOut, 0 },
	{ "zoom_in_key", "key", 0, zoomIn, 0 },
	{ "zoom_out_key", "key", 0, zoomOut, 0 },
	{ "zoom_specific_1_key", "key", 0, zoomSpecific1, 0 },
	{ "zoom_specific_2_key", "key", 0, zoomSpecific2, 0 },
	{ "zoom_specific_3_key", "key", 0, zoomSpecific3, 0 },
	{ "pan_left_key", "key", 0, zoomPanLeft, 0 },
	{ "pan_right_key", "key", 0, zoomPanRight, 0 },
	{ "pan_up_key", "key", 0, zoomPanUp, 0 },
	{ "pan_down_key", "key", 0, zoomPanDown, 0 },
	{ "fit_to_window_key", "key", 0, zoomToWindow, 0 },
	{ "center_mouse_key", "key", 0, zoomCenterMouse, 0 },
	{ "fit_to_zoom_key", "key", 0, zoomFitWindowToZoom, 0 },
	{ "ensure_visibility", "action", 0, ensureVisibilityAction, 0},
	{ "set_zoom_area", "action", 0, setZoomAreaAction, 0},
	{ "lock_zoom_key", "key", 0, lockZoomAction, 0},
	{ "zoom_box_button", "button", 0, zoomBoxActivate, zoomBoxDeactivate},

	CompKeyBinding initiate, zoom_in_key, zoom_out_key, zoom_specific_1_key, zoom_specific_2_key, zoom_specific_3_key.
	               pan_left_key, pan_right_key, pan_up_key, pan_down_key, fit_to_window_key, center_mouse_key,
	               fit_to_zoom_key, lock_zoom_key;

	CompButtonBinding zoom_in_button, zoom_out_button, zoom_box_button;
};
#endif

#if 0

static Bool
zoomInitiate (CompDisplay     *d,
              CompAction      *action,
              CompActionState state,
              CompOption      *option,
              int nOption)
{
	zoomIn (d, action, state, option, nOption);

	if (state & CompActionStateInitKey)
		action->state |= CompActionStateTermKey;

	if (state & CompActionStateInitButton)
		action->state |= CompActionStateTermButton;

	return TRUE;
}

static Bool
zoomTerminate (CompDisplay     *d,
               CompAction      *action,
               CompActionState state,
               CompOption      *option,
               int nOption)
{
	CompScreen *s;
	Window xid;

	xid = getIntOptionNamed (option, nOption, "root", 0);

	for (s = d->screens; s; s = s->next)
	{
		int out;
		ZOOM_SCREEN (s);

		if (xid && s->root != xid)
			continue;

		out = outputDeviceForPoint (s, pointerX, pointerY);

		if (zs->grabbed)
		{
			zs->zooms[out].newZoom = 1.0f;
			damageScreen (s);
		}
	}
	action->state &= ~(CompActionStateTermKey | CompActionStateTermButton);
	return FALSE;
}

#endif