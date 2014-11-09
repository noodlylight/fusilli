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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <X11/Xatom.h>
#include <X11/Xproto.h>

#include <fusilli-cube.h>

#define ROTATE_POINTER_SENSITIVITY_FACTOR 0.05f

static int bananaIndex;

static int displayPrivateIndex;
static int cubeDisplayPrivateIndex;

static CompButtonBinding initiate_button;

static CompKeyBinding rotate_left_key, rotate_right_key;
static CompKeyBinding rotate_left_window_key, rotate_right_window_key;

static CompButtonBinding rotate_left_button, rotate_right_button;
static CompButtonBinding rotate_left_window_button, rotate_right_window_button;

#define NUM_VIEWPORTS 9
//viewports start with 1
static CompKeyBinding rotate_to_key[NUM_VIEWPORTS + 1];
static CompKeyBinding rotate_with_window_key[NUM_VIEWPORTS + 1];

typedef struct _RotateDisplay {
	int             screenPrivateIndex;
	HandleEventProc handleEvent;
} RotateDisplay;

typedef struct _RotateScreen {
	PreparePaintScreenProc       preparePaintScreen;
	DonePaintScreenProc          donePaintScreen;
	PaintOutputProc              paintOutput;
	WindowGrabNotifyProc         windowGrabNotify;
	WindowUngrabNotifyProc       windowUngrabNotify;
	ActivateWindowProc           activateWindow;

	CubeGetRotationProc getRotation;

	float pointerSensitivity;

	Bool snapTop;
	Bool snapBottom;

	int grabIndex;

	GLfloat xrot, xVelocity;
	GLfloat yrot, yVelocity;

	GLfloat baseXrot;

	Bool    moving;
	GLfloat moveTo;

	Window moveWindow;
	int    moveWindowX;

	XPoint savedPointer;
	Bool   grabbed;
	Bool   focusDefault;

	CompTimeoutHandle rotateHandle;
	Bool              slow;
	unsigned int      grabMask;
	CompWindow        *grabWindow;

	float progress;
	float progressVelocity;

	GLfloat zoomTranslate;
} RotateScreen;

#define GET_ROTATE_DISPLAY(d) \
        ((RotateDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define ROTATE_DISPLAY(d) \
        RotateDisplay *rd = GET_ROTATE_DISPLAY (d)

#define GET_ROTATE_SCREEN(s, rd) \
        ((RotateScreen *) (s)->base.privates[(rd)->screenPrivateIndex].ptr)

#define ROTATE_SCREEN(s) \
        RotateScreen *rs = GET_ROTATE_SCREEN (s, GET_ROTATE_DISPLAY (&display))

static void
rotateChangeNotify (const char        *optionName,
                    BananaType        optionType,
                    const BananaValue *optionValue,
                    int               screenNum)
{
	if (strcasecmp (optionName, "sensitivity") == 0)
	{
		CompScreen *screen;

		if (screenNum != -1)
			screen = getScreenFromScreenNum (screenNum);
		else
			return;

		ROTATE_SCREEN (screen);

		rs->pointerSensitivity = 
		         optionValue->f * ROTATE_POINTER_SENSITIVITY_FACTOR;
	}
	else if (strcasecmp (optionName, "initiate_button") == 0)
	{
		updateButton (optionValue->s, &initiate_button);
	}
	else if (strcasecmp (optionName, "rotate_left_key") == 0)
	{
		updateKey (optionValue->s, &rotate_left_key);
	}
	else if (strcasecmp (optionName, "rotate_left_button") == 0)
	{
		updateButton (optionValue->s, &rotate_left_button);
	}
	else if (strcasecmp (optionName, "rotate_right_key") == 0)
	{
		updateKey (optionValue->s, &rotate_right_key);
	}
	else if (strcasecmp (optionName, "rotate_right_button") == 0)
	{
		updateButton (optionValue->s, &rotate_right_button);
	}
	else if (strcasecmp (optionName, "rotate_left_window_key") == 0)
	{
		updateKey (optionValue->s, &rotate_left_window_key);
	}
	else if (strcasecmp (optionName, "rotate_left_window_button") == 0)
	{
		updateButton (optionValue->s, &rotate_left_window_button);
	}
	else if (strcasecmp (optionName, "rotate_right_window_key") == 0)
	{
		updateKey (optionValue->s, &rotate_right_window_key);
	}
	else if (strcasecmp (optionName, "rotate_right_window_button") == 0)
	{
		updateButton (optionValue->s, &rotate_right_window_button);
	}
	else if (strstr (optionName, "rotate_to_key"))
	{
		int i = strlen ("rotate_to_key");
		int index = atoi (&optionName[i]);

		updateKey (optionValue->s, &rotate_to_key[index]);
	}
	else if (strstr (optionName, "rotate_with_window_key"))
	{
		int i = strlen ("rotate_with_window_key");
		int index = atoi (&optionName[i]);

		updateKey (optionValue->s, &rotate_with_window_key[index]);
	}
}

static int
adjustVelocity (CompScreen *s,
                int          size,
                int          invert)
{
	float xrot, yrot, adjust, amount;

	ROTATE_SCREEN (s);

	if (rs->moving)
	{
		xrot = rs->moveTo + (rs->xrot + rs->baseXrot);
	}
	else
	{
		xrot = rs->xrot;
		if (rs->xrot < -180.0f / size)
			xrot = 360.0f / size + rs->xrot;
		else if (rs->xrot > 180.0f / size)
			xrot = rs->xrot - 360.0f / size;
	}

	const BananaValue *
	option_acceleration = bananaGetOption (bananaIndex,
	                                       "acceleration",
	                                       s->screenNum);

	adjust = -xrot * 0.05f * option_acceleration->f;
	amount = fabs (xrot);
	if (amount < 10.0f)
		amount = 10.0f;
	else if (amount > 30.0f)
		amount = 30.0f;

	if (rs->slow)
		adjust *= 0.05f;

	rs->xVelocity = (amount * rs->xVelocity + adjust) / (amount + 2.0f);

	yrot = rs->yrot;
	/* Only snap if more than 2 viewports */
	if (size > 2)
	{
		if (rs->yrot > 50.0f && ((rs->snapTop && invert == 1) ||
		                         (rs->snapBottom && invert != 1)))
			yrot -= 90.f;
		else if (rs->yrot < -50.0f && ((rs->snapTop && invert != 1) ||
			                           (rs->snapBottom && invert == 1)))
			yrot += 90.f;
	}

	adjust = -yrot * 0.05f * option_acceleration->f;
	amount = fabs (rs->yrot);
	if (amount < 10.0f)
		amount = 10.0f;
	else if (amount > 30.0f)
		amount = 30.0f;

	rs->yVelocity = (amount * rs->yVelocity + adjust) / (amount + 2.0f);

	return (fabs (xrot) < 0.1f && fabs (rs->xVelocity) < 0.2f &&
	        fabs (yrot) < 0.1f && fabs (rs->yVelocity) < 0.2f);
}

static void
rotateReleaseMoveWindow (CompScreen *s)
{
	CompWindow *w;

	ROTATE_SCREEN (s);

	w = findWindowAtScreen (s, rs->moveWindow);
	if (w)
		syncWindowPosition (w);

	rs->moveWindow = None;
}

static void
rotatePreparePaintScreen (CompScreen *s,
                          int        msSinceLastPaint)
{
	ROTATE_SCREEN (s);
	CUBE_SCREEN (s);

	float oldXrot = rs->xrot + rs->baseXrot;

	if (rs->grabIndex || rs->moving)
	{
		int   steps;
		float amount, chunk;

		const BananaValue *
		option_speed = bananaGetOption (bananaIndex, "speed", s->screenNum);

		const BananaValue *
		option_timestep = bananaGetOption (bananaIndex,
		                                   "timestep",
		                                   s->screenNum);

		amount = msSinceLastPaint * 0.05f * option_speed->f;
		steps  = amount / (0.5f * option_timestep->f);

		if (!steps) steps = 1;
		chunk  = amount / (float) steps;

		while (steps--)
		{
			rs->xrot += rs->xVelocity * chunk;
			rs->yrot += rs->yVelocity * chunk;

			if (rs->xrot > 360.0f / s->hsize)
			{
				rs->baseXrot += 360.0f / s->hsize;
				rs->xrot -= 360.0f / s->hsize;
			}
			else if (rs->xrot < 0.0f)
			{
				rs->baseXrot -= 360.0f / s->hsize;
				rs->xrot += 360.0f / s->hsize;
			}

			if (cs->invert == -1)
			{
				if (rs->yrot > 45.0f)
				{
					rs->yVelocity = 0.0f;
					rs->yrot = 45.0f;
				}
				else if (rs->yrot < -45.0f)
				{
					rs->yVelocity = 0.0f;
					rs->yrot = -45.0f;
				}
			}
			else
			{
				if (rs->yrot > 100.0f)
				{
					rs->yVelocity = 0.0f;
					rs->yrot = 100.0f;
				}
				else if (rs->yrot < -100.0f)
				{
					rs->yVelocity = 0.0f;
					rs->yrot = -100.0f;
				}
			}

			if (rs->grabbed)
			{
				rs->xVelocity /= 1.25f;
				rs->yVelocity /= 1.25f;

				if (fabs (rs->xVelocity) < 0.01f)
					rs->xVelocity = 0.0f;
				if (fabs (rs->yVelocity) < 0.01f)
					rs->yVelocity = 0.0f;
			}
			else if (adjustVelocity (s, s->hsize, cs->invert))
			{
				rs->xVelocity = 0.0f;
				rs->yVelocity = 0.0f;

				if (fabs (rs->yrot) < 0.1f)
				{
					float xrot;
					int   tx;

					xrot = rs->baseXrot + rs->xrot;
					if (xrot < 0.0f)
						tx = (s->hsize * xrot / 360.0f) - 0.5f;
					else
						tx = (s->hsize * xrot / 360.0f) + 0.5f;

					/* flag end of rotation */
					cs->rotationState = RotationNone;

					moveScreenViewport (s, tx, 0, TRUE);

					rs->xrot = 0.0f;
					rs->yrot = 0.0f;
					rs->baseXrot = rs->moveTo = 0.0f;
					rs->moving = FALSE;

					if (rs->grabIndex)
					{
						removeScreenGrab (s, rs->grabIndex, &rs->savedPointer);
						rs->grabIndex = 0;
					}

					if (rs->moveWindow)
					{
						CompWindow *w;

						w = findWindowAtScreen (s, rs->moveWindow);
						if (w)
						{
							moveWindow (w, rs->moveWindowX - w->attrib.x, 0,
							    TRUE, TRUE);
							syncWindowPosition (w);
						}
					}
					else if (rs->focusDefault)
					{
						int i;

						for (i = 0; i < s->maxGrab; i++)
							if (s->grabs[i].active &&
							    strcmp ("switcher", s->grabs[i].name) == 0)
								break;

						/* only focus default window if switcher isn't active */
						if (i == s->maxGrab)
							focusDefaultWindow (s);
					}

					rs->moveWindow = 0;
				}
				break;
			}
		}

		if (rs->moveWindow)
		{
			CompWindow *w;

			w = findWindowAtScreen (s, rs->moveWindow);
			if (w)
			{
				float xrot = (s->hsize * (rs->baseXrot + rs->xrot)) / 360.0f;

				moveWindowToViewportPosition (w,
				                      rs->moveWindowX - xrot * s->width,
				                      w->attrib.y,
				                      FALSE);
			}
		}
	}

	if (rs->moving)
	{
		if (fabs (rs->xrot + rs->baseXrot + rs->moveTo) <=
		    (360.0 / (s->hsize * 2.0)))
		{
			rs->progress = fabs (rs->xrot + rs->baseXrot + rs->moveTo) /
			               (360.0 / (s->hsize * 2.0));
		}
		else if (fabs (rs->xrot + rs->baseXrot) <= (360.0 / (s->hsize * 2.0)))
		{
			rs->progress = fabs (rs->xrot + rs->baseXrot) /
			               (360.0 / (s->hsize * 2.0));
		}
		else
		{
			rs->progress += fabs (rs->xrot + rs->baseXrot - oldXrot) /
			                (360.0 / (s->hsize * 2.0));
			rs->progress = MIN (rs->progress, 1.0);
		}
	}
	else if (rs->progress != 0.0f || rs->grabbed)
	{
		int   steps;
		float amount, chunk;

		const BananaValue *
		option_speed = bananaGetOption (bananaIndex, "speed", s->screenNum);

		const BananaValue *
		option_timestep = bananaGetOption (bananaIndex,
		                                   "timestep",
		                                   s->screenNum);

		amount = msSinceLastPaint * 0.05f * option_speed->f;
		steps = amount / (0.5f * option_timestep->f);
		if (!steps)
			steps = 1;

		chunk = amount / (float) steps;

		while (steps--)
		{
			float dt, adjust, tamount;

			if (rs->grabbed)
				dt = 1.0 - rs->progress;
			else
				dt = 0.0f - rs->progress;

			adjust = dt * 0.15f;
			tamount = fabs (dt) * 1.5f;
			if (tamount < 0.2f)
				tamount = 0.2f;
			else if (tamount > 2.0f)
				tamount = 2.0f;

			rs->progressVelocity = (tamount * rs->progressVelocity + adjust) /
			                       (tamount + 1.0f);

			rs->progress += rs->progressVelocity * chunk;

			if (fabs (dt) < 0.01f && fabs (rs->progressVelocity) < 0.0001f)
			{
				if (rs->grabbed)
					rs->progress = 1.0f;
				else
					rs->progress = 0.0f;

				break;
			}
		}
	}

	if (cs->invert == 1 && !cs->unfolded)
	{
		const BananaValue *
		option_zoom = bananaGetOption (bananaIndex,
		                               "zoom",
		                               s->screenNum);

		rs->zoomTranslate = option_zoom->f *
		                    rs->progress;
	}
	else
	{
		rs->zoomTranslate = 0.0;
	}

	UNWRAP (rs, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, msSinceLastPaint);
	WRAP (rs, s, preparePaintScreen, rotatePreparePaintScreen);
}

static void
rotateDonePaintScreen (CompScreen *s)
{
	ROTATE_SCREEN (s);

	if (rs->grabIndex || rs->moving ||
	    (rs->progress != 0.0 && rs->progress != 1.0))
	{
		if ((!rs->grabbed && !rs->snapTop && !rs->snapBottom) ||
		    rs->xVelocity || rs->yVelocity || rs->progressVelocity)
		{
			damageScreen (s);
		}
	}

	UNWRAP (rs, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP (rs, s, donePaintScreen, rotateDonePaintScreen);
}

static void
rotateGetRotation (CompScreen *s,
                   float      *x,
                   float      *v,
                   float      *progress)
{
	CUBE_SCREEN (s);
	ROTATE_SCREEN (s);

	UNWRAP (rs, cs, getRotation);
	(*cs->getRotation) (s, x, v, progress);
	WRAP (rs, cs, getRotation, rotateGetRotation);

	*x += rs->baseXrot + rs->xrot;
	*v += rs->yrot;
	*progress = MAX (*progress, rs->progress);
}

static Bool
rotatePaintOutput (CompScreen              *s,
                   const ScreenPaintAttrib *sAttrib,
                   const CompTransform     *transform,
                   Region                  region,
                   CompOutput              *output,
                   unsigned int            mask)
{
	Bool status;

	ROTATE_SCREEN (s);

	if (rs->grabIndex || rs->moving || rs->progress != 0.0f)
	{
		CompTransform sTransform = *transform;

		matrixTranslate (&sTransform, 0.0f, 0.0f, -rs->zoomTranslate);

		mask &= ~PAINT_SCREEN_REGION_MASK;
		mask |= PAINT_SCREEN_TRANSFORMED_MASK;

		UNWRAP (rs, s, paintOutput);
		status = (*s->paintOutput) (s, sAttrib, &sTransform, region,
		                            output, mask);
		WRAP (rs, s, paintOutput, rotatePaintOutput);
	}
	else
	{
		UNWRAP (rs, s, paintOutput);
		status = (*s->paintOutput) (s, sAttrib, transform, region,
		                            output, mask);
		WRAP (rs, s, paintOutput, rotatePaintOutput);
	}

	return status;
}

static Bool
rotateInitiate (BananaArgument    *arg,
                int               nArg)
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
		ROTATE_SCREEN (s);
		CUBE_SCREEN (s);

		if (s->hsize < 2)
			return FALSE;

		if (rs->rotateHandle && rs->grabWindow)
		{
			if (otherScreenGrabExist (s, "rotate", "move", NULL))
				return FALSE;
		}
		else
		{
			if (otherScreenGrabExist (s, "rotate", "switcher", "cube", NULL))
				return FALSE;
		}

		rs->moving = FALSE;
		rs->slow   = FALSE;

		/* Set the rotation state for cube - if action is non-NULL,
		   we set it to manual (as we were called from the 'Initiate
		   Rotation' binding. Otherwise, we set it to Change. */
		//if (action)
			cs->rotationState = RotationManual;
		//else
		//	cs->rotationState = RotationChange;

		if (!rs->grabIndex)
		{
			rs->grabIndex = pushScreenGrab (s, s->invisibleCursor, "rotate");

			if (rs->grabIndex)
			{
				int x, y;

				BananaValue *arg_x = getArgNamed ("x", arg, nArg);

				if (arg_x != NULL)
					x = arg_x->i;
				else
					x = 0;

				BananaValue *arg_y = getArgNamed ("y", arg, nArg);

				if (arg_y != NULL)
					y = arg_y->i;
				else
					y = 0;

				rs->savedPointer.x = x;
				rs->savedPointer.y = y;
			}
		}

		if (rs->grabIndex)
		{
			rs->moveTo = 0.0f;

			rs->grabbed = TRUE;

			const BananaValue *
			option_snap_top = bananaGetOption (bananaIndex,
			                                   "snap_top",
			                                   s->screenNum);

			const BananaValue *
			option_snap_bottom = bananaGetOption (bananaIndex,
			                                      "snap_bottom",
			                                      s->screenNum);

			rs->snapTop = option_snap_top->b;
			rs->snapBottom = option_snap_bottom->b;
		}
	}

	return TRUE;
}

static Bool
rotateTerminate (BananaArgument  *arg,
                 int             nArg)
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
		ROTATE_SCREEN (s);

		if (xid && s->root != xid)
			continue;

		if (rs->grabIndex)
		{
			if (!xid)
			{
				rs->snapTop = FALSE;
				rs->snapBottom = FALSE;
			}

			rs->grabbed = FALSE;
			damageScreen (s);
		}
	}

	return FALSE;
}

static Bool
rotate (BananaArgument  *arg,
        int             nArg)
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
		int direction;

		ROTATE_SCREEN (s);

		if (s->hsize < 2)
			return FALSE;

		if (otherScreenGrabExist (s, "rotate", "move", "switcher",
		                          "group-drag", "cube", NULL))
			return FALSE;

		BananaValue *arg_direction = getArgNamed ("direction", arg, nArg);

		if (arg_direction != NULL)
			direction = arg_direction->i;
		else
			direction = 0;

		if (!direction)
			return FALSE;

		if (rs->moveWindow)
			rotateReleaseMoveWindow (s);

		BananaValue *arg_focus_default = 
		                      getArgNamed ("focus_default", arg, nArg);

		if (arg_focus_default != NULL)
			rs->focusDefault = arg_focus_default->b;
		else
			rs->focusDefault = TRUE;

		rs->moving  = TRUE;
		rs->moveTo += (360.0f / s->hsize) * direction;
		rs->grabbed = FALSE;

		damageScreen (s);
	}

	return FALSE;
}

static Bool
rotateWithWindow (BananaArgument  *arg,
                  int             nArg)
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
		const BananaValue *
		option_raise_on_rotate = bananaGetOption (bananaIndex,
		                                          "raise_on_rotate",
		                                          -1);

		Bool raise = option_raise_on_rotate->b;
		int  direction;

		ROTATE_SCREEN (s);

		if (s->hsize < 2)
			return FALSE;

		BananaValue *arg_direction = getArgNamed ("direction", arg, nArg);

		if (arg_direction != NULL)
			direction = arg_direction->i;
		else
			direction = 0;

		if (!direction)
			return FALSE;

		BananaValue *arg_window = getArgNamed ("window", arg, nArg);

		if (arg_window != NULL)
			xid = arg_window->i;
		else
			xid = 0;

		if (rs->moveWindow != xid)
		{
			CompWindow *w;

			rotateReleaseMoveWindow (s);

			if (!rs->grabIndex && !rs->moving)
			{
				w = findWindowAtScreen (s, xid);
				if (w)
				{
					if (!(w->type & (CompWindowTypeDesktopMask |
					             CompWindowTypeDockMask)))
					{
						if (!(w->state & CompWindowStateStickyMask))
						{
							rs->moveWindow  = w->id;
							rs->moveWindowX = w->attrib.x;

							if (raise)
								raiseWindow (w);
						}
					}
				}
			}
		}

		if (!rs->grabIndex)
		{
			BananaArgument argu[3];

			argu[0].type = BananaInt;
			argu[0].name = "x";
			BananaValue *arg_x = getArgNamed ("x", arg, nArg);
			if (arg_x != NULL)
				argu[0].value.i = arg_x->i;
			else
				argu[0].value.i = 0;

			argu[1].type = BananaInt;
			argu[1].name = "y";
			BananaValue *arg_y = getArgNamed ("y", arg, nArg);
			if (arg_y != NULL)
				argu[1].value.i = arg_y->i;
			else
				argu[1].value.i = 0;

			argu[2].type = BananaInt;
			argu[2].name = "root";
			argu[2].value.i = s->root;

			rotateInitiate (argu, 3);
		}

		if (rs->grabIndex)
		{
			rs->moving  = TRUE;
			rs->moveTo += (360.0f / s->hsize) * direction;
			rs->grabbed = FALSE;

			damageScreen (s);
		}
	}

	return FALSE;
}

static int
rotateRotationTo (CompScreen *s,
                  int        face)
{
	int delta;

	ROTATE_SCREEN (s);

	delta = face - s->x - (rs->moveTo / (360.0f / s->hsize));
	if (delta > s->hsize / 2)
		delta -= s->hsize;
	else if (delta < -(s->hsize / 2))
		delta += s->hsize;

	return delta;
}

static void
rotateHandleEvent (XEvent      *event)
{
	CompScreen *s;

	int i;

	ROTATE_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		s = findScreenAtDisplay (event->xkey.root);
		for (i = 1; i <= NUM_VIEWPORTS; i++)
		{
			if (isKeyPressEvent (event, &rotate_to_key[i]))
			{
				BananaArgument arg[4];

				arg[0].name = "x";
				arg[0].type = BananaInt;
				arg[0].value.i = event->xkey.x_root;

				arg[1].name = "y";
				arg[1].type = BananaInt;
				arg[1].value.i = event->xkey.y_root;

				arg[2].name = "root";
				arg[2].type = BananaInt;
				arg[2].value.i = event->xkey.root;

				arg[3].name = "direction";
				arg[3].type = BananaInt;
				arg[3].value.i = rotateRotationTo (s, i - 1);

				rotate (arg, 4);
			}
			else if (isKeyPressEvent (event, &rotate_with_window_key[i]))
			{
				BananaArgument arg[5];

				arg[0].name = "x";
				arg[0].type = BananaInt;
				arg[0].value.i = event->xkey.x_root;

				arg[1].name = "y";
				arg[1].type = BananaInt;
				arg[1].value.i = event->xkey.y_root;

				arg[2].name = "root";
				arg[2].type = BananaInt;
				arg[2].value.i = event->xkey.root;

				arg[3].name = "direction";
				arg[3].type = BananaInt;
				arg[3].value.i = rotateRotationTo (s, i - 1);

				arg[4].name = "window";
				arg[4].type = BananaInt;
				arg[4].value.i = display.activeWindow;

				rotateWithWindow (arg, 5);
			}
		}
		if (isKeyPressEvent (event, &rotate_left_key))
		{
			BananaArgument arg[4];

			arg[0].name = "x";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xkey.x_root;

			arg[1].name = "y";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xkey.y_root;

			arg[2].name = "root";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xkey.root;

			arg[3].name = "direction";
			arg[3].type = BananaInt;
			arg[3].value.i = -1;

			rotate (arg, 4);
		}
		else if (isKeyPressEvent (event, &rotate_right_key))
		{
			BananaArgument arg[4];

			arg[0].name = "x";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xkey.x_root;

			arg[1].name = "y";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xkey.y_root;

			arg[2].name = "root";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xkey.root;

			arg[3].name = "direction";
			arg[3].type = BananaInt;
			arg[3].value.i = 1;

			rotate (arg, 4);
		}
		else if (isKeyPressEvent (event, &rotate_left_window_key))
		{
			BananaArgument arg[5];

			arg[0].name = "x";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xkey.x_root;

			arg[1].name = "y";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xkey.y_root;

			arg[2].name = "root";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xkey.root;

			arg[3].name = "direction";
			arg[3].type = BananaInt;
			arg[3].value.i = -1;

			arg[4].name = "window";
			arg[4].type = BananaInt;
			arg[4].value.i = display.activeWindow;

			rotateWithWindow (arg, 5);
		}
		else if (isKeyPressEvent (event, &rotate_right_window_key))
		{
			BananaArgument arg[5];

			arg[0].name = "x";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xkey.x_root;

			arg[1].name = "y";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xkey.y_root;

			arg[2].name = "root";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xkey.root;

			arg[3].name = "direction";
			arg[3].type = BananaInt;
			arg[3].value.i = 1;

			arg[4].name = "window";
			arg[4].type = BananaInt;
			arg[4].value.i = display.activeWindow;

			rotateWithWindow (arg, 5);
		}
		break;
	case ButtonPress:
		if (isButtonPressEvent (event, &initiate_button))
		{
			BananaArgument arg[3];

			arg[0].name = "x";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xbutton.x_root;

			arg[1].name = "y";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xbutton.y_root;

			arg[2].name = "root";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xbutton.root;

			rotateInitiate (arg, 3);
		}
		else if (isButtonPressEvent (event, &rotate_left_button))
		{
			BananaArgument arg[4];

			arg[0].name = "x";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xbutton.x_root;

			arg[1].name = "y";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xbutton.y_root;

			arg[2].name = "root";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xbutton.root;

			arg[3].name = "direction";
			arg[3].type = BananaInt;
			arg[3].value.i = -1;

			rotate (arg, 4);
		}
		else if (isButtonPressEvent (event, &rotate_right_button))
		{
			BananaArgument arg[4];

			arg[0].name = "x";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xbutton.x_root;

			arg[1].name = "y";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xbutton.y_root;

			arg[2].name = "root";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xbutton.root;

			arg[3].name = "direction";
			arg[3].type = BananaInt;
			arg[3].value.i = 1;

			rotate (arg, 4);
		}
		else if (isButtonPressEvent (event, &rotate_left_window_button))
		{
			BananaArgument arg[5];

			arg[0].name = "x";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xbutton.x_root;

			arg[1].name = "y";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xbutton.y_root;

			arg[2].name = "root";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xbutton.root;

			arg[3].name = "direction";
			arg[3].type = BananaInt;
			arg[3].value.i = -1;

			arg[4].name = "window";
			arg[4].type = BananaInt;
			arg[4].value.i = display.activeWindow;

			rotateWithWindow (arg, 5);
		}
		else if (isButtonPressEvent (event, &rotate_right_window_button))
		{
			BananaArgument arg[5];

			arg[0].name = "x";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xbutton.x_root;

			arg[1].name = "y";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xbutton.y_root;

			arg[2].name = "root";
			arg[2].type = BananaInt;
			arg[2].value.i = event->xbutton.root;

			arg[3].name = "direction";
			arg[3].type = BananaInt;
			arg[3].value.i = 1;

			arg[4].name = "window";
			arg[4].type = BananaInt;
			arg[4].value.i = display.activeWindow;

			rotateWithWindow (arg, 5);
		}
		break;
	case ButtonRelease:
		if (event->xbutton.button == initiate_button.button)
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;

			rotateTerminate (&arg, 1);
		}
		break;
	case MotionNotify:
		s = findScreenAtDisplay (event->xmotion.root);
		if (s)
		{
			ROTATE_SCREEN (s);
			CUBE_SCREEN (s);

			if (rs->grabIndex)
			{
				if (rs->grabbed)
				{
					GLfloat pointerDx, pointerDy;

					pointerDx = pointerX - lastPointerX;
					pointerDy = pointerY - lastPointerY;

					if (event->xmotion.x_root < 50             ||
					    event->xmotion.y_root < 50             ||
					    event->xmotion.x_root > s->width  - 50 ||
					    event->xmotion.y_root > s->height - 50)
					{
						warpPointer (s,
						         (s->width  / 2) - pointerX,
						         (s->height / 2) - pointerY);
					}

					const BananaValue *
					option_invert_y = bananaGetOption (bananaIndex,
					                                   "invert_y",
					                                   s->screenNum);

					const BananaValue *
					option_invert_x = bananaGetOption (bananaIndex,
					                                   "invert_x",
					                                   s->screenNum);

					if (option_invert_y->b)
						pointerDy = -pointerDy;

					if (option_invert_x->b)
						pointerDx = -pointerDx;

					rs->xVelocity += pointerDx * rs->pointerSensitivity *
						cs->invert;
					rs->yVelocity += pointerDy * rs->pointerSensitivity;

					damageScreen (s);
				}
				else
				{
					rs->savedPointer.x += pointerX - lastPointerX;
					rs->savedPointer.y += pointerY - lastPointerY;
				}
			}
		}
		break;
	case ClientMessage:
		if (event->xclient.message_type == display.desktopViewportAtom)
		{
			s = findScreenAtDisplay (event->xclient.window);
			if (s)
			{
				int dx;

				ROTATE_SCREEN (s);

				if (otherScreenGrabExist (s, "rotate", "switcher", "cube", NULL))
					break;

				/* reset movement */
				rs->moveTo = 0.0f;

				dx = event->xclient.data.l[0] / s->width - s->x;
				if (dx)
				{
					Window       win;
					int          i, x, y;
					unsigned int ui;

					XQueryPointer (display.display, s->root,
					            &win, &win, &x, &y, &i, &i, &ui);

					if (dx * 2 > s->hsize)
						dx -= s->hsize;
					else if (dx * 2 < -s->hsize)
						dx += s->hsize;

					BananaArgument arg[4];

					arg[0].type    = BananaInt;
					arg[0].name    = "x";
					arg[0].value.i = x;

					arg[1].type    = BananaInt;
					arg[1].name    = "y";
					arg[1].value.i = y;

					arg[2].type    = BananaInt;
					arg[2].name    = "root";
					arg[2].value.i = s->root;

					arg[3].type    = BananaInt;
					arg[3].name    = "direction";
					arg[3].value.i = dx;

					rotate (arg, 4);
				}
			}
		}
	default:
		break;
	}

	UNWRAP (rd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (rd, &display, handleEvent, rotateHandleEvent);
}

static void
rotateActivateWindow (CompWindow *w)
{
	CompScreen *s = w->screen;

	ROTATE_SCREEN (s);

	if (w->placed &&
		!otherScreenGrabExist (s, "rotate", "switcher", "cube", NULL))
	{
		int dx;

		/* reset movement */
		rs->moveTo = 0.0f;

		defaultViewportForWindow (w, &dx, NULL);
		dx -= s->x;
		if (dx)
		{
			Window       win;
			int          i, x, y;
			unsigned int ui;

			XQueryPointer (display.display, s->root,
			               &win, &win, &x, &y, &i, &i, &ui);

			if (dx * 2 > s->hsize)
				dx -= s->hsize;
			else if (dx * 2 < -s->hsize)
				dx += s->hsize;

			BananaArgument arg[5];

			arg[0].type    = BananaInt;
			arg[0].name    = "x";
			arg[0].value.i = x;

			arg[1].type    = BananaInt;
			arg[1].name    = "y";
			arg[1].value.i = y;

			arg[2].type    = BananaInt;
			arg[2].name    = "root";
			arg[2].value.i = s->root;

			arg[3].type    = BananaInt;
			arg[3].name    = "direction";
			arg[3].value.i = dx;

			arg[4].type    = BananaBool;
			arg[4].name    = "focus_default";
			arg[4].value.b = FALSE;

			rotate (arg, 5);
		}
	}

	UNWRAP (rs, s, activateWindow);
	(*s->activateWindow) (w);
	WRAP (rs, s, activateWindow, rotateActivateWindow);
}

static void
rotateWindowGrabNotify (CompWindow   *w,
                        int          x,
                        int          y,
                        unsigned int state,
                        unsigned int mask)
{
	ROTATE_SCREEN (w->screen);

	if (!rs->grabWindow)
	{
		rs->grabMask   = mask;
		rs->grabWindow = w;
	}

	UNWRAP (rs, w->screen, windowGrabNotify);
	(*w->screen->windowGrabNotify) (w, x, y, state, mask);
	WRAP (rs, w->screen, windowGrabNotify, rotateWindowGrabNotify);
}

static void
rotateWindowUngrabNotify (CompWindow *w)
{
	ROTATE_SCREEN (w->screen);

	if (w == rs->grabWindow)
	{
		rs->grabMask   = 0;
		rs->grabWindow = NULL;
	}

	UNWRAP (rs, w->screen, windowUngrabNotify);
	(*w->screen->windowUngrabNotify) (w);
	WRAP (rs, w->screen, windowUngrabNotify, rotateWindowUngrabNotify);
}

static Bool
rotateInitDisplay (CompPlugin  *p,
                   CompDisplay *d)
{
	RotateDisplay *rd;

	rd = malloc (sizeof (RotateDisplay));
	if (!rd)
		return FALSE;

	rd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (rd->screenPrivateIndex < 0)
	{
		free (rd);
		return FALSE;
	}

	WRAP (rd, d, handleEvent, rotateHandleEvent);

	d->base.privates[displayPrivateIndex].ptr = rd;

	return TRUE;
}

static void
rotateFiniDisplay (CompPlugin  *p,
                   CompDisplay *d)
{
	ROTATE_DISPLAY (d);

	freeScreenPrivateIndex (rd->screenPrivateIndex);

	UNWRAP (rd, d, handleEvent);

	free (rd);
}

static Bool
rotateInitScreen (CompPlugin *p,
                  CompScreen *s)
{
	RotateScreen *rs;

	ROTATE_DISPLAY (&display);
	CUBE_SCREEN (s);

	rs = malloc (sizeof (RotateScreen));
	if (!rs)
		return FALSE;

	rs->grabIndex = 0;

	rs->xrot = 0.0f;
	rs->xVelocity = 0.0f;
	rs->yrot = 0.0f;
	rs->yVelocity = 0.0f;

	rs->baseXrot = 0.0f;

	rs->moving = FALSE;
	rs->moveTo = 0.0f;

	rs->moveWindow = 0;

	rs->savedPointer.x = 0;
	rs->savedPointer.y = 0;

	rs->focusDefault = TRUE;
	rs->grabbed	     = FALSE;
	rs->snapTop	     = FALSE;
	rs->snapBottom   = FALSE;

	rs->slow       = FALSE;
	rs->grabMask   = FALSE;
	rs->grabWindow = NULL;

	const BananaValue *
	option_sensitivity = bananaGetOption (bananaIndex,
	                                      "sensitivity",
	                                      s->screenNum);

	rs->pointerSensitivity = 
	      option_sensitivity->f * ROTATE_POINTER_SENSITIVITY_FACTOR;

	rs->rotateHandle = 0;

	rs->progress          = 0.0;
	rs->progressVelocity  = 0.0;

	rs->zoomTranslate = 0.0;

	WRAP (rs, s, preparePaintScreen, rotatePreparePaintScreen);
	WRAP (rs, s, donePaintScreen, rotateDonePaintScreen);
	WRAP (rs, s, paintOutput, rotatePaintOutput);
	WRAP (rs, s, windowGrabNotify, rotateWindowGrabNotify);
	WRAP (rs, s, windowUngrabNotify, rotateWindowUngrabNotify);
	WRAP (rs, s, activateWindow, rotateActivateWindow);

	WRAP (rs, cs, getRotation, rotateGetRotation);

	s->base.privates[rd->screenPrivateIndex].ptr = rs;

	return TRUE;
}

static void
rotateFiniScreen (CompPlugin *p,
                  CompScreen *s)
{
	CUBE_SCREEN (s);
	ROTATE_SCREEN (s);

	if (rs->rotateHandle)
		compRemoveTimeout (rs->rotateHandle);

	UNWRAP (rs, cs, getRotation);

	UNWRAP (rs, s, preparePaintScreen);
	UNWRAP (rs, s, donePaintScreen);
	UNWRAP (rs, s, paintOutput);
	UNWRAP (rs, s, windowGrabNotify);
	UNWRAP (rs, s, windowUngrabNotify);
	UNWRAP (rs, s, activateWindow);

	free (rs);
}
static CompBool
rotateInitObject (CompPlugin *p,
                  CompObject *o)
{
	static InitPluginObjectProc dispTab[] = {
		(InitPluginObjectProc) 0, /* InitCore */
		(InitPluginObjectProc) rotateInitDisplay,
		(InitPluginObjectProc) rotateInitScreen
	};

	RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
rotateFiniObject (CompPlugin *p,
                  CompObject *o)
{
	static FiniPluginObjectProc dispTab[] = {
		(FiniPluginObjectProc) 0, /* FiniCore */
		(FiniPluginObjectProc) rotateFiniDisplay,
		(FiniPluginObjectProc) rotateFiniScreen
	};

	DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
rotateInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("rotate", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	//get cubeDisplayPrivateIndex through the option system
	int cubeBananaIndex = bananaGetPluginIndex ("cube");
	if (cubeBananaIndex < 0)
	{
		compLogMessage ("rotate", CompLogLevelError, 
		                "bananaIndex for cube not available\n");
		freeDisplayPrivateIndex (displayPrivateIndex);
		return FALSE;
	}

	const BananaValue *
	option_index = bananaGetOption (cubeBananaIndex, "index", -1);

	cubeDisplayPrivateIndex = option_index->i;

	bananaIndex = bananaLoadPlugin ("rotate");
	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, rotateChangeNotify);

	const BananaValue *
	option_initiate_button = bananaGetOption (
	       bananaIndex, "initiate_button", -1);

	registerButton (option_initiate_button->s, &initiate_button);

	const BananaValue *
	option_rotate_left_key = bananaGetOption (
	       bananaIndex, "rotate_left_key", -1);

	registerKey (option_rotate_left_key->s, &rotate_left_key);

	const BananaValue *
	option_rotate_left_button = bananaGetOption (
	       bananaIndex, "rotate_left_button", -1);

	registerButton (option_rotate_left_button->s, &rotate_left_button);

	const BananaValue *
	option_rotate_right_key = bananaGetOption (
	       bananaIndex, "rotate_right_key", -1);

	registerKey (option_rotate_right_key->s, &rotate_right_key);

	const BananaValue *
	option_rotate_right_button = bananaGetOption (
	       bananaIndex, "rotate_right_button", -1);

	registerButton (option_rotate_right_button->s, &rotate_right_button);

	const BananaValue *
	option_rotate_left_window_key = bananaGetOption (
	       bananaIndex, "rotate_left_window_key", -1);

	registerKey (option_rotate_left_window_key->s, &rotate_left_window_key);

	const BananaValue *
	option_rotate_left_window_button = bananaGetOption (
	       bananaIndex, "rotate_left_window_button", -1);

	registerButton (option_rotate_left_window_button->s,
	                &rotate_left_window_button);

	const BananaValue *
	option_rotate_right_window_key = bananaGetOption (
	       bananaIndex, "rotate_right_window_key", -1);

	registerKey (option_rotate_right_window_key->s, &rotate_right_window_key);

	const BananaValue *
	option_rotate_right_window_button = bananaGetOption (
	       bananaIndex, "rotate_right_window_button", -1);

	registerButton (option_rotate_right_window_button->s,
	                &rotate_right_window_button);

	int i;
	for (i = 1; i <= NUM_VIEWPORTS; i++) //viewports start with 1
	{
		char optionName[50];
		const BananaValue *option;

		sprintf (optionName, "rotate_to_key%d", i);
		option = bananaGetOption (bananaIndex, optionName, -1);
		registerKey (option->s, &rotate_to_key[i]);

		sprintf (optionName, "rotate_with_window_key%d", i);
		option = bananaGetOption (bananaIndex, optionName, -1);
		registerKey (option->s, &rotate_with_window_key[i]);
	}
	return TRUE;
}

static void
rotateFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

CompPluginVTable rotateVTable = {
	"rotate",
	rotateInit,
	rotateFini,
	rotateInitObject,
	rotateFiniObject
};

CompPluginVTable *
getCompPluginInfo20140724 (void)
{
	return &rotateVTable;
}

#if 0
static Bool
rotateFlipLeft (void *closure)
{
	printf("rotateFlipLeft\n");
	CompScreen *s = closure;
	int        warpX;
	CompOption o[4];

	ROTATE_SCREEN (s);

	rs->moveTo = 0.0f;
	rs->slow = FALSE;

	if (otherScreenGrabExist (s, "rotate", "move", "group-drag", NULL))
		return FALSE;

	warpX = pointerX + s->width;
	warpPointer (s, s->width - 10, 0);
	lastPointerX = warpX;

	BananaArgument arg[4];

	arg[0].type    = BananaInt;
	arg[0].name    = "x";
	arg[0].value.i = 0;

	arg[1].type    = BananaInt;
	arg[1].name    = "y";
	arg[1].value.i = pointerY;

	arg[2].type    = BananaInt;
	arg[2].name    = "root";
	arg[2].value.i = s->root;

	arg[3].type    = BananaInt;
	arg[3].name    = "direction";
	arg[3].value.i = -1;

	rotate (arg, 4);

	XWarpPointer (s->display->display, None, None, 0, 0, 0, 0, -1, 0);
	rs->savedPointer.x = lastPointerX - 9;

	rs->rotateHandle = 0;

	return FALSE;
}
static Bool
rotateFlipRight (void *closure)
{
	printf("rotateFlipRight\n");
	CompScreen *s = closure;
	int        warpX;
	CompOption o[4];

	ROTATE_SCREEN (s);

	rs->moveTo = 0.0f;
	rs->slow = FALSE;

	if (otherScreenGrabExist (s, "rotate", "move", "group-drag", NULL))
		return FALSE;

	warpX = pointerX - s->width;
	warpPointer (s, 10 - s->width, 0);
	lastPointerX = warpX;

	BananaArgument arg[4];

	arg[0].type    = BananaInt;
	arg[0].name    = "x";
	arg[0].value.i = 0;

	arg[1].type    = BananaInt;
	arg[1].name    = "y";
	arg[1].value.i = pointerY;

	arg[2].type    = BananaInt;
	arg[2].name    = "root";
	arg[2].value.i = s->root;

	arg[3].type    = BananaInt;
	arg[3].name    = "direction";
	arg[3].value.i = 1;

	rotate (arg, 4);

	XWarpPointer (s->display->display, None, None, 0, 0, 0, 0, 1, 0);

	rs->savedPointer.x = lastPointerX + 9;

	rs->rotateHandle = 0;

	return FALSE;
}
#endif

#if 0
static void
rotateEdgeFlip (CompScreen      *s,
                int             edge,
                CompAction      *action,
                CompActionState state,
                CompOption      *option,
                int             nOption)
{
	CompOption o[4];

	ROTATE_DISPLAY (s->display);

	if (s->hsize < 2)
		return;

	if (otherScreenGrabExist (s, "rotate", "move", "group-drag", NULL))
		return;

	if (state & CompActionStateInitEdgeDnd)
	{
		if (!rd->opt[ROTATE_DISPLAY_OPTION_EDGEFLIP_DND].value.b)
			return;

		if (otherScreenGrabExist (s, "rotate", NULL))
			return;
	}
	else if (otherScreenGrabExist (s, "rotate", "group-drag", NULL))
	{
		ROTATE_SCREEN (s);

		if (!rd->opt[ROTATE_DISPLAY_OPTION_EDGEFLIP_WINDOW].value.b)
			return;

		if (!rs->grabWindow)
			return;

		/* bail out if window is horizontally maximized, fullscreen,
		   or sticky */
		if (rs->grabWindow->state & (CompWindowStateMaximizedHorzMask |
		                         CompWindowStateFullscreenMask |
		                         CompWindowStateStickyMask))
			return;
	}
	else if (otherScreenGrabExist (s, "rotate", NULL))
	{
		/* in that case, 'group-drag' must be the active screen grab */
		if (!rd->opt[ROTATE_DISPLAY_OPTION_EDGEFLIP_WINDOW].value.b)
			return;
	}
	else
	{
		if (!rd->opt[ROTATE_DISPLAY_OPTION_EDGEFLIP_POINTER].value.b)
			return;
	}

	o[0].type    = CompOptionTypeInt;
	o[0].name    = "x";
	o[0].value.i = 0;

	o[1].type    = CompOptionTypeInt;
	o[1].name    = "y";
	o[1].value.i = pointerY;

	o[2].type    = CompOptionTypeInt;
	o[2].name    = "root";
	o[2].value.i = s->root;

	o[3].type    = CompOptionTypeInt;
	o[3].name    = "direction";

	if (edge == SCREEN_EDGE_LEFT)
	{
		int flipTime = rd->opt[ROTATE_DISPLAY_OPTION_FLIPTIME].value.i;

		ROTATE_SCREEN (s);

		if (flipTime == 0 || (rs->moving && !rs->slow))
		{
			int pointerDx = pointerX - lastPointerX;
			int warpX;

			warpX = pointerX + s->width;
			warpPointer (s, s->width - 10, 0);
			lastPointerX = warpX - pointerDx;

			o[3].value.i = -1;

			rotate (s->display, NULL, 0, o, 4);

			XWarpPointer (s->display->display, None, None,
			              0, 0, 0, 0, -1, 0);
			rs->savedPointer.x = lastPointerX - 9;
		}
		else
		{
			if (!rs->rotateHandle)
			{
				int flipTime = rd->opt[ROTATE_DISPLAY_OPTION_FLIPTIME].value.i;

				rs->rotateHandle = compAddTimeout (flipTime,
				                       (float) flipTime * 1.2,
				                       rotateFlipLeft, s);
			}

			rs->moving  = TRUE;
			rs->moveTo -= 360.0f / s->hsize;
			rs->slow    = TRUE;

			if (state & CompActionStateInitEdge)
				action->state |= CompActionStateTermEdge;

			if (state & CompActionStateInitEdgeDnd)
				action->state |= CompActionStateTermEdgeDnd;

			damageScreen (s);
		}
	}
	else
	{
		int flipTime = rd->opt[ROTATE_DISPLAY_OPTION_FLIPTIME].value.i;

		ROTATE_SCREEN (s);

		if (flipTime == 0 || (rs->moving && !rs->slow))
		{
			int pointerDx = pointerX - lastPointerX;
			int warpX;

			warpX = pointerX - s->width;
			warpPointer (s, 10 - s->width, 0);
			lastPointerX = warpX - pointerDx;

			o[3].value.i = 1;

			rotate (s->display, NULL, 0, o, 4);

			XWarpPointer (s->display->display, None, None,
			              0, 0, 0, 0, 1, 0);
			rs->savedPointer.x = lastPointerX + 9;
		}
		else
		{
			if (!rs->rotateHandle)
			{
				int flipTime = rd->opt[ROTATE_DISPLAY_OPTION_FLIPTIME].value.i;

				rs->rotateHandle =
					compAddTimeout (flipTime, (float) flipTime * 1.2,
					                rotateFlipRight, s);
			}

			rs->moving  = TRUE;
			rs->moveTo += 360.0f / s->hsize;
			rs->slow    = TRUE;

			if (state & CompActionStateInitEdge)
				action->state |= CompActionStateTermEdge;

			if (state & CompActionStateInitEdgeDnd)
				action->state |= CompActionStateTermEdgeDnd;

			damageScreen (s);
		}
	}
}


static Bool
rotateFlipTerminate (CompDisplay     *d,
                     CompAction      *action,
                     CompActionState state,
                     CompOption      *option,
                     int             nOption)
{
	CompScreen *s;
	Window     xid;

	xid = getIntOptionNamed (option, nOption, "root", 0);

	for (s = d->screens; s; s = s->next)
	{
		ROTATE_SCREEN (s);

		if (xid && s->root != xid)
			continue;

		if (rs->rotateHandle)
		{
			compRemoveTimeout (rs->rotateHandle);
			rs->rotateHandle = 0;

			if (rs->slow)
			{
				rs->moveTo = 0.0f;
				rs->slow = FALSE;
			}

			damageScreen (s);
		}

		action->state &= ~(CompActionStateTermEdge |
		                   CompActionStateTermEdgeDnd);
	}

	return FALSE;
}

static Bool
rotateEdgeFlipLeft (CompDisplay     *d,
                    CompAction      *action,
                    CompActionState state,
                    CompOption      *option,
                    int             nOption)
{
	CompScreen *s;
	Window     xid;

	xid = getIntOptionNamed (option, nOption, "root", 0);

	s = findScreenAtDisplay (d, xid);
	if (s)
		rotateEdgeFlip (s, SCREEN_EDGE_LEFT, action, state, option, nOption);

	return FALSE;
}

static Bool
rotateEdgeFlipRight (CompDisplay     *d,
                     CompAction      *action,
                     CompActionState state,
                     CompOption      *option,
                     int             nOption)
{
	CompScreen *s;
	Window     xid;

	xid = getIntOptionNamed (option, nOption, "root", 0);

	s = findScreenAtDisplay (d, xid);
	if (s)
		rotateEdgeFlip (s, SCREEN_EDGE_RIGHT, action, state, option, nOption);

	return FALSE;
}
#endif