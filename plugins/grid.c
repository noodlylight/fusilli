/*
 * Compiz Fusion Grid plugin
 *
 * Copyright (c) 2008 Stephen Kennedy <suasol@gmail.com>
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
 * Description:
 *
 * Plugin to act like winsplit revolution (http://www.winsplit-revolution.com/)
 * use <Control><Alt>NUMPAD_KEY to move and tile your windows.
 * 
 * Press the tiling keys several times to cycle through some tiling options.
 */

#include <compiz-core.h>
#include <string.h>
#include "grid_options.h"

#define GRID_DEBUG 0

#if GRID_DEBUG
#   include <stdio.h>
	static FILE* gridOut;
#   define DEBUG_RECT(VAR) fprintf(gridOut, #VAR " %i %i %i %i\n", VAR.x, VAR.y, VAR.width, VAR.height)
#   define DEBUG_PRINT(ARGS) fprintf ARGS
#else
#   define DEBUG_RECT(VAR)
#   define DEBUG_PRINT(ARGS)
#endif

typedef enum
{
    GridUnknown = 0,
    GridBottomLeft = 1,
    GridBottom = 2,
    GridBottomRight = 3,
    GridLeft = 4,
    GridCenter = 5,
    GridRight = 6,
    GridTopLeft = 7,
    GridTop = 8,
    GridTopRight = 9,
} GridType;

typedef struct _GridProps
{
    int gravityRight;
    int gravityDown;
    int numCellsX;
    int numCellsY;
} GridProps;

static const GridProps gridProps[] =
{
    {0,1, 1,1},

    {0,1, 2,2},
    {0,1, 1,2},
    {1,1, 2,2},

    {0,0, 2,1},
    {0,0, 1,1},
    {1,0, 2,1},

    {0,0, 2,2},
    {0,0, 1,2},
    {1,0, 2,2},
};

static void
slotToRect (CompWindow *w,
	    XRectangle *slot,
	    XRectangle *rect)
{
    rect->x = slot->x + w->input.left;
    rect->y = slot->y + w->input.top;
    rect->width = slot->width - (w->input.left + w->input.right);
    rect->height = slot->height - (w->input.top + w->input.bottom);
}

static void
constrainSize (CompWindow *w,
	       XRectangle *slot,
	       XRectangle *rect)
{
    XRectangle workarea;
    XRectangle r;
    int        cw, ch;

    getWorkareaForOutput (w->screen, outputDeviceForWindow (w), &workarea);
    slotToRect (w, slot, &r);

    if (constrainNewWindowSize (w, r.width, r.height, &cw, &ch))
    {
	/* constrained size may put window offscreen, adjust for that case */
	int dx = r.x + cw - workarea.width - workarea.x + w->input.right;
	int dy = r.y + ch - workarea.height - workarea.y + w->input.bottom;

	if ( dx > 0 )
	    r.x -= dx;
	if ( dy > 0 )
	    r.y -= dy;

	r.width = cw;
	r.height = ch;
    }

    *rect = r;
}

static Bool
gridCommon (CompDisplay     *d,
	    CompAction      *action,
	    CompActionState state,
	    CompOption      *option,
	    int             nOption,
	    GridType        where)
{
    Window     xid;
    CompWindow *cw;

    xid = getIntOptionNamed (option, nOption, "window", 0);
    cw  = findWindowAtDisplay (d, xid);
    if (cw)
    {
	XRectangle     workarea;
	XRectangle     desiredSlot;
	XRectangle     desiredRect;
	XRectangle     currentRect;
	GridProps      props = gridProps[where];
	XWindowChanges xwc;

	DEBUG_PRINT ((gridOut, "\nPressed KP_%i\n", where));

	/* get current available area */
	getWorkareaForOutput (cw->screen, outputDeviceForWindow(cw), &workarea);
	DEBUG_RECT (workarea);

	/* Convention:
	 * xxxSlot include decorations (it's the screen area occupied)
	 * xxxRect are undecorated (it's the constrained position
	                            of the contents)
	 */

	/* slice and dice to get desired slot - including decorations */
	desiredSlot.y =  workarea.y + props.gravityDown *
	                 (workarea.height / props.numCellsY);
	desiredSlot.height = workarea.height / props.numCellsY;
	desiredSlot.x =  workarea.x + props.gravityRight *
	                 (workarea.width / props.numCellsX);
	desiredSlot.width = workarea.width / props.numCellsX;
	DEBUG_RECT (desiredSlot);

	/* Adjust for constraints and decorations */
	constrainSize (cw, &desiredSlot, &desiredRect);
	DEBUG_RECT (desiredRect);

	/* Get current rect not including decorations */
	currentRect.x = cw->serverX;
	currentRect.y = cw->serverY;
	currentRect.width  = cw->serverWidth;
	currentRect.height = cw->serverHeight;
	DEBUG_RECT (currentRect);

	if (desiredRect.y == currentRect.y &&
	    desiredRect.height == currentRect.height)
	{
	    int slotWidth33  = workarea.width / 3;
	    int slotWidth66  = workarea.width - slotWidth33;

	    DEBUG_PRINT ((gridOut, "Multi!\n"));

	    if (props.numCellsX == 2) /* keys (1, 4, 7, 3, 6, 9) */
	    {
		if (currentRect.width == desiredRect.width &&
		    currentRect.x == desiredRect.x)
		{
		    desiredSlot.width = slotWidth66;
		    desiredSlot.x = workarea.x +
			            props.gravityRight * slotWidth33;
		}
		else
		{
		    /* tricky, have to allow for window constraints when
		     * computing what the 33% and 66% offsets would be
		     */
		    XRectangle rect33, rect66, slot33, slot66;

		    slot33 = desiredSlot;
		    slot33.x = workarea.x + props.gravityRight * slotWidth66;
		    slot33.width = slotWidth33;
		    constrainSize (cw, &slot33, &rect33);
		    DEBUG_RECT (slot33);
		    DEBUG_RECT (rect33);

		    slot66 = desiredSlot;
		    slot66.x = workarea.x + props.gravityRight * slotWidth33;
		    slot66.width = slotWidth66;
		    constrainSize (cw, &slot66, &rect66);
		    DEBUG_RECT (slot66);
		    DEBUG_RECT (rect66);

		    if (currentRect.width == rect66.width &&
			currentRect.x == rect66.x)
		    {
			desiredSlot.width = slotWidth33;
			desiredSlot.x = workarea.x +
			                props.gravityRight * slotWidth66;
		    }
		}
	    }
	    else /* keys (2, 5, 8) */
	    {
		if (currentRect.width == desiredRect.width &&
		    currentRect.x == desiredRect.x)
		{
		    desiredSlot.width = slotWidth33;
		    desiredSlot.x = workarea.x + slotWidth33;
		}
	    }
	    constrainSize (cw, &desiredSlot, &desiredRect);
	    DEBUG_RECT (desiredRect);
	}

	xwc.x = desiredRect.x;
	xwc.y = desiredRect.y;
	xwc.width  = desiredRect.width;
	xwc.height = desiredRect.height;

	if (cw->mapNum)
	    sendSyncRequest (cw);

	if (cw->state & MAXIMIZE_STATE)
	{
	    /* maximized state interferes with us, clear it */
	    maximizeWindow (cw, 0);
	}

	/* TODO: animate move+resize */
	configureXWindow (cw, CWX | CWY | CWWidth | CWHeight, &xwc);
    }

    return TRUE;
}

#define HANDLER(WHERE)                                        \
    static Bool                                               \
	grid##WHERE(CompDisplay     *d,                       \
		    CompAction      *action,                  \
		    CompActionState state,                    \
		    CompOption      *option,                  \
		    int             nOption)                  \
	{                                                     \
	    return gridCommon (d, action, state,              \
			       option, nOption, Grid##WHERE); \
	}

HANDLER (BottomLeft)
HANDLER (Bottom)
HANDLER (BottomRight)
HANDLER (Left)
HANDLER (Center)
HANDLER (Right)
HANDLER (TopLeft)
HANDLER (Top)
HANDLER (TopRight)

#undef HANDLER

/* Configuration, initialization, boring stuff. */

static Bool
gridInitDisplay (CompPlugin  *p,
		 CompDisplay *d)
{
    if (!checkPluginABI ("core", CORE_ABIVERSION))
	return FALSE;

    gridSetPutCenterKeyInitiate (d, gridCenter);
    gridSetPutLeftKeyInitiate (d, gridLeft);
    gridSetPutRightKeyInitiate (d, gridRight);
    gridSetPutTopKeyInitiate (d, gridTop);
    gridSetPutBottomKeyInitiate (d, gridBottom);
    gridSetPutTopleftKeyInitiate (d, gridTopLeft);
    gridSetPutToprightKeyInitiate (d, gridTopRight);
    gridSetPutBottomleftKeyInitiate (d, gridBottomLeft);
    gridSetPutBottomrightKeyInitiate (d, gridBottomRight);

    return TRUE;
}

static CompBool
gridInitObject (CompPlugin *p,
		CompObject *o)
{
    static InitPluginObjectProc dispTab[] = {
	(InitPluginObjectProc) 0, /* InitCore */
	(InitPluginObjectProc) gridInitDisplay
    };

    RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static Bool
gridInitPlugin (CompPlugin *p)
{
#if GRID_DEBUG
    gridOut = fopen("/tmp/grid.log", "w");
    setlinebuf(gridOut);
#endif

    return TRUE;
}

static void
gridFiniPlugin (CompPlugin *p)
{
#if GRID_DEBUG
    fclose(gridOut);
    gridOut = NULL;
#endif
}

CompPluginVTable gridVTable =
{
    "grid",
    0,
    gridInitPlugin,
    gridFiniPlugin,
    gridInitObject,
    0,
    0,
    0
};

CompPluginVTable *
getCompPluginInfo ()
{
    return &gridVTable;
}
