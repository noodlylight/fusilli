/*
 * Compiz splash plugin
 *
 * splash.c
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

#include <compiz-core.h>
#include <X11/Xatom.h>

#include "splash_options.h"

#define SPLASH_BACKGROUND_DEFAULT ""
#define SPLASH_LOGO_DEFAULT ""

#define GET_SPLASH_DISPLAY(d)                                  \
    ((SplashDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define SPLASH_DISPLAY(d)                      \
    SplashDisplay *sd = GET_SPLASH_DISPLAY (d)

#define GET_SPLASH_SCREEN(s, sd)                                   \
    ((SplashScreen *) (s)->base.privates[(sd)->screenPrivateIndex].ptr)

#define SPLASH_SCREEN(s)                                                      \
    SplashScreen *ss = GET_SPLASH_SCREEN (s, GET_SPLASH_DISPLAY (s->display))


static int displayPrivateIndex = 0;

typedef struct _SplashDisplay
{
    Atom splashAtom;
    int screenPrivateIndex;
}
SplashDisplay;

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

}
SplashScreen;


static void
splashPreparePaintScreen (CompScreen *s,
			  int        ms)
{
    SPLASH_SCREEN (s);
    CompDisplay *d = s->display;

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
	ss->fade_in = ss->fade_out = splashGetFadeTime (d) * 1000.0;
	ss->time = splashGetDisplayTime (d) * 1000.0;
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

	    ss->hasBack =
		readImageToTexture (s, &ss->back_img, splashGetBackground (d),
				    &ss->backSize[0], &ss->backSize[1]);
	    ss->hasLogo =
		readImageToTexture (s, &ss->logo_img, splashGetLogo (d),
				    &ss->logoSize[0], &ss->logoSize[1]);

	    if (!ss->hasBack)
	    {
		ss->hasBack =
		    readImageToTexture (s, &ss->back_img,
					SPLASH_BACKGROUND_DEFAULT,
					&ss->backSize[0], &ss->backSize[1]);

		if (ss->hasBack)
		{
		    compLogMessage ("splash", CompLogLevelWarn,
				    "Could not load splash background image "
				    "\"%s\" using default!",
				    splashGetBackground (d) );
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
		    compLogMessage ("splash", CompLogLevelWarn,
				    "Could not load splash logo image "
				    "\"%s\" using default!",
				    splashGetLogo (d) );
		}
	    }

	    if (!ss->hasBack)
		compLogMessage ("splash", CompLogLevelWarn,
				"Could not load splash background image "
				"\"%s\" !", splashGetBackground (d) );

	    if (!ss->hasLogo)
		compLogMessage ("splash", CompLogLevelWarn,
				"Could not load splash logo image \"%s\" !",
				splashGetLogo (d) );
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
	XQueryPointer (s->display->display, s->root, &ignore_w, &ignore_w,
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
    CompDisplay *d = s->display;
    CompTransform sTransform = *transform;

    Bool status = TRUE;

    float alpha = 0.0;

    if (ss->active)
    {
	alpha = (1.0 - (ss->fade_in / (splashGetFadeTime (d) * 1000.0) ) ) *
		(ss->fade_out / (splashGetFadeTime (d) * 1000.0) );
	ss->saturation = 1.0 -
			 ((1.0 - (splashGetSaturation (d) / 100.0) ) * alpha);
	ss->brightness = 1.0 -
			 ((1.0 - (splashGetBrightness (d) / 100.0) ) * alpha);
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
		;
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
			      COMP_TEX_COORD_Y (&mat, cy2) );
		glVertex2f (ss->mesh[x][y + 1][0] *
			    ss->backSize[0],
			    ss->mesh[x][y + 1][1] * ss->backSize[1]);
		glTexCoord2f (COMP_TEX_COORD_X (&mat, cx2),
			      COMP_TEX_COORD_Y (&mat, cy2) );
		glVertex2f (ss->mesh[x + 1][y + 1][0] *
			    ss->backSize[0],
			    ss->mesh[x + 1][y + 1][1] * ss->backSize[1]);
		glTexCoord2f (COMP_TEX_COORD_X (&mat, cx2),
			      COMP_TEX_COORD_Y (&mat, cy1) );
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

    SPLASH_DISPLAY (s->display);

    SplashScreen *ss = (SplashScreen *) calloc (1, sizeof (SplashScreen) );

    s->base.privates[sd->screenPrivateIndex].ptr = ss;

    WRAP (ss, s, paintOutput, splashPaintOutput);
    WRAP (ss, s, preparePaintScreen, splashPreparePaintScreen);
    WRAP (ss, s, donePaintScreen, splashDonePaintScreen);
    WRAP (ss, s, paintWindow, splashPaintWindow);

    initTexture (s, &ss->back_img);
    initTexture (s, &ss->logo_img);

    ss->initiate = FALSE;

    if (splashGetFirststart (s->display) )
    {
	Atom actual;
	int result, format;
	unsigned long n, left;
	unsigned char *propData;

	result = XGetWindowProperty (s->display->display, s->root,
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
	    XChangeProperty (s->display->display, s->root, sd->splashAtom,
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

    //Restore the original function
    UNWRAP (ss, s, paintOutput);
    UNWRAP (ss, s, preparePaintScreen);
    UNWRAP (ss, s, donePaintScreen);
    UNWRAP (ss, s, paintWindow);

    finiTexture (s, &ss->back_img);
    finiTexture (s, &ss->logo_img);

    //Free the pointer
    free (ss);
}



static Bool
splashInitiate (CompDisplay     *d,
		CompAction      *ac,
		CompActionState state,
		CompOption      *option,
		int             nOption)
{
    CompScreen *s;

    s = findScreenAtDisplay (d, getIntOptionNamed (option, nOption, "root", 0));

    if (s)
    {
	SPLASH_SCREEN (s);
	ss->initiate = TRUE;
	damageScreen (s);
    }

    return FALSE;
}


static Bool
splashInitDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    SplashDisplay *sd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    /* Generate a splash display */
    sd = (SplashDisplay *) malloc (sizeof (SplashDisplay) );

    if (!sd)
	return FALSE;

    /* Allocate a private index */
    sd->screenPrivateIndex = allocateScreenPrivateIndex (d);
    
    /* Check if its valid */
    if (sd->screenPrivateIndex < 0)
    {
	/* It's invalid so free memory and return */
	free (sd);
	return FALSE;
    }

    sd->splashAtom = XInternAtom (d->display, "_COMPIZ_WM_SPLASH", 0);

    splashSetInitiateKeyInitiate (d, splashInitiate);

    d->base.privates[displayPrivateIndex].ptr = sd;
    return TRUE;
}

static void
splashFiniDisplay (CompPlugin  *p,
		   CompDisplay *d)
{
    SPLASH_DISPLAY (d);

    /* Free the private index */
    freeScreenPrivateIndex (d, sd->screenPrivateIndex);

    /* Free the pointer */
    free (sd);
}

static Bool
splashInit (CompPlugin *p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();

    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
splashFini (CompPlugin *p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

static CompBool
splashInitObject (CompPlugin *p,
		  CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) splashInitDisplay,
	(InitPluginObjectProc) splashInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
splashFiniObject (CompPlugin *p,
		  CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) splashFiniDisplay,
	(FiniPluginObjectProc) splashFiniScreen
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

CompPluginVTable splashVTable = {
    "splash",
    0,
    splashInit,
    splashFini,
    splashInitObject,
    splashFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &splashVTable;
}
