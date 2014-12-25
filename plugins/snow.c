/**
 *
 * Compiz snow plugin
 *
 * snow.c
 *
 * Copyright (c) 2006 Eckhart P. <beryl@cornergraf.net>
 * Copyright (c) 2006 Brian Jørgensen <qte@fundanemt.com>
 * Maintained by Danny Baumann <maniac@opencompositing.org>
 * Copyright (c) 2014 Michail Bitzes <noodlylight@gmail.com>
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

/*
 * Many thanks to Atie H. <atie.at.matrix@gmail.com> for providing
 * a clean plugin template
 * Also thanks to the folks from #beryl-dev, especially Quinn_Storm
 * for helping me make this possible
 */

#include <math.h>
#include <string.h>

#include <fusilli-core.h>

#define GET_SNOW_DISPLAY(d) \
	((SnowDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define SNOW_DISPLAY(d) \
	SnowDisplay *sd = GET_SNOW_DISPLAY (d)

#define GET_SNOW_SCREEN(s, sd) \
	((SnowScreen *) (s)->privates[(sd)->screenPrivateIndex].ptr)

#define SNOW_SCREEN(s) \
	SnowScreen *ss = GET_SNOW_SCREEN (s, GET_SNOW_DISPLAY (&display))

static int bananaIndex;

static int displayPrivateIndex = 0;

typedef struct _SnowDisplay {
	int screenPrivateIndex;

	Bool useTextures;
	HandleEventProc handleEvent;

	CompKeyBinding  toggle_key;
} SnowDisplay;

typedef struct _SnowTexture {
	CompTexture tex;

	unsigned int width;
	unsigned int height;

	Bool   loaded;
	GLuint dList;
} SnowTexture;

typedef struct _SnowFlake {
	float x, y, z;
	float xs, ys, zs;
	float ra; /* rotation angle */
	float rs; /* rotation speed */

	SnowTexture *tex;
} SnowFlake;

typedef struct _SnowScreen
{
	CompScreen *s;

	Bool active;

	CompTimeoutHandle timeoutHandle;

	PaintOutputProc paintOutput;
	DrawWindowProc  drawWindow;

	SnowTexture *snowTex;
	int         snowTexturesLoaded;

	GLuint displayList;
	Bool   displayListNeedsUpdate;

	SnowFlake *allSnowFlakes;
} SnowScreen;

/* some forward declarations */
static void initiateSnowFlake (SnowScreen * ss, SnowFlake * sf);
static void snowMove (SnowFlake * sf);

static void
snowThink (SnowScreen *ss,
           SnowFlake  *sf)
{
	int boxing;

	const BananaValue *
	option_screen_boxing = bananaGetOption (bananaIndex,
	                                        "screen_boxing",
	                                        -1);

	const BananaValue *
	option_screen_depth = bananaGetOption (bananaIndex,
	                                        "screen_depth",
	                                        -1);

	boxing = option_screen_boxing->i;

	if (sf->y >= ss->s->height + boxing ||
	    sf->x <= -boxing ||
	    sf->y >= ss->s->width + boxing ||
	    sf->z <= -((float) option_screen_depth->i / 500.0) ||
	    sf->z >= 1)
	{
		initiateSnowFlake (ss, sf);
	}

	snowMove (sf);
}

static void
snowMove (SnowFlake   *sf)
{
	const BananaValue *
	option_snow_speed = bananaGetOption (bananaIndex,
	                                     "snow_speed",
	                                     -1);

	const BananaValue *
	option_snow_update_delay = bananaGetOption (bananaIndex,
	                                            "snow_update_delay",
	                                            -1);

	float tmp = 1.0f / (101.0f - option_snow_speed->i);
	int   snowUpdateDelay = option_snow_update_delay->i;

	sf->x += (sf->xs * (float) snowUpdateDelay) * tmp;
	sf->y += (sf->ys * (float) snowUpdateDelay) * tmp;
	sf->z += (sf->zs * (float) snowUpdateDelay) * tmp;
	sf->ra += ((float) snowUpdateDelay) / (10.0f - sf->rs);
}

static Bool
stepSnowPositions (void *closure)
{
	CompScreen *s = closure;
	int        i, numFlakes;
	SnowFlake  *snowFlake;
	Bool       onTop;

	SNOW_SCREEN (s);

	if (!ss->active)
		return TRUE;

	const BananaValue *
	option_num_snowflakes = bananaGetOption (bananaIndex,
	                                         "num_snowflakes",
	                                         -1);

	const BananaValue *
	option_snow_over_windows = bananaGetOption (bananaIndex,
	                                            "snow_over_windows",
	                                            -1);

	snowFlake = ss->allSnowFlakes;
	numFlakes = option_num_snowflakes->i;
	onTop = option_snow_over_windows->b;

	for (i = 0; i < numFlakes; i++)
		snowThink(ss, snowFlake++);

	if (ss->active && !onTop)
	{
		CompWindow *w;

		for (w = s->windows; w; w = w->next)
		{
			if (w->type & CompWindowTypeDesktopMask)
				addWindowDamage (w);
		}
	}
	else if (ss->active)
		damageScreen (s);

	return TRUE;
}

static inline int
getRand (int min,
         int max)
{
	return (rand() % (max - min + 1) + min);
}

static inline float
mmRand (int   min,
        int   max,
        float divisor)
{
	return ((float) getRand(min, max)) / divisor;
};

static void
setupDisplayList (SnowScreen *ss)
{
	const BananaValue *
	option_snow_size = bananaGetOption (bananaIndex,
	                                    "snow_size",
	                                    -1);

	float snowSize = option_snow_size->f;

	ss->displayList = glGenLists (1);

	glNewList (ss->displayList, GL_COMPILE);
	glBegin (GL_QUADS);

	glColor4f (1.0, 1.0, 1.0, 1.0);
	glVertex3f (0, 0, -0.0);
	glColor4f (1.0, 1.0, 1.0, 1.0);
	glVertex3f (0, snowSize, -0.0);
	glColor4f (1.0, 1.0, 1.0, 1.0);
	glVertex3f (snowSize, snowSize, -0.0);
	glColor4f (1.0, 1.0, 1.0, 1.0);
	glVertex3f (snowSize, 0, -0.0);

	glEnd ();
	glEndList ();
}

static void
beginRendering (SnowScreen *ss,
                CompScreen *s)
{
	const BananaValue *
	option_use_blending = bananaGetOption (bananaIndex,
	                                       "use_blending",
	                                       -1);

	const BananaValue *
	option_use_textures = bananaGetOption (bananaIndex,
	                                       "use_textures",
	                                       -1);

	const BananaValue *
	option_num_snowflakes = bananaGetOption (bananaIndex,
	                                         "num_snowflakes",
	                                         -1);

	const BananaValue *
	option_snow_rotation = bananaGetOption (bananaIndex,
	                                        "snow_rotation",
	                                        -1);

	if (option_use_blending->b)
		glEnable (GL_BLEND);

	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	if (ss->displayListNeedsUpdate)
	{
		setupDisplayList (ss);
		ss->displayListNeedsUpdate = FALSE;
	}

	glColor4f (1.0, 1.0, 1.0, 1.0);


	if (ss->snowTexturesLoaded && option_use_textures->b)
	{
		int j;

		for (j = 0; j < ss->snowTexturesLoaded; j++)
		{
			SnowFlake *snowFlake = ss->allSnowFlakes;
			int       i, numFlakes = option_num_snowflakes->i;
			Bool      snowRotate = option_snow_rotation->b;

			enableTexture (ss->s, &ss->snowTex[j].tex,
			               COMP_TEXTURE_FILTER_GOOD);

			for (i = 0; i < numFlakes; i++)
			{
				if (snowFlake->tex == &ss->snowTex[j])
				{
					glTranslatef (snowFlake->x, snowFlake->y, snowFlake->z);

					if (snowRotate)
						glRotatef (snowFlake->ra, 0, 0, 1);

					glCallList (ss->snowTex[j].dList);

					if (snowRotate)
						glRotatef (-snowFlake->ra, 0, 0, 1);

					glTranslatef (-snowFlake->x, -snowFlake->y, -snowFlake->z);
				}
				snowFlake++;
			}
			disableTexture (ss->s, &ss->snowTex[j].tex);
		}
	}
	else
	{
		SnowFlake *snowFlake = ss->allSnowFlakes;
		int       i, numFlakes = option_num_snowflakes->i;

		for (i = 0; i < numFlakes; i++)
		{
			glTranslatef (snowFlake->x, snowFlake->y, snowFlake->z);
			glRotatef (snowFlake->ra, 0, 0, 1);
			glCallList (ss->displayList);
			glRotatef (-snowFlake->ra, 0, 0, 1);
			glTranslatef (-snowFlake->x, -snowFlake->y, -snowFlake->z);
			snowFlake++;
		}
	}

	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	if (option_use_blending->b)
	{
		glDisable (GL_BLEND);
		glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}
}

static Bool
snowPaintOutput (CompScreen              *s,
                 const ScreenPaintAttrib *sa,
                 const CompTransform     *transform,
                 Region                  region,
                 CompOutput              *output,
                 unsigned int            mask)
{
	Bool status;

	SNOW_SCREEN (s);

	const BananaValue *
	option_snow_over_windows = bananaGetOption (bananaIndex,
	                                            "snow_over_windows",
	                                            -1);

	if (ss->active && !option_snow_over_windows->b)
		mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;

	UNWRAP (ss, s, paintOutput);
	status = (*s->paintOutput) (s, sa, transform, region, output, mask);
	WRAP (ss, s, paintOutput, snowPaintOutput);

	if (ss->active && option_snow_over_windows->b)
	{
		CompTransform sTransform = *transform;

		transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

		glPushMatrix ();
		glLoadMatrixf (sTransform.m);
		beginRendering (ss, s);
		glPopMatrix ();
	}

	return status;
}

static Bool
snowDrawWindow (CompWindow           *w,
                const CompTransform  *transform,
                const FragmentAttrib *attrib,
                Region               region,
                unsigned int         mask)
{
	Bool status;

	SNOW_SCREEN (w->screen);

	/* First draw Window as usual */
	UNWRAP (ss, w->screen, drawWindow);
	status = (*w->screen->drawWindow) (w, transform, attrib, region, mask);
	WRAP (ss, w->screen, drawWindow, snowDrawWindow);

	const BananaValue *
	option_snow_over_windows = bananaGetOption (bananaIndex,
	                                            "snow_over_windows",
	                                            -1);

	/* Check whether this is the Desktop Window */
	if (ss->active && (w->type & CompWindowTypeDesktopMask) && 
	    !option_snow_over_windows->b)
	{
		beginRendering (ss, w->screen);
	}

	return status;
}

static void
initiateSnowFlake (SnowScreen *ss,
                   SnowFlake  *sf)
{
	/* TODO: possibly place snowflakes based on FOV, instead of a cube. */
	const BananaValue *
	option_screen_boxing = bananaGetOption (bananaIndex,
	                                        "screen_boxing",
	                                        -1);

	const BananaValue *
	option_snow_direction = bananaGetOption (bananaIndex,
	                                         "snow_direction",
	                                         -1);

	const BananaValue *
	option_screen_depth = bananaGetOption (bananaIndex,
	                                       "screen_depth",
	                                       1);

	int boxing = option_screen_boxing->i;

	switch (option_snow_direction->i) {
	case 0: //Top To Bottom
		sf->x  = mmRand (-boxing, ss->s->width + boxing, 1);
		sf->xs = mmRand (-1, 1, 500);
		sf->y  = mmRand (-300, 0, 1);
		sf->ys = mmRand (1, 3, 1);
		break;
	case 1: //Bottom To Top
		sf->x  = mmRand (-boxing, ss->s->width + boxing, 1);
		sf->xs = mmRand (-1, 1, 500);
		sf->y  = mmRand (ss->s->height, ss->s->height + 300, 1);
		sf->ys = -mmRand (1, 3, 1);
		break;
	case 2: //Right To Left
		sf->x  = mmRand (ss->s->width, ss->s->width + 300, 1);
		sf->xs = -mmRand (1, 3, 1);
		sf->y  = mmRand (-boxing, ss->s->height + boxing, 1);
		sf->ys = mmRand (-1, 1, 500);
		break;
	case 3: //Left To Right
		sf->x  = mmRand (-300, 0, 1);
		sf->xs = mmRand (1, 3, 1);
		sf->y  = mmRand (-boxing, ss->s->height + boxing, 1);
		sf->ys = mmRand (-1, 1, 500);
		break;
	default:
		break;
	}

	sf->z  = mmRand (-option_screen_depth->i, 0.1, 5000);
	sf->zs = mmRand (-1000, 1000, 500000);
	sf->ra = mmRand (-1000, 1000, 50);
	sf->rs = mmRand (-1000, 1000, 1000);
}

static void
setSnowflakeTexture (SnowScreen *ss,
                     SnowFlake  *sf)
{
	if (ss->snowTexturesLoaded)
		sf->tex = &ss->snowTex[rand () % ss->snowTexturesLoaded];
}

static void
updateSnowTextures (CompScreen *s)
{
	const BananaValue *
	option_snow_size = bananaGetOption (bananaIndex,
	                                    "snow_size",
	                                    -1);

	const BananaValue *
	option_num_snowflakes = bananaGetOption (bananaIndex,
	                                         "num_snowflakes",
	                                         -1);

	const BananaValue *
	option_snow_textures = bananaGetOption (bananaIndex,
	                                        "snow_textures",
	                                        -1);

	int       i, count = 0;
	float     snowSize = option_snow_size->f;
	int       numFlakes = option_num_snowflakes->i;
	SnowFlake *snowFlake;

	SNOW_SCREEN (s);

	snowFlake = ss->allSnowFlakes;

	for (i = 0; i < ss->snowTexturesLoaded; i++)
	{
		finiTexture (s, &ss->snowTex[i].tex);
		glDeleteLists (ss->snowTex[i].dList, 1);
	}

	if (ss->snowTex)
		free (ss->snowTex);

	ss->snowTexturesLoaded = 0;

	ss->snowTex = calloc (1,
	                 sizeof (SnowTexture) * option_snow_textures->list.nItem);

	for (i = 0; i < option_snow_textures->list.nItem; i++)
	{
		CompMatrix  *mat;
		SnowTexture *sTex;

		ss->snowTex[count].loaded =
		    readImageToTexture (s, &ss->snowTex[count].tex,
		        option_snow_textures->list.item[i].s,
		        &ss->snowTex[count].width,
		        &ss->snowTex[count].height);

		if (!ss->snowTex[count].loaded)
		{
			compLogMessage ("snow", CompLogLevelWarn,
			                "Texture not found : %s",
			                option_snow_textures->list.item[i].s);
			continue;
		}

		compLogMessage ("snow", CompLogLevelInfo,
		                "Loaded Texture %s",
		                option_snow_textures->list.item[i].s);

		mat = &ss->snowTex[count].tex.matrix;
		sTex = &ss->snowTex[count];

		sTex->dList = glGenLists (1);
		glNewList (sTex->dList, GL_COMPILE);

		glBegin (GL_QUADS);

		glTexCoord2f (COMP_TEX_COORD_X (mat, 0), COMP_TEX_COORD_Y (mat, 0));
		glVertex2f (0, 0);
		glTexCoord2f (COMP_TEX_COORD_X (mat, 0),
		              COMP_TEX_COORD_Y (mat, sTex->height));
		glVertex2f (0, snowSize * sTex->height / sTex->width);
		glTexCoord2f (COMP_TEX_COORD_X (mat, sTex->width),
		              COMP_TEX_COORD_Y (mat, sTex->height));
		glVertex2f (snowSize, snowSize * sTex->height / sTex->width);
		glTexCoord2f (COMP_TEX_COORD_X (mat, sTex->width),
		              COMP_TEX_COORD_Y (mat, 0));
		glVertex2f (snowSize, 0);

		glEnd ();
		glEndList ();

		count++;
	}

	ss->snowTexturesLoaded = count;
	if (count < option_snow_textures->list.nItem)
		ss->snowTex = realloc (ss->snowTex, sizeof (SnowTexture) * count);

	for (i = 0; i < numFlakes; i++)
		setSnowflakeTexture (ss, snowFlake++);
}

static Bool
snowInitScreen (CompPlugin *p,
                CompScreen *s)
{
	SnowScreen *ss;

	const BananaValue *
	option_num_snowflakes = bananaGetOption (bananaIndex,
	                                         "num_snowflakes",
	                                         -1);

	const BananaValue *
	option_snow_update_delay = bananaGetOption (bananaIndex,
	                                            "snow_update_delay",
	                                            -1);

	int        i, numFlakes = option_num_snowflakes->i;
	SnowFlake  *snowFlake;

	SNOW_DISPLAY (&display);

	ss = calloc (1, sizeof(SnowScreen));
	if (!ss)
		return FALSE;

	s->privates[sd->screenPrivateIndex].ptr = ss;

	ss->s = s;
	ss->snowTexturesLoaded = 0;
	ss->snowTex = NULL;
	ss->active = FALSE;
	ss->displayListNeedsUpdate = FALSE;

	ss->allSnowFlakes = snowFlake = malloc (numFlakes * sizeof (SnowFlake));
	if (!snowFlake)
	{
		free (ss);
		return FALSE;
	}

	for (i = 0; i < numFlakes; i++)
	{
		initiateSnowFlake (ss, snowFlake);
		setSnowflakeTexture (ss, snowFlake);
		snowFlake++;
	}

	updateSnowTextures (s);
	setupDisplayList (ss);

	WRAP (ss, s, paintOutput, snowPaintOutput);
	WRAP (ss, s, drawWindow, snowDrawWindow);

	ss->timeoutHandle = compAddTimeout (option_snow_update_delay->i,
	                (float)
	                option_snow_update_delay->i *
	                1.2,
	                stepSnowPositions, s);

	return TRUE;
}

static void
snowFiniScreen (CompPlugin *p,
                CompScreen *s)
{
	int i;

	SNOW_SCREEN (s);

	if (ss->timeoutHandle)
		compRemoveTimeout (ss->timeoutHandle);

	for (i = 0; i < ss->snowTexturesLoaded; i++)
	{
		finiTexture (s, &ss->snowTex[i].tex);
		glDeleteLists (ss->snowTex[i].dList, 1);
	}

	if (ss->snowTex)
		free (ss->snowTex);

	if (ss->allSnowFlakes)
		free (ss->allSnowFlakes);

	UNWRAP (ss, s, paintOutput);
	UNWRAP (ss, s, drawWindow);

	free (ss);
}

static void
snowChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	SNOW_DISPLAY (&display);

	if (strcasecmp (optionName, "snow_size") == 0)
	{
		CompScreen *s;

		for (s = display.screens; s; s = s->next)
		{
			SNOW_SCREEN (s);
			ss->displayListNeedsUpdate = TRUE;
			updateSnowTextures (s);
		}
	}
	else if (strcasecmp (optionName, "snow_update_delay") == 0)
	{
		CompScreen *s;

		for (s = display.screens; s; s = s->next)
		{
			SNOW_SCREEN (s);

			if (ss->timeoutHandle)
				compRemoveTimeout (ss->timeoutHandle);
			ss->timeoutHandle =
			    compAddTimeout (optionValue->i,
			            (float) optionValue->i * 1.2,
			            stepSnowPositions, s);
		}
	}
	else if (strcasecmp (optionName, "num_snowflakes") == 0)
	{
		CompScreen *s;
		int        i, numFlakes;
		SnowFlake  *snowFlake;

		numFlakes = optionValue->i;
		for (s = display.screens; s; s = s->next)
		{
			SNOW_SCREEN (s);
			ss->allSnowFlakes = realloc (ss->allSnowFlakes,
			                             numFlakes * sizeof (SnowFlake));
			snowFlake = ss->allSnowFlakes;

			for (i = 0; i < numFlakes; i++)
			{
				initiateSnowFlake (ss, snowFlake);
				setSnowflakeTexture (ss, snowFlake);
				snowFlake++;
			}
		}
	}
	else if (strcasecmp (optionName, "snow_textures") == 0)
	{
		CompScreen *s;

		for (s = display.screens; s; s = s->next)
			updateSnowTextures (s);
	}
	else if (strcasecmp (optionName, "toggle_key") == 0)
		updateKey (optionValue->s, &sd->toggle_key);

}

static void
snowHandleEvent (XEvent      *event)
{
	CompScreen *s;

	SNOW_DISPLAY (&display);

	switch (event->type) {

	case KeyPress:
		if (isKeyPressEvent (event, &sd->toggle_key))
		{
			s = findScreenAtDisplay (event->xkey.root);

			if (s)
			{
				SNOW_SCREEN (s);
				ss->active = !ss->active;
				if (!ss->active)
					damageScreen (s);
			}
		}
		break;

	default:
		break;
	}

	UNWRAP (sd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (sd, &display, handleEvent, snowHandleEvent);
}

static Bool
snowInitDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	SnowDisplay *sd;

	sd = malloc (sizeof (SnowDisplay));
	if (!sd)
		return FALSE;

	sd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (sd->screenPrivateIndex < 0)
	{
		free (sd);
		return FALSE;
	}

	const BananaValue *
	option_toggle_key = bananaGetOption (bananaIndex,
	                                     "toggle_key",
	                                     -1);

	registerKey (option_toggle_key->s, &sd->toggle_key);

	WRAP (sd, d, handleEvent, snowHandleEvent);

	d->privates[displayPrivateIndex].ptr = sd;

	return TRUE;
}

static void
snowFiniDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	SNOW_DISPLAY (d);

	freeScreenPrivateIndex (sd->screenPrivateIndex);

	UNWRAP (sd, d, handleEvent);

	free (sd);
}

static Bool
snowInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("snow", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("snow");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, snowChangeNotify);

	return TRUE;
}

static void
snowFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

CompPluginVTable snowVTable = {
	"snow",
	snowInit,
	snowFini,
	snowInitDisplay,
	snowFiniDisplay,
	snowInitScreen,
	snowFiniScreen,
	NULL, /* snowInitWindow */
	NULL  /* snowFiniWindow */
};

CompPluginVTable*
getCompPluginInfo20141205 (void)
{
	return &snowVTable;
}
