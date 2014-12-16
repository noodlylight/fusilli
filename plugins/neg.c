/*
 * Copyright (c) 2006 Darryll Truchan <moppsy@comcast.net>
 * Copyright (c) 2014 Michail Bitzes <noodlylight@gmail.com>
 *
 * Pixel shader negating by Dennis Kasprzyk <onestone@beryl-project.org>
 * Usage of matches by Danny Baumann <maniac@beryl-project.org>
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
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <fusilli-core.h>

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _NegDisplay {
	int screenPrivateIndex;

	CompKeyBinding window_toggle_key, screen_toggle_key;

	HandleEventProc            handleEvent;
	MatchPropertyChangedProc   matchPropertyChanged;
} NegDisplay;


typedef struct _NegScreen {
	int windowPrivateIndex;

	CompMatch neg_match, exclude_match;

	DrawWindowTextureProc drawWindowTexture;
	WindowAddNotifyProc   windowAddNotify;

	Bool isNeg; /* negative screen flag */

	int negFunction;
	int negAlphaFunction;
} NegScreen;

typedef struct _NegWindow {
	Bool isNeg; /* negative window flag */
} NegWindow;

#define GET_NEG_DISPLAY(d) \
	((NegDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define NEG_DISPLAY(d) \
	NegDisplay *nd = GET_NEG_DISPLAY (d)

#define GET_NEG_SCREEN(s, nd) \
	((NegScreen *) (s)->privates[(nd)->screenPrivateIndex].ptr)

#define NEG_SCREEN(s) \
	NegScreen *ns = GET_NEG_SCREEN (s, GET_NEG_DISPLAY (&display))

#define GET_NEG_WINDOW(w, ns) \
	((NegWindow *) (w)->privates[(ns)->windowPrivateIndex].ptr)

#define NEG_WINDOW(w) \
	NegWindow *nw = GET_NEG_WINDOW  (w, \
	                GET_NEG_SCREEN  (w->screen, \
	                GET_NEG_DISPLAY (&display)))


static void
negToggle (CompWindow *w)
{
	NEG_WINDOW (w);
	NEG_SCREEN (w->screen);

	/* toggle window negative flag */
	nw->isNeg = !nw->isNeg;

	/* check exclude list */
	if (matchEval (&ns->exclude_match, w))
		nw->isNeg = FALSE;

	/* cause repainting */
	addWindowDamage (w);
}

static void
negToggleScreen (CompScreen *s)
{
	CompWindow *w;

	NEG_SCREEN(s);

	/* toggle screen negative flag */
	ns->isNeg = !ns->isNeg;

	/* toggle every window */
	for (w = s->windows; w; w = w->next)
		if (w)
			negToggle (w);
}

static void
negHandleEvent (XEvent      *event)
{
	NEG_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &nd->window_toggle_key))
		{
			CompWindow *w = findWindowAtDisplay (display.activeWindow);

			if (w)
				negToggle (w);
		}
		else if (isKeyPressEvent (event, &nd->screen_toggle_key))
		{
			CompScreen *s = findScreenAtDisplay (event->xkey.root);

			if (s)
				negToggleScreen (s);
		}
	default:
		break;
	}

	UNWRAP (nd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (nd, &display, handleEvent, negHandleEvent);
}

static int
getNegFragmentFunction (CompScreen  *s,
                        CompTexture *texture,
                        Bool        alpha)
{
	CompFunctionData *data;
	int              target;

	NEG_SCREEN (s);

	if (texture->target == GL_TEXTURE_2D)
		target = COMP_FETCH_TARGET_2D;
	else
		target = COMP_FETCH_TARGET_RECT;

	if (alpha)
	{
		if (ns->negAlphaFunction)
			return ns->negAlphaFunction;
	}
	else
	{
		if (ns->negFunction)
			return ns->negFunction;
	}

	data = createFunctionData ();
	if (data)
	{
		Bool ok = TRUE;
		int  handle = 0;

		if (alpha)
			ok &= addTempHeaderOpToFunctionData (data, "neg" );

		ok &= addFetchOpToFunctionData (data, "output", NULL, target);
		if (alpha)
		{
			ok &= addDataOpToFunctionData (data, "RCP neg.a, output.a;");
			ok &= addDataOpToFunctionData (data,
			                          "MAD output.rgb, -neg.a, output, 1.0;");
		}
		else
			ok &= addDataOpToFunctionData (data,
			                               "SUB output.rgb, 1.0, output;");

		if (alpha)
			ok &= addDataOpToFunctionData (data,
			                               "MUL output.rgb, output.a, output;");

		ok &= addColorOpToFunctionData (data, "output", "output");
		if (!ok)
		{
			destroyFunctionData (data);
			return 0;
		}

		handle = createFragmentFunction (s, "neg", data);

		if (alpha)
			ns->negAlphaFunction = handle;
		else
			ns->negFunction = handle;

		destroyFunctionData (data);

		return handle;
	}

	return 0;
}

static void
negDrawWindowTexture (CompWindow           *w,
                      CompTexture          *texture,
                      const FragmentAttrib *attrib,
                      unsigned int         mask)
{
	int filter;

	NEG_SCREEN (w->screen);
	NEG_WINDOW (w);

	/* only negate window contents; that's the only case
	   where w->texture->name == texture->name */
	if (nw->isNeg && (texture->name == w->texture->name))
	{
		if (w->screen->fragmentProgram)
		{
			FragmentAttrib fa = *attrib;
			int            function;

			function = getNegFragmentFunction (w->screen, texture, w->alpha);
			if (function)
				addFragmentFunction (&fa, function);

			UNWRAP (ns, w->screen, drawWindowTexture);
			(*w->screen->drawWindowTexture) (w, texture, &fa, mask);
			WRAP (ns, w->screen, drawWindowTexture, negDrawWindowTexture);
		}
		else
		{
			/* this is for the most part taken from paint.c */

			if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
				filter = w->screen->filter[WINDOW_TRANS_FILTER];
			else if (mask & PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK)
				filter = w->screen->filter[SCREEN_TRANS_FILTER];
			else
				filter = w->screen->filter[NOTHING_TRANS_FILTER];

			/* if we can adjust saturation, even if it's just on and off */
			if (w->screen->canDoSaturated && attrib->saturation != COLOR)
			{
				GLfloat constant[4];

				/* if the paint mask has this set we want to blend */
				if (mask & PAINT_WINDOW_TRANSLUCENT_MASK)
					glEnable (GL_BLEND);

				/* enable the texture */
				enableTexture (w->screen, texture, filter);

				/* texture combiner */
				glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
				glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
				glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
				glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PRIMARY_COLOR);
				glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_PRIMARY_COLOR);

				/* negate */
				glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_RGB,
				           GL_ONE_MINUS_SRC_COLOR);

				glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
				glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);

				glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
				glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
				glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

				glColor4f (1.0f, 1.0f, 1.0f, 0.5f);

				/* make another texture active */
				(*w->screen->activeTexture) (GL_TEXTURE1_ARB);

				/* enable that texture */
				enableTexture (w->screen, texture, filter);

				glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
				glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_DOT3_RGB);
				glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
				glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
				glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
				glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);

				/* if we can do saturation that is in between min and max */
				if (w->screen->canDoSlightlySaturated && attrib->saturation > 0)
				{
					glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

					constant[0] = 0.5f + 0.5f * RED_SATURATION_WEIGHT;
					constant[1] = 0.5f + 0.5f * GREEN_SATURATION_WEIGHT;
					constant[2] = 0.5f + 0.5f * BLUE_SATURATION_WEIGHT;
					constant[3] = 1.0;

					glTexEnvfv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);

					/* mark another texture active */
					(*w->screen->activeTexture) (GL_TEXTURE2_ARB);

					/* enable that texture */
					enableTexture (w->screen, texture, filter);

					glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
					glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE0);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_PREVIOUS);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE2_RGB, GL_CONSTANT);

					/* negate */
					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_RGB,
					           GL_ONE_MINUS_SRC_COLOR);

					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);

					glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);

					/* color constant */
					constant[3] = attrib->saturation / 65535.0f;

					glTexEnvfv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);

					/* if we are not opaque or not fully bright */
					if (attrib->opacity < OPAQUE ||
					    attrib->brightness != BRIGHT)
					{
						/* activate a new texture */
						(*w->screen->activeTexture) (GL_TEXTURE3_ARB);

						/* enable that texture */
						enableTexture (w->screen, texture, filter);

						/* color constant */
						constant[3] = attrib->opacity / 65535.0f;
						constant[0] = constant[1] = constant[2] =
						           constant[3] * attrib->brightness / 65535.0f;

						glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR,
						                           constant);

						glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
						                          GL_COMBINE);

						glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_RGB,
						                           GL_MODULATE);
						glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_RGB,
						                           GL_PREVIOUS);
						glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE1_RGB,
						                           GL_CONSTANT);
						glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_RGB,
						                           GL_SRC_COLOR);
						glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND1_RGB,
						                           GL_SRC_COLOR);

						glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_ALPHA,
						                           GL_MODULATE);
						glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA,
						                          GL_PREVIOUS);
						glTexEnvf(GL_TEXTURE_ENV, GL_SOURCE1_ALPHA,
						                          GL_CONSTANT);
						glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA,
						                          GL_SRC_ALPHA);
						glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA,
						                          GL_SRC_ALPHA);

						/* draw the window geometry */
						(*w->drawWindowGeometry) (w);

						/* disable the current texture */
						disableTexture (w->screen, texture);

						/* set texture mode back to replace */
						glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE,
						                           GL_REPLACE);

						/* re-activate last texture */
						(*w->screen->activeTexture) (GL_TEXTURE2_ARB);
					}
					else
					{
						/* fully opaque and bright */

						/* draw the window geometry */
						(*w->drawWindowGeometry) (w);
					}

					/* disable the current texture */
					disableTexture (w->screen, texture);

					/* set the texture mode back to replace */
					glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

					/* re-activate last texture */
					(*w->screen->activeTexture) (GL_TEXTURE1_ARB);
				}
				else
				{
					/* fully saturated or fully unsaturated */

					glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_CONSTANT);
					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);

					/* color constant */
					constant[3] = attrib->opacity / 65535.0f;
					constant[0] = constant[1] = constant[2] =
					              constant[3] * attrib->brightness / 65535.0f;

					constant[0] = 0.5f + 0.5f * 
					              RED_SATURATION_WEIGHT * constant[0];
					constant[1] = 0.5f + 0.5f * 
					              GREEN_SATURATION_WEIGHT * constant[1];
					constant[2] = 0.5f + 0.5f * 
					              BLUE_SATURATION_WEIGHT * constant[2];

					glTexEnvfv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);

					/* draw the window geometry */
					(*w->drawWindowGeometry) (w);
				}

				/* disable the current texture */
				disableTexture (w->screen, texture);

				/* set the texture mode back to replace */
				glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

				/* re-activate last texture */
				(*w->screen->activeTexture) (GL_TEXTURE0_ARB);

				/* disable that texture */
				disableTexture (w->screen, texture);

				/* set the default color */
				glColor4usv (defaultColor);

				/* set screens texture mode back to replace */
				screenTexEnvMode (w->screen, GL_REPLACE);

				/* if it's a translucent window, disable blending */
				if (mask & PAINT_WINDOW_TRANSLUCENT_MASK)
					glDisable (GL_BLEND);
			}
			else
			{
				/* no saturation adjustments */

				/* enable the current texture */
				enableTexture (w->screen, texture, filter);

				glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
				glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
				glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);

				/* negate */
				glTexEnvf(GL_TEXTURE_ENV, GL_OPERAND0_RGB,
				          GL_ONE_MINUS_SRC_COLOR);

				/* we are not opaque or fully bright */
				if ((mask & PAINT_WINDOW_TRANSLUCENT_MASK) ||
				     attrib->brightness != BRIGHT)
				{
					GLfloat constant[4];

					/* enable blending */
					glEnable (GL_BLEND);

					/* color constant */
					constant[3] = attrib->opacity / 65535.0f;
					constant[0] = constant[3] * attrib->brightness / 65535.0f;
					constant[1] = constant[3] * attrib->brightness / 65535.0f;
					constant[2] = constant[3] * attrib->brightness / 65535.0f;

					glTexEnvfv (GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, constant);
					glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
					glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_TEXTURE);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);

					/* negate */
					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_RGB,
					           GL_ONE_MINUS_SRC_COLOR);

					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
					glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
					glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_TEXTURE);
					glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE1_ALPHA, GL_CONSTANT);
					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
					glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);

					/* draw the window geometry */
					(*w->drawWindowGeometry) (w);

					/* disable blending */
					glDisable (GL_BLEND);
				}
				else
				{
					/* no adjustments to saturation, brightness or opacity */

					/* draw the window geometry */
					(*w->drawWindowGeometry) (w);
				}

				/* disable the current texture */
				disableTexture (w->screen, texture);

				/* set the screens texture mode back to replace */
				screenTexEnvMode (w->screen, GL_REPLACE);
			}
		}
	}
	else
	{
		/* not negative */
		UNWRAP (ns, w->screen, drawWindowTexture);
		(*w->screen->drawWindowTexture) (w, texture, attrib, mask);
		WRAP (ns, w->screen, drawWindowTexture, negDrawWindowTexture);
	}
}

static void
negMatchPropertyChanged (CompWindow  *w)
{
	NEG_DISPLAY (&display);
	NEG_SCREEN (w->screen);
	NEG_WINDOW (w);
	Bool isNeg;

	UNWRAP (nd, &display, matchPropertyChanged);
	(*display.matchPropertyChanged) (w);
	WRAP (nd, &display, matchPropertyChanged, negMatchPropertyChanged);

	isNeg = matchEval (&ns->neg_match, w);
	isNeg = isNeg && !matchEval (&ns->exclude_match, w);

	if (isNeg != nw->isNeg && ns->isNeg)
		negToggle (w);
}

static void
negWindowAdd (CompScreen *s,
              CompWindow *w)
{
	NEG_SCREEN (s);

	/* nw->isNeg is initialized to FALSE in InitWindow, so we only
	   have to toggle it to TRUE if necessary */
	if (ns->isNeg && matchEval (&ns->neg_match, w))
		negToggle (w);
}

static void
matchesWereChanged (CompScreen *s)
{
	NEG_SCREEN (s);

	CompWindow *w;

	for (w = s->windows; w; w = w->next)
	{
		Bool isNeg;
		NEG_WINDOW (w);

		isNeg = matchEval (&ns->neg_match, w);
		isNeg = isNeg && !matchEval (&ns->exclude_match, w);

		if (isNeg && ns->isNeg && !nw->isNeg)
			negToggle (w);
		else if (!isNeg && nw->isNeg)
			negToggle (w);
	}
}

static void
negChangeNotify (const char        *optionName,
                 BananaType        optionType,
                 const BananaValue *optionValue,
                 int               screenNum)
{
	NEG_DISPLAY (&display);

	if (strcasecmp (optionName, "neg_match") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);
		NEG_SCREEN (s);

		matchFini (&ns->neg_match);
		matchInit (&ns->neg_match);
		matchAddFromString (&ns->neg_match, optionValue->s);
		matchUpdate (&ns->neg_match);

		matchesWereChanged (s);
	}
	else if (strcasecmp (optionName, "exclude_match") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);
		NEG_SCREEN (s);

		matchFini (&ns->exclude_match);
		matchInit (&ns->exclude_match);
		matchAddFromString (&ns->exclude_match, optionValue->s);
		matchUpdate (&ns->exclude_match);

		matchesWereChanged (s);
	}
	else if (strcasecmp (optionName, "window_toggle_key") == 0)
	{
		updateKey (optionValue->s, &nd->window_toggle_key);
	}
	else if (strcasecmp (optionName, "screen_toggle_key") == 0)
	{
		updateKey (optionValue->s, &nd->screen_toggle_key);
	}
}

static void
negWindowAddNotify (CompWindow *w)
{
	NEG_SCREEN (w->screen);

	negWindowAdd (w->screen, w);

	UNWRAP (ns, w->screen, windowAddNotify);
	(*w->screen->windowAddNotify) (w);
	WRAP (ns, w->screen, windowAddNotify, negWindowAddNotify);
}

static Bool
negInitDisplay (CompPlugin  *p,
                CompDisplay *d)
{
	NegDisplay *nd;

	nd = malloc (sizeof (NegDisplay));
	if (!nd)
		return FALSE;

	nd->screenPrivateIndex = allocateScreenPrivateIndex();
	if (nd->screenPrivateIndex < 0)
	{
		free (nd);
		return FALSE;
	}

	WRAP (nd, d, handleEvent, negHandleEvent);
	WRAP (nd, d, matchPropertyChanged, negMatchPropertyChanged);

	const BananaValue *
	option_window_toggle_key = bananaGetOption (bananaIndex,
	                                            "window_toggle_key",
	                                            -1);

	registerKey (option_window_toggle_key->s, &nd->window_toggle_key);

	const BananaValue *
	option_screen_toggle_key = bananaGetOption (bananaIndex,
	                                            "screen_toggle_key",
	                                            -1);

	registerKey (option_screen_toggle_key->s, &nd->screen_toggle_key);

	d->privates[displayPrivateIndex].ptr = nd;

	return TRUE;
}

static void
negFiniDisplay (CompPlugin  *p,
                CompDisplay *d)
{
	NEG_DISPLAY (d);

	UNWRAP (nd, d, handleEvent);
	UNWRAP (nd, d, matchPropertyChanged);

	freeScreenPrivateIndex (nd->screenPrivateIndex);

	free (nd);
}

static Bool
negInitScreen (CompPlugin *p,
               CompScreen *s)
{
	NegScreen *ns;

	NEG_DISPLAY (&display);

	ns = malloc (sizeof (NegScreen));
	if (!ns)
		return FALSE;

	ns->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (ns->windowPrivateIndex < 0)
	{
		free (ns);
		return FALSE;
	}

	/* initialize the screen variables
	 * you know what happens if you don't
	*/
	ns->isNeg = FALSE;

	ns->negFunction      = 0;
	ns->negAlphaFunction = 0;

	WRAP (ns, s, drawWindowTexture, negDrawWindowTexture);
	WRAP (ns, s, windowAddNotify, negWindowAddNotify);

	const BananaValue *
	option_neg_match = bananaGetOption (bananaIndex,
	                                    "neg_match",
	                                    s->screenNum);

	matchInit (&ns->neg_match);
	matchAddFromString (&ns->neg_match, option_neg_match->s);
	matchUpdate (&ns->neg_match);

	const BananaValue *
	option_exclude_match = bananaGetOption (bananaIndex,
	                                        "exclude_match",
	                                        s->screenNum);

	matchInit (&ns->exclude_match);
	matchAddFromString (&ns->exclude_match, option_exclude_match->s);
	matchUpdate (&ns->exclude_match);

	s->privates[nd->screenPrivateIndex].ptr = ns;

	return TRUE;
}

static void
negFiniScreen (CompPlugin *p,
               CompScreen *s)
{
	NEG_SCREEN (s);

	matchFini (&ns->neg_match);
	matchFini (&ns->exclude_match);

	freeWindowPrivateIndex (s, ns->windowPrivateIndex);

	UNWRAP (ns, s, drawWindowTexture);
	UNWRAP (ns, s, windowAddNotify);

	if (ns->negFunction)
		destroyFragmentFunction (s, ns->negFunction);

	if (ns->negAlphaFunction)
		destroyFragmentFunction (s, ns->negAlphaFunction);

	free (ns);
}

static Bool
negInitWindow (CompPlugin *p,
               CompWindow *w)
{
	NegWindow *nw;

	NEG_SCREEN (w->screen);

	nw = malloc (sizeof (NegWindow));
	if (!nw)
		return FALSE;

	nw->isNeg = FALSE;

	w->privates[ns->windowPrivateIndex].ptr = nw;

	return TRUE;
}

static void
negFiniWindow (CompPlugin *p,
               CompWindow *w)
{
	NEG_WINDOW (w);

	free (nw);
}

static Bool
negInit (CompPlugin * p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("neg", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("neg");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, negChangeNotify);

	return TRUE;
}

static void
negFini (CompPlugin * p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

CompPluginVTable negVTable = {
	"neg",
	negInit,
	negFini,
	negInitDisplay,
	negFiniDisplay,
	negInitScreen,
	negFiniScreen,
	negInitWindow,
	negFiniWindow
};

CompPluginVTable*
getCompPluginInfo20141205 (void)
{
	return &negVTable;
}


