/*
 * Copyright © 2006 Novell, Inc.
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
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include <fusilli-core.h>

static int bananaIndex;

static CompButtonBinding initiate_button;

static int displayPrivateIndex;

#define MAX_LINE_LENGTH 1024

typedef struct _ShotDisplay {
	int             screenPrivateIndex;
	HandleEventProc handleEvent;
} ShotDisplay;

typedef struct _ShotScreen {
	PaintOutputProc paintOutput;
	PaintScreenProc paintScreen;
	int             grabIndex;

	int  x1, y1, x2, y2;
	Bool grab;
} ShotScreen;

#define GET_SHOT_DISPLAY(d) \
        ((ShotDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define SHOT_DISPLAY(d) \
        ShotDisplay *sd = GET_SHOT_DISPLAY (d)

#define GET_SHOT_SCREEN(s, sd) \
        ((ShotScreen *) (s)->base.privates[(sd)->screenPrivateIndex].ptr)

#define SHOT_SCREEN(s) \
        ShotScreen *ss = GET_SHOT_SCREEN (s, GET_SHOT_DISPLAY (s->display))

static Bool
shotInitiate (BananaArgument     *arg,
              int                nArg)
{
	CompScreen *s;
	Window     xid;

	BananaValue *root = getArgNamed ("root", arg, nArg);

	if (root != NULL)
		xid = root->i;
	else
		xid = 0;

	s = findScreenAtDisplay (core.displays, xid);
	if (s)
	{
		SHOT_SCREEN (s);

		if (otherScreenGrabExist (s, "screenshot", NULL))
			return FALSE;

		if (!ss->grabIndex)
			ss->grabIndex = pushScreenGrab (s, None, "screenshot");

		/* start selection screenshot rectangle */

		ss->x1 = ss->x2 = pointerX;
		ss->y1 = ss->y2 = pointerY;

		ss->grab = TRUE;
	}

	return TRUE;
}

static Bool
shotTerminate (BananaArgument     *arg,
               int                nArg)
{
	CompScreen *s;
	Window     xid;

	BananaValue *root = getArgNamed ("root", arg, nArg);

	if (root != NULL)
		xid = root->i;
	else
		xid = 0;

	for (s = core.displays->screens; s; s = s->next)
	{
		SHOT_SCREEN (s);

		if (xid && s->root != xid)
			continue;

		if (ss->grabIndex)
		{
			removeScreenGrab (s, ss->grabIndex, NULL);
			ss->grabIndex = 0;

			BananaValue *cancel = getArgNamed ("cancel", arg, nArg);

			if (cancel != NULL && cancel->b)
				ss->grab = FALSE;

			if (ss->x1 != ss->x2 && ss->y1 != ss->y2)
			{
				REGION reg;

				reg.rects    = &reg.extents;
				reg.numRects = 1;

				reg.extents.x1 = MIN (ss->x1, ss->x2) - 1;
				reg.extents.y1 = MIN (ss->y1, ss->y2) - 1;
				reg.extents.x2 = MAX (ss->x1, ss->x2) + 1;
				reg.extents.y2 = MAX (ss->y1, ss->y2) + 1;

				damageScreenRegion (s, &reg);
			}
		}
	}

	return FALSE;
}

static int
shotFilter (const struct dirent *d)
{
	int number;

	if (sscanf (d->d_name, "screenshot%d.png", &number))
	{
		int nDigits = 0;

		for (; number > 0; number /= 10)
			nDigits++;

		/* make sure there are no trailing characters in the name */
		if (strlen (d->d_name) == 14 + nDigits)
			return 1;
	}
	return 0;
}

static int
shotSort (const void *_a,
          const void *_b)
{
	struct dirent **a = (struct dirent **) _a;
	struct dirent **b = (struct dirent **) _b;
	int           al = strlen ((*a)->d_name);
	int           bl = strlen ((*b)->d_name);

	if (al == bl)
		return strcoll ((*a)->d_name, (*b)->d_name);
	else
		return al - bl;
}

static char *
shotGetXDGDesktopDir (void)
{
	int        nPrinted;
	FILE       *userDirsFile;
	char       *userDirsFilePath = NULL;
	const char *userDirsPathSuffix = "/user-dirs.dirs";
	const char *varName = "XDG_DESKTOP_DIR";
	size_t     varLength = strlen (varName);
	char       line[MAX_LINE_LENGTH];

	char *home = getenv ("HOME");
	if (!home)
		return NULL;

	int homeLength = strlen (home);
	if (!homeLength)
		return NULL;

	char *configHome = getenv ("XDG_CONFIG_HOME");
	if (configHome && strlen (configHome))
	{
		nPrinted = asprintf (&userDirsFilePath, "%s%s",
		                     configHome, userDirsPathSuffix);
	}
	else
	{
		nPrinted = asprintf (&userDirsFilePath, "%s/.config%s",
		                     home, userDirsPathSuffix);
	}
	if (nPrinted < 0)
		return NULL;

	userDirsFile = fopen (userDirsFilePath, "r");
	free (userDirsFilePath);

	if (!userDirsFile)
	{
		return NULL;
	}

	/* The user-dirs file has lines like:
	 * XDG_DESKTOP_DIR="$HOME/Desktop"
	 * Read it line by line until the desired directory variable is found.
	 */
	while (fgets (line, MAX_LINE_LENGTH, userDirsFile) != NULL)
	{
		char *varStart = strstr (line, varName);
		if (varStart) /* if found */
		{
			fclose (userDirsFile);

			 /* Remove any trailing \r \n characters */
			while (strlen (line) > 0 &&
			       (line[strlen (line) - 1] == '\r' ||
			        line[strlen (line) - 1] == '\n'))
				line[strlen (line) - 1] = '\0';

			/* Skip the =" part */
			size_t valueStartPos = (varStart - line) + varLength + 2;

			/* Ignore the " at the end */
			size_t valueSrcLength = strlen (line) - valueStartPos - 1;
			size_t homeEndSrcPos = 0;

			size_t valueDstLength = valueSrcLength;
			size_t homeEndDstPos = 0;

			if (!strncmp (line + valueStartPos, "$HOME", 5))
			{
				valueDstLength += homeLength - 5;
				homeEndDstPos = homeLength;
				homeEndSrcPos = 5;
			}
			else if (!strncmp (line + valueStartPos, "${HOME}", 7))
			{
				valueDstLength += homeLength - 7;
				homeEndDstPos = homeLength;
				homeEndSrcPos = 7;
			}

			char *desktopDir = malloc (valueDstLength + 1);

			/* Copy the home folder part (if necessary) */
			if (homeEndDstPos > 0)
				strcpy (desktopDir, home);

			/* Copy the rest */
			strncpy (desktopDir + homeEndDstPos,
					 line + valueStartPos + homeEndSrcPos,
					 valueSrcLength - homeEndSrcPos);
			desktopDir[valueDstLength] = '\0';

			return desktopDir;
		}
	}
	fclose (userDirsFile);
	return NULL;
}

static void
shotPaintScreen (CompScreen   *s,
                 CompOutput   *outputs,
                 int          numOutput,
                 unsigned int mask)
{
	SHOT_SCREEN (s);

	UNWRAP (ss, s, paintScreen);
	(*s->paintScreen) (s, outputs, numOutput, mask);
	WRAP (ss, s, paintScreen, shotPaintScreen);

	if (ss->grab)
	{
		int x1, x2, y1, y2;

		x1 = MIN (ss->x1, ss->x2);
		y1 = MIN (ss->y1, ss->y2);
		x2 = MAX (ss->x1, ss->x2);
		y2 = MAX (ss->y1, ss->y2);
		
		if (!ss->grabIndex)
		{
			int w = x2 - x1;
			int h = y2 - y1;

			if (w && h)
			{
				GLubyte *buffer;

				const BananaValue *
				option_dir = bananaGetOption (bananaIndex, "directory", -1);

				char    *dir = option_dir->s;
				Bool    allocatedDir = FALSE;

				if (strlen (dir) == 0)
				{
					// If dir is empty, use user's desktop directory instead
					dir = shotGetXDGDesktopDir ();
					if (dir)
						allocatedDir = TRUE;
					else
						dir = "";
				}
				buffer = malloc (sizeof (GLubyte) * w * h * 4);
				if (buffer)
				{
					struct dirent **namelist;
					int           n;

					glReadPixels (x1, s->height - y2, w, h,
					           GL_RGBA, GL_UNSIGNED_BYTE,
					           (GLvoid *) buffer);

					n = scandir (dir, &namelist, shotFilter, shotSort);
					if (n >= 0)
					{
						char name[256];
						char *app;
						int  number = 0;

						if (n > 0)
							sscanf (namelist[n - 1]->d_name,
							        "screenshot%d.png",
							        &number);

						number++;

						if (n)
							free (namelist);

						sprintf (name, "screenshot%d.png", number);

						const BananaValue *
						option_launch_app = bananaGetOption (
						       bananaIndex, "launch_app", -1);

						app = option_launch_app->s;

						if (!writeImageToFile (s->display, dir, name, "png",
						                       w, h, buffer))
						{
							compLogMessage ("screenshot", CompLogLevelError,
							              "failed to write screenshot image");
						}
						else if (*app != '\0')
						{
							char *command;

							command = malloc (strlen (app) +
							          strlen (dir) +
							          strlen (name) + 3);
							if (command)
							{
								sprintf (command, "%s %s/%s", app, dir, name);

								runCommand (s, command);

								free (command);
							}
						}
					}
					else
					{
						perror (dir);
					}

					free (buffer);
				}
				if (allocatedDir)
					free (dir);
			}

			ss->grab = FALSE;
		}
	}
}

static Bool
shotPaintOutput (CompScreen              *s,
                 const ScreenPaintAttrib *sAttrib,
                 const CompTransform     *transform,
                 Region                  region,
                 CompOutput              *output,
                 unsigned int            mask)
{
	Bool status;

	SHOT_SCREEN (s);

	UNWRAP (ss, s, paintOutput);
	status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
	WRAP (ss, s, paintOutput, shotPaintOutput);

	if (status && ss->grab)
	{
		int x1, x2, y1, y2;

		x1 = MIN (ss->x1, ss->x2);
		y1 = MIN (ss->y1, ss->y2);
		x2 = MAX (ss->x1, ss->x2);
		y2 = MAX (ss->y1, ss->y2);

		if (ss->grabIndex)
		{
			glPushMatrix ();

			prepareXCoords (s, output, -DEFAULT_Z_CAMERA);

			glDisableClientState (GL_TEXTURE_COORD_ARRAY);
			glEnable (GL_BLEND);
			glColor4us (0x2fff, 0x2fff, 0x4fff, 0x4fff);
			glRecti (x1, y2, x2, y1);
			glColor4us (0x2fff, 0x2fff, 0x4fff, 0x9fff);
			glBegin (GL_LINE_LOOP);
			glVertex2i (x1, y1);
			glVertex2i (x2, y1);
			glVertex2i (x2, y2);
			glVertex2i (x1, y2);
			glEnd ();
			glColor4usv (defaultColor);
			glDisable (GL_BLEND);
			glEnableClientState (GL_TEXTURE_COORD_ARRAY);
			glPopMatrix ();
		}
	}

	return status;
}

static void
shotHandleMotionEvent (CompScreen *s,
                       int        xRoot,
                       int        yRoot)
{
	SHOT_SCREEN (s);

	/* update screenshot rectangle size */

	if (ss->grabIndex)
	{
		REGION reg;

		reg.rects    = &reg.extents;
		reg.numRects = 1;

		reg.extents.x1 = MIN (ss->x1, ss->x2) - 1;
		reg.extents.y1 = MIN (ss->y1, ss->y2) - 1;
		reg.extents.x2 = MAX (ss->x1, ss->x2) + 1;
		reg.extents.y2 = MAX (ss->y1, ss->y2) + 1;

		damageScreenRegion (s, &reg);

		ss->x2 = xRoot;
		ss->y2 = yRoot;

		reg.extents.x1 = MIN (ss->x1, ss->x2) - 1;
		reg.extents.y1 = MIN (ss->y1, ss->y2) - 1;
		reg.extents.x2 = MAX (ss->x1, ss->x2) + 1;
		reg.extents.y2 = MAX (ss->y1, ss->y2) + 1;

		damageScreenRegion (s, &reg);

		damageScreen (s);
	}
}

static void
shotHandleEvent (CompDisplay *d,
                 XEvent      *event)
{
	CompScreen *s;

	SHOT_DISPLAY (d);

	switch (event->type) {
	case ButtonPress:
		if (isButtonPressEvent (event, &initiate_button))
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;

			shotInitiate (&arg, 1);
		}
		break;
	case ButtonRelease:
		if (initiate_button.button == event->xbutton.button)
		{
			BananaArgument arg;

			arg.name = "root";
			arg.type = BananaInt;
			arg.value.i = event->xbutton.root;

			shotTerminate (&arg, 1);
		}
		break;
	case MotionNotify:
		s = findScreenAtDisplay (d, event->xmotion.root);
		if (s)
			shotHandleMotionEvent (s, pointerX, pointerY);
		break;
	case EnterNotify:
	case LeaveNotify:
		s = findScreenAtDisplay (d, event->xcrossing.root);
		if (s)
			shotHandleMotionEvent (s, pointerX, pointerY);
	default:
		break;
	}

	UNWRAP (sd, d, handleEvent);
	(*d->handleEvent) (d, event);
	WRAP (sd, d, handleEvent, shotHandleEvent);
}

static Bool
shotInitDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	ShotDisplay *sd;

	sd = malloc (sizeof (ShotDisplay));
	if (!sd)
		return FALSE;

	sd->screenPrivateIndex = allocateScreenPrivateIndex (d);
	if (sd->screenPrivateIndex < 0)
	{
		free (sd);
		return FALSE;
	}

	WRAP (sd, d, handleEvent, shotHandleEvent);

	d->base.privates[displayPrivateIndex].ptr = sd;

	return TRUE;
}

static void
shotFiniDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	SHOT_DISPLAY (d);

	freeScreenPrivateIndex (d, sd->screenPrivateIndex);

	UNWRAP (sd, d, handleEvent);

	free (sd);
}

static Bool
shotInitScreen (CompPlugin *p,
                CompScreen *s)
{
	ShotScreen *ss;

	SHOT_DISPLAY (s->display);

	ss = malloc (sizeof (ShotScreen));
	if (!ss)
		return FALSE;

	ss->grabIndex = 0;
	ss->grab      = FALSE;

	WRAP (ss, s, paintScreen, shotPaintScreen);
	WRAP (ss, s, paintOutput, shotPaintOutput);

	s->base.privates[sd->screenPrivateIndex].ptr = ss;

	return TRUE;
}

static void
shotFiniScreen (CompPlugin *p,
                CompScreen *s)
{
	SHOT_SCREEN (s);

	UNWRAP (ss, s, paintScreen);
	UNWRAP (ss, s, paintOutput);

	free (ss);
}

static CompBool
shotInitObject (CompPlugin *p,
                CompObject *o)
{
	static InitPluginObjectProc dispTab[] = {
		(InitPluginObjectProc) 0, /* InitCore */
		(InitPluginObjectProc) shotInitDisplay,
		(InitPluginObjectProc) shotInitScreen
	};

	RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
shotFiniObject (CompPlugin *p,
                CompObject *o)
{
	static FiniPluginObjectProc dispTab[] = {
		(FiniPluginObjectProc) 0, /* FiniCore */
		(FiniPluginObjectProc) shotFiniDisplay,
		(FiniPluginObjectProc) shotFiniScreen
	};

	DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static void
shotChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	if (strcasecmp (optionName, "initiate_button") == 0)
		updateButton (optionValue->s, &initiate_button);
}

static Bool
shotInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("screenshot", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("screenshot");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, shotChangeNotify);

	const BananaValue *
	option_initiate_button = bananaGetOption (bananaIndex,
	                                          "initiate_button",
	                                          -1);

	registerButton (option_initiate_button->s, &initiate_button);

	return TRUE;
}

static void
shotFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable shotVTable = {
	"screenshot",
	shotInit,
	shotFini,
	shotInitObject,
	shotFiniObject
};

CompPluginVTable *
getCompPluginInfo20140724 (void)
{
	return &shotVTable;
}
