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

#ifdef HAVE_CONFIG_H
#  include "../config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fusilli-core.h>

#define DEFAULT_CONFDIR ".config/fusilli"
#define DEFAULT_CONFFILE ".config/fusilli/banana.xml"

char *programName;
char **programArgv;
int  programArgc;

char *backgroundImage = NULL;

REGION   emptyRegion;
REGION   infiniteRegion;
GLushort defaultColor[4] = { 0xffff, 0xffff, 0xffff, 0xffff };
Window   currentRoot = 0;

Bool shutDown = FALSE;
Bool restartSignal = FALSE;
Bool coreInitialized = FALSE;

CompWindow *lastFoundWindow = 0;
CompWindow *lastDamagedWindow = 0;

Bool replaceCurrentWm = FALSE;
Bool indirectRendering = FALSE;
Bool strictBinding = TRUE;
Bool useDesktopHints = FALSE;
Bool onlyCurrentScreen = FALSE;
static Bool debugOutput = FALSE;

#ifdef USE_COW
Bool useCow = TRUE;
#endif

int coreBananaIndex;

char *metaDataDir = NULL;
char *configurationFile = NULL;

static void
usage (void)
{
	printf ("Usage: %s\n"
	        "\t[--display DISPLAY] "
	        "[--bg-image PNG] "
	        "[--indirect-rendering]\n"
	        "\t[--keep-desktop-hints] "
	        "[--loose-binding] "
	        "[--replace]\n"
	        "\t[--sm-disable] "
	        "[--sm-client-id ID] "
	        "[--only-current-screen]\n"
	        "\t[--metadatadir DIR] "
	        "[--bananafile FILE]\n    "

#ifdef USE_COW
	        "\t[--use-root-window]\n "
#endif

	        "\t[--debug] "
	        "[--version] "
	        "[--help]\n",
	        programName);
}

void
compLogMessage (const char   *componentName,
                CompLogLevel level,
                const char   *format,
                ...)
{
	va_list args;
	char    message[2048];

	va_start (args, format);

	vsnprintf (message, 2048, format, args);

	if (coreInitialized)
		(*core.logMessage) (componentName, level, message);
	else
		logMessage (componentName, level, message);

	va_end (args);
}

void
logMessage (const char   *componentName,
            CompLogLevel level,
            const char   *message)
{
	if (!debugOutput && level >= CompLogLevelDebug)
		return;

	fprintf (stderr, "%s (%s) - %s: %s\n",
	         programName, componentName,
	         logLevelToString (level), message);
}

const char *
logLevelToString (CompLogLevel level)
{
	switch (level) {
	case CompLogLevelFatal:
		return "Fatal";
	case CompLogLevelError:
		return "Error";
	case CompLogLevelWarn:
		return "Warn";
	case CompLogLevelInfo:
		return "Info";
	case CompLogLevelDebug:
		return "Debug";
	default:
		break;
	}

	return "Unknown";
}

static void
signalHandler (int sig)
{
	int status;

	switch (sig) {
	case SIGCHLD:
		waitpid (-1, &status, WNOHANG | WUNTRACED);
		break;
	case SIGHUP:
		restartSignal = TRUE;
		break;
	case SIGINT:
	case SIGTERM:
		shutDown = TRUE;
	default:
		break;
	}
}

int
main (int argc, char **argv)
{
	char      *displayName = 0;
	int       i;
	Bool      disableSm = FALSE;
	char      *clientId = NULL;

	programName = argv[0];
	programArgc = argc;
	programArgv = argv;

	signal (SIGHUP, signalHandler);
	signal (SIGCHLD, signalHandler);
	signal (SIGINT, signalHandler);
	signal (SIGTERM, signalHandler);

	emptyRegion.rects = &emptyRegion.extents;
	emptyRegion.numRects = 0;
	emptyRegion.extents.x1 = 0;
	emptyRegion.extents.y1 = 0;
	emptyRegion.extents.x2 = 0;
	emptyRegion.extents.y2 = 0;
	emptyRegion.size = 0;

	infiniteRegion.rects = &infiniteRegion.extents;
	infiniteRegion.numRects = 1;
	infiniteRegion.extents.x1 = MINSHORT;
	infiniteRegion.extents.y1 = MINSHORT;
	infiniteRegion.extents.x2 = MAXSHORT;
	infiniteRegion.extents.y2 = MAXSHORT;

	for (i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "--help"))
		{
			usage ();
			return 0;
		}
		else if (!strcmp (argv[i], "--version"))
		{
			printf (PACKAGE_STRING "\n");
			return 0;
		}
		else if (!strcmp (argv[i], "--debug"))
		{
			debugOutput = TRUE;
		}
		else if (!strcmp (argv[i], "--display"))
		{
			if (i + 1 < argc)
				displayName = argv[++i];
		}
		else if (!strcmp (argv[i], "--indirect-rendering"))
		{
			/* force Mesa libGL into indirect rendering mode, because
			   glXQueryExtensionsString is context-independant */
			setenv ("LIBGL_ALWAYS_INDIRECT", "1", True);
			indirectRendering = TRUE;
		}
		else if (!strcmp (argv[i], "--loose-binding"))
		{
			strictBinding = FALSE;
		}
		else if (!strcmp (argv[i], "--ignore-desktop-hints"))
		{
			/* keep command line parameter for backward compatibility */
			useDesktopHints = FALSE;
		}
		else if (!strcmp (argv[i], "--keep-desktop-hints"))
		{
			useDesktopHints = TRUE;
		}
		else if (!strcmp (argv[i], "--only-current-screen"))
		{
			onlyCurrentScreen = TRUE;
		}
		else if (!strcmp (argv[i], "--metadatadir"))
		{
			if (i + 1 < argc)
				metaDataDir = strdup (argv[++i]);
		}
		else if (!strcmp (argv[i], "--bananafile"))
		{
			if (i + 1 < argc)
				configurationFile = strdup (argv[++i]);
		}

#ifdef USE_COW
		else if (!strcmp (argv[i], "--use-root-window"))
		{
			useCow = FALSE;
		}
#endif

		else if (!strcmp (argv[i], "--replace"))
		{
			replaceCurrentWm = TRUE;
		}
		else if (!strcmp (argv[i], "--sm-disable"))
		{
			disableSm = TRUE;
		}
		else if (!strcmp (argv[i], "--sm-client-id"))
		{
			if (i + 1 < argc)
				clientId = argv[++i];
		}
		else if (!strcmp (argv[i], "--bg-image"))
		{
			if (i + 1 < argc)
				backgroundImage = argv[++i];
		}
		else
		{
			compLogMessage ("core", CompLogLevelWarn,
			                "Unknown option '%s'\n", argv[i]);
		}
	}

	printf("Fusilli Window Manager\n");
	printf("Version: %s\n", PACKAGE_VERSION);
#ifdef USE_INOTIFY
	printf("Built with inotify support\n");
#else
	printf("Built without inotify support\n");
#endif

	if (!initCore ())
		return 1;

	if (!metaDataDir)
		metaDataDir = strdup (METADATADIR);

	if (!configurationFile)
	{
		char *home;
		home = getenv ("HOME");
		if (home)
		{
			//ensure that ~/.config/fusilli exists;
			char *dir;
			dir = malloc (strlen (home) + strlen (DEFAULT_CONFDIR) + 2);
			sprintf (dir, "%s/%s", home, DEFAULT_CONFDIR);
			mkdir (dir, 0744);
			free (dir);

			char *path;
			path = malloc (strlen (home) + strlen (DEFAULT_CONFFILE) + 2);
			sprintf (path, "%s/%s", home, DEFAULT_CONFFILE);
			configurationFile = strdup (path);
			free (path);
		}
	}

	bananaInit (metaDataDir, configurationFile);

	coreBananaIndex = bananaLoadPlugin ("core");

	coreInitialized = TRUE;

	if (!disableSm)
		initSession (clientId);

	if (!addDisplay (displayName))
		return 1;

	eventLoop ();

	if (!disableSm)
		closeSession ();

	coreInitialized = FALSE;

	finiCore ();

	bananaUnloadPlugin (coreBananaIndex);

	bananaFini ();

	free (metaDataDir);
	free (configurationFile);

	if (restartSignal)
	{
		execvp (programName, programArgv);
		return 1;
	}

	return 0;
}
