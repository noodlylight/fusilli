/*
 * Copyright (c) 2006 Darryll Truchan <moppsy@comcast.net>
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

#include <compiz-core.h>
#include "neg_options.h"

static int displayPrivateIndex;
static int corePrivateIndex;

typedef struct _NEGCore {
    ObjectAddProc objectAdd;
} NEGCore;

typedef struct _NEGDisplay
{
    int screenPrivateIndex;
} NEGDisplay;


typedef struct _NEGSCreen
{
    int windowPrivateIndex;

    DrawWindowTextureProc drawWindowTexture;

    Bool isNeg; /* negative screen flag */

    int negFunction;
    int negAlphaFunction;
} NEGScreen;

typedef struct _NEGWindow
{
    Bool isNeg; /* negative window flag */
} NEGWindow;

#define GET_NEG_CORE(c) \
    ((NEGCore *) (c)->base.privates[corePrivateIndex].ptr)
#define NEG_CORE(c) \
    NEGCore *nc = GET_NEG_CORE (c)
#define GET_NEG_DISPLAY(d) \
    ((NEGDisplay *) (d)->base.privates[displayPrivateIndex].ptr)
#define NEG_DISPLAY(d) \
    NEGDisplay *nd = GET_NEG_DISPLAY (d)
#define GET_NEG_SCREEN(s, nd) \
    ((NEGScreen *) (s)->base.privates[(nd)->screenPrivateIndex].ptr)
#define NEG_SCREEN(s) \
    NEGScreen *ns = GET_NEG_SCREEN (s, GET_NEG_DISPLAY (s->display))
#define GET_NEG_WINDOW(w, ns) \
    ((NEGWindow *) (w)->base.privates[(ns)->windowPrivateIndex].ptr)
#define NEG_WINDOW(w) \
    NEGWindow *nw = GET_NEG_WINDOW  (w, \
		    GET_NEG_SCREEN  (w->screen, \
		    GET_NEG_DISPLAY (w->screen->display)))


static void
NEGToggle (CompWindow *w)
{
    NEG_WINDOW (w);

    /* toggle window negative flag */
    nw->isNeg = !nw->isNeg;

    /* check exclude list */
    if (matchEval (negGetExcludeMatch (w->screen), w))
	nw->isNeg = FALSE;

    /* cause repainting */
    addWindowDamage (w);
}

static void
NEGToggleScreen (CompScreen *s)
{
    CompWindow *w;

    NEG_SCREEN(s);

    /* toggle screen negative flag */
    ns->isNeg = !ns->isNeg;

    /* toggle every window */
    for (w = s->windows; w; w = w->next)
	if (w)
	    NEGToggle (w);
}

static Bool
negToggle (CompDisplay     *d,
	   CompAction      *action,
	   CompActionState state,
 	   CompOption      *option,
	   int             nOption)
{
    CompWindow *w;
    Window     xid;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    w = findWindowAtDisplay (d, xid);

    if (w)
	NEGToggle (w);

    return TRUE;
}

static Bool
negToggleAll (CompDisplay     *d,
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
	NEGToggleScreen (s);

    return TRUE;
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
NEGDrawWindowTexture (CompWindow           *w,
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
	    WRAP (ns, w->screen, drawWindowTexture, NEGDrawWindowTexture);
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

	    /* if we can addjust saturation, even if it's just on and off */
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
			              constant[3] * attrib->brightness /
				      65535.0f;

			glTexEnvfv(GL_TEXTURE_ENV,
				   GL_TEXTURE_ENV_COLOR, constant);

			glTexEnvf(GL_TEXTURE_ENV,
				  GL_TEXTURE_ENV_MODE, GL_COMBINE);

			glTexEnvf (GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
			glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE0_RGB, GL_PREVIOUS);
			glTexEnvf (GL_TEXTURE_ENV, GL_SOURCE1_RGB, GL_CONSTANT);
			glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND0_RGB,
				   GL_SRC_COLOR);
			glTexEnvf (GL_TEXTURE_ENV, GL_OPERAND1_RGB,
				   GL_SRC_COLOR);

			glTexEnvf (GL_TEXTURE_ENV,
				   GL_COMBINE_ALPHA, GL_MODULATE);
			glTexEnvf(GL_TEXTURE_ENV,
				  GL_SOURCE0_ALPHA, GL_PREVIOUS);
			glTexEnvf(GL_TEXTURE_ENV,
				  GL_SOURCE1_ALPHA, GL_CONSTANT);
			glTexEnvf(GL_TEXTURE_ENV,
				  GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
			glTexEnvf(GL_TEXTURE_ENV,
				  GL_OPERAND1_ALPHA, GL_SRC_ALPHA);

			/* draw the window geometry */
			(*w->drawWindowGeometry) (w);

			/* disable the current texture */
			disableTexture (w->screen, texture);

			/* set texture mode back to replace */
			glTexEnvi (GL_TEXTURE_ENV,
				   GL_TEXTURE_ENV_MODE, GL_REPLACE);

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

		    constant[0] =
			0.5f + 0.5f * RED_SATURATION_WEIGHT * constant[0];
		    constant[1] =
			0.5f + 0.5f * GREEN_SATURATION_WEIGHT * constant[1];
		    constant[2] =
			0.5f + 0.5f * BLUE_SATURATION_WEIGHT * constant[2];

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
	WRAP (ns, w->screen, drawWindowTexture, NEGDrawWindowTexture);
    }
}

static void
NEGWindowAdd (CompScreen *s,
	      CompWindow *w)
{
    NEG_SCREEN (s);

    /* nw->isNeg is initialized to FALSE in InitWindow, so we only
       have to toggle it to TRUE if necessary */
    if (ns->isNeg && matchEval (negGetNegMatch (s), w))
	NEGToggle (w);
}

static void
NEGScreenOptionChanged (CompScreen       *s,
			CompOption       *opt,
			NegScreenOptions num)
{
    switch (num)
    {
    case NegScreenOptionNegMatch:
    case NegScreenOptionExcludeMatch:
	{
	    CompWindow *w;
	    NEG_SCREEN (s);

	    for (w = s->windows; w; w = w->next)
	    {
		Bool isNeg;
		NEG_WINDOW (w);

		isNeg = matchEval (negGetNegMatch (s), w);
		isNeg = isNeg && !matchEval (negGetExcludeMatch (s), w);

		if (isNeg && ns->isNeg && !nw->isNeg)
		    NEGToggle (w);
		else if (!isNeg && nw->isNeg)
		    NEGToggle (w);
	    }
	}
	break;
    default:
	break;
    }
}

static void
NEGObjectAdd (CompObject *parent,
	      CompObject *object)
{
    static ObjectAddProc dispTab[] = {
	(ObjectAddProc) 0, /* CoreAdd */
        (ObjectAddProc) 0, /* DisplayAdd */
        (ObjectAddProc) 0, /* ScreenAdd */
        (ObjectAddProc) NEGWindowAdd
    };

    NEG_CORE (&core);

    UNWRAP (nc, &core, objectAdd);
    (*core.objectAdd) (parent, object);
    WRAP (nc, &core, objectAdd, NEGObjectAdd);

    DISPATCH (object, dispTab, ARRAY_SIZE (dispTab), (parent, object));
}

static Bool
NEGInitCore (CompPlugin *p,
  	     CompCore   *c)
{
    NEGCore *nc;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
        return FALSE;

    nc = malloc (sizeof (NEGCore));
    if (!nc)
        return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
        free (nc);
        return FALSE;
    }

    WRAP (nc, c, objectAdd, NEGObjectAdd);

    c->base.privates[corePrivateIndex].ptr = nc;

    return TRUE;
}

static void
NEGFiniCore (CompPlugin *p,
  	     CompCore   *c)
{
    NEG_CORE (c);

    freeDisplayPrivateIndex (displayPrivateIndex);

    UNWRAP (nc, c, objectAdd);

    free (nc);
}

static Bool
NEGInitDisplay (CompPlugin  *p,
		CompDisplay *d)
{
    NEGDisplay *nd;

    nd = malloc (sizeof (NEGDisplay));
    if (!nd)
	return FALSE;

    nd->screenPrivateIndex = allocateScreenPrivateIndex(d);
    if (nd->screenPrivateIndex < 0)
    {
	free (nd);
	return FALSE;
    }

    negSetWindowToggleKeyInitiate (d, negToggle);
    negSetScreenToggleKeyInitiate (d, negToggleAll);

    d->base.privates[displayPrivateIndex].ptr = nd;

    return TRUE;
}

static void
NEGFiniDisplay (CompPlugin  *p,
		CompDisplay *d)
{
    NEG_DISPLAY (d);

    freeScreenPrivateIndex (d, nd->screenPrivateIndex);

    free (nd);
}

static Bool
NEGInitScreen (CompPlugin *p,
	       CompScreen *s)
{
    NEGScreen *ns;

    NEG_DISPLAY (s->display);

    ns = malloc (sizeof (NEGScreen));
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

    negSetNegMatchNotify (s, NEGScreenOptionChanged);
    negSetExcludeMatchNotify (s, NEGScreenOptionChanged);

    /* wrap overloaded functions */
    WRAP (ns, s, drawWindowTexture, NEGDrawWindowTexture);

    s->base.privates[nd->screenPrivateIndex].ptr = ns;

    return TRUE;
}

static void
NEGFiniScreen (CompPlugin *p,
	       CompScreen *s)
{
    NEG_SCREEN (s);

    freeWindowPrivateIndex (s, ns->windowPrivateIndex);

    UNWRAP (ns, s, drawWindowTexture);

    if (ns->negFunction)
	destroyFragmentFunction (s, ns->negFunction);
    if (ns->negAlphaFunction)
	destroyFragmentFunction (s, ns->negAlphaFunction);

    free (ns);
}

static Bool
NEGInitWindow (CompPlugin *p,
	       CompWindow *w)
{
    NEGWindow *nw;

    NEG_SCREEN (w->screen);

    nw = malloc (sizeof (NEGWindow));
    if (!nw)
	return FALSE;

    nw->isNeg       = FALSE;

    w->base.privates[ns->windowPrivateIndex].ptr = nw;

    return TRUE;
}

static void
NEGFiniWindow (CompPlugin *p,
	       CompWindow *w)
{
    NEG_WINDOW (w);

    free (nw);
}

static Bool
NEGInit (CompPlugin * p)
{
    corePrivateIndex = allocateCorePrivateIndex ();
    if (corePrivateIndex < 0)
	return FALSE;

    return TRUE;
}

static void
NEGFini (CompPlugin * p)
{
    freeCorePrivateIndex (corePrivateIndex);
}

static CompBool
NEGInitObject (CompPlugin *p,
	       CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) NEGInitCore,
	(InitPluginObjectProc) NEGInitDisplay,
	(InitPluginObjectProc) NEGInitScreen,
	(InitPluginObjectProc) NEGInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
NEGFiniObject (CompPlugin *p,
	       CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) NEGFiniCore,
	(FiniPluginObjectProc) NEGFiniDisplay,
	(FiniPluginObjectProc) NEGFiniScreen,
	(FiniPluginObjectProc) NEGFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

CompPluginVTable NEGVTable = {
    "neg",
    0,
    NEGInit,
    NEGFini,
    NEGInitObject,
    NEGFiniObject,
    0,
    0,
};

CompPluginVTable*
getCompPluginInfo(void)
{
    return &NEGVTable;
}


