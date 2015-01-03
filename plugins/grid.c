/*
 * Compiz Fusion Grid plugin
 *
 * Copyright (c) 2008 Stephen Kennedy <suasol@gmail.com>
 * Copyright (c) 2014 Michail Bitzes <noodlylight@gmail.com>
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

#include <fusilli-core.h>
#include <string.h>

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _GridDisplay {
	CompKeyBinding  put_center_key,
	                put_left_key, put_right_key,
	                put_top_key, put_bottom_key,
	                put_topleft_key, put_topright_key,
	                put_bottomleft_key, put_bottomright_key;

	HandleEventProc handleEvent;
} GridDisplay;

#define GET_GRID_DISPLAY(d) \
        ((GridDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define GRID_DISPLAY(d) \
        GridDisplay *gd = GET_GRID_DISPLAY (d)

typedef enum {
	GridUnknown     = 0,
	GridBottomLeft  = 1,
	GridBottom      = 2,
	GridBottomRight = 3,
	GridLeft        = 4,
	GridCenter      = 5,
	GridRight       = 6,
	GridTopLeft     = 7,
	GridTop         = 8,
	GridTopRight    = 9,
} GridType;

typedef struct _GridProps {
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
gridPlaceWindow (Window          xid,
                 GridType        where)
{
	CompWindow *w;

	w  = findWindowAtDisplay (xid);
	if (w)
	{
		XRectangle     workarea;
		XRectangle     desiredSlot;
		XRectangle     desiredRect;
		XRectangle     currentRect;
		GridProps      props = gridProps[where];
		XWindowChanges xwc;

		/* get current available area */
		getWorkareaForOutput (w->screen, outputDeviceForWindow(w), &workarea);

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

		/* Adjust for constraints and decorations */
		constrainSize (w, &desiredSlot, &desiredRect);

		/* Get current rect not including decorations */
		currentRect.x = w->serverX;
		currentRect.y = w->serverY;
		currentRect.width  = w->serverWidth;
		currentRect.height = w->serverHeight;

		if (desiredRect.y == currentRect.y &&
		    desiredRect.height == currentRect.height)
		{
			int slotWidth33  = workarea.width / 3;
			int slotWidth66  = workarea.width - slotWidth33;

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
					constrainSize (w, &slot33, &rect33);

					slot66 = desiredSlot;
					slot66.x = workarea.x + props.gravityRight * slotWidth33;
					slot66.width = slotWidth66;
					constrainSize (w, &slot66, &rect66);

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
			constrainSize (w, &desiredSlot, &desiredRect);
		}

		xwc.x = desiredRect.x;
		xwc.y = desiredRect.y;
		xwc.width  = desiredRect.width;
		xwc.height = desiredRect.height;

		if (w->mapNum)
			sendSyncRequest (w);

		if (w->state & MAXIMIZE_STATE)
		{
			/* maximized state interferes with us, clear it */
			maximizeWindow (w, 0);
		}

		/* TODO: animate move+resize */
		configureXWindow (w, CWX | CWY | CWWidth | CWHeight, &xwc);
	}

	return TRUE;
}

static void
gridHandleEvent (XEvent      *event)
{
	GRID_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &gd->put_center_key))
			gridPlaceWindow (display.activeWindow, GridCenter);

		else if (isKeyPressEvent (event, &gd->put_left_key))
			gridPlaceWindow (display.activeWindow, GridLeft);

		else if (isKeyPressEvent (event, &gd->put_right_key))
			gridPlaceWindow (display.activeWindow, GridRight);

		else if (isKeyPressEvent (event, &gd->put_top_key))
			gridPlaceWindow (display.activeWindow, GridTop);

		else if (isKeyPressEvent (event, &gd->put_bottom_key))
			gridPlaceWindow (display.activeWindow, GridBottom);

		else if (isKeyPressEvent (event, &gd->put_topleft_key))
			gridPlaceWindow (display.activeWindow, GridTopLeft);

		else if (isKeyPressEvent (event, &gd->put_topright_key))
			gridPlaceWindow (display.activeWindow, GridTopRight);

		else if (isKeyPressEvent (event, &gd->put_bottomleft_key))
			gridPlaceWindow (display.activeWindow, GridBottomLeft);

		else if (isKeyPressEvent (event, &gd->put_bottomright_key))
			gridPlaceWindow (display.activeWindow, GridBottomRight);
		break;
	default:
		break;
	}

	UNWRAP (gd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (gd, &display, handleEvent, gridHandleEvent);
}

static Bool
gridInitDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	GridDisplay *gd;

	gd = malloc (sizeof (GridDisplay));
	if (!gd)
		return FALSE;

	WRAP (gd, d, handleEvent, gridHandleEvent);

	const BananaValue *
	option_put_center_key = bananaGetOption (bananaIndex,
	                                         "put_center_key", -1);

	registerKey (option_put_center_key->s, &gd->put_center_key);

	const BananaValue *
	option_put_left_key = bananaGetOption (bananaIndex,
	                                       "put_left_key", -1);

	registerKey (option_put_left_key->s, &gd->put_left_key);

	const BananaValue *
	option_put_right_key = bananaGetOption (bananaIndex,
	                                        "put_right_key", -1);

	registerKey (option_put_right_key->s, &gd->put_right_key);

	const BananaValue *
	option_put_top_key = bananaGetOption (bananaIndex,
	                                      "put_top_key", -1);

	registerKey (option_put_top_key->s, &gd->put_top_key);

	const BananaValue *
	option_put_bottom_key = bananaGetOption (bananaIndex,
	                                         "put_bottom_key", -1);

	registerKey (option_put_bottom_key->s, &gd->put_bottom_key);

	const BananaValue *
	option_put_topleft_key = bananaGetOption (bananaIndex,
	                                          "put_topleft_key", -1);

	registerKey (option_put_topleft_key->s, &gd->put_topleft_key);

	const BananaValue *
	option_put_topright_key = bananaGetOption (bananaIndex,
	                                           "put_topright_key", -1);

	registerKey (option_put_topright_key->s, &gd->put_topright_key);

	const BananaValue *
	option_put_bottomleft_key = bananaGetOption (bananaIndex,
	                                             "put_bottomleft_key", -1);

	registerKey (option_put_bottomleft_key->s, &gd->put_bottomleft_key);

	const BananaValue *
	option_put_bottomright_key = bananaGetOption (bananaIndex,
	                                              "put_bottomright_key", -1);

	registerKey (option_put_bottomright_key->s, &gd->put_bottomright_key);

	d->privates[displayPrivateIndex].ptr = gd;

	return TRUE;
}

static void
gridFiniDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	GRID_DISPLAY (d);

	UNWRAP (gd, d, handleEvent);

	free (gd);
}

static void
gridChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	GRID_DISPLAY (&display);

	if (strcasecmp (optionName, "put_center_key") == 0)
		updateKey (optionValue->s, &gd->put_center_key);

	else if (strcasecmp (optionName, "put_left_key") == 0)
		updateKey (optionValue->s, &gd->put_left_key);

	else if (strcasecmp (optionName, "put_right_key") == 0)
		updateKey (optionValue->s, &gd->put_right_key);

	else if (strcasecmp (optionName, "put_top_key") == 0)
		updateKey (optionValue->s, &gd->put_top_key);

	else if (strcasecmp (optionName, "put_bottom_key") == 0)
		updateKey (optionValue->s, &gd->put_bottom_key);

	else if (strcasecmp (optionName, "put_topleft_key") == 0)
		updateKey (optionValue->s, &gd->put_topleft_key);

	else if (strcasecmp (optionName, "put_topright_key") == 0)
		updateKey (optionValue->s, &gd->put_topright_key);

	else if (strcasecmp (optionName, "put_bottomleft_key") == 0)
		updateKey (optionValue->s, &gd->put_bottomleft_key);

	else if (strcasecmp (optionName, "put_bottomright_key") == 0)
		updateKey (optionValue->s, &gd->put_bottomright_key);
}

static Bool
gridInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("grid", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("grid");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, gridChangeNotify);

	return TRUE;
}

static void
gridFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable gridVTable = {
	"grid",
	gridInit,
	gridFini,
	gridInitDisplay,
	gridFiniDisplay,
	NULL, /* gridInitScreen */
	NULL, /* gridFiniScreen */
	NULL, /* gridInitWindow */
	NULL  /* gridFiniWindow */
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &gridVTable;
}