/*
 * Copyright © 2007 Novell, Inc.
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

#include <string.h>

#ifdef USE_INOTIFY
#include <unistd.h>
#include <poll.h>
#include <sys/inotify.h>
#endif

#include <fusilli-core.h>

CompCore core;

static char *corePrivateIndices = 0;
static int  corePrivateLen = 0;

#ifdef USE_INOTIFY
typedef struct _CompInotifyWatch {
	struct _CompInotifyWatch *next;
	CompFileWatchHandle      handle;
	int                      wd;  //inotify_add_watch
} CompInotifyWatch;

static int               fd; //inotify_init

static CompInotifyWatch  *watch; //linked list - one node per watched file.

static CompWatchFdHandle watchFdHandle; //compAddWatchFd
#endif

static int
reallocCorePrivate (int  size,
                    void *closure)
{
	void *privates;

	privates = realloc (core.base.privates, size * sizeof (CompPrivate));
	if (!privates)
		return FALSE;

	core.base.privates = (CompPrivate *) privates;

	return TRUE;
}

int
allocCoreObjectPrivateIndex (CompObject *parent)
{
	return allocatePrivateIndex (&corePrivateLen,
	                             &corePrivateIndices,
	                             reallocCorePrivate,
	                             0);
}

void
freeCoreObjectPrivateIndex (CompObject *parent,
                            int        index)
{
	freePrivateIndex (corePrivateLen, corePrivateIndices, index);
}

CompBool
forEachCoreObject (CompObject         *parent,
                   ObjectCallBackProc proc,
                   void               *closure)
{
	return TRUE;
}

char *
nameCoreObject (CompObject *object)
{
	return NULL;
}

CompObject *
findCoreObject (CompObject *parent,
                const char *name)
{
	return NULL;
}

int
allocateCorePrivateIndex (void)
{
	return compObjectAllocatePrivateIndex (NULL, COMP_OBJECT_TYPE_CORE);
}

void
freeCorePrivateIndex (int index)
{
	compObjectFreePrivateIndex (NULL, COMP_OBJECT_TYPE_CORE, index);
}

static CompBool
initCorePluginForObject (CompPlugin *p,
                         CompObject *o)
{
	return TRUE;
}

static void
finiCorePluginForObject (CompPlugin *p,
                         CompObject *o)
{
}

static void
coreObjectAdd (CompObject *parent,
               CompObject *object)
{
	object->parent = parent;
}

static void
coreObjectRemove (CompObject *parent,
                  CompObject *object)
{
	object->parent = NULL;
}

#ifdef USE_INOTIFY

static Bool
inotifyProcessEvents (void *data)
{
	char buf[256 * (sizeof (struct inotify_event) + 16)];
	int  len;

	len = read (fd, buf, sizeof (buf));
	if (len < 0)
	{
		perror ("read");
	}
	else
	{
		struct inotify_event *event;
		CompInotifyWatch     *iw;
		CompFileWatch        *fw;
		int                  i = 0;

		while (i < len)
		{
			event = (struct inotify_event *) &buf[i];

			for (iw = watch; iw; iw = iw->next)
				if (iw->wd == event->wd)
					break;

			if (iw)
			{
				for (fw = core.fileWatch; fw; fw = fw->next)
					if (fw->handle == iw->handle)
						break;

				if (fw)
				{
					if (event->len)
						(*fw->callBack) (event->name, fw->closure);
					else
						(*fw->callBack) (NULL, fw->closure);
				}
			}

			i += sizeof (*event) + event->len;
		}
	}

	return TRUE;
}

static void
inotifyAddFile (CompFileWatch *fileWatch)
{
	if (fd < 0)
		return;

	CompInotifyWatch *iw;
	int mask = 0;

	if (fileWatch->mask & NOTIFY_CREATE_MASK)
		mask |= IN_CREATE;

	if (fileWatch->mask & NOTIFY_DELETE_MASK)
		mask |= IN_DELETE;

	if (fileWatch->mask & NOTIFY_MOVE_MASK)
		mask |= IN_MOVE;

	if (fileWatch->mask & NOTIFY_MODIFY_MASK)
		mask |= IN_MODIFY;

	iw = malloc (sizeof (CompInotifyWatch));
	if (!iw)
		return;

	iw->handle = fileWatch->handle;
	iw->wd     = inotify_add_watch (fd, fileWatch->path, mask);

	if (iw->wd < 0)
	{
		perror ("inotify_add_watch");
		free (iw);
		return;
	}

	iw->next  = watch;
	watch = iw;
}

static void
inotifyRemoveFile (CompFileWatch *fileWatch)
{
	if (fd < 0)
		return;

	CompInotifyWatch *p = 0, *iw;

	for (iw = watch; iw; iw = iw->next)
	{
		if (iw->handle == fileWatch->handle)
			break;

		p = iw;
	}

	if (iw)
	{
		if (p)
			p->next = iw->next;
		else
			watch = iw->next;

		if (inotify_rm_watch (fd, iw->wd))
			perror ("inotify_rm_watch");

		free (iw);
	}
}

#endif

CompBool
initCore (void)
{
	CompPlugin *corePlugin;

	compObjectInit (&core.base, 0, COMP_OBJECT_TYPE_CORE);

	core.tmpRegion = XCreateRegion ();
	if (!core.tmpRegion)
		return FALSE;

	core.outputRegion = XCreateRegion ();
	if (!core.outputRegion)
	{
		XDestroyRegion (core.tmpRegion);
		return FALSE;
	}

	core.fileWatch = NULL;
	core.lastFileWatchHandle = 1;

	core.timeouts = NULL;
	core.lastTimeoutHandle = 1;

	core.watchFds = NULL;
	core.lastWatchFdHandle = 1;
	core.watchPollFds = NULL;
	core.nWatchFds = 0;

	gettimeofday (&core.lastTimeout, 0);

	core.initPluginForObject = initCorePluginForObject;
	core.finiPluginForObject = finiCorePluginForObject;

	core.objectAdd    = coreObjectAdd;
	core.objectRemove = coreObjectRemove;

	core.sessionEvent = sessionEvent;
	core.logMessage   = logMessage;

#ifdef USE_INOTIFY
	watch = NULL;

	fd = inotify_init ();

	if (fd >= 0)
	{
		watchFdHandle = compAddWatchFd (fd,
		                            POLLIN | POLLPRI | POLLHUP | POLLERR,
		                            inotifyProcessEvents,
		                            NULL);
	}
	else
	{
		perror ("inotify_init");
	}
#endif

	corePlugin = loadPlugin ("core");
	if (!corePlugin)
	{
		compLogMessage ("core", CompLogLevelFatal,
		                "Couldn't load core plugin");
		return FALSE;
	}

	if (!pushPlugin (corePlugin))
	{
		compLogMessage ("core", CompLogLevelFatal,
		                "Couldn't activate core plugin");
		return FALSE;
	}

	return TRUE;
}

void
finiCore (void)
{
	CompPlugin *p;

#ifdef USE_INOTIFY
	if (fd >= 0)
	{
		CompFileWatch *fw;

		compRemoveWatchFd (watchFdHandle);

		for (fw = core.fileWatch; fw; fw = fw->next)
			inotifyRemoveFile (fw);

		close (fd);
	}
#endif

	if (core.watchPollFds)
		free (core.watchPollFds);

	while ((p = popPlugin ()))
		unloadPlugin (p);

	XDestroyRegion (core.outputRegion);
	XDestroyRegion (core.tmpRegion);

	removeDisplay ();
}

CompFileWatchHandle
addFileWatch (const char             *path,
              int                    mask,
              FileWatchCallBackProc callBack,
              void                  *closure)
{
	CompFileWatch *fileWatch;

	fileWatch = malloc (sizeof (CompFileWatch));
	if (!fileWatch)
		return 0;

	fileWatch->path	= strdup (path);
	fileWatch->mask	= mask;
	fileWatch->callBack = callBack;
	fileWatch->closure  = closure;
	fileWatch->handle   = core.lastFileWatchHandle++;

	if (core.lastFileWatchHandle == MAXSHORT)
		core.lastFileWatchHandle = 1;

	fileWatch->next = core.fileWatch;
	core.fileWatch = fileWatch;

#ifdef USE_INOTIFY
	inotifyAddFile (fileWatch);
#endif

	return fileWatch->handle;
}

void
removeFileWatch (CompFileWatchHandle handle)
{
	CompFileWatch *p = 0, *w;

	for (w = core.fileWatch; w; w = w->next)
	{
		if (w->handle == handle)
			break;

		p = w;
	}

	if (w)
	{
		if (p)
			p->next = w->next;
		else
			core.fileWatch = w->next;

#ifdef USE_INOTIFY
			inotifyRemoveFile (w);
#endif

		if (w->path)
			free (w->path);

		free (w);
	}
}

int
getCoreABI (void)
{
	return CORE_ABIVERSION;
}