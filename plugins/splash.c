/*
 * Compiz splash plugin
 *
 * splash.c
 *
 * Copyright : (C) 2015 by Michail Bitzes
 * E-mail    : noodlylight@gmail.com
 *
 * Copyright : (C) 2006 by Dennis Kasprzyk
 * E-mail    : onestone@beryl-project.org
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
 */

#include <math.h>
#include <string.h>

#include <fusilli-core.h>
#include <X11/Xatom.h>

#define SPLASH_BACKGROUND_DEFAULT ""
#define SPLASH_LOGO_DEFAULT       ""

#define GET_SPLASH_DISPLAY(d) \
        ((SplashDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define SPLASH_DISPLAY(d) \
        SplashDisplay *sd = GET_SPLASH_DISPLAY (d)

#define GET_SPLASH_SCREEN(s, sd) \
        ((SplashScreen *) (s)->privates[(sd)->screenPrivateIndex].ptr)

#define SPLASH_SCREEN(s) \
        SplashScreen *ss = GET_SPLASH_SCREEN (s, GET_SPLASH_DISPLAY (&display))

static int bananaIndex;

static int displayPrivateIndex = 0;

typedef struct _SplashDisplay
{
	Atom splashAtom;
	int screenPrivateIndex;

	HandleEventProc handleEvent;
	CompKeyBinding initiate_key;
} SplashDisplay;

#define MESH_W 16
#define MESH_H 16

typedef struct _SplashScreen
{
	PreparePaintScreenProc preparePaintScreen;
	DonePaintScreenProc    donePaintScreen;
	PaintOutputProc        paintOutput;
	PaintWindowProc        paintWindow;

	int fade_in;
	int fade_out;
	int time;

	CompTexture back_img, logo_img;
	unsigned int backSize[2], logoSize[2];
	Bool hasInit, hasLogo, hasBack;

	float mesh[MESH_W][MESH_H][2];
	float mMove;

	float brightness;
	float saturation;

	Bool initiate;
	Bool active;

} SplashScreen;

static void
splashPreparePaintScreen (CompScreen *s,
                          int        ms)
{
	SPLASH_SCREEN (s);

	Bool lastShot = FALSE;

	ss->fade_in -= ms;

	if (ss->fade_in < 0)
	{
		ss->time += ss->fade_in;
		ss->fade_in = 0;

		if (ss->time < 0)
		{
			if (ss->fade_out > 0 && ss->fade_out <= ms)
				lastShot = TRUE;

			ss->fade_out += ss->time;

			ss->time = 0;

			if (ss->fade_out < 0)
				ss->fade_out = 0;
		}
	}

	if (ss->initiate)
	{
		const BananaValue *
		option_fade_time = bananaGetOption (bananaIndex,
		                                    "fade_time",
		                                    -1);

		const BananaValue *
		option_display_time = bananaGetOption (bananaIndex,
		                                       "display_time",
		                                       -1);

		ss->fade_in = ss->fade_out = option_fade_time->f * 1000.0;
		ss->time = option_display_time->f * 1000.0;
		ss->initiate = FALSE;
	}

	if (ss->fade_in || ss->fade_out || ss->time || lastShot)
	{
		ss->active = TRUE;
		ss->mMove += ms / 500.0;

		if (!ss->hasInit)
		{
			ss->hasInit = TRUE;
			ss->mMove = 0.0;

			const BananaValue *
			option_background = bananaGetOption (bananaIndex,
			                                     "background",
			                                     -1);

			const BananaValue *
			option_logo = bananaGetOption (bananaIndex,
			                               "logo",
			                               -1);

			ss->hasBack =
			readImageToTexture (s, &ss->back_img, option_background->s,
			                    &ss->backSize[0], &ss->backSize[1]);

			ss->hasLogo =
			readImageToTexture (s, &ss->logo_img, option_logo->s,
			                    &ss->logoSize[0], &ss->logoSize[1]);

			if (!ss->hasBack)
			{
				ss->hasBack =
				    readImageToTexture (s, &ss->back_img,
				            SPLASH_BACKGROUND_DEFAULT,
				            &ss->backSize[0], &ss->backSize[1]);

				if (ss->hasBack)
				{
					const BananaValue *
					option_background = bananaGetOption (bananaIndex,
					                                     "background",
					                                     -1);

					compLogMessage ("splash", CompLogLevelWarn,
					        "Could not load splash background image "
					        "\"%s\" using default!",
					        option_background->s);
				}
			}

			if (!ss->hasLogo)
			{
				ss->hasLogo =
				    readImageToTexture (s, &ss->logo_img,
				    SPLASH_LOGO_DEFAULT,
				    &ss->logoSize[0], &ss->logoSize[1]);

				if (ss->hasLogo)
				{
					const BananaValue *
					option_logo = bananaGetOption (bananaIndex,
					                               "logo",
					                               -1);

					compLogMessage ("splash", CompLogLevelWarn,
					        "Could not load splash logo image "
					        "\"%s\" using default!",
					        option_logo->s);
				}
			}

			if (!ss->hasBack)
			{
				const BananaValue *
				option_background = bananaGetOption (bananaIndex,
				                                     "background",
				                                     -1);

				compLogMessage ("splash", CompLogLevelWarn,
				                "Could not load splash background image "
				                "\"%s\" !", option_background->s);
			}

			if (!ss->hasLogo)
			{
				const BananaValue *
				option_logo = bananaGetOption (bananaIndex,
				                               "logo",
				                               -1);

				compLogMessage ("splash", CompLogLevelWarn,
				                "Could not load splash logo image \"%s\" !",
				                option_logo->s);
			}
		}
	}
	else
	{
		ss->active = FALSE;

		if (ss->hasInit)
		{
			ss->hasInit = FALSE;

			if (ss->hasBack)
			{
				finiTexture (s, &ss->back_img);
				initTexture (s, &ss->back_img);
				ss->hasBack = FALSE;
			}

			if (ss->hasLogo)
			{
				finiTexture (s, &ss->logo_img);
				initTexture (s, &ss->logo_img);
				ss->hasLogo = FALSE;
			}
		}
	}

	UNWRAP (ss, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, ms);
	WRAP (ss, s, preparePaintScreen, splashPreparePaintScreen);
}

static void
splashDonePaintScreen (CompScreen * s)
{
	SPLASH_SCREEN (s);

	if (ss->fade_in || ss->fade_out || ss->time)
		damageScreen (s);

	UNWRAP (ss, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP (ss, s, donePaintScreen, splashDonePaintScreen);
}

static void
splashGetCurrentOutputRect (CompScreen *s,
                            XRectangle *outputRect)
{
	int root_x = 0, root_y = 0;
	int ignore_i;
	unsigned int ignore_ui;
	int output;
	Window ignore_w;

	if (s->nOutputDev == 1)
		output = 0;
	else
	{
		XQueryPointer (display.display, s->root, &ignore_w, &ignore_w,
		               &root_x, &root_y, &ignore_i, &ignore_i, &ignore_ui);
		output = outputDeviceForPoint (s, root_x, root_y);
	}

	outputRect->x      = s->outputDev[output].region.extents.x1;
	outputRect->y      = s->outputDev[output].region.extents.y1;
	outputRect->width  = s->outputDev[output].region.extents.x2 -
	                     s->outputDev[output].region.extents.x1;
	outputRect->height = s->outputDev[output].region.extents.y2 -
	                     s->outputDev[output].region.extents.y1;
}

static Bool
splashPaintOutput (CompScreen              *s,
                   const ScreenPaintAttrib *sa,
                   const CompTransform     *transform,
                   Region                  region,
                   CompOutput              *output,
                   unsigned int            mask)
{
	SPLASH_SCREEN (s);
	CompTransform sTransform = *transform;

	Bool status = TRUE;

	float alpha = 0.0;

	if (ss->active)
	{
		const BananaValue *
		option_fade_time = bananaGetOption (bananaIndex,
		                                    "fade_time",
		                                    -1);

		const BananaValue *
		option_brightness = bananaGetOption (bananaIndex,
		                                     "brightness",
		                                     -1);

		const BananaValue *
		option_saturation = bananaGetOption (bananaIndex,
		                                     "saturation",
		                                     -1);

		alpha = (1.0 - (ss->fade_in / (option_fade_time->f * 1000.0) ) ) *
		        (ss->fade_out / (option_fade_time->f * 1000.0) );
		ss->saturation = 1.0 -
		        ((1.0 - (option_saturation->f / 100.0) ) * alpha);
		ss->brightness = 1.0 -
		         ((1.0 - (option_brightness->f / 100.0) ) * alpha);
	}

	UNWRAP (ss, s, paintOutput);
	status = (*s->paintOutput) (s, sa, transform, region, output, mask);
	WRAP (ss, s, paintOutput, splashPaintOutput);

	if (!ss->active)
		return status;

	transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

	glPushMatrix ();
	glLoadMatrixf (sTransform.m);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f (1.0, 1.0, 1.0, alpha);
	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	if (ss->hasBack)
	{
		int x, y;

		for (x = 0; x < MESH_W; x++)
		{
			for (y = 0; y < MESH_H; y++)
			{
				ss->mesh[x][y][0] =
				    (x / (MESH_W - 1.0) ) +
				    (0.02 * sin ( (y / (MESH_H - 1.0) * 8) + ss->mMove) );

				ss->mesh[x][y][1] =
				    (y / (MESH_H - 1.0) ) +
				    (0.02 * sin ( (ss->mesh[x][y][0] * 8) + ss->mMove) );
			}
		}

		enableTexture (s, &ss->back_img, COMP_TEXTURE_FILTER_GOOD);

		if (s->nOutputDev > 1)
		{
			XRectangle headOutputRect;

			splashGetCurrentOutputRect (s, &headOutputRect);

			x = (headOutputRect.width - ss->backSize[0]) / 2;
			y = (headOutputRect.height - ss->backSize[1]) / 2;

			x += headOutputRect.x;
			y += headOutputRect.y;
		}
		else
		{
			x = (s->width - ss->backSize[0]) / 2;
			y = (s->height - ss->backSize[1]) / 2;
		}

		CompMatrix mat = ss->back_img.matrix;

		glTranslatef (x, y, 0);

		float cx1, cx2, cy1, cy2;

		glBegin (GL_QUADS);

		for (x = 0; x < MESH_W - 1; x++)
		{
			for (y = 0; y < MESH_H - 1; y++)
			{
				cx1 = (x / (MESH_W - 1.0) ) * ss->backSize[0];
				cx2 = ( (x + 1) / (MESH_W - 1.0) ) * ss->backSize[0];
				cy1 = (y / (MESH_H - 1.0) ) * ss->backSize[1];
				cy2 = ( (y + 1) / (MESH_H - 1.0) ) * ss->backSize[1];

				glTexCoord2f (COMP_TEX_COORD_X (&mat, cx1),
				              COMP_TEX_COORD_Y (&mat, cy1) );

				glVertex2f (ss->mesh[x][y][0] *
				            ss->backSize[0],
				            ss->mesh[x][y][1] * ss->backSize[1]);

				glTexCoord2f (COMP_TEX_COORD_X (&mat, cx1),
				              COMP_TEX_COORD_Y (&mat, cy2));

				glVertex2f (ss->mesh[x][y + 1][0] *
				            ss->backSize[0],
				            ss->mesh[x][y + 1][1] * ss->backSize[1]);

				glTexCoord2f (COMP_TEX_COORD_X (&mat, cx2),
				              COMP_TEX_COORD_Y (&mat, cy2));

				glVertex2f (ss->mesh[x + 1][y + 1][0] *
				            ss->backSize[0],
				            ss->mesh[x + 1][y + 1][1] * ss->backSize[1]);

				glTexCoord2f (COMP_TEX_COORD_X (&mat, cx2),
				              COMP_TEX_COORD_Y (&mat, cy1));

				glVertex2f (ss->mesh[x + 1][y][0] *
				            ss->backSize[0],
				            ss->mesh[x + 1][y][1] * ss->backSize[1]);
			}
		}

		glEnd ();

		if (s->nOutputDev > 1)
		{
			XRectangle headOutputRect;

			splashGetCurrentOutputRect (s, &headOutputRect);

			x = (headOutputRect.width - ss->backSize[0]) / 2;
			y = (headOutputRect.height - ss->backSize[1]) / 2;

			x += headOutputRect.x;
			y += headOutputRect.y;
		}
		else
		{
			x = (s->width - ss->backSize[0]) / 2;
			y = (s->height - ss->backSize[1]) / 2;
		}

		glTranslatef (-x, -y, 0);

		disableTexture (s, &ss->back_img);
	}

	if (ss->hasLogo)
	{
		enableTexture (s, &ss->logo_img, COMP_TEXTURE_FILTER_GOOD);
		int x, y;

		if (s->nOutputDev > 1)
		{
			XRectangle headOutputRect;

			splashGetCurrentOutputRect (s, &headOutputRect);
			x = (headOutputRect.width - ss->logoSize[0]) / 2;
			y = (headOutputRect.height - ss->logoSize[1]) / 2;

			x += headOutputRect.x;
			y += headOutputRect.y;
		}
		else
		{
			x = (s->width - ss->logoSize[0]) / 2;
			y = (s->height - ss->logoSize[1]) / 2;
		}

		CompMatrix mat = ss->logo_img.matrix;

		glTranslatef (x, y, 0);

		glBegin (GL_QUADS);
		glTexCoord2f (COMP_TEX_COORD_X (&mat, 0), COMP_TEX_COORD_Y (&mat, 0) );
		glVertex2f (0, 0);
		glTexCoord2f (COMP_TEX_COORD_X (&mat, 0),
		              COMP_TEX_COORD_Y (&mat, ss->logoSize[1]) );
		glVertex2f (0, ss->logoSize[1]);
		glTexCoord2f (COMP_TEX_COORD_X (&mat, ss->logoSize[0]),
		              COMP_TEX_COORD_Y (&mat, ss->logoSize[1]) );
		glVertex2f (ss->logoSize[0], ss->logoSize[1]);
		glTexCoord2f (COMP_TEX_COORD_X (&mat, ss->logoSize[0]),
		              COMP_TEX_COORD_Y (&mat, 0) );
		glVertex2f (ss->logoSize[0], 0);
		glEnd ();

		glTranslatef (-x, -y, 0);

		disableTexture (s, &ss->logo_img);
	}

	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	glDisable (GL_BLEND);
	glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glColor4usv (defaultColor);
	glPopMatrix ();

	return status;
}

static Bool
splashPaintWindow (CompWindow              *w,
                   const WindowPaintAttrib *attrib,
                   const CompTransform     *transform,
                   Region                  region,
                   unsigned int            mask)
{
	CompScreen *s = w->screen;
	Bool status;

	SPLASH_SCREEN (s);

	if (ss->active)
	{
		WindowPaintAttrib pA = *attrib;
		pA.brightness = attrib->brightness * ss->brightness;
		pA.saturation = attrib->saturation * ss->saturation;

		UNWRAP (ss, s, paintWindow);
		status = (*s->paintWindow) (w, &pA, transform, region, mask);
		WRAP (ss, s, paintWindow, splashPaintWindow);
	}
	else
	{
		UNWRAP (ss, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP (ss, s, paintWindow, splashPaintWindow);
	}

	return status;
}

static Bool
splashInitScreen (CompPlugin *p,
                  CompScreen *s)
{
	SplashScreen *ss;

	SPLASH_DISPLAY (&display);

	ss = malloc (sizeof (SplashScreen));
	if (!ss)
		return FALSE;

	WRAP (ss, s, paintOutput, splashPaintOutput);
	WRAP (ss, s, preparePaintScreen, splashPreparePaintScreen);
	WRAP (ss, s, donePaintScreen, splashDonePaintScreen);
	WRAP (ss, s, paintWindow, splashPaintWindow);

	s->privates[sd->screenPrivateIndex].ptr = ss;

	initTexture (s, &ss->back_img);
	initTexture (s, &ss->logo_img);

	ss->initiate = FALSE;
	ss->fade_in = 0;
	ss->fade_out = 0;
	ss->time = 0;
	ss->hasInit = FALSE;
	ss->hasLogo = FALSE;
	ss->hasBack = FALSE;

	const BananaValue *
	option_firststart = bananaGetOption (bananaIndex,
	                                     "firststart",
	                                     -1);

	if (option_firststart->b)
	{
		Atom actual;
		int result, format;
		unsigned long n, left;
		unsigned char *propData;

		result = XGetWindowProperty (display.display, s->root,
		                 sd->splashAtom, 0L, 8192L, FALSE,
		                 XA_INTEGER, &actual, &format,
		                 &n, &left, &propData);

		if (result == Success && n && propData)
		{
			XFree (propData);
		}
		else
		{
			int value = 1;
			XChangeProperty (display.display, s->root, sd->splashAtom,
			                 XA_INTEGER, 32, PropModeReplace,
			                 (unsigned char *) &value, 1);
			ss->initiate = TRUE;
		}
	}

	return TRUE;
}

static void
splashFiniScreen (CompPlugin *p,
                  CompScreen *s)
{
	SPLASH_SCREEN (s);

	UNWRAP (ss, s, paintOutput);
	UNWRAP (ss, s, preparePaintScreen);
	UNWRAP (ss, s, donePaintScreen);
	UNWRAP (ss, s, paintWindow);

	finiTexture (s, &ss->back_img);
	finiTexture (s, &ss->logo_img);

	free (ss);
}

static void
splashHandleEvent (XEvent      *event)
{
	CompScreen *s;

	SPLASH_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &sd->initiate_key))
		{
			s = findScreenAtDisplay (event->xkey.root);

			if (s)
			{
				SPLASH_SCREEN (s);
				ss->initiate = TRUE;
				damageScreen (s);
			}
		}
		break;
	default:
		break;
	}

	UNWRAP (sd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (sd, &display, handleEvent, splashHandleEvent);
}

static Bool
splashInitDisplay (CompPlugin  *p,
                   CompDisplay *d)
{
	SplashDisplay *sd;

	sd = malloc (sizeof (SplashDisplay));
	if (!sd)
		return FALSE;

	sd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (sd->screenPrivateIndex < 0)
	{
		free (sd);
		return FALSE;
	}

	sd->splashAtom = XInternAtom (d->display, "_COMPIZ_WM_SPLASH", 0);

	WRAP (sd, d, handleEvent, splashHandleEvent);

	const BananaValue *
	option_initiate_key = bananaGetOption (bananaIndex,
	                                       "initiate_key", -1);

	registerKey (option_initiate_key->s, &sd->initiate_key);

	d->privates[displayPrivateIndex].ptr = sd;

	return TRUE;
}

static void
splashFiniDisplay (CompPlugin  *p,
                   CompDisplay *d)
{
	SPLASH_DISPLAY (d);

	freeScreenPrivateIndex (sd->screenPrivateIndex);

	UNWRAP (sd, d, handleEvent);

	free (sd);
}

static void
splashChangeNotify (const char        *optionName,
                    BananaType        optionType,
                    const BananaValue *optionValue,
                    int               screenNum)
{
	SPLASH_DISPLAY (&display);

	if (strcasecmp (optionName, "initiate_key") == 0)
		updateKey (optionValue->s, &sd->initiate_key);
}

static Bool
splashInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("splash", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("splash");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, splashChangeNotify);

	return TRUE;
}

static void
splashFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable splashVTable = {
	"splash",
	splashInit,
	splashFini,
	splashInitDisplay,
	splashFiniDisplay,
	splashInitScreen,
	splashFiniScreen,
	NULL, /* splashInitWindow */
	NULL  /* splashFiniWindow */
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &splashVTable;
}
