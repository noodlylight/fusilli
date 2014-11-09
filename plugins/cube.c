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
 *         Mirco Müller <macslow@bangang.de> (Skydome support)
 */

#include <string.h>
#include <math.h>

#include <X11/Xatom.h>
#include <X11/Xproto.h>

#include <fusilli-cube.h>

static int bananaIndex;

static int cubeCorePrivateIndex;
int cubeDisplayPrivateIndex;

static CompKeyBinding unfold_key;

static void
cubeLoadImg (CompScreen *s)
{
	unsigned int    width, height;
	int             pw, ph;

	CUBE_SCREEN (s);

	if (!cs->fullscreenOutput)
	{
		pw = s->width;
		ph = s->height;
	}
	else
	{
		pw = s->outputDev[0].width;
		ph = s->outputDev[0].height;
	}

	//if (!imgNFile || cs->pw != pw || cs->ph != ph)
	//{
	//	finiTexture (s, &cs->texture);
	//	initTexture (s, &cs->texture);

	//	if (!imgNFile)
	//		return;
	//}

	const BananaValue *
	option_cubecap_image = bananaGetOption (bananaIndex,
	                                        "cubecap_image",
	                                        s->screenNum);

	if (strlen(option_cubecap_image->s) > 0)
	{
		if (!readImageToTexture (s, &cs->texture,
		                        option_cubecap_image->s,
		                        &width, &height))
		{
			compLogMessage ("cube", CompLogLevelWarn,
			                "Failed to load cubecap: %s",
			                option_cubecap_image->s);

			finiTexture (s, &cs->texture);
			initTexture (s, &cs->texture);

			return;
		}
	}

	cs->tc[0] = COMP_TEX_COORD_X (&cs->texture.matrix, width / 2.0f);
	cs->tc[1] = COMP_TEX_COORD_Y (&cs->texture.matrix, height / 2.0f);

	const BananaValue *
	option_scale_image = bananaGetOption (bananaIndex,
	                                      "scale_image",
	                                      s->screenNum);

	if (option_scale_image->b)
	{
		cs->tc[2] = COMP_TEX_COORD_X (&cs->texture.matrix, width);
		cs->tc[3] = COMP_TEX_COORD_Y (&cs->texture.matrix, 0.0f);

		cs->tc[4] = COMP_TEX_COORD_X (&cs->texture.matrix, 0.0f);
		cs->tc[5] = COMP_TEX_COORD_Y (&cs->texture.matrix, 0.0f);

		cs->tc[6] = COMP_TEX_COORD_X (&cs->texture.matrix, 0.0f);
		cs->tc[7] = COMP_TEX_COORD_Y (&cs->texture.matrix, height);

		cs->tc[8] = COMP_TEX_COORD_X (&cs->texture.matrix, width);
		cs->tc[9] = COMP_TEX_COORD_Y (&cs->texture.matrix, height);

		cs->tc[10] = COMP_TEX_COORD_X (&cs->texture.matrix, width);
		cs->tc[11] = COMP_TEX_COORD_Y (&cs->texture.matrix, 0.0f);
	}
	else
	{
		float x1 = width  / 2.0f - pw / 2.0f;
		float y1 = height / 2.0f - ph / 2.0f;
		float x2 = width  / 2.0f + pw / 2.0f;
		float y2 = height / 2.0f + ph / 2.0f;

		cs->tc[2] = COMP_TEX_COORD_X (&cs->texture.matrix, x2);
		cs->tc[3] = COMP_TEX_COORD_Y (&cs->texture.matrix, y1);

		cs->tc[4] = COMP_TEX_COORD_X (&cs->texture.matrix, x1);
		cs->tc[5] = COMP_TEX_COORD_Y (&cs->texture.matrix, y1);

		cs->tc[6] = COMP_TEX_COORD_X (&cs->texture.matrix, x1);
		cs->tc[7] = COMP_TEX_COORD_Y (&cs->texture.matrix, y2);

		cs->tc[8] = COMP_TEX_COORD_X (&cs->texture.matrix, x2);
		cs->tc[9] = COMP_TEX_COORD_Y (&cs->texture.matrix, y2);

		cs->tc[10] = COMP_TEX_COORD_X (&cs->texture.matrix, x2);
		cs->tc[11] = COMP_TEX_COORD_Y (&cs->texture.matrix, y1);
	}
}

static Bool
cubeUpdateGeometry (CompScreen *s,
                    int        sides,
                    Bool       invert)
{
	GLfloat radius, distance;
	GLfloat *v;
	int     i, n;

	CUBE_SCREEN (s);

	sides *= cs->nOutput;

	distance = 0.5f / tanf (M_PI / sides);
	radius   = 0.5f / sinf (M_PI / sides);

	n = (sides + 2) * 2;

	if (cs->nVertices != n)
	{
		v = realloc (cs->vertices, sizeof (GLfloat) * n * 3);
		if (!v)
			return FALSE;

		cs->nVertices = n;
		cs->vertices  = v;
	}
	else
		v = cs->vertices;

	*v++ = 0.0f;
	*v++ = 0.5 * invert;
	*v++ = 0.0f;

	for (i = 0; i <= sides; i++)
	{
		*v++ = radius * sinf (i * 2 * M_PI / sides + M_PI / sides);
		*v++ = 0.5 * invert;
		*v++ = radius * cosf (i * 2 * M_PI / sides + M_PI / sides);
	}

	*v++ = 0.0f;
	*v++ = -0.5 * invert;
	*v++ = 0.0f;

	for (i = sides; i >= 0; i--)
	{
		*v++ = radius * sinf (i * 2 * M_PI / sides + M_PI / sides);
		*v++ = -0.5 * invert;
		*v++ = radius * cosf (i * 2 * M_PI / sides + M_PI / sides);
	}

	cs->invert	 = invert;
	cs->distance = distance;

	return TRUE;
}

static void
cubeUpdateOutputs (CompScreen *s)
{
	BoxPtr pBox0, pBox1;
	int    i, j, k, x;

	CUBE_SCREEN (s);

	k = 0;

	cs->fullscreenOutput = TRUE;

	for (i = 0; i < s->nOutputDev; i++)
	{
		cs->outputMask[i] = -1;

		/* dimensions must match first output */
		if (s->outputDev[i].width  != s->outputDev[0].width ||
		    s->outputDev[i].height != s->outputDev[0].height)
			continue;

		pBox0 = &s->outputDev[0].region.extents;
		pBox1 = &s->outputDev[i].region.extents;

		/* top and bottom line must match first output */
		if (pBox0->y1 != pBox1->y1 || pBox0->y2 != pBox1->y2)
			continue;

		k++;

		for (j = 0; j < s->nOutputDev; j++)
		{
			pBox0 = &s->outputDev[j].region.extents;

			/* must not intersect other output region */
			if (i != j && pBox0->x2 > pBox1->x1 && pBox0->x1 < pBox1->x2)
			{
				k--;
				break;
			}
		}
	}

	if (cs->moMode == CUBE_MOMODE_ONE)
	{
		cs->fullscreenOutput = FALSE;
		cs->nOutput = 1;
		return;
	}

	if (cs->moMode == CUBE_MOMODE_MULTI)
	{
		cs->fullscreenOutput = TRUE;
		cs->nOutput = 1;
		return;
	}

	if (k != s->nOutputDev)
	{
		cs->fullscreenOutput = FALSE;
		cs->nOutput = 1;
		return;
	}

	/* add output indices from left to right */
	j = 0;
	for (;;)
	{
		x = MAXSHORT;
		k = -1;

		for (i = 0; i < s->nOutputDev; i++)
		{
			if (cs->outputMask[i] != -1)
				continue;

			if (s->outputDev[i].region.extents.x1 < x)
			{
				x = s->outputDev[i].region.extents.x1;
				k = i;
			}
		}

		if (k < 0)
			break;

		cs->outputMask[k] = j;
		cs->output[j]     = k;

		j++;
	}

	cs->nOutput = j;

	if (cs->nOutput == 1)
	{
		if (s->outputDev[0].width  != s->width ||
			s->outputDev[0].height != s->height)
			cs->fullscreenOutput = FALSE;
	}
}

static void
cubeUpdateSkyDomeTexture (CompScreen *screen)
{
	CUBE_SCREEN (screen);

	finiTexture (screen, &cs->sky);
	initTexture (screen, &cs->sky);

	const BananaValue *
	option_skydome = bananaGetOption (bananaIndex,
	                                  "skydome",
	                                  screen->screenNum);

	if (!option_skydome->b)
		return;

	const BananaValue *
	option_skydome_image = bananaGetOption (bananaIndex,
	                                        "skydome_image",
	                                        screen->screenNum);

	if (strlen (option_skydome_image->s) == 0 ||
		!readImageToTexture (screen,
		                     &cs->sky,
		                     option_skydome_image->s,
		                     &cs->skyW,
		                     &cs->skyH))
	{
		const BananaValue *
		option_skydome_gradient_start_color = bananaGetOption (
		       bananaIndex, "skydome_gradient_start_color", screen->screenNum);

		const BananaValue *
		option_skydome_gradient_end_color = bananaGetOption (
		       bananaIndex, "skydome_gradient_end_color", screen->screenNum);

		unsigned short int gradStartColor[4];
		unsigned short int gradEndColor[4];

		stringToColor (option_skydome_gradient_start_color->s, gradStartColor);
		stringToColor (option_skydome_gradient_end_color->s, gradEndColor);

		GLfloat aaafTextureData[128][128][3];
		GLfloat fRStart = (GLfloat) gradStartColor[0] / 0xffff;
		GLfloat fGStart = (GLfloat) gradStartColor[1] / 0xffff;
		GLfloat fBStart = (GLfloat) gradStartColor[2] / 0xffff;
		GLfloat fREnd = (GLfloat) gradEndColor[0] / 0xffff;
		GLfloat fGEnd = (GLfloat) gradEndColor[1] / 0xffff;
		GLfloat fBEnd = (GLfloat) gradEndColor[2] / 0xffff;
		GLfloat fRStep = (fREnd - fRStart) / 128.0f;
		GLfloat fGStep = (fGEnd - fGStart) / 128.0f;
		GLfloat fBStep = (fBStart - fBEnd) / 128.0f;
		GLfloat fR = fRStart;
		GLfloat fG = fGStart;
		GLfloat fB = fBStart;

		int	iX, iY;

		for (iX = 127; iX >= 0; iX--)
		{
			fR += fRStep;
			fG += fGStep;
			fB -= fBStep;

			for (iY = 0; iY < 128; iY++)
			{
				aaafTextureData[iX][iY][0] = fR;
				aaafTextureData[iX][iY][1] = fG;
				aaafTextureData[iX][iY][2] = fB;
			}
		}

		cs->sky.target = GL_TEXTURE_2D;
		cs->sky.filter = GL_LINEAR;
		cs->sky.wrap   = GL_CLAMP_TO_EDGE;

		cs->sky.matrix.xx = 1.0 / 128.0;
		cs->sky.matrix.yy = -1.0 / 128.0;
		cs->sky.matrix.xy = 0;
		cs->sky.matrix.yx = 0;
		cs->sky.matrix.x0 = 0;
		cs->sky.matrix.y0 = 1.0;

		cs->skyW = 128;
		cs->skyH = 128;

		glGenTextures (1, &cs->sky.name);
		glBindTexture (cs->sky.target, cs->sky.name);

		glTexParameteri (cs->sky.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri (cs->sky.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexParameteri (cs->sky.target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (cs->sky.target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D (cs->sky.target,
		              0,
		              GL_RGB,
		              128,
		              128,
		              0,
		              GL_RGB,
		              GL_FLOAT,
		              aaafTextureData);

		glBindTexture (cs->sky.target, 0);
	}
}

static Bool
fillCircleTable (GLfloat   **ppSint,
                 GLfloat   **ppCost,
                 const int n)
{
	const GLfloat angle = 2 * M_PI / (GLfloat) ((n == 0) ? 1 : n);
	const int     size = abs (n);
	int           i;

	*ppSint = (GLfloat *) calloc (sizeof (GLfloat), size + 1);
	*ppCost = (GLfloat *) calloc (sizeof (GLfloat), size + 1);

	if (!(*ppSint) || !(*ppCost))
	{
		free (*ppSint);
		free (*ppCost);

		return FALSE;
	}

	(*ppSint)[0] = 0.0;
	(*ppCost)[0] = 1.0;

	for (i = 1; i < size; i++)
	{
		(*ppSint)[i] = sin (angle * i);
		(*ppCost)[i] = cos (angle * i);
	}

	(*ppSint)[size] = (*ppSint)[0];
	(*ppCost)[size] = (*ppCost)[0];

	return TRUE;
}

static void
cubeUpdateSkyDomeList (CompScreen *s,
                       GLfloat    fRadius)
{
	GLint   iSlices = 128;
	GLint   iStacks = 64;
	GLfloat afTexCoordX[4];
	GLfloat afTexCoordY[4];
	GLfloat *sint1;
	GLfloat *cost1;
	GLfloat *sint2;
	GLfloat *cost2;
	GLfloat r;
	GLfloat x;
	GLfloat y;
	GLfloat z;
	int     i;
	int     j;
	int     iStacksStart;
	int     iStacksEnd;
	int     iSlicesStart;
	int     iSlicesEnd;
	GLfloat fStepX;
	GLfloat fStepY;

	CUBE_SCREEN (s);

	const BananaValue *
	option_skydome_animated = bananaGetOption (bananaIndex,
	                                           "skydome_animated",
	                                           s->screenNum);

	if (option_skydome_animated->b)
	{
		iStacksStart = 11; /* min.   0 */
		iStacksEnd = 53;   /* max.  64 */
		iSlicesStart = 0;  /* min.   0 */
		iSlicesEnd = 128;  /* max. 128 */
	}
	else
	{
		iStacksStart = 21; /* min.   0 */
		iStacksEnd = 43;   /* max.  64 */
		iSlicesStart = 21; /* min.   0 */
		iSlicesEnd = 44;   /* max. 128 */
	}

	fStepX = 1.0 / (GLfloat) (iSlicesEnd - iSlicesStart);
	fStepY = 1.0 / (GLfloat) (iStacksEnd - iStacksStart);

	if (!fillCircleTable (&sint1, &cost1, -iSlices))
		return;

	if (!fillCircleTable (&sint2, &cost2, iStacks * 2))
	{
		free (sint1);
		free (cost1);
		return;
	}

	afTexCoordX[0] = 1.0f;
	afTexCoordY[0] = 1.0f - fStepY;
	afTexCoordX[1] = 1.0f - fStepX;
	afTexCoordY[1] = 1.0f - fStepY;
	afTexCoordX[2] = 1.0f - fStepX;
	afTexCoordY[2] = 1.0f;
	afTexCoordX[3] = 1.0f;
	afTexCoordY[3] = 1.0f;

	if (!cs->skyListId)
		cs->skyListId = glGenLists (1);

	glNewList (cs->skyListId, GL_COMPILE);

	enableTexture (s, &cs->sky, COMP_TEXTURE_FILTER_GOOD);

	glBegin (GL_QUADS);

	for (i = iStacksStart; i < iStacksEnd; i++)
	{
		afTexCoordX[0] = 1.0f;
		afTexCoordX[1] = 1.0f - fStepX;
		afTexCoordX[2] = 1.0f - fStepX;
		afTexCoordX[3] = 1.0f;

		for (j = iSlicesStart; j < iSlicesEnd; j++)
		{
			/* bottom-right */
			z = cost2[i];
			r = sint2[i];
			x = cost1[j];
			y = sint1[j];

			glTexCoord2f (
			    COMP_TEX_COORD_X (&cs->sky.matrix, afTexCoordX[3] * cs->skyW),
			    COMP_TEX_COORD_Y (&cs->sky.matrix, afTexCoordY[3] * cs->skyH));
			glVertex3f (x * r * fRadius, y * r * fRadius, z * fRadius);

			/* top-right */
			z = cost2[i + 1];
			r = sint2[i + 1];
			x = cost1[j];
			y = sint1[j];

			glTexCoord2f (
			    COMP_TEX_COORD_X (&cs->sky.matrix, afTexCoordX[0] * cs->skyW),
			    COMP_TEX_COORD_Y (&cs->sky.matrix, afTexCoordY[0] * cs->skyH));
			glVertex3f (x * r * fRadius, y * r * fRadius, z * fRadius);

			/* top-left */
			z = cost2[i + 1];
			r = sint2[i + 1];
			x = cost1[j + 1];
			y = sint1[j + 1];

			glTexCoord2f (
			    COMP_TEX_COORD_X (&cs->sky.matrix, afTexCoordX[1] * cs->skyW),
			    COMP_TEX_COORD_Y (&cs->sky.matrix, afTexCoordY[1] * cs->skyH));
			glVertex3f (x * r * fRadius, y * r * fRadius, z * fRadius);

			/* bottom-left */
			z = cost2[i];
			r = sint2[i];
			x = cost1[j + 1];
			y = sint1[j + 1];

			glTexCoord2f (
			    COMP_TEX_COORD_X (&cs->sky.matrix, afTexCoordX[2] * cs->skyW),
			    COMP_TEX_COORD_Y (&cs->sky.matrix, afTexCoordY[2] * cs->skyH));
			glVertex3f (x * r * fRadius, y * r * fRadius, z * fRadius);

			afTexCoordX[0] -= fStepX;
			afTexCoordX[1] -= fStepX;
			afTexCoordX[2] -= fStepX;
			afTexCoordX[3] -= fStepX;
		}

		afTexCoordY[0] -= fStepY;
		afTexCoordY[1] -= fStepY;
		afTexCoordY[2] -= fStepY;
		afTexCoordY[3] -= fStepY;
	}

	glEnd ();

	disableTexture (s, &cs->sky);

	glEndList ();

	free (sint1);
	free (cost1);
	free (sint2);
	free (cost2);
}

static void
cubeChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	if (strcasecmp (optionName, "color") == 0)
	{
		CompScreen *screen;

		if (screenNum != -1)
			screen = getScreenFromScreenNum (screenNum);
		else
			return;

		CUBE_SCREEN (screen);

		unsigned short int color[4];
		if (stringToColor (optionValue->s, color))
		{
			memcpy (cs->color, color, sizeof (cs->color));
			damageScreen (screen);
		}
	}
	else if (strcasecmp (optionName, "in") == 0)
	{
		CompScreen *screen;

		if (screenNum != -1)
			screen = getScreenFromScreenNum (screenNum);
		else
			return;

		cubeUpdateGeometry (screen, screen->hsize, optionValue->b ? -1 : 1);
	}
	else if (strcasecmp (optionName, "scale_image") == 0 ||
	         strcasecmp (optionName, "cubecap_image") == 0)
	{
		CompScreen *screen;

		if (screenNum != -1)
			screen = getScreenFromScreenNum (screenNum);
		else
			return;

		if (optionValue->b)
		{
			cubeLoadImg (screen);
			damageScreen (screen);
		}
	}
	else if (strcasecmp (optionName, "skydome") == 0 ||
	         strcasecmp (optionName, "skydome_image") == 0 ||
	         strcasecmp (optionName, "skydome_animated") == 0 ||
	         strcasecmp (optionName, "skydome_gradient_start_color") == 0 ||
	         strcasecmp (optionName, "skydome_gradient_end_color") == 0)
	{
		CompScreen *screen;

		if (screenNum != -1)
			screen = getScreenFromScreenNum (screenNum);
		else
			return;

		cubeUpdateSkyDomeTexture (screen);
		cubeUpdateSkyDomeList (screen, 1.0f);
		damageScreen (screen);
	}
	else if (strcasecmp (optionName, "multioutput_mode") == 0)
	{
		CompScreen *screen;

		if (screenNum != -1)
			screen = getScreenFromScreenNum (screenNum);
		else
			return;

		CUBE_SCREEN (screen);

		cs->moMode = optionValue->i;
		cubeUpdateOutputs (screen);
		cubeUpdateGeometry (screen, screen->hsize, cs->invert);
		damageScreen (screen);
	}
	else if (strcasecmp (optionName, "unfold_key") == 0)
	{
		updateKey (optionValue->s, &unfold_key);
	}
}

static int
adjustVelocity (CompScreen *s)
{
	CUBE_SCREEN (s);

	float unfold, adjust, amount;

	if (cs->unfolded)
		unfold = 1.0f - cs->unfold;
	else
		unfold = 0.0f - cs->unfold;

	const BananaValue *
	option_acceleration = bananaGetOption (bananaIndex,
	                                       "acceleration",
	                                       s->screenNum);

	adjust = unfold * 0.02f * option_acceleration->f;
	amount = fabs (unfold);
	if (amount < 1.0f)
		amount = 1.0f;
	else if (amount > 3.0f)
		amount = 3.0f;

	cs->unfoldVelocity = (amount * cs->unfoldVelocity + adjust) /
	     (amount + 2.0f);

	return (fabs (unfold) < 0.002f && fabs (cs->unfoldVelocity) < 0.01f);
}

static void
cubePreparePaintScreen (CompScreen *s,
                        int        msSinceLastPaint)
{
	float x, progress;

	CUBE_SCREEN (s);

	if (cs->grabIndex)
	{
		int   steps;
		float amount, chunk;

		const BananaValue *
		option_speed = bananaGetOption (bananaIndex, "speed", s->screenNum);

		const BananaValue *
		option_timestep = bananaGetOption (bananaIndex,
		                                   "timestep",
		                                   s->screenNum);

		amount = msSinceLastPaint * 0.2f * option_speed->f;
		steps  = amount / (0.5f * option_timestep->f);
		if (!steps) steps = 1;
		chunk  = amount / (float) steps;

		while (steps--)
		{
			cs->unfold += cs->unfoldVelocity * chunk;
			if (cs->unfold > 1.0f)
				cs->unfold = 1.0f;

			if (adjustVelocity (s))
			{
				if (cs->unfold < 0.5f)
				{
					if (cs->grabIndex)
					{
						removeScreenGrab (s, cs->grabIndex, NULL);
						cs->grabIndex = 0;
					}

					cs->unfold = 0.0f;
				}
				break;
			}
		}
	}

	memset (cs->cleared, 0, sizeof (Bool) * s->nOutputDev);
	memset (cs->capsPainted, 0, sizeof (Bool) * s->nOutputDev);

	/* Transparency handling */
	const BananaValue *
	option_transparent_manual_only = bananaGetOption (bananaIndex,
	                                                  "transparent_manual_only",
	                                                  s->screenNum);

	if (cs->rotationState == RotationManual ||
	    (cs->rotationState == RotationChange &&
	    !option_transparent_manual_only->b))
	{
		const BananaValue *
		option_active_opacity = bananaGetOption (bananaIndex,
		                                         "active_opacity",
		                                         s->screenNum);

		cs->lastOpacity = option_active_opacity->f;

		cs->toOpacity = (option_active_opacity->f / 100.0f) * OPAQUE; 
	}
	else if (cs->rotationState == RotationChange)
	{
		const BananaValue *
		option_inactive_opacity = bananaGetOption (bananaIndex,
		                                           "inactive_opacity",
		                                           s->screenNum);

		cs->lastOpacity = option_inactive_opacity->f;

		cs->toOpacity = (option_inactive_opacity->f / 100.0f) * OPAQUE;
	}
	else
	{
		const BananaValue *
		option_inactive_opacity = bananaGetOption (bananaIndex,
		                                           "inactive_opacity",
		                                           s->screenNum);

		cs->toOpacity = (option_inactive_opacity->f / 100.0f) * OPAQUE;
	}

	(*cs->getRotation) (s, &x, &x, &progress);

	if (cs->desktopOpacity != cs->toOpacity ||
		(progress > 0.0 && progress < 1.0))
	{
		const BananaValue *
		option_inactive_opacity = bananaGetOption (bananaIndex,
		                                           "inactive_opacity",
		                                           s->screenNum);

		cs->desktopOpacity =
		    (option_inactive_opacity->f -
		    ((option_inactive_opacity->f -
		    cs->lastOpacity) * progress))
		    / 100.0f * OPAQUE;
	}

	cs->paintAllViewports = (cs->desktopOpacity != OPAQUE);

	UNWRAP (cs, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, msSinceLastPaint);
	WRAP (cs, s, preparePaintScreen, cubePreparePaintScreen);
}

static void
cubePaintScreen (CompScreen   *s,
                 CompOutput   *outputs,
                 int          numOutputs,
                 unsigned int mask)
{
	float x, progress;

	CUBE_SCREEN (s);

	(*cs->getRotation) (s, &x, &x, &progress);

	UNWRAP (cs, s, paintScreen);
	if (cs->moMode == CUBE_MOMODE_ONE && s->nOutputDev &&
	    (progress > 0.0f || cs->desktopOpacity != OPAQUE))
		(*s->paintScreen) (s, &s->fullscreenOutput, 1, mask);
	else
		(*s->paintScreen) (s, outputs, numOutputs, mask);
	WRAP (cs, s, paintScreen, cubePaintScreen);
}

static Bool
cubePaintOutput (CompScreen              *s,
                 const ScreenPaintAttrib *sAttrib,
                 const CompTransform     *transform,
                 Region                  region,
                 CompOutput              *output,
                 unsigned int            mask)
{
	Bool status;

	CUBE_SCREEN (s);

	if (cs->grabIndex || cs->desktopOpacity != OPAQUE)
	{
		mask &= ~PAINT_SCREEN_REGION_MASK;
		mask |= PAINT_SCREEN_TRANSFORMED_MASK;
	}

	cs->srcOutput = (output->id != ~0) ? output->id : 0;
	/* Always use BTF painting on non-transformed screen */
	cs->paintOrder = BTF;

	UNWRAP (cs, s, paintOutput);
	status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
	WRAP (cs, s, paintOutput, cubePaintOutput);

	return status;
}

static void
cubeDonePaintScreen (CompScreen *s)
{
	CUBE_SCREEN (s);

	if (cs->grabIndex || cs->desktopOpacity != cs->toOpacity)
		damageScreen (s);

	UNWRAP (cs, s, donePaintScreen);
	(*s->donePaintScreen) (s);
	WRAP (cs, s, donePaintScreen, cubeDonePaintScreen);
}

static Bool
cubeCheckOrientation (CompScreen              *s,
                      const ScreenPaintAttrib *sAttrib,
                      const CompTransform     *transform,
                      CompOutput              *outputPtr,
                      CompVector              *points)
{
	CompTransform sTransform = *transform;
	CompTransform mvp, pm;
	CompVector    pntA, pntB, pntC;
	CompVector    vecA, vecB, ortho;
	Bool          rv = FALSE;

	CUBE_SCREEN (s);

	(*s->applyScreenTransform) (s, sAttrib, outputPtr, &sTransform);
	matrixTranslate (&sTransform, cs->outputXOffset, -cs->outputYOffset, 0.0f);
	matrixScale (&sTransform, cs->outputXScale, cs->outputYScale, 1.0f);

	memcpy (pm.m, s->projection, sizeof (pm.m));
	matrixMultiply (&mvp, &pm, &sTransform);

	matrixMultiplyVector (&pntA, &points[0], &mvp);

	if (pntA.w < 0.0f)
		rv = !rv;

	matrixVectorDiv (&pntA);

	matrixMultiplyVector (&pntB, &points[1], &mvp);

	if (pntB.w < 0.0f)
		rv = !rv;

	matrixVectorDiv (&pntB);
	matrixMultiplyVector (&pntC, &points[2], &mvp);
	matrixVectorDiv (&pntC);

	vecA.x = pntC.x - pntA.x;
	vecA.y = pntC.y - pntA.y;
	vecA.z = pntC.z - pntA.z;

	vecB.x = pntC.x - pntB.x;
	vecB.y = pntC.y - pntB.y;
	vecB.z = pntC.z - pntB.z;

	ortho.x = vecA.y * vecB.z - vecA.z * vecB.y;
	ortho.y = vecA.z * vecB.x - vecA.x * vecB.z;
	ortho.z = vecA.x * vecB.y - vecA.y * vecB.x;

	if (ortho.z > 0.0f)
		rv = !rv;

	return rv;
}

static Bool
cubeShouldPaintViewport (CompScreen              *s,
                         const ScreenPaintAttrib *sAttrib,
                         const CompTransform     *transform,
                         CompOutput              *outputPtr,
                         PaintOrder              order)
{
	Bool  ftb;
	float pointZ;

	CUBE_SCREEN (s);

	pointZ = cs->invert * cs->distance;
	CompVector vPoints[3] = { {.v = { -0.5, 0.0, pointZ, 1.0 } },
	                          {.v = {  0.0, 0.5, pointZ, 1.0 } },
	                          {.v = {  0.0, 0.0, pointZ, 1.0 } } };

	ftb = (*cs->checkOrientation) (s, sAttrib, transform, outputPtr, vPoints);

	return (order == FTB && ftb) || (order == BTF && !ftb);
}

static void
cubeMoveViewportAndPaint (CompScreen              *s,
                          const ScreenPaintAttrib *sAttrib,
                          const CompTransform     *transform,
                          CompOutput              *outputPtr,
                          unsigned int            mask,
                          PaintOrder              paintOrder,
                          int                     dx)
{
	int   output;

	CUBE_SCREEN (s);

	if (!(*cs->shouldPaintViewport) (s,
	                                 sAttrib,
	                                 transform,
	                                 outputPtr,
	                                 paintOrder))
		return;

	output = (outputPtr->id != ~0) ? outputPtr->id : 0;

	cs->paintOrder = paintOrder;

	if (cs->nOutput > 1)
	{
		int cubeOutput, dView;

		/* translate to cube output */
		cubeOutput = cs->outputMask[output];

		/* convert from window movement to viewport movement */
		dView = -dx;

		cubeOutput += dView;

		dView      = cubeOutput / cs->nOutput;
		cubeOutput = cubeOutput % cs->nOutput;

		if (cubeOutput < 0)
		{
			cubeOutput += cs->nOutput;
			dView--;
		}

		/* translate back to fusilli output */
		output = cs->srcOutput = cs->output[cubeOutput];

		setWindowPaintOffset (s, -dView * s->width, 0);
		(*cs->paintViewport) (s, sAttrib, transform,
		                      &s->outputDev[output].region,
		                      &s->outputDev[output], mask);
		setWindowPaintOffset (s, 0, 0);
	}
	else
	{
		Region region;

		setWindowPaintOffset (s, dx * s->width, 0);

		if (cs->moMode == CUBE_MOMODE_MULTI)
			region = &outputPtr->region;
		else
			region = &s->region;

		(*cs->paintViewport) (s, sAttrib, transform, region, outputPtr, mask);

		setWindowPaintOffset (s, 0, 0);
	}
}

static void
cubePaintAllViewports (CompScreen          *s,
                       ScreenPaintAttrib   *sAttrib,
                       const CompTransform *transform,
                       Region              region,
                       CompOutput          *outputPtr,
                       unsigned int        mask,
                       int                 xMove,
                       float               size,
                       int                 hsize,
                       PaintOrder          paintOrder)
{
	ScreenPaintAttrib sa = *sAttrib;

	int i;
	int xMoveAdd;
	int origXMoveAdd = 0; /* dx for the viewport we start
	                         painting with (back-most). */
	int iFirstSign;       /* 1 if we do xMove += i first and
	                         -1 if we do xMove -= i first. */

	CUBE_SCREEN (s);

	if (cs->invert == 1)
	{
		/* xMove ==> dx for the viewport which is the
		   nearest to the viewer in z axis.
		   xMove +/- hsize / 2 ==> dx for the viewport
		   which is the farthest to the viewer in z axis. */

		if ((sa.xRotate < 0.0f && hsize % 2 == 1) ||
			(sa.xRotate > 0.0f && hsize % 2 == 0))
		{
			origXMoveAdd = hsize / 2;
			iFirstSign = 1;
		}
		else
		{
			origXMoveAdd = -hsize / 2;
			iFirstSign = -1;
		}
	}
	else
	{
		/* xMove is already the dx for farthest viewport. */
		if (sa.xRotate > 0.0f)
			iFirstSign = -1;
		else
			iFirstSign = 1;
	}

	for (i = 0; i <= hsize / 2; i++)
	{
		/* move to the correct viewport (back to front). */
		xMoveAdd = origXMoveAdd;    /* move to farthest viewport. */
		xMoveAdd += iFirstSign * i; /* move i more viewports to
		                               the right / left. */

		/* Needed especially for unfold.
		   We paint the viewports around xMove viewport.
		   Adding or subtracting hsize from xMove has no effect on
		   what viewport we paint, but can make shorter paths. */
		if (xMoveAdd < -hsize / 2)
			xMoveAdd += hsize;
		else if (xMoveAdd > hsize / 2)
			xMoveAdd -= hsize;

		/* Paint the viewport. */
		xMove += xMoveAdd;

		sa.yRotate -= cs->invert * xMoveAdd * 360.0f / size;
		cubeMoveViewportAndPaint (s, &sa, transform, outputPtr, mask,
		                          paintOrder, xMove);
		sa.yRotate += cs->invert * xMoveAdd * 360.0f / size;

		xMove -= xMoveAdd;

		/* do the same for an equally far viewport. */
		if (i == 0 || i * 2 == hsize)
			continue;

		xMoveAdd = origXMoveAdd;    /* move to farthest viewport. */
		xMoveAdd -= iFirstSign * i; /* move i more viewports to the
		                               left / right (opposite side
		                               from the one chosen first) */

		if (xMoveAdd < -hsize / 2)
			xMoveAdd += hsize;
		else if (xMoveAdd > hsize / 2)
			xMoveAdd -= hsize;

		xMove += xMoveAdd;

		sa.yRotate -= cs->invert * xMoveAdd * 360.0f / size;
		cubeMoveViewportAndPaint (s, &sa, transform, outputPtr, mask,
		                          paintOrder, xMove);
		sa.yRotate += cs->invert * xMoveAdd * 360.0f / size;

		xMove -= xMoveAdd;
	}
}

static void
cubeGetRotation (CompScreen *s,
                 float      *x,
                 float      *v,
                 float      *progress)
{
	*x        = 0.0f;
	*v        = 0.0f;
	*progress = 0.0f;
}

static void
cubeClearTargetOutput (CompScreen *s,
                       float      xRotate,
                       float      vRotate)
{
	CUBE_SCREEN (s);

	if (cs->sky.name)
	{
		screenLighting (s, FALSE);

		glPushMatrix ();

		const BananaValue *
		option_skydome_animated = bananaGetOption (bananaIndex,
		                                           "skydome_animated",
		                                           s->screenNum);

		if (option_skydome_animated->b &&
		    cs->grabIndex == 0)
		{
			glRotatef (vRotate / 5.0f + 90.0f, 1.0f, 0.0f, 0.0f);
			glRotatef (xRotate, 0.0f, 0.0f, -1.0f);
		}
		else
		{
			glRotatef (90.0f, 1.0f, 0.0f, 0.0f);
		}

		glCallList (cs->skyListId);
		glPopMatrix ();
	}
	else
	{
		clearTargetOutput (GL_COLOR_BUFFER_BIT);
	}
}

static void
cubePaintTop (CompScreen              *s,
              const ScreenPaintAttrib *sAttrib,
              const CompTransform     *transform,
              CompOutput              *output,
              int                     size)
{
	ScreenPaintAttrib sa = *sAttrib;
	CompTransform     sTransform = *transform;

	CUBE_SCREEN (s);

	screenLighting (s, TRUE);

	glColor4us (cs->color[0], cs->color[1], cs->color[2], cs->desktopOpacity);

	glPushMatrix ();

	sa.yRotate += (360.0f / size) * (cs->xRotations + 1);

	const BananaValue *
	option_adjust_image = bananaGetOption (bananaIndex,
	                                       "adjust_image",
	                                       s->screenNum);

	if (!option_adjust_image->b)
		sa.yRotate -= (360.0f / size) * s->x;

	(*s->applyScreenTransform) (s, &sa, output, &sTransform);

	glLoadMatrixf (sTransform.m);
	glTranslatef (cs->outputXOffset, -cs->outputYOffset, 0.0f);
	glScalef (cs->outputXScale, cs->outputYScale, 1.0f);

	if (cs->desktopOpacity != OPAQUE)
	{
		screenTexEnvMode (s, GL_MODULATE);
		glEnable (GL_BLEND);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	glVertexPointer (3, GL_FLOAT, 0, cs->vertices);

	if (cs->invert == 1 && size == 4 && cs->texture.name)
	{
		enableTexture (s, &cs->texture, COMP_TEXTURE_FILTER_GOOD);
		glTexCoordPointer (2, GL_FLOAT, 0, cs->tc);
		glDrawArrays (GL_TRIANGLE_FAN, 0, cs->nVertices >> 1);
		disableTexture (s, &cs->texture);
		glDisableClientState (GL_TEXTURE_COORD_ARRAY);
	}
	else
	{
		glDisableClientState (GL_TEXTURE_COORD_ARRAY);
		glDrawArrays (GL_TRIANGLE_FAN, 0, cs->nVertices >> 1);
	}

	glPopMatrix ();

	glColor4usv (defaultColor);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);

	screenTexEnvMode (s, GL_REPLACE);
	glDisable (GL_BLEND);
	glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static void
cubePaintBottom (CompScreen              *s,
                 const ScreenPaintAttrib *sAttrib,
                 const CompTransform     *transform,
                 CompOutput              *output,
                 int                     size)
{
	ScreenPaintAttrib sa = *sAttrib;
	CompTransform     sTransform = *transform;

	CUBE_SCREEN (s);

	screenLighting (s, TRUE);

	glColor4us (cs->color[0], cs->color[1], cs->color[2], cs->desktopOpacity);

	glPushMatrix ();

	sa.yRotate += (360.0f / size) * (cs->xRotations + 1);

	const BananaValue *
	option_adjust_image = bananaGetOption (bananaIndex,
	                                       "adjust_image",
	                                       s->screenNum);

	if (!option_adjust_image->b)
		sa.yRotate -= (360.0f / size) * s->x;

	(*s->applyScreenTransform) (s, &sa, output, &sTransform);

	glLoadMatrixf (sTransform.m);
	glTranslatef (cs->outputXOffset, -cs->outputYOffset, 0.0f);
	glScalef (cs->outputXScale, cs->outputYScale, 1.0f);

	if (cs->desktopOpacity != OPAQUE)
	{
		screenTexEnvMode (s, GL_MODULATE);
		glEnable (GL_BLEND);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	glVertexPointer (3, GL_FLOAT, 0, cs->vertices);

	glDrawArrays (GL_TRIANGLE_FAN, cs->nVertices >> 1,
	              cs->nVertices >> 1);

	glPopMatrix ();

	glColor4usv (defaultColor);
	glEnableClientState (GL_TEXTURE_COORD_ARRAY);

	screenTexEnvMode (s, GL_REPLACE);
	glDisable (GL_BLEND);
	glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

static void
cubePaintInside (CompScreen              *s,
                 const ScreenPaintAttrib *sAttrib,
                 const CompTransform     *transform,
                 CompOutput              *output,
                 int                     size)
{
}

static void
cubeEnableOutputClipping (CompScreen          *s,
                          const CompTransform *transform,
                          Region              region,
                          CompOutput          *output)
{
	CUBE_SCREEN (s);

	if (cs->rotationState != RotationNone)
	{
		glPushMatrix ();
		glLoadMatrixf (transform->m);
		glTranslatef (cs->outputXOffset, -cs->outputYOffset, 0.0f);
		glScalef (cs->outputXScale, cs->outputYScale, 1.0f);

		if (cs->invert == 1)
		{
			GLdouble clipPlane0[] = {  1.0, 0.0, 0.5 / cs->distance, 0.0 };
			GLdouble clipPlane1[] = {  -1.0,  0.0, 0.5 / cs->distance, 0.0 };
			GLdouble clipPlane2[] = {  0.0,  -1.0, 0.5 / cs->distance, 0.0 };
			GLdouble clipPlane3[] = { 0.0,  1.0, 0.5 / cs->distance, 0.0 };
			glClipPlane (GL_CLIP_PLANE0, clipPlane0);
			glClipPlane (GL_CLIP_PLANE1, clipPlane1);
			glClipPlane (GL_CLIP_PLANE2, clipPlane2);
			glClipPlane (GL_CLIP_PLANE3, clipPlane3);
		}
		else
		{
			GLdouble clipPlane0[] = {  -1.0, 0.0, -0.5 / cs->distance, 0.0 };
			GLdouble clipPlane1[] = {  1.0,  0.0, -0.5 / cs->distance, 0.0 };
			GLdouble clipPlane2[] = {  0.0,  1.0, -0.5 / cs->distance, 0.0 };
			GLdouble clipPlane3[] = { 0.0,  -1.0, -0.5 / cs->distance, 0.0 };
			glClipPlane (GL_CLIP_PLANE0, clipPlane0);
			glClipPlane (GL_CLIP_PLANE1, clipPlane1);
			glClipPlane (GL_CLIP_PLANE2, clipPlane2);
			glClipPlane (GL_CLIP_PLANE3, clipPlane3);
		}

		glEnable (GL_CLIP_PLANE0);
		glEnable (GL_CLIP_PLANE1);
		glEnable (GL_CLIP_PLANE2);
		glEnable (GL_CLIP_PLANE3);

		glPopMatrix ();
	}
	else
	{
		UNWRAP (cs, s, enableOutputClipping);
		(*s->enableOutputClipping) (s, transform, region, output);
		WRAP (cs, s, enableOutputClipping, cubeEnableOutputClipping);
	}
}

static void
cubePaintViewport (CompScreen              *s,
                   const ScreenPaintAttrib *sAttrib,
                   const CompTransform     *transform,
                   Region                  region,
                   CompOutput              *output,
                   unsigned int            mask)
{
	(*s->paintTransformedOutput) (s, sAttrib, transform, region, output, mask);
}

static void
cubePaintTransformedOutput (CompScreen              *s,
                            const ScreenPaintAttrib *sAttrib,
                            const CompTransform     *transform,
                            Region                  region,
                            CompOutput              *outputPtr,
                            unsigned int            mask)
{
	ScreenPaintAttrib sa = *sAttrib;
	float             xRotate, vRotate, progress;
	int               hsize;
	float             size;
	GLenum            filter = display.textureFilter;
	PaintOrder        paintOrder;
	Bool              wasCulled = FALSE;
	Bool              paintCaps;
	int               cullNorm, cullInv;
	int               output = 0;

	CUBE_SCREEN (s);

	output = (outputPtr->id != ~0) ? outputPtr->id : 0;

	if (((outputPtr->id != ~0) && cs->recalcOutput) ||
	    ((outputPtr->id == ~0) && !cs->recalcOutput && cs->nOutput > 1))
	{
		cs->recalcOutput = (outputPtr->id == ~0);
		cs->nOutput      = 1;
		cubeUpdateGeometry (s, s->hsize, cs->invert);
	}

	hsize = s->hsize * cs->nOutput;
	size  = hsize;

	glGetIntegerv (GL_CULL_FACE_MODE, &cullNorm);
	cullInv   = (cullNorm == GL_BACK)? GL_FRONT : GL_BACK;
	wasCulled = glIsEnabled (GL_CULL_FACE);

	if (!cs->fullscreenOutput)
	{
		cs->outputXScale = (float) s->width / outputPtr->width;
		cs->outputYScale = (float) s->height / outputPtr->height;

		cs->outputXOffset =
		    (s->width / 2.0f -
		     (outputPtr->region.extents.x1 +
		      outputPtr->region.extents.x2) / 2.0f) /
		    (float) outputPtr->width;

		cs->outputYOffset =
		    (s->height / 2.0f -
		     (outputPtr->region.extents.y1 +
		      outputPtr->region.extents.y2) / 2.0f) /
		    (float) outputPtr->height;
	}
	else
	{
		cs->outputXScale  = 1.0f;
		cs->outputYScale  = 1.0f;
		cs->outputXOffset = 0.0f;
		cs->outputYOffset = 0.0f;
	}

	(*cs->getRotation) (s, &xRotate, &vRotate, &progress);

	sa.xRotate += xRotate;
	sa.vRotate += vRotate;

	if (!cs->cleared[output])
	{
		float rRotate;

		rRotate = xRotate - ((s->x *360.0f) / s->hsize);

		(*cs->clearTargetOutput) (s, rRotate, vRotate);
		cs->cleared[output] = TRUE;
	}

	mask &= ~PAINT_SCREEN_CLEAR_MASK;

	UNWRAP (cs, s, paintTransformedOutput);

	if (cs->grabIndex)
	{
		sa.vRotate = 0.0f;

		size += cs->unfold * 8.0f;
		size += powf (cs->unfold, 6) * 64.0;
		size += powf (cs->unfold, 16) * 8192.0;

		sa.zTranslate = -cs->invert * (0.5f / tanf (M_PI / size));

		/* distance we move the camera back when unfolding the cube.
		   currently hardcoded to 1.5 but it should probably be optional. */
		sa.zCamera -= cs->unfold * 1.5f;
	}
	else
	{
		if (vRotate > 100.0f)
			sa.vRotate = 100.0f;
		else if (vRotate < -100.0f)
			sa.vRotate = -100.0f;
		else
			sa.vRotate = vRotate;

		sa.zTranslate = -cs->invert * cs->distance;
	}

	if (sa.xRotate > 0.0f)
		cs->xRotations = (int) (hsize * sa.xRotate + 180.0f) / 360.0f;
	else
		cs->xRotations = (int) (hsize * sa.xRotate - 180.0f) / 360.0f;

	sa.xRotate -= (360.0f * cs->xRotations) / hsize;
	sa.xRotate *= cs->invert;

	sa.xRotate = sa.xRotate / size * hsize;

	const BananaValue *
	option_mipmap = bananaGetOption (bananaIndex, "mipmap", s->screenNum);

	if (cs->grabIndex && option_mipmap->b)
		display.textureFilter = GL_LINEAR_MIPMAP_LINEAR;

	if (cs->invert == 1)
	{
		/* Outside cube - start with FTB faces */
		paintOrder = FTB;
		glCullFace (cullInv);
	}
	else
	{
		/* Inside cube - start with BTF faces */
		paintOrder = BTF;
	}

	if (cs->invert == -1 || cs->paintAllViewports)
		cubePaintAllViewports (s, &sa, transform, region, outputPtr,
		                       mask, cs->xRotations, size, hsize, paintOrder);

	glCullFace (cullNorm);

	if (wasCulled && cs->paintAllViewports)
		glDisable (GL_CULL_FACE);

	paintCaps = !cs->grabIndex && (hsize > 2) && !cs->capsPainted[output] &&
	            (cs->invert != 1 || cs->desktopOpacity != OPAQUE ||
	             cs->paintAllViewports || sa.vRotate != 0.0f ||
	             sa.yTranslate != 0.0f);

	if (paintCaps)
	{
		Bool topDir, bottomDir, allCaps;

		static CompVector top[3] = { { .v = { 0.5, 0.5,  0.0, 1.0} },
		                         { .v = { 0.0, 0.5, -0.5, 1.0} },
		                         { .v = { 0.0, 0.5,  0.0, 1.0} } };
		static CompVector bottom[3] = { { .v = { 0.5, -0.5,  0.0, 1.0} },
		                                { .v = { 0.0, -0.5, -0.5, 1.0} },
		                        { .v = { 0.0, -0.5,  0.0, 1.0} } };

		topDir    = (*cs->checkOrientation) (s, &sa, transform, outputPtr, top);
		bottomDir = (*cs->checkOrientation) (s, &sa, transform,
		                            outputPtr, bottom);

		cs->capsPainted[output] = TRUE;

		allCaps = cs->paintAllViewports || cs->invert != 1;

		if (topDir && bottomDir)
		{
			glNormal3f (0.0f, -1.0f, 0.0f);
			if (allCaps)
			{
				(*cs->paintBottom) (s, &sa, transform, outputPtr, hsize);
				glNormal3f (0.0f, 0.0f, -1.0f);
				(*cs->paintInside) (s, &sa, transform, outputPtr, hsize);
				glNormal3f (0.0f, -1.0f, 0.0f);
			}
			(*cs->paintTop) (s, &sa, transform, outputPtr, hsize);
		}
		else if (!topDir && !bottomDir)
		{
			glNormal3f (0.0f, 1.0f, 0.0f);
			if (allCaps)
			{
				(*cs->paintTop) (s, &sa, transform, outputPtr, hsize);
				glNormal3f (0.0f, 0.0f, -1.0f);
				(*cs->paintInside) (s, &sa, transform, outputPtr, hsize);
				glNormal3f (0.0f, 1.0f, 0.0f);
			}
			(*cs->paintBottom) (s, &sa, transform, outputPtr, hsize);
		}
		else if (allCaps)
		{
			glNormal3f (0.0f, 1.0f, 0.0f);
			(*cs->paintTop) (s, &sa, transform, outputPtr, hsize);
			glNormal3f (0.0f, -1.0f, 0.0f);
			(*cs->paintBottom) (s, &sa, transform, outputPtr, hsize);
			glNormal3f (0.0f, 0.0f, -1.0f);
			(*cs->paintInside) (s, &sa, transform, outputPtr, hsize);
		}
		glNormal3f (0.0f, 0.0f, -1.0f);
	}

	if (wasCulled)
		glEnable (GL_CULL_FACE);

	if (cs->invert == 1)
	{
		/* Outside cube - continue with BTF faces */
		paintOrder = BTF;
	}
	else
	{
		/* Inside cube - continue with FTB faces */
		paintOrder = FTB;
		glCullFace (cullInv);
	}

	if (cs->invert == 1 || cs->paintAllViewports)
		cubePaintAllViewports (s, &sa, transform, region,
		                       outputPtr, mask, cs->xRotations,
		                       size, hsize, paintOrder);

	glCullFace (cullNorm);

	display.textureFilter = filter;

	WRAP (cs, s, paintTransformedOutput, cubePaintTransformedOutput);
}

static Bool
cubePaintWindow (CompWindow               *w,
                 const WindowPaintAttrib  *attrib,
                 const CompTransform      *transform,
                 Region                   region,
                 unsigned int             mask)
{
	Bool       status;
	CompScreen *s = w->screen;
	CUBE_SCREEN (s);

	if ((w->type & CompWindowTypeDesktopMask) &&
	    (attrib->opacity != cs->desktopOpacity))
	{
		WindowPaintAttrib wAttrib = *attrib;

		wAttrib.opacity = cs->desktopOpacity;

		UNWRAP (cs, s, paintWindow);
		status = (*s->paintWindow) (w, &wAttrib, transform, region, mask);
		WRAP (cs, s, paintWindow, cubePaintWindow);
	}
	else
	{
		UNWRAP (cs, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP (cs, s, paintWindow, cubePaintWindow);
	}

	return status;
}

static void
cubeInitWindowWalker (CompScreen *s, CompWalker* walker)
{
	CUBE_SCREEN (s);

	UNWRAP (cs, s, initWindowWalker);
	(*s->initWindowWalker) (s, walker);
	WRAP (cs, s, initWindowWalker, cubeInitWindowWalker);

	if (cs->paintOrder == FTB)
	{
		WalkInitProc tmpInit = walker->first;
		WalkStepProc tmpStep = walker->next;

		walker->first = walker->last;
		walker->last = tmpInit;

		walker->next = walker->prev;
		walker->prev = tmpStep;
	}
}

static void
cubeApplyScreenTransform (CompScreen              *s,
                          const ScreenPaintAttrib *sAttrib,
                          CompOutput              *output,
                          CompTransform	          *transform)
{
	CUBE_SCREEN (s);

	matrixTranslate (transform, cs->outputXOffset, -cs->outputYOffset, 0.0f);
	matrixScale (transform, cs->outputXScale, cs->outputYScale, 1.0f);

	UNWRAP (cs, s, applyScreenTransform);
	(*s->applyScreenTransform) (s, sAttrib, output, transform);
	WRAP (cs, s, applyScreenTransform, cubeApplyScreenTransform);

	matrixScale (transform, 1.0f / cs->outputXScale,
	             1.0f / cs->outputYScale, 1.0f);
	matrixTranslate (transform, -cs->outputXOffset, cs->outputYOffset, 0.0f);
}

static Bool
cubeUnfold (BananaArgument  *arg,
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
		CUBE_SCREEN (s);

		if (s->hsize * cs->nOutput < 4)
			return FALSE;

		if (otherScreenGrabExist (s, "rotate", "switcher", "cube", NULL))
			return FALSE;

		if (!cs->grabIndex)
			cs->grabIndex = pushScreenGrab (s, s->invisibleCursor, "cube");

		if (cs->grabIndex)
		{
			cs->unfolded = TRUE;
			damageScreen (s);
		}
	}

	return FALSE;
}

static Bool
cubeFold (BananaArgument  *arg,
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
		CUBE_SCREEN (s);

		if (xid && s->root != xid)
			continue;

		if (cs->grabIndex)
		{
			cs->unfolded = FALSE;
			damageScreen (s);
		}
	}

	return FALSE;
}

static void
cubeHandleEvent (XEvent      *event)
{
	CUBE_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &unfold_key))
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xkey.root;

			cubeUnfold (&arg, 1);
		}
		break;
	default:
		if (event->type == display.xkbEvent)
		{
			XkbAnyEvent *xkbEvent = (XkbAnyEvent *) event;

			if (xkbEvent->xkb_type == XkbStateNotify)
			{
				XkbStateNotifyEvent *stateEvent = (XkbStateNotifyEvent *) event;
				if (stateEvent->event_type == KeyRelease)
				{

					cubeFold (NULL, 0);
				}
			}
		}
		break;
	}

	UNWRAP (cd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (cd, &display, handleEvent, cubeHandleEvent);
}

static void
cubeOutputChangeNotify (CompScreen *s)
{
	CUBE_SCREEN (s);

	cubeUpdateOutputs (s);
	cubeUpdateGeometry (s, s->hsize, cs->invert);

	const BananaValue *
	option_cubecap_image = bananaGetOption (bananaIndex,
	                                        "cubecap_image",
	                                        s->screenNum);

	if (strlen (option_cubecap_image->s) > 0)
		cubeLoadImg (s);

	UNWRAP (cs, s, outputChangeNotify);
	(*s->outputChangeNotify) (s);
	WRAP (cs, s, outputChangeNotify, cubeOutputChangeNotify);
}

static void
coreChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	if (strcasecmp (optionName, "hsize") == 0)
	{
		CompScreen *screen;

		if (screenNum != -1)
			screen = getScreenFromScreenNum (screenNum);
		else
			return;

		CUBE_SCREEN (screen);

		cubeUpdateGeometry (screen, optionValue->i, cs->invert);
	}
}

static Bool
cubeInitCore (CompPlugin *p,
              CompCore   *c)
{
	CubeCore *cc;

	cc = malloc (sizeof (CubeCore));
	if (!cc)
		return FALSE;

	cubeDisplayPrivateIndex = allocateDisplayPrivateIndex ();
	if (cubeDisplayPrivateIndex < 0)
	{
		free (cc);
		return FALSE;
	}

	//write cubeDisplayPrivateIndex to option index (for rotate plugin)
	BananaValue index;
	index.i = cubeDisplayPrivateIndex;
	bananaSetOption (bananaIndex, "index", -1, &index);

	c->base.privates[cubeCorePrivateIndex].ptr = cc;

	return TRUE;
}

static void
cubeFiniCore (CompPlugin *p,
              CompCore   *c)
{
	CUBE_CORE (c);

	freeDisplayPrivateIndex (cubeDisplayPrivateIndex);

	free (cc);
}

static Bool
cubeInitDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	CubeDisplay *cd;

	cd = malloc (sizeof (CubeDisplay));
	if (!cd)
		return FALSE;

	cd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (cd->screenPrivateIndex < 0)
	{
		free (cd);
		return FALSE;
	}

	WRAP (cd, d, handleEvent, cubeHandleEvent);

	d->base.privates[cubeDisplayPrivateIndex].ptr = cd;

	return TRUE;
}

static void
cubeFiniDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	CUBE_DISPLAY (d);

	freeScreenPrivateIndex (cd->screenPrivateIndex);

	UNWRAP (cd, d, handleEvent);

	free (cd);
}

static Bool
cubeInitScreen (CompPlugin *p,
                CompScreen *s)
{
	CubeScreen *cs;

	CUBE_DISPLAY (&display);

	cs = malloc (sizeof (CubeScreen));
	if (!cs)
		return FALSE;

	cs->pw = 0;
	cs->ph = 0;

	cs->invert = 1;

	cs->tc[0] = cs->tc[1] = cs->tc[2] = cs->tc[3] = 0.0f;
	cs->tc[4] = cs->tc[5] = cs->tc[6] = cs->tc[7] = 0.0f;

	const BananaValue *
	option_color = bananaGetOption (bananaIndex, "color", s->screenNum);

	unsigned short int color[4];
	if (stringToColor (option_color->s, color))
		memcpy (cs->color, color, sizeof (cs->color));

	cs->nVertices = 0;
	cs->vertices  = NULL;

	cs->grabIndex = 0;

	cs->srcOutput = 0;

	cs->skyListId = 0;

	cs->getRotation         = cubeGetRotation;
	cs->clearTargetOutput   = cubeClearTargetOutput;
	cs->paintTop            = cubePaintTop;
	cs->paintBottom         = cubePaintBottom;
	cs->paintInside         = cubePaintInside;
	cs->checkOrientation    = cubeCheckOrientation;
	cs->paintViewport       = cubePaintViewport;
	cs->shouldPaintViewport = cubeShouldPaintViewport;

	s->base.privates[cd->screenPrivateIndex].ptr = cs;

	initTexture (s, &cs->texture);
	initTexture (s, &cs->sky);

	cs->unfolded = FALSE;
	cs->unfold   = 0.0f;

	cs->unfoldVelocity = 0.0f;

	cs->paintAllViewports = FALSE;
	cs->fullscreenOutput  = TRUE;

	cs->outputXScale  = 1.0f;
	cs->outputYScale  = 1.0f;
	cs->outputXOffset = 0.0f;
	cs->outputYOffset = 0.0f;

	cs->rotationState = RotationNone;

	cs->desktopOpacity = OPAQUE;

	const BananaValue *
	option_inactive_opacity = bananaGetOption (bananaIndex,
	                                           "inactive_opacity",
	                                           s->screenNum);

	cs->lastOpacity = option_inactive_opacity->f;

	const BananaValue *
	option_multioutput_mode = bananaGetOption (bananaIndex,
	                                           "multioutput_mode",
	                                           s->screenNum);

	cs->moMode = option_multioutput_mode->i;

	cs->recalcOutput = FALSE;

	memset (cs->cleared, 0, sizeof (cs->cleared));

	cubeUpdateOutputs (s);

	if (!cubeUpdateGeometry (s, s->hsize, cs->invert))
	{
		free (cs);
		return FALSE;
	}

	const BananaValue *
	option_cubecap_image = bananaGetOption (bananaIndex,
	                                        "cubecap_image",
	                                        s->screenNum);

	if (strlen(option_cubecap_image->s) > 0)
	{
		cubeLoadImg (s);
		damageScreen (s);
	}

	WRAP (cs, s, preparePaintScreen, cubePreparePaintScreen);
	WRAP (cs, s, donePaintScreen, cubeDonePaintScreen);
	WRAP (cs, s, paintScreen, cubePaintScreen);
	WRAP (cs, s, paintOutput, cubePaintOutput);
	WRAP (cs, s, paintTransformedOutput, cubePaintTransformedOutput);
	WRAP (cs, s, enableOutputClipping, cubeEnableOutputClipping);
	WRAP (cs, s, paintWindow, cubePaintWindow);
	WRAP (cs, s, applyScreenTransform, cubeApplyScreenTransform);
	WRAP (cs, s, outputChangeNotify, cubeOutputChangeNotify);
	WRAP (cs, s, initWindowWalker, cubeInitWindowWalker);

	return TRUE;
}

static void
cubeFiniScreen (CompPlugin *p,
                CompScreen *s)
{
	CUBE_SCREEN (s);

	if (cs->vertices)
		free (cs->vertices);

	if (cs->skyListId)
		glDeleteLists (cs->skyListId, 1);

	UNWRAP (cs, s, preparePaintScreen);
	UNWRAP (cs, s, donePaintScreen);
	UNWRAP (cs, s, paintScreen);
	UNWRAP (cs, s, paintOutput);
	UNWRAP (cs, s, paintTransformedOutput);
	UNWRAP (cs, s, enableOutputClipping);
	UNWRAP (cs, s, paintWindow);
	UNWRAP (cs, s, applyScreenTransform);
	UNWRAP (cs, s, outputChangeNotify);
	UNWRAP (cs, s, initWindowWalker);

	finiTexture (s, &cs->texture);
	finiTexture (s, &cs->sky);

	free (cs);
}

static CompBool
cubeInitObject (CompPlugin *p,
                CompObject *o)
{
	static InitPluginObjectProc dispTab[] = {
		(InitPluginObjectProc) cubeInitCore,
		(InitPluginObjectProc) cubeInitDisplay,
		(InitPluginObjectProc) cubeInitScreen
	};

	RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
cubeFiniObject (CompPlugin *p,
                CompObject *o)
{
	static FiniPluginObjectProc dispTab[] = {
		(FiniPluginObjectProc) cubeFiniCore,
		(FiniPluginObjectProc) cubeFiniDisplay,
		(FiniPluginObjectProc) cubeFiniScreen
	};

	DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
cubeInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("cube", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	cubeCorePrivateIndex = allocateCorePrivateIndex ();

	if (cubeCorePrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("cube");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, cubeChangeNotify);

	bananaAddChangeNotifyCallBack (coreBananaIndex, coreChangeNotify);

	const BananaValue *
	option_unfold_key = bananaGetOption (bananaIndex, "unfold_key", -1);

	registerKey (option_unfold_key->s, &unfold_key);

	return TRUE;
}

static void
cubeFini (CompPlugin *p)
{
	freeCorePrivateIndex (cubeCorePrivateIndex);

	bananaRemoveChangeNotifyCallBack (coreBananaIndex, coreChangeNotify);

	bananaUnloadPlugin (bananaIndex);
}

CompPluginVTable cubeVTable = {
	"cube",
	cubeInit,
	cubeFini,
	cubeInitObject,
	cubeFiniObject
};

CompPluginVTable *
getCompPluginInfo20140724 (void)
{
	return &cubeVTable;
}
