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
 */

#ifndef _FUSILLI_SCALE_H
#define _FUSILLI_SCALE_H

#include <fusilli-core.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define SCALE_ABIVERSION 20081007

#define SCALE_STATE_NONE 0
#define SCALE_STATE_OUT  1
#define SCALE_STATE_WAIT 2
#define SCALE_STATE_IN   3

#define SCALE_MOMODE_CURRENT 0
#define SCALE_MOMODE_ALL     1
#define SCALE_MOMODE_LAST    SCALE_MOMODE_ALL

typedef struct _ScaleSlot {
	int   x1, y1, x2, y2;
	int   filled;
	float scale;
} ScaleSlot;

typedef struct _SlotArea {
	int        nWindows;
	XRectangle workArea;
} SlotArea;

typedef struct _ScaleDisplay {
	int             screenPrivateIndex;
	HandleEventProc handleEvent;

	unsigned int lastActiveNum;
	Window       lastActiveWindow;

	Window       selectedWindow;
	Window       hoveredWindow;
	Window       previousActiveWindow;

	KeyCode	 leftKeyCode, rightKeyCode, upKeyCode, downKeyCode;
} ScaleDisplay;

typedef enum {
	ScaleTypeNormal = 0,
	ScaleTypeOutput,
	ScaleTypeGroup,
	ScaleTypeAll
} ScaleType;

typedef Bool (*ScaleLayoutSlotsAndAssignWindowsProc) (CompScreen *s);

typedef Bool (*ScaleSetScaledPaintAttributesProc) (CompWindow        *w,
                                                   WindowPaintAttrib *attrib);

typedef void (*ScalePaintDecorationProc) (CompWindow              *w,
                                          const WindowPaintAttrib *attrib,
                                          const CompTransform     *transform,
                                          Region                  region,
                                          unsigned int            mask);

typedef void (*ScaleSelectWindowProc) (CompWindow *w);

typedef struct _ScaleScreen {
	int windowPrivateIndex;

	PreparePaintScreenProc preparePaintScreen;
	DonePaintScreenProc    donePaintScreen;
	PaintOutputProc        paintOutput;
	PaintWindowProc        paintWindow;
	DamageWindowRectProc   damageWindowRect;

	ScaleLayoutSlotsAndAssignWindowsProc layoutSlotsAndAssignWindows;
	ScaleSetScaledPaintAttributesProc    setScaledPaintAttributes;
	ScalePaintDecorationProc             scalePaintDecoration;
	ScaleSelectWindowProc                selectWindow;

	Bool grab;
	int  grabIndex;

	CompTimeoutHandle hoverHandle;

	int state;
	int moreAdjust;

	Cursor cursor;

	ScaleSlot *slots;
	int        slotsSize;
	int        nSlots;

	/* only used for sorting */
	CompWindow **windows;
	int        windowsSize;
	int        nWindows;

	GLushort opacity;

	ScaleType type;

	Window clientLeader;

	CompMatch window_match;
} ScaleScreen;

typedef struct _ScaleWindow {
	ScaleSlot *slot;

	int sid;
	int distance;

	GLfloat xVelocity, yVelocity, scaleVelocity;
	GLfloat scale;
	GLfloat tx, ty;
	float   delta;
	Bool    adjust;

	float lastThumbOpacity;
} ScaleWindow;

#define GET_SCALE_DISPLAY(d) \
        ((ScaleDisplay *) (d)->base.privates[scaleDisplayPrivateIndex].ptr)

#define SCALE_DISPLAY(d) \
        ScaleDisplay *sd = GET_SCALE_DISPLAY (d)

#define GET_SCALE_SCREEN(s, sd) \
        ((ScaleScreen *) (s)->base.privates[(sd)->screenPrivateIndex].ptr)

#define SCALE_SCREEN(s) \
        ScaleScreen *ss = GET_SCALE_SCREEN (s, GET_SCALE_DISPLAY (&display))

#define GET_SCALE_WINDOW(w, ss) \
        ((ScaleWindow *) (w)->base.privates[(ss)->windowPrivateIndex].ptr)

#define SCALE_WINDOW(w) \
        ScaleWindow *sw = GET_SCALE_WINDOW  (w, \
                          GET_SCALE_SCREEN  (w->screen, \
                          GET_SCALE_DISPLAY (&display)))

#ifdef  __cplusplus
}
#endif

#endif
