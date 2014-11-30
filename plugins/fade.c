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
 *         Michail Bitzes <noodlylight@gmail.com>
 */

#include <stdlib.h>
#include <string.h>

#include <fusilli-core.h>

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _FadeDisplay {
	int                        screenPrivateIndex;
	HandleEventProc            handleEvent;
	int                        displayModals;
	Bool                       suppressMinimizeOpenClose;
	CompMatch                  alwaysFadeWindowMatch;
} FadeDisplay;

#define FADE_MODE_CONSTANTSPEED 0
#define FADE_MODE_CONSTANTTIME  1
#define FADE_MODE_MAX           FADE_MODE_CONSTANTTIME

typedef struct _FadeScreen {
	int            windowPrivateIndex;
	int            fadeTime;

	PreparePaintScreenProc preparePaintScreen;
	PaintWindowProc        paintWindow;
	DamageWindowRectProc   damageWindowRect;
	FocusWindowProc        focusWindow;
	WindowResizeNotifyProc windowResizeNotify;

	CompMatch match;
} FadeScreen;

typedef struct _FadeWindow {
	GLushort opacity;
	GLushort brightness;
	GLushort saturation;

	int dModal;

	int destroyCnt;
	int unmapCnt;

	Bool shaded;
	Bool alive;
	Bool fadeOut;

	int steps;

	int fadeTime;

	int opacityDiff;
	int brightnessDiff;
	int saturationDiff;

	GLushort targetOpacity;
	GLushort targetBrightness;
	GLushort targetSaturation;
} FadeWindow;

#define GET_FADE_DISPLAY(d) \
        ((FadeDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define FADE_DISPLAY(d) \
        FadeDisplay *fd = GET_FADE_DISPLAY (d)

#define GET_FADE_SCREEN(s, fd) \
        ((FadeScreen *) (s)->privates[(fd)->screenPrivateIndex].ptr)

#define FADE_SCREEN(s) \
        FadeScreen *fs = GET_FADE_SCREEN (s, GET_FADE_DISPLAY (&display))

#define GET_FADE_WINDOW(w, fs) \
        ((FadeWindow *) (w)->privates[(fs)->windowPrivateIndex].ptr)

#define FADE_WINDOW(w) \
        FadeWindow *fw = GET_FADE_WINDOW  (w, \
                         GET_FADE_SCREEN  (w->screen, \
                         GET_FADE_DISPLAY (&display)))

static void
fadeChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	CompScreen *screen;

	screen = getScreenFromScreenNum (screenNum);

	FADE_SCREEN (screen);

	if (strcasecmp (optionName, "fade_speed") == 0)
	{
		fs->fadeTime = 1000.0f / optionValue->f;
	}
	else if (strcasecmp (optionName, "window_match") == 0)
	{
		matchFini (&fs->match);
		matchInit (&fs->match);
		matchAddFromString (&fs->match, "!type=desktop");
		matchAddFromString (&fs->match, optionValue->s);
		matchUpdate (&fs->match);
	}
}

static void
fadePreparePaintScreen (CompScreen *s,
                        int        msSinceLastPaint)
{
	CompWindow *w;
	int	       steps;

	FADE_SCREEN (s);

	const BananaValue *
	option_fade_mode = bananaGetOption (bananaIndex, "fade_mode", s->screenNum);

	switch (option_fade_mode->i) {
	case FADE_MODE_CONSTANTSPEED:
		steps = (msSinceLastPaint * OPAQUE) / fs->fadeTime;
		if (steps < 12)
			steps = 12;

		for (w = s->windows; w; w = w->next)
		{
			FadeWindow *fw = GET_FADE_WINDOW (w, fs);
			fw->steps    = steps;
			fw->fadeTime = 0;
		}

		break;
	case FADE_MODE_CONSTANTTIME:
		for (w = s->windows; w; w = w->next)
		{
			FadeWindow *fw = GET_FADE_WINDOW (w, fs);

			if (fw->fadeTime)
			{
				fw->steps     = 1;
				fw->fadeTime -= msSinceLastPaint;
				if (fw->fadeTime < 0)
					fw->fadeTime = 0;
			}
			else
			{
				fw->steps = 0;
			}
		}
		
		break;
	}


	UNWRAP (fs, s, preparePaintScreen);
	(*s->preparePaintScreen) (s, msSinceLastPaint);
	WRAP (fs, s, preparePaintScreen, fadePreparePaintScreen);
}

static void
fadeWindowStop (CompWindow *w)
{
	FADE_WINDOW (w);

	while (fw->unmapCnt)
	{
		unmapWindow (w);
		fw->unmapCnt--;
	}

	while (fw->destroyCnt)
	{
		destroyWindow (w);
		fw->destroyCnt--;
	}
}

static Bool
fadePaintWindow (CompWindow              *w,
                 const WindowPaintAttrib *attrib,
                 const CompTransform     *transform,
                 Region                  region,
                 unsigned int            mask)
{
	CompScreen *s = w->screen;
	Bool       status;

	FADE_DISPLAY (&display);
	FADE_SCREEN (s);
	FADE_WINDOW (w);

	if (!w->screen->canDoSlightlySaturated)
		fw->saturation = attrib->saturation;

	if (!w->alive                            ||
		fw->destroyCnt                       ||
		fw->unmapCnt                         ||
		fw->opacity    != attrib->opacity    ||
		fw->brightness != attrib->brightness ||
		fw->saturation != attrib->saturation ||
		fd->displayModals)
	{
		const BananaValue *option_fade_mode = bananaGetOption (bananaIndex,
		                                                       "fade_mode",
		                                                       s->screenNum);

		WindowPaintAttrib fAttrib = *attrib;
		int               mode = option_fade_mode->i;

		const BananaValue *
		option_dim_unresponsive = bananaGetOption (bananaIndex,
		                                           "dim_unresponsive",
		                                           s->screenNum);

		if (!w->alive && option_dim_unresponsive->b)
		{
			GLuint value;

			const BananaValue *
			option_unresponsive_brightness = bananaGetOption (bananaIndex,
				                                    "unresponsive_brightness",
			                                        s->screenNum);

			value = option_unresponsive_brightness->i;
			if (value != 100)
				fAttrib.brightness = fAttrib.brightness * value / 100;

			const BananaValue *
			option_unresponsive_saturation = bananaGetOption (bananaIndex,
				                                  "unresponsive_saturation",
			                                      s->screenNum);

			value = option_unresponsive_saturation->i;
			if (value != 100 && s->canDoSlightlySaturated)
				fAttrib.saturation = fAttrib.saturation * value / 100;
		}
		else if (fd->displayModals && !fw->dModal)
		{
			fAttrib.brightness = 0xa8a8;
			fAttrib.saturation = 0;
		}

		if (fw->fadeOut)
			fAttrib.opacity = 0;

		if (mode == FADE_MODE_CONSTANTTIME)
		{
			if (fAttrib.opacity    != fw->targetOpacity    ||
			    fAttrib.brightness != fw->targetBrightness ||
			    fAttrib.saturation != fw->targetSaturation)
			{
				const BananaValue *option_fade_time = bananaGetOption (
				                       bananaIndex, "fade_time", s->screenNum);

				fw->fadeTime = option_fade_time->i;
				fw->steps    = 1;

				fw->opacityDiff    = fAttrib.opacity - fw->opacity;
				fw->brightnessDiff = fAttrib.brightness - fw->brightness;
				fw->saturationDiff = fAttrib.saturation - fw->saturation;

				fw->targetOpacity    = fAttrib.opacity;
				fw->targetBrightness = fAttrib.brightness;
				fw->targetSaturation = fAttrib.saturation;
			}
		}

		if (fw->steps)
		{
			GLint opacity    = OPAQUE;
			GLint brightness = BRIGHT;
			GLint saturation = COLOR;

			if (mode == FADE_MODE_CONSTANTSPEED)
			{
				opacity = fw->opacity;
				if (fAttrib.opacity > fw->opacity)
				{
					opacity = fw->opacity + fw->steps;
					if (opacity > fAttrib.opacity)
						opacity = fAttrib.opacity;
				}
				else if (fAttrib.opacity < fw->opacity)
				{
					opacity = fw->opacity - fw->steps;
					if (opacity < fAttrib.opacity)
						opacity = fAttrib.opacity;
				}

				brightness = fw->brightness;
				if (fAttrib.brightness > fw->brightness)
				{
					brightness = fw->brightness + (fw->steps / 12);
					if (brightness > fAttrib.brightness)
						brightness = fAttrib.brightness;
				}
				else if (fAttrib.brightness < fw->brightness)
				{
					brightness = fw->brightness - (fw->steps / 12);
					if (brightness < fAttrib.brightness)
						brightness = fAttrib.brightness;
				}

				saturation = fw->saturation;
				if (fAttrib.saturation > fw->saturation)
				{
					saturation = fw->saturation + (fw->steps / 6);
					if (saturation > fAttrib.saturation)
						saturation = fAttrib.saturation;
				}
				else if (fAttrib.saturation < fw->saturation)
				{
					saturation = fw->saturation - (fw->steps / 6);
					if (saturation < fAttrib.saturation)
						saturation = fAttrib.saturation;
				}
			}
			else if (mode == FADE_MODE_CONSTANTTIME)
			{
				const BananaValue *
				option_fade_time = bananaGetOption (bananaIndex,
				                                 "fade_time", s->screenNum);

				int fadeTime = option_fade_time->i;

				opacity = fAttrib.opacity -
				          (fw->opacityDiff * fw->fadeTime / fadeTime);
				brightness = fAttrib.brightness -
				             (fw->brightnessDiff * fw->fadeTime / fadeTime);
				saturation = fAttrib.saturation -
				             (fw->saturationDiff * fw->fadeTime / fadeTime);
			}

			fw->steps = 0;

			if (opacity > 0)
			{
				fw->opacity    = opacity;
				fw->brightness = brightness;
				fw->saturation = saturation;

				if (opacity    != fAttrib.opacity    ||
				    brightness != fAttrib.brightness ||
				    saturation != fAttrib.saturation)
					addWindowDamage (w);
			}
			else
			{
				fw->opacity = 0;

				fadeWindowStop (w);
			}
		}

		fAttrib.opacity    = fw->opacity;
		fAttrib.brightness = fw->brightness;
		fAttrib.saturation = fw->saturation;

		UNWRAP (fs, s, paintWindow);
		status = (*s->paintWindow) (w, &fAttrib, transform, region, mask);
		WRAP (fs, s, paintWindow, fadePaintWindow);
	}
	else
	{
		UNWRAP (fs, s, paintWindow);
		status = (*s->paintWindow) (w, attrib, transform, region, mask);
		WRAP (fs, s, paintWindow, fadePaintWindow);
	}

	return status;
}

static void
fadeAddDisplayModal (CompDisplay *d,
                     CompWindow  *w)
{
	FADE_DISPLAY (d);
	FADE_WINDOW (w);

	if (!(w->state & CompWindowStateDisplayModalMask))
		return;

	if (fw->dModal)
		return;

	fw->dModal = 1;

	fd->displayModals++;
	if (fd->displayModals == 1)
	{
		CompScreen *s;
		for (s = d->screens; s; s = s->next)
			damageScreen (s);
	}
}

static void
fadeRemoveDisplayModal (CompDisplay *d,
                        CompWindow  *w)
{
	FADE_DISPLAY (d);
	FADE_WINDOW (w);

	if (!fw->dModal)
		return;

	fw->dModal = 0;

	fd->displayModals--;
	if (fd->displayModals == 0)
	{
		CompScreen *s;
		for (s = d->screens; s; s = s->next)
			damageScreen (s);
	}
}

/* Returns whether this window should be faded
 * on open and close events. */
static Bool
isFadeWinForOpenClose (CompWindow *w)
{
	FADE_DISPLAY (&display);

	const BananaValue *
	option_minimize_open_close = bananaGetOption (bananaIndex,
	                                              "minimize_open_close",
	                                              w->screen->screenNum);

	if (option_minimize_open_close->b &&
	    !fd->suppressMinimizeOpenClose)
	{
		return TRUE;
	}
	return matchEval (&fd->alwaysFadeWindowMatch, w);
}

static void
fadeHandleEvent (XEvent      *event)
{
	CompWindow *w;

	FADE_DISPLAY (&display);

	switch (event->type) {
	case DestroyNotify:
		w = findWindowAtDisplay (event->xdestroywindow.window);
		if (w)
		{
			FADE_SCREEN (w->screen);

			if (w->texture->pixmap && isFadeWinForOpenClose (w) &&
			    matchEval (&fs->match, w))
			{
				FADE_WINDOW (w);

				if (fw->opacity == 0xffff)
					fw->opacity = 0xfffe;

				fw->destroyCnt++;
				w->destroyRefCnt++;

				fw->fadeOut = TRUE;

				addWindowDamage (w);
			}

			fadeRemoveDisplayModal (&display, w);
		}
		break;
	case UnmapNotify:
		w = findWindowAtDisplay (event->xunmap.window);
		if (w)
		{
			FADE_SCREEN (w->screen);
			FADE_WINDOW (w);

			fw->shaded = w->shaded;

			const BananaValue *
			option_minimize_open_close = bananaGetOption (
			        bananaIndex, "minimize_open_close", w->screen->screenNum);

			if (option_minimize_open_close->b &&
			    !fd->suppressMinimizeOpenClose &&
			    !fw->shaded && w->texture->pixmap &&
			    matchEval (&fs->match, w))
			{
				if (fw->opacity == 0xffff)
					fw->opacity = 0xfffe;

				fw->unmapCnt++;
				w->unmapRefCnt++;

				fw->fadeOut = TRUE;

				addWindowDamage (w);
			}

			fadeRemoveDisplayModal (&display, w);
		}
		break;
	case MapNotify:
		w = findWindowAtDisplay (event->xmap.window);
		if (w)
		{
			const BananaValue *
			option_minimize_open_close = bananaGetOption (
			        bananaIndex, "minimize_open_close", w->screen->screenNum);

			if (option_minimize_open_close->b &&
			    !fd->suppressMinimizeOpenClose)
			{
				fadeWindowStop (w);
			}
			if (w->state & CompWindowStateDisplayModalMask)
				fadeAddDisplayModal (&display, w);
		}
		break;
	default:
		if (event->type == display.xkbEvent)
		{
			XkbAnyEvent *xkbEvent = (XkbAnyEvent *) event;

			if (xkbEvent->xkb_type == XkbBellNotify)
			{
				XkbBellNotifyEvent *xkbBellEvent = (XkbBellNotifyEvent *)
					xkbEvent;

				w = findWindowAtDisplay (xkbBellEvent->window);
				if (!w)
					w = findWindowAtDisplay (display.activeWindow);

				if (w)
				{
					CompScreen *s = w->screen;

					const BananaValue *option_visual_bell = bananaGetOption (
					     bananaIndex, "visual_bell", s->screenNum);

					if (option_visual_bell->b)
					{
						const BananaValue *
						option_fullscreen_visual_bell = bananaGetOption (
						  bananaIndex, "fullscreen_visual_bell", s->screenNum);

						if (option_fullscreen_visual_bell->b)
						{
							for (w = s->windows; w; w = w->next)
							{
								if (w->destroyed)
									continue;

								if (w->attrib.map_state != IsViewable)
									continue;

								if (w->damaged)
								{
									FADE_WINDOW (w);

									fw->brightness = w->paint.brightness / 2;
								}
							}

							damageScreen (s);
						}
						else
						{
							FADE_WINDOW (w);

							fw->brightness = w->paint.brightness / 2;

							addWindowDamage (w);
						}
					}
				}
			}
		}
		break;
	}

	UNWRAP (fd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (fd, &display, handleEvent, fadeHandleEvent);

	switch (event->type) {
	case PropertyNotify:
		if (event->xproperty.atom == display.winStateAtom)
		{
			w = findWindowAtDisplay (event->xproperty.window);
			if (w && w->attrib.map_state == IsViewable)
			{
				if (w->state & CompWindowStateDisplayModalMask)
					fadeAddDisplayModal (&display, w);
				else
					fadeRemoveDisplayModal (&display, w);
			}
		}
		break;
	case ClientMessage:
		if (event->xclient.message_type == display.wmProtocolsAtom &&
		    event->xclient.data.l[0] == display.wmPingAtom)
		{
			w = findWindowAtDisplay (event->xclient.data.l[2]);
			if (w)
			{
				FADE_WINDOW (w);

				if (w->alive != fw->alive)
				{
					addWindowDamage (w);
					fw->alive = w->alive;
				}
			}
		}
	}
}

static Bool
fadeDamageWindowRect (CompWindow *w,
                      Bool       initial,
                      BoxPtr     rect)
{
	Bool status;

	FADE_SCREEN (w->screen);

	if (initial)
	{
		FADE_WINDOW (w);

		fw->fadeOut = FALSE;

		if (fw->shaded)
		{
			fw->shaded = w->shaded;
		}
		else if (matchEval (&fs->match, w))
		{
			if (isFadeWinForOpenClose (w))
			{
				fw->opacity       = 0;
				fw->targetOpacity = 0;
			}
		}
	}

	UNWRAP (fs, w->screen, damageWindowRect);
	status = (*w->screen->damageWindowRect) (w, initial, rect);
	WRAP (fs, w->screen, damageWindowRect, fadeDamageWindowRect);

	return status;
}

static Bool
fadeFocusWindow (CompWindow *w)
{
	Bool status;

	FADE_SCREEN (w->screen);
	FADE_WINDOW (w);

	if (fw->destroyCnt || fw->unmapCnt)
		return FALSE;

	UNWRAP (fs, w->screen, focusWindow);
	status = (*w->screen->focusWindow) (w);
	WRAP (fs, w->screen, focusWindow, fadeFocusWindow);

	return status;
}

static void
fadeWindowResizeNotify (CompWindow *w,
                        int        dx,
                        int        dy,
                        int        dwidth,
                        int        dheight)
{
	FADE_SCREEN (w->screen);

	if (!w->mapNum)
		fadeWindowStop (w);

	UNWRAP (fs, w->screen, windowResizeNotify);
	(*w->screen->windowResizeNotify) (w, dx, dy, dwidth, dheight);
	WRAP (fs, w->screen, windowResizeNotify, fadeWindowResizeNotify);
}

static Bool
fadeInitDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	FadeDisplay *fd;

	fd = malloc (sizeof (FadeDisplay));
	if (!fd)
		return FALSE;

	fd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (fd->screenPrivateIndex < 0)
	{
		free (fd);
		return FALSE;
	}

	fd->displayModals = 0;

	fd->suppressMinimizeOpenClose = (findActivePlugin ("animation") != NULL);

	/* Always fade opening and closing of screen-dimming layer of 
	   logout window and gksu. */
	matchInit (&fd->alwaysFadeWindowMatch);
	matchAddExp (&fd->alwaysFadeWindowMatch, 0, "title=gksu");
	matchAddExp (&fd->alwaysFadeWindowMatch, 0, "title=x-session-manager");
	matchAddExp (&fd->alwaysFadeWindowMatch, 0, "title=gnome-session");
	matchUpdate (&fd->alwaysFadeWindowMatch);

	WRAP (fd, d, handleEvent, fadeHandleEvent);

	d->privates[displayPrivateIndex].ptr = fd;

	return TRUE;
}

static void
fadeFiniDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	FADE_DISPLAY (d);

	freeScreenPrivateIndex (fd->screenPrivateIndex);

	matchFini (&fd->alwaysFadeWindowMatch);

	UNWRAP (fd, d, handleEvent);

	free (fd);
}

static Bool
fadeInitScreen (CompPlugin *p,
                CompScreen *s)
{
	FadeScreen *fs;

	FADE_DISPLAY (&display);

	fs = malloc (sizeof (FadeScreen));
	if (!fs)
		return FALSE;

	fs->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (fs->windowPrivateIndex < 0)
	{
		free (fs);
		return FALSE;
	}

	const BananaValue *
	option_fade_speed = bananaGetOption (bananaIndex,
	                                     "fade_speed",
	                                     s->screenNum);

	fs->fadeTime = 1000.0f / option_fade_speed->f;

	const BananaValue *
	option_window_match = bananaGetOption (bananaIndex,
	                                       "window_match",
	                                       s->screenNum);

	matchInit (&fs->match);
	matchAddFromString (&fs->match, "!type=desktop");
	matchAddFromString (&fs->match, option_window_match->s);
	matchUpdate (&fs->match);

	WRAP (fs, s, preparePaintScreen, fadePreparePaintScreen);
	WRAP (fs, s, paintWindow, fadePaintWindow);
	WRAP (fs, s, damageWindowRect, fadeDamageWindowRect);
	WRAP (fs, s, focusWindow, fadeFocusWindow);
	WRAP (fs, s, windowResizeNotify, fadeWindowResizeNotify);

	s->privates[fd->screenPrivateIndex].ptr = fs;

	return TRUE;
}

static void
fadeFiniScreen (CompPlugin *p,
                CompScreen *s)
{
	FADE_SCREEN (s);

	matchFini (&fs->match);

	freeWindowPrivateIndex (s, fs->windowPrivateIndex);

	UNWRAP (fs, s, preparePaintScreen);
	UNWRAP (fs, s, paintWindow);
	UNWRAP (fs, s, damageWindowRect);
	UNWRAP (fs, s, focusWindow);
	UNWRAP (fs, s, windowResizeNotify);

	free (fs);
}

static Bool
fadeInitWindow (CompPlugin *p,
                CompWindow *w)
{
	FadeWindow *fw;

	FADE_SCREEN (w->screen);

	fw = malloc (sizeof (FadeWindow));
	if (!fw)
		return FALSE;

	fw->opacity    = w->paint.opacity;
	fw->brightness = w->paint.brightness;
	fw->saturation = w->paint.saturation;

	fw->targetOpacity    = fw->opacity;
	fw->targetBrightness = fw->brightness;
	fw->targetSaturation = fw->saturation;

	fw->opacityDiff    = 0;
	fw->brightnessDiff = 0;
	fw->saturationDiff = 0;

	fw->dModal = 0;

	fw->destroyCnt = 0;
	fw->unmapCnt   = 0;
	fw->shaded     = w->shaded;
	fw->fadeOut    = FALSE;
	fw->alive      = w->alive;

	fw->steps      = 0;
	fw->fadeTime   = 0;

	w->privates[fs->windowPrivateIndex].ptr = fw;

	if (w->attrib.map_state == IsViewable)
	{
		if (w->state & CompWindowStateDisplayModalMask)
			fadeAddDisplayModal (&display, w);
	}

	return TRUE;
}

static void
fadeFiniWindow (CompPlugin *p,
                CompWindow *w)
{
	FADE_WINDOW (w);

	fadeRemoveDisplayModal (&display, w);
	fadeWindowStop (w);

	free (fw);
}

static Bool
fadeInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("fade", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("fade");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, fadeChangeNotify);

	return TRUE;
}

static void
fadeFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable fadeVTable = {
	"fade",
	fadeInit,
	fadeFini,
	NULL, /* fadeInitCore */
	NULL, /* fadeFiniCore */
	fadeInitDisplay,
	fadeFiniDisplay,
	fadeInitScreen,
	fadeFiniScreen,
	fadeInitWindow,
	fadeFiniWindow
};

CompPluginVTable *
getCompPluginInfo20141130 (void)
{
	return &fadeVTable;
}
