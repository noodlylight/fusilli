/*
 *
 * Compiz show mouse pointer plugin
 *
 * showmouse.c
 *
 * Copyright : (C) 2008 by Dennis Kasprzyk
 * E-mail    : onestone@opencompositing.org
 *
 * Copyright : (C) 2015 by Michail Bitzes
 * E-mail    : noodlylight@gmail.com
 *
 * Copyright (C) 2015 Hypra
 * http://www.hypra.fr
 * Added guides.
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
#include <fusilli-mousepoll.h>

#include "showmouse_tex.h"

#define GET_SHOWMOUSE_DISPLAY(d) \
        ((ShowmouseDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define SHOWMOUSE_DISPLAY(d) \
        ShowmouseDisplay *sd = GET_SHOWMOUSE_DISPLAY (d)

#define GET_SHOWMOUSE_SCREEN(s, sd) \
        ((ShowmouseScreen *) (s)->privates[(sd)->screenPrivateIndex].ptr)

#define SHOWMOUSE_SCREEN(s) \
        ShowmouseScreen *ss = GET_SHOWMOUSE_SCREEN (s, GET_SHOWMOUSE_DISPLAY (&display))

typedef struct _Particle
{
	float life;         // particle life
	float fade;         // fade speed
	float width;        // particle width
	float height;       // particle height
	float w_mod;        // particle size modification during life
	float h_mod;        // particle size modification during life
	float r;            // red value
	float g;            // green value
	float b;            // blue value
	float a;            // alpha value
	float x;            // X position
	float y;            // Y position
	float z;            // Z position
	float xi;           // X direction
	float yi;           // Y direction
	float zi;           // Z direction
	float xg;           // X gravity
	float yg;           // Y gravity
	float zg;           // Z gravity
	float xo;           // orginal X position
	float yo;           // orginal Y position
	float zo;           // orginal Z position
} Particle;

typedef struct _ParticleSystem
{
	int      numParticles;
	Particle *particles;
	float    slowdown;
	GLuint   tex;
	Bool     active;
	int      x, y;
	float    darken;
	GLuint   blendMode;

	// Moved from drawParticles to get rid of spurious malloc's
	GLfloat *vertices_cache;
	int     vertex_cache_count;

	GLfloat *coords_cache;
	int     coords_cache_count;

	GLfloat *colors_cache;
	int     color_cache_count;

	GLfloat *dcolors_cache;
	int     dcolors_cache_count;
} ParticleSystem;

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _ShowmouseDisplay {
	int screenPrivateIndex;
	HandleEventProc handleEvent;

	CompButtonBinding initiate_button;
	CompKeyBinding initiate_key;
} ShowmouseDisplay;

typedef struct _ShowmouseScreen {
	int posX;
	int posY;

	Bool active;

	ParticleSystem *ps;

	float rot;

	PositionPollingHandle pollHandle;

	PreparePaintScreenProc preparePaintScreen;
	DonePaintScreenProc    donePaintScreen;
	PaintOutputProc        paintOutput;
} ShowmouseScreen;

static void
initParticles (int numParticles,
               ParticleSystem *ps)
{
	if (ps->particles)
		free (ps->particles);

	ps->particles    = calloc (numParticles, sizeof(Particle));
	ps->tex          = 0;
	ps->numParticles = numParticles;
	ps->slowdown     = 1;
	ps->active       = FALSE;

	// Initialize cache
	ps->vertices_cache      = NULL;
	ps->colors_cache        = NULL;
	ps->coords_cache        = NULL;
	ps->dcolors_cache       = NULL;
	ps->vertex_cache_count  = 0;
	ps->color_cache_count   = 0;
	ps->coords_cache_count  = 0;
	ps->dcolors_cache_count = 0;

	Particle *part = ps->particles;
	int i;
	for (i = 0; i < numParticles; i++, part++)
		part->life = 0.0f;
}

static void
drawParticles (CompScreen     *s,
               ParticleSystem *ps)
{
	glPushMatrix ();

	glEnable (GL_BLEND);
	if (ps->tex)
	{
		glBindTexture (GL_TEXTURE_2D, ps->tex);
		glEnable (GL_TEXTURE_2D);
	}
	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	/* Check that the cache is big enough */
	if (ps->numParticles > ps->vertex_cache_count)
	{
		ps->vertices_cache =
		    realloc (ps->vertices_cache,
		             ps->numParticles * 4 * 3 * sizeof (GLfloat));
		ps->vertex_cache_count = ps->numParticles;
	}

	if (ps->numParticles > ps->coords_cache_count)
	{
		ps->coords_cache =
		    realloc (ps->coords_cache,
		             ps->numParticles * 4 * 2 * sizeof (GLfloat));
		ps->coords_cache_count = ps->numParticles;
	}

	if (ps->numParticles > ps->color_cache_count)
	{
		ps->colors_cache =
		    realloc (ps->colors_cache,
		             ps->numParticles * 4 * 4 * sizeof (GLfloat));
		ps->color_cache_count = ps->numParticles;
	}

	if (ps->darken > 0)
	{
		if (ps->dcolors_cache_count < ps->numParticles)
		{
			ps->dcolors_cache =
			    realloc (ps->dcolors_cache,
			             ps->numParticles * 4 * 4 * sizeof (GLfloat));
			ps->dcolors_cache_count = ps->numParticles;
		}
	}

	GLfloat *dcolors  = ps->dcolors_cache;
	GLfloat *vertices = ps->vertices_cache;
	GLfloat *coords   = ps->coords_cache;
	GLfloat *colors   = ps->colors_cache;

	int cornersSize = sizeof (GLfloat) * 8;
	int colorSize   = sizeof (GLfloat) * 4;

	GLfloat cornerCoords[8] = {0.0, 0.0,
	                           0.0, 1.0,
	                           1.0, 1.0,
	                           1.0, 0.0};

	int numActive = 0;

	Particle *part = ps->particles;
	int i;
	for (i = 0; i < ps->numParticles; i++, part++)
	{
		if (part->life > 0.0f)
		{
			numActive += 4;

			float w = part->width / 2;
			float h = part->height / 2;

			w += (w * part->w_mod) * part->life;
			h += (h * part->h_mod) * part->life;

			vertices[0] = part->x - w;
			vertices[1] = part->y - h;
			vertices[2] = part->z;

			vertices[3] = part->x - w;
			vertices[4] = part->y + h;
			vertices[5] = part->z;

			vertices[6] = part->x + w;
			vertices[7] = part->y + h;
			vertices[8] = part->z;

			vertices[9]  = part->x + w;
			vertices[10] = part->y - h;
			vertices[11] = part->z;

			vertices += 12;

			memcpy (coords, cornerCoords, cornersSize);

			coords += 8;

			colors[0] = part->r;
			colors[1] = part->g;
			colors[2] = part->b;
			colors[3] = part->life * part->a;
			memcpy (colors + 4, colors, colorSize);
			memcpy (colors + 8, colors, colorSize);
			memcpy (colors + 12, colors, colorSize);

			colors += 16;

			if (ps->darken > 0)
			{
				dcolors[0] = part->r;
				dcolors[1] = part->g;
				dcolors[2] = part->b;
				dcolors[3] = part->life * part->a * ps->darken;
				memcpy (dcolors + 4, dcolors, colorSize);
				memcpy (dcolors + 8, dcolors, colorSize);
				memcpy (dcolors + 12, dcolors, colorSize);

				dcolors += 16;
			}
		}
	}

	glEnableClientState (GL_COLOR_ARRAY);

	glTexCoordPointer (2, GL_FLOAT, 2 * sizeof(GLfloat), ps->coords_cache);
	glVertexPointer (3, GL_FLOAT, 3 * sizeof(GLfloat), ps->vertices_cache);

	// darken the background
	if (ps->darken > 0)
	{
		glBlendFunc (GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
		glColorPointer (4, GL_FLOAT, 4 * sizeof(GLfloat), ps->dcolors_cache);
		glDrawArrays (GL_QUADS, 0, numActive);
	}

	// draw particles
	glBlendFunc (GL_SRC_ALPHA, ps->blendMode);

	glColorPointer (4, GL_FLOAT, 4 * sizeof(GLfloat), ps->colors_cache);

	glDrawArrays (GL_QUADS, 0, numActive);

	glDisableClientState (GL_COLOR_ARRAY);

	glPopMatrix ();
	glColor4usv (defaultColor);
	screenTexEnvMode (s, GL_REPLACE);
	glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glDisable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);
}

static void
updateParticles (ParticleSystem * ps,
                 float time)
{
	int i;
	Particle *part;
	float speed    = (time / 50.0);
	float slowdown = ps->slowdown * (1 - MAX(0.99, time / 1000.0)) * 1000;

	ps->active = FALSE;

	part = ps->particles;

	for (i = 0; i < ps->numParticles; i++, part++)
	{
		if (part->life > 0.0f)
		{
			// move particle
			part->x += part->xi / slowdown;
			part->y += part->yi / slowdown;
			part->z += part->zi / slowdown;

			// modify speed
			part->xi += part->xg * speed;
			part->yi += part->yg * speed;
			part->zi += part->zg * speed;

			// modify life
			part->life -= part->fade * speed;
			ps->active  = TRUE;
		}
	}
}

static void
finiParticles (ParticleSystem *ps)
{
	free (ps->particles);
	if (ps->tex)
		glDeleteTextures (1, &ps->tex);

	if (ps->vertices_cache)
		free (ps->vertices_cache);

	if (ps->colors_cache)
		free (ps->colors_cache);

	if (ps->coords_cache)
		free (ps->coords_cache);

	if (ps->dcolors_cache)
		free (ps->dcolors_cache);
}

static void
genNewParticles (CompScreen     *s,
                 ParticleSystem *ps,
                 int            time)
{
	SHOWMOUSE_SCREEN (s);

	const BananaValue *
	option_random = bananaGetOption (bananaIndex,
	                                 "random",
	                                 s->screenNum);

	const BananaValue *
	option_life = bananaGetOption (bananaIndex,
	                               "life",
	                               s->screenNum);

	const BananaValue *
	option_emitters = bananaGetOption (bananaIndex,
	                                   "emitters",
	                                   s->screenNum);

	unsigned int nE = option_emitters->i;

	if (nE == 0)
	{
		ps->active = TRUE; // Don't stop drawing: we may have guides.
		return;
	}

	Bool rColor     = option_random->b;
	float life      = option_life->f;
	float lifeNeg   = 1 - life;
	float fadeExtra = 0.2f * (1.01 - life);
	float max_new   = ps->numParticles * ((float)time / 50) * (1.05 - life);

	const BananaValue *
	option_color = bananaGetOption (bananaIndex,
	                                "color",
	                                s->screenNum);

	unsigned short c[] = { 0, 0, 0, 0 };

	stringToColor (option_color->s, c);

	float colr1 = (float)c[0] / 0xffff;
	float colg1 = (float)c[1] / 0xffff;
	float colb1 = (float)c[2] / 0xffff;
	float colr2 = 1.0 / 4.0 * (float)c[0] / 0xffff;
	float colg2 = 1.0 / 4.0 * (float)c[1] / 0xffff;
	float colb2 = 1.0 / 4.0 * (float)c[2] / 0xffff;
	float cola  = (float)c[3] / 0xffff;
	float rVal;

	const BananaValue *
	option_size = bananaGetOption (bananaIndex,
	                               "size",
	                               s->screenNum);

	float partw = option_size->f * 5;
	float parth = partw;

	Particle *part = ps->particles;
	int i, j;

	const BananaValue *
	option_radius = bananaGetOption (bananaIndex,
	                                 "radius",
	                                 s->screenNum);

	float pos[10][2];
	float rA     = (2 * M_PI) / nE;
	int radius   = option_radius->i;

	for (i = 0; i < nE; i++)
	{
		pos[i][0]  = sin (ss->rot + (i * rA)) * radius;
		pos[i][0] += ss->posX;
		pos[i][1]  = cos (ss->rot + (i * rA)) * radius;
		pos[i][1] += ss->posY;
	}

	for (i = 0; i < ps->numParticles && max_new > 0; i++, part++)
	{
		if (part->life <= 0.0f)
		{
			// give gt new life
			rVal = (float)(random() & 0xff) / 255.0;
			part->life = 1.0f;
			part->fade = rVal * lifeNeg + fadeExtra; // Random Fade Value

			// set size
			part->width = partw;
			part->height = parth;
			rVal = (float)(random() & 0xff) / 255.0;
			part->w_mod = part->h_mod = -1;

			// choose random position
			j        = random() % nE;
			part->x  = pos[j][0];
			part->y  = pos[j][1];
			part->z  = 0.0;
			part->xo = part->x;
			part->yo = part->y;
			part->zo = part->z;

			// set speed and direction
			rVal     = (float)(random() & 0xff) / 255.0;
			part->xi = ((rVal * 20.0) - 10.0f);
			rVal     = (float)(random() & 0xff) / 255.0;
			part->yi = ((rVal * 20.0) - 10.0f);
			part->zi = 0.0f;

			if (rColor)
			{
				// Random colors! (aka Mystical Fire)
				rVal    = (float)(random() & 0xff) / 255.0;
				part->r = rVal;
				rVal    = (float)(random() & 0xff) / 255.0;
				part->g = rVal;
				rVal    = (float)(random() & 0xff) / 255.0;
				part->b = rVal;
			}
			else
			{
				rVal    = (float)(random() & 0xff) / 255.0;
				part->r = colr1 - rVal * colr2;
				part->g = colg1 - rVal * colg2;
				part->b = colb1 - rVal * colb2;
			}
			// set transparancy
			part->a = cola;

			// set gravity
			part->xg = 0.0f;
			part->yg = 0.0f;
			part->zg = 0.0f;

			ps->active = TRUE;
			max_new   -= 1;
		}
	}
}

static void
damageRegion (CompScreen *s)
{
	REGION   r;
	int      i;
	Particle *p;
	float    w, h, x1, x2, y1, y2;

	SHOWMOUSE_SCREEN (s);

	if (!ss->ps)
		return;

	x1 = s->width;
	x2 = 0;
	y1 = s->height;
	y2 = 0;

	p = ss->ps->particles;

	for (i = 0; i < ss->ps->numParticles; i++, p++)
	{
		w = p->width / 2;
		h = p->height / 2;

		w += (w * p->w_mod) * p->life;
		h += (h * p->h_mod) * p->life;
		
		x1 = MIN (x1, p->x - w);
		x2 = MAX (x2, p->x + w);
		y1 = MIN (y1, p->y - h);
		y2 = MAX (y2, p->y + h);
	}

	r.rects = &r.extents;
	r.numRects = r.size = 1;

	r.extents.x1 = floor (x1);
	r.extents.x2 = ceil (x2);
	r.extents.y1 = floor (y1);
	r.extents.y2 = ceil (y2);

	damageScreenRegion (s, &r);
}

static void
positionUpdate (CompScreen *s,
                int        x,
                int        y)
{
	SHOWMOUSE_SCREEN (s);

	ss->posX = x;
	ss->posY = y;
}

static void
showmousePreparePaintScreen (CompScreen *s,
                             int        time)
{
	SHOWMOUSE_SCREEN (s);

	if (ss->active && !ss->pollHandle)
	{
		getCurrentMousePosition (s, &ss->posX, &ss->posY);
		ss->pollHandle = addPositionPollingCallback (s, positionUpdate);
	}

	if (ss->active && !ss->ps)
	{
		ss->ps = malloc (sizeof(ParticleSystem));
		if (!ss->ps)
		{
			UNWRAP (ss, s, preparePaintScreen);
			(*s->preparePaintScreen) (s, time);
			WRAP (ss, s, preparePaintScreen, showmousePreparePaintScreen);
			return;
		}

		ss->ps->particles = NULL;

		const BananaValue *
		option_num_particles = bananaGetOption (bananaIndex,
		                                        "num_particles",
		                                        s->screenNum);

		const BananaValue *
		option_slowdown = bananaGetOption (bananaIndex,
		                                   "slowdown",
		                                   s->screenNum);

		const BananaValue *
		option_darken = bananaGetOption (bananaIndex,
		                                 "darken",
		                                 s->screenNum);

		const BananaValue *
		option_blend = bananaGetOption (bananaIndex,
		                                "blend",
		                                s->screenNum);

		initParticles (option_num_particles->i, ss->ps);

		ss->ps->slowdown = option_slowdown->f;
		ss->ps->darken = option_darken->f;
		ss->ps->blendMode = (option_blend->b) ? GL_ONE :
		                                        GL_ONE_MINUS_SRC_ALPHA;

		glGenTextures (1, &ss->ps->tex);
		glBindTexture (GL_TEXTURE_2D, ss->ps->tex);

		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
		              GL_RGBA, GL_UNSIGNED_BYTE, starTex);

		glBindTexture (GL_TEXTURE_2D, 0);
	}

	const BananaValue *
	option_rotation_speed = bananaGetOption (bananaIndex,
	                                         "rotation_speed",
	                                         s->screenNum);

	if (ss->active)
		ss->rot = fmod (ss->rot + (((float)time / 1000.0) * 2 * M_PI *
		          option_rotation_speed->f), 2 * M_PI);

	if (ss->ps && ss->ps->active)
	{
		updateParticles (ss->ps, time);
		damageRegion (s);
	}

	if (ss->ps && ss->active)
		genNewParticles (s, ss->ps, time);

	UNWRAP (ss, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, time);
	WRAP (ss, s, preparePaintScreen, showmousePreparePaintScreen);
}

static void
showmouseDonePaintScreen (CompScreen *s)
{
	SHOWMOUSE_SCREEN (s);

	if (ss->active || (ss->ps && ss->ps->active))
		damageRegion (s);

	if (!ss->active && ss->pollHandle)
	{
		removePositionPollingCallback (s, ss->pollHandle);
		ss->pollHandle = 0;
	}

	if (!ss->active && ss->ps && !ss->ps->active)
	{
		finiParticles (ss->ps);
		free (ss->ps);
		ss->ps = NULL;
	}

	UNWRAP (ss, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP (ss, s, donePaintScreen, showmouseDonePaintScreen);
}

static void
drawLine (double         x1,
          double         y1,
          double         x2,
          double         y2,
          unsigned short *color)
{
	glBegin (GL_LINES);

	glColor3f ((float)color[0] / (float)256,
	           (float)color[1] / (float)256,
	           (float)color[2] / (float)256);

	glVertex2f ((GLfloat) x1, (GLfloat) y1);
	glVertex2f ((GLfloat) x2, (GLfloat) y2);
	glEnd ();
}

static void
drawGuides (CompScreen *s)
{
	const BananaValue *
	option_guide_color = bananaGetOption (bananaIndex,
	                                      "guide_color",
	                                      s->screenNum);

	const BananaValue *
	option_guide_thickness = bananaGetOption (bananaIndex,
	                                          "guide_thickness",
	                                          s->screenNum);

	const BananaValue *
	option_guide_empty_radius = bananaGetOption (bananaIndex,
	                                             "guide_empty_radius",
	                                             s->screenNum);

	unsigned short color[] = { 0, 0, 0, 0 };
	stringToColor (option_guide_color->s, color);

	int x, y;
	getCurrentMousePosition (s, &x, &y);

	if (option_guide_thickness->i > 0)
	{
		glLineWidth (option_guide_thickness->i);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glEnable (GL_BLEND);

		drawLine (x, 0, x, y - option_guide_empty_radius->i, color);
		drawLine (x, y + option_guide_empty_radius->i, x, s->height, color);
		drawLine (0, y, x - option_guide_empty_radius->i, y, color);
		drawLine (x + option_guide_empty_radius->i, y, s->width, y, color);

		glDisable (GL_BLEND);
	}

	//TODO: maybe use damageScreenRegion instead ? 
	//see the patch in commit dfd65b0ee4f2629cbb528f5617bfa00ca97173a9
	damageScreen (s);
}

static Bool
showmousePaintOutput (CompScreen              *s,
                      const ScreenPaintAttrib *sa,
                      const CompTransform     *transform,
                      Region                  region,
                      CompOutput              *output,
                      unsigned int            mask)
{
	Bool           status;
	CompTransform  sTransform;

	SHOWMOUSE_SCREEN (s);

	UNWRAP (ss, s, paintOutput);
	status = (*s->paintOutput) (s, sa, transform, region, output, mask);
	WRAP (ss, s, paintOutput, showmousePaintOutput);

	if (!ss->ps || !ss->ps->active)
		return status;

	matrixGetIdentity (&sTransform);

	transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

	glPushMatrix ();
	glLoadMatrixf (sTransform.m);

	drawGuides (s);

	const BananaValue *
	option_emitters = bananaGetOption (bananaIndex,
	                                   "emitters",
	                                   s->screenNum);

	if (option_emitters->i > 0)
		drawParticles (s, ss->ps);

	glPopMatrix();

	glColor4usv (defaultColor);

	return status;
}

static Bool
showmouseInitScreen (CompPlugin *p,
                     CompScreen *s)
{
	SHOWMOUSE_DISPLAY (&display);

	ShowmouseScreen *ss = malloc (sizeof (ShowmouseScreen));

	if (!ss)
		return FALSE;

	ss->active = FALSE;

	ss->pollHandle = 0;

	ss->ps  = NULL;
	ss->rot = 0;

	WRAP (ss, s, paintOutput, showmousePaintOutput);
	WRAP (ss, s, preparePaintScreen, showmousePreparePaintScreen);
	WRAP (ss, s, donePaintScreen, showmouseDonePaintScreen);

	s->privates[sd->screenPrivateIndex].ptr = ss;

	return TRUE;
}

static void
showmouseFiniScreen (CompPlugin *p,
                     CompScreen *s)
{
	SHOWMOUSE_SCREEN (s);

	UNWRAP (ss, s, paintOutput);
	UNWRAP (ss, s, preparePaintScreen);
	UNWRAP (ss, s, donePaintScreen);

	if (ss->pollHandle)
		removePositionPollingCallback (s, ss->pollHandle);

	if (ss->ps && ss->ps->active)
		damageScreen (s);

	free (ss);
}

static void
showmouseHandleEvent (XEvent      *event)
{
	CompScreen *s;

	SHOWMOUSE_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &sd->initiate_key))
		{
			s = findScreenAtDisplay (event->xkey.root);
			if (s)
			{
				SHOWMOUSE_SCREEN (s);

				if (ss->active)
				{
					ss->active = FALSE;
					damageRegion (s);
				}
				else
				{
					ss->active = TRUE;
					damageRegion (s);
				}
			}
		}
		break;
	case ButtonPress:
		if (isButtonPressEvent (event, &sd->initiate_button))
		{
			s = findScreenAtDisplay (event->xbutton.root);
			if (s)
			{
				SHOWMOUSE_SCREEN (s);

				if (ss->active)
				{
					ss->active = FALSE;
					damageRegion (s);
				}
				else
					ss->active = TRUE;
			}
		}
	default:
		break;
	}

	UNWRAP (sd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (sd, &display, handleEvent, showmouseHandleEvent);
}

static Bool
showmouseInitDisplay (CompPlugin  *p,
                      CompDisplay *d)
{
	ShowmouseDisplay *sd;

	sd = malloc (sizeof (ShowmouseDisplay));

	if (!sd)
		return FALSE;

	sd->screenPrivateIndex = allocateScreenPrivateIndex ();

	if (sd->screenPrivateIndex < 0)
	{
		free (sd);
		return FALSE;
	}

	const BananaValue *
	option_initiate_button = bananaGetOption (bananaIndex,
	                                          "initiate_button",
	                                          -1);

	const BananaValue *
	option_initiate_key = bananaGetOption (bananaIndex,
	                                       "initiate_key",
	                                       -1);

	registerButton (option_initiate_button->s, &sd->initiate_button);
	registerKey (option_initiate_key->s, &sd->initiate_key);

	WRAP (sd, d, handleEvent, showmouseHandleEvent);

	d->privates[displayPrivateIndex].ptr = sd;

	return TRUE;
}

static void
showmouseFiniDisplay (CompPlugin  *p,
                      CompDisplay *d)
{
	SHOWMOUSE_DISPLAY (d);

	freeScreenPrivateIndex (sd->screenPrivateIndex);

	UNWRAP (sd, d, handleEvent);

	free (sd);
}

static void
showmouseChangeNotify (const char        *optionName,
                       BananaType        optionType,
                       const BananaValue *optionValue,
                       int               screenNum)
{
	SHOWMOUSE_DISPLAY (&display);

	if (strcasecmp (optionName, "initiate_button") == 0)
		updateButton (optionValue->s, &sd->initiate_button);

	else if (strcasecmp (optionName, "initiate_key") == 0)
		updateKey (optionValue->s, &sd->initiate_key);
}

static Bool
showmouseInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("showmouse", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("showmouse");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, showmouseChangeNotify);

	return TRUE;
}

static void
showmouseFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable showmouseVTable = {
	"showmouse",
	showmouseInit,
	showmouseFini,
	showmouseInitDisplay,
	showmouseFiniDisplay,
	showmouseInitScreen,
	showmouseFiniScreen,
	NULL, /* showmouseInitWindow */
	NULL  /* showmouseFiniWindow */
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &showmouseVTable;
}
