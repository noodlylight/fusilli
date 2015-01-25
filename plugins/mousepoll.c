/*
 *
 * Compiz mouse position polling plugin
 *
 * mousepoll.c
 *
 * Copyright : (C) 2008 by Dennis Kasprzyk
 * E-mail    : onestone@opencompositing.org
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
 */

#include <compiz-core.h>

#include "compiz-mousepoll.h"

static CompMetadata mousepollMetadata;

static int displayPrivateIndex;
static int functionsPrivateIndex;

typedef struct _MousepollClient MousepollClient;

struct _MousepollClient {
    MousepollClient *next;
    MousepollClient *prev;

    PositionPollingHandle id;
    PositionUpdateProc    update;
};

typedef enum _MousepollDisplayOptions
{
    MP_DISPLAY_OPTION_ABI,
    MP_DISPLAY_OPTION_INDEX,
    MP_DISPLAY_OPTION_MOUSE_POLL_INTERVAL,
    MP_DISPLAY_OPTION_NUM
} MousepollDisplayOptions;

typedef struct _MousepollDisplay {
    int	screenPrivateIndex;

    CompOption opt[MP_DISPLAY_OPTION_NUM];
} MousepollDisplay;

typedef struct _MousepollScreen {

    MousepollClient       *clients;
    PositionPollingHandle freeId;

    CompTimeoutHandle updateHandle;
    int posX;
    int posY;

} MousepollScreen;

#define GET_MOUSEPOLL_DISPLAY(d)				      \
    ((MousepollDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define MOUSEPOLL_DISPLAY(d)		           \
    MousepollDisplay *md = GET_MOUSEPOLL_DISPLAY (d)

#define GET_MOUSEPOLL_SCREEN(s, md)				         \
    ((MousepollScreen *) (s)->base.privates[(md)->screenPrivateIndex].ptr)

#define MOUSEPOLL_SCREEN(s)						        \
    MousepollScreen *ms = GET_MOUSEPOLL_SCREEN (s, GET_MOUSEPOLL_DISPLAY (s->display))

#define NUM_OPTIONS(s) (sizeof ((s)->opt) / sizeof (CompOption))

static Bool
getMousePosition (CompScreen *s)
{
    Window       root_return;
    Window       child_return;
    int          rootX, rootY;
    int          winX, winY;
    unsigned int maskReturn;
    Bool         status;

    MOUSEPOLL_SCREEN (s);

    status = XQueryPointer (s->display->display, s->root,
			    &root_return, &child_return,
			    &rootX, &rootY, &winX, &winY, &maskReturn);

    if (!status || rootX > s->width || rootY > s->height ||
	s->root != root_return)
	return FALSE;

    if ((rootX != ms->posX || rootY != ms->posY))
    {
	ms->posX = rootX;
	ms->posY = rootY;
	return TRUE;
    }
    return FALSE;
}

static Bool
updatePosition (void *c)
{
    CompScreen      *s = (CompScreen *)c;
    MousepollClient *mc;

    MOUSEPOLL_SCREEN (s);

    if (!ms->clients)
	return FALSE;

    if (getMousePosition (s))
    {
	MousepollClient *next;
	for (mc = ms->clients; mc; mc = next)
	{
	    next = mc->next;
	    if (mc->update)
		(*mc->update) (s, ms->posX, ms->posY);
	}
    }

    return TRUE;
}

static PositionPollingHandle
mousepollAddPositionPolling (CompScreen         *s,
			     PositionUpdateProc update)
{
    MOUSEPOLL_SCREEN  (s);
    MOUSEPOLL_DISPLAY (s->display);

    Bool start = FALSE;

    MousepollClient *mc = malloc (sizeof (MousepollClient));

    if (!mc)
	return -1;

    if (!ms->clients)
	start = TRUE;

    mc->update = update;
    mc->id     = ms->freeId;
    ms->freeId++;

    mc->prev = NULL;
    mc->next = ms->clients;

    if (ms->clients)
	ms->clients->prev = mc;

    ms->clients = mc;

    if (start)
    {
	getMousePosition (s);
	ms->updateHandle =
	    compAddTimeout (
		md->opt[MP_DISPLAY_OPTION_MOUSE_POLL_INTERVAL].value.i / 2,
		md->opt[MP_DISPLAY_OPTION_MOUSE_POLL_INTERVAL].value.i,
		updatePosition, s);
    }

    return mc->id;
}

static void
mousepollRemovePositionPolling (CompScreen            *s,
				PositionPollingHandle id)
{
    MOUSEPOLL_SCREEN (s);

    MousepollClient *mc = ms->clients;

    if (ms->clients && ms->clients->id == id)
    {
	ms->clients = ms->clients->next;
	if (ms->clients)
	    ms->clients->prev = NULL;

	free (mc);
	return;
    }

    for (mc = ms->clients; mc; mc = mc->next)
	if (mc->id == id)
	{
	    if (mc->next)
		mc->next->prev = mc->prev;
	    if (mc->prev)
		mc->prev->next = mc->next;
	    free (mc);
	    return;
	}

    if (!ms->clients && ms->updateHandle)
    {
	compRemoveTimeout (ms->updateHandle);
	ms->updateHandle = 0;
    }
}

static void
mousepollGetCurrentPosition (CompScreen *s,
			     int        *x,
			     int        *y)
{
    MOUSEPOLL_SCREEN (s);

    if (!ms->clients)
	getMousePosition (s);

    if (x)
	*x = ms->posX;
    if (y)
	*y = ms->posY;
}

static const CompMetadataOptionInfo mousepollDisplayOptionInfo[] = {
    { "abi", "int", 0, 0, 0 },
    { "index", "int", 0, 0, 0 },
    { "mouse_poll_interval", "int", "<min>1</min><max>500</max><default>10</default>", 0, 0 }
};

static CompOption *
mousepollGetDisplayOptions (CompPlugin  *plugin,
			    CompDisplay *display,
			    int         *count)
{
    MOUSEPOLL_DISPLAY (display);
    *count = NUM_OPTIONS (md);
    return md->opt;
}

static Bool
mousepollSetDisplayOption (CompPlugin      *plugin,
			   CompDisplay     *display,
			   const char      *name,
			   CompOptionValue *value)
{
    CompOption      *o;
    CompScreen      *s;
    MousepollScreen *ms;
    int	            index;
    Bool            status = FALSE;
    MOUSEPOLL_DISPLAY (display);
    o = compFindOption (md->opt, NUM_OPTIONS (md), name, &index);
    if (!o)
	return FALSE;

    switch (index) {
    case MP_DISPLAY_OPTION_ABI:
    case MP_DISPLAY_OPTION_INDEX:
        break;
    case MP_DISPLAY_OPTION_MOUSE_POLL_INTERVAL:
	status = compSetDisplayOption (display, o, value);
	for (s = display->screens; s; s = s->next)
	{
	    ms = GET_MOUSEPOLL_SCREEN (s, md);
	    if (ms->updateHandle)
	    {
		compRemoveTimeout (ms->updateHandle);
		ms->updateHandle =
		    compAddTimeout (
			md->opt[MP_DISPLAY_OPTION_MOUSE_POLL_INTERVAL].value.i
			/ 2,
			md->opt[MP_DISPLAY_OPTION_MOUSE_POLL_INTERVAL].value.i,
   			updatePosition, s);
	    }
	}
	return status;
	break;
    default:
        return compSetDisplayOption (display, o, value);
    }

    return FALSE;
}

static MousePollFunc mousepollFunctions =
{
    .addPositionPolling    = mousepollAddPositionPolling,
    .removePositionPolling = mousepollRemovePositionPolling,
    .getCurrentPosition    = mousepollGetCurrentPosition,
};

static Bool
mousepollInitDisplay (CompPlugin  *p,
		      CompDisplay *d)
{
    MousepollDisplay *md;

    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    md = malloc (sizeof (MousepollDisplay));
    if (!md)
	return FALSE;
    if (!compInitDisplayOptionsFromMetadata (d,
					     &mousepollMetadata,
					     mousepollDisplayOptionInfo,
					     md->opt,
					     MP_DISPLAY_OPTION_NUM))
    {
	free (md);
	return FALSE;
    }

    md->screenPrivateIndex = allocateScreenPrivateIndex (d);
    if (md->screenPrivateIndex < 0)
    {
	compFiniDisplayOptions (d, md->opt, MP_DISPLAY_OPTION_NUM);
	free (md);
	return FALSE;
    }

    md->opt[MP_DISPLAY_OPTION_ABI].value.i   = MOUSEPOLL_ABIVERSION;
    md->opt[MP_DISPLAY_OPTION_INDEX].value.i = functionsPrivateIndex;

    d->base.privates[displayPrivateIndex].ptr   = md;
    d->base.privates[functionsPrivateIndex].ptr = &mousepollFunctions;
    return TRUE;
}

static void
mousepollFiniDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    MOUSEPOLL_DISPLAY (d);

    compFiniDisplayOptions (d, md->opt, MP_DISPLAY_OPTION_NUM);
    free (md);
}

static Bool
mousepollInitScreen (CompPlugin *p,
		     CompScreen *s)
{
    MousepollScreen *ms;

    MOUSEPOLL_DISPLAY (s->display);

    ms = malloc (sizeof (MousepollScreen));
    if (!ms)
	return FALSE;

    ms->posX = 0;
    ms->posY = 0;

    ms->clients = NULL;
    ms->freeId  = 1;
    
    ms->updateHandle = 0;

    s->base.privates[md->screenPrivateIndex].ptr = ms;
    return TRUE;
}

static void
mousepollFiniScreen (CompPlugin *p,
		     CompScreen *s)
{
    MOUSEPOLL_SCREEN (s);

    if (ms->updateHandle)
	compRemoveTimeout (ms->updateHandle);

    free (ms);
}

static CompBool
mousepollInitObject (CompPlugin *p,
		     CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) mousepollInitDisplay,
	(InitPluginObjectProc) mousepollInitScreen
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
mousepollFiniObject (CompPlugin *p,
		     CompObject *o)
{
    static FiniPluginObjectProc dispTab[] = {
	(FiniPluginObjectProc) 0, /* FiniCore */
	(FiniPluginObjectProc) mousepollFiniDisplay,
	(FiniPluginObjectProc) mousepollFiniScreen
    };

    DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static Bool
mousepollInit (CompPlugin *p)
{
    if (!compInitPluginMetadataFromInfo (&mousepollMetadata,
					 p->vTable->name,
					 mousepollDisplayOptionInfo,
					 MP_DISPLAY_OPTION_NUM,
					 NULL, 0))
	return FALSE;

    displayPrivateIndex = allocateDisplayPrivateIndex ();
    if (displayPrivateIndex < 0)
    {
	compFiniMetadata (&mousepollMetadata);
	return FALSE;
    }

    functionsPrivateIndex = allocateDisplayPrivateIndex ();
    if (functionsPrivateIndex < 0)
    {
	freeDisplayPrivateIndex (displayPrivateIndex);
	compFiniMetadata (&mousepollMetadata);
	return FALSE;
    }
    
    compAddMetadataFromFile (&mousepollMetadata, p->vTable->name);
    return TRUE;
}

static CompOption *
mousepollGetObjectOptions (CompPlugin *plugin,
			   CompObject *object,
			   int        *count)
{
    static GetPluginObjectOptionsProc dispTab[] = {
	(GetPluginObjectOptionsProc) 0, /* GetCoreOptions */
	(GetPluginObjectOptionsProc) mousepollGetDisplayOptions
    };

    *count = 0;
    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab),
		     NULL, (plugin, object, count));
}

static CompBool
mousepollSetObjectOption (CompPlugin      *plugin,
			  CompObject      *object,
			  const char      *name,
			  CompOptionValue *value)
{
    static SetPluginObjectOptionProc dispTab[] = {
	(SetPluginObjectOptionProc) 0, /* SetCoreOption */
	(SetPluginObjectOptionProc) mousepollSetDisplayOption
    };

    RETURN_DISPATCH (object, dispTab, ARRAY_SIZE (dispTab), FALSE,
		     (plugin, object, name, value));
}

static void
mousepollFini (CompPlugin *p)
{
    freeDisplayPrivateIndex (displayPrivateIndex);
    freeDisplayPrivateIndex (functionsPrivateIndex);
    compFiniMetadata (&mousepollMetadata);
}

static CompMetadata *
mousepollGetMetadata (CompPlugin *plugin)
{
    return &mousepollMetadata;
}

CompPluginVTable mousepollVTable = {
    "mousepoll",
    mousepollGetMetadata,
    mousepollInit,
    mousepollFini,
    mousepollInitObject,
    mousepollFiniObject,
    mousepollGetObjectOptions,
    mousepollSetObjectOption
};

CompPluginVTable *
getCompPluginInfo20070830 (void)
{
    return &mousepollVTable;
}
