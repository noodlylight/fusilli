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

#ifdef USE_INOTIFY
typedef struct _CompInotifyWatch {
	struct _CompInotifyWatch *next;
	CompFileWatchHandle      handle;
	int                      wd;  //inotify_add_watch
} CompInotifyWatch;

static int               fd; //inotify_init

static CompInotifyWatch  *watch; //linked list - one node per watched file.

static CompWatchFdHandle watchFdHandle; //compAddWatchFd

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

static Bool
dbusProcessMessages (void *data)
{
	DBusDispatchStatus status;

	do
	{
		dbus_connection_read_write_dispatch (core.dbusConnection, 0);
		status = dbus_connection_get_dispatch_status (core.dbusConnection);
	}
	while (status == DBUS_DISPATCH_DATA_REMAINS);

	return TRUE;
}

static void
initDbus (void)
{
	DBusError error;
	dbus_bool_t status;
	int fd, ret;

	dbus_error_init (&error);

	core.dbusConnection = dbus_bus_get (DBUS_BUS_SESSION, &error);

	if (dbus_error_is_set (&error))
	{
		compLogMessage ("core", CompLogLevelError,
		                "dbus_bus_get error: %s", error.message);

		dbus_error_free (&error);

		core.dbusConnection = NULL;
	}

	ret = dbus_bus_request_name (core.dbusConnection,
	                             "org.fusilli",
	                             DBUS_NAME_FLAG_REPLACE_EXISTING |
	                             DBUS_NAME_FLAG_ALLOW_REPLACEMENT,
	                             &error);

	if (dbus_error_is_set (&error))
	{
		compLogMessage ("core", CompLogLevelError,
		                "dbus_bus_request_name error: %s", error.message);

		dbus_error_free (&error);

		core.dbusConnection = NULL;
	}

	dbus_error_free (&error);

	if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
	{
		compLogMessage ("core", CompLogLevelError,
		                "dbus_bus_request_name reply is not primary owner");

		core.dbusConnection = NULL;
	}

	if (core.dbusConnection != NULL)
	{
		status = dbus_connection_get_unix_fd (core.dbusConnection, &fd);

		if (!status)
		{
			compLogMessage ("core", CompLogLevelError,
			                "dbus_connection_get_unix_fd failed");

			core.dbusConnection = NULL;
		}

		core.dbusWatchFdHandle = compAddWatchFd (fd,
		                            POLLIN | POLLPRI | POLLHUP | POLLERR,
		                            dbusProcessMessages,
		                            0);
	}
}

CompBool
initCore (void)
{
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

	initDbus ();

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

	compRemoveWatchFd (core.dbusWatchFdHandle);

	if (core.watchPollFds)
		free (core.watchPollFds);

	while ((p = popPlugin ()))
		unloadPlugin (p);

	dbus_bus_release_name (core.dbusConnection, "org.fusilli", NULL);

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