/**
 *
 * Compiz group plugin
 *
 * group-internal.h
 *
 * Copyright : (C) 2006-2007 by Patrick Niklaus, Roi Cohen, Danny Baumann
 * Authors: Patrick Niklaus <patrick.niklaus@googlemail.com>
 *          Roi Cohen       <roico.beryl@gmail.com>
 *          Danny Baumann   <maniac@opencompositing.org>
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

#ifndef _GROUP_H
#define _GROUP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include <X11/Xlib.h>
#include <cairo/cairo-xlib-xrender.h>
#include <compiz-core.h>
#include <compiz-text.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>

#include <math.h>
#include <limits.h>

#include "group_options.h"

/*
 * Constants
 *
 */
#define PI 3.1415926535897

/*
 * Helpers
 *
 */
#define GET_GROUP_DISPLAY(d) \
    ((GroupDisplay *) (d)->base.privates[groupDisplayPrivateIndex].ptr)
#define GROUP_DISPLAY(d) \
    GroupDisplay *gd = GET_GROUP_DISPLAY (d)

#define GET_GROUP_SCREEN(s, gd) \
    ((GroupScreen *) (s)->base.privates[(gd)->screenPrivateIndex].ptr)
#define GROUP_SCREEN(s) \
    GroupScreen *gs = GET_GROUP_SCREEN (s, GET_GROUP_DISPLAY (s->display))

#define GET_GROUP_WINDOW(w, gs) \
    ((GroupWindow *) (w)->base.privates[(gs)->windowPrivateIndex].ptr)
#define GROUP_WINDOW(w) \
    GroupWindow *gw = GET_GROUP_WINDOW (w, \
		      GET_GROUP_SCREEN  (w->screen, \
		      GET_GROUP_DISPLAY (w->screen->display)))

#define WIN_X(w) (w->attrib.x)
#define WIN_Y(w) (w->attrib.y)
#define WIN_WIDTH(w) (w->attrib.width)
#define WIN_HEIGHT(w) (w->attrib.height)

#define WIN_CENTER_X(w) (WIN_X (w) + (WIN_WIDTH (w) / 2))
#define WIN_CENTER_Y(w) (WIN_Y (w) + (WIN_HEIGHT (w) / 2))

/* definitions used for glow painting */
#define WIN_REAL_X(w) (w->attrib.x - w->input.left)
#define WIN_REAL_Y(w) (w->attrib.y - w->input.top)
#define WIN_REAL_WIDTH(w) (w->width + 2 * w->attrib.border_width + \
			   w->input.left + w->input.right)
#define WIN_REAL_HEIGHT(w) (w->height + 2 * w->attrib.border_width + \
			    w->input.top + w->input.bottom)

#define TOP_TAB(g) ((g)->topTab->window)
#define PREV_TOP_TAB(g) ((g)->prevTopTab->window)
#define NEXT_TOP_TAB(g) ((g)->nextTopTab->window)

#define HAS_TOP_WIN(group) (((group)->topTab) && ((group)->topTab->window))
#define HAS_PREV_TOP_WIN(group) (((group)->prevTopTab) && \
				 ((group)->prevTopTab->window))

#define IS_TOP_TAB(w, group) (HAS_TOP_WIN (group) && \
			      ((TOP_TAB (group)->id) == (w)->id))
#define IS_PREV_TOP_TAB(w, group) (HAS_PREV_TOP_WIN (group) && \
				   ((PREV_TOP_TAB (group)->id) == (w)->id))

/*
 * Structs
 *
 */

/*
 * Window states
 */
typedef enum {
    WindowNormal = 0,
    WindowMinimized,
    WindowShaded
} GroupWindowState;

/*
 * Screengrab states
 */
typedef enum {
    ScreenGrabNone = 0,
    ScreenGrabSelect,
    ScreenGrabTabDrag
} GroupScreenGrabState;

/*
 * Ungrouping states
 */
typedef enum {
    UngroupNone = 0,
    UngroupAll,
    UngroupSingle
} GroupUngroupState;

/*
 * Rotation direction for change tab animation
 */
typedef enum {
    RotateUncertain = 0,
    RotateLeft,
    RotateRight
} ChangeTabAnimationDirection;

typedef struct _GlowTextureProperties {
    char *textureData;
    int  textureSize;
    int  glowOffset;
} GlowTextureProperties;

/*
 * Structs for pending callbacks
 */
typedef struct _GroupPendingMoves GroupPendingMoves;
struct _GroupPendingMoves {
    CompWindow        *w;
    int               dx;
    int               dy;
    Bool              immediate;
    Bool              sync;
    GroupPendingMoves *next;
};

typedef struct _GroupPendingGrabs GroupPendingGrabs;
struct _GroupPendingGrabs {
    CompWindow        *w;
    int               x;
    int               y;
    unsigned int      state;
    unsigned int      mask;
    GroupPendingGrabs *next;
};

typedef struct _GroupPendingUngrabs GroupPendingUngrabs;
struct _GroupPendingUngrabs {
    CompWindow          *w;
    GroupPendingUngrabs *next;
};

typedef struct _GroupPendingSyncs GroupPendingSyncs;
struct _GroupPendingSyncs {
    CompWindow        *w;
    GroupPendingSyncs *next;
};

/*
 * Pointer to display list
 */
extern int groupDisplayPrivateIndex;

/*
 * PaintState
 */

/* Mask values for groupTabSetVisibility */
#define SHOW_BAR_INSTANTLY_MASK (1 << 0)
#define PERMANENT		(1 << 1)

/* Mask values for tabbing animation */
#define IS_ANIMATED		(1 << 0)
#define FINISHED_ANIMATION	(1 << 1)
#define CONSTRAINED_X		(1 << 2)
#define CONSTRAINED_Y		(1 << 3)
#define DONT_CONSTRAIN		(1 << 4)
#define IS_UNGROUPING           (1 << 5)

typedef enum {
    PaintOff = 0,
    PaintFadeIn,
    PaintFadeOut,
    PaintOn,
    PaintPermanentOn
} PaintState;

typedef enum {
    AnimationNone = 0,
    AnimationPulse,
    AnimationReflex
} GroupAnimationType;

typedef enum {
    NoTabChange = 0,
    TabChangeOldOut,
    TabChangeNewIn
} TabChangeState;

typedef enum {
    NoTabbing = 0,
    Tabbing,
    Untabbing
} TabbingState;

typedef struct _GroupCairoLayer {
    CompTexture	    texture;

    /* used if layer is used for cairo drawing */
    unsigned char   *buffer;
    cairo_surface_t *surface;
    cairo_t	    *cairo;

    /* used if layer is used for text drawing */
    Pixmap pixmap;

    int texWidth;
    int texHeight;

    PaintState state;
    int        animationTime;
} GroupCairoLayer;

/*
 * GroupTabBarSlot
 */
typedef struct _GroupTabBarSlot GroupTabBarSlot;
struct _GroupTabBarSlot {
    GroupTabBarSlot *prev;
    GroupTabBarSlot *next;

    Region region;

    CompWindow *window;

    /* For DnD animations */
    int	  springX;
    int	  speed;
    float msSinceLastMove;
};

/*
 * GroupTabBar
 */
typedef struct _GroupTabBar {
    GroupTabBarSlot *slots;
    GroupTabBarSlot *revSlots;
    int		    nSlots;

    GroupTabBarSlot *hoveredSlot;
    GroupTabBarSlot *textSlot;

    GroupCairoLayer *textLayer;
    GroupCairoLayer *bgLayer;
    GroupCairoLayer *selectionLayer;

    /* For animations */
    int                bgAnimationTime;
    GroupAnimationType bgAnimation;

    PaintState state;
    int        animationTime;
    Region     region;
    int        oldWidth;

    CompTimeoutHandle timeoutHandle;

    /* For DnD animations */
    int   leftSpringX, rightSpringX;
    int   leftSpeed, rightSpeed;
    float leftMsSinceLastMove, rightMsSinceLastMove;
} GroupTabBar;

/*
 * GroupGlow
 */

typedef struct _GlowQuad {
    BoxRec     box;
    CompMatrix matrix;
} GlowQuad;

#define GLOWQUAD_TOPLEFT	 0
#define GLOWQUAD_TOPRIGHT	 1
#define GLOWQUAD_BOTTOMLEFT	 2
#define GLOWQUAD_BOTTOMRIGHT     3
#define GLOWQUAD_TOP		 4
#define GLOWQUAD_BOTTOM		 5
#define GLOWQUAD_LEFT		 6
#define GLOWQUAD_RIGHT		 7
#define NUM_GLOWQUADS		 8

/*
 * GroupSelection
 */
typedef struct _GroupSelection GroupSelection;
struct _GroupSelection {
    GroupSelection *prev;
    GroupSelection *next;

    CompScreen *screen;
    CompWindow **windows;
    int        nWins;

    /* Unique identifier for this group */
    long int identifier;

    GroupTabBarSlot* topTab;
    GroupTabBarSlot* prevTopTab;

    /* needed for untabbing animation */
    CompWindow *lastTopTab;

    /* Those two are only for the change-tab animation,
       when the tab was changed again during animation.
       Another animation should be started again,
       switching for this window. */
    ChangeTabAnimationDirection nextDirection;
    GroupTabBarSlot             *nextTopTab;

    /* check focus stealing prevention after changing tabs */
    Bool checkFocusAfterTabChange;

    GroupTabBar *tabBar;

    int            changeAnimationTime;
    int            changeAnimationDirection;
    TabChangeState changeState;

    TabbingState tabbingState;

    GroupUngroupState ungroupState;

    Window       grabWindow;
    unsigned int grabMask;

    Window inputPrevention;
    Bool   ipwMapped;

    GLushort color[4];
};

typedef struct _GroupWindowHideInfo {
    Window frameWindow;

    unsigned long skipState;
    unsigned long shapeMask;

    XRectangle *inputRects;
    int        nInputRects;
    int        inputRectOrdering;
} GroupWindowHideInfo;

typedef struct _GroupResizeInfo {
    CompWindow *resizedWindow;
    XRectangle origGeometry;
} GroupResizeInfo;

/*
 * GroupDisplay structure
 */
typedef struct _GroupDisplay {
    int screenPrivateIndex;

    HandleEventProc handleEvent;

    Bool ignoreMode;

    GroupResizeInfo *resizeInfo;

    GlowTextureProperties *glowTextureProperties;

    GroupSelection *lastRestackedGroup;

    Atom groupWinPropertyAtom;
    Atom resizeNotifyAtom;

    TextFunc *textFunc;
} GroupDisplay;

/*
 * GroupScreen structure
 */

typedef struct _GroupScreen {
    int windowPrivateIndex;

    WindowMoveNotifyProc          windowMoveNotify;
    WindowResizeNotifyProc        windowResizeNotify;
    GetOutputExtentsForWindowProc getOutputExtentsForWindow;
    PreparePaintScreenProc        preparePaintScreen;
    PaintOutputProc               paintOutput;
    DrawWindowProc                drawWindow;
    PaintWindowProc               paintWindow;
    PaintTransformedOutputProc    paintTransformedOutput;
    DonePaintScreenProc           donePaintScreen;
    WindowGrabNotifyProc          windowGrabNotify;
    WindowUngrabNotifyProc        windowUngrabNotify;
    DamageWindowRectProc          damageWindowRect;
    WindowStateChangeNotifyProc   windowStateChangeNotify;
    ActivateWindowProc            activateWindow;

    GroupPendingMoves   *pendingMoves;
    GroupPendingGrabs   *pendingGrabs;
    GroupPendingUngrabs *pendingUngrabs;
    CompTimeoutHandle   dequeueTimeoutHandle;

    GroupSelection *groups;
    GroupSelection tmpSel;

    Bool queued;

    GroupScreenGrabState grabState;
    int                  grabIndex;

    GroupSelection *lastHoveredGroup;

    CompTimeoutHandle showDelayTimeoutHandle;

    /* For selection */
    Bool painted;
    int  vpX, vpY;
    int  x1, y1, x2, y2;

    /* For d&d */
    GroupTabBarSlot   *draggedSlot;
    CompTimeoutHandle dragHoverTimeoutHandle;
    Bool              dragged;
    int               prevX, prevY; /* Buffer for mouse coordinates */

    CompTimeoutHandle initialActionsTimeoutHandle;

    CompTexture glowTexture;
} GroupScreen;

/*
 * GroupWindow structure
 */
typedef struct _GroupWindow {
    GroupSelection *group;
    Bool inSelection;

    /* To prevent freeing the group
       property in groupFiniWindow. */
    Bool readOnlyProperty;

    /* For the tab bar */
    GroupTabBarSlot *slot;

    Bool needsPosSync;

    GlowQuad *glowQuads;

    GroupWindowState    windowState;
    GroupWindowHideInfo *windowHideInfo;

    XRectangle *resizeGeometry;

    /* For tab animation */
    int    animateState;
    XPoint mainTabOffset;
    XPoint destination;
    XPoint orgPos;

    float tx,ty;
    float xVelocity, yVelocity;
} GroupWindow;

/*
 * Pre-Definitions
 *
 */

/*
 * group.c
 */

Bool
groupIsGroupWindow (CompWindow *w);

void
groupUpdateWindowProperty (CompWindow *w);

Bool
groupCheckWindowProperty (CompWindow *w,
			  long int   *id,
			  Bool       *tabbed,
			  GLushort   *color);

void
groupGrabScreen (CompScreen           *s,
		 GroupScreenGrabState newState);

void
groupHandleEvent (CompDisplay *d,
		  XEvent      *event);

void
groupDeleteGroupWindow (CompWindow *w);

void
groupRemoveWindowFromGroup (CompWindow *w);

void
groupDeleteGroup (GroupSelection *group);

void
groupAddWindowToGroup (CompWindow     *w,
		       GroupSelection *group,
		       long int       initialIdent);

Bool
groupGroupWindows (CompDisplay     *d,
		   CompAction      *action,
		   CompActionState state,
		   CompOption      *option,
		   int             nOption);

Bool
groupUnGroupWindows (CompDisplay     *d,
		     CompAction      *action,
		     CompActionState state,
		     CompOption      *option,
		     int             nOption);

Bool
groupRemoveWindow (CompDisplay     *d,
		   CompAction      *action,
		   CompActionState state,
		   CompOption      *option,
		   int             nOption);

Bool
groupCloseWindows (CompDisplay     *d,
		   CompAction      *action,
		   CompActionState state,
		   CompOption      *option,
		   int             nOption);

Bool
groupChangeColor (CompDisplay     *d,
		  CompAction      *action,
		  CompActionState state,
		  CompOption      *option,
		  int             nOption);

Bool
groupSetIgnore (CompDisplay     *d,
		CompAction      *action,
		CompActionState state,
		CompOption      *option,
		int             nOption);

Bool
groupUnsetIgnore (CompDisplay     *d,
		  CompAction      *action,
		  CompActionState state,
		  CompOption      *option,
		  int             nOption);

void
groupWindowResizeNotify (CompWindow *w,
			 int        dx,
			 int        dy,
			 int        dwidth,
			 int        dheight);

void
groupWindowGrabNotify (CompWindow   *w,
		       int          x,
		       int          y,
		       unsigned int state,
		       unsigned int mask);

void
groupWindowUngrabNotify (CompWindow *w);

void
groupWindowMoveNotify (CompWindow *w,
		       int        dx,
		       int        dy,
		       Bool       immediate);

void
groupWindowStateChangeNotify (CompWindow   *w,
			      unsigned int lastState);

void
groupGetOutputExtentsForWindow (CompWindow        *w,
				CompWindowExtents *output);

Bool
groupDamageWindowRect (CompWindow *w,
		       Bool       initial,
		       BoxPtr     rect);

void
groupActivateWindow (CompWindow *w);

/*
 * cairo.c
 */

void
groupClearCairoLayer (GroupCairoLayer *layer);

void
groupDestroyCairoLayer (CompScreen      *s,
			GroupCairoLayer *layer);

GroupCairoLayer*
groupRebuildCairoLayer (CompScreen      *s,
			GroupCairoLayer *layer,
			int             width,
			int             height);

GroupCairoLayer*
groupCreateCairoLayer (CompScreen *s,
		       int        width,
		       int        height);

void
groupRenderTopTabHighlight (GroupSelection *group);

void
groupRenderTabBarBackground (GroupSelection *group);

void
groupRenderWindowTitle (GroupSelection *group);


/*
 * tab.c
 */

void
groupSetWindowVisibility (CompWindow *w,
			  Bool       visible);

void
groupClearWindowInputShape (CompWindow          *w,
			    GroupWindowHideInfo *hideInfo);

void
groupHandleAnimation (GroupSelection *group);

void
groupHandleHoverDetection (GroupSelection *group);

void
groupHandleTabBarFade (GroupSelection *group,
		       int            msSinceLastPaint);

void
groupHandleTabBarAnimation (GroupSelection *group,
			    int            msSinceLastPaint);

void
groupHandleTextFade (GroupSelection *group,
		     int            msSinceLastPaint);

void
groupDrawTabAnimation (GroupSelection *group,
		       int            msSinceLastPaint);

void
groupUpdateTabBars (CompScreen *s,
		    Window     enteredWin);

void
groupGetDrawOffsetForSlot (GroupTabBarSlot *slot,
			   int             *hoffset,
			   int             *voffset);

void
groupTabSetVisibility (GroupSelection *group,
		       Bool           visible,
		       unsigned int   mask);

Bool
groupGetCurrentMousePosition (CompScreen *s,
			      int        *x,
			      int        *y);

void
groupRecalcTabBarPos (GroupSelection *group,
		      int            middleX,
		      int            minX1,
		      int            maxX2);

void
groupInsertTabBarSlotAfter (GroupTabBar     *bar,
			    GroupTabBarSlot *slot,
			    GroupTabBarSlot *prevSlot);

void
groupInsertTabBarSlotBefore (GroupTabBar     *bar,
			     GroupTabBarSlot *slot,
			     GroupTabBarSlot *nextSlot);

void
groupInsertTabBarSlot (GroupTabBar     *bar,
		       GroupTabBarSlot *slot);

void
groupUnhookTabBarSlot (GroupTabBar     *bar,
		       GroupTabBarSlot *slot,
		       Bool            temporary);

void
groupDeleteTabBarSlot (GroupTabBar     *bar,
		       GroupTabBarSlot *slot);

void
groupCreateSlot (GroupSelection *group,
		 CompWindow     *w);

void
groupApplyForces (CompScreen      *s,
		  GroupTabBar     *bar,
		  GroupTabBarSlot *draggedSlot);

void
groupApplySpeeds (CompScreen     *s,
		  GroupSelection *group,
		  int            msSinceLastRepaint);

void
groupInitTabBar (GroupSelection *group,
		 CompWindow     *topTab);

void
groupDeleteTabBar (GroupSelection *group);

void
groupStartTabbingAnimation (GroupSelection *group,
			    Bool           tab);

void
groupTabGroup (CompWindow *main);

void
groupUntabGroup (GroupSelection *group);

Bool
groupInitTab (CompDisplay     *d,
	      CompAction      *action,
	      CompActionState state,
	      CompOption      *option,
	      int             nOption);

Bool
groupChangeTab (GroupTabBarSlot             *topTab,
		ChangeTabAnimationDirection direction);

Bool
groupChangeTabLeft (CompDisplay     *d,
		    CompAction      *action,
		    CompActionState state,
		    CompOption      *option,
		    int             nOption);

Bool
groupChangeTabRight (CompDisplay     *d,
		     CompAction      *action,
		     CompActionState state,
		     CompOption      *option,
		     int             nOption);

void
groupSwitchTopTabInput (GroupSelection *group,
			Bool           enable);

void
groupCreateInputPreventionWindow (GroupSelection *group);

void
groupDestroyInputPreventionWindow (GroupSelection *group);

Region
groupGetClippingRegion (CompWindow *w);

void
groupMoveTabBarRegion (GroupSelection *group,
		       int            dx,
		       int            dy,
		       Bool           syncIPW);

void
groupResizeTabBarRegion (GroupSelection *group,
			 XRectangle     *box,
			 Bool           syncIPW);

void
groupDamageTabBarRegion (GroupSelection *group);

/*
 * paint.c
 */

void
groupComputeGlowQuads (CompWindow *w,
		       CompMatrix *matrix);

void
groupPreparePaintScreen (CompScreen *s,
			 int        msSinceLastPaint);

Bool
groupPaintOutput (CompScreen              *s,
		  const ScreenPaintAttrib *sAttrib,
		  const CompTransform     *transform,
		  Region                  region,
		  CompOutput              *output,
		  unsigned int            mask);

void
groupPaintTransformedOutput (CompScreen              *s,
			     const ScreenPaintAttrib *sa,
			     const CompTransform     *transform,
			     Region                  region,
			     CompOutput              *output,
			     unsigned int            mask);

void
groupDonePaintScreen (CompScreen *s);

Bool
groupDrawWindow (CompWindow           *w,
		 const CompTransform  *transform,
		 const FragmentAttrib *attrib,
		 Region               region,
		 unsigned int         mask);

void
groupGetStretchRectangle (CompWindow *w,
			  BoxPtr     pBox,
			  float      *xScale,
			  float      *yScale);

void
groupDamagePaintRectangle (CompScreen *s,
			   BoxPtr     pBox);

Bool
groupPaintWindow (CompWindow              *w,
		  const WindowPaintAttrib *attrib,
		  const CompTransform     *transform,
		  Region                  region,
		  unsigned int            mask);


/*
 * queues.c
 */

void
groupEnqueueMoveNotify (CompWindow *w,
			int        dx,
			int        dy,
			Bool       immediate,
			Bool       sync);

void
groupDequeueMoveNotifies (CompScreen *s);

void
groupEnqueueGrabNotify (CompWindow   *w,
			int          x,
			int          y,
			unsigned int state,
			unsigned int mask);

void
groupEnqueueUngrabNotify (CompWindow *w);

/*
 * selection.c
 */

Bool
groupSelectSingle (CompDisplay     *d,
		   CompAction      *action,
		   CompActionState state,
		   CompOption      *option,
		   int             nOption);

Bool groupSelect (CompDisplay     *d,
		  CompAction      *action,
		  CompActionState state,
		  CompOption      *option,
		  int             nOption);

Bool
groupSelectTerminate (CompDisplay     *d,
		      CompAction      *action,
		      CompActionState state,
		      CompOption      *option,
		      int             nOption);

void
groupDamageSelectionRect (CompScreen *s,
			  int        xRoot,
			  int        yRoot);

#endif
