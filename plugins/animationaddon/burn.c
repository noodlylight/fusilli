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
#include "animation_tex.h"

// =====================  Effect: Burn  =========================

Bool
fxBurnInit (CompWindow * w)
{
    ANIMADDON_DISPLAY (w->screen->display);
    ANIMADDON_WINDOW (w);

    if (!aw->eng.numPs)
    {
	aw->eng.ps = calloc(2, sizeof(ParticleSystem));
	if (!aw->eng.ps)
	{
	    ad->animBaseFunctions->postAnimationCleanup (w);
	    return FALSE;
	}

	aw->eng.numPs = 2;
    }
    initParticles (animGetI (w, ANIMADDON_SCREEN_OPTION_FIRE_PARTICLES)/
		   10, &aw->eng.ps[0]);
    initParticles (animGetI (w, ANIMADDON_SCREEN_OPTION_FIRE_PARTICLES),
		   &aw->eng.ps[1]);
    aw->eng.ps[1].slowdown = animGetF (w, ANIMADDON_SCREEN_OPTION_FIRE_SLOWDOWN);
    aw->eng.ps[1].darken = 0.5;
    aw->eng.ps[1].blendMode = GL_ONE;

    aw->eng.ps[0].slowdown =
	animGetF (w, ANIMADDON_SCREEN_OPTION_FIRE_SLOWDOWN) / 2.0;
    aw->eng.ps[0].darken = 0.0;
    aw->eng.ps[0].blendMode = GL_ONE_MINUS_SRC_ALPHA;

    if (!aw->eng.ps[0].tex)
	glGenTextures(1, &aw->eng.ps[0].tex);
    glBindTexture(GL_TEXTURE_2D, aw->eng.ps[0].tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
		 GL_RGBA, GL_UNSIGNED_BYTE, fireTex);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!aw->eng.ps[1].tex)
	glGenTextures(1, &aw->eng.ps[1].tex);
    glBindTexture(GL_TEXTURE_2D, aw->eng.ps[1].tex);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0,
		 GL_RGBA, GL_UNSIGNED_BYTE, fireTex);
    glBindTexture(GL_TEXTURE_2D, 0);

    aw->animFireDirection = ad->animBaseFunctions->getActualAnimDirection
	(w, animGetI (w, ANIMADDON_SCREEN_OPTION_FIRE_DIRECTION), FALSE);

    if (animGetB (w, ANIMADDON_SCREEN_OPTION_FIRE_CONSTANT_SPEED))
    {
	aw->com->animTotalTime *= WIN_H(w) / 500.0;
	aw->com->animRemainingTime *= WIN_H(w) / 500.0;
    }

    return TRUE;
}

static void
fxBurnGenNewFire(CompWindow * w,
		 ParticleSystem * ps,
		 int x,
		 int y,
		 int width,
		 int height,
		 float size,
		 float time)
{
    Bool mysticalFire = animGetB (w, ANIMADDON_SCREEN_OPTION_FIRE_MYSTICAL);
    float fireLife = animGetF (w, ANIMADDON_SCREEN_OPTION_FIRE_LIFE);
    float fireLifeNeg = 1 - fireLife;
    float fadeExtra = 0.2f * (1.01 - fireLife);
    float max_new = ps->numParticles * (time / 50) * (1.05 - fireLife);

    // set color ABAB ANIMADDON_SCREEN_OPTION_FIRE_COLOR
    unsigned short *c =
	animGetC (w, ANIMADDON_SCREEN_OPTION_FIRE_COLOR);
    float colr1 = (float)c[0] / 0xffff;
    float colg1 = (float)c[1] / 0xffff;
    float colb1 = (float)c[2] / 0xffff;
    float colr2 = 1 / 1.7 * (float)c[0] / 0xffff;
    float colg2 = 1 / 1.7 * (float)c[1] / 0xffff;
    float colb2 = 1 / 1.7 * (float)c[2] / 0xffff;
    float cola = (float)c[3] / 0xffff;
    float rVal;

    float partw = animGetF (w, ANIMADDON_SCREEN_OPTION_FIRE_SIZE);
    float parth = partw * 1.5;

    // Limit max number of new particles created simultaneously
    if (max_new > ps->numParticles / 5)
	max_new = ps->numParticles / 5;

    Particle *part = ps->particles;
    int i;
    for (i = 0; i < ps->numParticles && max_new > 0; i++, part++)
    {
	if (part->life <= 0.0f)
	{
	    // give gt new life
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->life = 1.0f;
	    part->fade = rVal * fireLifeNeg + fadeExtra; // Random Fade Value

	    // set size
	    part->width = partw;
	    part->height = parth;
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->w_mod = part->h_mod = size * rVal;

	    // choose random position
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->x = x + ((width > 1) ? (rVal * width) : 0);
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->y = y + ((height > 1) ? (rVal * height) : 0);
	    part->z = 0.0;
	    part->xo = part->x;
	    part->yo = part->y;
	    part->zo = part->z;

	    // set speed and direction
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->xi = ((rVal * 20.0) - 10.0f);
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->yi = ((rVal * 20.0) - 15.0f);
	    part->zi = 0.0f;

	    if (mysticalFire)
	    {
		// Random colors! (aka Mystical Fire)
		rVal = (float)(random() & 0xff) / 255.0;
		part->r = rVal;
		rVal = (float)(random() & 0xff) / 255.0;
		part->g = rVal;
		rVal = (float)(random() & 0xff) / 255.0;
		part->b = rVal;
	    }
	    else
	    {
		rVal = (float)(random() & 0xff) / 255.0;
		part->r = colr1 - rVal * colr2;
		part->g = colg1 - rVal * colg2;
		part->b = colb1 - rVal * colb2;
	    }
	    // set transparancy
	    part->a = cola;

	    // set gravity
	    part->xg = (part->x < part->xo) ? 1.0 : -1.0;
	    part->yg = -3.0f;
	    part->zg = 0.0f;

	    ps->active = TRUE;
	    max_new -= 1;
	}
	else
	{
	    part->xg = (part->x < part->xo) ? 1.0 : -1.0;
	}
    }

}

static void
fxBurnGenNewSmoke(CompWindow * w,
		  ParticleSystem * ps,
		  int x,
		  int y,
		  int width,
		  int height,
		  float size,
		  float time)
{
    float max_new =
	ps->numParticles * (time / 50) *
	(1.05 - animGetF (w, ANIMADDON_SCREEN_OPTION_FIRE_LIFE));
    float rVal;

    float fireLife = animGetF (w, ANIMADDON_SCREEN_OPTION_FIRE_LIFE);
    float fireLifeNeg = 1 - fireLife;
    float fadeExtra = 0.2f * (1.01 - fireLife);

    float partSize = animGetF (w, ANIMADDON_SCREEN_OPTION_FIRE_SIZE) * size * 5;
    float sizeNeg = -size;

    // Limit max number of new particles created simultaneously
    if (max_new > ps->numParticles)
	max_new = ps->numParticles;

    Particle *part = ps->particles;
    int i;
    for (i = 0; i < ps->numParticles && max_new > 0; i++, part++)
    {
	if (part->life <= 0.0f)
	{
	    // give gt new life
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->life = 1.0f;
	    part->fade = rVal * fireLifeNeg + fadeExtra; // Random Fade Value

	    // set size
	    part->width = partSize;
	    part->height = partSize;
	    part->w_mod = -0.8;
	    part->h_mod = -0.8;

	    // choose random position
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->x = x + ((width > 1) ? (rVal * width) : 0);
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->y = y + ((height > 1) ? (rVal * height) : 0);
	    part->z = 0.0;
	    part->xo = part->x;
	    part->yo = part->y;
	    part->zo = part->z;

	    // set speed and direction
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->xi = ((rVal * 20.0) - 10.0f);
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->yi = (rVal + 0.2) * -size;
	    part->zi = 0.0f;

	    // set color
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->r = rVal / 4.0;
	    part->g = rVal / 4.0;
	    part->b = rVal / 4.0;
	    rVal = (float)(random() & 0xff) / 255.0;
	    part->a = 0.5 + (rVal / 2.0);

	    // set gravity
	    part->xg = (part->x < part->xo) ? size : sizeNeg;
	    part->yg = sizeNeg;
	    part->zg = 0.0f;

	    ps->active = TRUE;
	    max_new -= 1;
	}
	else
	{
	    part->xg = (part->x < part->xo) ? size : sizeNeg;
	}
    }

}

void
fxBurnAnimStep (CompWindow *w, float time)
{
    CompScreen *s = w->screen;

    ANIMADDON_WINDOW (w);

    Bool smoke = animGetB (w, ANIMADDON_SCREEN_OPTION_FIRE_SMOKE);

    float timestep = (s->slowAnimations ? 2 :	// For smooth slow-mo (refer to display.c)
		      getIntenseTimeStep (s));
    float old = 1 - (aw->com->animRemainingTime) / (aw->com->animTotalTime - timestep);
    float stepSize;

    aw->com->animRemainingTime -= timestep;
    if (aw->com->animRemainingTime <= 0)
	aw->com->animRemainingTime = 0;	// avoid sub-zero values
    float new = 1 - (aw->com->animRemainingTime) / (aw->com->animTotalTime - timestep);

    stepSize = new - old;

    if (aw->com->curWindowEvent == WindowEventOpen ||
	aw->com->curWindowEvent == WindowEventUnminimize ||
	aw->com->curWindowEvent == WindowEventUnshade)
    {
	new = 1 - new;
    }
    if (!aw->com->drawRegion)
	aw->com->drawRegion = XCreateRegion();
    if (aw->com->animRemainingTime > 0)
    {
	XRectangle rect;

	switch (aw->animFireDirection)
	{
	case AnimDirectionUp:
	    rect.x = 0;
	    rect.y = 0;
	    rect.width = WIN_W(w);
	    rect.height = WIN_H(w) - (new * WIN_H(w));
	    break;
	case AnimDirectionRight:
	    rect.x = (new * WIN_W(w));
	    rect.y = 0;
	    rect.width = WIN_W(w) - (new * WIN_W(w));
	    rect.height = WIN_H(w);
	    break;
	case AnimDirectionLeft:
	    rect.x = 0;
	    rect.y = 0;
	    rect.width = WIN_W(w) - (new * WIN_W(w));
	    rect.height = WIN_H(w);
	    break;
	case AnimDirectionDown:
	default:
	    rect.x = 0;
	    rect.y = (new * WIN_H(w));
	    rect.width = WIN_W(w);
	    rect.height = WIN_H(w) - (new * WIN_H(w));
	    break;
	}
	rect.x += WIN_X(w);
	rect.y += WIN_Y(w);
	XUnionRectWithRegion (&rect, &emptyRegion, aw->com->drawRegion);
    }
    else
    {
	XUnionRegion (&emptyRegion, &emptyRegion, aw->com->drawRegion);
    }
    aw->com->useDrawRegion = (fabs (new) > 1e-5);

    if (aw->com->animRemainingTime > 0 && aw->eng.numPs)
    {
	switch (aw->animFireDirection)
	{
	case AnimDirectionUp:
	    if (smoke)
		fxBurnGenNewSmoke(w, &aw->eng.ps[0], WIN_X(w),
				  WIN_Y(w) + ((1 - new) * WIN_H(w)),
				  WIN_W(w), 1, WIN_W(w) / 40.0, time);
	    fxBurnGenNewFire(w, &aw->eng.ps[1], WIN_X(w),
			     WIN_Y(w) + ((1 - new) * WIN_H(w)),
			     WIN_W(w), (stepSize) * WIN_H(w),
			     WIN_W(w) / 40.0, time);
	    break;
	case AnimDirectionLeft:
	    if (smoke)
		fxBurnGenNewSmoke(w, &aw->eng.ps[0],
				  WIN_X(w) + ((1 - new) * WIN_W(w)),
				  WIN_Y(w),
				  (stepSize) * WIN_W(w),
				  WIN_H(w), WIN_H(w) / 40.0, time);
	    fxBurnGenNewFire(w, &aw->eng.ps[1],
			     WIN_X(w) + ((1 - new) * WIN_W(w)),
			     WIN_Y(w), (stepSize) * WIN_W(w),
			     WIN_H(w), WIN_H(w) / 40.0, time);
	    break;
	case AnimDirectionRight:
	    if (smoke)
		fxBurnGenNewSmoke(w, &aw->eng.ps[0],
				  WIN_X(w) + (new * WIN_W(w)),
				  WIN_Y(w),
				  (stepSize) * WIN_W(w),
				  WIN_H(w), WIN_H(w) / 40.0, time);
	    fxBurnGenNewFire(w, &aw->eng.ps[1],
			     WIN_X(w) + (new * WIN_W(w)),
			     WIN_Y(w), (stepSize) * WIN_W(w),
			     WIN_H(w), WIN_H(w) / 40.0, time);
	    break;
	case AnimDirectionDown:
	default:
	    if (smoke)
		fxBurnGenNewSmoke(w, &aw->eng.ps[0], WIN_X(w),
				  WIN_Y(w) + (new * WIN_H(w)),
				  WIN_W(w), 1, WIN_W(w) / 40.0, time);
	    fxBurnGenNewFire(w, &aw->eng.ps[1], WIN_X(w),
			     WIN_Y(w) + (new * WIN_H(w)),
			     WIN_W(w), (stepSize) * WIN_H(w),
			     WIN_W(w) / 40.0, time);
	    break;
	}

    }
    if (aw->com->animRemainingTime <= 0 && aw->eng.numPs
	&& (aw->eng.ps[0].active || aw->eng.ps[1].active))
	aw->com->animRemainingTime = timestep;

    if (!aw->eng.numPs || !aw->eng.ps)
    {
	if (aw->eng.ps)
	{
	    finiParticles(aw->eng.ps);
	    free(aw->eng.ps);
	    aw->eng.ps = NULL;
	}
	// Abort animation
	aw->com->animRemainingTime = 0;
	return;
    }

    int i;
    int nParticles;
    Particle *part;

    if (aw->com->animRemainingTime > 0 && smoke)
    {
	float partxg = WIN_W(w) / 40.0;
	float partxgNeg = -partxg;

	nParticles = aw->eng.ps[0].numParticles;
	part = aw->eng.ps[0].particles;

	for (i = 0; i < nParticles; i++, part++)
	    part->xg = (part->x < part->xo) ? partxg : partxgNeg;
    }
    aw->eng.ps[0].x = WIN_X(w);
    aw->eng.ps[0].y = WIN_Y(w);

    if (aw->com->animRemainingTime > 0)
    {
	nParticles = aw->eng.ps[1].numParticles;
	part = aw->eng.ps[1].particles;

	for (i = 0; i < nParticles; i++, part++)
	    part->xg = (part->x < part->xo) ? 1.0 : -1.0;
    }
    aw->eng.ps[1].x = WIN_X(w);
    aw->eng.ps[1].y = WIN_Y(w);
}

