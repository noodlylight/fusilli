/**
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
 * Airplane added by : Carlo Palma
 * E-mail            : carlopalma@salug.it
 * Based on code originally written by Mark J. Kilgard
 *
 * Beam-Up added by : Florencio Guimaraes
 * E-mail           : florencio@nexcorp.com.br
 *
 * Fold and Skewer added by : Tomasz Kolodziejski
 * E-mail                   : tkolodziejski@gmail.com
 *
 * Hexagon tessellator added by : Mike Slegeir
 * E-mail                       : mikeslegeir@mail.utexas.edu>
 *
 * Particle system added by : (C) 2006 Dennis Kasprzyk
 * E-mail                   : onestone@beryl-project.org
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
 **/

/*
 * TODO:
 *
 * - Custom bounding box update function for Airplane
 *
 * - Auto direction option: Close in opposite direction of opening
 * - Proper side surface normals for lighting
 * - decoration shadows
 *   - shadow quad generation
 *   - shadow texture coords (from clip tex. matrices)
 *   - draw shadows
 *   - fade in shadows
 *
 * - Voronoi tessellation
 * - Brick tessellation
 * - Triangle tessellation
 * - Hexagonal tessellation
 *
 * Effects:
 * - Circular action for tornado type fx
 * - Tornado 3D (especially for minimize)
 * - Helix 3D (hor. strips descend while they rotate and fade in)
 * - Glass breaking 3D
 *   - Gaussian distr. points (for gradually increasing polygon size
 *                           starting from center or near mouse pointer)
 *   - Drawing cracks
 *   - Gradual cracking
 *
 * - fix slowness during transparent cube with <100 opacity
 * - fix occasional wrong side color in some windows
 * - fix on top windows and panels
 *   (These two only matter for viewing during Rotate Cube.
 *    All windows should be painted with depth test on
 *    like 3d-plugin does)
 * - play better with rotate (fix cube face drawn on top of polygons
 *   after 45 deg. rotation)
 *
 */

#include <GL/glu.h>
#include "animation-internal.h"


#define EXTENSION_INCREMENT 4

#define FAKE_ICON_SIZE 4

int animDisplayPrivateIndex;
int animFunctionsPrivateIndex;
CompMetadata animMetadata;

static int switcherPostWait = 0;


char *eventNames[AnimEventNum] =
{"Open", "Close", "Minimize", "Shade", "Focus"};

int chosenEffectOptionIds[AnimEventNum] =
{
    ANIM_SCREEN_OPTION_OPEN_EFFECTS,
    ANIM_SCREEN_OPTION_CLOSE_EFFECTS,
    ANIM_SCREEN_OPTION_MINIMIZE_EFFECTS,
    ANIM_SCREEN_OPTION_SHADE_EFFECTS,
    ANIM_SCREEN_OPTION_FOCUS_EFFECTS
};

int randomEffectOptionIds[AnimEventNum] =
{
    ANIM_SCREEN_OPTION_OPEN_RANDOM_EFFECTS,
    ANIM_SCREEN_OPTION_CLOSE_RANDOM_EFFECTS,
    ANIM_SCREEN_OPTION_MINIMIZE_RANDOM_EFFECTS,
    ANIM_SCREEN_OPTION_SHADE_RANDOM_EFFECTS,
    -1
};

int customOptionOptionIds[AnimEventNum] =
{
    ANIM_SCREEN_OPTION_OPEN_OPTIONS,
    ANIM_SCREEN_OPTION_CLOSE_OPTIONS,
    ANIM_SCREEN_OPTION_MINIMIZE_OPTIONS,
    ANIM_SCREEN_OPTION_SHADE_OPTIONS,
    ANIM_SCREEN_OPTION_FOCUS_OPTIONS
};

int matchOptionIds[AnimEventNum] =
{
    ANIM_SCREEN_OPTION_OPEN_MATCHES,
    ANIM_SCREEN_OPTION_CLOSE_MATCHES,
    ANIM_SCREEN_OPTION_MINIMIZE_MATCHES,
    ANIM_SCREEN_OPTION_SHADE_MATCHES,
    ANIM_SCREEN_OPTION_FOCUS_MATCHES
};

int durationOptionIds[AnimEventNum] =
{
    ANIM_SCREEN_OPTION_OPEN_DURATIONS,
    ANIM_SCREEN_OPTION_CLOSE_DURATIONS,
    ANIM_SCREEN_OPTION_MINIMIZE_DURATIONS,
    ANIM_SCREEN_OPTION_SHADE_DURATIONS,
    ANIM_SCREEN_OPTION_FOCUS_DURATIONS
};


// Bind each effect in the list of chosen effects for every event, to the
// corresponding animation effect (i.e. effect with that name) if it is
// provided by a plugin, otherwise set it to None.
static void
updateEventEffects (CompScreen *s,
		    AnimEvent e,
		    Bool forRandom)
{
    ANIM_SCREEN (s);

    CompListValue *listVal;
    EffectSet *effectSet;
    if (forRandom)
    {
	listVal = &as->opt[randomEffectOptionIds[e]].value.list;
	effectSet = &as->randomEffects[e];
    }
    else
    {
	listVal = &as->opt[chosenEffectOptionIds[e]].value.list;
	effectSet = &as->eventEffects[e];
    }
    int n = listVal->nValue;
    
    if (effectSet->effects)
	free (effectSet->effects);
    effectSet->effects = calloc (n, sizeof (AnimEffect));
    if (!effectSet->effects)
    {
	compLogMessage ("animation", CompLogLevelError,
			"Not enough memory");
	return;
    }
    effectSet->n = n;

    int nEventEffectsAllowed = as->nEventEffectsAllowed[e];
    const AnimEffect *eventEffectsAllowed = as->eventEffectsAllowed[e];

    int r;
    for (r = 0; r < n; r++) // for each row
    {
	const char *animName = listVal->value[r].s;
	
	// Find the animation effect with matching name
	effectSet->effects[r] = AnimEffectNone;
	int i;
	for (i = 0; i < nEventEffectsAllowed; i++)
	{
	    if (0 == strcasecmp (animName, eventEffectsAllowed[i]->name))
	    {
		effectSet->effects[r] = eventEffectsAllowed[i];
		break;
	    }
	}
    }
}

static void
updateAllEventEffects (CompScreen *s)
{
    AnimEvent e;
    for (e = 0; e < AnimEventNum; e++) // for each anim event
	updateEventEffects (s, e, FALSE);
    for (e = 0; e < AnimEventNum - 1; e++) // for each anim event except focus
	updateEventEffects (s, e, TRUE);
}

// Free everything related to effects
static void
freeAllEffects (AnimScreen *as)
{
    AnimEvent e;
    for (e = 0; e < AnimEventNum; e++)
    {
	if (as->randomEffects[e].effects)
	    free (as->randomEffects[e].effects);
	if (as->eventEffectsAllowed[e])
	    free (as->eventEffectsAllowed[e]);
	if (as->eventEffects[e].n > 0 && as->eventEffects[e].effects)
	    free (as->eventEffects[e].effects);
    }
}

// Extension functions

static void
animAddExtension (CompScreen *s,
		  ExtensionPluginInfo *extensionPluginInfo)
{
    ANIM_SCREEN (s);

    // Make sure there is enough space for extension plugins
    if (as->nExtensionPlugins == as->maxExtensionPlugins)
    {
	ExtensionPluginInfo **newExtensionPlugins =
	    realloc (as->extensionPlugins,
		     (as->maxExtensionPlugins + EXTENSION_INCREMENT) *
		     sizeof (ExtensionPluginInfo *));
	if (!newExtensionPlugins)
	{
	    compLogMessage ("animation", CompLogLevelError,
			    "Not enough memory");
	    return;
	}
	as->extensionPlugins = newExtensionPlugins;
	as->maxExtensionPlugins += EXTENSION_INCREMENT;
    }

    as->extensionPlugins[as->nExtensionPlugins] = extensionPluginInfo;
    as->nExtensionPlugins++;

    unsigned int nPluginEffects = extensionPluginInfo->nEffects;

    // Make sure there is enough space for event effects
    AnimEvent e;
    for (e = 0; e < AnimEventNum; e++) // for each anim event
    {
	if (as->maxEventEffectsAllowed[e] <
	    as->nEventEffectsAllowed[e] + nPluginEffects)
	{
	    int newNum = as->nEventEffectsAllowed[e] + nPluginEffects;
	    AnimEffect *newEventEfffects =
		realloc (as->eventEffectsAllowed[e],
			 newNum * sizeof (AnimEffect));
	    if (!newEventEfffects)
	    {
		compLogMessage ("animation", CompLogLevelError,
				"Not enough memory");
		return;
	    }
	    as->eventEffectsAllowed[e] = newEventEfffects;
	    as->maxEventEffectsAllowed[e] = newNum;
	}
    }

    Bool eventEffectsNeedUpdate[AnimEventNum] =
	{FALSE, FALSE, FALSE, FALSE, FALSE};

    // Put this plugin's effects into as->eventEffects and
    // as->eventEffectsAllowed
    int j;
    for (j = 0; j < nPluginEffects; j++)
    {
	const AnimEffect effect = extensionPluginInfo->effects[j];

	// Update allowed effects for each event
	for (e = 0; e < AnimEventNum; e++)
	{
	    if (effect->usedForEvents[e])
	    {
		as->eventEffectsAllowed[e][as->nEventEffectsAllowed[e]++] =
		    effect;
		eventEffectsNeedUpdate[e] = TRUE;
	    }
	}
    }
    for (e = 0; e < AnimEventNum; e++)
	if (eventEffectsNeedUpdate[e])
	{
	    updateEventEffects (s, e, FALSE);
	    if (e != AnimEventFocus)
		updateEventEffects (s, e, TRUE);
	}
}

static void
animRemoveExtension (CompScreen *s,
		     ExtensionPluginInfo *extensionPluginInfo)
{
    ANIM_SCREEN (s);
    char *pluginName = NULL;
    int pluginNameLen = 0;
    
    if (extensionPluginInfo->nEffects > 0)
    {
	pluginName = extensionPluginInfo->effects[0]->name;
	pluginNameLen = strchr (pluginName, ':') - pluginName;
    }

    // Stop all ongoing animations
    CompWindow *w;
    for (w = s->windows; w; w = w->next)
    {
	ANIM_WINDOW (w);
	if (aw->com.curAnimEffect != AnimEffectNone)
	    postAnimationCleanup (w);
    }

    int p;
    for (p = 0; p < as->nExtensionPlugins; p++)
    {
	// Find the matching one
	if (as->extensionPlugins[p] == extensionPluginInfo)
	    break;
    }
    if (p == as->nExtensionPlugins)
	return; // couldn't find that extension plugin

    // Remove extensionPlugins[p] (shift following plugins)
    as->nExtensionPlugins--;
    if (as->nExtensionPlugins > 0)
	memmove (&as->extensionPlugins[p],
		 &as->extensionPlugins[p + 1],
		 (as->nExtensionPlugins - p) *
		 sizeof (ExtensionPluginInfo *));

    AnimEvent e;
    for (e = 0; e < AnimEventNum; e++)
    {
	AnimEffect *eventEffectsAllowed = as->eventEffectsAllowed[e];
	int n = as->nEventEffectsAllowed[e];

	// nUpto: number of event effects upto the removed plugin
	int nUpto;
	for (nUpto = 0; nUpto < n; nUpto++)
	{
	    // if plugin name matches
	    if (0 == strncmp (pluginName, eventEffectsAllowed[nUpto]->name,
			      pluginNameLen))
		break;
	}
	// nUptoNext: number of event effects upto the next plugin
	int nUptoNext;
	for (nUptoNext = nUpto; nUptoNext < n; nUptoNext++)
	{
	    // if plugin name doesn't match
	    if (0 != strncmp (pluginName, eventEffectsAllowed[nUpto]->name,
			      pluginNameLen))
		break;
	}
	if (nUpto < nUptoNext)
	{
	    // Remove event effects for plugin p (Shift following effects)
	    if (nUptoNext < n)
		memmove (&eventEffectsAllowed[nUpto],
			 &eventEffectsAllowed[nUptoNext],
			 (nUptoNext - nUpto) * sizeof (AnimEffect));
	    as->nEventEffectsAllowed[e] -= nUptoNext - nUpto;

	    // Update event effects to complete removal
	    updateEventEffects (s, e, FALSE);
	    if (e != AnimEventFocus)
		updateEventEffects (s, e, TRUE);
	}
    }
}

// End of extension functions


Bool
defaultAnimInit (CompWindow * w)
{
    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    // store window opacity
    aw->com.storedOpacity = w->paint.opacity;

    aw->com.timestep =
    	(w->screen->slowAnimations ? 2 : // For smooth slow-mo (refer to display.c)
	 as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

    return TRUE;
}

Bool
animZoomToIcon (CompWindow *w)
{
    ANIM_WINDOW(w);

    if (aw->com.curAnimEffect->properties.zoomToIconFunc)
	return aw->com.curAnimEffect->properties.zoomToIconFunc (w);

    return FALSE;
}

static Bool
defaultMinimizeAnimInit (CompWindow * w)
{
    ANIM_WINDOW(w);

    if (animZoomToIcon (w))
    {
	aw->com.animTotalTime /= ZOOM_PERCEIVED_T;
	aw->com.animRemainingTime = aw->com.animTotalTime;
	aw->com.usingTransform = TRUE;
    }
    return defaultAnimInit (w);
}

static Bool
animWithTransformInit (CompWindow * w)
{
    ANIM_WINDOW(w);

    aw->com.usingTransform = TRUE;

    return defaultMinimizeAnimInit (w);
}

static inline Bool
returnTrue (CompWindow *w)
{
    return TRUE;
}

// Assumes events in the metadata are in
// [Open, Close, Minimize, Focus, Shade] order
// and effects among those are in alphabetical order
// but with "(Event) None" first and "(Event) Random" last.
static AnimEffect
getMatchingAnimSelection (CompWindow *w,
			  AnimEvent e,
			  int *duration)
{
    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    EffectSet *eventEffects;
    CompOptionValue *valMatch;
    CompOptionValue *valDuration;
    CompOptionValue *valCustomOptions;

    eventEffects = &as->eventEffects[e];
    valMatch = &as->opt[matchOptionIds[e]].value;
    valDuration = &as->opt[durationOptionIds[e]].value;
    valCustomOptions = &as->opt[customOptionOptionIds[e]].value;

    int nRows = valMatch->list.nValue;
    if (nRows != eventEffects->n ||
	nRows != valDuration->list.nValue ||
	nRows != valCustomOptions->list.nValue)
    {
	compLogMessage ("animation", CompLogLevelError,
			"Animation settings mismatch in \"Animation "
			"Selection\" list for %s event.", eventNames[e]);
	return AnimEffectNone;
    }

    // Find the first row that matches this window for this event
    int i;
    for (i = 0; i < nRows; i++)
    {
	if (!matchEval (&valMatch->list.value[i].match, w))
	    continue;

	aw->prevAnimSelectionRow = aw->curAnimSelectionRow;
	aw->curAnimSelectionRow = i;

	if (duration)
	    *duration = valDuration->list.value[i].i;

	return eventEffects->effects[i];
    }

    return AnimEffectNone;
}

static inline AnimEffect
animGetAnimEffect (AnimScreen *as,
		   AnimEffect effect,
		   AnimEvent animEvent)
{
    Bool allRandom = as->opt[ANIM_SCREEN_OPTION_ALL_RANDOM].value.b;
    AnimEffect *randomEffects = as->randomEffects[animEvent].effects;
    unsigned int nRandomEffects = as->randomEffects[animEvent].n;

    if ((effect == AnimEffectRandom) || allRandom)
    {
	if (nRandomEffects == 0) // no random animation selected, assume "all"
	{
	    // exclude None and Random
	    randomEffects = as->eventEffectsAllowed[animEvent] + 2;
	    nRandomEffects = as->nEventEffectsAllowed[animEvent] - 2;
	}
	unsigned int index;
	index = (unsigned int)(nRandomEffects * (double)rand() / RAND_MAX);
	return randomEffects[index];
    }
    else
	return effect;
}

// Converts animation direction string to an integer direction
// (up, down, left, or right)
AnimDirection getActualAnimDirection (CompWindow * w,
				      AnimDirection dir,
				      Bool openDir)
{
    ANIM_WINDOW(w);

    if (dir == AnimDirectionRandom)
    {
	dir = rand() % 4;
    }
    else if (dir == AnimDirectionAuto)
    {
	// away from icon
	int centerX = BORDER_X(w) + BORDER_W(w) / 2;
	int centerY = BORDER_Y(w) + BORDER_H(w) / 2;
	float relDiffX = ((float)centerX - aw->com.icon.x) / BORDER_W(w);
	float relDiffY = ((float)centerY - aw->com.icon.y) / BORDER_H(w);

	if (openDir)
	{
	    if (aw->com.curWindowEvent == WindowEventMinimize ||
		aw->com.curWindowEvent == WindowEventUnminimize)
		// min/unmin. should always result in +/- y direction
		dir = aw->com.icon.y < w->screen->height - aw->com.icon.y ?
		    AnimDirectionDown : AnimDirectionUp;
	    else if (fabs(relDiffY) > fabs(relDiffX))
		dir = relDiffY > 0 ? AnimDirectionDown : AnimDirectionUp;
	    else
		dir = relDiffX > 0 ? AnimDirectionRight : AnimDirectionLeft;
	}
	else
	{
	    if (aw->com.curWindowEvent == WindowEventMinimize ||
		aw->com.curWindowEvent == WindowEventUnminimize)
		// min/unmin. should always result in +/- y direction
		dir = aw->com.icon.y < w->screen->height - aw->com.icon.y ?
		    AnimDirectionUp : AnimDirectionDown;
	    else if (fabs(relDiffY) > fabs(relDiffX))
		dir = relDiffY > 0 ? AnimDirectionUp : AnimDirectionDown;
	    else
		dir = relDiffX > 0 ? AnimDirectionLeft : AnimDirectionRight;
	}
    }
    return dir;
}

float
defaultAnimProgress (CompWindow *w)
{
    ANIM_WINDOW (w);

    float forwardProgress =
	1 - aw->com.animRemainingTime / (aw->com.animTotalTime - aw->com.timestep);
    forwardProgress = MIN(forwardProgress, 1);
    forwardProgress = MAX(forwardProgress, 0);

    if (aw->com.curWindowEvent == WindowEventOpen ||
	aw->com.curWindowEvent == WindowEventUnminimize ||
	aw->com.curWindowEvent == WindowEventUnshade ||
	aw->com.curWindowEvent == WindowEventFocus)
	forwardProgress = 1 - forwardProgress;

    return forwardProgress;
}

float
sigmoidAnimProgress (CompWindow *w)
{
    ANIM_WINDOW (w);

    float forwardProgress =
	1 - aw->com.animRemainingTime / (aw->com.animTotalTime - aw->com.timestep);
    forwardProgress = MIN(forwardProgress, 1);
    forwardProgress = MAX(forwardProgress, 0);

    // Apply sigmoid and normalize
    forwardProgress =
	(sigmoid(forwardProgress) - sigmoid(0)) /
	(sigmoid(1) - sigmoid(0));

    if (aw->com.curWindowEvent == WindowEventOpen ||
	aw->com.curWindowEvent == WindowEventUnminimize ||
	aw->com.curWindowEvent == WindowEventUnshade ||
	aw->com.curWindowEvent == WindowEventFocus)
	forwardProgress = 1 - forwardProgress;

    return forwardProgress;
}

// Gives some acceleration (when closing a window)
// or deceleration (when opening a window)
// Applies a sigmoid with slope s,
// where minx and maxx are the
// starting and ending points on the sigmoid
float decelerateProgressCustom(float progress, float minx, float maxx)
{
    float x = 1 - progress;
    float s = 8;

    return (1 -
	    ((sigmoid2(minx + (x * (maxx - minx)), s) - sigmoid2(minx, s)) /
	     (sigmoid2(maxx, s) - sigmoid2(minx, s))));
}

float decelerateProgress(float progress)
{
    return decelerateProgressCustom(progress, 0.5, 0.75);
}

float
getProgressAndCenter (CompWindow *w,
		      Point *center)
{
    float forwardProgress = 0;

    ANIM_WINDOW (w);

    if (center)
	center->x = WIN_X (w) + WIN_W (w) / 2.0;

    if (animZoomToIcon (w))
    {
	float dummy;
	fxZoomAnimProgress (w, &forwardProgress, &dummy, TRUE);

	if (center)
	    getZoomCenterScale (w, center, NULL);
    }
    else
    {
	forwardProgress = defaultAnimProgress (w);

	if (center)
	{
	    if (aw->com.curWindowEvent == WindowEventShade ||
		aw->com.curWindowEvent == WindowEventUnshade)
	    {
		float origCenterY = WIN_Y (w) + WIN_H (w) / 2.0;
		center->y =
		    (1 - forwardProgress) * origCenterY +
		    forwardProgress * (WIN_Y (w) + aw->com.model->topHeight);
	    }
	    else // i.e. (un)minimizing without zooming
	    {
		    center->y = WIN_Y (w) + WIN_H (w) / 2.0;
	    }
	}
    }
    return forwardProgress;
}

void
defaultAnimStep (CompWindow *w, float time)
{
    int steps;

    ANIM_SCREEN (w->screen);
    ANIM_WINDOW (w);

    float timestep =
    	(w->screen->slowAnimations ? 2 : // For smooth slow-mo (refer to display.c)
	 as->opt[ANIM_SCREEN_OPTION_TIME_STEP].value.i);

    aw->com.timestep = timestep;

    aw->remainderSteps += time / timestep;
    steps = floor(aw->remainderSteps);
    aw->remainderSteps -= steps;

    steps = MAX(1, steps);

    aw->com.animRemainingTime -= timestep * steps;

    // avoid sub-zero values
    aw->com.animRemainingTime = MAX(aw->com.animRemainingTime, 0);

    matrixGetIdentity (&aw->com.transform);
    if (animZoomToIcon (w))
    {
	applyZoomTransform (w);
    }
}

void
defaultUpdateWindowTransform (CompWindow *w,
			      CompTransform *wTransform)
{
    ANIM_WINDOW(w);

    if (aw->com.usingTransform)
    {
	if (aw->com.curAnimEffect->properties.modelAnimIs3D)
	{
	    // center for perspective correction
	    Point center;
	    getProgressAndCenter (w, &center);
	
	    ANIM_SCREEN (w->screen);
	    CompTransform skewTransform;
	    matrixGetIdentity (&skewTransform);
	    applyPerspectiveSkew (as->output, &skewTransform, &center);
	    applyTransform (wTransform, &aw->com.transform);
	    applyTransform (wTransform, &skewTransform);
	}
	else
	{
	    applyTransform (wTransform, &aw->com.transform);
	}
    }
}

// Apply transform to wTransform
inline void
applyTransform (CompTransform *wTransform,
		CompTransform *transform)
{
    matrixMultiply (wTransform, wTransform, transform);
}

static void
copyResetBB (AnimWindow *aw)
{
    memcpy (&aw->lastBB, &aw->BB, sizeof (Box));
    aw->BB.x1 = aw->BB.y1 = MAXSHORT;
    aw->BB.x2 = aw->BB.y2 = MINSHORT;
}

void
expandBoxWithBox (Box *target, Box *source)
{
    if (source->x1 < target->x1)
	target->x1 = source->x1;
    if (source->x2 > target->x2)
	target->x2 = source->x2;
    if (source->y1 < target->y1)
	target->y1 = source->y1;
    if (source->y2 > target->y2)
	target->y2 = source->y2;
}

void
expandBoxWithPoint (Box *target, float fx, float fy)
{
    short x = MAX (MIN (fx, MAXSHORT - 1), MINSHORT);
    short y = MAX (MIN (fy, MAXSHORT - 1), MINSHORT);

    if (target->x1 == MAXSHORT)
    {
	target->x1 = x;
	target->y1 = y;
	target->x2 = x + 1;
	target->y2 = y + 1;
	return;
    }
    if (x < target->x1)
	target->x1 = x;
    else if (x > target->x2)
	target->x2 = x;

    if (y < target->y1)
	target->y1 = y;
    else if (y > target->y2)
	target->y2 = y;
}

// This will work for zoom-like 2D transforms,
// but not for glide-like 3D transforms.
static void
expandBoxWithPoint2DTransform (CompScreen *s,
			       Box *target,
			       CompVector *coords,
			       CompTransform *transformMat)
{
    CompVector coordsTransformed;
    
    matrixMultiplyVector (&coordsTransformed, coords, transformMat);
    expandBoxWithPoint (target, coordsTransformed.x, coordsTransformed.y);
}

// Either points or objects should be non-NULL.
static Bool
expandBoxWithPoints3DTransform (CompOutput          *output,
				CompScreen          *s,
				const CompTransform *transform,
				Box                 *targetBox,
				const float         *points,
				Object              *objects,
				int                 nPoints)
{
    GLdouble dModel[16];
    GLdouble dProjection[16];
    GLdouble x, y, z;
    int i;
    for (i = 0; i < 16; i++)
    {
	dModel[i] = transform->m[i];
	dProjection[i] = s->projection[i];
    }
    GLint viewport[4] =
	{output->region.extents.x1,
	 output->region.extents.y1,
	 output->width,
	 output->height};

    if (points) // use points
    {
	for (; nPoints; nPoints--, points += 3)
	{
	    if (!gluProject (points[0], points[1], points[2],
			     dModel, dProjection, viewport,
			     &x, &y, &z))
		return FALSE;
    
	    expandBoxWithPoint (targetBox, x + 0.5, (s->height - y) + 0.5);
	}
    }
    else // use objects
    {
	Object *object = objects;
	for (; nPoints; nPoints--, object++)
	{
	    if (!gluProject (object->position.x,
			     object->position.y,
			     object->position.z,
			     dModel, dProjection, viewport,
			     &x, &y, &z))
		return FALSE;

	    expandBoxWithPoint (targetBox, x + 0.5, (s->height - y) + 0.5);
	}
    }
    return TRUE;
}

static void
modelUpdateBB (CompOutput *output,
	       CompWindow * w,
	       Box *BB)
{
    int i;

    ANIM_WINDOW (w);

    Model *model = aw->com.model;
    if (!model)
	return;

    Object *object = model->objects;

    if (aw->com.usingTransform)
    {
	if (aw->com.curAnimEffect->properties.modelAnimIs3D)
	{
	    CompTransform wTransform;

	    // center for perspective correction
	    Point center;
	    getProgressAndCenter (w, &center);

	    CompTransform fullTransform;
	    memcpy (fullTransform.m, aw->com.transform.m, sizeof (float) * 16);
	    applyPerspectiveSkew (output, &fullTransform, &center);

	    prepareTransform (w->screen, output, &wTransform, &fullTransform);

	    expandBoxWithPoints3DTransform (output,
					    w->screen,
					    &wTransform,
					    BB,
					    NULL,
					    model->objects,
					    model->numObjects);
	}
	else
	{
	    Object *object = model->objects;
	    for (i = 0; i < model->numObjects; i++, object++)
	    {
		CompVector coords;
    
		coords.x = object->position.x;
		coords.y = object->position.y;
		coords.z = 0;
		coords.w = 1;

		expandBoxWithPoint2DTransform (w->screen,
					       BB,
					       &coords,
					       &aw->com.transform);
	    }
  	}
    }
    else
    {
	for (i = 0; i < model->numObjects; i++, object++)
	{
	    expandBoxWithPoint (BB,
				object->position.x + 0.5,
				object->position.y + 0.5);
	}
    }
}

void
updateBBWindow (CompOutput *output,
		CompWindow * w,
		Box *BB)
{
    Box windowBox = {WIN_X(w), WIN_X(w) + WIN_W(w),
		     WIN_Y(w), WIN_Y(w) + WIN_H(w)};
    expandBoxWithBox (BB, &windowBox);
}

void
updateBBScreen (CompOutput *output,
		CompWindow * w,
		Box *BB)
{
    Box screenBox = {0, w->screen->width,
		     0, w->screen->height};
    expandBoxWithBox (BB, &screenBox);
}

void
prepareTransform (CompScreen *s,
		  CompOutput *output,
		  CompTransform *resultTransform,
		  CompTransform *transform)
{
    CompTransform sTransform;

    matrixGetIdentity (&sTransform);
    transformToScreenSpace (s, output,
			    -DEFAULT_Z_CAMERA, &sTransform);

    matrixMultiply (resultTransform, &sTransform, transform);
}

void
compTransformUpdateBB (CompOutput *output,
		       CompWindow *w,
		       Box *BB)
{
    ANIM_WINDOW(w);
    CompScreen *s = w->screen;
    CompTransform wTransform;

    prepareTransform (s, output, &wTransform, &aw->com.transform);

    float corners[4*3] = {WIN_X(w), WIN_Y(w), 0,
			  WIN_X(w) + WIN_W(w), WIN_Y(w), 0,
			  WIN_X(w), WIN_Y(w) + WIN_H(w), 0,
			  WIN_X(w) + WIN_W(w), WIN_Y(w) + WIN_H(w), 0};

    expandBoxWithPoints3DTransform (output,
				    s,
				    &wTransform,
				    BB,
				    corners,
				    NULL,
				    4);
}

// Damage the union of window's bounding box
// before and after animStepFunc does its job
static void
damageBoundingBox (CompWindow * w)
{
    ANIM_WINDOW(w);

    if (aw->BB.x1 == MAXSHORT) // unintialized BB
	return;

    // Find union of BB and lastBB
    Region regionToDamage = XCreateRegion();
    if (!regionToDamage)
	return;

    XRectangle rect;

    BoxPtr BB = &aw->BB;
    BoxPtr lastBB = &aw->lastBB;

    // Have a 1 pixel margin to prevent occasional 1 pixel line artifact
    rect.x = BB->x1 - 1;
    rect.y = BB->y1 - 1;
    rect.width  = BB->x2 - BB->x1 + 2;
    rect.height = BB->y2 - BB->y1 + 2;
    XUnionRectWithRegion (&rect, &emptyRegion, regionToDamage);

    rect.x = lastBB->x1 - 1;
    rect.y = lastBB->y1 - 1;
    rect.width  = lastBB->x2 - lastBB->x1 + 2;
    rect.height = lastBB->y2 - lastBB->y1 + 2;
    XUnionRectWithRegion (&rect, regionToDamage, regionToDamage);

    damageScreenRegion (w->screen, regionToDamage);

    XDestroyRegion (regionToDamage);
}

Bool getMousePointerXY(CompScreen * s, short *x, short *y)
{
    Window w1, w2;
    int xp, yp, xj, yj;
    unsigned int m;

    if (XQueryPointer
	(s->display->display, s->root, &w1, &w2, &xj, &yj, &xp, &yp, &m))
    {
	*x = xp;
	*y = yp;
	return TRUE;
    }
    return FALSE;
}

static int animGetWindowState(CompWindow * w)
{
    Atom actual;
    int result, format;
    unsigned long n, left;
    unsigned char *data;
    int retval = WithdrawnState;

    result = XGetWindowProperty(w->screen->display->display, w->id,
				w->screen->display->wmStateAtom, 0L,
				1L, FALSE,
				w->screen->display->wmStateAtom,
				&actual, &format, &n, &left, &data);

    if (result == Success && data)
    {
	if (n)
    	    memcpy(&retval, data, sizeof(int));

	XFree((void *)data);
    }

    return retval;
}

static Bool
animSetScreenOptions(CompPlugin *plugin,
		     CompScreen * screen,
		     const char *name,
		     CompOptionValue * value)
{
    CompOption *o;
    int index;

    ANIM_SCREEN(screen);

    o = compFindOption(as->opt, NUM_OPTIONS(as), name, &index);
    if (!o)
	return FALSE;

    switch (index)
    {
    case ANIM_SCREEN_OPTION_OPEN_MATCHES:
    case ANIM_SCREEN_OPTION_CLOSE_MATCHES:
    case ANIM_SCREEN_OPTION_MINIMIZE_MATCHES:
    case ANIM_SCREEN_OPTION_SHADE_MATCHES:
    case ANIM_SCREEN_OPTION_FOCUS_MATCHES:
	if (compSetOptionList(o, value))
	{
	    int i;
	    for (i = 0; i < o->value.list.nValue; i++)
		matchUpdate (screen->display, &o->value.list.value[i].match);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_OPEN_OPTIONS:
	if (compSetOptionList(o, value))
	{
	    updateOptionSets (screen, AnimEventOpen);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_CLOSE_OPTIONS:
	if (compSetOptionList(o, value))
	{
	    updateOptionSets (screen, AnimEventClose);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_MINIMIZE_OPTIONS:
	if (compSetOptionList(o, value))
	{
	    updateOptionSets (screen, AnimEventMinimize);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_SHADE_OPTIONS:
	if (compSetOptionList(o, value))
	{
	    updateOptionSets (screen, AnimEventShade);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_FOCUS_OPTIONS:
	if (compSetOptionList(o, value))
	{
	    updateOptionSets (screen, AnimEventFocus);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_OPEN_EFFECTS:
	if (compSetOptionList(o, value))
	{
	    updateEventEffects (screen, AnimEventOpen, FALSE);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_CLOSE_EFFECTS:
	if (compSetOptionList(o, value))
	{
	    updateEventEffects (screen, AnimEventClose, FALSE);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_MINIMIZE_EFFECTS:
	if (compSetOptionList(o, value))
	{
	    updateEventEffects (screen, AnimEventMinimize, FALSE);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_SHADE_EFFECTS:
	if (compSetOptionList(o, value))
	{
	    updateEventEffects (screen, AnimEventShade, FALSE);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_FOCUS_EFFECTS:
	if (compSetOptionList(o, value))
	{
	    updateEventEffects (screen, AnimEventFocus, FALSE);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_OPEN_RANDOM_EFFECTS:
	if (compSetOptionList(o, value))
	{
	    updateEventEffects (screen, AnimEventOpen, TRUE);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_CLOSE_RANDOM_EFFECTS:
	if (compSetOptionList(o, value))
	{
	    updateEventEffects (screen, AnimEventClose, TRUE);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_MINIMIZE_RANDOM_EFFECTS:
	if (compSetOptionList(o, value))
	{
	    updateEventEffects (screen, AnimEventMinimize, TRUE);
	    return TRUE;
	}
	break;
    case ANIM_SCREEN_OPTION_SHADE_RANDOM_EFFECTS:
	if (compSetOptionList(o, value))
	{
	    updateEventEffects (screen, AnimEventShade, TRUE);
	    return TRUE;
	}
	break;
    default:
	return compSetScreenOption (screen, o, value);
	break;
    }

    return FALSE;
}

static const CompMetadataOptionInfo animScreenOptionInfo[] = {
    // Event settings
    { "open_effects", "list", "<type>string</type>", 0, 0 },
    { "open_durations", "list", "<type>int</type><min>50</min>", 0, 0 },
    { "open_matches", "list", "<type>match</type>", 0, 0 },
    { "open_options", "list", "<type>string</type>", 0, 0 },
    { "open_random_effects", "list", "<type>string</type>", 0, 0 },
    { "close_effects", "list", "<type>string</type>", 0, 0 },
    { "close_durations", "list", "<type>int</type><min>50</min>", 0, 0 },
    { "close_matches", "list", "<type>match</type>", 0, 0 },
    { "close_options", "list", "<type>string</type>", 0, 0 },
    { "close_random_effects", "list", "<type>string</type>", 0, 0 },
    { "minimize_effects", "list", "<type>string</type>", 0, 0 },
    { "minimize_durations", "list", "<type>int</type><min>50</min>", 0, 0 },
    { "minimize_matches", "list", "<type>match</type>", 0, 0 },
    { "minimize_options", "list", "<type>string</type>", 0, 0 },
    { "minimize_random_effects", "list", "<type>string</type>", 0, 0 },
    { "shade_effects", "list", "<type>string</type>", 0, 0 },
    { "shade_durations", "list", "<type>int</type><min>50</min>", 0, 0 },
    { "shade_matches", "list", "<type>match</type>", 0, 0 },
    { "shade_options", "list", "<type>string</type>", 0, 0 },
    { "shade_random_effects", "list", "<type>string</type>", 0, 0 },
    { "focus_effects", "list", "<type>string</type>", 0, 0 },
    { "focus_durations", "list", "<type>int</type><min>50</min>", 0, 0 },
    { "focus_matches", "list", "<type>match</type>", 0, 0 },
    { "focus_options", "list", "<type>string</type>", 0, 0 },
    // Misc. settings
    { "all_random", "bool", 0, 0, 0 },
    { "time_step", "int", "<min>1</min>", 0, 0 },
    // Effect settings
    { "curved_fold_amp_mult", "float", "<min>-1.5</min><max>2.0</max>", 0, 0 },
    { "curved_fold_zoom_to_taskbar", "bool", 0, 0, 0 },
    { "dodge_gap_ratio", "float", "<min>0.0</min><max>1.0</max>", 0, 0 },
    { "dream_zoom_to_taskbar", "bool", 0, 0, 0 },
    { "glide1_away_position", "float", 0, 0, 0 },
    { "glide1_away_angle", "float", 0, 0, 0 },
    { "glide1_zoom_to_taskbar", "bool", 0, 0, 0 },
    { "glide2_away_position", "float", 0, 0, 0 },
    { "glide2_away_angle", "float", 0, 0, 0 },
    { "glide2_zoom_to_taskbar", "bool", 0, 0, 0 },
    { "horizontal_folds_amp_mult", "float", "<min>-1.0</min><max>3.0</max>", 0, 0 },
    { "horizontal_folds_num_folds", "int", "<min>1</min>", 0, 0 },
    { "horizontal_folds_zoom_to_taskbar", "bool", 0, 0, 0 },
    { "magic_lamp_moving_end", "bool", 0, 0, 0 },
    { "magic_lamp_grid_res", "int", "<min>4</min>", 0, 0 },
    { "magic_lamp_max_waves", "int", "<min>3</min>", 0, 0 },
    { "magic_lamp_amp_min", "float", "<min>200</min>", 0, 0 },
    { "magic_lamp_amp_max", "float", "<min>200</min>", 0, 0 },
    { "magic_lamp_open_start_width", "int", "<min>0</min>", 0, 0 },
    { "rollup_fixed_interior", "bool", 0, 0, 0 },
    { "sidekick_num_rotations", "float", "<min>0</min>", 0, 0 },
    { "sidekick_springiness", "float", "<min>0</min><max>1</max>", 0, 0 },
    { "sidekick_zoom_from_center", "int", RESTOSTRING (0, LAST_ZOOM_FROM_CENTER), 0, 0 },
    { "vacuum_moving_end", "bool", 0, 0, 0 },
    { "vacuum_grid_res", "int", "<min>4</min>", 0, 0 },
    { "vacuum_open_start_width", "int", "<min>0</min>", 0, 0 },
    { "wave_width", "float", "<min>0</min>", 0, 0 },
    { "wave_amp_mult", "float", "<min>-20.0</min><max>20.0</max>", 0, 0 },
    { "zoom_from_center", "int", RESTOSTRING (0, LAST_ZOOM_FROM_CENTER), 0, 0 },
    { "zoom_springiness", "float", "<min>0</min><max>1</max>", 0, 0 }
};

static CompOption *
animGetScreenOptions(CompPlugin *plugin, CompScreen * screen, int *count)
{
    ANIM_SCREEN(screen);

    *count = NUM_OPTIONS(as);
    return as->opt;
}

static void
objectInit(Object * object,
	   float positionX, float positionY,
	   float gridPositionX, float gridPositionY)
{
    object->gridPosition.x = gridPositionX;
    object->gridPosition.y = gridPositionY;

    object->position.x = positionX;
    object->position.y = positionY;

    object->offsetTexCoordForQuadBefore.x = 0;
    object->offsetTexCoordForQuadBefore.y = 0;
    object->offsetTexCoordForQuadAfter.x = 0;
    object->offsetTexCoordForQuadAfter.y = 0;
}

void
modelInitObjects(Model * model, int x, int y, int width, int height)
{
    int gridX, gridY;
    int nGridCellsX, nGridCellsY;
    float x0, y0;

    x0 = model->scaleOrigin.x;
    y0 = model->scaleOrigin.y;

    // number of grid cells in x direction
    nGridCellsX = model->gridWidth - 1;

    if (model->forWindowEvent == WindowEventShade ||
	model->forWindowEvent == WindowEventUnshade)
    {
	// number of grid cells in y direction
	nGridCellsY = model->gridHeight - 3;	// One allocated for top, one for bottom

	float winContentsHeight =
	    height - model->topHeight - model->bottomHeight;

	//Top
	float objectY = y + (0 - y0) * model->scale.y + y0;

	for (gridX = 0; gridX < model->gridWidth; gridX++)
	{
	    objectInit(&model->objects[gridX],
		       x + ((gridX * width / nGridCellsX) - x0) * 
		       model->scale.x + x0, objectY,
		       (float)gridX / nGridCellsX, 0);
	}

	// Window contents
	for (gridY = 1; gridY < model->gridHeight - 1; gridY++)
	{
	    float inWinY =
		(gridY - 1) * winContentsHeight / nGridCellsY +
		model->topHeight;
	    float gridPosY = inWinY / height;

	    objectY = y + (inWinY - y0) * model->scale.y + y0;

	    for (gridX = 0; gridX < model->gridWidth; gridX++)
	    {
		objectInit(&model->objects[gridY * model->gridWidth + gridX],
			   x + ((gridX * width / nGridCellsX) - x0) * 
			   model->scale.x + x0,
			   objectY, (float)gridX / nGridCellsX, gridPosY);
	    }
	}

	// Bottom (gridY is model->gridHeight-1 now)
	objectY = y + (height - y0) * model->scale.y + y0;

	for (gridX = 0; gridX < model->gridWidth; gridX++)
	{
	    objectInit(&model->objects[gridY * model->gridWidth + gridX],
		       x + ((gridX * width / nGridCellsX) - x0) * 
		       model->scale.x + x0, objectY,
		       (float)gridX / nGridCellsX, 1);
	}
    }
    else
    {
	int objIndex = 0;

	// number of grid cells in y direction
	nGridCellsY = model->gridHeight - 1;

	for (gridY = 0; gridY < model->gridHeight; gridY++)
	{
	    float objectY =
		y + ((gridY * height / nGridCellsY) -
		     y0) * model->scale.y + y0;
	    for (gridX = 0; gridX < model->gridWidth; gridX++)
	    {
		objectInit(&model->objects[objIndex],
			   x + ((gridX * width / nGridCellsX) - x0) * 
			   model->scale.x + x0,
			   objectY,
			   (float)gridX / nGridCellsX,
			   (float)gridY / nGridCellsY);
		objIndex++;
	    }
	}
    }
}

static void
modelMove (Model *model,
	   float tx,
	   float ty)
{
    Object *object = model->objects;
    int i;
    for (i = 0; i < model->numObjects; i++, object++)
    {
	object->position.x += tx;
	object->position.y += ty;
    }
}

static Model *createModel(CompWindow * w,
			  WindowEvent forWindowEvent,
			  AnimEffect forAnimEffect, int gridWidth,
			  int gridHeight)
{
    int x = WIN_X(w);
    int y = WIN_Y(w);
    int width = WIN_W(w);
    int height = WIN_H(w);

    Model *model;

    model = calloc(1, sizeof(Model));
    if (!model)
    {
	compLogMessage ("animation", CompLogLevelError,
			"Not enough memory");
	return 0;
    }

    model->gridWidth = gridWidth;
    model->gridHeight = gridHeight;
    model->numObjects = gridWidth * gridHeight;
    model->objects = calloc(model->numObjects, sizeof(Object));
    if (!model->objects)
    {
	compLogMessage ("animation", CompLogLevelError,
			"Not enough memory");
	free(model);
	return 0;
    }

    // Store win. size to check later
    model->winWidth = width;
    model->winHeight = height;

    // For shading
    model->forWindowEvent = forWindowEvent;
    model->topHeight = w->output.top;
    model->bottomHeight = w->output.bottom;

    model->scale.x = 1.0f;
    model->scale.y = 1.0f;

    model->scaleOrigin.x = 0.0f;
    model->scaleOrigin.y = 0.0f;

    modelInitObjects(model, x, y, width, height);

    return model;
}

static void
animFreeModel(AnimWindow *aw)
{
    if (!aw->com.model)
	return;

    if (aw->com.model->objects)
	free(aw->com.model->objects);
    free(aw->com.model);
    aw->com.model = NULL;
}

static Bool
animEnsureModel(CompWindow * w)
{
    ANIM_WINDOW(w);

    WindowEvent forWindowEvent = aw->com.curWindowEvent;
    AnimEffect forAnimEffect = aw->com.curAnimEffect;

    int gridWidth = 2;
    int gridHeight = 2;

    if (forAnimEffect->properties.initGridFunc)
	forAnimEffect->properties.initGridFunc (w, &gridWidth, &gridHeight);

    Bool isShadeUnshadeEvent =
	(forWindowEvent == WindowEventShade ||
	 forWindowEvent == WindowEventUnshade);

    Bool wasShadeUnshadeEvent = aw->com.model &&
	(aw->com.model->forWindowEvent == WindowEventShade ||
	 aw->com.model->forWindowEvent == WindowEventUnshade);

    if (!aw->com.model ||
	gridWidth != aw->com.model->gridWidth ||
	gridHeight != aw->com.model->gridHeight ||
	(isShadeUnshadeEvent != wasShadeUnshadeEvent) ||
	aw->com.model->winWidth != WIN_W(w) || aw->com.model->winHeight != WIN_H(w))
    {
	animFreeModel(aw);
	aw->com.model = createModel(w, forWindowEvent, forAnimEffect,
				gridWidth, gridHeight);
	if (!aw->com.model)
	    return FALSE;
    }

    return TRUE;
}

static void cleanUpParentChildChainItem(AnimScreen *as, AnimWindow *aw)
{
    if (aw->winThisIsPaintedBefore && !aw->winThisIsPaintedBefore->destroyed)
    {
	AnimWindow *aw2 =
	    GET_ANIM_WINDOW(aw->winThisIsPaintedBefore, as);
	if (aw2)
	    aw2->winToBePaintedBeforeThis = NULL;
    }
    aw->winThisIsPaintedBefore = NULL;
    aw->moreToBePaintedPrev = NULL;
    aw->moreToBePaintedNext = NULL;
    aw->isDodgeSubject = FALSE;
    aw->skipPostPrepareScreen = FALSE;
}

// Update this window's dodgers so that they no longer point
// to this window as their subject
static void
clearDodgersSubject (AnimScreen *as, CompWindow *w)
{
    CompWindow *dw;
    AnimWindow *adw;

    ANIM_WINDOW (w);

    for (dw = aw->dodgeChainStart; dw; dw = adw->dodgeChainNext)
    {
	adw = GET_ANIM_WINDOW(dw, as);
	if (!adw)
	    break;
	if (adw->dodgeSubjectWin == w)
	    adw->dodgeSubjectWin = NULL;
    }
}

// Remove this dodger window from the dodge chain
static void
removeFromDodgeChain (AnimScreen *as, CompWindow *dw)
{
    AnimWindow *adw = GET_ANIM_WINDOW(dw, as);
    if (!adw)
	return;

    if (adw->dodgeSubjectWin)
    {
	AnimWindow *awSubject = GET_ANIM_WINDOW (adw->dodgeSubjectWin, as);

	// if dw is the starting window in the dodge chain
	if (awSubject && awSubject->dodgeChainStart == dw)
	{
	    if (adw->dodgeChainNext)
	    {
		// Subject is being raised and there is a next dodger.
		awSubject->dodgeChainStart = adw->dodgeChainNext;
	    }
	    else
	    {
		// Subject is being lowered or there is no next dodger.
		// In either case, we can point chain start to prev. in chain,
		// which can be NULL.
		awSubject->dodgeChainStart = adw->dodgeChainPrev;
	    }
	}
    }

    if (adw->dodgeChainNext)
    {
	AnimWindow *awNext = GET_ANIM_WINDOW (adw->dodgeChainNext, as);

	// Point adw->next's prev. to adw->prev.
	if (awNext)
	    awNext->dodgeChainPrev = adw->dodgeChainPrev;
    }

    if (adw->dodgeChainPrev)
    {
	AnimWindow *awPrev = GET_ANIM_WINDOW (adw->dodgeChainPrev, as);

	// Point adw->prev.'s next to adw->next
	if (awPrev)
	    awPrev->dodgeChainNext = adw->dodgeChainNext;
    }
}

static void postAnimationCleanupCustom (CompWindow * w,
					Bool closing,
					Bool finishing,
					Bool clearMatchingRow)
{
    ANIM_WINDOW(w);
    ANIM_SCREEN(w->screen);

    if (// make sure window shadows (which are not drawn by polygon engine)
	// are damaged
	(aw->com.curAnimEffect &&
	 aw->com.curAnimEffect != AnimEffectNone &&
	 aw->com.curAnimEffect != AnimEffectRandom &&
	 aw->com.curAnimEffect->properties.addCustomGeometryFunc &&
	 (aw->com.curWindowEvent == WindowEventOpen ||
	  aw->com.curWindowEvent == WindowEventUnminimize ||
	  aw->com.curWindowEvent == WindowEventUnshade ||
	  aw->com.curWindowEvent == WindowEventFocus)) ||
	// make sure the window gets fully damaged with
	// effects that possibly have models that don't cover
	// the whole window (like in magic lamp with menus)
	aw->com.curAnimEffect == AnimEffectMagicLamp ||
	aw->com.curAnimEffect == AnimEffectVacuum ||
	// make sure dodging windows get one last damage
	aw->com.curAnimEffect == AnimEffectDodge ||
	// make sure non-animated closing windows get a damage
	(aw->com.curWindowEvent == WindowEventClose &&
	 aw->com.curAnimEffect == AnimEffectNone))
    {
	updateBBWindow (NULL, w, &aw->BB);
    }
    // Clear winPassingThrough of each window
    // that this one was passing through
    // during focus effect
    if (aw->com.curAnimEffect == AnimEffectFocusFade)
    {
	CompWindow *w2;
	for (w2 = w->screen->windows; w2; w2 = w2->next)
	{
	    AnimWindow *aw2;

	    aw2 = GET_ANIM_WINDOW(w2, as);
	    if (aw2->winPassingThrough == w)
		aw2->winPassingThrough = NULL;
	}
    }

    if (aw->com.curAnimEffect == AnimEffectFocusFade ||
	aw->com.curAnimEffect == AnimEffectDodge)
    {
	as->walkerAnimCount--;
    }

    if (aw->com.curAnimEffect &&
	aw->com.curAnimEffect != AnimEffectNone &&
	aw->com.curAnimEffect != AnimEffectRandom &&
	aw->com.curAnimEffect->properties.cleanupFunc)
	aw->com.curAnimEffect->properties.cleanupFunc (w);

    if (aw->isDodgeSubject)
	clearDodgersSubject (as, w);
    else if (aw->com.curAnimEffect == AnimEffectDodge)
	removeFromDodgeChain (as, w);

    aw->com.curWindowEvent = WindowEventNone;
    aw->com.curAnimEffect = AnimEffectNone;
    aw->com.animOverrideProgressDir = 0;
    aw->com.usingTransform = FALSE;

    aw->magicLampWaveCount = 0;

    if (aw->magicLampWaves)
    {
	free (aw->magicLampWaves);
	aw->magicLampWaves = 0;
    }

    if (aw->BB.x1 != MAXSHORT)
    {
	// damage BB
	damageBoundingBox (w);
    }
    aw->BB.x1 = aw->BB.y1 = MAXSHORT;
    aw->BB.x2 = aw->BB.y2 = MINSHORT;

    Bool thereIsUnfinishedChainElem = FALSE;

    // Look for still playing windows in parent-child chain
    CompWindow *wCur = aw->moreToBePaintedNext;
    while (wCur)
    {
	AnimWindow *awCur = GET_ANIM_WINDOW(wCur, as);

	if (awCur->com.animRemainingTime > 0)
	{
	    thereIsUnfinishedChainElem = TRUE;
	    break;
	}
	wCur = awCur->moreToBePaintedNext;
    }
    if (!thereIsUnfinishedChainElem)
    {
	wCur = aw->moreToBePaintedPrev;
	while (wCur)
	{
	    AnimWindow *awCur = GET_ANIM_WINDOW(wCur, as);

	    if (awCur->com.animRemainingTime > 0)
	    {
		thereIsUnfinishedChainElem = TRUE;
		break;
	    }
	    wCur = awCur->moreToBePaintedPrev;
	}
    }
    if (closing || finishing || !thereIsUnfinishedChainElem)
    {
	// Finish off all windows in parent-child chain
	CompWindow *wCur = aw->moreToBePaintedNext;
	while (wCur)
	{
	    AnimWindow *awCur = GET_ANIM_WINDOW(wCur, as);
	    if (awCur->isDodgeSubject)
		clearDodgersSubject (as, wCur);
	    wCur = awCur->moreToBePaintedNext;
	    cleanUpParentChildChainItem(as, awCur);
	}
	wCur = w;
	while (wCur)
	{
	    AnimWindow *awCur = GET_ANIM_WINDOW(wCur, as);
	    if (awCur->isDodgeSubject)
		clearDodgersSubject (as, wCur);
	    wCur = awCur->moreToBePaintedPrev;
	    cleanUpParentChildChainItem(as, awCur);
	}
    }

    aw->state = aw->newState;

    if (clearMatchingRow)
	aw->curAnimSelectionRow = -1;

    if (aw->com.drawRegion)
	XDestroyRegion(aw->com.drawRegion);
    aw->com.drawRegion = NULL;
    aw->com.useDrawRegion = FALSE;

    aw->animInitialized = FALSE;
    aw->remainderSteps = 0;
    aw->com.animRemainingTime = 0;

    // Reset dodge parameters
    aw->dodgeMaxAmount = 0;
    if (!(aw->moreToBePaintedPrev ||
	  aw->moreToBePaintedNext))
    {
	aw->isDodgeSubject = FALSE;
	aw->skipPostPrepareScreen = FALSE;
    }

    if (aw->restackInfo)
    {
	free(aw->restackInfo);
	aw->restackInfo = NULL;
    }

    if (!finishing)
    {
	aw->ignoreDamage = TRUE;
	while (aw->unmapCnt)
	{
	    unmapWindow(w);
	    aw->unmapCnt--;
	}
	aw->ignoreDamage = FALSE;
    }
    while (aw->destroyCnt)
    {
	destroyWindow(w);
	aw->destroyCnt--;
    }
}

void postAnimationCleanup (CompWindow * w)
{
    postAnimationCleanupCustom (w, FALSE, FALSE, TRUE);
}

static void
postAnimationCleanupPrev (CompWindow * w,
			  Bool closing,
			  Bool clearMatchingRow)
{
    ANIM_WINDOW(w);

    int curAnimSelectionRow = aw->curAnimSelectionRow;
    // Use previous event's anim selection row
    aw->curAnimSelectionRow = aw->prevAnimSelectionRow;

    postAnimationCleanupCustom (w, closing, FALSE, clearMatchingRow);

    // Restore current event's anim selection row
    aw->curAnimSelectionRow = curAnimSelectionRow;
}

static void
animActivateEvent (CompScreen *s,
		   Bool       activating)
{
    ANIM_SCREEN(s);
	
    if (activating)
    {
	if (as->animInProgress)
	    return;
    }
    as->animInProgress = activating;

    CompOption o[2];

    o[0].type = CompOptionTypeInt;
    o[0].name = "root";
    o[0].value.i = s->root;

    o[1].type = CompOptionTypeBool;
    o[1].name = "active";
    o[1].value.b = activating;

    (*s->display->handleCompizEvent) (s->display, "animation", "activate", o, 2);
}

static const PluginEventInfo watchedPlugins[] =
{
    {"switcher", "activate"},
    {"staticswitcher", "activate"},
    {"ring", "activate"},
    {"shift", "activate"},
    {"stackswitch", "activate"},
    {"scale", "activate"},
    // the above ones are the switchers
    {"group", "tabChangeActivate"},
    {"fadedesktop", "activate"},
};

static Bool
otherPluginsActive(AnimScreen *as)
{
    int i;
    for (i = 0; i < NUM_WATCHED_PLUGINS; i++)
	if (as->pluginActive[i])
	    return TRUE;
    return FALSE;
}

static inline Bool
isWinVisible(CompWindow *w)
{
    return (!w->destroyed &&
	    !(!w->shaded &&
	      (w->attrib.map_state != IsViewable)));
}

static inline void
getHostedOnWin (AnimScreen *as,
		CompWindow *w,
		CompWindow *wHost)
{
    ANIM_WINDOW(w);
    AnimWindow *awHost = GET_ANIM_WINDOW(wHost, as);
    awHost->winToBePaintedBeforeThis = w;
    aw->winThisIsPaintedBefore = wHost;
}

static void
initiateFocusAnimation(CompWindow *w)
{
    CompScreen *s = w->screen;
    ANIM_SCREEN(s);
    ANIM_WINDOW(w);
    int duration = 200;

    if (aw->com.curWindowEvent != WindowEventNone || otherPluginsActive(as))
	return;

    // Check the "switcher post-wait" counter that effectively prevents
    // focus animation to be initiated when the zoom option value is low
    // in Switcher.
    if (switcherPostWait)
	return;

    AnimEffect chosenEffect =
	getMatchingAnimSelection (w, AnimEventFocus, &duration);

    if (chosenEffect != AnimEffectNone &&
	// On unminimization, focus event is fired first.
	// When this happens and minimize is in progress,
	// don't prevent rewinding of minimize when unminimize is fired
	// right after this focus event.
	aw->com.curWindowEvent != WindowEventMinimize)
    {
	CompWindow *wStart = NULL;
	CompWindow *wEnd = NULL;
	CompWindow *wOldAbove = NULL;

	RestackInfo *restackInfo = aw->restackInfo;
	Bool raised = TRUE;

	if (restackInfo)
	{
	    wStart = restackInfo->wStart;
	    wEnd = restackInfo->wEnd;
	    wOldAbove = restackInfo->wOldAbove;
	    raised = restackInfo->raised;
	}

	// FOCUS event!

	aw->com.curWindowEvent = WindowEventFocus;
	aw->com.curAnimEffect = chosenEffect;

	if (chosenEffect == AnimEffectFocusFade ||
	    chosenEffect == AnimEffectDodge)
	{
	    as->walkerAnimCount++;

	    // Find union region of all windows that will be
	    // faded through by w. If the region is empty, don't
	    // run focus fade effect.

	    Region fadeRegion = XCreateRegion();
	    Region thisAndSubjectIntersection = XCreateRegion();
	    Region thisWinRegion = XCreateRegion();
	    Region subjectWinRegion = XCreateRegion();
	    XRectangle rect;

	    int numDodgingWins = 0;

	    // Compute subject win. region
	    rect.x = BORDER_X(w);
	    rect.y = BORDER_Y(w);
	    rect.width = BORDER_W(w);
	    rect.height = BORDER_H(w);
	    XUnionRectWithRegion(&rect, &emptyRegion, subjectWinRegion);

	    CompWindow *dw; // Dodge or Focus fade candidate window
	    for (dw = wStart; dw && dw != wEnd->next; dw = dw->next)
	    {
		if (!isWinVisible(dw) ||
		    dw->wmType & CompWindowTypeDockMask)
		    continue;

		AnimWindow *adw = GET_ANIM_WINDOW(dw, as);

		// Skip windows that have been restacked
		if (dw != wEnd && adw->restackInfo)
		    continue;

		// Skip subject window for focus fade
		if (w == dw && chosenEffect == AnimEffectFocusFade)
		    continue;

		Bool nonMatching = FALSE;
		if (chosenEffect == AnimEffectDodge &&
		    getMatchingAnimSelection (dw, AnimEventFocus, NULL) !=
		    chosenEffect)
		    nonMatching = TRUE;

		// Compute intersection of this (dw) with subject
		rect.x = BORDER_X(dw);
		rect.y = BORDER_Y(dw);
		rect.width = BORDER_W(dw);
		rect.height = BORDER_H(dw);
		XUnionRectWithRegion(&rect, &emptyRegion, thisWinRegion);
		XIntersectRegion(subjectWinRegion, thisWinRegion,
				 thisAndSubjectIntersection);
		XUnionRegion(fadeRegion, thisAndSubjectIntersection,
			     fadeRegion);

		if (chosenEffect == AnimEffectFocusFade)
		{
		    adw->winPassingThrough = w;
		}
		else if (chosenEffect == AnimEffectDodge &&
			 !XEmptyRegion(thisAndSubjectIntersection) &&
			 (adw->com.curAnimEffect == AnimEffectNone ||
			  (adw->com.curAnimEffect == AnimEffectDodge)) &&
			 dw->id != w->id) // don't let the subject dodge itself
		{
		    // Mark this window for dodge

		    numDodgingWins++;
		    adw->dodgeOrder = numDodgingWins;
		    if (nonMatching) // Use neg. values for non-matching windows
			adw->dodgeOrder *= -1;
		}
	    }

	    if (XEmptyRegion(fadeRegion))
	    {
		// empty intersection -> won't be drawn (will end prematurely)
		duration = 0;
	    }
	    if ((chosenEffect == AnimEffectFocusFade ||
		 chosenEffect == AnimEffectDodge) && wOldAbove)
	    {
		// Store this window in the next window
		// so that this is drawn before that,
		// i.e. in its old place
		getHostedOnWin(as, w, wOldAbove);
	    }

	    if (chosenEffect == AnimEffectDodge)
	    {
		float maxTransformTotalProgress = 0;
		float dodgeMaxStartProgress =
		    numDodgingWins *
		    animGetF (w, ANIM_SCREEN_OPTION_DODGE_GAP_RATIO) *
		    duration / 1000.0f;

		CompWindow *wDodgeChainLastVisited = NULL;

		animActivateEvent(s, TRUE);

		aw->isDodgeSubject = TRUE;
		aw->dodgeChainStart = NULL;

		for (dw = wStart; dw && dw != wEnd->next; dw = dw->next)
		{
		    AnimWindow *adw = GET_ANIM_WINDOW(dw, as);

		    // Skip non-dodgers
		    if (adw->dodgeOrder == 0)
			continue;

		    // Initiate dodge for this window

		    Bool stationaryDodger = FALSE;
		    if (adw->dodgeOrder < 0)
		    {
			adw->dodgeOrder *= -1; // Make it positive again
			stationaryDodger = TRUE;
		    }
		    if (adw->com.curAnimEffect != AnimEffectDodge)
		    {
			adw->com.curAnimEffect = AnimEffectDodge;
			as->walkerAnimCount++;
		    }
		    adw->dodgeSubjectWin = w;

		    // Slight change in dodge movement start
		    // to reflect stacking order of dodgy windows
		    if (raised)
			adw->com.transformStartProgress =
			    dodgeMaxStartProgress *
			    (adw->dodgeOrder - 1) / numDodgingWins;
		    else
			adw->com.transformStartProgress =
			    dodgeMaxStartProgress *
			    (1 - (float)adw->dodgeOrder / numDodgingWins);

		    float transformTotalProgress =
			1 + adw->com.transformStartProgress;

		    if (maxTransformTotalProgress < transformTotalProgress)
			maxTransformTotalProgress = transformTotalProgress;

		    // normalize
		    adw->com.transformStartProgress /=
			transformTotalProgress;

		    if (stationaryDodger)
		    {
			adw->com.transformStartProgress = 0;
			transformTotalProgress = 0;
		    }

		    adw->com.animTotalTime =
			transformTotalProgress * duration;
		    adw->com.animRemainingTime = adw->com.animTotalTime;

		    // Put window on dodge chain

		    // if dodge chain was started before
		    if (wDodgeChainLastVisited)
		    {
			AnimWindow *awDodgeChainLastVisited =
			    GET_ANIM_WINDOW(wDodgeChainLastVisited, as);
			if (raised)
			    awDodgeChainLastVisited->dodgeChainNext = dw;
			else
			    awDodgeChainLastVisited->dodgeChainPrev = dw;
		    }
		    else if (raised) // mark chain start
		    {
			aw->dodgeChainStart = dw;
		    }
		    if (raised)
		    {
			adw->dodgeChainPrev = wDodgeChainLastVisited;
			adw->dodgeChainNext = NULL;
		    }
		    else
		    {
			adw->dodgeChainPrev = NULL;
			adw->dodgeChainNext = wDodgeChainLastVisited;
		    }

		    // Find direction (left, right, up, down)
		    // that minimizes dodge amount

		    // Dodge amount (dodge shadows as well)

		    int dodgeAmount[4];

		    int i;
		    for (i = 0; i < 4; i++)
			dodgeAmount[i] = DODGE_AMOUNT(w, dw, i);

		    int amountMin = abs(dodgeAmount[0]);
		    int iMin = 0;
		    for (i=1; i<4; i++)
		    {
			int absAmount = abs(dodgeAmount[i]);
			if (absAmount < amountMin)
			{
			    amountMin = absAmount;
			    iMin = i;
			}
		    }
		    adw->dodgeMaxAmount = dodgeAmount[iMin];
		    adw->dodgeDirection = iMin;

		    wDodgeChainLastVisited = dw;

		    // Reset back to 0 for the next dodge calculation
		    adw->dodgeOrder = 0;
		}
		if (aw->isDodgeSubject)
		    aw->dodgeMaxAmount = 0;

		// if subject is being lowered,
		// point chain-start to the topmost dodging window
		if (!raised)
		{
		    aw->dodgeChainStart = wDodgeChainLastVisited;
		}

		aw->com.animTotalTime =
		    maxTransformTotalProgress * duration;
	    }

	    XDestroyRegion (fadeRegion);
	    XDestroyRegion (thisAndSubjectIntersection);
	    XDestroyRegion (thisWinRegion);
	    XDestroyRegion (subjectWinRegion);
	}

	if (!animEnsureModel(w))
	{
	    postAnimationCleanup (w);
	    return;
	}

	animActivateEvent(s, TRUE);

	if (chosenEffect != AnimEffectDodge)
	    aw->com.animTotalTime = duration;
	aw->com.animRemainingTime = aw->com.animTotalTime;

	damagePendingOnScreen (s);
    }
}

// returns whether this window is relevant for fade focus
static Bool
relevantForFadeFocus(CompWindow *nw)
{
    if (!((nw->wmType &
	   // these two are to be used as "host" windows
	   // to host the painting of windows being focused
	   // at a stacking order lower than them
	   (CompWindowTypeDockMask | CompWindowTypeSplashMask)) ||
	  nw->wmType == CompWindowTypeNormalMask ||
	  nw->wmType == CompWindowTypeDialogMask ||
	  nw->wmType == CompWindowTypeUtilMask ||
	  nw->wmType == CompWindowTypeUnknownMask))
    {
	return FALSE;
    }
    return isWinVisible(nw);
}

static Bool
restackInfoStillGood(CompScreen *s, RestackInfo *restackInfo)
{
    Bool wStartGood = FALSE;
    Bool wEndGood = FALSE;
    Bool wOldAboveGood = FALSE;
    Bool wRestackedGood = FALSE;

    CompWindow *w;
    for (w = s->windows; w; w = w->next)
    {
	if (restackInfo->wStart == w && isWinVisible(w))
	    wStartGood = TRUE;
	if (restackInfo->wEnd == w && isWinVisible(w))
	    wEndGood = TRUE;
	if (restackInfo->wRestacked == w && isWinVisible(w))
	    wRestackedGood = TRUE;
	if (restackInfo->wOldAbove == w && isWinVisible(w))
	    wOldAboveGood = TRUE;
    }
    return (wStartGood && wEndGood && wOldAboveGood && wRestackedGood);
}

// Reset stacking related info
static void
resetStackingInfo (CompScreen *s)
{
    CompWindow *w;
    for (w = s->windows; w; w = w->next)
    {
	ANIM_WINDOW (w);

	aw->configureNotified = FALSE;
	if (aw->restackInfo)
	{
	    free (aw->restackInfo);
	    aw->restackInfo = NULL;
	}
    }
}

// Returns TRUE if linking wCur to wNext would not result
// in a circular chain being formed.
static Bool
wontCreateCircularChain (CompWindow *wCur, CompWindow *wNext)
{
    ANIM_SCREEN (wCur->screen);
    AnimWindow *awNext = NULL;

    while (wNext)
    {
	if (wNext == wCur) // would form circular chain
	    return FALSE;

	awNext = GET_ANIM_WINDOW (wNext, as);
	if (!awNext)
	    return FALSE;

	wNext = awNext->moreToBePaintedNext;
    }
    return TRUE; 
}

static void animPreparePaintScreen(CompScreen * s, int msSinceLastPaint)
{
    CompWindow *w;

    ANIM_SCREEN(s);

    // Check and update "switcher post wait" counter
    if (switcherPostWait > 0)
    {
	switcherPostWait++;
	if (switcherPostWait > 4) // wait over
	{
	    switcherPostWait = 0;

	    // Reset stacking related info since it will
	    // cause problems because of the restacking
	    // just done by Switcher.
	    resetStackingInfo (s);
	}
    }

    if (as->aWinWasRestackedJustNow)
    {
	/*
	  Handle focusing windows with multiple utility/dialog windows
	  (like gobby), as in this case where gobby was raised with its
	  utility windows:

	  was: C0001B 36000A5 1E0000C 1E0005B 1E00050 3205B63 600003 
	  now: C0001B 36000A5 1E0000C 1E00050 3205B63 1E0005B 600003 

	  was: C0001B 36000A5 1E0000C 1E00050 3205B63 1E0005B 600003 
	  now: C0001B 36000A5 1E0000C 3205B63 1E00050 1E0005B 600003 

	  was: C0001B 36000A5 1E0000C 3205B63 1E00050 1E0005B 600003 
	  now: C0001B 36000A5 3205B63 1E0000C 1E00050 1E0005B 600003 
	*/
	CompWindow *wOldAbove = NULL;
	for (w = s->windows; w; w = w->next)
	{
	    ANIM_WINDOW(w);
	    if (aw->restackInfo)
	    {
		if (aw->com.curWindowEvent != WindowEventNone ||
		    otherPluginsActive(as) ||
		    // Don't animate with stale restack info
		    !restackInfoStillGood(s, aw->restackInfo))
		{
		    continue;
		}
		if (!wOldAbove)
		{
		    // Pick the old above of the bottommost one
		    wOldAbove = aw->restackInfo->wOldAbove;
		}
		else
		{
		    // Use as wOldAbove for every focus fading window
		    // (i.e. the utility/dialog windows of an app.)
		    if (wOldAbove != w)
			aw->restackInfo->wOldAbove = wOldAbove;
		}
	    }
	}
	// do in reverse order so that focus-fading chains are handled
	// properly
	for (w = s->reverseWindows; w; w = w->prev)
	{
	    ANIM_WINDOW(w);
	    if (aw->restackInfo)
	    {
		if (aw->com.curWindowEvent != WindowEventNone ||
		    // Don't initiate focus anim for current dodgers
		    aw->com.curAnimEffect != AnimEffectNone ||
		    // Don't initiate focus anim for windows being passed thru
		    aw->winPassingThrough ||
		    otherPluginsActive(as) ||
		    // Don't animate with stale restack info
		    !restackInfoStillGood(s, aw->restackInfo))
		{
		    free(aw->restackInfo);
		    aw->restackInfo = NULL;
		    continue;
		}

		// Find the first window at a higher stacking order than w
		CompWindow *nw;
		for (nw = w->next; nw; nw = nw->next)
		{
		    if (relevantForFadeFocus(nw))
			break;
		}

		// If w is being lowered, there has to be a window
		// at a higher stacking position than w (like a panel)
		// which this w's copy can be painted before.
		// Otherwise the animation will only show w fading in
		// rather than 2 copies of it cross-fading.
		if (!aw->restackInfo->raised && !nw)
		{
		    // Free unnecessary restackInfo
		    free(aw->restackInfo);
		    aw->restackInfo = NULL;
		    continue;
		}

		// Check if above window is focus-fading too.
		// (like a dialog of an app. window)
		// If so, focus-fade this together with the one above
		// (link to it)
		if (nw)
		{
		    AnimWindow *awNext = GET_ANIM_WINDOW(nw, as);
		    if (awNext && awNext->winThisIsPaintedBefore &&
			wontCreateCircularChain (w, nw))
		    {
			awNext->moreToBePaintedPrev = w;
			aw->moreToBePaintedNext = nw;
			aw->restackInfo->wOldAbove =
			    awNext->winThisIsPaintedBefore;
		    }
		}
		initiateFocusAnimation(w);
	    }
	}

	for (w = s->reverseWindows; w; w = w->prev)
	{
	    ANIM_WINDOW(w);

	    if (!aw->isDodgeSubject)
		continue;
	    Bool dodgersAreOnlySubjects = TRUE;
	    CompWindow *dw;
	    AnimWindow *adw;
	    for (dw = aw->dodgeChainStart; dw; dw = adw->dodgeChainNext)
	    {
		adw = GET_ANIM_WINDOW(dw, as);
		if (!adw)
		    break;
		if (!adw->isDodgeSubject)
		    dodgersAreOnlySubjects = FALSE;
	    }
	    if (dodgersAreOnlySubjects)
		aw->skipPostPrepareScreen = TRUE;
	}
    }
	
    if (as->animInProgress)
    {
	AnimWindow *aw;
	Bool animStillInProgress = FALSE;

	for (w = s->windows; w; w = w->next)
	{
	    aw = GET_ANIM_WINDOW(w, as);

	    if (aw->com.animRemainingTime > 0 &&
		(!aw->com.curAnimEffect ||
		 aw->com.curAnimEffect == AnimEffectNone ||
		 aw->com.curAnimEffect == AnimEffectRandom))
	    {
	    	postAnimationCleanup (w);
	    }
	    else if (aw->com.animRemainingTime > 0)
	    {
		if (aw->com.curAnimEffect->properties.prePrepPaintScreenFunc &&
		    aw->com.curAnimEffect->properties.prePrepPaintScreenFunc
			(w, msSinceLastPaint))
		    animStillInProgress = TRUE;

	    	// If just starting, call fx init func.
		if (!aw->animInitialized &&
		    aw->com.curAnimEffect->properties.initFunc)
		{
		    if (!aw->com.curAnimEffect->properties.initFunc (w))
		    {
			// Abort this window's animation
			postAnimationCleanup (w);
			continue;
		    }
		}

		if (aw->com.model &&
		    (aw->com.model->winWidth != WIN_W(w) ||
		     aw->com.model->winHeight != WIN_H(w)))
		{
		    // model needs update
		    // re-create model
		    if (!animEnsureModel (w))
		    {
			// Abort this window's animation
			postAnimationCleanup (w);
			continue;
		    }
		}

		if (aw->com.curAnimEffect->properties.updateBBFunc)
		{
		    copyResetBB (aw);

		    if (!aw->animInitialized &&
			(aw->com.curWindowEvent == WindowEventClose ||
			 aw->com.curWindowEvent == WindowEventMinimize ||
			 aw->com.curWindowEvent == WindowEventShade ||
			 ((aw->com.curWindowEvent == WindowEventFocus ||
			   // for dodging windows
			   aw->com.curAnimEffect == AnimEffectDodge) &&
			  !aw->isDodgeSubject)))
			updateBBWindow (NULL, w, &aw->BB);
		}
		aw->animInitialized = TRUE;

		if (aw->com.curAnimEffect->properties.animStepFunc)
		    aw->com.curAnimEffect->properties.animStepFunc
			(w, msSinceLastPaint);

		if (aw->com.curAnimEffect->properties.updateBBFunc)
		{
		    int i;
		    for (i = 0; i < s->nOutputDev; i++)
			aw->com.curAnimEffect->properties.
			    updateBBFunc (&s->outputDev[i], w, &aw->BB);

		    if (!(s->damageMask & COMP_SCREEN_DAMAGE_ALL_MASK))
			damageBoundingBox (w);
		}

		if (aw->com.animRemainingTime <= 0)
		{
		    // Animation done
		    postAnimationCleanup (w);
		}
		animStillInProgress |= (aw->com.animRemainingTime > 0);
	    }

	    if (aw->com.animRemainingTime <= 0)
	    {
		if (aw->com.curAnimEffect != AnimEffectNone ||
		    aw->unmapCnt > 0 || aw->destroyCnt > 0)
		{
		    postAnimationCleanup (w);
		}
		aw->com.curWindowEvent = WindowEventNone;
		aw->com.curAnimEffect = AnimEffectNone;
	    }
	}

	for (w = s->windows; w; w = w->next)
	{
	    aw = GET_ANIM_WINDOW(w, as);
	    if (aw &&
		aw->com.curAnimEffect &&
		aw->com.curAnimEffect != AnimEffectNone &&
		aw->com.curAnimEffect != AnimEffectRandom &&
		aw->com.curAnimEffect->properties.postPrepPaintScreenFunc)
	    {
		aw->com.curAnimEffect->properties.postPrepPaintScreenFunc (w);
	    }
	}

	if (!animStillInProgress)
	    animActivateEvent(s, FALSE);
    }

    UNWRAP(as, s, preparePaintScreen);
    (*s->preparePaintScreen) (s, msSinceLastPaint);
    WRAP(as, s, preparePaintScreen, animPreparePaintScreen);
}

static void animDonePaintScreen(CompScreen * s)
{
    ANIM_SCREEN(s);

    if (as->animInProgress)
	damagePendingOnScreen (s);

    UNWRAP(as, s, donePaintScreen);
    (*s->donePaintScreen) (s);
    WRAP(as, s, donePaintScreen, animDonePaintScreen);
}

// Scales z by 0 and does perspective distortion so that it
// looks the same wherever on the screen
void
perspectiveDistortAndResetZ (CompScreen *s,
			     CompTransform *transform)
{
    float v = -1.0 / s->width;
    /*
      This does
      transform = M * transform, where M is
      1, 0, 0, 0,
      0, 1, 0, 0,
      0, 0, 0, v,
      0, 0, 0, 1
    */
    float *m = transform->m;
    m[8] = v * m[12];
    m[9] = v * m[13];
    m[10] = v * m[14];
    m[11] = v * m[15];
}

void
applyPerspectiveSkew (CompOutput *output,
		      CompTransform *transform,
		      Point *center)
{
    GLfloat skewx = -(((center->x - output->region.extents.x1) -
		       output->width / 2) * 1.15);
    GLfloat skewy = -(((center->y - output->region.extents.y1) -
		       output->height / 2) * 1.15);

    /* transform = M * transform, where M is the skew matrix
	{1,0,0,0,
	 0,1,0,0,
	 skewx,skewy,1,0,
	 0,0,0,1};
    */

    float *m = transform->m;
    m[8] = skewx * m[0] + skewy * m[4] + m[8];
    m[9] = skewx * m[1] + skewy * m[5] + m[9];
    m[10] = skewx * m[2] + skewy * m[6] + m[10];
    m[11] = skewx * m[3] + skewy * m[7] + m[11];
}

static void
animAddWindowGeometry(CompWindow * w,
		      CompMatrix * matrix,
		      int nMatrix, Region region, Region clip)
{
    ANIM_WINDOW(w);
    ANIM_SCREEN(w->screen);

    // if window is being animated
    if (aw->com.animRemainingTime > 0 && aw->com.model &&
	!(aw->com.curAnimEffect->properties.letOthersDrawGeomsFunc &&
	  aw->com.curAnimEffect->properties.letOthersDrawGeomsFunc (w)))
    {
	BoxPtr pClip;
	int nClip;
	int nVertices, nIndices;
	GLushort *i;
	GLfloat *v;
	int x1, y1, x2, y2;
	float width, height;
	float winContentsY, winContentsHeight;
	float deformedX, deformedY;
	float deformedZ = 0;
	int nVertX, nVertY, wx, wy;
	int vSize, it;
	float gridW, gridH, x, y;
	Bool rect = TRUE;
	Bool useTextureQ = FALSE;
	Model *model = aw->com.model;
	Region awRegion = NULL;

	Bool notUsing3dCoords =
	    !aw->com.curAnimEffect->properties.modelAnimIs3D;

	// Use Q texture coordinate to avoid jagged-looking quads
	// http://www.r3.nu/~cass/qcoord/
	if (aw->com.curAnimEffect->properties.useQTexCoord)
	    useTextureQ = TRUE;

	if (aw->com.useDrawRegion)
	{
	    awRegion = XCreateRegion();
	    XIntersectRegion (region, aw->com.drawRegion, awRegion);
	    nClip = awRegion->numRects;
	    pClip = awRegion->rects;
	}
	else
	{
	    nClip = region->numRects;
	    pClip = region->rects;
	}

	if (nClip == 0)			// nothing to do
	{
	    if (awRegion)
		XDestroyRegion(awRegion);
	    return;
	}

	for (it = 0; it < nMatrix; it++)
	{
	    if (matrix[it].xy != 0.0f || matrix[it].yx != 0.0f)
	    {
		rect = FALSE;
		break;
	    }
	}

	w->drawWindowGeometry = animDrawWindowGeometry;

	if (aw->com.curAnimEffect->properties.addCustomGeometryFunc)
	{
	    if (nMatrix == 0)
		return;
	    aw->com.curAnimEffect->properties.
		addCustomGeometryFunc (w, nClip, pClip,
				       nMatrix, matrix);

	    // If addGeometryFunc exists, it is expected to do everthing
	    // to add geometries (instead of the rest of this function).

	    if (w->vCount == 0)	// if there is no vertex
	    {
		// put a dummy quad in vertices and indices

		w->texUnits = 1;
		w->texCoordSize = 4;
		vSize = 3 + w->texUnits * w->texCoordSize;

		if (4 > w->indexSize)
		{
		    if (!moreWindowIndices(w, 4))
			return;
		}
		if (4 * vSize > w->vertexSize)
		{
		    if (!moreWindowVertices(w, 4 * vSize))
			return;
		}
		w->vCount = 4;
		w->indexCount = 4;
		w->vertexStride = vSize;

		// Clear dummy quad coordinates/indices
		memset(w->vertices, 0, sizeof(GLfloat) * 4 * vSize);
		memset(w->indices, 0, sizeof(GLushort) * 4);
	    }
	    return;				// We're done here.
	}

	// window coordinates and size
	wx = WIN_X(w);
	wy = WIN_Y(w);
	width = WIN_W(w);
	height = WIN_H(w);

	// to be used if event is shade/unshade
	winContentsY = w->attrib.y;
	winContentsHeight = w->height;

	w->texUnits = nMatrix;

	if (w->vCount == 0)
	{
	    // reset
	    w->indexCount = 0;
	    w->texCoordSize = 4;
	}
	w->vertexStride = 3 + w->texUnits * w->texCoordSize;
	vSize = w->vertexStride;

	nVertices = w->vCount;
	nIndices = w->indexCount;

	v = w->vertices + (nVertices * vSize);
	i = w->indices + nIndices;

	// For each clip passed to this function
	for (; nClip--; pClip++)
	{
	    x1 = pClip->x1;
	    y1 = pClip->y1;
	    x2 = pClip->x2;
	    y2 = pClip->y2;

	    gridW = (float)width / (model->gridWidth - 1);

	    if (aw->com.curWindowEvent == WindowEventShade ||
		aw->com.curWindowEvent == WindowEventUnshade)
	    {
		if (y1 < w->attrib.y)	// if at top part
		{
		    gridH = model->topHeight;
		}
		else if (y2 > w->attrib.y + w->height)	// if at bottom
		{
		    gridH = model->bottomHeight;
		}
		else			// in window contents (only in Y coords)
		{
		    float winContentsHeight =
			height - model->topHeight - model->bottomHeight;
		    gridH = winContentsHeight / (model->gridHeight - 3);
		}
	    }
	    else
		gridH = (float)height / (model->gridHeight - 1);

	    // nVertX, nVertY: number of vertices for this clip in x and y dimensions
	    // + 2 to avoid running short of vertices in some cases
	    nVertX = ceil((x2 - x1) / gridW) + 2;
	    nVertY = (gridH ? ceil((y2 - y1) / gridH) : 0) + 2;

	    // Allocate 4 indices for each quad
	    int newIndexSize = nIndices + ((nVertX - 1) * (nVertY - 1) * 4);

	    if (newIndexSize > w->indexSize)
	    {
		if (!moreWindowIndices(w, newIndexSize))
		    return;

		i = w->indices + nIndices;
	    }
	    // Assign quad vertices to indices
	    int jx, jy;
	    for (jy = 0; jy < nVertY - 1; jy++)
	    {
		for (jx = 0; jx < nVertX - 1; jx++)
		{
		    *i++ = nVertices + nVertX * (2 * jy + 1) + jx;
		    *i++ = nVertices + nVertX * (2 * jy + 1) + jx + 1;
		    *i++ = nVertices + nVertX * 2 * jy + jx + 1;
		    *i++ = nVertices + nVertX * 2 * jy + jx;

		    nIndices += 4;
		}
	    }

	    // Allocate vertices
	    int newVertexSize =
		(nVertices + nVertX * (2 * nVertY - 2)) * vSize;
	    if (newVertexSize > w->vertexSize)
	    {
		if (!moreWindowVertices(w, newVertexSize))
		    return;

		v = w->vertices + (nVertices * vSize);
	    }

	    float rowTexCoordQ = 1;
	    float prevRowCellWidth = 0;	// this initial value won't be used
	    float rowCellWidth = 0;

	    // For each vertex
	    for (jy = 0, y = y1; jy < nVertY; jy++)
	    {
		float topiyFloat;
		Bool applyOffsets = TRUE;

		if (y > y2)
		    y = y2;

		// Do calculations for y here to avoid repeating
		// them unnecessarily in the x loop

		if (aw->com.curWindowEvent == WindowEventShade
		    || aw->com.curWindowEvent == WindowEventUnshade)
		{
		    if (y1 < w->attrib.y)	// if at top part
		    {
			topiyFloat = (y - WIN_Y(w)) / model->topHeight;
			topiyFloat = MIN(topiyFloat, 0.999);	// avoid 1.0
			applyOffsets = FALSE;
		    }
		    else if (y2 > w->attrib.y + w->height)	// if at bottom
		    {
			topiyFloat = (model->gridHeight - 2) +
			    (model->bottomHeight ? (y - winContentsY -
						    winContentsHeight) /
			     model->bottomHeight : 0);
			applyOffsets = FALSE;
		    }
		    else		// in window contents (only in Y coords)
		    {
			topiyFloat = (model->gridHeight - 3) * 
			    (y - winContentsY) / winContentsHeight + 1;
		    }
		}
		else
		{
		    topiyFloat = (model->gridHeight - 1) * (y - wy) / height;
		}
		// topiy should be at most (model->gridHeight - 2)
		int topiy = (int)(topiyFloat + 1e-4);

		if (topiy == model->gridHeight - 1)
		    topiy--;
		int bottomiy = topiy + 1;
		float iny = topiyFloat - topiy;

		// End of calculations for y

		for (jx = 0, x = x1; jx < nVertX; jx++)
		{
		    if (x > x2)
			x = x2;

		    // find containing grid cell (leftix rightix) x (topiy bottomiy)
		    float leftixFloat =
			(model->gridWidth - 1) * (x - wx) / width;
		    int leftix = (int)(leftixFloat + 1e-4);

		    if (leftix == model->gridWidth - 1)
			leftix--;
		    int rightix = leftix + 1;

		    // Objects that are at top, bottom, left, right corners of quad
		    Object *objToTopLeft =
			&(model->objects[topiy * model->gridWidth + leftix]);
		    Object *objToTopRight =
			&(model->objects[topiy * model->gridWidth + rightix]);
		    Object *objToBottomLeft =
			&(model->objects[bottomiy * model->gridWidth + leftix]);
		    Object *objToBottomRight =
			&(model->objects[bottomiy * model->gridWidth + rightix]);

		    // find position in cell by taking remainder of flooring
		    float inx = leftixFloat - leftix;

		    // Interpolate to find deformed coordinates

		    float hor1x = (1 - inx) *
			objToTopLeft->position.x +
			inx * objToTopRight->position.x;
		    float hor1y = (1 - inx) *
			objToTopLeft->position.y +
			inx * objToTopRight->position.y;
		    float hor1z = notUsing3dCoords ? 0 :
			(1 - inx) *
			objToTopLeft->position.z +
			inx * objToTopRight->position.z;
		    float hor2x = (1 - inx) *
			objToBottomLeft->position.x +
			inx * objToBottomRight->position.x;
		    float hor2y = (1 - inx) *
			objToBottomLeft->position.y +
			inx * objToBottomRight->position.y;
		    float hor2z = notUsing3dCoords ? 0 :
			(1 - inx) *
			objToBottomLeft->position.z +
			inx * objToBottomRight->position.z;

		    deformedX = (1 - iny) * hor1x + iny * hor2x;
		    deformedY = (1 - iny) * hor1y + iny * hor2y;
		    deformedZ = (1 - iny) * hor1z + iny * hor2z;

		    // Texture coordinates (s, t, r, q)

		    if (useTextureQ)
		    {
			if (jx == 1)
			    rowCellWidth = deformedX - v[-3];

			// do only once per row for all rows except row 0
			if (jy > 0 && jx == 1)
			{
			    rowTexCoordQ = (rowCellWidth / prevRowCellWidth);

			    for (it = 0; it < nMatrix; it++, v += 4)
			    {
				// update first column
				// (since we didn't know rowTexCoordQ before)
				v[-vSize]     *= rowTexCoordQ; // multiply s & t by q
				v[-vSize + 1] *= rowTexCoordQ;
				v[-vSize + 3] = rowTexCoordQ;  // copy q
			    }
			    v -= nMatrix * 4;
			}
		    }

		    // Loop for each texture element
		    // (4 texture coordinates for each one)
		    for (it = 0; it < nMatrix; it++, v += 4)
		    {
			float offsetY = 0;

			if (rect)
			{
			    if (applyOffsets && y < y2)
				offsetY = objToTopLeft->offsetTexCoordForQuadAfter.y;
			    v[0] = COMP_TEX_COORD_X (&matrix[it], x); // s
			    v[1] = COMP_TEX_COORD_Y (&matrix[it], y + offsetY); // t
			}
			else
			{
			    if (applyOffsets && y < y2)
				// FIXME:
			    	// The correct y offset below produces wrong
				// texture coordinates for some reason.
				offsetY = 0;
				// offsetY = objToTopLeft->offsetTexCoordForQuadAfter.y;
			    v[0] = COMP_TEX_COORD_XY (&matrix[it], x, y + offsetY); // s
			    v[1] = COMP_TEX_COORD_YX (&matrix[it], x, y + offsetY); // t
			}
			v[2] = 0; // r

			if (0 < jy && jy < nVertY - 1)
			{
			    // copy s, t, r to duplicate row
			    memcpy(v + nVertX * vSize, v,
				   3 * sizeof(GLfloat));
			    v[3 + nVertX * vSize] = 1; // q
			}

			if (applyOffsets && 
			    objToTopLeft->offsetTexCoordForQuadBefore.y != 0)
			{
			    // After copying to next row, update texture y coord
			    // by following object's offset
			    offsetY = objToTopLeft->offsetTexCoordForQuadBefore.y;
			    if (rect)
			    {
				v[1] = COMP_TEX_COORD_Y (&matrix[it], y + offsetY);
			    }
			    else
			    {
				v[0] = COMP_TEX_COORD_XY (&matrix[it],
							  x, y + offsetY);
				v[1] = COMP_TEX_COORD_YX (&matrix[it],
							  x, y + offsetY);
			    }
			}
			if (useTextureQ)
			{
			    v[3] = rowTexCoordQ; // q

			    if (jx > 0)	// since column 0 is updated when jx == 1
			    {
				// multiply s & t by q
				v[0] *= rowTexCoordQ;
				v[1] *= rowTexCoordQ;
			    }
			}
			else
			{
			    v[3] = 1; // q
			}
		    }

		    v[0] = deformedX;
		    v[1] = deformedY;
		    v[2] = deformedZ;

		    // Copy vertex coordinates to duplicate row
		    if (0 < jy && jy < nVertY - 1)
			memcpy(v + nVertX * vSize, v, 3 * sizeof(GLfloat));

		    nVertices++;

		    // increment x properly (so that coordinates fall on grid intersections)
		    x = rightix * gridW + wx;

		    v += 3; // move on to next vertex
		}
		if (useTextureQ)
		    prevRowCellWidth = rowCellWidth;

		if (0 < jy && jy < nVertY - 1)
		{
		    v += nVertX * vSize;	// skip the duplicate row
		    nVertices += nVertX;
		}
		// increment y properly (so that coordinates fall on grid intersections)
		if (aw->com.curWindowEvent == WindowEventShade
		    || aw->com.curWindowEvent == WindowEventUnshade)
		{
		    y += gridH;
		}
		else
		{
		    y = bottomiy * gridH + wy;
		}
	    }
	}
	w->vCount = nVertices;
	w->indexCount = nIndices;
	if (awRegion)
	{
	    XDestroyRegion(awRegion);
	    awRegion = NULL;
	}
    }
    else
    {
	UNWRAP(as, w->screen, addWindowGeometry);
	(*w->screen->addWindowGeometry) (w, matrix, nMatrix, region, clip);
	WRAP(as, w->screen, addWindowGeometry, animAddWindowGeometry);
    }
}

static void
animDrawWindowTexture(CompWindow * w, CompTexture * texture,
		      const FragmentAttrib *attrib,
		      unsigned int mask)
{
    ANIM_WINDOW(w);
    ANIM_SCREEN(w->screen);

    if (aw->com.animRemainingTime > 0)	// if animation in progress, store texture
    {
	aw->com.curPaintAttrib = *attrib;
    }

    UNWRAP(as, w->screen, drawWindowTexture);
    (*w->screen->drawWindowTexture) (w, texture, attrib, mask);
    WRAP(as, w->screen, drawWindowTexture, animDrawWindowTexture);
}

void
animDrawWindowGeometry(CompWindow * w)
{
    ANIM_WINDOW (w);

    if (aw->com.curAnimEffect->properties.drawCustomGeometryFunc)
    {
	aw->com.curAnimEffect->properties.drawCustomGeometryFunc (w);
	return;
    }
    int texUnit = w->texUnits;
    int currentTexUnit = 0;
    int stride = 3 + texUnit * w->texCoordSize;
    GLfloat *vertices = w->vertices + (stride - 3);

    stride *= sizeof(GLfloat);

    glVertexPointer(3, GL_FLOAT, stride, vertices);

    while (texUnit--)
    {
	if (texUnit != currentTexUnit)
	{
	    w->screen->clientActiveTexture(GL_TEXTURE0_ARB + texUnit);
	    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	    currentTexUnit = texUnit;
	}
	vertices -= w->texCoordSize;
	glTexCoordPointer(w->texCoordSize, GL_FLOAT, stride, vertices);
    }

    glDrawElements(GL_QUADS, w->indexCount, GL_UNSIGNED_SHORT,
		   w->indices);

    // disable all texture coordinate arrays except 0
    texUnit = w->texUnits;
    if (texUnit > 1)
    {
	while (--texUnit)
	{
	    (*w->screen->clientActiveTexture) (GL_TEXTURE0_ARB + texUnit);
	    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	(*w->screen->clientActiveTexture) (GL_TEXTURE0_ARB);
    }
}

static Bool
animPaintWindow(CompWindow * w,
		const WindowPaintAttrib * attrib,
		const CompTransform    *transform,
		Region region, unsigned int mask)
{
    Bool status;

    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    if (aw->com.animRemainingTime > 0)
    {
	if (!as->animInProgress)
	{
	    // This window shouldn't really be undergoing animation,
	    // because it won't make progress with false as->animInProgress.
	    postAnimationCleanup (w);

	    UNWRAP(as, w->screen, paintWindow);
	    status = (*w->screen->paintWindow) (w, attrib, transform, region,
						mask);
	    WRAP(as, w->screen, paintWindow, animPaintWindow);

	    return status;
	}
	if (aw->com.curAnimEffect == AnimEffectDodge &&
	    aw->isDodgeSubject &&
	    aw->walkerOverNewCopy)
	{
	    // if aw is to be painted somewhere other than in its
	    // original stacking order, we don't
	    // need to paint it now
	    return FALSE;
	}
	if (aw->com.curWindowEvent == WindowEventFocus && otherPluginsActive(as))
	    postAnimationCleanup (w);

	WindowPaintAttrib wAttrib = *attrib;
	CompTransform wTransform = *transform;

	if (aw->com.curAnimEffect->properties.addCustomGeometryFunc)
	{
	    // Use slightly smaller brightness to force core
	    // to handle <max saturation case with <max brightness.
	    // Otherwise polygon effects show fully unsaturated colors
	    // in that case.
	    wAttrib.brightness = MAX (0, wAttrib.brightness - 1);
	}
	w->indexCount = 0;

	// TODO: should only happen for distorting effects
	mask |= PAINT_WINDOW_TRANSFORMED_MASK;

	wAttrib.xScale = 1.0f;
	wAttrib.yScale = 1.0f;

	if (aw->com.curAnimEffect->properties.updateWindowAttribFunc)
	    aw->com.curAnimEffect->properties.
		updateWindowAttribFunc (w, &wAttrib);

	if (aw->com.curAnimEffect->properties.updateWinTransformFunc)
	    aw->com.curAnimEffect->properties.
		updateWinTransformFunc (w, &wTransform);

	if (aw->com.curAnimEffect->properties.prePaintWindowFunc)
	    aw->com.curAnimEffect->properties.prePaintWindowFunc (w);

	UNWRAP(as, w->screen, paintWindow);
	status = (*w->screen->paintWindow) (w, &wAttrib, &wTransform, region, mask);
	WRAP(as, w->screen, paintWindow, animPaintWindow);

	if (aw->com.curAnimEffect->properties.postPaintWindowFunc)
	{
	    // Transform to make post-paint coincide with the window
	    glPushMatrix ();
	    glLoadMatrixf (wTransform.m);

	    aw->com.curAnimEffect->properties.postPaintWindowFunc (w);

	    glPopMatrix ();
	}
    }
    else
    {
	UNWRAP(as, w->screen, paintWindow);
	status = (*w->screen->paintWindow) (w, attrib, transform, region, mask);
	WRAP(as, w->screen, paintWindow, animPaintWindow);
    }

    return status;
}

// Go to the bottommost window in this "focus chain"
// This chain is used to handle some cases: e.g when Find dialog
// of an app is open, both windows should be faded when the Find
// dialog is raised.
static CompWindow*
getBottommostInFocusChain (CompWindow *w)
{
    if (!w)
	return w;

    ANIM_WINDOW (w);
    ANIM_SCREEN (w->screen);

    CompWindow *bottommost = aw->winToBePaintedBeforeThis;

    if (!bottommost || bottommost->destroyed)
	return w;

    AnimWindow *awBottommost = GET_ANIM_WINDOW (bottommost, as);
    CompWindow *wPrev = NULL;

    if (awBottommost)
	wPrev = awBottommost->moreToBePaintedPrev;
    while (wPrev)
    {
	bottommost = wPrev;
	wPrev = GET_ANIM_WINDOW(wPrev, as)->moreToBePaintedPrev;
    }
    return bottommost;
}

static void
resetWalkerMarks (CompScreen *s)
{
    CompWindow *w;
    for (w = s->windows; w; w = w->next)
    {
	ANIM_WINDOW(w);
	aw->walkerOverNewCopy = FALSE;
	aw->walkerVisitCount = 0;
    }
}

static CompWindow*
animWalkFirst (CompScreen *s)
{
    ANIM_SCREEN (s);

    resetWalkerMarks (s);

    CompWindow *w = getBottommostInFocusChain(s->windows);
    if (w)
    {
	AnimWindow *aw = GET_ANIM_WINDOW (w, as);
	aw->walkerVisitCount++;
    }
    return w;
}

static CompWindow*
animWalkLast (CompScreen *s)
{
    ANIM_SCREEN (s);

    resetWalkerMarks (s);

    CompWindow *w = s->reverseWindows;
    if (w)
    {
	AnimWindow *aw = GET_ANIM_WINDOW (w, as);
	aw->walkerVisitCount++;
    }
    return w;
}

static Bool
markNewCopy (CompWindow *w)
{
    ANIM_WINDOW (w);

    // if window is in a focus chain
    if (aw->winThisIsPaintedBefore ||
	aw->moreToBePaintedPrev)
    {
	aw->walkerOverNewCopy = TRUE;
	return TRUE;
    }
    return FALSE;
}

static CompWindow*
animWalkNext (CompWindow *w)
{
    ANIM_WINDOW (w);
    CompWindow *wRet = NULL;

    if (!aw->walkerOverNewCopy)
    {
	// Within a chain? (not the 1st or 2nd window)
	if (aw->moreToBePaintedNext)
	    wRet = aw->moreToBePaintedNext;
	else if (aw->winThisIsPaintedBefore) // 2nd one in chain?
	    wRet = aw->winThisIsPaintedBefore;
    }
    else
	aw->walkerOverNewCopy = FALSE;

    if (!wRet && w->next && markNewCopy (w->next))
	wRet = w->next;
    else if (!wRet)
	wRet = getBottommostInFocusChain(w->next);

    if (wRet)
    {
	ANIM_SCREEN (w->screen);

	AnimWindow *awRet = GET_ANIM_WINDOW (wRet, as);
	// Prevent cycles, which cause freezes
	if (awRet->walkerVisitCount > 1) // each window is visited at most twice
	    return NULL;
	awRet->walkerVisitCount++;
    }
    return wRet;
}

static CompWindow*
animWalkPrev (CompWindow *w)
{
    ANIM_WINDOW (w);
    CompWindow *wRet = NULL;

    // Focus chain start?
    CompWindow *w2 = aw->winToBePaintedBeforeThis;
    if (w2)
	wRet = w2;
    else if (!aw->walkerOverNewCopy)
    {
	// Within a focus chain? (not the last window)
	CompWindow *wPrev = aw->moreToBePaintedPrev;
	if (wPrev)
	    wRet = wPrev;
	else if (aw->winThisIsPaintedBefore) // Focus chain end?
	    // go to the chain beginning and get the
	    // prev. in X stacking order
	{
	    if (aw->winThisIsPaintedBefore->prev)
		markNewCopy (aw->winThisIsPaintedBefore->prev);

	    wRet = aw->winThisIsPaintedBefore->prev;
	}
    }
    else
	aw->walkerOverNewCopy = FALSE;

    if (!wRet && w->prev)
	markNewCopy (w->prev);

    wRet = w->prev;
    if (wRet)
    {
	ANIM_SCREEN (w->screen);

	AnimWindow *awRet = GET_ANIM_WINDOW (wRet, as);
	// Prevent cycles, which cause freezes
	if (awRet->walkerVisitCount > 1) // each window is visited at most twice
	    return NULL;
	awRet->walkerVisitCount++;
    }
    return wRet;
}

static void
animInitWindowWalker (CompScreen *s,
		      CompWalker *walker)
{
    ANIM_SCREEN (s);

    UNWRAP (as, s, initWindowWalker);
    (*s->initWindowWalker) (s, walker);
    WRAP (as, s, initWindowWalker, animInitWindowWalker);

    if (as->walkerAnimCount > 0) // only walk if necessary
    {
	if (!as->animInProgress) // just in case
	{
	    as->walkerAnimCount = 0;
	    return;
	}
	walker->first = animWalkFirst;
	walker->last  = animWalkLast;
	walker->next  = animWalkNext;
	walker->prev  = animWalkPrev;
    }
}

static void animHandleCompizEvent(CompDisplay * d, const char *pluginName,
				  const char *eventName, CompOption * option,
				  int nOption)
{
    ANIM_DISPLAY(d);

    UNWRAP (ad, d, handleCompizEvent);
    (*d->handleCompizEvent) (d, pluginName, eventName, option, nOption);
    WRAP (ad, d, handleCompizEvent, animHandleCompizEvent);

    int i;
    for (i = 0; i < NUM_WATCHED_PLUGINS; i++)
	if (strcmp(pluginName, watchedPlugins[i].pluginName) == 0)
	{
	    if (strcmp(eventName, watchedPlugins[i].activateEventName) == 0)
	    {
		Window xid = getIntOptionNamed(option, nOption, "root", 0);
		CompScreen *s = findScreenAtDisplay(d, xid);

		if (s)
		{
		    ANIM_SCREEN(s);
		    as->pluginActive[i] =
			getBoolOptionNamed(option, nOption, "active", FALSE);
		    if (i < NUM_SWITCHERS) // if it's a switcher plugin
		    {
			if (!as->pluginActive[i])
			    switcherPostWait = 1;
		    }
		}
	    }
	    break;
	}
}

static void
updateLastClientListStacking(CompScreen *s)
{
    ANIM_SCREEN(s);
    int n = s->nClientList;
    Window *clientListStacking = (Window *) (s->clientList + n) + n;

    if (as->nLastClientListStacking != n) // the number of windows has changed
    {
	Window *list;

	list = realloc (as->lastClientListStacking, sizeof (Window) * n);
	as->lastClientListStacking  = list;

	if (!list)
	{
	    as->nLastClientListStacking = 0;
	    return;
	}

	as->nLastClientListStacking = n;
    }

    // Store new client stack listing
    memcpy(as->lastClientListStacking, clientListStacking,
	   sizeof (Window) * n);
}

// Returns true for windows that don't have a pixmap or certain properties,
// like the dimming layer of gksudo and x-session-manager
static inline Bool
shouldIgnoreForAnim (CompWindow *w, Bool checkPixmap)
{
    ANIM_DISPLAY (w->screen->display);

    return ((checkPixmap && !w->texture->pixmap) ||
	    matchEval (&ad->neverAnimateMatch, w));
}

static void animHandleEvent(CompDisplay * d, XEvent * event)
{
    CompWindow *w;

    ANIM_DISPLAY(d);

    switch (event->type)
    {
    case PropertyNotify:
	if (event->xproperty.atom == d->clientListStackingAtom)
	{
	    CompScreen *s = findScreenAtDisplay (d, event->xproperty.window);
	    if (s)
		updateLastClientListStacking(s);
	}
	break;
    case MapNotify:
	w = findWindowAtDisplay(d, event->xmap.window);
	if (w)
	{
	    ANIM_WINDOW(w);

	    if (aw->com.animRemainingTime > 0)
	    {
		aw->state = aw->newState;
	    }
	    aw->ignoreDamage = TRUE;
	    while (aw->unmapCnt)
	    {
		unmapWindow(w);
		aw->unmapCnt--;
	    }
	    aw->ignoreDamage = FALSE;
	}
	break;
    case DestroyNotify:
	w = findWindowAtDisplay(d, event->xdestroywindow.window);
	if (w)
	{
	    ANIM_WINDOW(w);
	    int duration;

	    if (shouldIgnoreForAnim (w, TRUE))
		break;

	    if (AnimEffectNone ==
		getMatchingAnimSelection (w, AnimEventClose, &duration))
		break;

	    aw->destroyCnt++;
	    w->destroyRefCnt++;
	    addWindowDamage(w);
	}
	break;
    case UnmapNotify:
	w = findWindowAtDisplay(d, event->xunmap.window);
	if (w)
	{
	    ANIM_SCREEN(w->screen);

	    if (w->pendingUnmaps && onCurrentDesktop(w)) // Normal -> Iconic
	    {
		ANIM_WINDOW(w);
		int duration = 200;
		AnimEffect chosenEffect =
		    getMatchingAnimSelection (w, AnimEventShade, &duration);

		if (w->shaded)
		{
		    // SHADE event!

		    aw->nowShaded = TRUE;

		    if (chosenEffect != AnimEffectNone)
		    {
			Bool startingNew = TRUE;

			if (aw->com.curWindowEvent != WindowEventNone)
			{
			    if (aw->com.curWindowEvent != WindowEventUnshade)
			    {
				postAnimationCleanupPrev (w, FALSE, FALSE);
			    }
			    else
			    {
				// Play the unshade effect backwards from where it left
				aw->com.animRemainingTime =
				    aw->com.animTotalTime -
				    aw->com.animRemainingTime;

				// avoid window remains
				if (aw->com.animRemainingTime <= 0)
				    aw->com.animRemainingTime = 1;

				startingNew = FALSE;
				if (aw->com.animOverrideProgressDir == 0)
				    aw->com.animOverrideProgressDir = 2;
				else if (aw->com.animOverrideProgressDir == 1)
				    aw->com.animOverrideProgressDir = 0;
			    }
			}

			if (startingNew)
			{
			    AnimEffect effectToBePlayed;
			    effectToBePlayed =
				animGetAnimEffect (as,
						   chosenEffect,
						   AnimEventShade);

			    // handle empty random effect list
			    if (effectToBePlayed == AnimEffectNone)
				break;

			    aw->com.curAnimEffect = effectToBePlayed;
			    aw->com.animTotalTime = duration;
			    aw->com.animRemainingTime = aw->com.animTotalTime;
			}

			animActivateEvent(w->screen, TRUE);
			aw->com.curWindowEvent = WindowEventShade;

			if (!animEnsureModel(w))
			{
			    postAnimationCleanup (w);
			}

			aw->unmapCnt++;
			w->unmapRefCnt++;

			damagePendingOnScreen (w->screen);
		    }
		}
		else if (!w->invisible && !w->hidden)
		{
		    // MINIMIZE event!

		    // Always reset stacking related info when a window is
		    // minimized.
		    resetStackingInfo (w->screen);

		    aw->newState = IconicState;

		    if (w->iconGeometrySet)
		    {
			aw->com.icon = w->iconGeometry;
		    }
		    else
		    {
			// Minimize to mouse pointer if there is no
			// window list or if the window skips taskbar
			if (!getMousePointerXY (w->screen,
						&aw->com.icon.x,
						&aw->com.icon.y))
			{
			    // Use screen center if can't get mouse coords
			    aw->com.icon.x = w->screen->width / 2;
			    aw->com.icon.y = w->screen->height / 2;
			}
			aw->com.icon.width = FAKE_ICON_SIZE;
			aw->com.icon.height = FAKE_ICON_SIZE;
		    }

		    chosenEffect =
			getMatchingAnimSelection (w, AnimEventMinimize, &duration);

		    if (chosenEffect != AnimEffectNone)
		    {
			Bool startingNew = TRUE;

			if (aw->com.curWindowEvent != WindowEventNone)
			{
			    if (aw->com.curWindowEvent != WindowEventUnminimize)
			    {
				postAnimationCleanupPrev (w, FALSE, FALSE);
			    }
			    else
			    {
				// Play the unminimize effect backwards from where it left
				aw->com.animRemainingTime =
				    aw->com.animTotalTime - aw->com.animRemainingTime;

				// avoid window remains
				if (aw->com.animRemainingTime == 0)
				    aw->com.animRemainingTime = 1;

				startingNew = FALSE;
				if (aw->com.animOverrideProgressDir == 0)
				    aw->com.animOverrideProgressDir = 2;
				else if (aw->com.animOverrideProgressDir == 1)
				    aw->com.animOverrideProgressDir = 0;
			    }
			}

			if (startingNew)
			{
			    AnimEffect effectToBePlayed;
			    effectToBePlayed =
				animGetAnimEffect (as,
						   chosenEffect,
						   AnimEventMinimize);

			    // handle empty random effect list
			    if (effectToBePlayed == AnimEffectNone)
			    {
				aw->state = aw->newState;
				break;
			    }
			    aw->com.curAnimEffect = effectToBePlayed;
			    aw->com.animTotalTime = duration;
			    aw->com.animRemainingTime = aw->com.animTotalTime;
			}

			animActivateEvent(w->screen, TRUE);
			aw->com.curWindowEvent = WindowEventMinimize;

			if (!animEnsureModel(w))
			{
			    postAnimationCleanup (w);
			}
			else
			{
			    aw->unmapCnt++;
			    w->unmapRefCnt++;

			    damagePendingOnScreen (w->screen);
			}
		    }
		    else
		        aw->state = aw->newState;
		}
	    }
	    else				// X -> Withdrawn
	    {
		ANIM_WINDOW(w);
		int duration = 200;

		// Always reset stacking related info when a window is closed.
		resetStackingInfo (w->screen);

		if (shouldIgnoreForAnim (w, TRUE) ||
		    otherPluginsActive (as))
		    break;

		AnimEffect chosenEffect =
		    getMatchingAnimSelection (w, AnimEventClose, &duration);

		// CLOSE event!

		aw->state = NormalState;
		aw->newState = WithdrawnState;

		if (chosenEffect != AnimEffectNone)
		{
		    int tmpSteps = 0;
		    Bool startingNew = TRUE;

		    if (aw->com.animRemainingTime > 0 &&
			aw->com.curWindowEvent != WindowEventOpen)
		    {
			tmpSteps = aw->com.animRemainingTime;
			aw->com.animRemainingTime = 0;
		    }
		    if (aw->com.curWindowEvent != WindowEventNone)
		    {
			if (aw->com.curWindowEvent == WindowEventOpen)
			{
			    // Play the create effect backward from where it left
			    aw->com.animRemainingTime =
				aw->com.animTotalTime - aw->com.animRemainingTime;

			    // avoid window remains
			    if (aw->com.animRemainingTime <= 0)
				aw->com.animRemainingTime = 1;

			    startingNew = FALSE;
			    if (aw->com.animOverrideProgressDir == 0)
				aw->com.animOverrideProgressDir = 2;
			    else if (aw->com.animOverrideProgressDir == 1)
				aw->com.animOverrideProgressDir = 0;
			}
			else if (aw->com.curWindowEvent == WindowEventClose)
			{
			    if (aw->com.animOverrideProgressDir == 2)
			    {
				aw->com.animRemainingTime = tmpSteps;
				startingNew = FALSE;
			    }
			}
			else
			{
			    postAnimationCleanupPrev (w, TRUE, FALSE);
			}
		    }

		    if (startingNew)
		    {
			AnimEffect effectToBePlayed;
			effectToBePlayed = animGetAnimEffect (as,
							      chosenEffect,
							      AnimEventClose);

			// handle empty random effect list
			if (effectToBePlayed == AnimEffectNone)
			{
			    aw->state = aw->newState;
			    break;
			}
			aw->com.curAnimEffect = effectToBePlayed;
			aw->com.animTotalTime = duration;
			aw->com.animRemainingTime = aw->com.animTotalTime;
		    }
		    animActivateEvent(w->screen, TRUE);
		    aw->com.curWindowEvent = WindowEventClose;

		    if (!animEnsureModel(w))
		    {
			postAnimationCleanupCustom (w, TRUE, FALSE, TRUE);
		    }
		    else if (getMousePointerXY
			     (w->screen, &aw->com.icon.x, &aw->com.icon.y))
		    {
			aw->com.icon.width = FAKE_ICON_SIZE;
			aw->com.icon.height = FAKE_ICON_SIZE;

			if (aw->com.curAnimEffect == AnimEffectMagicLamp)
			    aw->com.icon.width = 
				MAX(aw->com.icon.width,
				    animGetI (w, ANIM_SCREEN_OPTION_MAGIC_LAMP_OPEN_START_WIDTH));
			else if (aw->com.curAnimEffect == AnimEffectVacuum)
			    aw->com.icon.width =
				MAX(aw->com.icon.width,
				    animGetI (w, ANIM_SCREEN_OPTION_VACUUM_OPEN_START_WIDTH));

			aw->unmapCnt++;
			w->unmapRefCnt++;

			damagePendingOnScreen (w->screen);
		    }
		}
		else if (AnimEffectNone !=
			 getMatchingAnimSelection (w, AnimEventOpen, &duration))
		{
		    // stop the current animation and prevent it from rewinding

		    if (aw->com.animRemainingTime > 0 &&
			aw->com.curWindowEvent != WindowEventOpen)
		    {
			aw->com.animRemainingTime = 0;
		    }
		    if ((aw->com.curWindowEvent != WindowEventNone) &&
			(aw->com.curWindowEvent != WindowEventClose))
		    {
			postAnimationCleanupCustom (w, TRUE, FALSE, TRUE);
		    }
		    // set some properties to make sure this window will use the
		    // correct open effect the next time it's "opened"

		    animActivateEvent(w->screen, TRUE);
		    aw->com.curWindowEvent = WindowEventClose;

		    aw->unmapCnt++;
		    w->unmapRefCnt++;

		    damagePendingOnScreen (w->screen);
		}
		else
		    aw->state = aw->newState;
	    }
	}
	break;
    case ConfigureNotify:
    {
	XConfigureEvent *ce = &event->xconfigure;
	w = findWindowAtDisplay (d, ce->window);
	if (!w)
	    break;
	if (w->prev)
	{
	    if (ce->above && ce->above == w->prev->id)
		break;
	}
	else if (ce->above == None)
	    break;
	CompScreen *s = findScreenAtDisplay (d, event->xproperty.window);
	if (!s)
	    break;

	ANIM_SCREEN(s);
	int n = s->nClientList;
	Bool winOpenedClosed = FALSE;

	Window *clientList = (Window *) (s->clientList + n);
	Window *clientListStacking = clientList + n;

	if (n != as->nLastClientListStacking)
	    winOpenedClosed = TRUE;

	// if restacking occurred and not window open/close
	if (!winOpenedClosed)
	{
	    ANIM_WINDOW(w);
	    aw->configureNotified = TRUE;

	    // Find which window is restacked 
	    // e.g. here 8507730 was raised:
	    // 54526074 8507730 48234499 14680072 6291497
	    // 54526074 48234499 14680072 8507730 6291497
	    // compare first changed win. of row 1 with last
	    // changed win. of row 2, and vica versa
	    // the matching one is the restacked one
	    CompWindow *wRestacked = 0;
	    CompWindow *wStart = 0;
	    CompWindow *wEnd = 0;
	    CompWindow *wOldAbove = 0;
	    CompWindow *wChangeStart = 0;
	    CompWindow *wChangeEnd = 0;

	    Bool raised = FALSE;
	    int changeStart = -1;
	    int changeEnd = -1;

	    int i;
	    for (i = 0; i < n; i++)
	    {
		CompWindow *wi =
		    findWindowAtScreen (s, clientListStacking[i]);

		// skip if minimized (prevents flashing problem)
		if (!wi || !isWinVisible(wi))
		    continue;

		// skip if (tabbed and) hidden by Group plugin
		if (wi->state & (CompWindowStateSkipPagerMask |
				 CompWindowStateSkipTaskbarMask))
		    continue;

		if (clientListStacking[i] !=
		    as->lastClientListStacking[i])
		{
		    if (changeStart < 0)
		    {
			changeStart = i;
			wChangeStart = wi; // make use of already found w
		    }
		    else
		    {
			changeEnd = i;
			wChangeEnd = wi;
		    }
		}
		else if (changeStart >= 0) // found some change earlier
		    break;
	    }

	    // if restacking occurred
	    if (changeStart >= 0 && changeEnd >= 0)
	    {
		CompWindow *w2;

		// if we have only 2 windows changed, 
		// choose the one clicked on
		Bool preferRaised = FALSE;
		Bool onlyTwo = FALSE;

		if (wChangeEnd &&
		    clientListStacking[changeEnd] ==
		    as->lastClientListStacking[changeStart] &&
		    clientListStacking[changeStart] ==
		    as->lastClientListStacking[changeEnd])
		{
		    // Check if the window coming on top was
		    // configureNotified (clicked on)
		    AnimWindow *aw2 = GET_ANIM_WINDOW(wChangeEnd, as);
		    if (aw2->configureNotified)
		    {
			preferRaised = TRUE;
		    }
		    onlyTwo = TRUE;
		}
		// Clear all configureNotified's
		for (w2 = s->windows; w2; w2 = w2->next)
		{
		    AnimWindow *aw2 = GET_ANIM_WINDOW(w2, as);
		    aw2->configureNotified = FALSE;
		}

		if (preferRaised ||
		    (!onlyTwo &&
		     clientListStacking[changeEnd] ==
		     as->lastClientListStacking[changeStart]))
		{
		    // raised
		    raised = TRUE;
		    wRestacked = wChangeEnd;
		    wStart = wChangeStart;
		    wEnd = wRestacked;
		    wOldAbove = wStart;
		}
		else if (clientListStacking[changeStart] ==
			 as->lastClientListStacking[changeEnd] && // lowered
			 // We don't animate lowering if there is no
			 // window above this window, since this window needs
			 // to be drawn on such a "host" in animPaintWindow
			 // (at least for now).
			 changeEnd < n - 1)
		{
		    wRestacked = wChangeStart;
		    wStart = wRestacked;
		    wEnd = wChangeEnd;
		    wOldAbove = findWindowAtScreen
			(s, as->lastClientListStacking[changeEnd+1]);
		}
		for (; wOldAbove && !isWinVisible(wOldAbove);
		     wOldAbove = wOldAbove->next)
		    ;
	    }
	    if (wRestacked && wStart && wEnd && wOldAbove)
	    {
		AnimWindow *awRestacked = GET_ANIM_WINDOW(wRestacked, as);
		if (awRestacked->created)
		{
		    RestackInfo *restackInfo = calloc(1, sizeof(RestackInfo));
		    if (restackInfo)
		    {
			restackInfo->wRestacked = wRestacked;
			restackInfo->wStart = wStart;
			restackInfo->wEnd = wEnd;
			restackInfo->wOldAbove = wOldAbove;
			restackInfo->raised = raised;

			if (awRestacked->restackInfo)
			    free(awRestacked->restackInfo);

			awRestacked->restackInfo = restackInfo;
			as->aWinWasRestackedJustNow = TRUE;
		    }
		}
	    }
	}
	updateLastClientListStacking(s);
    }
    break;
    default:
	break;
    }

    UNWRAP(ad, d, handleEvent);
    (*d->handleEvent) (d, event);
    WRAP(ad, d, handleEvent, animHandleEvent);

    switch (event->type)
    {
    case PropertyNotify:
	if (event->xproperty.atom == d->winActiveAtom &&
	    d->activeWindow != ad->activeWindow)
	{
	    ad->activeWindow = d->activeWindow;
	    w = findWindowAtDisplay(d, d->activeWindow);

	    if (w)
	    {
		int duration = 200;
		AnimEffect chosenEffect =
		    getMatchingAnimSelection (w, AnimEventFocus, &duration);

		if (!(chosenEffect == AnimEffectFocusFade ||
		      chosenEffect == AnimEffectDodge))
		    initiateFocusAnimation(w);
	    }
	}
	break;
    case MapRequest:
	w = findWindowAtDisplay (d, event->xmaprequest.window);
	if (w && w->hints && w->hints->initial_state == IconicState)
	{
	    ANIM_WINDOW (w);
	    aw->state = aw->newState = IconicState;
	}
	break;
    default:
	break;
    }
}

static Bool animDamageWindowRect(CompWindow * w, Bool initial, BoxPtr rect)
{
    Bool status;

    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    if (aw->ignoreDamage)
	return TRUE; // if doing the unmap at animation's end, ignore the damage

    if (initial)				// Unminimize or Open
    {
	int duration = 200;
	AnimEffect chosenEffect;

	if (aw->state == IconicState)
	{
	    chosenEffect =
		getMatchingAnimSelection (w, AnimEventMinimize, &duration);

	    // UNMINIMIZE event!

	    if (!w->invisible && !w->hidden &&
		chosenEffect != AnimEffectNone &&
		!as->pluginActive[3]) // fadedesktop
	    {
		Bool startingNew = TRUE;
		Bool playEffect = TRUE;

		// Always reset stacking related info when a window is
		// unminimized.
		resetStackingInfo (w->screen);

		if (aw->com.curWindowEvent != WindowEventNone)
		{
		    if (aw->com.curWindowEvent != WindowEventMinimize)
		    {
			postAnimationCleanupPrev (w, FALSE, FALSE);
		    }
		    else
		    {
			// Play the minimize effect backwards from where it left
			aw->com.animRemainingTime =
			    aw->com.animTotalTime - aw->com.animRemainingTime;

			// avoid window remains
			if (aw->com.animRemainingTime <= 0)
			    aw->com.animRemainingTime = 1;

			startingNew = FALSE;
			if (aw->com.animOverrideProgressDir == 0)
			    aw->com.animOverrideProgressDir = 1;
			else if (aw->com.animOverrideProgressDir == 2)
			    aw->com.animOverrideProgressDir = 0;
		    }
		}

		if (startingNew)
		{
		    AnimEffect effectToBePlayed;
		    effectToBePlayed = animGetAnimEffect (as,
							  chosenEffect,
							  AnimEventMinimize);

		    // handle empty random effect list
		    if (effectToBePlayed == AnimEffectNone)
			playEffect = FALSE;

		    if (playEffect)
		    {
			aw->com.curAnimEffect = effectToBePlayed;
			aw->com.animTotalTime = duration;
			aw->com.animRemainingTime = aw->com.animTotalTime;
		    }
		}

		if (playEffect)
		{
		    animActivateEvent(w->screen, TRUE);
		    aw->com.curWindowEvent = WindowEventUnminimize;

		    if (animEnsureModel(w))
		    {
			if (w->iconGeometrySet)
			{
			    aw->com.icon = w->iconGeometry;
			}
			else
			{
			    // Unminimize from mouse pointer if there is no
			    // window list or if the window skips taskbar
			    if (!getMousePointerXY (w->screen,
						    &aw->com.icon.x,
						    &aw->com.icon.y))
			    {
				// Use screen center if can't get mouse coords
				aw->com.icon.x = w->screen->width / 2;
				aw->com.icon.y = w->screen->height / 2;
			    }
			    aw->com.icon.width = FAKE_ICON_SIZE;
			    aw->com.icon.height = FAKE_ICON_SIZE;
			}

			damagePendingOnScreen (w->screen);
		    }
		    else
		    {
			postAnimationCleanup (w);
		    }
		}
	    }
	}
	else if (aw->nowShaded)
	{
	    chosenEffect =
		getMatchingAnimSelection (w, AnimEventShade, &duration);

	    // UNSHADE event!

	    aw->nowShaded = FALSE;

	    if (chosenEffect != AnimEffectNone)
	    {
		Bool startingNew = TRUE;
		Bool playEffect = TRUE;

		if (aw->com.curWindowEvent != WindowEventNone)
		{
		    if (aw->com.curWindowEvent != WindowEventShade)
		    {
			postAnimationCleanupPrev (w, FALSE, FALSE);
		    }
		    else
		    {
			// Play the shade effect backwards from where it left
			aw->com.animRemainingTime =
			    aw->com.animTotalTime - aw->com.animRemainingTime;

			// avoid window remains
			if (aw->com.animRemainingTime <= 0)
			    aw->com.animRemainingTime = 1;

			startingNew = FALSE;
			if (aw->com.animOverrideProgressDir == 0)
			    aw->com.animOverrideProgressDir = 1;
			else if (aw->com.animOverrideProgressDir == 2)
			    aw->com.animOverrideProgressDir = 0;
		    }
		}

		if (startingNew)
		{
		    AnimEffect effectToBePlayed;
		    effectToBePlayed = animGetAnimEffect (as,
							  chosenEffect,
							  AnimEventShade);

		    // handle empty random effect list
		    if (effectToBePlayed == AnimEffectNone)
			playEffect = FALSE;

		    if (playEffect)
		    {
			aw->com.curAnimEffect = effectToBePlayed;
			aw->com.animTotalTime = duration;
			aw->com.animRemainingTime = aw->com.animTotalTime;
		    }
		}

		if (playEffect)
		{
		    animActivateEvent(w->screen, TRUE);
		    aw->com.curWindowEvent = WindowEventUnshade;

		    if (animEnsureModel(w))
			damagePendingOnScreen (w->screen);
		    else
			postAnimationCleanup (w);
		}
	    }
	}
	else if (!w->invisible && as->startCountdown == 0)
	{
	    AnimEffect chosenEffect;
	    int duration = 200;

	    // Always reset stacking related info when a window is opened.
	    resetStackingInfo (w->screen);

	    aw->created = TRUE;

	    // OPEN event!

	    if (!otherPluginsActive (as) &&
		!shouldIgnoreForAnim (w, FALSE) &&
		AnimEffectNone !=
		(chosenEffect =
		 getMatchingAnimSelection (w, AnimEventOpen, &duration)) &&
		getMousePointerXY(w->screen, &aw->com.icon.x, &aw->com.icon.y))
	    {
		Bool startingNew = TRUE;
		Bool playEffect = TRUE;

		if (aw->com.curWindowEvent != WindowEventNone)
		{
		    if (aw->com.curWindowEvent != WindowEventClose)
		    {
			postAnimationCleanupPrev (w, FALSE, FALSE);
		    }
		    else
		    {
			// Play the close effect backwards from where it left
			aw->com.animRemainingTime =
			    aw->com.animTotalTime - aw->com.animRemainingTime;

			// avoid window remains
			if (aw->com.animRemainingTime == 0)
			    aw->com.animRemainingTime = 1;

			startingNew = FALSE;
			if (aw->com.animOverrideProgressDir == 0)
			    aw->com.animOverrideProgressDir = 1;
			else if (aw->com.animOverrideProgressDir == 2)
			    aw->com.animOverrideProgressDir = 0;
		    }
		}

		if (startingNew)
		{
		    AnimEffect effectToBePlayed;
		    effectToBePlayed = animGetAnimEffect (as,
							  chosenEffect,
							  AnimEventOpen);

		    // handle empty random effect list
		    if (effectToBePlayed == AnimEffectNone)
			playEffect = FALSE;

		    if (playEffect)
		    {
			aw->com.curAnimEffect = effectToBePlayed;
			aw->com.animTotalTime = duration;
			aw->com.animRemainingTime = aw->com.animTotalTime;
		    }
		}

		if (playEffect)
		{
		    animActivateEvent(w->screen, TRUE);
		    aw->com.curWindowEvent = WindowEventOpen;

		    aw->com.icon.width = FAKE_ICON_SIZE;
		    aw->com.icon.height = FAKE_ICON_SIZE;

		    if (aw->com.curAnimEffect == AnimEffectMagicLamp)
			aw->com.icon.width = 
			    MAX(aw->com.icon.width,
				animGetI (w, ANIM_SCREEN_OPTION_MAGIC_LAMP_OPEN_START_WIDTH));
		    else if (aw->com.curAnimEffect == AnimEffectVacuum)
			aw->com.icon.width =
			    MAX(aw->com.icon.width,
				animGetI (w, ANIM_SCREEN_OPTION_VACUUM_OPEN_START_WIDTH));

		    aw->com.icon.x -= aw->com.icon.width / 2;
		    aw->com.icon.y -= aw->com.icon.height / 2;

		    if (animEnsureModel(w))
			damagePendingOnScreen (w->screen);
		    else
			postAnimationCleanup (w);
		}
	    }
	}

	aw->newState = NormalState;
    }

    UNWRAP(as, w->screen, damageWindowRect);
    status = (*w->screen->damageWindowRect) (w, initial, rect);
    WRAP(as, w->screen, damageWindowRect, animDamageWindowRect);

    return status;
}

static void animWindowResizeNotify(CompWindow * w, int dx, int dy, int dwidth, int dheight)
{
    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    // Don't let transient window open anim be interrupted with a resize notify
    if (!(aw->com.curWindowEvent == WindowEventOpen &&
	  (w->wmType &
	   (CompWindowTypeDropdownMenuMask |
	    CompWindowTypePopupMenuMask |
       	    CompWindowTypeMenuMask |
	    CompWindowTypeTooltipMask |
	    CompWindowTypeNotificationMask |
	    CompWindowTypeComboMask |
	    CompWindowTypeDndMask))))
    {
	if (aw->com.curAnimEffect->properties.refreshFunc)
	    aw->com.curAnimEffect->properties.refreshFunc (w, aw->animInitialized);

	if (aw->com.animRemainingTime > 0)
	{
	    aw->com.animRemainingTime = 0;
	    postAnimationCleanup (w);
	}
    }

    if (aw->com.model)
    {
	modelInitObjects(aw->com.model, 
			 WIN_X(w), WIN_Y(w), 
			 WIN_W(w), WIN_H(w));
    }

    UNWRAP(as, w->screen, windowResizeNotify);
    (*w->screen->windowResizeNotify) (w, dx, dy, dwidth, dheight);
    WRAP(as, w->screen, windowResizeNotify, animWindowResizeNotify);
}

static void
animWindowMoveNotify(CompWindow * w, int dx, int dy, Bool immediate)
{
    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    if (!immediate)
    {
	if (!(aw->com.animRemainingTime > 0 &&
	      (aw->com.curAnimEffect == AnimEffectFocusFade ||
	       aw->com.curAnimEffect == AnimEffectDodge)))
	{
	    CompWindow *w2;

	    if (aw->com.curAnimEffect->properties.refreshFunc)
		aw->com.curAnimEffect->properties.refreshFunc
		    (w, aw->animInitialized);

	    if (aw->com.animRemainingTime > 0 && aw->grabbed)
	    {
		aw->com.animRemainingTime = 0;
		if (as->animInProgress)
		{
		    Bool animStillInProgress = FALSE;
		    for (w2 = w->screen->windows; w2; w2 = w2->next)
		    {
			AnimWindow *aw2;

			aw2 = GET_ANIM_WINDOW(w2, as);
			if (aw2->com.animRemainingTime > 0)
			{
			    animStillInProgress = TRUE;
			    break;
			}
		    }

		    if (!animStillInProgress)
			animActivateEvent(w->screen, FALSE);
		}
		postAnimationCleanup (w);
	    }

	    if (aw->com.model)
	    {
		modelInitObjects(aw->com.model, WIN_X(w), WIN_Y(w), WIN_W(w),
				 WIN_H(w));
	    }
	}
    }
    else if (aw->com.model)
	modelMove (aw->com.model, dx, dy);

    UNWRAP(as, w->screen, windowMoveNotify);
    (*w->screen->windowMoveNotify) (w, dx, dy, immediate);
    WRAP(as, w->screen, windowMoveNotify, animWindowMoveNotify);
}

static void
animWindowGrabNotify(CompWindow * w,
		     int x, int y, unsigned int state, unsigned int mask)
{
    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    aw->grabbed = TRUE;

    UNWRAP(as, w->screen, windowGrabNotify);
    (*w->screen->windowGrabNotify) (w, x, y, state, mask);
    WRAP(as, w->screen, windowGrabNotify, animWindowGrabNotify);
}

static void animWindowUngrabNotify(CompWindow * w)
{
    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    aw->grabbed = FALSE;

    UNWRAP(as, w->screen, windowUngrabNotify);
    (*w->screen->windowUngrabNotify) (w);
    WRAP(as, w->screen, windowUngrabNotify, animWindowUngrabNotify);
}

static Bool
animPaintOutput(CompScreen * s,
		const ScreenPaintAttrib * sAttrib,
		const CompTransform    *transform,
		Region region, CompOutput *output, 
		unsigned int mask)
{
    Bool status;

    ANIM_SCREEN(s);

    if (as->animInProgress)
    {
	int p;
	for (p = 0; p < as->nExtensionPlugins; p++)
	{
	    const ExtensionPluginInfo *extPlugin = as->extensionPlugins[p];
	    if (extPlugin->prePaintOutputFunc)
		extPlugin->prePaintOutputFunc (s, output);
	}

	mask |= PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK;
    }

    as->output = output;

    UNWRAP(as, s, paintOutput);
    status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
    WRAP(as, s, paintOutput, animPaintOutput);

    CompWindow *w;
    if (as->aWinWasRestackedJustNow)
    {
	as->aWinWasRestackedJustNow = FALSE;
    }
    if (as->startCountdown > 0)
    {
	as->startCountdown--;
	if (as->startCountdown == 0)
	{
	    // Mark all windows as "created"
	    for (w = s->windows; w; w = w->next)
	    {
		ANIM_WINDOW(w);
		aw->created = TRUE;
	    }
	}
    }

    return status;
}

static const CompMetadataOptionInfo animDisplayOptionInfo[] = {
    { "abi", "int", 0, 0, 0 },
    { "index", "int", 0, 0, 0 }
};

static CompOption *
animGetDisplayOptions (CompPlugin  *plugin,
		       CompDisplay *display,
		       int         *count)
{
    ANIM_DISPLAY (display);
    *count = NUM_OPTIONS (ad);
    return ad->opt;
}

static Bool
animSetDisplayOption (CompPlugin      *plugin,
		      CompDisplay     *display,
		      const char      *name,
		      CompOptionValue *value)
{
    CompOption      *o;
    int	            index;
    ANIM_DISPLAY (display);
    o = compFindOption (ad->opt, NUM_OPTIONS (ad), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case ANIM_DISPLAY_OPTION_ABI:
    case ANIM_DISPLAY_OPTION_INDEX:
        break;
    default:
        return compSetDisplayOption (display, o, value);
    }

    return FALSE;
}

static AnimWindowCommon *
getAnimWindowCommon (CompWindow *w)
{
    ANIM_WINDOW (w);

    return &aw->com;
}

AnimBaseFunctions animBaseFunctions =
{
    .addExtension		= animAddExtension,
    .removeExtension		= animRemoveExtension,
    .getPluginOptVal		= animGetPluginOptVal,
    .getMousePointerXY		= getMousePointerXY,
    .defaultAnimInit		= defaultAnimInit,
    .defaultAnimStep		= defaultAnimStep,
    .defaultUpdateWindowTransform = defaultUpdateWindowTransform,
    .getProgressAndCenter	= getProgressAndCenter,
    .defaultAnimProgress	= defaultAnimProgress,
    .sigmoidAnimProgress	= sigmoidAnimProgress,
    .decelerateProgressCustom	= decelerateProgressCustom,
    .decelerateProgress		= decelerateProgress,
    .updateBBScreen		= updateBBScreen,
    .updateBBWindow		= updateBBWindow,
    .modelUpdateBB		= modelUpdateBB,
    .compTransformUpdateBB	= compTransformUpdateBB,
    .getActualAnimDirection	= getActualAnimDirection,
    .expandBoxWithBox		= expandBoxWithBox,
    .expandBoxWithPoint		= expandBoxWithPoint,
    .prepareTransform		= prepareTransform,
    .getAnimWindowCommon	= getAnimWindowCommon,
    .returnTrue			= returnTrue,
    .postAnimationCleanup	= postAnimationCleanup,
    .fxZoomUpdateWindowAttrib	= fxZoomUpdateWindowAttrib
};

static Bool animInitDisplay(CompPlugin * p, CompDisplay * d)
{
    AnimDisplay *ad;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    ad = calloc(1, sizeof(AnimDisplay));
    if (!ad)
	return FALSE;

    if (!compInitDisplayOptionsFromMetadata (d,
					     &animMetadata,
					     animDisplayOptionInfo,
					     ad->opt,
					     ANIM_DISPLAY_OPTION_NUM))
    {
	free (ad);
	return FALSE;
    }

    ad->screenPrivateIndex = allocateScreenPrivateIndex(d);
    if (ad->screenPrivateIndex < 0)
    {
	free(ad);
	return FALSE;
    }

    // Never animate screen-dimming layer of logout window and gksu.
    matchInit (&ad->neverAnimateMatch);
    matchAddExp (&ad->neverAnimateMatch, 0, "title=gksu");
    matchAddExp (&ad->neverAnimateMatch, 0, "title=x-session-manager");
    matchAddExp (&ad->neverAnimateMatch, 0, "title=gnome-session");
    matchUpdate (d, &ad->neverAnimateMatch);

    WRAP(ad, d, handleEvent, animHandleEvent);
    WRAP(ad, d, handleCompizEvent, animHandleCompizEvent);

    ad->opt[ANIM_DISPLAY_OPTION_ABI].value.i   = ANIMATION_ABIVERSION;
    ad->opt[ANIM_DISPLAY_OPTION_INDEX].value.i = animFunctionsPrivateIndex;

    d->base.privates[animDisplayPrivateIndex].ptr = ad;
    d->base.privates[animFunctionsPrivateIndex].ptr = &animBaseFunctions;

    return TRUE;
}

static void animFiniDisplay(CompPlugin * p, CompDisplay * d)
{
    ANIM_DISPLAY(d);

    freeScreenPrivateIndex(d, ad->screenPrivateIndex);

    matchFini (&ad->neverAnimateMatch);

    compFiniDisplayOptions (d, ad->opt, ANIM_DISPLAY_OPTION_NUM);

    UNWRAP(ad, d, handleCompizEvent);
    UNWRAP(ad, d, handleEvent);

    free(ad);
}

AnimEffect AnimEffectNone = &(AnimEffectInfo)
    {"animation:None",
     {TRUE, TRUE, TRUE, TRUE, TRUE}};

AnimEffect AnimEffectRandom = &(AnimEffectInfo)
    {"animation:Random",
     {TRUE, TRUE, TRUE, TRUE, FALSE}};

AnimEffect AnimEffectCurvedFold = &(AnimEffectInfo)
    {"animation:Curved Fold",
     {TRUE, TRUE, TRUE, TRUE, FALSE},
     {.updateWindowAttribFunc	= fxFoldUpdateWindowAttrib,
      .animStepFunc		= fxCurvedFoldModelStep,
      .initFunc			= animWithTransformInit,
      .initGridFunc		= fxMagicLampInitGrid,
      .updateWinTransformFunc	= defaultUpdateWindowTransform,
      .updateBBFunc		= modelUpdateBB,
      .zoomToIconFunc		= fxCurvedFoldZoomToIcon,
      .modelAnimIs3D		= TRUE}};

AnimEffect AnimEffectDodge = &(AnimEffectInfo)
    {"animation:Dodge",
     {FALSE, FALSE, FALSE, FALSE, TRUE},
     {.animStepFunc		= fxDodgeAnimStep,
      .initFunc			= defaultAnimInit,
      .letOthersDrawGeomsFunc	= returnTrue,
      .updateWinTransformFunc	= fxDodgeUpdateWindowTransform,
      .postPrepPaintScreenFunc	= fxDodgePostPreparePaintScreen,
      .updateBBFunc 		= fxDodgeUpdateBB}};

AnimEffect AnimEffectDream = &(AnimEffectInfo)
    {"animation:Dream",
     {TRUE, TRUE, TRUE, FALSE, FALSE},
     {.updateWindowAttribFunc 	= fxDreamUpdateWindowAttrib,
      .animStepFunc		= fxDreamModelStep,
      .initFunc			= fxDreamAnimInit,
      .initGridFunc		= fxMagicLampInitGrid,
      .updateWinTransformFunc	= defaultUpdateWindowTransform,
      .updateBBFunc		= modelUpdateBB,
      .zoomToIconFunc		= fxDreamZoomToIcon}};

AnimEffect AnimEffectFade = &(AnimEffectInfo)
    {"animation:Fade",
     {TRUE, TRUE, TRUE, FALSE, FALSE},
     {.updateWindowAttribFunc	= fxFadeUpdateWindowAttrib,
      .animStepFunc 		= defaultAnimStep,
      .initFunc			= defaultAnimInit,
      .letOthersDrawGeomsFunc	= returnTrue,
      .updateBBFunc		= updateBBWindow}};

AnimEffect AnimEffectFocusFade = &(AnimEffectInfo)
    {"animation:Focus Fade",
     {FALSE, FALSE, FALSE, FALSE, TRUE},
     {.updateWindowAttribFunc	= fxFocusFadeUpdateWindowAttrib,
      .animStepFunc		= defaultAnimStep,
      .initFunc			= defaultAnimInit,
      .letOthersDrawGeomsFunc	= returnTrue,
      .updateBBFunc		= updateBBWindow}};

AnimEffect AnimEffectGlide1 = &(AnimEffectInfo)
    {"animation:Glide 1",
     {TRUE, TRUE, TRUE, FALSE, FALSE},
     {.updateWindowAttribFunc	= fxGlideUpdateWindowAttrib,
      .prePaintWindowFunc	= fxGlidePrePaintWindow,
      .postPaintWindowFunc	= fxGlidePostPaintWindow,
      .animStepFunc		= fxGlideAnimStep,
      .initFunc			= fxGlideInit,
      .letOthersDrawGeomsFunc	= returnTrue,
      .updateWinTransformFunc	= fxGlideUpdateWindowTransform,
      .updateBBFunc		= compTransformUpdateBB,
      .zoomToIconFunc		= fxGlideZoomToIcon}};

AnimEffect AnimEffectGlide2 = &(AnimEffectInfo)
    {"animation:Glide 2",
     {TRUE, TRUE, TRUE, FALSE, FALSE},
     {.updateWindowAttribFunc	= fxGlideUpdateWindowAttrib,
      .prePaintWindowFunc	= fxGlidePrePaintWindow,
      .postPaintWindowFunc	= fxGlidePostPaintWindow,
      .animStepFunc		= fxGlideAnimStep,
      .initFunc			= fxGlideInit,
      .letOthersDrawGeomsFunc	= returnTrue,
      .updateWinTransformFunc	= fxGlideUpdateWindowTransform,
      .updateBBFunc		= compTransformUpdateBB,
      .zoomToIconFunc		= fxGlideZoomToIcon}};

AnimEffect AnimEffectHorizontalFolds = &(AnimEffectInfo)
    {"animation:Horizontal Folds",
     {TRUE, TRUE, TRUE, TRUE, FALSE},
     {.updateWindowAttribFunc	= fxFoldUpdateWindowAttrib,
      .animStepFunc		= fxHorizontalFoldsModelStep,
      .initFunc			= animWithTransformInit,
      .initGridFunc		= fxHorizontalFoldsInitGrid,
      .updateWinTransformFunc	= defaultUpdateWindowTransform,
      .updateBBFunc		= modelUpdateBB,
      .zoomToIconFunc		= fxHorizontalFoldsZoomToIcon,
      .modelAnimIs3D		= TRUE}};

AnimEffect AnimEffectMagicLamp = &(AnimEffectInfo)
    {"animation:Magic Lamp",
     {TRUE, TRUE, TRUE, FALSE, FALSE},
     {.animStepFunc		= fxMagicLampModelStep,
      .initFunc			= fxMagicLampInit,
      .initGridFunc		= fxMagicLampInitGrid,
      .updateBBFunc		= modelUpdateBB,
      .useQTexCoord		= TRUE}};

AnimEffect AnimEffectRollUp = &(AnimEffectInfo)
    {"animation:Roll Up",
     {TRUE, TRUE, TRUE, TRUE, FALSE},
     {.animStepFunc		= fxRollUpModelStep,
      .initFunc			= fxRollUpAnimInit,
      .initGridFunc		= fxRollUpInitGrid,
      .updateBBFunc		= modelUpdateBB}};

AnimEffect AnimEffectSidekick = &(AnimEffectInfo)
    {"animation:Sidekick",
     {TRUE, TRUE, TRUE, FALSE, FALSE},
     {.updateWindowAttribFunc	= fxZoomUpdateWindowAttrib,
      .animStepFunc		= defaultAnimStep,
      .initFunc			= fxSidekickInit,
      .letOthersDrawGeomsFunc	= returnTrue,
      .updateWinTransformFunc	= defaultUpdateWindowTransform,
      .updateBBFunc		= compTransformUpdateBB,
      .zoomToIconFunc		= returnTrue}};

AnimEffect AnimEffectVacuum = &(AnimEffectInfo)
    {"animation:Vacuum",
     {TRUE, TRUE, FALSE, FALSE, FALSE},
     {.animStepFunc		= fxMagicLampModelStep,
      .initFunc			= fxMagicLampInit,
      .initGridFunc		= fxVacuumInitGrid,
      .updateBBFunc		= modelUpdateBB,
      .useQTexCoord		= TRUE}};

AnimEffect AnimEffectWave = &(AnimEffectInfo)                                       
    {"animation:Wave",
     {TRUE, TRUE, TRUE, FALSE, TRUE},
     {.animStepFunc		= fxWaveModelStep,
      .initFunc			= animWithTransformInit,
      .initGridFunc		= fxMagicLampInitGrid,
      .updateWinTransformFunc	= defaultUpdateWindowTransform,
      .updateBBFunc		= modelUpdateBB,
      .modelAnimIs3D		= TRUE}};

AnimEffect AnimEffectZoom = &(AnimEffectInfo)
    {"animation:Zoom",
     {TRUE, TRUE, TRUE, FALSE, FALSE},
     {.updateWindowAttribFunc	= fxZoomUpdateWindowAttrib,
      .animStepFunc		= defaultAnimStep,
      .initFunc			= fxZoomInit,
      .letOthersDrawGeomsFunc	= returnTrue,
      .updateWinTransformFunc	= defaultUpdateWindowTransform,
      .updateBBFunc		= compTransformUpdateBB,
      .zoomToIconFunc		= returnTrue}};

AnimEffect animEffects[NUM_EFFECTS];

ExtensionPluginInfo animExtensionPluginInfo = {
    .nEffects		= NUM_EFFECTS,
    .effects		= animEffects,

    .nEffectOptions	= ANIM_SCREEN_OPTION_NUM - NUM_NONEFFECT_OPTIONS,
};

static Bool animInitScreen(CompPlugin * p, CompScreen * s)
{
    AnimScreen *as;
    CompDisplay *d = s->display;

    ANIM_DISPLAY(d);

    as = calloc(1, sizeof(AnimScreen));
    if (!as)
	return FALSE;

    if (!compInitScreenOptionsFromMetadata (s,
					    &animMetadata,
					    animScreenOptionInfo,
					    as->opt,
					    ANIM_SCREEN_OPTION_NUM))
    {
	free (as);
	return FALSE;
    }

    as->windowPrivateIndex = allocateWindowPrivateIndex(s);
    if (as->windowPrivateIndex < 0)
    {
	compFiniScreenOptions (s, as->opt, ANIM_SCREEN_OPTION_NUM);
	free(as);
	return FALSE;
    }

    s->base.privates[ad->screenPrivateIndex].ptr = as;

    as->animInProgress = FALSE;

    AnimEffect animEffectsTmp[NUM_EFFECTS] =
    {
	AnimEffectNone,
	AnimEffectRandom,
	AnimEffectCurvedFold,
	AnimEffectDodge,
	AnimEffectDream,
	AnimEffectFade,
	AnimEffectFocusFade,
	AnimEffectGlide1,
	AnimEffectGlide2,
	AnimEffectHorizontalFolds,
	AnimEffectMagicLamp,
	AnimEffectRollUp,
	AnimEffectSidekick,
	AnimEffectVacuum,
	AnimEffectWave,
	AnimEffectZoom
    };
    memcpy (animEffects,
	    animEffectsTmp,
	    NUM_EFFECTS * sizeof (AnimEffect));

    animExtensionPluginInfo.effectOptions = &as->opt[NUM_NONEFFECT_OPTIONS];

    // Extends itself with the basic set of animation effects.
    animAddExtension (s, &animExtensionPluginInfo);

    AnimEvent e;
    for (e = 0; e < AnimEventNum; e++) // for each anim event
	updateOptionSets (s, e);

    updateAllEventEffects (s);

    as->lastClientListStacking = NULL;
    as->nLastClientListStacking = 0;

    WRAP(as, s, preparePaintScreen, animPreparePaintScreen);
    WRAP(as, s, donePaintScreen, animDonePaintScreen);
    WRAP(as, s, paintOutput, animPaintOutput);
    WRAP(as, s, paintWindow, animPaintWindow);
    WRAP(as, s, damageWindowRect, animDamageWindowRect);
    WRAP(as, s, addWindowGeometry, animAddWindowGeometry);
    WRAP(as, s, drawWindowTexture, animDrawWindowTexture);
    WRAP(as, s, windowResizeNotify, animWindowResizeNotify);
    WRAP(as, s, windowMoveNotify, animWindowMoveNotify);
    WRAP(as, s, windowGrabNotify, animWindowGrabNotify);
    WRAP(as, s, windowUngrabNotify, animWindowUngrabNotify);
    WRAP(as, s, initWindowWalker, animInitWindowWalker);

    as->startCountdown = 20; // start the countdown

    return TRUE;
}

static void animFiniScreen(CompPlugin * p, CompScreen * s)
{
    ANIM_SCREEN(s);

    if (as->animInProgress)
	animActivateEvent(s, FALSE);

    freeWindowPrivateIndex(s, as->windowPrivateIndex);

    if (as->lastClientListStacking)
	free(as->lastClientListStacking);

    free (as->extensionPlugins);
    freeAllEffects (as);
    freeAllOptionSets (as);

    UNWRAP(as, s, preparePaintScreen);
    UNWRAP(as, s, donePaintScreen);
    UNWRAP(as, s, paintOutput);
    UNWRAP(as, s, paintWindow);
    UNWRAP(as, s, damageWindowRect);
    UNWRAP(as, s, addWindowGeometry);
    UNWRAP(as, s, drawWindowTexture);
    UNWRAP(as, s, windowResizeNotify);
    UNWRAP(as, s, windowMoveNotify);
    UNWRAP(as, s, windowGrabNotify);
    UNWRAP(as, s, windowUngrabNotify);
    UNWRAP(as, s, initWindowWalker);

    compFiniScreenOptions (s, as->opt, ANIM_SCREEN_OPTION_NUM);

    free(as);
}

static Bool animInitWindow(CompPlugin * p, CompWindow * w)
{
    AnimWindow *aw;

    ANIM_SCREEN(w->screen);

    aw = calloc(1, sizeof(AnimWindow));
    if (!aw)
	return FALSE;

    aw->com.model = 0;
    aw->com.animRemainingTime = 0;
    aw->animInitialized = FALSE;
    aw->com.curAnimEffect = AnimEffectNone;
    aw->com.curWindowEvent = WindowEventNone;
    aw->com.animOverrideProgressDir = 0;
    aw->curAnimSelectionRow = -1;

    w->indexCount = 0;

    aw->unmapCnt = 0;
    aw->destroyCnt = 0;

    aw->grabbed = FALSE;

    aw->com.useDrawRegion = FALSE;
    aw->com.drawRegion = NULL;

    aw->BB.x1 = aw->BB.y1 = MAXSHORT;
    aw->BB.x2 = aw->BB.y2 = MINSHORT;

    aw->nowShaded = FALSE;

    if (w->minimized)
    {
	aw->state = aw->newState = IconicState;
    }
    else if (w->shaded)
    {
	aw->state = aw->newState = NormalState;
	aw->nowShaded = TRUE;
    }
    else
    {
	aw->state = aw->newState = animGetWindowState(w);
    }

    w->base.privates[as->windowPrivateIndex].ptr = aw;

    return TRUE;
}

static void animFiniWindow(CompPlugin * p, CompWindow * w)
{
    ANIM_SCREEN(w->screen);
    ANIM_WINDOW(w);

    postAnimationCleanupCustom (w, FALSE, TRUE, TRUE);

    animFreeModel(aw);

    free(aw);
    w->base.privates[as->windowPrivateIndex].ptr = NULL;
}

static CompBool
animInitObject (CompPlugin *p,
		CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) animInitDisplay,
	(InitPluginObjectProc) animInitScreen,
	(InitPluginObjectProc) animInitWindow
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
animFiniObject (CompPlugin *p,
		CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) animFiniDisplay,
	(FiniPluginObjectProc) animFiniScreen,
	(FiniPluginObjectProc) animFiniWindow
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static CompOption *
animGetObjectOptions (CompPlugin *plugin,
		      CompObject *object,
		      int	   *count)
{
    static GetPluginObjectOptionsProc dispTab[] = {
	(GetPluginObjectOptionsProc) 0, /* GetCoreOptions */
	(GetPluginObjectOptionsProc) animGetDisplayOptions,
	(GetPluginObjectOptionsProc) animGetScreenOptions
    };

    *count = 0;
    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab),
		     NULL, (plugin, object, count));
}

static CompBool
animSetObjectOption (CompPlugin      *plugin,
		     CompObject      *object,
		     const char      *name,
		     CompOptionValue *value)
{
    static SetPluginObjectOptionProc dispTab[] = {
	(SetPluginObjectOptionProc) 0, /* SetCoreOption */
	(SetPluginObjectOptionProc) animSetDisplayOption,
	(SetPluginObjectOptionProc) animSetScreenOptions
    };

    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab), FALSE,
		     (plugin, object, name, value));
}

static Bool animInit(CompPlugin * p)
{
    if (!compInitPluginMetadataFromInfo (&animMetadata,
					 p->vTable->name,
					 0, 0,
					 animScreenOptionInfo,
					 ANIM_SCREEN_OPTION_NUM))
	return FALSE;

    animDisplayPrivateIndex = allocateDisplayPrivateIndex();
    if (animDisplayPrivateIndex < 0)
    {
	compFiniMetadata (&animMetadata);
	return FALSE;
    }

    animFunctionsPrivateIndex = allocateDisplayPrivateIndex ();
    if (animFunctionsPrivateIndex < 0)
    {
	freeDisplayPrivateIndex (animDisplayPrivateIndex);
	compFiniMetadata (&animMetadata);
	return FALSE;
    }

    compAddMetadataFromFile (&animMetadata, p->vTable->name);

    return TRUE;
}

static void animFini(CompPlugin * p)
{
    freeDisplayPrivateIndex(animDisplayPrivateIndex);
    freeDisplayPrivateIndex (animFunctionsPrivateIndex);
    compFiniMetadata (&animMetadata);
}

static CompMetadata *
animGetMetadata (CompPlugin *plugin)
{
    return &animMetadata;
}

CompPluginVTable animVTable = {
    "animation",
    animGetMetadata,
    animInit,
    animFini,
    animInitObject,
    animFiniObject,
    animGetObjectOptions,
    animSetObjectOption,
};

CompPluginVTable*
getCompPluginInfo20070830 (void)
{
    return &animVTable;
}

