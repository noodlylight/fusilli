/*
 * Copyright © 2005 Novell, Inc.
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
 *         Michail Bitzes <noodlylight@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <X11/cursorfont.h>

#include <fusilli-core.h>

static int bananaIndex;

static int displayPrivateIndex;

static CompButtonBinding initiate_button;
static CompButtonBinding zoom_in_button;
static CompButtonBinding zoom_out_button;
static CompButtonBinding zoom_pan_button;

typedef struct _ZoomDisplay {
	int             screenPrivateIndex;
	HandleEventProc handleEvent;
} ZoomDisplay;

typedef struct _ZoomBox {
	float x1;
	float y1;
	float x2;
	float y2;
} ZoomBox;

typedef struct _ZoomScreen {
	PreparePaintScreenProc   preparePaintScreen;
	DonePaintScreenProc      donePaintScreen;
	PaintOutputProc          paintOutput;

	float pointerSensitivity;

	int  grabIndex;
	Bool grab;

	int zoomed;

	Bool adjust;

	int    panGrabIndex;
	Cursor panCursor;

	GLfloat velocity;
	GLfloat scale;

	ZoomBox current[16];
	ZoomBox last[16];

	int x1, y1, x2, y2;

	int zoomOutput;
} ZoomScreen;

#define GET_ZOOM_DISPLAY(d) \
        ((ZoomDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define ZOOM_DISPLAY(d) \
        ZoomDisplay *zd = GET_ZOOM_DISPLAY (d)

#define GET_ZOOM_SCREEN(s, zd) \
        ((ZoomScreen *) (s)->privates[(zd)->screenPrivateIndex].ptr)

#define ZOOM_SCREEN(s) \
        ZoomScreen *zs = GET_ZOOM_SCREEN (s, GET_ZOOM_DISPLAY (&display))

static int
adjustZoomVelocity (ZoomScreen *zs)
{
	float d, adjust, amount;

	d = (1.0f - zs->scale) * 10.0f;

	adjust = d * 0.002f;
	amount = fabs (d);
	if (amount < 1.0f)
		amount = 1.0f;
	else if (amount > 5.0f)
		amount = 5.0f;

	zs->velocity = (amount * zs->velocity + adjust) / (amount + 1.0f);

	return (fabs (d) < 0.02f && fabs (zs->velocity) < 0.005f);
}

static void
zoomInEvent (CompScreen *s)
{
	BananaArgument arg[6];

	ZOOM_SCREEN (s);

	arg[0].type    = BananaInt;
	arg[0].name    = "root";
	arg[0].value.i = s->root;

	arg[1].type    = BananaInt;
	arg[1].name    = "output";
	arg[1].value.i = zs->zoomOutput;

	arg[2].type    = BananaInt;
	arg[2].name    = "x1";
	arg[2].value.i = zs->current[zs->zoomOutput].x1;

	arg[3].type    = BananaInt;
	arg[3].name    = "y1";
	arg[3].value.i = zs->current[zs->zoomOutput].y1;

	arg[4].type    = BananaInt;
	arg[4].name    = "x2";
	arg[4].value.i = zs->current[zs->zoomOutput].x2;

	arg[5].type    = BananaInt;
	arg[5].name    = "y2";
	arg[5].value.i = zs->current[zs->zoomOutput].y2;

	(*display.handleFusilliEvent) ("zoom", "in", arg, 6);
}

static void
zoomOutEvent (CompScreen *s)
{
	BananaArgument arg[2];

	ZOOM_SCREEN (s);

	arg[0].type    = BananaInt;
	arg[0].name    = "root";
	arg[0].value.i = s->root;

	arg[1].type    = BananaInt;
	arg[1].name    = "output";
	arg[1].value.i = zs->zoomOutput;

	(*display.handleFusilliEvent) ("zoom", "out", arg, 2);
}

static void
zoomPreparePaintScreen (CompScreen *s,
                        int        msSinceLastPaint)
{
	ZOOM_SCREEN (s);

	if (zs->adjust)
	{
		int   steps;
		float amount;

		const BananaValue *
		option_speed = bananaGetOption (bananaIndex,
		                                "speed",
		                                s->screenNum);

		const BananaValue *
		option_timestep = bananaGetOption (bananaIndex,
		                                   "timestep",
		                                   s->screenNum);

		amount = msSinceLastPaint * 0.35f * option_speed->f;
		steps  = amount / (0.5f * option_timestep->f);
		if (!steps) steps = 1;

		while (steps--)
		{
			if (adjustZoomVelocity (zs))
			{
				BoxPtr pBox = &s->outputDev[zs->zoomOutput].region.extents;

				zs->scale = 1.0f;
				zs->velocity = 0.0f;
				zs->adjust = FALSE;

				if (zs->current[zs->zoomOutput].x1 == pBox->x1 &&
				    zs->current[zs->zoomOutput].y1 == pBox->y1 &&
				    zs->current[zs->zoomOutput].x2 == pBox->x2 &&
				    zs->current[zs->zoomOutput].y2 == pBox->y2)
				{
					zs->zoomed &= ~(1 << zs->zoomOutput);
					zoomOutEvent (s);
				}
				else
				{
					zoomInEvent (s);
				}

				break;
			}
			else
			{
				zs->scale += (zs->velocity * msSinceLastPaint) / s->redrawTime;
			}
		}
	}

	UNWRAP (zs, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, msSinceLastPaint);
	WRAP (zs, s, preparePaintScreen, zoomPreparePaintScreen);
}

static void
zoomGetCurrentZoom (CompScreen *s,
                    int        output,
                    ZoomBox    *pBox)
{
	ZOOM_SCREEN (s);

	if (output == zs->zoomOutput)
	{
		float inverse;

		inverse = 1.0f - zs->scale;

		pBox->x1 = zs->scale * zs->current[output].x1 +
			inverse * zs->last[output].x1;
		pBox->y1 = zs->scale * zs->current[output].y1 +
			inverse * zs->last[output].y1;
		pBox->x2 = zs->scale * zs->current[output].x2 +
			inverse * zs->last[output].x2;
		pBox->y2 = zs->scale * zs->current[output].y2 +
			inverse * zs->last[output].y2;
	}
	else
	{
		pBox->x1 = zs->current[output].x1;
		pBox->y1 = zs->current[output].y1;
		pBox->x2 = zs->current[output].x2;
		pBox->y2 = zs->current[output].y2;
	}
}

static void
zoomDonePaintScreen (CompScreen *s)
{
	ZOOM_SCREEN (s);

	if (zs->adjust)
		damageScreen (s);

	UNWRAP (zs, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP (zs, s, donePaintScreen, zoomDonePaintScreen);
}

static Bool
zoomPaintOutput (CompScreen              *s,
                 const ScreenPaintAttrib *sAttrib,
                 const CompTransform     *transform,
                 Region                  region,
                 CompOutput              *output,
                 unsigned int            mask)
{
	CompTransform zTransform = *transform;
	Bool          status;

	ZOOM_SCREEN (s);

	if (output->id != ~0 && (zs->zoomed & (1 << output->id)))
	{
		int     saveFilter;
		ZoomBox box;
		float   scale, x, y, x1, y1;
		float   oWidth = output->width;
		float   oHeight = output->height;

		mask &= ~PAINT_SCREEN_REGION_MASK;

		zoomGetCurrentZoom (s, output->id, &box);

		x1 = box.x1 - output->region.extents.x1;
		y1 = box.y1 - output->region.extents.y1;

		scale = oWidth / (box.x2 - box.x1);

		x = ((oWidth  / 2.0f) - x1) / oWidth;
		y = ((oHeight / 2.0f) - y1) / oHeight;

		x = 0.5f - x * scale;
		y = 0.5f - y * scale;

		matrixTranslate (&zTransform, -x, y, 0.0f);
		matrixScale (&zTransform, scale, scale, 1.0f);

		mask |= PAINT_SCREEN_TRANSFORMED_MASK;

		saveFilter = s->filter[SCREEN_TRANS_FILTER];

		const BananaValue *
		option_filter_linear = bananaGetOption (bananaIndex,
		                                        "filter_linear",
		                                        s->screenNum);

		if ((zs->zoomOutput != output->id || !zs->adjust) && scale > 3.9f &&
		    !option_filter_linear->b)
		    s->filter[SCREEN_TRANS_FILTER] = COMP_TEXTURE_FILTER_FAST;

		UNWRAP (zs, s, paintOutput);
		status = (*s->paintOutput) (s, sAttrib, &zTransform, region, output,
		                        mask);
		WRAP (zs, s, paintOutput, zoomPaintOutput);

		s->filter[SCREEN_TRANS_FILTER] = saveFilter;
	}
	else
	{
		UNWRAP (zs, s, paintOutput);
		status = (*s->paintOutput) (s, sAttrib, transform, region, output,
		                        mask);
		WRAP (zs, s, paintOutput, zoomPaintOutput);
	}

	if (status && zs->grab)
	{
		int x1, x2, y1, y2;

		x1 = MIN (zs->x1, zs->x2);
		y1 = MIN (zs->y1, zs->y2);
		x2 = MAX (zs->x1, zs->x2);
		y2 = MAX (zs->y1, zs->y2);

		if (zs->grabIndex)
		{
			transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &zTransform);

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
	}

	return status;
}

static void
zoomInitiateForSelection (CompScreen *s,
                          int        output)
{
	int tmp;

	ZOOM_SCREEN (s);

	if (zs->x1 > zs->x2)
	{
		tmp = zs->x1;
		zs->x1 = zs->x2;
		zs->x2 = tmp;
	}

	if (zs->y1 > zs->y2)
	{
		tmp = zs->y1;
		zs->y1 = zs->y2;
		zs->y2 = tmp;
	}

	if (zs->x1 < zs->x2 && zs->y1 < zs->y2)
	{
		float  oWidth, oHeight;
		float  xScale, yScale, scale;
		BoxRec box;
		int    cx, cy;
		int    width, height;

		oWidth  = s->outputDev[output].width;
		oHeight = s->outputDev[output].height;

		cx = (int) ((zs->x1 + zs->x2) / 2.0f + 0.5f);
		cy = (int) ((zs->y1 + zs->y2) / 2.0f + 0.5f);

		width  = zs->x2 - zs->x1;
		height = zs->y2 - zs->y1;

		xScale = oWidth  / width;
		yScale = oHeight / height;

		scale = MAX (MIN (xScale, yScale), 1.0f);

		box.x1 = cx - (oWidth  / scale) / 2.0f;
		box.y1 = cy - (oHeight / scale) / 2.0f;
		box.x2 = cx + (oWidth  / scale) / 2.0f;
		box.y2 = cy + (oHeight / scale) / 2.0f;

		if (box.x1 < s->outputDev[output].region.extents.x1)
		{
			box.x2 += s->outputDev[output].region.extents.x1 - box.x1;
			box.x1 = s->outputDev[output].region.extents.x1;
		}
		else if (box.x2 > s->outputDev[output].region.extents.x2)
		{
			box.x1 -= box.x2 - s->outputDev[output].region.extents.x2;
			box.x2 = s->outputDev[output].region.extents.x2;
		}

		if (box.y1 < s->outputDev[output].region.extents.y1)
		{
			box.y2 += s->outputDev[output].region.extents.y1 - box.y1;
			box.y1 = s->outputDev[output].region.extents.y1;
		}
		else if (box.y2 > s->outputDev[output].region.extents.y2)
		{
			box.y1 -= box.y2 - s->outputDev[output].region.extents.y2;
			box.y2 = s->outputDev[output].region.extents.y2;
		}

		if (zs->zoomed & (1 << output))
		{
			zoomGetCurrentZoom (s, output, &zs->last[output]);
		}
		else
		{
			zs->last[output].x1 = s->outputDev[output].region.extents.x1;
			zs->last[output].y1 = s->outputDev[output].region.extents.y1;
			zs->last[output].x2 = s->outputDev[output].region.extents.x2;
			zs->last[output].y2 = s->outputDev[output].region.extents.y2;
		}

		zs->current[output].x1 = box.x1;
		zs->current[output].y1 = box.y1;
		zs->current[output].x2 = box.x2;
		zs->current[output].y2 = box.y2;

		zs->scale = 0.0f;
		zs->adjust = TRUE;
		zs->zoomOutput = output;
		zs->zoomed |= (1 << output);

		damageScreen (s);
	}
}

static Bool
zoomIn (BananaArgument     *arg,
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
		float   w, h, x0, y0;
		int     output;
		ZoomBox box;

		ZOOM_SCREEN (s);

		output = outputDeviceForPoint (s, pointerX, pointerY);

		if (!zs->grabIndex)
			zs->grabIndex = pushScreenGrab (s, None, "zoom");

		if (zs->zoomed & (1 << output))
		{
			zoomGetCurrentZoom (s, output, &box);
		}
		else
		{
			box.x1 = s->outputDev[output].region.extents.x1;
			box.y1 = s->outputDev[output].region.extents.y1;
			box.x2 = s->outputDev[output].region.extents.x2;
			box.y2 = s->outputDev[output].region.extents.y2;
		}

		const BananaValue *
		option_zoom_factor = bananaGetOption (bananaIndex,
		                                      "zoom_factor",
		                                      s->screenNum);

		w = (box.x2 - box.x1) / option_zoom_factor->f;
		h = (box.y2 - box.y1) / option_zoom_factor->f;

		x0 = (pointerX - s->outputDev[output].region.extents.x1) / (float)
		    s->outputDev[output].width;
		y0 = (pointerY - s->outputDev[output].region.extents.y1) / (float)
		    s->outputDev[output].height;

		zs->x1 = box.x1 + (x0 * (box.x2 - box.x1) - x0 * w + 0.5f);
		zs->y1 = box.y1 + (y0 * (box.y2 - box.y1) - y0 * h + 0.5f);
		zs->x2 = zs->x1 + w;
		zs->y2 = zs->y1 + h;

		zoomInitiateForSelection (s, output);

		return TRUE;
	}

	return FALSE;
}

static Bool
zoomInitiate (BananaArgument     *arg,
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
		int   output, x1, y1;
		float scale;

		ZOOM_SCREEN (s);

		if (otherScreenGrabExist (s, "zoom", NULL))
			return FALSE;

		if (!zs->grabIndex)
			zs->grabIndex = pushScreenGrab (s, None, "zoom");

		/* start selection zoom rectangle */

		output = outputDeviceForPoint (s, pointerX, pointerY);

		if (zs->zoomed & (1 << output))
		{
			ZoomBox box;
			float   oWidth;

			zoomGetCurrentZoom (s, output, &box);

			oWidth = s->outputDev[output].width;
			scale = oWidth / (box.x2 - box.x1);

			x1 = box.x1;
			y1 = box.y1;
		}
		else
		{
			scale = 1.0f;
			x1 = s->outputDev[output].region.extents.x1;
			y1 = s->outputDev[output].region.extents.y1;
		}

		zs->x1 = zs->x2 = x1 +
		    ((pointerX - s->outputDev[output].region.extents.x1) /
		     scale + 0.5f);
		zs->y1 = zs->y2 = y1 +
		    ((pointerY - s->outputDev[output].region.extents.y1) /
		     scale + 0.5f);

		zs->zoomOutput = output;

		zs->grab = TRUE;

		damageScreen (s);

		return TRUE;
	}

	return FALSE;
}

static Bool
zoomOut (BananaArgument     *arg,
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
		int output;

		ZOOM_SCREEN (s);

		output = outputDeviceForPoint (s, pointerX, pointerY);

		zoomGetCurrentZoom (s, output, &zs->last[output]);

		zs->current[output].x1 = s->outputDev[output].region.extents.x1;
		zs->current[output].y1 = s->outputDev[output].region.extents.y1;
		zs->current[output].x2 = s->outputDev[output].region.extents.x2;
		zs->current[output].y2 = s->outputDev[output].region.extents.y2;

		zs->zoomOutput = output;
		zs->scale = 0.0f;
		zs->adjust = TRUE;
		zs->grab = FALSE;

		if (zs->grabIndex)
		{
			removeScreenGrab (s, zs->grabIndex, NULL);
			zs->grabIndex = 0;
		}

		damageScreen (s);

		return TRUE;
	}

	return FALSE;
}

static Bool
zoomTerminate (BananaArgument     *arg,
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
		ZOOM_SCREEN (s);

		if (xid && s->root != xid)
			continue;

		if (zs->grab)
		{
			int output;

			output = outputDeviceForPoint (s, zs->x1, zs->y1);

			if (zs->x2 > s->outputDev[output].region.extents.x2)
				zs->x2 = s->outputDev[output].region.extents.x2;

			if (zs->y2 > s->outputDev[output].region.extents.y2)
				zs->y2 = s->outputDev[output].region.extents.y2;

			zoomInitiateForSelection (s, output);

			zs->grab = FALSE;
		}
		else
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = s->root;

			zoomOut (&arg, 1);
		}
	}

	return FALSE;
}

static Bool
zoomInitiatePan (BananaArgument     *arg,
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
		int output;

		ZOOM_SCREEN (s);

		output = outputDeviceForPoint (s, pointerX, pointerY);

		if (!(zs->zoomed & (1 << output)))
			return FALSE;

		if (otherScreenGrabExist (s, "zoom", NULL))
			return FALSE;

		if (!zs->panGrabIndex)
			zs->panGrabIndex = pushScreenGrab (s, zs->panCursor, "zoom-pan");

		zs->zoomOutput = output;

		return TRUE;
	}

	return FALSE;
}

static Bool
zoomTerminatePan (BananaArgument     *arg,
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
		ZOOM_SCREEN (s);

		if (xid && s->root != xid)
			continue;

		if (zs->panGrabIndex)
		{
			removeScreenGrab (s, zs->panGrabIndex, NULL);
			zs->panGrabIndex = 0;

			zoomInEvent (s);
		}

		return TRUE;
	}

	return FALSE;
}

static void
zoomHandleMotionEvent (CompScreen *s,
                       int        xRoot,
                       int        yRoot)
{
	ZOOM_SCREEN (s);

	if (zs->grabIndex)
	{
		int     output = zs->zoomOutput;
		ZoomBox box;
		float   scale, oWidth = s->outputDev[output].width;

		zoomGetCurrentZoom (s, output, &box);

		if (zs->zoomed & (1 << output))
			scale = oWidth / (box.x2 - box.x1);
		else
			scale = 1.0f;

		if (zs->panGrabIndex)
		{
			float dx, dy;

			dx = (xRoot - lastPointerX) / scale;
			dy = (yRoot - lastPointerY) / scale;

			box.x1 -= dx;
			box.y1 -= dy;
			box.x2 -= dx;
			box.y2 -= dy;

			if (box.x1 < s->outputDev[output].region.extents.x1)
			{
				box.x2 += s->outputDev[output].region.extents.x1 - box.x1;
				box.x1 = s->outputDev[output].region.extents.x1;
			}
			else if (box.x2 > s->outputDev[output].region.extents.x2)
			{
				box.x1 -= box.x2 - s->outputDev[output].region.extents.x2;
				box.x2 = s->outputDev[output].region.extents.x2;
			}

			if (box.y1 < s->outputDev[output].region.extents.y1)
			{
				box.y2 += s->outputDev[output].region.extents.y1 - box.y1;
				box.y1 = s->outputDev[output].region.extents.y1;
			}
			else if (box.y2 > s->outputDev[output].region.extents.y2)
			{
				box.y1 -= box.y2 - s->outputDev[output].region.extents.y2;
				box.y2 = s->outputDev[output].region.extents.y2;
			}

			zs->current[output] = box;

			damageScreen (s);
		}
		else
		{
			int x1, y1;

			if (zs->zoomed & (1 << output))
			{
				x1 = box.x1;
				y1 = box.y1;
			}
			else
			{
				x1 = s->outputDev[output].region.extents.x1;
				y1 = s->outputDev[output].region.extents.y1;
			}

			zs->x2 = x1 +
			    ((xRoot - s->outputDev[output].region.extents.x1) /
			     scale + 0.5f);
			zs->y2 = y1 +
			    ((yRoot - s->outputDev[output].region.extents.y1) /
			     scale + 0.5f);

			damageScreen (s);
		}
	}
}

static void
zoomHandleEvent (XEvent      *event)
{
	CompScreen *s;

	ZOOM_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (event->xkey.keycode == display.escapeKeyCode)
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xkey.root;

			zoomTerminate (&arg, 1);
		}
	case ButtonPress:
		if (isButtonPressEvent (event, &initiate_button))
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;

			zoomInitiate (&arg, 1);
		}
		else if (isButtonPressEvent (event, &zoom_in_button))
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;

			zoomIn (&arg, 1);
		}
		else if (isButtonPressEvent (event, &zoom_out_button))
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;

			zoomOut (&arg, 1);
		}
		else if (isButtonPressEvent (event, &zoom_pan_button))
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;

			zoomInitiatePan (&arg, 1);
		}

		break;
	case ButtonRelease:
		if (initiate_button.button == event->xbutton.button)
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;

			zoomTerminate (&arg, 1);
		}
		else if (zoom_pan_button.button == event->xbutton.button)
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;

			zoomTerminatePan (&arg, 1);
		}
		break;
	case MotionNotify:
		s = findScreenAtDisplay (event->xmotion.root);
		if (s)
			zoomHandleMotionEvent (s, pointerX, pointerY);
		break;
	case EnterNotify:
	case LeaveNotify:
		s = findScreenAtDisplay (event->xcrossing.root);
		if (s)
			zoomHandleMotionEvent (s, pointerX, pointerY);
	default:
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

	WRAP (zd, d, handleEvent, zoomHandleEvent);

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
	ZoomScreen *zs;

	ZOOM_DISPLAY (&display);

	zs = malloc (sizeof (ZoomScreen));
	if (!zs)
		return FALSE;

	zs->grabIndex = 0;
	zs->grab = FALSE;

	zs->velocity = 0.0f;

	zs->zoomOutput = 0;

	zs->zoomed = 0;
	zs->adjust = FALSE;

	zs->panGrabIndex = 0;
	zs->panCursor = XCreateFontCursor (display.display, XC_fleur);

	zs->scale = 0.0f;

	memset (&zs->current, 0, sizeof (zs->current));
	memset (&zs->last, 0, sizeof (zs->last));

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

	if (zs->panCursor)
		XFreeCursor (display.display, zs->panCursor);

	UNWRAP (zs, s, preparePaintScreen);
	UNWRAP (zs, s, donePaintScreen);
	UNWRAP (zs, s, paintOutput);

	free (zs);
}

static void
zoomChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	if (strcasecmp (optionName, "initiate_button") == 0)
		updateButton (optionValue->s, &initiate_button);

	else if (strcasecmp (optionName, "zoom_in_button") == 0)
		updateButton (optionValue->s, &zoom_in_button);

	else if (strcasecmp (optionName, "zoom_out_button") == 0)
		updateButton (optionValue->s, &zoom_out_button);

	else if (strcasecmp (optionName, "zoom_pan_button") == 0)
		updateButton (optionValue->s, &zoom_pan_button);
}

static Bool
zoomInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("zoom", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("zoom");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, zoomChangeNotify);

	const BananaValue *
	option_initiate_button = bananaGetOption (bananaIndex,
	                                          "initiate_button",
	                                          -1);

	const BananaValue *
	option_zoom_in_button = bananaGetOption (bananaIndex,
	                                         "zoom_in_button",
	                                         -1);

	const BananaValue *
	option_zoom_out_button = bananaGetOption (bananaIndex,
	                                          "zoom_out_button",
	                                          -1);

	const BananaValue *
	option_zoom_pan_button = bananaGetOption (bananaIndex,
	                                          "zoom_pan_button",
	                                          -1);

	registerButton (option_initiate_button->s, &initiate_button);
	registerButton (option_zoom_in_button->s, &zoom_in_button);
	registerButton (option_zoom_out_button->s, &zoom_out_button);
	registerButton (option_zoom_pan_button->s, &zoom_pan_button);

	return TRUE;
}

static void
zoomFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

CompPluginVTable zoomVTable = {
	"zoom",
	zoomInit,
	zoomFini,
	NULL, /* zoomInitCore */
	NULL, /* zoomFiniCore */
	zoomInitDisplay,
	zoomFiniDisplay,
	zoomInitScreen,
	zoomFiniScreen,
	NULL, /* zoomInitWindow */
	NULL  /* zoomFiniWindow */
};

CompPluginVTable *
getCompPluginInfo20141130 (void)
{
	return &zoomVTable;
}
