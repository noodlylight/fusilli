/*
 *
 * Compiz magnifier plugin
 *
 * mag.c
 *
 * Copyright : (C) 2008 by Dennis Kasprzyk
 * E-mail    : onestone@opencompositing.org
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

#include <compiz-core.h>
#include <compiz-mousepoll.h>

#include "mag_options.h"

#define GET_MAG_DISPLAY(d)                                  \
    ((MagDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define MAG_DISPLAY(d)                      \
    MagDisplay *md = GET_MAG_DISPLAY (d)

#define GET_MAG_SCREEN(s, md)                                   \
    ((MagScreen *) (s)->base.privates[(md)->screenPrivateIndex].ptr)

#define MAG_SCREEN(s)                                                      \
    MagScreen *ms = GET_MAG_SCREEN (s, GET_MAG_DISPLAY (s->display))

static int displayPrivateIndex = 0;

typedef struct _MagDisplay
{
    int  screenPrivateIndex;

    MousePollFunc *mpFunc;
}
MagDisplay;

typedef struct _MagImage
{
    CompTexture  tex;
    unsigned int width;
    unsigned int height;

    Bool loaded;
}
MagImage;

typedef struct _MagScreen
{
    int posX;
    int posY;

    Bool adjust;

    GLfloat zVelocity;
    GLfloat zTarget;
    GLfloat zoom;

    MagModeEnum mode;

    GLuint texture;
    GLenum target;

    int width;
    int height;

    MagImage overlay;
    MagImage mask;

    GLuint program;

    PositionPollingHandle pollHandle;
	
    PreparePaintScreenProc preparePaintScreen;
    DonePaintScreenProc    donePaintScreen;
    PaintScreenProc        paintScreen;
}
MagScreen;

static const char *fisheyeFpString =
    "!!ARBfp1.0"

    "PARAM p0 = program.env[0];"
    "PARAM p1 = program.env[1];"
    "PARAM p2 = program.env[2];"

    "TEMP t0, t1, t2, t3;"

    "SUB t1, p0.xyww, fragment.texcoord[0];"
    "DP3 t2, t1, t1;"
    "RSQ t2, t2.x;"
    "SUB t0, t2, p0;"

    "RCP t3, t2.x;"
    "MAD t3, t3, p1.z, p2.z;"
    "COS t3, t3.x;"

    "MUL t3, t3, p1.w;"

    "MUL t1, t2, t1;"
    "MAD t1, t1, t3, fragment.texcoord[0];"

    "CMP t1, t0.z, fragment.texcoord[0], t1;"
		
    "MAD t1, t1, p1, p2;"
    "TEX result.color, t1, texture[0], %s;"

    "END";

static void
magCleanup (CompScreen *s)
{
    MAG_SCREEN (s);

    if (ms->overlay.loaded)
    {
	ms->overlay.loaded = FALSE;
	finiTexture (s, &ms->overlay.tex);
	initTexture (s, &ms->overlay.tex);
    }
    if (ms->mask.loaded)
    {
	ms->mask.loaded = FALSE;
	finiTexture (s, &ms->mask.tex);
	initTexture (s, &ms->mask.tex);
    }

    if (ms->program)
    {
	(*s->deletePrograms) (1, &ms->program);
	ms->program = 0;
    }
}

static Bool
loadFragmentProgram (CompScreen *s)
{
    char  buffer[1024];
    GLint errorPos;

    MAG_SCREEN (s);

    if (!s->fragmentProgram)
	return FALSE;

    if (ms->target == GL_TEXTURE_2D)
	sprintf (buffer, fisheyeFpString, "2D");
    else
	sprintf (buffer, fisheyeFpString, "RECT");

    /* clear errors */
    glGetError ();

    if (!ms->program)
	(*s->genPrograms) (1, &ms->program);

    (*s->bindProgram) (GL_FRAGMENT_PROGRAM_ARB, ms->program);
    (*s->programString) (GL_FRAGMENT_PROGRAM_ARB,
			 GL_PROGRAM_FORMAT_ASCII_ARB,
			 strlen (buffer), buffer);

    glGetIntegerv (GL_PROGRAM_ERROR_POSITION_ARB, &errorPos);
    if (glGetError () != GL_NO_ERROR || errorPos != -1)
    {
	compLogMessage ("mag", CompLogLevelError,
			"failed to fisheye program");

	(*s->deletePrograms) (1, &ms->program);
	ms->program = 0;

	return FALSE;
    }
    (*s->bindProgram) (GL_FRAGMENT_PROGRAM_ARB, 0);

    return TRUE;
}

static Bool
loadImages (CompScreen *s)
{
    MAG_SCREEN (s);

    if (!s->multiTexCoord2f)
	return FALSE;

    ms->overlay.loaded = readImageToTexture (s, &ms->overlay.tex,
					     magGetOverlay (s),
					     &ms->overlay.width,
					     &ms->overlay.height);
    
    if (!ms->overlay.loaded)
    {
	compLogMessage ("mag", CompLogLevelWarn,
			"Could not load magnifier overlay image \"%s\"!",
			magGetOverlay (s));
	return FALSE;
    }

    ms->mask.loaded = readImageToTexture (s, &ms->mask.tex,
					  magGetMask (s),
					  &ms->mask.width,
					  &ms->mask.height);

    if (!ms->mask.loaded)
    {
	compLogMessage ("mag", CompLogLevelWarn,
			"Could not load magnifier mask image \"%s\"!",
			magGetOverlay (s));
	ms->overlay.loaded = FALSE;
	finiTexture (s, &ms->overlay.tex);
	initTexture (s, &ms->overlay.tex);
	return FALSE;
    }

    if (ms->overlay.width != ms->mask.width ||
	ms->overlay.height != ms->mask.height)
    {
	compLogMessage ("mag", CompLogLevelWarn,
			"Image dimensions do not match!");
	ms->overlay.loaded = FALSE;
	finiTexture (s, &ms->overlay.tex);
	initTexture (s, &ms->overlay.tex);
	ms->mask.loaded = FALSE;
	finiTexture (s, &ms->mask.tex);
	initTexture (s, &ms->mask.tex);
	return FALSE;
    }

    return TRUE;
}

static void
magOptionsChanged (CompScreen	   *s,
		   CompOption	   *opt,
		   MagScreenOptions num)
{
    MAG_SCREEN (s);

    magCleanup (s);

    switch (magGetMode (s))
    {
    case ModeImageOverlay:
	if (loadImages (s))
	    ms->mode = ModeImageOverlay;
	else
	    ms->mode = ModeSimple;
	break;
    case ModeFisheye:
	if (loadFragmentProgram (s))
	    ms->mode = ModeFisheye;
	else
	    ms->mode = ModeSimple;
	break;
    default:
	ms->mode = ModeSimple;
    }
    
    if (ms->zoom != 1.0)
	damageScreen (s);
}

static void
damageRegion (CompScreen *s)
{
    REGION r;

    MAG_SCREEN (s);

    r.rects = &r.extents;
    r.numRects = r.size = 1;

    switch (ms->mode)
    {
    case ModeSimple:
        {
	    int  w, h, b;

	    w = magGetBoxWidth (s);
	    h = magGetBoxHeight (s);
	    b = magGetBorder (s);
	    w += 2 * b;
	    h += 2 * b;

	    r.extents.x1 = MAX (0, MIN (ms->posX - (w / 2), s->width - w));
	    r.extents.x2 = r.extents.x1 + w;
	    r.extents.y1 = MAX (0, MIN (ms->posY - (h / 2), s->height - h));
	    r.extents.y2 = r.extents.y1 + h;
	}
	break;
    case ModeImageOverlay:
	r.extents.x1 = ms->posX - magGetXOffset (s);
	r.extents.x2 = r.extents.x1 + ms->overlay.width;
	r.extents.y1 = ms->posY - magGetYOffset (s);
	r.extents.y2 = r.extents.y1 + ms->overlay.height;
	break;
    case ModeFisheye:
        {
	    int radius = magGetRadius (s);

	    r.extents.x1 = MAX (0.0, ms->posX - radius);
	    r.extents.x2 = MIN (s->width, ms->posX + radius);
	    r.extents.y1 = MAX (0.0, ms->posY - radius);
	    r.extents.y2 = MIN (s->height, ms->posY + radius);
	}
	break;
    }

    damageScreenRegion (s, &r);
}

static void
positionUpdate (CompScreen *s,
		int        x,
		int        y)
{
    MAG_SCREEN (s);

    damageRegion (s);

    ms->posX = x;
    ms->posY = y;

    damageRegion (s);
}

static int
adjustZoom (CompScreen *s, float chunk)
{
    float dx, adjust, amount;
    float change;

    MAG_SCREEN(s);

    dx = ms->zTarget - ms->zoom;

    adjust = dx * 0.15f;
    amount = fabs(dx) * 1.5f;
    if (amount < 0.2f)
	amount = 0.2f;
    else if (amount > 2.0f)
	amount = 2.0f;

    ms->zVelocity = (amount * ms->zVelocity + adjust) / (amount + 1.0f);

    if (fabs (dx) < 0.002f && fabs (ms->zVelocity) < 0.004f)
    {
	ms->zVelocity = 0.0f;
	ms->zoom = ms->zTarget;
	return FALSE;
    }

    change = ms->zVelocity * chunk;
    if (!change)
    {
	if (ms->zVelocity)
	    change = (dx > 0) ? 0.01 : -0.01;
    }

    ms->zoom += change;

    return TRUE;
}

static void
magPreparePaintScreen (CompScreen *s,
		       int        time)
{
    MAG_SCREEN (s);
    MAG_DISPLAY (s->display);

    if (ms->adjust)
    {
	int   steps;
	float amount, chunk;

	amount = time * 0.35f * magGetSpeed (s);
	steps  = amount / (0.5f * magGetTimestep (s));

	if (!steps)
	    steps = 1;

	chunk  = amount / (float) steps;

	while (steps--)
	{
	    ms->adjust = adjustZoom (s, chunk);
	    if (ms->adjust)
		break;
	}
    }

    if (ms->zoom != 1.0)
    {
	if (!ms->pollHandle)
	{
	    (*md->mpFunc->getCurrentPosition) (s, &ms->posX, &ms->posY);
	    ms->pollHandle =
		(*md->mpFunc->addPositionPolling) (s, positionUpdate);
	}
	damageRegion (s);
    }

    UNWRAP (ms, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, time);
    WRAP (ms, s, preparePaintScreen, magPreparePaintScreen);
}

static void
magDonePaintScreen (CompScreen *s)
{
    MAG_SCREEN (s);
    MAG_DISPLAY (s->display);

    if (ms->adjust)
	damageRegion (s);

    if (!ms->adjust && ms->zoom == 1.0 && (ms->width || ms->height))
    {
	glEnable (ms->target);

	glBindTexture (ms->target, ms->texture);

	glTexImage2D (ms->target, 0, GL_RGB, 0, 0, 0,
		      GL_RGB, GL_UNSIGNED_BYTE, NULL);

	ms->width = 0;
	ms->height = 0;
	
	glBindTexture (ms->target, 0);

	glDisable (ms->target);
    }

    if (ms->zoom == 1.0 && !ms->adjust && ms->pollHandle)
    {
	(*md->mpFunc->removePositionPolling) (s, ms->pollHandle);
	ms->pollHandle = 0;
    }

    UNWRAP (ms, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP (ms, s, donePaintScreen, magDonePaintScreen);
}

static void
magPaintSimple (CompScreen *s)
{
    float          pw, ph, bw, bh;
    int            x1, x2, y1, y2;
    float          vc[4];
    float          tc[4];
    int            w, h, cw, ch, cx, cy;
    Bool           kScreen;
    unsigned short *color;
    float          tmp;

    MAG_SCREEN (s);

    w = magGetBoxWidth (s);
    h = magGetBoxHeight (s);

    kScreen = magGetKeepScreen (s);

    x1 = ms->posX - (w / 2);
    if (kScreen)
	x1 = MAX (0, MIN (x1, s->width - w));
    x2 = x1 + w;
    y1 = ms->posY - (h / 2);
    if (kScreen)
	y1 = MAX (0, MIN (y1, s->height - h));
    y2 = y1 + h;
 
    cw = ceil ((float)w / (ms->zoom * 2.0)) * 2.0;
    ch = ceil ((float)h / (ms->zoom * 2.0)) * 2.0;
    cw = MIN (w, cw + 2);
    ch = MIN (h, ch + 2);
    cx = (w - cw) / 2;
    cy = (h - ch) / 2;

    cx = MAX (0, MIN (w - cw, cx));
    cy = MAX (0, MIN (h - ch, cy));

    if (x1 != (ms->posX - (w / 2)))
    {
	cx = 0;
	cw = w;
    }
    if (y1 != (ms->posY - (h / 2)))
    {
	cy = 0;
	ch = h;
    }

    glEnable (ms->target);

    glBindTexture (ms->target, ms->texture);

    if (ms->width != w || ms->height != h)
    {
	glCopyTexImage2D(ms->target, 0, GL_RGB, x1, s->height - y2,
			 w, h, 0);
	ms->width = w;
	ms->height = h;
    }
    else
	glCopyTexSubImage2D (ms->target, 0, cx, cy,
			     x1 + cx, s->height - y2 + cy, cw, ch);

    if (ms->target == GL_TEXTURE_2D)
    {
	pw = 1.0 / ms->width;
	ph = 1.0 / ms->height;
    }
    else
    {
	pw = 1.0;
	ph = 1.0;
    }

    glMatrixMode (GL_PROJECTION);
    glPushMatrix ();
    glLoadIdentity ();
    glMatrixMode (GL_MODELVIEW);
    glPushMatrix ();
    glLoadIdentity ();

    vc[0] = ((x1 * 2.0) / s->width) - 1.0;
    vc[1] = ((x2 * 2.0) / s->width) - 1.0;
    vc[2] = ((y1 * -2.0) / s->height) + 1.0;
    vc[3] = ((y2 * -2.0) / s->height) + 1.0;

    tc[0] = 0.0;
    tc[1] = w * pw;
    tc[2] = h * ph;
    tc[3] = 0.0;

    glColor4usv (defaultColor);

    glPushMatrix ();

    glTranslatef ((float)(ms->posX - (s->width / 2)) * 2 / s->width,
		  (float)(ms->posY - (s->height / 2)) * 2 / -s->height, 0.0);

    glScalef (ms->zoom, ms->zoom, 1.0);

    glTranslatef ((float)((s->width / 2) - ms->posX) * 2 / s->width,
		  (float)((s->height / 2) - ms->posY) * 2 / -s->height, 0.0);

    glScissor (x1, s->height - y2, w, h);

    glEnable (GL_SCISSOR_TEST);

    glBegin (GL_QUADS);
    glTexCoord2f (tc[0], tc[2]);
    glVertex2f (vc[0], vc[2]);
    glTexCoord2f (tc[0], tc[3]);
    glVertex2f (vc[0], vc[3]);
    glTexCoord2f (tc[1], tc[3]);
    glVertex2f (vc[1], vc[3]);
    glTexCoord2f (tc[1], tc[2]);
    glVertex2f (vc[1], vc[2]);
    glEnd ();

    glDisable (GL_SCISSOR_TEST);

    glPopMatrix ();

    glBindTexture (ms->target, 0);

    glDisable (ms->target);

    glEnable (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    tmp = MIN (1.0, (ms->zoom - 1) * 3.0);

    bw = bh = magGetBorder (s);

    bw = bw * 2.0 / s->width;
    bh = bh * 2.0 / s->height;

    bw = bh = magGetBorder (s);

    bw *= 2.0 / (float)s->width;
    bh *= 2.0 / (float)s->height;

    color = magGetBoxColor (s);

    glColor4us (color[0], color[1], color[2], color[3] * tmp);

    glBegin (GL_QUADS);
    glVertex2f (vc[0] - bw, vc[2] + bh);
    glVertex2f (vc[0] - bw, vc[2]);
    glVertex2f (vc[1] + bw, vc[2]);
    glVertex2f (vc[1] + bw, vc[2] + bh);
    glVertex2f (vc[0] - bw, vc[3]);
    glVertex2f (vc[0] - bw, vc[3] - bh);
    glVertex2f (vc[1] + bw, vc[3] - bh);
    glVertex2f (vc[1] + bw, vc[3]);
    glVertex2f (vc[0] - bw, vc[2]);
    glVertex2f (vc[0] - bw, vc[3]);
    glVertex2f (vc[0], vc[3]);
    glVertex2f (vc[0], vc[2]);
    glVertex2f (vc[1], vc[2]);
    glVertex2f (vc[1], vc[3]);
    glVertex2f (vc[1] + bw, vc[3]);
    glVertex2f (vc[1] + bw, vc[2]);
    glEnd();

    glColor4usv (defaultColor);
    glDisable (GL_BLEND);
    glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glPopMatrix();
    glMatrixMode (GL_PROJECTION);
    glPopMatrix ();
    glMatrixMode (GL_MODELVIEW);
}

static void
magPaintImage (CompScreen *s)
{
    float          pw, ph;
    int            x1, x2, y1, y2;
    float          vc[4];
    float          tc[4];
    int            w, h, cw, ch, cx, cy;
    float          tmp, xOff, yOff;

    MAG_SCREEN (s);

    w = ms->overlay.width;
    h = ms->overlay.height;

    xOff = MIN (w, magGetXOffset (s));
    yOff = MIN (h, magGetYOffset (s));

    x1 = ms->posX - xOff;
    x2 = x1 + w;
    y1 = ms->posY - yOff;
    y2 = y1 + h;
 
    cw = ceil ((float)w / (ms->zoom * 2.0)) * 2.0;
    ch = ceil ((float)h / (ms->zoom * 2.0)) * 2.0;
    cw = MIN (w, cw + 2);
    ch = MIN (h, ch + 2);
    cx = floor (xOff - (xOff / ms->zoom));
    cy = h - ch - floor (yOff - (yOff / ms->zoom));

    cx = MAX (0, MIN (w - cw, cx));
    cy = MAX (0, MIN (h - ch, cy));

    glPushAttrib (GL_TEXTURE_BIT);
    
    glEnable (ms->target);

    glBindTexture (ms->target, ms->texture);

    if (ms->width != w || ms->height != h)
    {
	glCopyTexImage2D(ms->target, 0, GL_RGB, x1, s->height - y2,
			 w, h, 0);
	ms->width = w;
	ms->height = h;
    }
    else
	glCopyTexSubImage2D (ms->target, 0, cx, cy,
			     x1 + cx, s->height - y2 + cy, cw, ch);

    if (ms->target == GL_TEXTURE_2D)
    {
	pw = 1.0 / ms->width;
	ph = 1.0 / ms->height;
    }
    else
    {
	pw = 1.0;
	ph = 1.0;
    }

    glMatrixMode (GL_PROJECTION);
    glPushMatrix ();
    glLoadIdentity ();
    glMatrixMode (GL_MODELVIEW);
    glPushMatrix ();
    glLoadIdentity ();

    vc[0] = ((x1 * 2.0) / s->width) - 1.0;
    vc[1] = ((x2 * 2.0) / s->width) - 1.0;
    vc[2] = ((y1 * -2.0) / s->height) + 1.0;
    vc[3] = ((y2 * -2.0) / s->height) + 1.0;

    tc[0] = xOff - (xOff / ms->zoom);
    tc[1] = tc[0] + (w / ms->zoom);

    tc[2] = h - (yOff - (yOff / ms->zoom));
    tc[3] = tc[2] - (h / ms->zoom);

    tc[0] *= pw;
    tc[1] *= pw;
    tc[2] *= ph;
    tc[3] *= ph;

    glEnable (GL_BLEND);

    glColor4usv (defaultColor);
    glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    (*s->activeTexture) (GL_TEXTURE1_ARB);
    enableTexture (s, &ms->mask.tex, COMP_TEXTURE_FILTER_FAST);
    glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glBegin (GL_QUADS);
    (*s->multiTexCoord2f) (GL_TEXTURE0_ARB, tc[0], tc[2]);
    (*s->multiTexCoord2f) (GL_TEXTURE1_ARB,
			   COMP_TEX_COORD_X (&ms->mask.tex.matrix, 0),
			   COMP_TEX_COORD_Y (&ms->mask.tex.matrix, 0));
    glVertex2f (vc[0], vc[2]);
    (*s->multiTexCoord2f) (GL_TEXTURE0_ARB, tc[0], tc[3]);
    (*s->multiTexCoord2f) (GL_TEXTURE1_ARB,
			   COMP_TEX_COORD_X (&ms->mask.tex.matrix, 0),
			   COMP_TEX_COORD_Y (&ms->mask.tex.matrix, h));
    glVertex2f (vc[0], vc[3]);
    (*s->multiTexCoord2f) (GL_TEXTURE0_ARB, tc[1], tc[3]);
    (*s->multiTexCoord2f) (GL_TEXTURE1_ARB,
			   COMP_TEX_COORD_X (&ms->mask.tex.matrix, w),
			   COMP_TEX_COORD_Y (&ms->mask.tex.matrix, h));
    glVertex2f (vc[1], vc[3]);
    (*s->multiTexCoord2f) (GL_TEXTURE0_ARB, tc[1], tc[2]);
    (*s->multiTexCoord2f) (GL_TEXTURE1_ARB,
			   COMP_TEX_COORD_X (&ms->mask.tex.matrix, w),
			   COMP_TEX_COORD_Y (&ms->mask.tex.matrix, 0));
    glVertex2f (vc[1], vc[2]);
    glEnd ();

    disableTexture (s, &ms->mask.tex);
    (*s->activeTexture) (GL_TEXTURE0_ARB);

    glBindTexture (ms->target, 0);

    glDisable (ms->target);

    tmp = MIN (1.0, (ms->zoom - 1) * 3.0);

    glColor4f (tmp, tmp, tmp, tmp);

    enableTexture (s, &ms->overlay.tex, COMP_TEXTURE_FILTER_FAST);
    glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glBegin (GL_QUADS);
    glTexCoord2f (COMP_TEX_COORD_X (&ms->overlay.tex.matrix, 0),
		  COMP_TEX_COORD_Y (&ms->overlay.tex.matrix, 0));
    glVertex2f (vc[0], vc[2]);
    glTexCoord2f (COMP_TEX_COORD_X (&ms->overlay.tex.matrix, 0),
		  COMP_TEX_COORD_Y (&ms->overlay.tex.matrix, h));
    glVertex2f (vc[0], vc[3]);
    glTexCoord2f (COMP_TEX_COORD_X (&ms->overlay.tex.matrix, w),
		  COMP_TEX_COORD_Y (&ms->overlay.tex.matrix, h));
    glVertex2f (vc[1], vc[3]);
    glTexCoord2f (COMP_TEX_COORD_X (&ms->overlay.tex.matrix, w),
		  COMP_TEX_COORD_Y (&ms->overlay.tex.matrix, 0));
    glVertex2f (vc[1], vc[2]);
    glEnd ();

    disableTexture (s, &ms->overlay.tex);

    glColor4usv (defaultColor);
    glDisable (GL_BLEND);
    glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glPopMatrix();
    glMatrixMode (GL_PROJECTION);
    glPopMatrix ();
    glMatrixMode (GL_MODELVIEW);

    glPopAttrib ();

}

static void
magPaintFisheye (CompScreen   *s)
{
    float      pw, ph;
    float      radius, zoom, base;
    int        x1, x2, y1, y2;
    float      vc[4];
    int        size;

    MAG_SCREEN (s);

    radius = magGetRadius (s);
    base   = 0.5 + (0.0015 * radius);
    zoom   = (ms->zoom * base) + 1.0 - base;

    size = radius + 1;

    x1 = MAX (0.0, ms->posX - size);
    x2 = MIN (s->width, ms->posX + size);
    y1 = MAX (0.0, ms->posY - size);
    y2 = MIN (s->height, ms->posY + size);
 
    glEnable (ms->target);

    glBindTexture (ms->target, ms->texture);

    if (ms->width != 2 * size || ms->height != 2 * size)
    {
	glCopyTexImage2D(ms->target, 0, GL_RGB, x1, s->height - y2,
			 size * 2, size * 2, 0);
	ms->width = ms->height = 2 * size;
    }
    else
	glCopyTexSubImage2D (ms->target, 0, 0, 0,
			     x1, s->height - y2, x2 - x1, y2 - y1);

    if (ms->target == GL_TEXTURE_2D)
    {
	pw = 1.0 / ms->width;
	ph = 1.0 / ms->height;
    }
    else
    {
	pw = 1.0;
	ph = 1.0;
    }
    
    glMatrixMode (GL_PROJECTION);
    glPushMatrix ();
    glLoadIdentity ();
    glMatrixMode (GL_MODELVIEW);
    glPushMatrix ();
    glLoadIdentity ();

    glColor4usv (defaultColor);

    glEnable (GL_FRAGMENT_PROGRAM_ARB);
    (*s->bindProgram) (GL_FRAGMENT_PROGRAM_ARB, ms->program);

    (*s->programEnvParameter4f) (GL_FRAGMENT_PROGRAM_ARB, 0,
				 ms->posX, s->height - ms->posY,
				 1.0 / radius, 0.0f);
    (*s->programEnvParameter4f) (GL_FRAGMENT_PROGRAM_ARB, 1,
				 pw, ph, M_PI / radius,
				 (zoom - 1.0) * zoom);
    (*s->programEnvParameter4f) (GL_FRAGMENT_PROGRAM_ARB, 2,
				 -x1 * pw, -(s->height - y2) * ph,
				 -M_PI / 2.0, 0.0);

    x1 = MAX (0.0, ms->posX - radius);
    x2 = MIN (s->width, ms->posX + radius);
    y1 = MAX (0.0, ms->posY - radius);
    y2 = MIN (s->height, ms->posY + radius);

    vc[0] = ((x1 * 2.0) / s->width) - 1.0;
    vc[1] = ((x2 * 2.0) / s->width) - 1.0;
    vc[2] = ((y1 * -2.0) / s->height) + 1.0;
    vc[3] = ((y2 * -2.0) / s->height) + 1.0;

    y1 = s->height - y1;
    y2 = s->height - y2;

    glBegin (GL_QUADS);
    glTexCoord2f (x1, y1);
    glVertex2f (vc[0], vc[2]);
    glTexCoord2f (x1, y2);
    glVertex2f (vc[0], vc[3]);
    glTexCoord2f (x2, y2);
    glVertex2f (vc[1], vc[3]);
    glTexCoord2f (x2, y1);
    glVertex2f (vc[1], vc[2]);
    glEnd ();

    glDisable (GL_FRAGMENT_PROGRAM_ARB);

    glColor4usv (defaultColor);
    
    glPopMatrix();
    glMatrixMode (GL_PROJECTION);
    glPopMatrix ();
    glMatrixMode (GL_MODELVIEW);

    glBindTexture (ms->target, 0);

    glDisable (ms->target);
}

static void
magPaintScreen (CompScreen   *s,
		CompOutput   *outputs,
		int          numOutput,
		unsigned int mask)
{
    XRectangle     r;

    MAG_SCREEN (s);

    UNWRAP (ms, s, paintScreen);
    (*s->paintScreen) (s, outputs, numOutput, mask);
    WRAP (ms, s, paintScreen, magPaintScreen);

    if (ms->zoom == 1.0)
	return; 

    r.x      = 0;
    r.y      = 0;
    r.width  = s->width;
    r.height = s->height;

    if (s->lastViewport.x      != r.x     ||
	s->lastViewport.y      != r.y     ||
	s->lastViewport.width  != r.width ||
	s->lastViewport.height != r.height)
    {
	glViewport (r.x, r.y, r.width, r.height);
	s->lastViewport = r;
    }

    switch (ms->mode)
    {
    case ModeImageOverlay:
	magPaintImage (s);
	break;
    case ModeFisheye:
	magPaintFisheye (s);
	break;
    default:
	magPaintSimple (s);
    }

}

static Bool
magTerminate (CompDisplay     *d,
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
    {
	MAG_SCREEN (s);

	ms->zTarget = 1.0;
	ms->adjust  = TRUE;
	damageScreen (s);

	return TRUE;
    }
    return FALSE;
}

static Bool
magInitiate (CompDisplay     *d,
	     CompAction      *action,
	     CompActionState state,
	     CompOption      *option,
	     int             nOption)
{
    CompScreen *s;
    Window     xid;
    float      factor;

    xid    = getIntOptionNamed (option, nOption, "root", 0);
    factor = getFloatOptionNamed (option, nOption, "factor", 0.0);

    s = findScreenAtDisplay (d, xid);
    if (s)
    {
	MAG_SCREEN (s);

	if (factor == 0.0 && ms->zTarget != 1.0)
	    return magTerminate (d, action, state, option, nOption);

	if (ms->mode == ModeFisheye)
	{
	    if (factor != 1.0)
		factor = magGetZoomFactor (s) * 3;

	    ms->zTarget = MAX (1.0, MIN (10.0, factor));
	}
	else
	{
	    if (factor != 1.0)
		factor = magGetZoomFactor (s);

	    ms->zTarget = MAX (1.0, MIN (64.0, factor));
	}
	ms->adjust  = TRUE;
	damageScreen (s);

	return TRUE;
    }
    return FALSE;
}

static Bool
magZoomIn (CompDisplay     *d,
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
    {
	MAG_SCREEN (s);

	if (ms->mode == ModeFisheye)
	    ms->zTarget = MIN (10.0, ms->zTarget + 1.0);
	else
	    ms->zTarget = MIN (64.0, ms->zTarget * 1.2);
	ms->adjust  = TRUE;
	damageScreen (s);

	return TRUE;
    }
    return FALSE;
}

static Bool
magZoomOut (CompDisplay     *d,
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
    {
	MAG_SCREEN (s);

	if (ms->mode == ModeFisheye)
	    ms->zTarget = MAX (1.0, ms->zTarget - 1.0);
	else
	    ms->zTarget = MAX (1.0, ms->zTarget / 1.2);
	ms->adjust  = TRUE;
	damageScreen (s);

	return TRUE;
    }
    return FALSE;
}


static Bool
magInitScreen (CompPlugin *p,
	       CompScreen *s)
{
    MagScreen *ms;

    MAG_DISPLAY (s->display);

    ms = calloc (1, sizeof (MagScreen));
    if (!ms)
	return FALSE;

    s->base.privates[md->screenPrivateIndex].ptr = ms;

    WRAP (ms, s, paintScreen, magPaintScreen);
    WRAP (ms, s, preparePaintScreen, magPreparePaintScreen);
    WRAP (ms, s, donePaintScreen, magDonePaintScreen);

    ms->zoom = 1.0;
    ms->zVelocity = 0.0;
    ms->zTarget = 1.0;

    ms->pollHandle = 0;

    glGenTextures (1, &ms->texture);

    if (s->textureNonPowerOfTwo)
	ms->target = GL_TEXTURE_2D;
    else
	ms->target = GL_TEXTURE_RECTANGLE_ARB;

    glEnable (ms->target);

    /* Bind the texture */
    glBindTexture (ms->target, ms->texture);

    /* Load the parameters */
    glTexParameteri (ms->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (ms->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (ms->target, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri (ms->target, GL_TEXTURE_WRAP_T, GL_CLAMP);

    glTexImage2D (ms->target, 0, GL_RGB, 0, 0, 0,
		  GL_RGB, GL_UNSIGNED_BYTE, NULL);

    ms->width = 0;
    ms->height = 0;

    glBindTexture (ms->target, 0);

    glDisable (ms->target);

    initTexture (s, &ms->overlay.tex);
    initTexture (s, &ms->mask.tex);
    ms->overlay.loaded = FALSE;
    ms->mask.loaded    = FALSE;

    ms->program = 0;

    magSetOverlayNotify (s, magOptionsChanged);
    magSetMaskNotify (s, magOptionsChanged);
    magSetModeNotify (s, magOptionsChanged);

    switch (magGetMode (s))
    {
    case ModeImageOverlay:
	if (loadImages (s))
	    ms->mode = ModeImageOverlay;
	else
	    ms->mode = ModeSimple;
	break;
    case ModeFisheye:
	if (loadFragmentProgram (s))
	    ms->mode = ModeFisheye;
	else
	    ms->mode = ModeSimple;
	break;
    default:
	ms->mode = ModeSimple;
    }

    if (!s->fragmentProgram)
	compLogMessage ("mag", CompLogLevelWarn,
			"GL_ARB_fragment_program not supported. "
			"Fisheye mode will not work.");

    return TRUE;
}


static void
magFiniScreen (CompPlugin *p,
	       CompScreen *s)
{
    MAG_SCREEN (s);
    MAG_DISPLAY (s->display);

    /* Restore the original function */
    UNWRAP (ms, s, paintScreen);
    UNWRAP (ms, s, preparePaintScreen);
    UNWRAP (ms, s, donePaintScreen);

    if (ms->pollHandle)
	(*md->mpFunc->removePositionPolling) (s, ms->pollHandle);

    if (ms->zoom)
	damageScreen (s);

    glDeleteTextures (1, &ms->target);

    magCleanup (s);

    /* Free the pointer */
    free (ms);
}

static Bool
magInitDisplay (CompPlugin  *p,
	        CompDisplay *d)
{
    /* Generate a mag display */
    MagDisplay *md;
    int        index;

    if (!checkPluginABI ("core", CORE_ABIVERSION) ||
        !checkPluginABI ("mousepoll", MOUSEPOLL_ABIVERSION))
	return FALSE;

    if (!getPluginDisplayIndex (d, "mousepoll", &index))
	return FALSE;

    md = malloc (sizeof (MagDisplay));
    if (!md)
	return FALSE;
 
    /* Allocate a private index */
    md->screenPrivateIndex = allocateScreenPrivateIndex (d);

    /* Check if its valid */
    if (md->screenPrivateIndex < 0)
    {
	/* It's invalid so free memory and return */
	free (md);
	return FALSE;
    }

    md->mpFunc = d->base.privates[index].ptr;

    magSetInitiateInitiate (d, magInitiate);
    magSetInitiateTerminate (d, magTerminate);

    magSetZoomInButtonInitiate (d, magZoomIn);
    magSetZoomOutButtonInitiate (d, magZoomOut);

    /* Record the display */
    d->base.privates[displayPrivateIndex].ptr = md;
    return TRUE;
}

static void
magFiniDisplay (CompPlugin  *p,
	        CompDisplay *d)
{
    MAG_DISPLAY (d);

    /* Free the private index */
    freeScreenPrivateIndex (d, md->screenPrivateIndex);
    /* Free the pointer */
    free (md);
}

static Bool
magInit (CompPlugin * p)
{
    displayPrivateIndex = allocateDisplayPrivateIndex ();

    if (displayPrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
magFini (CompPlugin * p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
}

static CompBool
magInitObject (CompPlugin *p,
	       CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) magInitDisplay,
	(InitPluginObjectProc) magInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
magFiniObject (CompPlugin *p,
	       CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) magFiniDisplay,
	(FiniPluginObjectProc) magFiniScreen
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

CompPluginVTable magVTable = {
    "mag",
    0,
    magInit,
    magFini,
    magInitObject,
    magFiniObject,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo (void)
{
    return &magVTable;
}
