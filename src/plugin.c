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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <fusilli-core.h>

CompPlugin *plugins = 0;

static Bool
dlloaderLoadPlugin (CompPlugin *p,
                    const char *path,
                    const char *name)
{
	char        *file;
	void        *dlhand;
	struct stat fileInfo;
	Bool        loaded = FALSE;

	file = malloc ((path ? strlen (path) : 0) + strlen (name) + 8);
	if (!file)
		return FALSE;

	if (path)
		sprintf (file, "%s/lib%s.so", path, name);
	else
		sprintf (file, "lib%s.so", name);

	if (stat (file, &fileInfo) != 0)
	{
		/* file likely not present */
		compLogMessage ("core", CompLogLevelDebug,
		                "Could not stat() file %s : %s",
		                file, strerror (errno));
		free (file);
		return FALSE;
	}

	dlhand = dlopen (file, RTLD_LAZY);
	if (dlhand)
	{
		PluginGetInfoProc getInfo;
		char              *error;

		dlerror ();

		getInfo = (PluginGetInfoProc) dlsym (dlhand,
		                      "getCompPluginInfo20141205");

		error = dlerror ();
		if (error)
		{
			compLogMessage ("core", CompLogLevelError, "dlsym: %s", error);

			getInfo = 0;
		}

		if (getInfo)
		{
			p->vTable = (*getInfo) ();
			if (!p->vTable)
			{
				compLogMessage ("core", CompLogLevelError,
				            "Couldn't get vtable from '%s' plugin",
				            file);
			}
			else
			{
				p->dlhand     = dlhand;
				loaded        = TRUE;
			}
		}
	}
	else
	{
		compLogMessage ("core", CompLogLevelError,
		                "Couldn't load plugin '%s' : %s", file, dlerror ());
	}

	free (file);

	if (!loaded && dlhand)
		dlclose (dlhand);

	return loaded;
}

static int
dlloaderFilter (const struct dirent *name)
{
	int length = strlen (name->d_name);

	if (length < 7)
		return 0;

	if (strncmp (name->d_name, "lib", 3) ||
	    strncmp (name->d_name + length - 3, ".so", 3))
		return 0;

	return 1;
}

static char **
dlloaderListPlugins (const char *path,
                     int        *n)
{
	struct dirent **nameList;
	char      **list;
	char      *name;
	int       length, nFile, i, j = 0;

	if (!path)
		path = ".";

	nFile = scandir (path, &nameList, dlloaderFilter, alphasort);
	if (!nFile)
		return NULL;

	list = malloc ((j + nFile) * sizeof (char *));
	if (!list)
		return NULL;

	for (i = 0; i < nFile; i++)
	{
		length = strlen (nameList[i]->d_name);

		name = malloc ((length - 5) * sizeof (char));
		if (name)
		{
			strncpy (name, nameList[i]->d_name + 3, length - 6);
			name[length - 6] = '\0';

			list[j++] = name;
		}
	}

	if (j)
	{
		*n = j;

		return list;
	}

	free (list);

	return NULL;
}

static Bool
initPlugin (CompPlugin *p)
{
	if (strcmp (p->vTable->name, "core") == 0)
		return TRUE;

	//InitPlugin
	if (!(*p->vTable->init) (p))
	{
		compLogMessage ("core", CompLogLevelError,
		                "InitPlugin '%s' failed", p->vTable->name);
		return FALSE;
	}

	//InitDisplay
	if (p->vTable->initDisplay && !(*p->vTable->initDisplay) (p, &display))
	{
		compLogMessage ("core", CompLogLevelError,
		                "InitDisplay '%s' failed", p->vTable->name);
		return FALSE;
	}

	//InitScreen
	if (p->vTable->initScreen)
	{
		CompScreen *s;
		for (s = display.screens; s; s = s->next)
		{
			if (!(*p->vTable->initScreen) (p, s))
			{
				compLogMessage ("core", CompLogLevelError,
				                "InitScreen '%s' failed", p->vTable->name);
				return FALSE;
			}

			if (p->vTable->initWindow)
			{
				CompWindow *w;
				for (w = s->windows; w; w = w->next)
				{
					if (!(*p->vTable->initWindow) (p, w))
					{
						compLogMessage ("core", CompLogLevelError,
						                "InitWindow '%s' failed", p->vTable->name);
						return FALSE;
					}
				}
			}
		}
	}

	return TRUE;
}

static void
finiPlugin (CompPlugin *p)
{
	if (strcmp (p->vTable->name, "core") == 0)
		return;

	//FiniWindow
	if (p->vTable->finiWindow)
	{
		CompScreen *s;
		CompWindow *w;
		for (s = display.screens; s; s = s->next)
			for (w = s->windows; w; w = w->next)
				(*p->vTable->finiWindow) (p, w);
	}

	//FiniScreen
	if (p->vTable->finiScreen)
	{
		CompScreen *s;
		for (s = display.screens; s; s = s->next)
			(*p->vTable->finiScreen) (p, s);
	}

	//FiniDisplay
	if (p->vTable->finiDisplay)
		(*p->vTable->finiDisplay) (p, &display);

	//FiniPlugin
	(*p->vTable->fini) (p);
}

//run InitWindow(w) for every active plugin
Bool
windowInitPlugins (CompWindow *w)
{
	CompPlugin *p;
	int        i, j = 0;

	for (p = plugins; p; p = p->next)
		j++;

	while (j--)
	{
		i = 0;
		for (p = plugins; i < j; p = p->next)
			i++;

		if (p->vTable->initWindow)
		{
			if (!(*p->vTable->initWindow) (p, w))
			{
				compLogMessage ("core", CompLogLevelError,
				                "InitWindow '%s' failed", p->vTable->name);
				return FALSE;
			}
		}
	}

	return TRUE;
}

//run FiniWindow(w) for every active plugin
void
windowFiniPlugins (CompWindow *w)
{
	CompPlugin *p;
	int        i, j = 0;

	for (p = plugins; p; p = p->next)
		j++;

	while (j--)
	{
		i = 0;
		for (p = plugins; i < j; p = p->next)
			i++;

		if (p->vTable->finiWindow)
			(p->vTable->finiWindow) (p, w);
	}
}

CompPlugin *
findActivePlugin (const char *name)
{
	CompPlugin *p;

	for (p = plugins; p; p = p->next)
	{
		if (strcmp (p->vTable->name, name) == 0)
			return p;
	}

	return 0;
}

void
unloadPlugin (CompPlugin *p)
{
	compLogMessage("core", CompLogLevelInfo,
	               "Unloading plugin: %s", p->vTable->name);

	dlclose (p->dlhand);

	free (p);
}

CompPlugin *
loadPlugin (const char *name)
{
	CompPlugin *p;
	char       *home, *plugindir;
	Bool       status;

	compLogMessage("core", CompLogLevelInfo,
	               "Loading plugin: %s",
	               name);

	p = malloc (sizeof (CompPlugin));
	if (!p)
		return 0;

	p->next            = 0;
	p->dlhand          = 0;
	p->vTable          = 0;

	home = getenv ("HOME");
	if (home)
	{
		plugindir = malloc (strlen (home) + strlen (HOME_PLUGINDIR) + 3);
		if (plugindir)
		{
			sprintf (plugindir, "%s/%s", home, HOME_PLUGINDIR);
			status = dlloaderLoadPlugin (p, plugindir, name);
			free (plugindir);

			if (status)
				return p;
		}
	}

	status = dlloaderLoadPlugin (p, PLUGINDIR, name);
	if (status)
		return p;

	status = dlloaderLoadPlugin (p, NULL, name);
	if (status)
		return p;

	compLogMessage ("core", CompLogLevelError,
	                "Couldn't load plugin '%s'", name);

	free (p);

	return 0;
}

Bool
pushPlugin (CompPlugin *p)
{
	if (findActivePlugin (p->vTable->name))
	{
		compLogMessage ("core", CompLogLevelWarn,
		                "Plugin '%s' already active",
		                p->vTable->name);

		return FALSE;
	}

	p->next = plugins;
	plugins = p;

	if (!initPlugin (p))
	{
		compLogMessage ("core", CompLogLevelError,
		                "Couldn't activate plugin '%s'", p->vTable->name);
		plugins = p->next;

		return FALSE;
	}

	return TRUE;
}

CompPlugin *
popPlugin (void)
{
	CompPlugin *p = plugins;

	if (!p)
		return 0;

	finiPlugin (p);

	plugins = p->next;

	return p;
}

CompPlugin *
getPlugins (void)
{
	return plugins;
}

static Bool
stringExist (char **list,
             int  nList,
             char *s)
{
	int i;

	for (i = 0; i < nList; i++)
		if (strcmp (list[i], s) == 0)
			return TRUE;

	return FALSE;
}

char **
availablePlugins (int *n)
{
	char *home, *plugindir;
	char **list, **currentList, **pluginList, **homeList = NULL;
	int  nCurrentList, nPluginList, nHomeList;
	int  count, i, j;

	home = getenv ("HOME");
	if (home)
	{
		plugindir = malloc (strlen (home) + strlen (HOME_PLUGINDIR) + 3);
		if (plugindir)
		{
			sprintf (plugindir, "%s/%s", home, HOME_PLUGINDIR);
			homeList = dlloaderListPlugins (plugindir, &nHomeList);
			free (plugindir);
		}
	}

	pluginList  = dlloaderListPlugins (PLUGINDIR, &nPluginList);
	currentList = dlloaderListPlugins (NULL, &nCurrentList);

	count = 0;
	if (homeList)
		count += nHomeList;
	if (pluginList)
		count += nPluginList;
	if (currentList)
		count += nCurrentList;

	if (!count)
		return NULL;

	list = malloc (count * sizeof (char *));
	if (!list)
		return NULL;

	j = 0;
	if (homeList)
	{
		for (i = 0; i < nHomeList; i++)
			if (!stringExist (list, j, homeList[i]))
				list[j++] = homeList[i];

		free (homeList);
	}

	if (pluginList)
	{
		for (i = 0; i < nPluginList; i++)
			if (!stringExist (list, j, pluginList[i]))
				list[j++] = pluginList[i];

		free (pluginList);
	}

	if (currentList)
	{
		for (i = 0; i < nCurrentList; i++)
			if (!stringExist (list, j, currentList[i]))
				list[j++] = currentList[i];

		free (currentList);
	}

	*n = j;

	return list;
}
