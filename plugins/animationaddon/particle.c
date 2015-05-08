/*
 * Animation plugin for compiz/beryl
 *
 * animation.c
 *
 * Copyright : (C) 2006 Erkin Bahceci
 * E-mail    : erkinbah@gmail.com
 *
 * Based on Wobbly and Minimize plugins by
 *           : David Reveman
 * E-mail    : davidr@novell.com>
 *
 * Particle system added by : (C) 2006 Dennis Kasprzyk
 * E-mail                   : onestone@beryl-project.org
 *
 * Beam-Up added by : Florencio Guimaraes
 * E-mail           : florencio@nexcorp.com.br
 *
 * Hexagon tessellator added by : Mike Slegeir
 * E-mail                       : mikeslegeir@mail.utexas.edu>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "animationaddon.h"

void initParticles(int numParticles, ParticleSystem * ps)
{
    if (ps->particles)
	free(ps->particles);
    ps->particles = (Particle *) malloc (numParticles * sizeof (Particle));
    ps->tex = 0;
    ps->numParticles = numParticles;
    ps->slowdown = 1;
    ps->active = FALSE;

    // Initialize cache
    ps->vertices_cache = NULL;
    ps->colors_cache = NULL;
    ps->coords_cache = NULL;
    ps->dcolors_cache = NULL;
    ps->vertex_cache_count = 0;
    ps->color_cache_count = 0;
    ps->coords_cache_count = 0;
    ps->dcolors_cache_count = 0;

    Particle *part = ps->particles;
    int i;
    for (i = 0; i < numParticles; i++, part++)
	part->life = 0.0f;
}

void drawParticles (CompWindow * w, ParticleSystem * ps)
{
    CompScreen *s = w->screen;

    glPushMatrix();
    if (w)
	glTranslated(WIN_X(w) - ps->x, WIN_Y(w) - ps->y, 0);

    glEnable(GL_BLEND);
    if (ps->tex)
    {
	glBindTexture(GL_TEXTURE_2D, ps->tex);
	glEnable(GL_TEXTURE_2D);
    }
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    /* Check that the cache is big enough */
    if (ps->numParticles > ps->vertex_cache_count)
    {
	ps->vertices_cache =
	    realloc(ps->vertices_cache,
		    ps->numParticles * 4 * 3 * sizeof(GLfloat));
	ps->vertex_cache_count = ps->numParticles;
    }

    if (ps->numParticles > ps->coords_cache_count)
    {
	ps->coords_cache =
	    realloc(ps->coords_cache,
		    ps->numParticles * 4 * 2 * sizeof(GLfloat));
	ps->coords_cache_count = ps->numParticles;
    }

    if (ps->numParticles > ps->color_cache_count)
    {
	ps->colors_cache =
	    realloc(ps->colors_cache,
		    ps->numParticles * 4 * 4 * sizeof(GLfloat));
	ps->color_cache_count = ps->numParticles;
    }

    if (ps->darken > 0)
    {
	if (ps->dcolors_cache_count < ps->numParticles)
	{
	    ps->dcolors_cache =
		realloc(ps->dcolors_cache,
			ps->numParticles * 4 * 4 * sizeof(GLfloat));
	    ps->dcolors_cache_count = ps->numParticles;
	}
    }

    GLfloat *dcolors = ps->dcolors_cache;
    GLfloat *vertices = ps->vertices_cache;
    GLfloat *coords = ps->coords_cache;
    GLfloat *colors = ps->colors_cache;

    int cornersSize = sizeof (GLfloat) * 8;
    int colorSize = sizeof (GLfloat) * 4;

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

	    vertices[9] = part->x + w;
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

    glEnableClientState(GL_COLOR_ARRAY);

    glTexCoordPointer(2, GL_FLOAT, 2 * sizeof(GLfloat), ps->coords_cache);
    glVertexPointer(3, GL_FLOAT, 3 * sizeof(GLfloat), ps->vertices_cache);

    // darken the background
    if (ps->darken > 0)
    {
	glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
	glColorPointer(4, GL_FLOAT, 4 * sizeof(GLfloat), ps->dcolors_cache);
	glDrawArrays(GL_QUADS, 0, numActive);
    }
    // draw particles
    glBlendFunc(GL_SRC_ALPHA, ps->blendMode);

    glColorPointer(4, GL_FLOAT, 4 * sizeof(GLfloat), ps->colors_cache);

    glDrawArrays(GL_QUADS, 0, numActive);

    glDisableClientState(GL_COLOR_ARRAY);

    glPopMatrix();
    glColor4usv(defaultColor);
    screenTexEnvMode(s, GL_REPLACE);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void drawParticleSystems (CompWindow * w)
{
    ANIMADDON_WINDOW (w);

    if (aw->eng.numPs && !WINDOW_INVISIBLE(w))
    {
	int i = 0;

	for (i = 0; i < aw->eng.numPs; i++)
	{
	    if (aw->eng.ps[i].active)
		drawParticles (w, &aw->eng.ps[i]);
	}
    }
}

void updateParticles(ParticleSystem * ps, float time)
{
    int i;
    Particle *part;
    float speed = (time / 50.0);
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
	    ps->active = TRUE;
	}
    }
}

void finiParticles(ParticleSystem * ps)
{
    free(ps->particles);
    if (ps->tex)
	glDeleteTextures(1, &ps->tex);

    if (ps->vertices_cache)
	free(ps->vertices_cache);
    if (ps->colors_cache)
	free(ps->colors_cache);
    if (ps->coords_cache)
	free(ps->coords_cache);
    if (ps->dcolors_cache)
	free(ps->dcolors_cache);
}

void
particlesUpdateBB (CompOutput *output,
		   CompWindow * w,
		   Box *BB)
{
    ANIMADDON_DISPLAY (w->screen->display);
    ANIMADDON_WINDOW (w);

    int i;
    for (i = 0; i < aw->eng.numPs; i++)
    {
	ParticleSystem * ps = &aw->eng.ps[i];
	if (ps->active)
	{
	    Particle *part = ps->particles;
	    int j;
	    for (j = 0; j < ps->numParticles; j++, part++)
	    {
		if (part->life <= 0.0f)	     // Ignore dead particles
		    continue;

		float w = part->width / 2;
		float h = part->height / 2;

		w += (w * part->w_mod) * part->life;
		h += (h * part->h_mod) * part->life;

		Box particleBox =
		    {part->x - w, part->x + w,
		     part->y - h, part->y + h};

		ad->animBaseFunctions->expandBoxWithBox (BB, &particleBox);
	    }
	}
    }
    if (aw->com->useDrawRegion)
    {
	int nClip = aw->com->drawRegion->numRects;
	Box *pClip = aw->com->drawRegion->rects;

	for (; nClip--; pClip++)
	    ad->animBaseFunctions->expandBoxWithBox (BB, pClip);
    }
    else // drawing full window
	ad->animBaseFunctions->updateBBWindow (output, w, BB);
}

void
particlesCleanup (CompWindow * w)
{
    ANIMADDON_WINDOW (w);

    if (aw->eng.numPs)
    {
	int i = 0;

	for (i = 0; i < aw->eng.numPs; i++)
	    finiParticles (aw->eng.ps + i);
	free (aw->eng.ps);
	aw->eng.ps = NULL;
	aw->eng.numPs = 0;
    }
}

Bool
particlesPrePrepPaintScreen (CompWindow * w, int msSinceLastPaint)
{
    ANIMADDON_WINDOW (w);

    Bool particleAnimInProgress = FALSE;

    if (aw->eng.numPs)
    {
	int i;
	for (i = 0; i < aw->eng.numPs; i++)
	{
	    if (aw->eng.ps[i].active)
	    {
		updateParticles (&aw->eng.ps[i], msSinceLastPaint);
		particleAnimInProgress = TRUE;
	    }
	}
    }

    return particleAnimInProgress;
}

