/**
 *
 * Compiz fade to desktop plugin
 *
 * fadedesktop.c
 *
 * Copyright (c) 2006 Robert Carr <racarr@beryl-project.org>
 *                       2007 Danny Baumann <maniac@beryl-project.org>
 * Copyright (c) 2015 Michail Bitzes <noodlylight@gmail.com>
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
 **/

#include <stdlib.h>
#include <string.h>
#include <fusilli-core.h>

#include <math.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>

static int bananaIndex;

static int displayPrivateIndex;

typedef enum
{
	FD_STATE_OFF = 0,
	FD_STATE_OUT,
	FD_STATE_ON,
	FD_STATE_IN
} FadeDesktopState;

typedef struct _FadeDesktopDisplay
{
	int screenPrivateIndex;
} FadeDesktopDisplay;

typedef struct _FadeDesktopScreen
{
	int windowPrivateIndex;

	PreparePaintScreenProc preparePaintScreen;
	DonePaintScreenProc donePaintScreen;
	PaintWindowProc paintWindow;
	EnterShowDesktopModeProc enterShowDesktopMode;
	LeaveShowDesktopModeProc leaveShowDesktopMode;

	FadeDesktopState state;
	int fadeTime;

	CompMatch window_match;
} FadeDesktopScreen;

typedef struct _FadeDesktopWindow
{
	Bool fading;
	Bool isHidden;

	GLushort opacity;
} FadeDesktopWindow;

#define GET_FADEDESKTOP_DISPLAY(d) \
	((FadeDesktopDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define FD_DISPLAY(d) \
	FadeDesktopDisplay *fd = GET_FADEDESKTOP_DISPLAY (d)

#define GET_FADEDESKTOP_SCREEN(s, fd) \
	((FadeDesktopScreen *) (s)->privates[(fd)->screenPrivateIndex].ptr)

#define FD_SCREEN(s) \
	FadeDesktopScreen *fs = GET_FADEDESKTOP_SCREEN (s, GET_FADEDESKTOP_DISPLAY (&display))

#define GET_FADEDESKTOP_WINDOW(w, fs) \
	((FadeDesktopWindow *) (w)->privates[(fs)->windowPrivateIndex].ptr)

#define FD_WINDOW(w) \
	FadeDesktopWindow *fw = GET_FADEDESKTOP_WINDOW (w, \
	                                                GET_FADEDESKTOP_SCREEN (w->screen, GET_FADEDESKTOP_DISPLAY (&display)))

static void
fadeDesktopActivateEvent (CompScreen *s,
                          Bool       activating)
{
	BananaArgument arg[2];

	arg[0].type = BananaInt;
	arg[0].name = "root";
	arg[0].value.i = s->root;

	arg[1].type = BananaBool;
	arg[1].name = "active";
	arg[1].value.b = activating;

	(*display.handleFusilliEvent) ("fadedesktop", "activate", arg, 2);
}

static Bool
isFDWin (CompWindow *w)
{
	FD_SCREEN (w->screen);

	if (w->attrib.override_redirect)
		return FALSE;

	if (w->grabbed)
		return FALSE;

	if (!w->managed)
		return FALSE;

	if (w->wmType & (CompWindowTypeDesktopMask |
	                 CompWindowTypeDockMask))
		return FALSE;

	if (w->state & CompWindowStateSkipPagerMask)
		return FALSE;

	if (!matchEval (&fs->window_match, w))
		return FALSE;

	return TRUE;
}

static void
fadeDesktopPreparePaintScreen (CompScreen *s,
                               int        msSinceLastPaint)
{
	FD_SCREEN (s);

	Bool doFade;

	const BananaValue *
	option_fade_time = bananaGetOption (bananaIndex,
	                                    "fade_time",
	                                    s->screenNum);

	fs->fadeTime -= msSinceLastPaint;
	if (fs->fadeTime < 0)
		fs->fadeTime = 0;

	if ((fs->state == FD_STATE_OUT) || (fs->state == FD_STATE_IN))
	{
		CompWindow *w;
		for (w = s->windows; w; w = w->next)
		{
			FD_WINDOW (w);

			if (fs->state == FD_STATE_OUT)
				doFade = fw->fading && w->inShowDesktopMode;
			else
				doFade = fw->fading && !w->inShowDesktopMode;

			if (doFade)
			{
				fw->opacity = w->paint.opacity *
				              (float)((fs->state == FD_STATE_OUT) ? fs->fadeTime :
				                      option_fade_time->i - fs->fadeTime) /
				              (float)option_fade_time->i;
			}
		}
	}

	UNWRAP (fs, s, preparePaintScreen);
	(*s->preparePaintScreen)(s, msSinceLastPaint);
	WRAP (fs, s, preparePaintScreen, fadeDesktopPreparePaintScreen);
}

static void
fadeDesktopDonePaintScreen (CompScreen *s)
{
	FD_SCREEN (s);

	if ((fs->state == FD_STATE_OUT) || (fs->state == FD_STATE_IN))
	{
		if (fs->fadeTime <= 0)
		{
			CompWindow *w;
			Bool isStillSD = FALSE;

			for (w = s->windows; w; w = w->next)
			{
				FD_WINDOW (w);

				if (fw->fading)
				{
					if (fs->state == FD_STATE_OUT)
					{
						hideWindow (w);
						fw->isHidden = TRUE;
					}
					fw->fading = FALSE;
				}
				if (w->inShowDesktopMode)
					isStillSD = TRUE;
			}

			if ((fs->state == FD_STATE_OUT) || isStillSD)
				fs->state = FD_STATE_ON;
			else
				fs->state = FD_STATE_OFF;

			fadeDesktopActivateEvent (s, FALSE);
		}
		else
			damageScreen (s);
	}

	UNWRAP (fs, s, donePaintScreen);
	(*s->donePaintScreen)(s);
	WRAP (fs, s, donePaintScreen, fadeDesktopDonePaintScreen);
}

static Bool
fadeDesktopPaintWindow (CompWindow              *w,
                        const WindowPaintAttrib *attrib,
                        const CompTransform     *transform,
                        Region                  region,
                        unsigned int            mask)
{
	Bool status;
	CompScreen *s = w->screen;
	FD_WINDOW (w);
	FD_SCREEN (s);

	if (fw->fading || fw->isHidden)
	{
		WindowPaintAttrib wAttrib = *attrib;
		wAttrib.opacity = fw->opacity;

		UNWRAP (fs, s, paintWindow);
		status = (*s->paintWindow)(w, &wAttrib, transform, region, mask);
		WRAP (fs, s, paintWindow, fadeDesktopPaintWindow);
	}
	else
	{
		UNWRAP (fs, s, paintWindow);
		status = (*s->paintWindow)(w, attrib, transform, region, mask);
		WRAP (fs, s, paintWindow, fadeDesktopPaintWindow);
	}

	return status;
}

static void
fadeDesktopEnterShowDesktopMode (CompScreen *s)
{
	FD_SCREEN (s);

	CompWindow *w;

	const BananaValue *
	option_fade_time = bananaGetOption (bananaIndex,
	                                    "fade_time",
	                                    s->screenNum);

	if ((fs->state == FD_STATE_OFF) || (fs->state == FD_STATE_IN))
	{
		if (fs->state == FD_STATE_OFF)
			fadeDesktopActivateEvent (s, TRUE);

		fs->state = FD_STATE_OUT;
		fs->fadeTime = option_fade_time->i - fs->fadeTime;

		for (w = s->windows; w; w = w->next)
		{
			if (isFDWin (w))
			{
				FD_WINDOW (w);

				fw->fading = TRUE;
				w->inShowDesktopMode = TRUE;
				fw->opacity = w->paint.opacity;
			}
		}

		damageScreen (s);
	}

	UNWRAP (fs, s, enterShowDesktopMode);
	(*s->enterShowDesktopMode)(s);
	WRAP (fs, s, enterShowDesktopMode, fadeDesktopEnterShowDesktopMode);
}

static void
fadeDesktopLeaveShowDesktopMode (CompScreen *s,
                                 CompWindow *w)
{
	FD_SCREEN (s);

	const BananaValue *
	option_fade_time = bananaGetOption (bananaIndex,
	                                    "fade_time",
	                                    s->screenNum);

	if (fs->state != FD_STATE_OFF)
	{
		CompWindow *cw;

		if (fs->state != FD_STATE_IN)
		{
			if (fs->state == FD_STATE_ON)
				fadeDesktopActivateEvent (s, TRUE);

			fs->state = FD_STATE_IN;
			fs->fadeTime = option_fade_time->i - fs->fadeTime;
		}

		for (cw = s->windows; cw; cw = cw->next)
		{
			if (w && (w->id != cw->id))
				continue;

			FD_WINDOW (cw);

			if (fw->isHidden)
			{
				cw->inShowDesktopMode = FALSE;
				showWindow (cw);
				fw->isHidden = FALSE;
				fw->fading = TRUE;
			}
			else if (fw->fading)
			{
				cw->inShowDesktopMode = FALSE;
			}
		}

		damageScreen (s);
	}

	UNWRAP (fs, s, leaveShowDesktopMode);
	(*s->leaveShowDesktopMode) (s, w);
	WRAP (fs, s, leaveShowDesktopMode, fadeDesktopLeaveShowDesktopMode);
}

static Bool
fadeDesktopInitDisplay (CompPlugin  *p,
                        CompDisplay *d)
{
	FadeDesktopDisplay *fd;

	fd = malloc (sizeof(FadeDesktopDisplay));
	if (!fd)
		return FALSE;

	fd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (fd->screenPrivateIndex < 0)
	{
		free (fd);
		return FALSE;
	}

	d->privates[displayPrivateIndex].ptr = fd;

	return TRUE;
}

static void
fadeDesktopFiniDisplay (CompPlugin  *p,
                        CompDisplay *d)
{
	FD_DISPLAY (d);

	freeScreenPrivateIndex (fd->screenPrivateIndex);

	free (fd);
}

static Bool
fadeDesktopInitScreen (CompPlugin *p,
                       CompScreen *s)
{
	FadeDesktopScreen *fs;

	FD_DISPLAY (&display);

	fs = malloc (sizeof (FadeDesktopScreen));
	if (!fs)
		return FALSE;

	fs->windowPrivateIndex = allocateWindowPrivateIndex (s);
	if (fs->windowPrivateIndex < 0)
	{
		free (fs);
		return FALSE;
	}

	fs->state = FD_STATE_OFF;
	fs->fadeTime = 0;

	WRAP (fs, s, paintWindow, fadeDesktopPaintWindow);
	WRAP (fs, s, preparePaintScreen, fadeDesktopPreparePaintScreen);
	WRAP (fs, s, donePaintScreen, fadeDesktopDonePaintScreen);
	WRAP (fs, s, enterShowDesktopMode, fadeDesktopEnterShowDesktopMode);
	WRAP (fs, s, leaveShowDesktopMode, fadeDesktopLeaveShowDesktopMode);

	const BananaValue *
	option_window_match = bananaGetOption (bananaIndex,
	                                       "window_match",
	                                       s->screenNum);

	matchInit (&fs->window_match);
	matchAddFromString (&fs->window_match, option_window_match->s);
	matchUpdate (&fs->window_match);

	s->privates[fd->screenPrivateIndex].ptr = fs;

	return TRUE;

}

static void
fadeDesktopFiniScreen (CompPlugin *p,
                       CompScreen *s)
{
	FD_SCREEN (s);

	matchFini (&fs->window_match);

	UNWRAP (fs, s, paintWindow);
	UNWRAP (fs, s, preparePaintScreen);
	UNWRAP (fs, s, donePaintScreen);
	UNWRAP (fs, s, enterShowDesktopMode);
	UNWRAP (fs, s, leaveShowDesktopMode);

	freeWindowPrivateIndex (s, fs->windowPrivateIndex);

	free (fs);
}

static Bool
fadeDesktopInitWindow (CompPlugin *p,
                       CompWindow *w)
{
	FadeDesktopWindow *fw;

	FD_SCREEN (w->screen);

	fw = malloc (sizeof (FadeDesktopWindow));
	if (!fw)
		return FALSE;

	fw->isHidden = FALSE;
	fw->fading = FALSE;

	w->privates[fs->windowPrivateIndex].ptr = fw;

	return TRUE;

}

static void
fadeDesktopFiniWindow (CompPlugin *p,
                       CompWindow *w)
{
	FD_WINDOW (w);

	free (fw);
}

static void
fadeDesktopChangeNotify (const char        *optionName,
                         BananaType        optionType,
                         const BananaValue *optionValue,
                         int               screenNum)
{
	if (strcasecmp (optionName, "window_match") == 0)
	{
		CompScreen *s = getScreenFromScreenNum (screenNum);
		FD_SCREEN (s);

		matchFini (&fs->window_match);
		matchInit (&fs->window_match);
		matchAddFromString (&fs->window_match, optionValue->s);
		matchUpdate (&fs->window_match);
	}
}

static Bool
fadeDesktopInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("fadedesktop", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("fadedesktop");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, fadeDesktopChangeNotify);

	return TRUE;
}

static void
fadeDesktopFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable fadeDesktopVTable = {
	"fadedesktop",
	fadeDesktopInit,
	fadeDesktopFini,
	fadeDesktopInitDisplay,
	fadeDesktopFiniDisplay,
	fadeDesktopInitScreen,
	fadeDesktopFiniScreen,
	fadeDesktopInitWindow,
	fadeDesktopFiniWindow
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &fadeDesktopVTable;
}
