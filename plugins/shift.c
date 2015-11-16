/*
 *
 * Compiz shift switcher plugin
 *
 * shift.c
 *
 * Copyright : (C) 2007 by Dennis Kasprzyk
 * E-mail    : onestone@opencompositing.org
 *
 *
 * Based on ring.c:
 * Copyright : (C) 2007 by Danny Baumann
 * E-mail    : maniac@opencompositing.org
 *
 * Based on scale.c and switcher.c:
 * Copyright : (C) 2007 David Reveman
 * E-mail    : davidr@novell.com
 *
 * Rounded corner drawing taken from wall.c:
 * Copyright : (C) 2007 Robert Carr
 * E-mail    : racarr@beryl-project.org
 *
 * Port to fusilli
 * Copyright : (C) 2015 by Michail Bitzes
 * E-mail    : noodlylight@gmail.com
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include <X11/Xatom.h>
#include <X11/cursorfont.h>

#include <fusilli-core.h>
#include <fusilli-text.h>

typedef enum {
	ShiftStateNone = 0,
	ShiftStateOut,
	ShiftStateSwitching,
	ShiftStateFinish,
	ShiftStateIn
} ShiftState;

typedef enum {
	ShiftTypeNormal = 0,
	ShiftTypeAll
} ShiftType;

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _ShiftSlot {
	int x, y;          /* thumb center coordinates */
	float z;
	float scale;       /* size scale (fit to maximal thumb size */
	float opacity;
	float rotation;

	GLfloat tx;
	GLfloat ty;

	Bool primary;
} ShiftSlot;

typedef struct _ShiftDrawSlot {
	CompWindow *w;
	ShiftSlot  *slot;
	float      distance;
} ShiftDrawSlot;

typedef struct _ShiftDisplay {
	int screenPrivateIndex;

	HandleEventProc handleEvent;

	KeyCode leftKey;
	KeyCode rightKey;
	KeyCode upKey;
	KeyCode downKey;

	CompKeyBinding initiate_key, initiate_all_key, next_key, prev_key,
	               next_all_key, prev_all_key;

	CompButtonBinding initiate_button, initiate_all_button, terminate_button;
} ShiftDisplay;

typedef struct _ShiftScreen {
	int windowPrivateIndex;

	PreparePaintScreenProc preparePaintScreen;
	DonePaintScreenProc donePaintScreen;
	PaintScreenProc paintScreen;
	PaintOutputProc paintOutput;
	PaintWindowProc paintWindow;
	DamageWindowRectProc damageWindowRect;

	int grabIndex;

	ShiftState state;
	ShiftType type;

	Bool moreAdjust;
	Bool moveAdjust;

	float mvTarget;
	float mvAdjust;
	GLfloat mvVelocity;
	Bool invert;

	Cursor cursor;

	/* only used for sorting */
	CompWindow   **windows;
	int windowsSize;
	int nWindows;

	ShiftDrawSlot *drawSlots;
	int slotsSize;
	int nSlots;
	ShiftDrawSlot *activeSlot;

	Window clientLeader;

	CompWindow *selectedWindow;

	/* text display support */
	CompTextData *textData;

	CompMatch window_match;

	CompOutput *output;
	int usedOutput;

	float anim;
	float animVelocity;

	float reflectBrightness;
	Bool reflectActive;

	int buttonPressTime;
	Bool buttonPressed;
	int startX;
	int startY;
	float startTarget;
	float lastTitle;

	Bool paintingAbove;

	Bool canceled;

	int maxThumbWidth;
	int maxThumbHeight;
} ShiftScreen;

typedef struct _ShiftWindow {
	ShiftSlot slots[2];

	float opacity;
	float brightness;
	float opacityVelocity;
	float brightnessVelocity;

	Bool active;
} ShiftWindow;

#define PI 3.1415926
#define ICON_SIZE 64
#define MAX_ICON_SIZE 256

#define GET_SHIFT_DISPLAY(d) \
	((ShiftDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define SHIFT_DISPLAY(d) \
	ShiftDisplay *sd = GET_SHIFT_DISPLAY (d)

#define GET_SHIFT_SCREEN(s, sd) \
	((ShiftScreen *) (s)->privates[(sd)->screenPrivateIndex].ptr)

#define SHIFT_SCREEN(s) \
	ShiftScreen *ss = GET_SHIFT_SCREEN (s, GET_SHIFT_DISPLAY (&display))

#define GET_SHIFT_WINDOW(w, ss) \
	((ShiftWindow *) (w)->privates[(ss)->windowPrivateIndex].ptr)

#define SHIFT_WINDOW(w) \
	ShiftWindow *sw = GET_SHIFT_WINDOW  (w, \
	                                     GET_SHIFT_SCREEN  (w->screen, \
	                                                        GET_SHIFT_DISPLAY (&display)))

static Bool
isShiftWin (CompWindow *w)
{
	SHIFT_SCREEN (w->screen);

	if (w->destroyed)
		return FALSE;

	if (w->attrib.override_redirect)
		return FALSE;

	if (w->wmType & (CompWindowTypeDockMask | CompWindowTypeDesktopMask))
		return FALSE;

	const BananaValue *
	option_minimized = bananaGetOption (bananaIndex,
	                                    "minimized",
	                                    w->screen->screenNum);

	if (!w->mapNum || w->attrib.map_state != IsViewable)
	{
		if (option_minimized->b)
		{
			if (!w->minimized && !w->inShowDesktopMode && !w->shaded)
				return FALSE;
		}
		else
			return FALSE;
	}

	if (ss->type == ShiftTypeNormal)
	{
		if (!w->mapNum || w->attrib.map_state != IsViewable)
		{
			if (w->serverX + w->width  <= 0    ||
			    w->serverY + w->height <= 0    ||
			    w->serverX >= w->screen->width ||
			    w->serverY >= w->screen->height)
				return FALSE;
		}
		else
		{
			if (!(*w->screen->focusWindow)(w))
				return FALSE;
		}
	}

	if (w->state & CompWindowStateSkipTaskbarMask)
		return FALSE;

	if (!matchEval (&ss->window_match, w))
		return FALSE;

	return TRUE;
}

static void
shiftFreeWindowTitle (CompScreen *s)
{
	SHIFT_SCREEN (s);

	if (!ss->textData)
		return;

	textFiniTextData (s, ss->textData);
	ss->textData = NULL;
}

static void
shiftRenderWindowTitle (CompScreen *s)
{
	CompTextAttrib tA;
	int ox1, ox2, oy1, oy2;

	SHIFT_SCREEN (s);

	shiftFreeWindowTitle (s);

	const BananaValue *
	option_window_title = bananaGetOption (bananaIndex,
	                                       "window_title",
	                                       s->screenNum);

	if (!option_window_title->b)
		return;

	const BananaValue *
	option_multioutput_mode = bananaGetOption (bananaIndex,
	                                           "multioutput_mode",
	                                           s->screenNum);

	if (option_multioutput_mode->i == 2) //one big switcher
	{
		ox1 = oy1 = 0;
		ox2 = s->width;
		oy2 = s->height;
	}
	else
		getCurrentOutputExtents (s, &ox1, &oy1, &ox2, &oy2);

	const BananaValue *
	option_title_font_size = bananaGetOption (bananaIndex,
	                                          "title_font_size",
	                                          s->screenNum);

	const BananaValue *
	option_title_font_color = bananaGetOption (bananaIndex,
	                                           "title_font_color",
	                                           s->screenNum);

	unsigned short font_color[] = { 0, 0, 0, 0 };

	stringToColor (option_title_font_color->s, font_color);

	const BananaValue *
	option_title_back_color = bananaGetOption (bananaIndex,
	                                           "title_back_color",
	                                           s->screenNum);
	const BananaValue *
	option_title_font_family = bananaGetOption (bananaIndex,
	                                          "title_font_family",
	                                          s->screenNum);

	unsigned short back_color[] = { 0, 0, 0, 0 };

	stringToColor (option_title_back_color->s, back_color);

	/* 75% of the output device as maximum width */
	tA.maxWidth = (ox2 - ox1) * 3 / 4;
	tA.maxHeight = 100;

	tA.family = option_title_font_family->s;
	tA.size = option_title_font_size->i;
	tA.color[0] = font_color[0];
	tA.color[1] = font_color[1];
	tA.color[2] = font_color[2];
	tA.color[3] = font_color[3];

	const BananaValue *
	option_title_font_bold = bananaGetOption (bananaIndex,
	                                          "title_font_bold",
	                                          s->screenNum);

	tA.flags = CompTextFlagWithBackground | CompTextFlagEllipsized;
	if (option_title_font_bold->b)
		tA.flags |= CompTextFlagStyleBold;

	tA.bgHMargin = 15;
	tA.bgVMargin = 15;
	tA.bgColor[0] = back_color[0];
	tA.bgColor[1] = back_color[1];
	tA.bgColor[2] = back_color[2];
	tA.bgColor[3] = back_color[3];

	ss->textData = textRenderWindowTitle (s,
	                                      ss->selectedWindow,
	                                      ss->type == ShiftTypeAll,
	                                      &tA);
}

static void
shiftDrawWindowTitle (CompScreen *s)
{
	float height, border = 10.0f;
	int ox1, ox2, oy1, oy2;

	SHIFT_SCREEN (s);

	//width = ss->textData->width;
	height = ss->textData->height;

	const BananaValue *
	option_multioutput_mode = bananaGetOption (bananaIndex,
	                                           "multioutput_mode",
	                                           s->screenNum);

	if (option_multioutput_mode->i == 2) //one big switcher
	{
		ox1 = oy1 = 0;
		ox2 = s->width;
		oy2 = s->height;
	}
	else
	{
		ox1 = s->outputDev[ss->usedOutput].region.extents.x1;
		ox2 = s->outputDev[ss->usedOutput].region.extents.x2;
		oy1 = s->outputDev[ss->usedOutput].region.extents.y1;
		oy2 = s->outputDev[ss->usedOutput].region.extents.y2;
	}

	float x = ox1 + ((ox2 - ox1) / 2) - ((int)ss->textData->width / 2);
	float y;

	const BananaValue *
	option_title_text_placement = bananaGetOption (bananaIndex,
	                                               "title_text_placement",
	                                               s->screenNum);

	/* assign y (for the lower corner!) according to the setting */
	switch (option_title_text_placement->i)
	{
	case 0: //centered on screen
		y = oy1 + ((oy2 - oy1) / 2) + (height / 2);
		break;
	case 1: //above
	case 2: //below
	{
		XRectangle workArea;
		getWorkareaForOutput (s, s->currentOutputDev, &workArea);

		if (option_title_text_placement->i == 1) //above
			y = oy1 + workArea.y + (2 * border) + height;
		else
			y = oy1 + workArea.y + workArea.height - (2 * border);
	}
	break;
	default:
		return;
	}

	textDrawText (s, ss->textData, floor (x), floor (y), 1.0f);
}

static Bool
shiftPaintWindow (CompWindow             *w,
                  const WindowPaintAttrib *attrib,
                  const CompTransform     *transform,
                  Region                  region,
                  unsigned int            mask)
{
	CompScreen *s = w->screen;
	Bool status;

	SHIFT_SCREEN (s);
	SHIFT_WINDOW (w);

	if (ss->state != ShiftStateNone && !ss->paintingAbove)
	{
		WindowPaintAttrib sAttrib = *attrib;
		Bool scaled = FALSE;

		if (w->mapNum)
		{
			if (!w->texture->pixmap && !w->bindFailed)
				bindWindow (w);
		}


		if (sw->active)
			scaled = (ss->activeSlot != NULL);

		if (sw->opacity > 0.01 && (ss->activeSlot == NULL))
		{
			sAttrib.brightness = sAttrib.brightness * sw->brightness;
			sAttrib.opacity = sAttrib.opacity * sw->opacity;
		}
		else
			mask |= PAINT_WINDOW_NO_CORE_INSTANCE_MASK;

		if (sw->active &&
		    (ss->output->id == ss->usedOutput || ss->output->id == ~0))
			mask |= PAINT_WINDOW_NO_CORE_INSTANCE_MASK;


		UNWRAP (ss, s, paintWindow);
		status = (*s->paintWindow)(w, &sAttrib, transform, region, mask);
		WRAP (ss, s, paintWindow, shiftPaintWindow);

		if (scaled && w->texture->pixmap)
		{
			FragmentAttrib fragment;
			CompTransform wTransform = *transform;
			ShiftSlot      *slot = ss->activeSlot->slot;

			float sx     = ss->anim * slot->tx;
			float sy     = ss->anim * slot->ty;
			float sz     = ss->anim * slot->z;
			float srot   = (ss->anim * slot->rotation);
			float anim   = MIN (1.0, MAX (0.0, ss->anim));

			float sscale;
			float sopacity;


			if (slot->primary)
				sscale = (ss->anim * slot->scale) + (1 - ss->anim);
			else
				sscale = ss->anim * slot->scale;

			if (slot->primary && !ss->reflectActive)
				sopacity = (ss->anim * slot->opacity) + (1 - ss->anim);
			else
				sopacity = anim * anim * slot->opacity;

			if (sopacity <= 0.05)
				return status;

			if (mask & PAINT_WINDOW_OCCLUSION_DETECTION_MASK)
				return FALSE;

			initFragmentAttrib (&fragment, attrib);

			fragment.opacity    = (float)fragment.opacity * sopacity;
			fragment.brightness = (float)fragment.brightness *
			                      ss->reflectBrightness;

			if (w->alpha || fragment.opacity != OPAQUE)
				mask |= PAINT_WINDOW_TRANSLUCENT_MASK;

			matrixTranslate (&wTransform, sx, sy, sz);

			matrixTranslate (&wTransform,
			                 w->attrib.x + (w->attrib.width  * sscale / 2),
			                 w->attrib.y + (w->attrib.height  * sscale / 2.0),
			                 0.0f);

			matrixScale (&wTransform, ss->output->width, -ss->output->height,
			             1.0f);

			matrixRotate (&wTransform, srot, 0.0, 1.0, 0.0);

			matrixScale (&wTransform, 1.0f  / ss->output->width,
			             -1.0f / ss->output->height, 1.0f);

			matrixScale (&wTransform, sscale, sscale, 1.0f);
			matrixTranslate (&wTransform, -w->attrib.x - (w->width / 2),
			                 -w->attrib.y - (w->attrib.height / 2), 0.0f);

			glPushMatrix ();
			glLoadMatrixf (wTransform.m);

			(*s->drawWindow)(w, &wTransform, &fragment, region,
			                 mask | PAINT_WINDOW_TRANSFORMED_MASK);

			glPopMatrix ();
		}

		const BananaValue *
		option_overlay_icon = bananaGetOption (bananaIndex,
		                                       "overlay_icon",
		                                       s->screenNum);

		if (scaled && ((option_overlay_icon->i != 0) || //overlay icon none
		               !w->texture->pixmap))
		{
			CompIcon *icon;

			icon = getWindowIcon (w, MAX_ICON_SIZE, MAX_ICON_SIZE);
			if (!icon)
				icon = w->screen->defaultIcon;

			if (icon && (icon->texture.name || iconToTexture (w->screen, icon)))
			{
				REGION iconReg;
				float scale;
				float x, y;
				int width, height;
				int scaledWinWidth, scaledWinHeight;
				int iconOverlay = option_overlay_icon->i;
				ShiftSlot      *slot = ss->activeSlot->slot;

				float sx       = ss->anim * slot->tx;
				float sy       = ss->anim * slot->ty;
				float sz       = ss->anim * slot->z;
				float srot     = (ss->anim * slot->rotation);
				float sopacity = ss->anim * slot->opacity;

				float sscale;

				if (slot->primary)
					sscale = (ss->anim * slot->scale) + (1 - ss->anim);
				else
					sscale = ss->anim * ss->anim * slot->scale;

				scaledWinWidth  = w->attrib.width  * sscale;
				scaledWinHeight = w->attrib.height * sscale;

				if (!w->texture->pixmap)
					iconOverlay = 2; //overlay icon big

				switch (iconOverlay)
				{
				case 0: //overlay icon none
				case 1: //overlay icon emblem
					scale = MIN (((float) ICON_SIZE / icon->width),
					             ((float) ICON_SIZE / icon->height));
					break;
				case 2: //overlay icon big
				default:
					if (w->texture->pixmap)
					{
						/* only change opacity if not painting an
						   icon for a minimized window */
						sAttrib.opacity /= 3;
						scale = MIN (((float) scaledWinWidth / icon->width),
						             ((float) scaledWinHeight / icon->height));
					}
					else
					{
						scale = MIN (((float) 128 / icon->width),
						             ((float) 128 / icon->height));
					}
					break;
				}

				width  = icon->width  * scale;
				height = icon->height * scale;

				switch (iconOverlay)
				{
				case 0: //overlay icon none
				case 1: //overlay icon emblem
					x = scaledWinWidth - width;
					y = scaledWinHeight - height;
					break;
				case 2: //overlay icon big
				default:
					x = scaledWinWidth / 2 - width / 2;
					if (w->texture->pixmap)
						y = scaledWinHeight / 2 - height / 2;
					else
						y = scaledWinHeight - height;
					break;
				}

				mask |= PAINT_WINDOW_BLEND_MASK | PAINT_WINDOW_TRANSFORMED_MASK;

				iconReg.rects    = &iconReg.extents;
				iconReg.numRects = 1;

				iconReg.extents.x1 = 0;
				iconReg.extents.y1 = 0;
				iconReg.extents.x2 = icon->width;
				iconReg.extents.y2 = icon->height;

				w->vCount = w->indexCount = 0;
				(*w->screen->addWindowGeometry)(w, &icon->texture.matrix, 1,
				                                &iconReg, &infiniteRegion);

				if (w->vCount)
				{
					FragmentAttrib fragment;
					CompTransform wTransform = *transform;

					if (!w->texture->pixmap)
						sAttrib.opacity = w->paint.opacity;

					initFragmentAttrib (&fragment, &sAttrib);

					fragment.opacity = (float)fragment.opacity * sopacity;
					fragment.brightness = (float)fragment.brightness *
					                      ss->reflectBrightness;

					matrixTranslate (&wTransform, sx, sy, sz);

					matrixTranslate (&wTransform, w->attrib.x +
					                 (w->width  * sscale / 2),
					                 w->attrib.y +
					                 (w->attrib.height  * sscale / 2.0), 0.0f);

					matrixScale (&wTransform, ss->output->width,
					             -ss->output->height, 1.0f);

					matrixRotate (&wTransform, srot, 0.0, 1.0, 0.0);

					matrixScale (&wTransform, 1.0f  / ss->output->width,
					             -1.0f / ss->output->height, 1.0f);

					matrixTranslate (&wTransform, x -
					                 (w->attrib.width * sscale / 2), y -
					                 (w->attrib.height * sscale / 2.0), 0.0f);
					matrixScale (&wTransform, scale, scale, 1.0f);

					glPushMatrix ();
					glLoadMatrixf (wTransform.m);

					(*w->screen->drawWindowTexture)(w,
					                                &icon->texture, &fragment,
					                                mask);

					glPopMatrix ();
				}
			}
		}

	}
	else
	{
		WindowPaintAttrib sAttrib = *attrib;

		if (ss->paintingAbove)
		{
			sAttrib.opacity = sAttrib.opacity * (1.0 - ss->anim);

			if (ss->anim > 0.99)
				mask |= PAINT_WINDOW_NO_CORE_INSTANCE_MASK;
		}

		UNWRAP (ss, s, paintWindow);
		status = (*s->paintWindow)(w, &sAttrib, transform, region, mask);
		WRAP (ss, s, paintWindow, shiftPaintWindow);
	}

	return status;
}

static void
shiftActivateEvent (CompScreen *s,
                    Bool       activating)
{
	BananaArgument arg[2];

	arg[0].type = BananaInt;
	arg[0].name = "root";
	arg[0].value.i = s->root;

	arg[1].type = BananaBool;
	arg[1].name = "active";
	arg[1].value.b = activating;

	(*display.handleFusilliEvent) ("shift", "activate", arg, 2);
}

static int
compareWindows (const void *elem1,
                const void *elem2)
{
	CompWindow *w1 = *((CompWindow **) elem1);
	CompWindow *w2 = *((CompWindow **) elem2);
	CompWindow *w  = w1;

	if (w1 == w2)
		return 0;

	if (!w1->shaded && w1->attrib.map_state != IsViewable &&
	    (w2->shaded || w2->attrib.map_state == IsViewable))
	{
		return 1;
	}

	if (!w2->shaded && w2->attrib.map_state != IsViewable &&
	    (w1->shaded || w1->attrib.map_state == IsViewable))
	{
		return -1;
	}

	while (w)
	{
		if (w == w2)
			return 1;
		w = w->next;
	}
	return -1;
}

static int
compareShiftWindowDistance (const void *elem1,
                            const void *elem2)
{
	float a1   = ((ShiftDrawSlot *) elem1)->distance;
	float a2   = ((ShiftDrawSlot *) elem2)->distance;
	float ab   = fabs (a1 - a2);

	if (ab > 0.3 && a1 > a2)
		return -1;
	else if (ab > 0.3 && a1 < a2)
		return 1;
	else
		return compareWindows (&((ShiftDrawSlot *) elem2)->w,
		                       &((ShiftDrawSlot *) elem1)->w);
}

static Bool
layoutThumbsCover (CompScreen *s)
{
	SHIFT_SCREEN (s);
	CompWindow *w;
	int index;
	int ww, wh;
	float xScale, yScale;
	float distance;
	int i;

	int ox1, ox2, oy1, oy2;

	const BananaValue *
	option_multioutput_mode = bananaGetOption (bananaIndex,
	                                           "multioutput_mode",
	                                           s->screenNum);

	if (option_multioutput_mode->i == 2) //one big switcher
	{
		ox1 = oy1 = 0;
		ox2 = s->width;
		oy2 = s->height;
	}
	else
	{
		ox1 = s->outputDev[ss->usedOutput].region.extents.x1;
		ox2 = s->outputDev[ss->usedOutput].region.extents.x2;
		oy1 = s->outputDev[ss->usedOutput].region.extents.y1;
		oy2 = s->outputDev[ss->usedOutput].region.extents.y2;
	}

	/* the center of the ellipse is in the middle
	   of the used output device */
	int centerX = ox1 + (ox2 - ox1) / 2;
	int centerY = oy1 + (oy2 - oy1) / 2;

	const BananaValue *
	option_size = bananaGetOption (bananaIndex,
	                               "size",
	                               s->screenNum);

	ss->maxThumbWidth  = (ox2 - ox1) * option_size->i / 100;
	ss->maxThumbHeight = (oy2 - oy1) * option_size->i / 100;

	for (index = 0; index < ss->nWindows; index++)
	{
		w = ss->windows[index];
		SHIFT_WINDOW (w);

		ww = w->attrib.width  + w->input.left + w->input.right;
		wh = w->attrib.height + w->input.top  + w->input.bottom;

		if (ww > ss->maxThumbWidth)
			xScale = (float)(ss->maxThumbWidth) / (float)ww;
		else
			xScale = 1.0f;

		if (wh > ss->maxThumbHeight)
			yScale = (float)(ss->maxThumbHeight) / (float)wh;
		else
			yScale = 1.0f;


		float val1 = floor((float)ss->nWindows / 2.0);

		float pos;
		float space = ss->maxThumbWidth / 2;
		space *= cos (sin (PI / 4) * PI / 3);
		space *= 2;
		//space += (space / sin (PI / 4)) - space;

		for (i = 0; i < 2; i++)
		{
			const BananaValue *
			option_cover_offset = bananaGetOption (bananaIndex,
			                                       "cover_offset",
			                                       s->screenNum);

			if (ss->invert ^ (i == 0))
			{
				distance = ss->mvTarget - index;
				distance += option_cover_offset->f;
			}
			else
			{
				distance = ss->mvTarget - index + ss->nWindows;
				distance += option_cover_offset->f;
				if (distance > ss->nWindows)
					distance -= ss->nWindows * 2;
			}

			pos = MIN (1.0, MAX (-1.0, distance));

			sw->slots[i].opacity = 1.0 - MIN (1.0,
			                                  MAX (0.0, fabs(distance) - val1));
			sw->slots[i].scale   = MIN (xScale, yScale);

			sw->slots[i].y = centerY + (ss->maxThumbHeight / 2.0) -
			                 (((w->attrib.height / 2.0) + w->input.bottom) *
			                  sw->slots[i].scale);

			if (fabs(distance) < 1.0)
			{
				sw->slots[i].x  = centerX + (sin(pos * PI * 0.5) * space);
				sw->slots[i].z  = fabs (distance);
				sw->slots[i].z *= -(ss->maxThumbWidth / (2.0 * (ox2 - ox1)));

				sw->slots[i].rotation = sin(pos * PI * 0.5) * -60;
			}
			else
			{
				float rad = (space / (ox2 - ox1)) / sin(PI / 6.0);

				float ang = (PI / MAX(72.0, ss->nWindows * 2)) *
				            (distance - pos) + (pos * (PI / 6.0));

				sw->slots[i].x  = centerX;
				sw->slots[i].x += sin(ang) * rad * (ox2 - ox1);

				sw->slots[i].rotation  = 90;
				sw->slots[i].rotation -= fabs(ang) * 180.0 / PI;
				sw->slots[i].rotation *= -pos;

				sw->slots[i].z  = -(ss->maxThumbWidth / (2.0 * (ox2 - ox1)));
				sw->slots[i].z += -(cos(PI / 6.0) * rad);
				sw->slots[i].z += (cos(ang) * rad);
			}

			ss->drawSlots[index * 2 + i].w     = w;
			ss->drawSlots[index * 2 + i].slot  = &sw->slots[i];
			ss->drawSlots[index * 2 + i].distance = fabs(distance);

		}

		if (ss->drawSlots[index * 2].distance >
		    ss->drawSlots[index * 2 + 1].distance)
		{
			ss->drawSlots[index * 2].slot->primary     = FALSE;
			ss->drawSlots[index * 2 + 1].slot->primary = TRUE;
		}
		else
		{
			ss->drawSlots[index * 2].slot->primary     = TRUE;
			ss->drawSlots[index * 2 + 1].slot->primary = FALSE;
		}

	}

	ss->nSlots = ss->nWindows * 2;

	qsort (ss->drawSlots, ss->nSlots, sizeof (ShiftDrawSlot),
	       compareShiftWindowDistance);

	return TRUE;
}

static Bool
layoutThumbsFlip (CompScreen *s)
{
	SHIFT_SCREEN (s);

	CompWindow *w;
	int index;
	int ww, wh;
	float xScale, yScale;
	float distance;
	int i;
	float angle;
	int slotNum;

	int ox1, ox2, oy1, oy2;

	const BananaValue *
	option_multioutput_mode = bananaGetOption (bananaIndex,
	                                           "multioutput_mode",
	                                           s->screenNum);

	if (option_multioutput_mode->i == 2) //one big switcher
	{
		ox1 = oy1 = 0;
		ox2 = s->width;
		oy2 = s->height;
	}
	else
	{
		ox1 = s->outputDev[ss->usedOutput].region.extents.x1;
		ox2 = s->outputDev[ss->usedOutput].region.extents.x2;
		oy1 = s->outputDev[ss->usedOutput].region.extents.y1;
		oy2 = s->outputDev[ss->usedOutput].region.extents.y2;
	}

	/* the center of the ellipse is in the middle
	   of the used output device */
	int centerX = ox1 + (ox2 - ox1) / 2;
	int centerY = oy1 + (oy2 - oy1) / 2;

	const BananaValue *
	option_size = bananaGetOption (bananaIndex,
	                               "size",
	                               s->screenNum);

	ss->maxThumbWidth  = (ox2 - ox1) * option_size->i / 100;
	ss->maxThumbHeight = (oy2 - oy1) * option_size->i / 100;

	slotNum = 0;

	for (index = 0; index < ss->nWindows; index++)
	{
		w = ss->windows[index];
		SHIFT_WINDOW (w);

		ww = w->attrib.width  + w->input.left + w->input.right;
		wh = w->attrib.height + w->input.top  + w->input.bottom;

		if (ww > ss->maxThumbWidth)
			xScale = (float)(ss->maxThumbWidth) / (float)ww;
		else
			xScale = 1.0f;

		if (wh > ss->maxThumbHeight)
			yScale = (float)(ss->maxThumbHeight) / (float)wh;
		else
			yScale = 1.0f;

		const BananaValue *
		option_flip_rotation = bananaGetOption (bananaIndex,
		                                        "flip_rotation",
		                                        s->screenNum);

		angle = option_flip_rotation->i * PI / 180.0;

		for (i = 0; i < 2; i++)
		{

			if (ss->invert ^ (i == 0))
				distance = ss->mvTarget - index;
			else
			{
				distance = ss->mvTarget - index + ss->nWindows;
				if (distance > 1.0)
					distance -= ss->nWindows * 2;
			}

			if (distance > 0.0)
				sw->slots[i].opacity = MAX (0.0, 1.0 - (distance * 1.0));
			else
			{
				if (distance < -(ss->nWindows - 1))
					sw->slots[i].opacity = MAX (0.0, ss->nWindows +
					                            distance);
				else
					sw->slots[i].opacity = 1.0;
			}

			if (distance > 0.0 && w != ss->selectedWindow)
				sw->slots[i].primary = FALSE;
			else
				sw->slots[i].primary = TRUE;

			sw->slots[i].scale   = MIN (xScale, yScale);

			sw->slots[i].y = centerY + (ss->maxThumbHeight / 2.0) -
			                 (((w->attrib.height / 2.0) + w->input.bottom) *
			                  sw->slots[i].scale);

			sw->slots[i].x  = sin(angle) * distance * (ss->maxThumbWidth / 2);
			if (distance > 0 && FALSE)
				sw->slots[i].x *= 1.5;
			sw->slots[i].x += centerX;

			sw->slots[i].z  = cos(angle) * distance;
			if (distance > 0)
				sw->slots[i].z *= 1.5;
			sw->slots[i].z *= (ss->maxThumbWidth / (2.0 * (ox2 - ox1)));

			const BananaValue *
			option_flip_rotation = bananaGetOption (bananaIndex,
			                                        "flip_rotation",
			                                        s->screenNum);

			sw->slots[i].rotation = option_flip_rotation->i;

			if (sw->slots[i].opacity > 0.0)
			{
				ss->drawSlots[slotNum].w     = w;
				ss->drawSlots[slotNum].slot  = &sw->slots[i];
				ss->drawSlots[slotNum].distance = -distance;
				slotNum++;
			}
		}
	}

	ss->nSlots = slotNum;

	qsort (ss->drawSlots, ss->nSlots, sizeof (ShiftDrawSlot),
	       compareShiftWindowDistance);

	return TRUE;
}


static Bool
layoutThumbs (CompScreen *s)
{
	Bool result = FALSE;

	SHIFT_SCREEN (s);

	if (ss->state == ShiftStateNone)
		return FALSE;

	const BananaValue *
	option_mode = bananaGetOption (bananaIndex,
	                               "mode",
	                               s->screenNum);

	switch (option_mode->i)
	{
	case 0: //mode cover
		result = layoutThumbsCover (s);
		break;
	case 1: //mode flip
		result = layoutThumbsFlip (s);
		break;
	}

	if (ss->state == ShiftStateIn)
		return FALSE;

	return result;
}


static void
shiftAddWindowToList (CompScreen *s,
                      CompWindow *w)
{
	SHIFT_SCREEN (s);

	if (ss->windowsSize <= ss->nWindows)
	{
		ss->windows = realloc (ss->windows,
		                       sizeof (CompWindow *) * (ss->nWindows + 32));
		if (!ss->windows)
			return;

		ss->windowsSize = ss->nWindows + 32;
	}

	if (ss->slotsSize <= ss->nWindows * 2)
	{
		ss->drawSlots = realloc (ss->drawSlots,
		                         sizeof (ShiftDrawSlot) *
		                         ((ss->nWindows * 2) + 64));

		if (!ss->drawSlots)
			return;

		ss->slotsSize = (ss->nWindows * 2) + 64;
	}

	ss->windows[ss->nWindows++] = w;
}

static Bool
shiftUpdateWindowList (CompScreen *s)
{
	SHIFT_SCREEN (s);

	int i, idx;
	CompWindow **wins;

	qsort (ss->windows, ss->nWindows, sizeof (CompWindow *), compareWindows);

	ss->mvTarget = 0;
	ss->mvAdjust = 0;
	ss->mvVelocity = 0;
	for (i = 0; i < ss->nWindows; i++)
	{
		if (ss->windows[i] == ss->selectedWindow)
			break;

		ss->mvTarget++;
	}
	if (ss->mvTarget == ss->nWindows)
		ss->mvTarget = 0;

	/* create spetial window order to create a good animation
	   A,B,C,D,E --> A,B,D,E,C to get B,D,E,C,(A),B,D,E,C as initial state */
	const BananaValue *
	option_mode = bananaGetOption (bananaIndex,
	                               "mode",
	                               s->screenNum);

	if (option_mode->i == 0) //mode cover
	{
		wins = malloc(ss->nWindows * sizeof (CompWindow *));
		if (!wins)
			return FALSE;

		memcpy (wins, ss->windows, ss->nWindows * sizeof (CompWindow *));

		for (i = 0; i < ss->nWindows; i++)
		{
			idx = ceil (i * 0.5);
			idx *= (i & 1) ? 1 : -1;

			if (idx < 0)
				idx += ss->nWindows;

			ss->windows[idx] = wins[i];
		}

		free (wins);
	}

	return layoutThumbs (s);
}

static Bool
shiftCreateWindowList (CompScreen *s)
{
	CompWindow *w;

	SHIFT_SCREEN (s);

	ss->nWindows = 0;

	for (w = s->windows; w; w = w->next)
	{
		if (isShiftWin (w))
		{
			SHIFT_WINDOW (w);

			shiftAddWindowToList (s, w);
			sw->active = TRUE;
		}
	}

	return shiftUpdateWindowList (s);
}

static void
switchToWindow (CompScreen *s,
                Bool       toNext)
{
	CompWindow *w;
	int cur;

	SHIFT_SCREEN (s);

	if (!ss->grabIndex)
		return;

	for (cur = 0; cur < ss->nWindows; cur++)
	{
		if (ss->windows[cur] == ss->selectedWindow)
			break;
	}

	if (cur == ss->nWindows)
		return;

	if (toNext)
		w = ss->windows[(cur + 1) % ss->nWindows];
	else
		w = ss->windows[(cur + ss->nWindows - 1) % ss->nWindows];

	if (w)
	{
		CompWindow *old = ss->selectedWindow;
		ss->selectedWindow = w;

		if (old != w)
		{
			if (toNext)
				ss->mvAdjust += 1;
			else
				ss->mvAdjust -= 1;

			ss->moveAdjust = TRUE;
			damageScreen (s);
			shiftRenderWindowTitle (s);
		}
	}
}

static int
shiftCountWindows (CompScreen *s)
{
	CompWindow *w;
	int count = 0;

	for (w = s->windows; w; w = w->next)
	{
		if (isShiftWin (w))
			count++;
	}

	return count;
}

static int adjustShiftMovement (CompScreen *s,
                                float      chunk)
{
	float dx, adjust, amount;
	float change;

	SHIFT_SCREEN(s);

	dx = ss->mvAdjust;

	adjust = dx * 0.15f;
	amount = fabs(dx) * 1.5f;
	if (amount < 0.2f)
		amount = 0.2f;
	else if (amount > 2.0f)
		amount = 2.0f;

	ss->mvVelocity = (amount * ss->mvVelocity + adjust) / (amount + 1.0f);

	if (fabs (dx) < 0.002f && fabs (ss->mvVelocity) < 0.004f)
	{
		ss->mvVelocity = 0.0f;
		ss->mvTarget = ss->mvTarget + ss->mvAdjust;
		ss->mvAdjust = 0;
		layoutThumbs (s);
		return FALSE;
	}

	change = ss->mvVelocity * chunk;
	if (!change)
	{
		if (ss->mvVelocity)
			change = (ss->mvAdjust > 0) ? 0.01 : -0.01;
	}

	ss->mvAdjust -= change;
	ss->mvTarget += change;

	while (ss->mvTarget >= ss->nWindows)
	{
		ss->mvTarget -= ss->nWindows;
		ss->invert = !ss->invert;
	}

	while (ss->mvTarget < 0)
	{
		ss->mvTarget += ss->nWindows;
		ss->invert = !ss->invert;
	}

	if (!layoutThumbs (s))
		return FALSE;

	return TRUE;
}

static Bool
adjustShiftWindowAttribs (CompWindow *w,
                          float      chunk)
{
	float dp, db, adjust, amount;
	float opacity, brightness;

	SHIFT_WINDOW (w);
	SHIFT_SCREEN (w->screen);

	const BananaValue *
	option_hide_all = bananaGetOption (bananaIndex,
	                                   "hide_all",
	                                   w->screen->screenNum);

	if ((sw->active && ss->state != ShiftStateIn &&
	     ss->state != ShiftStateNone) ||
	    (option_hide_all->b && !(w->type & CompWindowTypeDesktopMask) &&
	     (ss->state == ShiftStateOut || ss->state == ShiftStateSwitching ||
	      ss->state == ShiftStateFinish)))
		opacity = 0.0;
	else
		opacity = 1.0;

	const BananaValue *
	option_background_intensity = bananaGetOption (bananaIndex,
	                                               "background_intensity",
	                                               w->screen->screenNum);

	if (ss->state == ShiftStateIn || ss->state == ShiftStateNone)
		brightness = 1.0;
	else
		brightness = option_background_intensity->f;

	dp = opacity - sw->opacity;
	adjust = dp * 0.1f;
	amount = fabs (dp) * 7.0f;
	if (amount < 0.01f)
		amount = 0.01f;
	else if (amount > 0.15f)
		amount = 0.15f;

	sw->opacityVelocity = (amount * sw->opacityVelocity + adjust) /
	                      (amount + 1.0f);

	db = brightness - sw->brightness;
	adjust = db * 0.1f;
	amount = fabs (db) * 7.0f;
	if (amount < 0.01f)
		amount = 0.01f;
	else if (amount > 0.15f)
		amount = 0.15f;

	sw->brightnessVelocity = (amount * sw->brightnessVelocity + adjust) /
	                         (amount + 1.0f);

	if (fabs (dp) < 0.01f && fabs (sw->opacityVelocity) < 0.02f &&
	    fabs (db) < 0.01f && fabs (sw->brightnessVelocity) < 0.02f)
	{

		sw->brightness = brightness;
		sw->opacity = opacity;
		return FALSE;
	}

	sw->brightness += sw->brightnessVelocity * chunk;
	sw->opacity += sw->opacityVelocity * chunk;

	return TRUE;
}

static Bool
adjustShiftAnimationAttribs (CompScreen *s,
                             float      chunk)
{
	float dr, adjust, amount;
	float anim;

	SHIFT_SCREEN (s);

	if (ss->state != ShiftStateIn && ss->state != ShiftStateNone)
		anim = 1.0;
	else
		anim = 0.0;

	dr = anim - ss->anim;
	adjust = dr * 0.1f;
	amount = fabs (dr) * 7.0f;
	if (amount < 0.002f)
		amount = 0.002f;
	else if (amount > 0.15f)
		amount = 0.15f;

	ss->animVelocity = (amount * ss->animVelocity + adjust) /
	                   (amount + 1.0f);

	if (fabs (dr) < 0.002f && fabs (ss->animVelocity) < 0.004f)
	{

		ss->anim = anim;
		return FALSE;
	}

	ss->anim += ss->animVelocity * chunk;
	return TRUE;
}

static Bool
shiftPaintOutput (CompScreen              *s,
                  const ScreenPaintAttrib *sAttrib,
                  const CompTransform      *transform,
                  Region                   region,
                  CompOutput               *output,
                  unsigned int             mask)
{
	Bool status;

	SHIFT_SCREEN (s);

	if (ss->state != ShiftStateNone)
		mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;

	ss->paintingAbove = FALSE;

	ss->output = output;

	UNWRAP (ss, s, paintOutput);
	status = (*s->paintOutput)(s, sAttrib, transform, region, output, mask);
	WRAP (ss, s, paintOutput, shiftPaintOutput);

	if (ss->state != ShiftStateNone &&
	    (output->id == ss->usedOutput || output->id == ~0))
	{
		const BananaValue *
		option_size = bananaGetOption (bananaIndex,
		                               "size",
		                               s->screenNum);

		const BananaValue *
		option_multioutput_mode = bananaGetOption (bananaIndex,
		                                           "multioutput_mode",
		                                           s->screenNum);

		const BananaValue *
		option_reflection = bananaGetOption (bananaIndex,
		                                     "reflection",
		                                     s->screenNum);

		CompWindow    *w;
		CompTransform sTransform = *transform;
		int i;
		int oy1 = s->outputDev[ss->usedOutput].region.extents.y1;
		int oy2 = s->outputDev[ss->usedOutput].region.extents.y2;
		int maxThumbHeight = (oy2 - oy1) * option_size->i / 100;
		int oldFilter = display.textureFilter;

		if (option_multioutput_mode->i == 2) //one big switcher
		{
			oy1 = 0;
			oy2 = s->height;
		}

		transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

		GLdouble clip[4] = { 0.0, -1.0, 0.0, 0.0 };

		clip[3] = ((oy1 + (oy2 - oy1) / 2)) + (maxThumbHeight / 2.0);

		if (option_reflection->b)
		{
			CompTransform rTransform = sTransform;
			unsigned short color[4];
			int cull, cullInv;
			glGetIntegerv (GL_CULL_FACE_MODE, &cull);
			cullInv = (cull == GL_BACK) ? GL_FRONT : GL_BACK;

			matrixTranslate (&rTransform, 0.0, oy1 + oy2 + maxThumbHeight,
			                 0.0);
			matrixScale (&rTransform, 1.0, -1.0, 1.0);

			glPushMatrix ();
			glLoadMatrixf (rTransform.m);

			glCullFace (cullInv);

			const BananaValue *
			option_mipmaps = bananaGetOption (bananaIndex,
			                                  "mipmaps",
			                                  s->screenNum);

			if (option_mipmaps->b)
				display.textureFilter = GL_LINEAR_MIPMAP_LINEAR;

			if (ss->anim == 1.0)
			{
				glClipPlane (GL_CLIP_PLANE0, clip);
				glEnable (GL_CLIP_PLANE0);
			}

			const BananaValue *
			option_intensity = bananaGetOption (bananaIndex,
			                                    "intensity",
			                                    s->screenNum);

			ss->reflectActive = TRUE;
			ss->reflectBrightness = option_intensity->f;
			for (i = 0; i < ss->nSlots; i++)
			{
				w = ss->drawSlots[i].w;

				ss->activeSlot = &ss->drawSlots[i];
				{
					(*s->paintWindow) (w, &w->paint, &rTransform,
					                   &infiniteRegion, 0);
				}
			}

			glDisable (GL_CLIP_PLANE0);
			glCullFace (cull);

			glLoadIdentity();
			glTranslatef (0.0, 0.0, -DEFAULT_Z_CAMERA);

			glEnable (GL_BLEND);
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			glBegin (GL_QUADS);
			glColor4f (0.0, 0.0, 0.0, 0.0);
			glVertex2f (0.5, 0.0);
			glVertex2f (-0.5, 0.0);
			glColor4f (0.0, 0.0, 0.0,
			           MIN (1.0, 1.0 - option_intensity->f) * 2.0 *
			           ss->anim);
			glVertex2f (-0.5, -0.5);
			glVertex2f (0.5, -0.5);
			glEnd();

			const BananaValue *
			option_ground_size = bananaGetOption (bananaIndex,
			                                      "ground_size",
			                                      s->screenNum);

			if (option_ground_size->f > 0.0)
			{
				const BananaValue *
				option_ground_color1 = bananaGetOption (bananaIndex,
				                                        "ground_color1",
				                                        s->screenNum);

				unsigned short ground_color1[] = { 0, 0, 0, 0 };

				stringToColor (option_ground_color1->s, ground_color1);

				const BananaValue *
				option_ground_color2 = bananaGetOption (bananaIndex,
				                                        "ground_color2",
				                                        s->screenNum);

				unsigned short ground_color2[] = { 0, 0, 0, 0 };

				stringToColor (option_ground_color2->s, ground_color2);

				glBegin (GL_QUADS);
				color[0] = ground_color1[0];
				color[1] = ground_color1[1];
				color[2] = ground_color1[2];
				color[3] = (float) ground_color1[3] * ss->anim;
				glColor4usv (color);
				glVertex2f (-0.5, -0.5);
				glVertex2f (0.5, -0.5);
				color[0] = ground_color2[0];
				color[1] = ground_color2[1];
				color[2] = ground_color2[2];
				color[3] = (float) ground_color2[3] * ss->anim;
				glColor4usv (color);
				glVertex2f (0.5, -0.5 + option_ground_size->f);
				glVertex2f (-0.5, -0.5 + option_ground_size->f);
				glEnd();
			}

			glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_BLEND);
			glColor4f (1.0, 1.0, 1.0, 1.0);
			glPopMatrix ();
		}

		glPushMatrix ();
		glLoadMatrixf (sTransform.m);

		if (option_reflection->b && ss->anim == 1.0)
		{
			glClipPlane (GL_CLIP_PLANE0, clip);
			glEnable (GL_CLIP_PLANE0);
		}

		ss->reflectBrightness = 1.0;
		ss->reflectActive     = FALSE;

		for (i = 0; i < ss->nSlots; i++)
		{
			w = ss->drawSlots[i].w;

			ss->activeSlot = &ss->drawSlots[i];
			{
				(*s->paintWindow)(w, &w->paint, &sTransform,
				                  &infiniteRegion, 0);
			}
		}

		glDisable (GL_CLIP_PLANE0);

		ss->activeSlot = NULL;

		display.textureFilter = oldFilter;

		if (ss->textData && (ss->state != ShiftStateIn))
			shiftDrawWindowTitle (s);

		if (ss->state == ShiftStateIn || ss->state == ShiftStateOut)
		{
			Bool found;
			ss->paintingAbove = TRUE;

			for (w = ss->selectedWindow; w; w = w->next)
			{
				if (w->destroyed)
					continue;

				if (!w->shaded)
				{
					if (w->attrib.map_state != IsViewable || !w->damaged)
						continue;
				}
				found = FALSE;
				for (i = 0; i < ss->nWindows; i++)
					if (ss->windows[i] == w)
						found = TRUE;
				if (found)
					continue;
				(*s->paintWindow)(w, &w->paint, &sTransform,
				                  &infiniteRegion, 0);
			}

			ss->paintingAbove = FALSE;
		}

		glPopMatrix ();
	}

	return status;
}

static void
shiftPaintScreen (CompScreen   *s,
                  CompOutput   *outputs,
                  int          numOutputs,
                  unsigned int mask)
{
	SHIFT_SCREEN (s);

	const BananaValue *
	option_multioutput_mode = bananaGetOption (bananaIndex,
	                                           "multioutput_mode",
	                                           s->screenNum);

	if (ss->state != ShiftStateNone && numOutputs > 0 &&
	    option_multioutput_mode->i != 0) //multioutput mode disabled
	{
		outputs = &s->fullscreenOutput;
		numOutputs = 1;
	}

	UNWRAP (ss, s, paintScreen);
	(*s->paintScreen)(s, outputs, numOutputs, mask);
	WRAP (ss, s, paintScreen, shiftPaintScreen);
}

static void
shiftPreparePaintScreen (CompScreen *s,
                         int        msSinceLastPaint)
{
	SHIFT_SCREEN (s);

	if (ss->state != ShiftStateNone &&
	    (ss->moreAdjust || ss->moveAdjust))
	{
		CompWindow *w;
		int steps;
		float amount, chunk;
		int i;

		const BananaValue *
		option_shift_speed = bananaGetOption (bananaIndex,
		                                      "shift_speed",
		                                      s->screenNum);

		const BananaValue *
		option_timestep = bananaGetOption (bananaIndex,
		                                   "timestep",
		                                   s->screenNum);

		amount = msSinceLastPaint * 0.05f * option_shift_speed->f;
		steps  = amount / (0.5f * option_timestep->f);

		if (!steps)
			steps = 1;
		chunk  = amount / (float) steps;

		while (steps--)
		{
			ss->moveAdjust = adjustShiftMovement (s, chunk);
			if (!ss->moveAdjust)
				break;
		}

		const BananaValue *
		option_speed = bananaGetOption (bananaIndex,
		                                "speed",
		                                s->screenNum);

		amount = msSinceLastPaint * 0.05f * option_speed->f;
		steps  = amount / (0.5f * option_timestep->f);

		if (!steps)
			steps = 1;
		chunk  = amount / (float) steps;

		while (steps--)
		{
			ss->moreAdjust = adjustShiftAnimationAttribs (s, chunk);

			for (w = s->windows; w; w = w->next)
			{
				SHIFT_WINDOW (w);

				ss->moreAdjust |= adjustShiftWindowAttribs (w, chunk);
				for (i = 0; i < 2; i++)
				{
					ShiftSlot *slot = &sw->slots[i];
					slot->tx = slot->x - w->attrib.x -
					           (w->attrib.width * slot->scale) / 2;
					slot->ty = slot->y - w->attrib.y -
					           (w->attrib.height * slot->scale) / 2;
				}
			}

			if (!ss->moreAdjust)
				break;
		}
	}

	UNWRAP (ss, s, preparePaintScreen);
	(*s->preparePaintScreen)(s, msSinceLastPaint);
	WRAP (ss, s, preparePaintScreen, shiftPreparePaintScreen);
}

static Bool
canStackRelativeTo (CompWindow *w)
{
	if (w->attrib.override_redirect)
		return FALSE;

	if (!w->shaded && !w->pendingMaps)
	{
		if (w->attrib.map_state != IsViewable || w->mapNum == 0)
			return FALSE;
	}

	return TRUE;
}

static void
shiftDonePaintScreen (CompScreen *s)
{
	SHIFT_SCREEN (s);
	CompWindow *w;

	if (ss->state != ShiftStateNone)
	{
		if (ss->moreAdjust)
		{
			damageScreen (s);
		}
		else
		{
			if (ss->state == ShiftStateIn)
			{
				ss->state = ShiftStateNone;
				shiftActivateEvent(s, FALSE);

				for (w = s->windows; w; w = w->next)
				{
					SHIFT_WINDOW (w);
					sw->active = FALSE;
				}
				damageScreen (s);
			}
			else if (ss->state == ShiftStateOut)
				ss->state = ShiftStateSwitching;

			if (ss->moveAdjust)
				damageScreen (s);
		}

		if (ss->state == ShiftStateFinish)
		{
			CompWindow *w;

			CompWindow *pw = NULL;
			int i;

			ss->state = ShiftStateIn;
			ss->moreAdjust = TRUE;
			damageScreen (s);

			if (!ss->canceled && ss->mvTarget != 0)
				for (i = 0; i < ss->nSlots; i++)
				{
					w = ss->drawSlots[i].w;
					if (ss->drawSlots[i].slot->primary && canStackRelativeTo (w))
					{
						if (pw)
							restackWindowAbove (w,pw);
						pw = w;
					}
				}

			if (!ss->canceled && ss->selectedWindow &&
			    !ss->selectedWindow->destroyed)
			{
				sendWindowActivationRequest (s, ss->selectedWindow->id);
			}
		}

		if (ss->state == ShiftStateNone)
			shiftActivateEvent (s, FALSE);
	}

	UNWRAP (ss, s, donePaintScreen);
	(*s->donePaintScreen)(s);
	WRAP (ss, s, donePaintScreen, shiftDonePaintScreen);
}

static void
shiftTerm (CompScreen *s,
           Bool       cancel)
{
	SHIFT_SCREEN (s);

	if (ss->grabIndex)
	{
		removeScreenGrab (s, ss->grabIndex, 0);
		ss->grabIndex = 0;
	}

	if (ss->state != ShiftStateNone &&
	    ss->state != ShiftStateFinish &&
	    ss->state != ShiftStateIn)
	{
		if (cancel && ss->mvTarget != 0)
		{
			if (ss->nWindows - ss->mvTarget > ss->mvTarget)
				ss->mvAdjust = -ss->mvTarget;
			else
				ss->mvAdjust = ss->nWindows - ss->mvTarget;
			ss->moveAdjust = TRUE;
		}

		ss->moreAdjust = TRUE;
		ss->state = ShiftStateFinish;
		ss->canceled = cancel;
		damageScreen (s);
	}
}

static Bool
shiftTerminate (Window xid,
                Bool   cancel)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
	{
		if (xid && s->root != xid)
			continue;

		shiftTerm (s, cancel);
	}

	return FALSE;
}

static Bool
shiftInitiateScreen (CompScreen *s)
{
	int count;

	SHIFT_SCREEN (s);

	if (otherScreenGrabExist (s, "shift", NULL))
		return FALSE;

	count = shiftCountWindows (s);

	if (count < 1)
		return FALSE;

	if (!ss->grabIndex)
		ss->grabIndex = pushScreenGrab (s, s->invisibleCursor, "shift");

	if (ss->grabIndex)
	{
		ss->state = ShiftStateOut;

		shiftActivateEvent (s, TRUE);

		if (!shiftCreateWindowList (s))
			return FALSE;

		ss->selectedWindow = ss->windows[0];
		shiftRenderWindowTitle (s);
		ss->mvTarget = 0;
		ss->mvAdjust = 0;
		ss->mvVelocity = 0;

		ss->moreAdjust = TRUE;
		damageScreen (s);

		shiftActivateEvent (s, TRUE);
	}

	ss->usedOutput = s->currentOutputDev;

	return TRUE;
}

static Bool
shiftDoSwitch (Window    xid,
               Bool      nextWindow,
               ShiftType type)
{
	CompScreen *s;
	Bool       ret = TRUE;

	s = findScreenAtDisplay (xid);
	if (s)
	{
		SHIFT_SCREEN (s);

		if ((ss->state == ShiftStateNone) || (ss->state == ShiftStateIn))
		{
			ss->type = type;
			ret = shiftInitiateScreen (s);
		}

		if (ret)
			switchToWindow (s, nextWindow);
	}

	return ret;
}

static Bool
shiftInitiate (Window xid)
{
	CompScreen *s;

	Bool ret = TRUE;

	s = findScreenAtDisplay (xid);
	if (s)
	{
		SHIFT_SCREEN (s);

		ss->type = ShiftTypeNormal;

		if ((ss->state == ShiftStateNone) || (ss->state == ShiftStateIn) ||
		    (ss->state == ShiftStateFinish))
			ret = shiftInitiateScreen (s);
		else
			ret = shiftTerminate (xid, FALSE);
	}

	return ret;
}

static Bool
shiftInitiateAll (Window xid)
{
	CompScreen *s;

	Bool ret = TRUE;

	s = findScreenAtDisplay (xid);
	if (s)
	{
		SHIFT_SCREEN (s);

		ss->type = ShiftTypeAll;

		if ((ss->state == ShiftStateNone) || (ss->state == ShiftStateIn) ||
		    (ss->state == ShiftStateFinish))
			ret = shiftInitiateScreen (s);
		else
			ret = shiftTerminate (xid, FALSE);
	}

	return ret;
}

static void
shiftWindowRemove (CompWindow *w)
{
	if (w)
	{
		Bool inList = FALSE;
		int j, i = 0;
		CompWindow *selected;

		SHIFT_SCREEN (w->screen);

		if (ss->state == ShiftStateNone)
			return;

		if (isShiftWin (w))
			return;

		selected = ss->selectedWindow;

		while (i < ss->nWindows)
		{
			if (w == ss->windows[i])
			{
				inList = TRUE;

				if (w == selected)
				{
					if (i < (ss->nWindows - 1))
						selected = ss->windows[i + 1];
					else
						selected = ss->windows[0];

					ss->selectedWindow = selected;
					shiftRenderWindowTitle (w->screen);
				}

				ss->nWindows--;
				for (j = i; j < ss->nWindows; j++)
					ss->windows[j] = ss->windows[j + 1];
			}
			else
			{
				i++;
			}
		}

		if (!inList)
			return;

		if (ss->nWindows == 0)
		{
			shiftTerminate (w->screen->root, FALSE);
			return;
		}

		// Let the window list be updated to avoid crash
		// when a window is closed while ending shift (ShiftStateIn).
		if (!ss->grabIndex && ss->state != ShiftStateIn)
			return;

		if (shiftUpdateWindowList (w->screen))
		{
			ss->moreAdjust = TRUE;
			ss->state = ShiftStateOut;
			damageScreen (w->screen);
		}
	}
}

static void
shiftHandleEvent (XEvent *event)
{
	SHIFT_DISPLAY (&display);

	CompScreen *s;
	CompWindow *w = NULL;

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &sd->initiate_key))
		{
			shiftInitiate (event->xkey.root);
		}
		else if (isKeyPressEvent (event, &sd->initiate_all_key))
		{
			shiftInitiateAll (event->xkey.root);
		}
		else if (isKeyPressEvent (event, &sd->prev_key))
		{
			shiftDoSwitch (event->xkey.root, FALSE, ShiftTypeNormal);
		}
		else if (isKeyPressEvent (event, &sd->prev_all_key))
		{
			shiftDoSwitch (event->xkey.root, FALSE, ShiftTypeAll);
		}
		else if (isKeyPressEvent (event, &sd->next_key))
		{
			shiftDoSwitch (event->xkey.root, TRUE, ShiftTypeNormal);
		}
		else if (isKeyPressEvent (event, &sd->next_all_key))
		{
			shiftDoSwitch (event->xkey.root, TRUE, ShiftTypeAll);
		}
		break;
	case ButtonPress:
		if (isButtonPressEvent (event, &sd->terminate_button))
		{
			shiftTerminate (event->xbutton.root, FALSE);
		}
		else if (isButtonPressEvent (event, &sd->initiate_button))
		{
			shiftInitiate (event->xbutton.root);
		}
		else if (isButtonPressEvent (event, &sd->initiate_all_button))
		{
			shiftInitiateAll (event->xbutton.root);
		}
		break;
	case DestroyNotify:
		/* We need to get the CompWindow * for event->xdestroywindow.window
		   here because in the (*d->handleEvent) call below, that CompWindow's
		   id will become 1, so findWindowAtDisplay won't be able to find the
		   CompWindow after that. */
		w = findWindowAtDisplay (event->xdestroywindow.window);
		break;
	}

	UNWRAP (sd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (sd, &display, handleEvent, shiftHandleEvent);

	switch (event->type) {
	case PropertyNotify:
		if (event->xproperty.atom == XA_WM_NAME)
		{
			w = findWindowAtDisplay (event->xproperty.window);
			if (w)
			{
				SHIFT_SCREEN (w->screen);

				if (ss->grabIndex && (w == ss->selectedWindow))
				{
					shiftRenderWindowTitle (w->screen);
					damageScreen (w->screen);
				}
			}
		}
		break;
	case UnmapNotify:
		w = findWindowAtDisplay (event->xunmap.window);
		shiftWindowRemove (w);
		break;
	case DestroyNotify:
		shiftWindowRemove (w);
		break;
	case KeyPress:
		s = findScreenAtDisplay (event->xkey.root);

		if (s)
		{
			SHIFT_SCREEN (s);

			if (ss->state == ShiftStateSwitching)
			{
				if (event->xkey.keycode == sd->leftKey)
					switchToWindow (s, FALSE);
				else if (event->xkey.keycode == sd->rightKey)
					switchToWindow (s, TRUE);
				else if (event->xkey.keycode == sd->upKey)
					switchToWindow (s, FALSE);
				else if (event->xkey.keycode == sd->downKey)
					switchToWindow (s, TRUE);
			}
		}

		break;
	case ButtonPress:
		s = findScreenAtDisplay (event->xbutton.root);

		if (s)
		{
			SHIFT_SCREEN (s);

			if (ss->state == ShiftStateSwitching || ss->state == ShiftStateOut)
			{
				if (event->xbutton.button == Button5)
					switchToWindow (s, FALSE);
				else if (event->xbutton.button == Button4)
					switchToWindow (s, TRUE);
				if (event->xbutton.button == Button1)
				{
					ss->buttonPressTime = event->xbutton.time;
					ss->buttonPressed   = TRUE;
					ss->startX          = event->xbutton.x_root;
					ss->startY          = event->xbutton.y_root;
					ss->startTarget     = ss->mvTarget + ss->mvAdjust;
				}
			}
		}
		break;
	case ButtonRelease:
		s = findScreenAtDisplay (event->xbutton.root);

		if (s)
		{
			SHIFT_SCREEN (s);

			if (ss->state == ShiftStateSwitching || ss->state == ShiftStateOut)
			{
				if (event->xbutton.button == Button1 && ss->buttonPressed)
				{
					const BananaValue *
					option_click_duration = bananaGetOption (bananaIndex,
					                                         "click_duration",
					                                         s->screenNum);

					int new;

					if ((int)(event->xbutton.time - ss->buttonPressTime) <
					    option_click_duration->i)
						shiftTerm (s, FALSE);

					ss->buttonPressTime = 0;
					ss->buttonPressed   = FALSE;

					if (ss->mvTarget - floor (ss->mvTarget) >= 0.5)
					{
						ss->mvAdjust = ceil (ss->mvTarget) - ss->mvTarget;
						new = ceil (ss->mvTarget);
					}
					else
					{
						ss->mvAdjust = floor (ss->mvTarget) - ss->mvTarget;
						new = floor (ss->mvTarget);
					}

					while (new < 0)
						new += ss->nWindows;
					new = new % ss->nWindows;

					ss->selectedWindow = ss->windows[new];

					shiftRenderWindowTitle (s);
					ss->moveAdjust = TRUE;
					damageScreen (s);
				}

			}
		}
		break;
	case MotionNotify:
		s = findScreenAtDisplay (event->xbutton.root);

		if (s)
		{
			SHIFT_SCREEN (s);

			if (ss->state == ShiftStateSwitching || ss->state == ShiftStateOut)
			{
				if (ss->buttonPressed)
				{
					int ox1 = s->outputDev[ss->usedOutput].region.extents.x1;
					int ox2 = s->outputDev[ss->usedOutput].region.extents.x2;
					int oy1 = s->outputDev[ss->usedOutput].region.extents.y1;
					int oy2 = s->outputDev[ss->usedOutput].region.extents.y2;

					float div = 0;
					int wx  = 0;
					int wy  = 0;
					int new;

					const BananaValue *
					option_mode = bananaGetOption (bananaIndex,
					                               "mode",
					                               s->screenNum);

					const BananaValue *
					option_mouse_speed = bananaGetOption (bananaIndex,
					                                      "mouse_speed",
					                                      s->screenNum);

					switch (option_mode->i)
					{
					case 0: //mode cover
						div = event->xmotion.x_root - ss->startX;
						div /= (ox2 - ox1) / option_mouse_speed->f;
						break;
					case 1: //mode flip
						div = event->xmotion.y_root - ss->startY;
						div /= (oy2 - oy1) / option_mouse_speed->f;
						break;
					}

					ss->mvTarget = ss->startTarget + div - ss->mvAdjust;
					ss->moveAdjust = TRUE;
					while (ss->mvTarget >= ss->nWindows)
					{
						ss->mvTarget -= ss->nWindows;
						ss->invert = !ss->invert;
					}

					while (ss->mvTarget < 0)
					{
						ss->mvTarget += ss->nWindows;
						ss->invert = !ss->invert;
					}

					if (ss->mvTarget - floor (ss->mvTarget) >= 0.5)
						new = ceil(ss->mvTarget);
					else
						new = floor(ss->mvTarget);

					while (new < 0)
						new += ss->nWindows;
					new = new % ss->nWindows;

					if (ss->selectedWindow != ss->windows[new])
					{
						ss->selectedWindow = ss->windows[new];
						shiftRenderWindowTitle (s);
					}

					if (event->xmotion.x_root < 50)
						wx = 50;
					if (s->width - event->xmotion.x_root < 50)
						wx = -50;
					if (event->xmotion.y_root < 50)
						wy = 50;
					if (s->height - event->xmotion.y_root < 50)
						wy = -50;
					if (wx != 0 || wy != 0)
					{
						warpPointer (s, wx, wy);
						ss->startX += wx;
						ss->startY += wy;
					}

					damageScreen(s);
				}

			}
		}
		break;
	}
}

static Bool
shiftDamageWindowRect (CompWindow *w,
                       Bool       initial,
                       BoxPtr     rect)
{
	Bool status = FALSE;

	SHIFT_SCREEN (w->screen);

	if (initial)
	{
		if (ss->grabIndex && isShiftWin (w))
		{
			shiftAddWindowToList (w->screen, w);
			if (shiftUpdateWindowList (w->screen))
			{
				SHIFT_WINDOW (w);

				sw->active = TRUE;
				ss->moreAdjust = TRUE;
				ss->state = ShiftStateOut;
				damageScreen (w->screen);
			}
		}
	}
	else if (ss->state == ShiftStateSwitching)
	{
		SHIFT_WINDOW (w);

		if (sw->active)
		{
			damageScreen (w->screen);
			status = TRUE;
		}
	}

	UNWRAP (ss, w->screen, damageWindowRect);
	status |= (*w->screen->damageWindowRect)(w, initial, rect);
	WRAP (ss, w->screen, damageWindowRect, shiftDamageWindowRect);

	return status;
}

static Bool
shiftInitDisplay (CompPlugin  *p,
                  CompDisplay *d)
{
	ShiftDisplay *sd;

	sd = malloc (sizeof (ShiftDisplay));
	if (!sd)
		return FALSE;

	sd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (sd->screenPrivateIndex < 0)
	{
		free (sd);
		return FALSE;
	}

	sd->leftKey  = XKeysymToKeycode (d->display, XStringToKeysym ("Left"));
	sd->rightKey = XKeysymToKeycode (d->display, XStringToKeysym ("Right"));
	sd->upKey    = XKeysymToKeycode (d->display, XStringToKeysym ("Up"));
	sd->downKey  = XKeysymToKeycode (d->display, XStringToKeysym ("Down"));

	WRAP (sd, d, handleEvent, shiftHandleEvent);

	const BananaValue *
	option_initiate_key = bananaGetOption (bananaIndex,
	                                       "initiate_key",
	                                       -1);

	registerKey (option_initiate_key->s, &sd->initiate_key);

	const BananaValue *
	option_initiate_button = bananaGetOption (bananaIndex,
	                                          "initiate_button",
	                                          -1);

	registerButton (option_initiate_button->s, &sd->initiate_button);

	const BananaValue *
	option_initiate_all_key = bananaGetOption (bananaIndex,
	                                           "initiate_all_key",
	                                           -1);

	registerKey (option_initiate_all_key->s, &sd->initiate_all_key);

	const BananaValue *
	option_initiate_all_button = bananaGetOption (bananaIndex,
	                                              "initiate_all_button",
	                                              -1);

	registerButton (option_initiate_all_button->s, &sd->initiate_all_button);

	const BananaValue *
	option_terminate_button = bananaGetOption (bananaIndex,
	                                           "terminate_button",
	                                           -1);

	registerButton (option_terminate_button->s, &sd->terminate_button);

	const BananaValue *
	option_next_key = bananaGetOption (bananaIndex,
	                                   "next_key",
	                                   -1);

	registerKey (option_next_key->s, &sd->next_key);

	const BananaValue *
	option_prev_key = bananaGetOption (bananaIndex,
	                                   "prev_key",
	                                   -1);

	registerKey (option_prev_key->s, &sd->prev_key);

	const BananaValue *
	option_next_all_key = bananaGetOption (bananaIndex,
	                                       "next_all_key",
	                                       -1);

	registerKey (option_next_all_key->s, &sd->next_all_key);

	const BananaValue *
	option_prev_all_key = bananaGetOption (bananaIndex,
	                                       "prev_all_key",
	                                       -1);

	registerKey (option_prev_all_key->s, &sd->prev_all_key);

	d->privates[displayPrivateIndex].ptr = sd;

	return TRUE;
}

static void
shiftFiniDisplay (CompPlugin  *p,
                  CompDisplay *d)
{
	SHIFT_DISPLAY (d);

	freeScreenPrivateIndex (sd->screenPrivateIndex);

	UNWRAP (sd, d, handleEvent);

	free (sd);
}

static Bool
shiftInitScreen (CompPlugin *p,
                 CompScreen *s)
{
	ShiftScreen *ss;

	SHIFT_DISPLAY (&display);

	ss = malloc (sizeof (ShiftScreen));
	if (!ss)
		return FALSE;

	ss->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (ss->windowPrivateIndex < 0)
	{
		free (ss);
		return FALSE;
	}

	ss->grabIndex = 0;

	ss->state = ShiftStateNone;

	ss->windows = NULL;
	ss->windowsSize = 0;

	ss->drawSlots = NULL;
	ss->slotsSize = 0;

	ss->activeSlot = NULL;

	ss->selectedWindow = NULL;

	ss->moreAdjust   = FALSE;

	ss->usedOutput = 0;

	ss->mvAdjust = 0;
	ss->mvVelocity = 0;
	ss->mvTarget = 0;
	ss->invert = FALSE;

	ss->textData = NULL;

	ss->anim         = 0.0;
	ss->animVelocity = 0.0;

	ss->buttonPressed = FALSE;

	WRAP (ss, s, preparePaintScreen, shiftPreparePaintScreen);
	WRAP (ss, s, donePaintScreen, shiftDonePaintScreen);
	WRAP (ss, s, paintScreen, shiftPaintScreen);
	WRAP (ss, s, paintOutput, shiftPaintOutput);
	WRAP (ss, s, paintWindow, shiftPaintWindow);
	WRAP (ss, s, damageWindowRect, shiftDamageWindowRect);

	const BananaValue *
	option_window_match = bananaGetOption (bananaIndex,
	                                       "window_match",
	                                       s->screenNum);

	matchInit (&ss->window_match);
	matchAddFromString (&ss->window_match, option_window_match->s);
	matchUpdate (&ss->window_match);

	ss->cursor = XCreateFontCursor (display.display, XC_left_ptr);

	s->privates[sd->screenPrivateIndex].ptr = ss;

	return TRUE;
}

static void
shiftFiniScreen (CompPlugin *p,
                 CompScreen *s)
{
	SHIFT_SCREEN (s);

	freeWindowPrivateIndex (s, ss->windowPrivateIndex);

	UNWRAP (ss, s, preparePaintScreen);
	UNWRAP (ss, s, donePaintScreen);
	UNWRAP (ss, s, paintScreen);
	UNWRAP (ss, s, paintOutput);
	UNWRAP (ss, s, paintWindow);
	UNWRAP (ss, s, damageWindowRect);

	matchFini (&ss->window_match);

	shiftFreeWindowTitle (s);

	XFreeCursor (display.display, ss->cursor);

	if (ss->windows)
		free (ss->windows);

	if (ss->drawSlots)
		free (ss->drawSlots);

	free (ss);
}

static Bool
shiftInitWindow (CompPlugin *p,
                 CompWindow *w)
{
	ShiftWindow *sw;

	SHIFT_SCREEN (w->screen);

	sw = malloc (sizeof (ShiftWindow));
	if (!sw)
		return FALSE;

	sw->slots[0].scale = 1.0;
	sw->slots[1].scale = 1.0;

	sw->brightness = 1.0;
	sw->opacity    = 1.0;

	w->privates[ss->windowPrivateIndex].ptr = sw;

	return TRUE;
}

static void
shiftFiniWindow (CompPlugin *p,
                 CompWindow *w)
{
	SHIFT_WINDOW (w);

	free (sw);
}

static void
shiftChangeNotify (const char        *optionName,
                   BananaType        optionType,
                   const BananaValue *optionValue,
                   int               screenNum)
{
	SHIFT_DISPLAY (&display);

	if (strcasecmp (optionName, "window_match") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);
		SHIFT_SCREEN (s);

		matchFini (&ss->window_match);
		matchInit (&ss->window_match);
		matchAddFromString (&ss->window_match, optionValue->s);
		matchUpdate (&ss->window_match);
	}
	else if (strcasecmp (optionName, "initiate_key") == 0)
		updateKey (optionValue->s, &sd->initiate_key);

	else if (strcasecmp (optionName, "initiate_all_key") == 0)
		updateKey (optionValue->s, &sd->initiate_all_key);

	else if (strcasecmp (optionName, "next_key") == 0)
		updateKey (optionValue->s, &sd->next_key);

	else if (strcasecmp (optionName, "prev_key") == 0)
		updateKey (optionValue->s, &sd->prev_key);

	else if (strcasecmp (optionName, "next_all_key") == 0)
		updateKey (optionValue->s, &sd->next_all_key);

	else if (strcasecmp (optionName, "prev_all_key") == 0)
		updateKey (optionValue->s, &sd->prev_all_key);

	else if (strcasecmp (optionName, "initiate_button") == 0)
		updateButton (optionValue->s, &sd->initiate_button);

	else if (strcasecmp (optionName, "initiate_all_button") == 0)
		updateButton (optionValue->s, &sd->initiate_all_button);

	else if (strcasecmp (optionName, "terminate_button") == 0)
		updateButton (optionValue->s, &sd->terminate_button);
}

static Bool
shiftInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("shift", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("shift");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, shiftChangeNotify);

	return TRUE;
}

static void
shiftFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable shiftVTable = {
	"shift",
	shiftInit,
	shiftFini,
	shiftInitDisplay,
	shiftFiniDisplay,
	shiftInitScreen,
	shiftFiniScreen,
	shiftInitWindow,
	shiftFiniWindow
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &shiftVTable;
}
