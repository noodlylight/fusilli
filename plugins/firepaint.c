/*
 * Compiz fire effect plugin
 *
 * firepaint.c
 *
 * Copyright : (C) 2007 by Dennis Kasprzyk
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

#include <compiz-core.h>

#include "firepaint_options.h"
#include "firepaint_tex.h"

static int displayPrivateIndex = 0;

/* =====================  Particle engine  ========================= */

typedef struct _Particle
{
    float life;		/* particle life */
    float fade;		/* fade speed */
    float width;	/* particle width */
    float height;	/* particle height */
    float w_mod;	/* particle size modification during life */
    float h_mod;	/* particle size modification during life */
    float r;		/* red value */
    float g;		/* green value */
    float b;		/* blue value */
    float a;		/* alpha value */
    float x;		/* X position */
    float y;		/* Y position */
    float z;		/* Z position */
    float xi;		/* X direction */
    float yi;		/* Y direction */
    float zi;		/* Z direction */
    float xg;		/* X gravity */
    float yg;		/* Y gravity */
    float zg;		/* Z gravity */
    float xo;		/* orginal X position */
    float yo;		/* orginal Y position */
    float zo;		/* orginal Z position */
}
Particle;

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

    /* Moved from drawParticles to get rid of spurious malloc's */
    GLfloat *vertices_cache;
    GLfloat *coords_cache;
    GLfloat *colors_cache;
    GLfloat *dcolors_cache;

    int coords_cache_count;
    int vertex_cache_count;
    int color_cache_count;
    int dcolors_cache_count;
}
ParticleSystem;


static void
initParticles (int            numParticles,
	       ParticleSystem *ps)
{
    if (ps->particles)
	free (ps->particles);

    ps->particles = calloc (1, sizeof (Particle) * numParticles);
    ps->tex = 0;
    ps->numParticles = numParticles;
    ps->slowdown = 1;
    ps->active = FALSE;

    // Initialize cache
    ps->vertices_cache = NULL;
    ps->colors_cache   = NULL;
    ps->coords_cache   = NULL;
    ps->dcolors_cache  = NULL;

    ps->vertex_cache_count  = 0;
    ps->color_cache_count   = 0;
    ps->coords_cache_count  = 0;
    ps->dcolors_cache_count = 0;

    int i;

    for (i = 0; i < numParticles; i++)
    {
	ps->particles[i].life = 0.0f;
    }
}

static void
drawParticles (CompScreen     *s,
	       ParticleSystem *ps)
{
    GLfloat *dcolors;
    GLfloat *vertices;
    GLfloat *coords;
    GLfloat *colors;

    glPushMatrix ();

    glEnable (GL_BLEND);

    if (ps->tex)
    {
	glBindTexture (GL_TEXTURE_2D, ps->tex);
	glEnable (GL_TEXTURE_2D);
    }

    glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    int i;
    Particle *part;

    /* Check that the cache is big enough */

    if (ps->numParticles > ps->vertex_cache_count)
    {
	ps->vertices_cache = realloc (ps->vertices_cache,
				      ps->numParticles * 4 * 3 *
				      sizeof (GLfloat));
	ps->vertex_cache_count = ps->numParticles;
    }

    if (ps->numParticles > ps->coords_cache_count)
    {
	ps->coords_cache = realloc (ps->coords_cache,
				    ps->numParticles * 4 * 2 *
				    sizeof (GLfloat));
	ps->coords_cache_count = ps->numParticles;
    }

    if (ps->numParticles > ps->color_cache_count)
    {
	ps->colors_cache = realloc (ps->colors_cache,
				    ps->numParticles * 4 * 4 *
				    sizeof (GLfloat));
	ps->color_cache_count = ps->numParticles;
    }

    if (ps->darken > 0)
    {
	if (ps->dcolors_cache_count < ps->numParticles)
	{
	    ps->dcolors_cache = realloc (ps->dcolors_cache,
					 ps->numParticles * 4 * 4 *
					 sizeof (GLfloat));
	    ps->dcolors_cache_count = ps->numParticles;
	}
    }

    dcolors  = ps->dcolors_cache;
    vertices = ps->vertices_cache;
    coords   = ps->coords_cache;
    colors   = ps->colors_cache;

    int numActive = 0;

    for (i = 0; i < ps->numParticles; i++)
    {
	part = &ps->particles[i];

	if (part->life > 0.0f)
	{
	    numActive += 4;

	    float w = part->width / 2;
	    float h = part->height / 2;

	    w += (w * part->w_mod) * part->life;
	    h += (h * part->h_mod) * part->life;

	    vertices[0]  = part->x - w;
	    vertices[1]  = part->y - h;
	    vertices[2]  = part->z;

	    vertices[3]  = part->x - w;
	    vertices[4]  = part->y + h;
	    vertices[5]  = part->z;

	    vertices[6]  = part->x + w;
	    vertices[7]  = part->y + h;
	    vertices[8]  = part->z;

	    vertices[9]  = part->x + w;
	    vertices[10] = part->y - h;
	    vertices[11] = part->z;

	    vertices += 12;

	    coords[0] = 0.0;
	    coords[1] = 0.0;

	    coords[2] = 0.0;
	    coords[3] = 1.0;

	    coords[4] = 1.0;
	    coords[5] = 1.0;

	    coords[6] = 1.0;
	    coords[7] = 0.0;

	    coords += 8;

	    colors[0]  = part->r;
	    colors[1]  = part->g;
	    colors[2]  = part->b;
	    colors[3]  = part->life * part->a;
	    colors[4]  = part->r;
	    colors[5]  = part->g;
	    colors[6]  = part->b;
	    colors[7]  = part->life * part->a;
	    colors[8]  = part->r;
	    colors[9]  = part->g;
	    colors[10] = part->b;
	    colors[11] = part->life * part->a;
	    colors[12] = part->r;
	    colors[13] = part->g;
	    colors[14] = part->b;
	    colors[15] = part->life * part->a;

	    colors += 16;

	    if (ps->darken > 0)
	    {

		dcolors[0]  = part->r;
		dcolors[1]  = part->g;
		dcolors[2]  = part->b;
		dcolors[3]  = part->life * part->a * ps->darken;
		dcolors[4]  = part->r;
		dcolors[5]  = part->g;
		dcolors[6]  = part->b;
		dcolors[7]  = part->life * part->a * ps->darken;
		dcolors[8]  = part->r;
		dcolors[9]  = part->g;
		dcolors[10] = part->b;
		dcolors[11] = part->life * part->a * ps->darken;
		dcolors[12] = part->r;
		dcolors[13] = part->g;
		dcolors[14] = part->b;
		dcolors[15] = part->life * part->a * ps->darken;

		dcolors += 16;
	    }
	}
    }

    glEnableClientState (GL_COLOR_ARRAY);

    glTexCoordPointer (2, GL_FLOAT, 2 * sizeof (GLfloat), ps->coords_cache);
    glVertexPointer (3, GL_FLOAT, 3 * sizeof (GLfloat), ps->vertices_cache);

    // darken the background

    if (ps->darken > 0)
    {
	glBlendFunc (GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
	glColorPointer (4, GL_FLOAT, 4 * sizeof (GLfloat), ps->dcolors_cache);
	glDrawArrays (GL_QUADS, 0, numActive);
    }

    // draw particles
    glBlendFunc (GL_SRC_ALPHA, ps->blendMode);

    glColorPointer (4, GL_FLOAT, 4 * sizeof (GLfloat), ps->colors_cache);
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
updateParticles (ParticleSystem *ps,
		 float          time)
{
    int i;
    Particle *part;
    float speed = (time / 50.0);
    float slowdown = ps->slowdown * (1 - MAX (0.99, time / 1000.0) ) * 1000;

    ps->active = FALSE;

    for (i = 0; i < ps->numParticles; i++)
    {
	part = &ps->particles[i];

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
	    ps->active = TRUE;
	}
    }
}

static void
finiParticles (ParticleSystem * ps)
{
    free (ps->particles);
    ps->particles = NULL;

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

/* =====================  END: Particle engine  ========================= */

#define NUM_ADD_POINTS 1000

typedef struct _FireDisplay
{
    int screenPrivateIndex;
    HandleEventProc handleEvent;

}
FireDisplay;


typedef struct _FireScreen
{
    ParticleSystem ps;
    Bool init;

    XPoint *points;
    int    pointsSize;
    int    numPoints;
    
    float brightness;

    int grabIndex;

    PreparePaintScreenProc preparePaintScreen;
    PaintOutputProc        paintOutput;
    DonePaintScreenProc    donePaintScreen;

}
FireScreen;

#define GET_FIRE_DISPLAY(d)                                  \
    ((FireDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define FIRE_DISPLAY(d)                                      \
    FireDisplay *fd = GET_FIRE_DISPLAY (d)

#define GET_FIRE_SCREEN(s, fd)                               \
    ((FireScreen *) (s)->base.privates[(fd)->screenPrivateIndex].ptr)

#define FIRE_SCREEN(s)                                       \
    FireScreen *fs = GET_FIRE_SCREEN (s, GET_FIRE_DISPLAY (s->display))

static void
fireAddPoint (CompScreen *s,
	      int        x,
	      int        y,
	      Bool       requireGrab)
{
    FIRE_SCREEN (s);

    if (!requireGrab || fs->grabIndex)
    {
	if (fs->pointsSize < fs->numPoints + 1)
	{
	    fs->points = realloc (fs->points,
				  (fs->pointsSize + NUM_ADD_POINTS) *
				  sizeof (XPoint));
	    fs->pointsSize += NUM_ADD_POINTS;
	}

	fs->points[fs->numPoints].x = x;
	fs->points[fs->numPoints].y = y;
	fs->numPoints++;
    }
}


static Bool
fireAddParticle (CompDisplay     *d,
		 CompAction      *action,
		 CompActionState state,
		 CompOption      *option,
		 int		 nOption)
{
    CompScreen *s;
    Window     xid;

    xid  = getIntOptionNamed (option, nOption, "root", 0);

    s = findScreenAtDisplay (d, xid);
    if (s)
    {
	float x, y;

	x = getFloatOptionNamed (option, nOption, "x", 0);
	y = getFloatOptionNamed (option, nOption, "y", 0);

	fireAddPoint (s, x, y, FALSE);

	damageScreen (s);
    }

    return FALSE;
}


static Bool
fireInitiate (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption)
{
    CompScreen *s;
    Window xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    s = findScreenAtDisplay (d, xid);

    if (s)
    {
	FIRE_SCREEN (s);

	if (otherScreenGrabExist (s, NULL) )
	    return FALSE;

	if (!fs->grabIndex)
	    fs->grabIndex = pushScreenGrab (s, None, "firepaint");

	if (state & CompActionStateInitButton)
	    action->state |= CompActionStateTermButton;

	if (state & CompActionStateInitKey)
	    action->state |= CompActionStateTermKey;

	fireAddPoint (s, pointerX, pointerY, TRUE);

    }

    return TRUE;
}

static Bool
fireTerminate (CompDisplay     *d,
	       CompAction      *action,
	       CompActionState state,
	       CompOption      *option,
	       int             nOption)
{
    CompScreen *s;
    Window xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    for (s = d->screens; s; s = s->next)
    {
	FIRE_SCREEN (s);

	if (xid && s->root != xid)
	    continue;

	if (fs->grabIndex)
	{
	    removeScreenGrab (s, fs->grabIndex, NULL);
	    fs->grabIndex = 0;
	}
    }

    action->state &= ~ (CompActionStateTermKey | CompActionStateTermButton);

    return FALSE;
}


static Bool
fireClear (CompDisplay     *d,
	   CompAction      *action,
	   CompActionState state,
	   CompOption      *option,
	   int             nOption)
{
    CompScreen *s;
    Window xid;

    xid = getIntOptionNamed (option, nOption, "root", 0);

    s = findScreenAtDisplay (d, xid);

    if (s)
    {
	FIRE_SCREEN (s);
	fs->numPoints = 0;
	return TRUE;
    }

    return FALSE;
}


static void
firePreparePaintScreen (CompScreen *s,
			int        time)
{
    int i;
    float size = 4;
    float bg = (float) firepaintGetBgBrightness (s) / 100.0;

    FIRE_SCREEN (s);

    if (fs->init && fs->numPoints)
    {
	initParticles (firepaintGetNumParticles (s), &fs->ps);
	fs->init = FALSE;

	glGenTextures (1, &fs->ps.tex);
	glBindTexture (GL_TEXTURE_2D, fs->ps.tex);

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
		      GL_RGBA, GL_UNSIGNED_BYTE, fireTex);
	glBindTexture (GL_TEXTURE_2D, 0);

	fs->ps.slowdown  = firepaintGetFireSlowdown (s);
	fs->ps.darken    = 0.5;
	fs->ps.blendMode = GL_ONE;

    }

    if (!fs->init)
	updateParticles (&fs->ps, time);

    if (fs->numPoints)
    {
	float max_new = MIN (fs->ps.numParticles,  fs->numPoints * 2) *
			((float) time / 50.0) *
			(1.05 -	firepaintGetFireLife(s));
	Particle *part;
	float rVal;
	int rVal2;

	for (i = 0; i < fs->ps.numParticles && max_new > 0; i++)
	{
	    part = &fs->ps.particles[i];

	    if (part->life <= 0.0f)
	    {
		/* give gt new life */
		rVal = (float) (random () & 0xff) / 255.0;
		part->life = 1.0f;
		/* Random Fade Value */
		part->fade = (rVal * (1 - firepaintGetFireLife (s)) +
			      (0.2f * (1.01 - firepaintGetFireLife (s))));

		/* set size */
		part->width  = firepaintGetFireSize (s);
		part->height = firepaintGetFireSize (s) * 1.5;
		rVal = (float) (random () & 0xff) / 255.0;
		part->w_mod = size * rVal;
		part->h_mod = size * rVal;

		/* choose random position */
		rVal2 = random () % fs->numPoints;
		part->x = fs->points[rVal2].x;
		part->y = fs->points[rVal2].y;
		part->z = 0.0;
		part->xo = part->x;
		part->yo = part->y;
		part->zo = part->z;

		/* set speed and direction */
		rVal = (float) (random () & 0xff) / 255.0;
		part->xi = ( (rVal * 20.0) - 10.0f);
		rVal = (float) (random () & 0xff) / 255.0;
		part->yi = ( (rVal * 20.0) - 15.0f);
		part->zi = 0.0f;
		rVal = (float) (random () & 0xff) / 255.0;

		if (firepaintGetFireMystical (s) )
		{
		    /* Random colors! (aka Mystical Fire) */
		    rVal = (float) (random () & 0xff) / 255.0;
		    part->r = rVal;
		    rVal = (float) (random () & 0xff) / 255.0;
		    part->g = rVal;
		    rVal = (float) (random () & 0xff) / 255.0;
		    part->b = rVal;
		}
		else
		{
		    part->r = (float) firepaintGetFireColorRed (s) / 0xffff -
			      (rVal / 1.7 *
			       (float) firepaintGetFireColorRed (s) / 0xffff);
		    part->g = (float) firepaintGetFireColorGreen (s) / 0xffff -
			      (rVal / 1.7 *
			      (float) firepaintGetFireColorGreen (s) / 0xffff);
		    part->b = (float) firepaintGetFireColorBlue (s) / 0xffff -
			      (rVal / 1.7 *
			      (float) firepaintGetFireColorBlue (s) / 0xffff);
		}

		/* set transparancy */
		part->a = (float) firepaintGetFireColorAlpha (s) / 0xffff;

		/* set gravity */
		part->xg = (part->x < part->xo) ? 1.0 : -1.0;
		part->yg = -3.0f;
		part->zg = 0.0f;

		fs->ps.active = TRUE;

		max_new -= 1;
	    }
	    else
	    {
		part->xg = (part->x < part->xo) ? 1.0 : -1.0;
	    }
	}
    }

    if (fs->numPoints && fs->brightness != bg)
    {
	float div = 1.0 - bg;
	div *= (float) time / 500.0;
	fs->brightness = MAX (bg, fs->brightness - div);

    }

    if (!fs->numPoints && fs->brightness != 1.0)
    {
	float div = 1.0 - bg;
	div *= (float) time / 500.0;
	fs->brightness = MIN (1.0, fs->brightness + div);

    }

    if (!fs->init && !fs->numPoints && !fs->ps.active)
    {
	finiParticles (&fs->ps);
	fs->init = TRUE;
    }

    UNWRAP (fs, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, time);
    WRAP (fs, s, preparePaintScreen, firePreparePaintScreen);
}

static Bool
firePaintOutput (CompScreen              *s,
		 const ScreenPaintAttrib *sAttrib,
		 const CompTransform     *transform,
		 Region                  region,
		 CompOutput              *output,
		 unsigned int            mask)
{
    Bool status;

    FIRE_SCREEN (s);

    UNWRAP (fs, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
    WRAP (fs, s, paintOutput, firePaintOutput);

    if ( (!fs->init && fs->ps.active) || fs->brightness < 1.0)
    {
	CompTransform sTransform = *transform;

	transformToScreenSpace (s, output, -DEFAULT_Z_CAMERA, &sTransform);

	glPushMatrix ();
	glLoadMatrixf (sTransform.m);

	if (fs->brightness < 1.0)
	{
	    glColor4f (0.0, 0.0, 0.0, 1.0 - fs->brightness);
	    glEnable (GL_BLEND);
	    glBegin (GL_QUADS);
	    glVertex2d (output->region.extents.x1,
			output->region.extents.y1);
	    glVertex2d (output->region.extents.x1,
			output->region.extents.y2);
	    glVertex2d (output->region.extents.x2,
			output->region.extents.y2);
	    glVertex2d (output->region.extents.x2,
			output->region.extents.y1);
	    glEnd ();
	    glDisable (GL_BLEND);
	    glColor4usv (defaultColor);
	}

	if (!fs->init && fs->ps.active)
	    drawParticles (s, &fs->ps);

	glPopMatrix ();
    }

    return status;
}



static void
fireDonePaintScreen (CompScreen * s)
{
    FIRE_SCREEN (s);

    if ( (!fs->init && fs->ps.active) || fs->numPoints || fs->brightness < 1.0)
    {
	damageScreen (s);
    }

    UNWRAP (fs, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (fs, s, donePaintScreen, fireDonePaintScreen);
}


static void
fireHandleEvent (CompDisplay *d,
		 XEvent      *event)
{
    CompScreen *s;

    FIRE_DISPLAY (d);

    switch (event->type)
    {

    case MotionNotify:
	s = findScreenAtDisplay (d, event->xmotion.root);
	if (s)
	    fireAddPoint (s, pointerX, pointerY, TRUE);
	break;

    case EnterNotify:
    case LeaveNotify:
	s = findScreenAtDisplay (d, event->xcrossing.root);
	if (s)
	    fireAddPoint (s, pointerX, pointerY, TRUE);

    default:
	break;
    }

    UNWRAP (fd, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP (fd, d, handleEvent, fireHandleEvent);
}

static Bool
fireInitDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    FireDisplay *fd;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    fd = calloc (1, sizeof (FireDisplay) );

    if (!fd)
	return FALSE;

    fd->screenPrivateIndex = allocateScreenPrivateIndex (d);

    if (fd->screenPrivateIndex < 0)
    {
	free (fd);
	return FALSE;
    }

    d->base.privates[displayPrivateIndex].ptr = fd;

    WRAP (fd, d, handleEvent, fireHandleEvent);

    firepaintSetInitiateKeyInitiate (d, fireInitiate);
    firepaintSetInitiateKeyTerminate (d, fireTerminate);
    firepaintSetInitiateButtonInitiate (d, fireInitiate);
    firepaintSetInitiateButtonTerminate (d, fireTerminate);
    firepaintSetClearKeyInitiate (d, fireClear);
    firepaintSetClearButtonInitiate (d, fireClear);
    firepaintSetAddParticleInitiate (d, fireAddParticle);

    return TRUE;
}

static void
fireFiniDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    FIRE_DISPLAY (d);
    freeScreenPrivateIndex (d, fd->screenPrivateIndex);

    UNWRAP (fd, d, handleEvent);

    free (fd);
}


static Bool
fireInitScreen (CompPlugin *p,
		CompScreen *s)
{
    FireScreen *fs;

    FIRE_DISPLAY (s->display);

    fs = calloc (1, sizeof (FireScreen) );

    if (!fs)
	return FALSE;

    s->base.privates[fd->screenPrivateIndex].ptr = fs;

    fs->points     = NULL;
    fs->pointsSize = 0;
    fs->numPoints  = 0;

    fs->grabIndex = 0;

    fs->brightness = 1.0;

    fs->init = TRUE;

    WRAP (fs, s, preparePaintScreen, firePreparePaintScreen);
    WRAP (fs, s, paintOutput, firePaintOutput);
    WRAP (fs, s, donePaintScreen, fireDonePaintScreen);

    return TRUE;
}


static void
fireFiniScreen (CompPlugin *p,
		CompScreen *s)
{
    FIRE_SCREEN (s);

    UNWRAP (fs, s, preparePaintScreen);
    UNWRAP (fs, s, paintOutput);
    UNWRAP (fs, s, donePaintScreen);

    if (!fs->init)
	finiParticles (&fs->ps);

    if (fs->points)
	free (fs->points);

    free (fs);
}

static Bool
fireInit (CompPlugin * p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();

    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
fireFini (CompPlugin * p)
{
    if (displayPrivateIndex >= 0)
	freeDisplayPrivateIndex (displayPrivateIndex);
}

static CompBool
fireInitObject (CompPlugin *p,
		 CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) fireInitDisplay,
	(InitPluginObjectProc) fireInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
fireFiniObject (CompPlugin *p,
		 CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) fireFiniDisplay,
	(FiniPluginObjectProc) fireFiniScreen
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

CompPluginVTable fireVTable = {
    "firepaint",
    0,
    fireInit,
    fireFini,
    fireInitObject,
    fireFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &fireVTable;
}
