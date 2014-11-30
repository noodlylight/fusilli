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

#ifndef _FUSILLI_CORE_H
#define _FUSILLI_CORE_H

#include <fusilli-plugin.h>

#define CORE_ABIVERSION 20141130

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sys/time.h>

#include <X11/Xutil.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/sync.h>
#include <X11/Xregion.h>
#include <X11/XKBlib.h>

#define SN_API_NOT_YET_FROZEN
#include <libsn/sn.h>

#include <GL/gl.h>
#include <GL/glx.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus.h>

#ifdef  __cplusplus
extern "C" {
#endif

#if COMPOSITE_MAJOR > 0 || COMPOSITE_MINOR > 2
#define USE_COW
#endif

/*
 * WORDS_BIGENDIAN should be defined before including this file for
 * IMAGE_BYTE_ORDER and BITMAP_BIT_ORDER to be set correctly.
 */
#define LSBFirst 0
#define MSBFirst 1

#ifdef WORDS_BIGENDIAN
#  define IMAGE_BYTE_ORDER MSBFirst
#  define BITMAP_BIT_ORDER MSBFirst
#else
#  define IMAGE_BYTE_ORDER LSBFirst
#  define BITMAP_BIT_ORDER LSBFirst
#endif

typedef struct _CompTexture       CompTexture;
typedef struct _CompIcon          CompIcon;
typedef struct _CompWindowExtents CompWindowExtents;
typedef struct _CompWindowExtents CompFullscreenMonitorSet;
typedef struct _CompProgram       CompProgram;
typedef struct _CompFunction      CompFunction;
typedef struct _CompFunctionData  CompFunctionData;
typedef struct _FragmentAttrib    FragmentAttrib;
typedef struct _CompCursor        CompCursor;
typedef struct _CompMatch         CompMatch;
typedef struct _CompOutput        CompOutput;
typedef struct _CompWalker        CompWalker;

#define REAL_MOD_MASK (ShiftMask | ControlMask | Mod1Mask | Mod2Mask | \
Mod3Mask | Mod4Mask | Mod5Mask | CompNoMask)

/* virtual modifiers */

#define CompModAlt        0
#define CompModMeta       1
#define CompModSuper      2
#define CompModHyper      3
#define CompModModeSwitch 4
#define CompModNumLock    5
#define CompModScrollLock 6
#define CompModNum        7

#define CompAltMask        (1 << 16)
#define CompMetaMask       (1 << 17)
#define CompSuperMask      (1 << 18)
#define CompHyperMask      (1 << 19)
#define CompModeSwitchMask (1 << 20)
#define CompNumLockMask    (1 << 21)
#define CompScrollLockMask (1 << 22)

#define CompNoMask         (1 << 25)

#define CompWindowProtocolDeleteMask      (1 << 0)
#define CompWindowProtocolTakeFocusMask	  (1 << 1)
#define CompWindowProtocolPingMask        (1 << 2)
#define CompWindowProtocolSyncRequestMask (1 << 3)

#define CompWindowTypeDesktopMask      (1 << 0)
#define CompWindowTypeDockMask         (1 << 1)
#define CompWindowTypeToolbarMask      (1 << 2)
#define CompWindowTypeMenuMask         (1 << 3)
#define CompWindowTypeUtilMask         (1 << 4)
#define CompWindowTypeSplashMask       (1 << 5)
#define CompWindowTypeDialogMask       (1 << 6)
#define CompWindowTypeNormalMask       (1 << 7)
#define CompWindowTypeDropdownMenuMask (1 << 8)
#define CompWindowTypePopupMenuMask    (1 << 9)
#define CompWindowTypeTooltipMask      (1 << 10)
#define CompWindowTypeNotificationMask (1 << 11)
#define CompWindowTypeComboMask	       (1 << 12)
#define CompWindowTypeDndMask          (1 << 13)
#define CompWindowTypeModalDialogMask  (1 << 14)
#define CompWindowTypeFullscreenMask   (1 << 15)
#define CompWindowTypeUnknownMask      (1 << 16)

#define NO_FOCUS_MASK (CompWindowTypeDesktopMask | \
                       CompWindowTypeDockMask    | \
                       CompWindowTypeSplashMask)

#define CompWindowStateModalMask            (1 <<  0)
#define CompWindowStateStickyMask           (1 <<  1)
#define CompWindowStateMaximizedVertMask    (1 <<  2)
#define CompWindowStateMaximizedHorzMask    (1 <<  3)
#define CompWindowStateShadedMask           (1 <<  4)
#define CompWindowStateSkipTaskbarMask      (1 <<  5)
#define CompWindowStateSkipPagerMask        (1 <<  6)
#define CompWindowStateHiddenMask           (1 <<  7)
#define CompWindowStateFullscreenMask       (1 <<  8)
#define CompWindowStateAboveMask            (1 <<  9)
#define CompWindowStateBelowMask            (1 << 10)
#define CompWindowStateDemandsAttentionMask (1 << 11)
#define CompWindowStateDisplayModalMask     (1 << 12)

#define MAXIMIZE_STATE (CompWindowStateMaximizedHorzMask | \
                        CompWindowStateMaximizedVertMask)

#define CompWindowActionMoveMask          (1 << 0)
#define CompWindowActionResizeMask        (1 << 1)
#define CompWindowActionStickMask         (1 << 2)
#define CompWindowActionMinimizeMask      (1 << 3)
#define CompWindowActionMaximizeHorzMask  (1 << 4)
#define CompWindowActionMaximizeVertMask  (1 << 5)
#define CompWindowActionFullscreenMask    (1 << 6)
#define CompWindowActionCloseMask         (1 << 7)
#define CompWindowActionShadeMask         (1 << 8)
#define CompWindowActionChangeDesktopMask (1 << 9)
#define CompWindowActionAboveMask         (1 << 10)
#define CompWindowActionBelowMask         (1 << 11)

#define MwmFuncAll      (1L << 0)
#define MwmFuncResize   (1L << 1)
#define MwmFuncMove     (1L << 2)
#define MwmFuncIconify  (1L << 3)
#define MwmFuncMaximize (1L << 4)
#define MwmFuncClose    (1L << 5)

#define MwmDecorAll      (1L << 0)
#define MwmDecorBorder   (1L << 1)
#define MwmDecorHandle   (1L << 2)
#define MwmDecorTitle    (1L << 3)
#define MwmDecorMenu     (1L << 4)
#define MwmDecorMinimize (1L << 5)
#define MwmDecorMaximize (1L << 6)

#define WmMoveResizeSizeTopLeft      0
#define WmMoveResizeSizeTop          1
#define WmMoveResizeSizeTopRight     2
#define WmMoveResizeSizeRight        3
#define WmMoveResizeSizeBottomRight  4
#define WmMoveResizeSizeBottom       5
#define WmMoveResizeSizeBottomLeft   6
#define WmMoveResizeSizeLeft         7
#define WmMoveResizeMove             8
#define WmMoveResizeSizeKeyboard     9
#define WmMoveResizeMoveKeyboard    10
#define WmMoveResizeCancel          11

/* EWMH source indication client types */
#define ClientTypeUnknown      0
#define ClientTypeApplication  1
#define ClientTypePager        2

#define OPAQUE 0xffff
#define COLOR  0xffff
#define BRIGHT 0xffff

#define RED_SATURATION_WEIGHT   0.30f
#define GREEN_SATURATION_WEIGHT 0.59f
#define BLUE_SATURATION_WEIGHT  0.11f

extern char       *programName;
extern char       **programArgv;
extern int        programArgc;
extern char       *backgroundImage;
extern REGION     emptyRegion;
extern REGION     infiniteRegion;
extern GLushort   defaultColor[4];
extern Window     currentRoot;
extern Bool       shutDown;
extern Bool       restartSignal;
extern CompWindow *lastFoundWindow;
extern CompWindow *lastDamagedWindow;
extern Bool       replaceCurrentWm;
extern Bool       indirectRendering;
extern Bool       strictBinding;
extern Bool       useCow;
extern Bool       useDesktopHints;
extern Bool       onlyCurrentScreen;

extern int lastPointerX;
extern int lastPointerY;
extern int pointerX;
extern int pointerY;

extern CompCore     core;
extern CompDisplay  display;
extern int coreBananaIndex;

#define RESTRICT_VALUE(value, min, max) \
        (((value) < (min)) ? (min): ((value) > (max)) ? (max) : (value))

#define MOD(a,b) ((a) < 0 ? ((b) - ((-(a) - 1) % (b))) - 1 : (a) % (b))

/* banana.c */
typedef enum _BananaType {

	BananaBool,
	BananaInt,
	BananaFloat,
	BananaString,

	BananaListBool,
	BananaListInt,
	BananaListFloat,
	BananaListString

} BananaType;

typedef struct _BananaList   BananaList;
typedef union  _BananaValue  BananaValue;

struct _BananaList {
	BananaValue       *item;
	int               nItem;

	//private banana business
	size_t            bytes; //size of memory chunk pointed by item
};

union _BananaValue {
	Bool       b;
	int        i;
	float      f;
	char       *s;

	BananaList list;
};
//----------------------------------------------------------------------------//
void
initBananaValue (BananaValue *v,
                 BananaType  type);

void
finiBananaValue (BananaValue *v,
                 BananaType  type);

void 
copyBananaValue (BananaValue       *dest,
                 const BananaValue *src,
                 BananaType        type);

Bool
isEqualBananaValue (const BananaValue *a,
                    const BananaValue *b,
                    BananaType        type);

void
addItemToBananaList (const char     *s,
                     BananaType     type,
                     BananaValue    *l);

void
stringToBananaValue (const char        *s,
                     BananaType        type,
                     BananaValue       *value);
//----------------------------------------------------------------------------//
typedef void (*BananaChangeNotifyCallBack) (const char        *optionName,
                                            BananaType        optionType,
                                            const BananaValue *optionValue,
                                            int               screenNum);
//----------------------------------------------------------------------------//
typedef struct _BananaArgument {
	char        *name;
	BananaType  type;
	BananaValue value;
} BananaArgument;

BananaValue *
getArgNamed (const char         *name,
             BananaArgument     *arg,
             int                nArg);
//----------------------------------------------------------------------------//
void
bananaInit (const char *metaDataDir,
            const char *configurationFile);

void
bananaFini (void);
//----------------------------------------------------------------------------//
int
bananaLoadPlugin (const char *pluginName);

int
bananaGetPluginIndex (const char *pluginName);

void
bananaAddChangeNotifyCallBack (int                        bananaIndex,
                               BananaChangeNotifyCallBack callback);

void
bananaRemoveChangeNotifyCallBack (int                        bananaIndex,
                                  BananaChangeNotifyCallBack callback);

void
bananaUnloadPlugin (int bananaIndex);
//----------------------------------------------------------------------------//
const BananaValue *
bananaGetOption (int        bananaIndex,
                 const char *optionName,
                 int        screenNum);

void
bananaSetOption (int         bananaIndex,
                 const char  *optionName,
                 int         screenNum,
                 BananaValue *value);

//----------------------------------------------------------------------------//

/* privates.c */

#define WRAP(priv, real, func, wrapFunc) \
        (priv)->func = (real)->func; \
        (real)->func = (wrapFunc)

#define UNWRAP(priv, real, func) \
        (real)->func = (priv)->func

typedef union _CompPrivate {
	void          *ptr;
	long          val;
	unsigned long uval;
	void          *(*fptr) (void);
} CompPrivate;

typedef int (*ReallocPrivatesProc) (int size, void *closure);

int
allocatePrivateIndex (int                 *len,
                      char                **indices,
                      ReallocPrivatesProc reallocProc,
                      void                *closure);

void
freePrivateIndex (int  len,
                  char *indices,
                  int  index);

/* session.c */

typedef enum {
	CompSessionEventSaveYourself = 0,
	CompSessionEventSaveComplete,
	CompSessionEventDie,
	CompSessionEventShutdownCancelled
} CompSessionEvent;

typedef enum {
	CompSessionClientId = 0,
	CompSessionPrevClientId
} CompSessionClientIdType;

typedef void (*SessionEventProc) (CompCore         *c,
                                  CompSessionEvent event,
                                  BananaArgument   *arg,
                                  unsigned int     nArg);

void
initSession (char *smPrevClientId);

void
closeSession (void);

void
sessionEvent (CompCore         *c,
              CompSessionEvent event,
              BananaArgument   *arg,
              unsigned int     nArg);

char *
getSessionClientId (CompSessionClientIdType type);

/* option.c */

typedef enum {
	CompBindingTypeNone       = 0,
	CompBindingTypeKey        = 1 << 0,
	CompBindingTypeButton     = 1 << 1,
	CompBindingTypeEdgeButton = 1 << 2
} CompBindingType;

typedef enum {
	CompLogLevelFatal = 0,
	CompLogLevelError,
	CompLogLevelWarn,
	CompLogLevelInfo,
	CompLogLevelDebug
} CompLogLevel;

typedef struct _CompKeyBinding {
	int          keycode;
	unsigned int modifiers;
	Bool         active;
} CompKeyBinding;

typedef struct _CompButtonBinding {
	int          button;
	unsigned int modifiers;
	Bool         active;
	Bool         clickOnDesktop;
} CompButtonBinding;

typedef union _CompMatchOp CompMatchOp;

struct _CompMatch {
	Bool        active;
	CompMatchOp *op;
	int         nOp;
};

char *
keyBindingToString (CompKeyBinding *key);

char *
buttonBindingToString (CompButtonBinding *button);

Bool
stringToKeyBinding (const char     *binding,
                    CompKeyBinding *key);

Bool
stringToButtonBinding (const char        *binding,
                       CompButtonBinding *button);

unsigned int
stringToModifiers (const char  *binding);


const char *
edgeToString (unsigned int edge);

unsigned int
stringToEdgeMask (const char *edge);

char *
edgeMaskToString (unsigned int edgeMask);

Bool
stringToColor (const char     *color,
               unsigned short *rgba);

char *
colorToString (unsigned short *rgba);


/* core.c */

#define NOTIFY_CREATE_MASK (1 << 0)
#define NOTIFY_DELETE_MASK (1 << 1)
#define NOTIFY_MOVE_MASK   (1 << 2)
#define NOTIFY_MODIFY_MASK (1 << 3)

typedef void (*FileWatchCallBackProc) (const char *name,
                                       void       *closure);

typedef int CompFileWatchHandle;

typedef struct _CompFileWatch {
	struct _CompFileWatch *next;
	char                  *path;
	int                   mask;
	FileWatchCallBackProc callBack;
	void                  *closure;
	CompFileWatchHandle   handle;
} CompFileWatch;

typedef struct _CompTimeout {
	struct _CompTimeout *next;
	int                 minTime;
	int                 maxTime;
	int                 minLeft;
	int                 maxLeft;
	CallBackProc        callBack;
	void                *closure;
	CompTimeoutHandle   handle;
} CompTimeout;

typedef struct _CompWatchFd {
	struct _CompWatchFd *next;
	int                 fd;
	CallBackProc        callBack;
	void                *closure;
	CompWatchFdHandle   handle;
} CompWatchFd;

typedef void (*LogMessageProc) (const char   *componentName,
                                CompLogLevel level,
                                const char   *message);

struct _CompCore {
	CompPrivate    *privates;

	Region tmpRegion;
	Region outputRegion;

	CompFileWatch       *fileWatch;
	CompFileWatchHandle lastFileWatchHandle;

	CompTimeout       *timeouts;
	struct timeval    lastTimeout;
	CompTimeoutHandle lastTimeoutHandle;

	CompWatchFd       *watchFds;
	CompWatchFdHandle lastWatchFdHandle;
	struct pollfd     *watchPollFds;
	int               nWatchFds;

	SessionEventProc sessionEvent;
	LogMessageProc   logMessage;

	DBusConnection    *dbusConnection;
	CompWatchFdHandle dbusWatchFdHandle;
};

CompBool
initCore (void);

void
finiCore (void);

int
allocateCorePrivateIndex (void);

void
freeCorePrivateIndex (int index);

CompFileWatchHandle
addFileWatch (const char            *path,
              int                   mask,
              FileWatchCallBackProc callBack,
              void                  *closure);

void
removeFileWatch (CompFileWatchHandle handle);

int
getCoreABI (void);


/* display.c */

typedef void (*HandleEventProc) (XEvent      *event);

typedef void (*HandleFusilliEventProc) (const char     *pluginName,
                                        const char     *eventName,
                                        BananaArgument *arg,
                                        int            nArg);

typedef void (*ForEachWindowProc) (CompWindow *window,
                                   void       *closure);

typedef Bool (*FileToImageProc) (const char  *path,
                                 const char  *name,
                                 int         *width,
                                 int         *height,
                                 int         *stride,
                                 void        **data);

typedef Bool (*ImageToFileProc) (const char  *path,
                                 const char  *name,
                                 const char  *format,
                                 int         width,
                                 int         height,
                                 int         stride,
                                 void        *data);

#define MATCH_OP_AND_MASK (1 << 0)
#define MATCH_OP_NOT_MASK (1 << 1)

typedef enum {
	CompMatchOpTypeGroup,
	CompMatchOpTypeExp
} CompMatchOpType;

typedef struct _CompMatchAnyOp {
	CompMatchOpType type;
	int             flags;
} CompMatchAnyOp;

typedef struct _CompMatchGroupOp {
	CompMatchOpType type;
	int             flags;
	CompMatchOp     *op;
	int             nOp;
} CompMatchGroupOp;

typedef void (*CompMatchExpFiniProc) (CompPrivate priv);

typedef Bool (*CompMatchExpEvalProc) (CompWindow  *window,
                                      CompPrivate priv);

typedef struct _CompMatchExp {
	CompMatchExpFiniProc fini;
	CompMatchExpEvalProc eval;
	CompPrivate          priv;
} CompMatchExp;

typedef struct _CompMatchExpOp {
	CompMatchOpType type;
	int             flags;
	char            *value;
	CompMatchExp    e;
} CompMatchExpOp;

union _CompMatchOp {
	CompMatchOpType  type;
	CompMatchAnyOp   any;
	CompMatchGroupOp group;
	CompMatchExpOp   exp;
};

typedef void (*MatchPropertyChangedProc) (CompWindow  *window);

struct _CompDisplay {
	CompPrivate    *privates;

	Display    *display;
	CompScreen *screens;

	CompWatchFdHandle watchFdHandle;

	char *screenPrivateIndices;
	int  screenPrivateLen;

	int compositeEvent, compositeError, compositeOpcode;
	int damageEvent, damageError;
	int syncEvent, syncError;
	int fixesEvent, fixesError, fixesVersion;

	Bool randrExtension;
	int randrEvent, randrError;

	Bool shapeExtension;
	int  shapeEvent, shapeError;

	Bool xkbExtension;
	int  xkbEvent, xkbError;

	Bool xineramaExtension;
	int  xineramaEvent, xineramaError;

	XineramaScreenInfo *screenInfo;
	int                nScreenInfo;

	SnDisplay *snDisplay;

	Atom supportedAtom;
	Atom supportingWmCheckAtom;

	Atom utf8StringAtom;

	Atom wmNameAtom;

	Atom winTypeAtom;
	Atom winTypeDesktopAtom;
	Atom winTypeDockAtom;
	Atom winTypeToolbarAtom;
	Atom winTypeMenuAtom;
	Atom winTypeUtilAtom;
	Atom winTypeSplashAtom;
	Atom winTypeDialogAtom;
	Atom winTypeNormalAtom;
	Atom winTypeDropdownMenuAtom;
	Atom winTypePopupMenuAtom;
	Atom winTypeTooltipAtom;
	Atom winTypeNotificationAtom;
	Atom winTypeComboAtom;
	Atom winTypeDndAtom;

	Atom winOpacityAtom;
	Atom winBrightnessAtom;
	Atom winSaturationAtom;
	Atom winActiveAtom;
	Atom winDesktopAtom;

	Atom workareaAtom;

	Atom desktopViewportAtom;
	Atom desktopGeometryAtom;
	Atom currentDesktopAtom;
	Atom numberOfDesktopsAtom;

	Atom roleAtom;
	Atom visibleNameAtom;

	Atom winStateAtom;
	Atom winStateModalAtom;
	Atom winStateStickyAtom;
	Atom winStateMaximizedVertAtom;
	Atom winStateMaximizedHorzAtom;
	Atom winStateShadedAtom;
	Atom winStateSkipTaskbarAtom;
	Atom winStateSkipPagerAtom;
	Atom winStateHiddenAtom;
	Atom winStateFullscreenAtom;
	Atom winStateAboveAtom;
	Atom winStateBelowAtom;
	Atom winStateDemandsAttentionAtom;
	Atom winStateDisplayModalAtom;

	Atom winActionMoveAtom;
	Atom winActionResizeAtom;
	Atom winActionStickAtom;
	Atom winActionMinimizeAtom;
	Atom winActionMaximizeHorzAtom;
	Atom winActionMaximizeVertAtom;
	Atom winActionFullscreenAtom;
	Atom winActionCloseAtom;
	Atom winActionShadeAtom;
	Atom winActionChangeDesktopAtom;
	Atom winActionAboveAtom;
	Atom winActionBelowAtom;

	Atom wmAllowedActionsAtom;

	Atom wmStrutAtom;
	Atom wmStrutPartialAtom;

	Atom wmUserTimeAtom;

	Atom wmIconAtom;
	Atom wmIconGeometryAtom;

	Atom clientListAtom;
	Atom clientListStackingAtom;

	Atom frameExtentsAtom;
	Atom frameWindowAtom;

	Atom wmStateAtom;
	Atom wmChangeStateAtom;
	Atom wmProtocolsAtom;
	Atom wmClientLeaderAtom;

	Atom wmDeleteWindowAtom;
	Atom wmTakeFocusAtom;
	Atom wmPingAtom;

	Atom wmSyncRequestAtom;
	Atom wmSyncRequestCounterAtom;

	Atom wmFullscreenMonitorsAtom;

	Atom closeWindowAtom;
	Atom wmMoveResizeAtom;
	Atom moveResizeWindowAtom;
	Atom restackWindowAtom;

	Atom showingDesktopAtom;

	Atom xBackgroundAtom[2];

	Atom toolkitActionAtom;
	Atom toolkitActionWindowMenuAtom;
	Atom toolkitActionForceQuitDialogAtom;

	Atom mwmHintsAtom;

	Atom xdndAwareAtom;
	Atom xdndEnterAtom;
	Atom xdndLeaveAtom;
	Atom xdndPositionAtom;
	Atom xdndStatusAtom;
	Atom xdndDropAtom;

	Atom managerAtom;
	Atom targetsAtom;
	Atom multipleAtom;
	Atom timestampAtom;
	Atom versionAtom;
	Atom atomPairAtom;

	Atom startupIdAtom;

	unsigned int      lastPing;
	CompTimeoutHandle pingHandle;

	GLenum textureFilter;

	Window activeWindow;
	Window nextActiveWindow;

	Window below;
	char   displayString[256];

	XModifierKeymap *modMap;
	unsigned int    modMask[CompModNum];
	unsigned int    ignoredModMask;

	KeyCode escapeKeyCode;
	KeyCode returnKeyCode;

	CompTimeoutHandle autoRaiseHandle;
	Window            autoRaiseWindow;

	CompTimeoutHandle edgeDelayHandle;

	BananaValue     plugin;
	Bool            dirtyPluginList;

	HandleEventProc       handleEvent;
	HandleFusilliEventProc handleFusilliEvent;

	FileToImageProc fileToImage;
	ImageToFileProc imageToFile;

	MatchPropertyChangedProc   matchPropertyChanged;

	LogMessageProc logMessage;

	CompKeyBinding close_window_key,
	               raise_window_key,
	               lower_window_key,
	               unmaximize_window_key,
	               minimize_window_key,
	               maximize_window_key,
	               maximize_window_horizontally_key,
	               maximize_window_vertically_key,
	               window_menu_key,
	               show_desktop_key,
	               toggle_window_maximized_key,
	               toggle_window_maximized_horizontally_key,
	               toggle_window_maximized_vertically_key,
	               toggle_window_shaded_key,
	               slow_animations_key;

	CompButtonBinding close_window_button,
	                  raise_window_button,
	                  lower_window_button,
	                  minimize_window_button,
	                  window_menu_button,
	                  toggle_window_maximized_button;

	void *reserved;
};

int
allocateDisplayPrivateIndex (void);

void
freeDisplayPrivateIndex (int index);

void
displayChangeNotify (const char        *optionName,
                     BananaType        optionType,
                     const BananaValue *optionValue,
                     int               screenNum);

void
compLogMessage (const char   *componentName,
                CompLogLevel level,
                const char   *format,
                ...);

void
logMessage (const char   *componentName,
            CompLogLevel level,
            const char   *message);

const char *
logLevelToString (CompLogLevel level);

int
compCheckForError (Display *dpy);

void
addScreenToDisplay (CompScreen *s);

Bool
addDisplay (const char *name);

void
removeDisplay (void);

Time
getCurrentTimeFromDisplay (void);

void
forEachWindowOnDisplay (ForEachWindowProc proc,
                        void              *closure);

CompScreen *
findScreenAtDisplay (Window      root);

CompScreen *
getScreenFromScreenNum (int screenNum);

CompWindow *
findWindowAtDisplay (Window      id);

CompWindow *
findTopLevelWindowAtDisplay (Window      id);

unsigned int
virtualToRealModMask (unsigned int modMask);

void
updateModifierMappings (void);

unsigned int
keycodeToModifiers (int         keycode);

void
eventLoop (void);

void
handleSelectionRequest (XEvent      *event);

void
handleSelectionClear (XEvent      *event);

void
warpPointer (CompScreen *screen,
             int        dx,
             int        dy);

Bool
addDisplayKeyBinding (CompKeyBinding *key);

Bool
addDisplayButtonBinding (CompButtonBinding *button);

void
removeDisplayKeyBinding (CompKeyBinding *key);

void
removeDisplayButtonBinding (CompButtonBinding *button);

void
registerButton (const char        *s,
                CompButtonBinding *button);

void
registerKey (const char        *s,
             CompKeyBinding    *key);

void
updateButton (const char        *s,
              CompButtonBinding *button);

void
updateKey (const char        *s,
           CompKeyBinding    *key);

Bool
isKeyPressEvent (XEvent         *event,
                 CompKeyBinding *key);

Bool
isButtonPressEvent (XEvent            *event,
                    CompButtonBinding *button);

Bool
readImageFromFile (const char  *name,
                   int         *width,
                   int         *height,
                   void        **data);

Bool
writeImageToFile (const char  *path,
                  const char  *name,
                  const char  *format,
                  int         width,
                  int         height,
                  void        *data);

Bool
fileToImage (const char  *path,
             const char  *name,
             int         *width,
             int         *height,
             int         *stride,
             void        **data);

Bool
imageToFile (const char  *path,
             const char  *name,
             const char  *format,
             int         width,
             int         height,
             int         stride,
             void        *data);

CompCursor *
findCursorAtDisplay (void);


/* event.c */

void
handleEvent (XEvent      *event);

void
handleFusilliEvent (const char      *pluginName,
                    const char      *eventName,
                    BananaArgument  *arg,
                    int             nArg);

void
handleSyncAlarm (CompWindow *w);

void
clearTargetOutput (unsigned int mask);

/* paint.c */

#define MULTIPLY_USHORT(us1, us2) \
        (((GLuint) (us1) * (GLuint) (us2)) / 0xffff)

/* camera distance from screen, 0.5 * tan (FOV) */
#define DEFAULT_Z_CAMERA 0.866025404f

#define DEG2RAD (M_PI / 180.0f)

typedef struct _CompTransform {
	float m[16];
} CompTransform;

typedef union _CompVector {
	float v[4];
	struct {
		float x;
		float y;
		float z;
		float w;
	};
} CompVector;

/* XXX: ScreenPaintAttrib will be removed */
typedef struct _ScreenPaintAttrib {
	GLfloat xRotate;
	GLfloat yRotate;
	GLfloat vRotate;
	GLfloat xTranslate;
	GLfloat yTranslate;
	GLfloat zTranslate;
	GLfloat zCamera;
} ScreenPaintAttrib;

/* XXX: scale and translate fields will be removed */
typedef struct _WindowPaintAttrib {
	GLushort opacity;
	GLushort brightness;
	GLushort saturation;
	GLfloat  xScale;
	GLfloat  yScale;
	GLfloat  xTranslate;
	GLfloat  yTranslate;
} WindowPaintAttrib;

extern ScreenPaintAttrib defaultScreenPaintAttrib;
extern WindowPaintAttrib defaultWindowPaintAttrib;

typedef struct _CompMatrix {
	float xx; float yx;
	float xy; float yy;
	float x0; float y0;
} CompMatrix;

#define COMP_TEX_COORD_X(m, vx) ((m)->xx * (vx) + (m)->x0)
#define COMP_TEX_COORD_Y(m, vy) ((m)->yy * (vy) + (m)->y0)

#define COMP_TEX_COORD_XY(m, vx, vy) \
        ((m)->xx * (vx) + (m)->xy * (vy) + (m)->x0)
#define COMP_TEX_COORD_YX(m, vx, vy) \
        ((m)->yx * (vx) + (m)->yy * (vy) + (m)->y0)


typedef void (*PreparePaintScreenProc) (CompScreen *screen,
                                        int        msSinceLastPaint);

typedef void (*DonePaintScreenProc) (CompScreen *screen);

#define PAINT_SCREEN_REGION_MASK                   (1 << 0)
#define PAINT_SCREEN_FULL_MASK                     (1 << 1)
#define PAINT_SCREEN_TRANSFORMED_MASK              (1 << 2)
#define PAINT_SCREEN_WITH_TRANSFORMED_WINDOWS_MASK (1 << 3)
#define PAINT_SCREEN_CLEAR_MASK                    (1 << 4)
#define PAINT_SCREEN_NO_OCCLUSION_DETECTION_MASK   (1 << 5)
#define PAINT_SCREEN_NO_BACKGROUND_MASK            (1 << 6)

typedef void (*PaintScreenProc) (CompScreen   *screen,
                                 CompOutput   *outputs,
                                 int          numOutput,
                                 unsigned int mask);

typedef Bool (*PaintOutputProc) (CompScreen              *screen,
                                 const ScreenPaintAttrib *sAttrib,
                                 const CompTransform     *transform,
                                 Region                  region,
                                 CompOutput              *output,
                                 unsigned int            mask);

typedef void (*PaintTransformedOutputProc) (CompScreen              *screen,
                                            const ScreenPaintAttrib *sAttrib,
                                            const CompTransform     *transform,
                                            Region                  region,
                                            CompOutput              *output,
                                            unsigned int            mask);

/* XXX: ApplyScreenTransformProc will be removed */
typedef void (*ApplyScreenTransformProc) (CompScreen              *screen,
                                          const ScreenPaintAttrib *sAttrib,
                                          CompOutput              *output,
                                          CompTransform           *transform);

typedef void (*EnableOutputClippingProc) (CompScreen          *screen,
                                          const CompTransform *transform,
                                          Region              region,
                                          CompOutput          *output);

typedef void (*DisableOutputClippingProc) (CompScreen *screen);

typedef void (*WalkerFiniProc) (CompScreen *screen,
                                CompWalker *walker);

typedef CompWindow *(*WalkInitProc) (CompScreen *screen);
typedef CompWindow *(*WalkStepProc) (CompWindow *window);

struct _CompWalker {
	WalkerFiniProc fini;
	CompPrivate    priv;

	WalkInitProc first;
	WalkInitProc last;
	WalkStepProc next;
	WalkStepProc prev;
};

/*
  window paint flags

  bit 1-16 are used for read-only flags and they provide
  information that describe the screen rendering pass
  currently in process.

  bit 17-32 are writable flags and they provide information
  that is used to optimize rendering.
*/

/*
  this flag is present when window is being painted
  on a transformed screen.
*/
#define PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK (1 << 0)

/*
  this flag is present when window is being tested
  for occlusion of other windows.
*/
#define PAINT_WINDOW_OCCLUSION_DETECTION_MASK   (1 << 1)

/*
  this flag indicates that the window ist painted with
  an offset
*/
#define PAINT_WINDOW_WITH_OFFSET_MASK           (1 << 2)

/*
  flag indicate that window is translucent.
*/
#define PAINT_WINDOW_TRANSLUCENT_MASK           (1 << 16)

/*
  flag indicate that window is transformed.
*/
#define PAINT_WINDOW_TRANSFORMED_MASK           (1 << 17)

/*
  flag indicate that core PaintWindow function should
  not draw this window.
*/
#define PAINT_WINDOW_NO_CORE_INSTANCE_MASK      (1 << 18)

/*
  flag indicate that blending is required.
*/
#define PAINT_WINDOW_BLEND_MASK                 (1 << 19)


typedef Bool (*PaintWindowProc) (CompWindow              *window,
                                 const WindowPaintAttrib *attrib,
                                 const CompTransform     *transform,
                                 Region                  region,
                                 unsigned int            mask);

typedef Bool (*DrawWindowProc) (CompWindow           *window,
                                const CompTransform  *transform,
                                const FragmentAttrib *fragment,
                                Region               region,
                                unsigned int         mask);

typedef void (*AddWindowGeometryProc) (CompWindow *window,
                                       CompMatrix *matrix,
                                       int        nMatrix,
                                       Region     region,
                                       Region     clip);

typedef void (*DrawWindowTextureProc) (CompWindow           *w,
                                       CompTexture          *texture,
                                       const FragmentAttrib *fragment,
                                       unsigned int         mask);

typedef void (*DrawWindowGeometryProc) (CompWindow *window);

typedef void (*PaintCursorProc) (CompCursor          *cursor,
                                 const CompTransform *transform,
                                 Region              region,
                                 unsigned int        mask);

void
preparePaintScreen (CompScreen *screen,
                    int         msSinceLastPaint);

void
donePaintScreen (CompScreen *screen);

void
transformToScreenSpace (CompScreen    *screen,
                        CompOutput    *output,
                        float         z,
                        CompTransform *transform);

/* XXX: prepareXCoords will be removed */
void
prepareXCoords (CompScreen *screen,
                CompOutput *output,
                float      z);

void
paintTransformedOutput (CompScreen              *screen,
                        const ScreenPaintAttrib *sAttrib,
                        const CompTransform     *transform,
                        Region                  region,
                        CompOutput              *output,
                        unsigned int            mask);

/* XXX: applyScreenTransform will be removed */
void
applyScreenTransform (CompScreen              *screen,
                      const ScreenPaintAttrib *sAttrib,
                      CompOutput              *output,
                      CompTransform           *transform);

void
enableOutputClipping (CompScreen          *screen,
                      const CompTransform *transform,
                      Region              region,
                      CompOutput          *output);

void
disableOutputClipping (CompScreen *screen);

void
paintScreen (CompScreen   *screen,
             CompOutput   *outputs,
             int          numOutput,
             unsigned int mask);

Bool
paintOutput (CompScreen              *screen,
             const ScreenPaintAttrib *sAttrib,
             const CompTransform     *transform,
             Region                  region,
             CompOutput              *output,
             unsigned int            mask);

Bool
moreWindowVertices (CompWindow *w,
                    int        newSize);

Bool
moreWindowIndices (CompWindow *w,
                   int        newSize);

void
addWindowGeometry (CompWindow *w,
                   CompMatrix *matrix,
                   int        nMatrix,
                   Region     region,
                   Region     clip);

void
drawWindowTexture (CompWindow           *w,
                   CompTexture          *texture,
                   const FragmentAttrib *fragment,
                   unsigned int         mask);

Bool
drawWindow (CompWindow           *w,
            const CompTransform  *transform,
            const FragmentAttrib *fragment,
            Region               region,
            unsigned int         mask);

Bool
paintWindow (CompWindow              *w,
             const WindowPaintAttrib *attrib,
             const CompTransform     *transform,
             Region                  region,
             unsigned int            mask);

void
paintCursor (CompCursor          *cursor,
             const CompTransform *transform,
             Region              region,
             unsigned int        mask);

/* texture.c */

#define POWER_OF_TWO(v) ((v & (v - 1)) == 0)

typedef enum {
	COMP_TEXTURE_FILTER_FAST,
	COMP_TEXTURE_FILTER_GOOD
} CompTextureFilter;

struct _CompTexture {
	GLuint     name;
	GLenum     target;
	GLfloat    dx, dy;
	GLXPixmap  pixmap;
	GLenum     filter;
	GLenum     wrap;
	CompMatrix matrix;
	Bool       oldMipmaps;
	Bool       mipmap;
	int        refCount;
};

void
initTexture (CompScreen  *screen,
             CompTexture *texture);

void
finiTexture (CompScreen  *screen,
             CompTexture *texture);

CompTexture *
createTexture (CompScreen *screen);

void
destroyTexture (CompScreen  *screen,
                CompTexture *texture);

Bool
imageBufferToTexture (CompScreen   *screen,
                      CompTexture  *texture,
                      const char   *image,
                      unsigned int width,
                      unsigned int height);

Bool
imageDataToTexture (CompScreen   *screen,
                    CompTexture  *texture,
                    const char   *image,
                    unsigned int width,
                    unsigned int height,
                    GLenum       format,
                    GLenum       type);


Bool
readImageToTexture (CompScreen   *screen,
                    CompTexture  *texture,
                    const char   *imageFileName,
                    unsigned int *width,
                    unsigned int *height);

Bool
iconToTexture (CompScreen *screen,
               CompIcon   *icon);

Bool
bindPixmapToTexture (CompScreen  *screen,
                     CompTexture *texture,
                     Pixmap      pixmap,
                     int         width,
                     int         height,
                     int         depth);

void
releasePixmapFromTexture (CompScreen  *screen,
                          CompTexture *texture);

void
enableTexture (CompScreen        *screen,
               CompTexture       *texture,
               CompTextureFilter filter);

void
enableTextureClampToBorder (CompScreen        *screen,
                            CompTexture       *texture,
                            CompTextureFilter filter);

void
enableTextureClampToEdge (CompScreen        *screen,
                          CompTexture       *texture,
                          CompTextureFilter filter);

void
disableTexture (CompScreen  *screen,
                CompTexture *texture);


/* screen.c */

#define DEFAULT_REFRESH_RATE               50

#ifndef GLX_EXT_texture_from_pixmap
#define GLX_BIND_TO_TEXTURE_RGB_EXT        0x20D0
#define GLX_BIND_TO_TEXTURE_RGBA_EXT       0x20D1
#define GLX_BIND_TO_MIPMAP_TEXTURE_EXT     0x20D2
#define GLX_BIND_TO_TEXTURE_TARGETS_EXT    0x20D3
#define GLX_Y_INVERTED_EXT                 0x20D4
#define GLX_TEXTURE_FORMAT_EXT             0x20D5
#define GLX_TEXTURE_TARGET_EXT             0x20D6
#define GLX_MIPMAP_TEXTURE_EXT             0x20D7
#define GLX_TEXTURE_FORMAT_NONE_EXT        0x20D8
#define GLX_TEXTURE_FORMAT_RGB_EXT         0x20D9
#define GLX_TEXTURE_FORMAT_RGBA_EXT        0x20DA
#define GLX_TEXTURE_1D_BIT_EXT             0x00000001
#define GLX_TEXTURE_2D_BIT_EXT             0x00000002
#define GLX_TEXTURE_RECTANGLE_BIT_EXT      0x00000004
#define GLX_TEXTURE_1D_EXT                 0x20DB
#define GLX_TEXTURE_2D_EXT                 0x20DC
#define GLX_TEXTURE_RECTANGLE_EXT          0x20DD
#define GLX_FRONT_LEFT_EXT                 0x20DE
#endif

#define OUTPUT_OVERLAP_MODE_SMART          0
#define OUTPUT_OVERLAP_MODE_PREFER_LARGER  1
#define OUTPUT_OVERLAP_MODE_PREFER_SMALLER 2
#define OUTPUT_OVERLAP_MODE_LAST           OUTPUT_OVERLAP_MODE_PREFER_SMALLER

#define FOCUS_PREVENTION_LEVEL_NONE     0
#define FOCUS_PREVENTION_LEVEL_LOW      1
#define FOCUS_PREVENTION_LEVEL_NORMAL   2
#define FOCUS_PREVENTION_LEVEL_HIGH     3
#define FOCUS_PREVENTION_LEVEL_VERYHIGH 4
#define FOCUS_PREVENTION_LEVEL_LAST     FOCUS_PREVENTION_LEVEL_VERYHIGH

typedef void (*FuncPtr) (void);
typedef FuncPtr (*GLXGetProcAddressProc) (const GLubyte *procName);

typedef void    (*GLXBindTexImageProc)    (Display       *display,
                                           GLXDrawable   drawable,
                                           int           buffer,
                                           int           *attribList);
typedef void    (*GLXReleaseTexImageProc) (Display      *display,
                                           GLXDrawable  drawable,
                                           int          buffer);
typedef void    (*GLXQueryDrawableProc)   (Display      *display,
                                           GLXDrawable  drawable,
                                           int          attribute,
                                           unsigned int  *value);

typedef void (*GLXCopySubBufferProc) (Display     *display,
                                      GLXDrawable drawable,
                                      int         x,
                                      int         y,
                                      int         width,
                                      int         height);

typedef int (*GLXGetVideoSyncProc)  (unsigned int *count);
typedef int (*GLXWaitVideoSyncProc) (int          divisor,
                                     int          remainder,
                                     unsigned int *count);

#ifndef GLX_VERSION_1_3
typedef struct __GLXFBConfigRec *GLXFBConfig;
#endif

typedef GLXFBConfig *(*GLXGetFBConfigsProc) (Display *display,
                                            int     screen,
                                            int     *nElements);
typedef int (*GLXGetFBConfigAttribProc) (Display     *display,
                                         GLXFBConfig config,
                                         int         attribute,
                                         int         *value);
typedef GLXPixmap (*GLXCreatePixmapProc) (Display     *display,
                                          GLXFBConfig config,
                                          Pixmap      pixmap,
                                          const int   *attribList);
typedef void (*GLXDestroyPixmapProc) (Display   *display,
                                      GLXPixmap pixmap);

typedef void (*GLActiveTextureProc) (GLenum texture);
typedef void (*GLClientActiveTextureProc) (GLenum texture);
typedef void (*GLMultiTexCoord2fProc) (GLenum, GLfloat, GLfloat);

typedef void (*GLGenProgramsProc) (GLsizei n,
                                   GLuint  *programs);
typedef void (*GLDeleteProgramsProc) (GLsizei n,
                                      GLuint  *programs);
typedef void (*GLBindProgramProc) (GLenum target,
                                   GLuint program);
typedef void (*GLProgramStringProc) (GLenum       target,
                                     GLenum       format,
                                     GLsizei      len,
                                     const GLvoid *string);
typedef void (*GLProgramParameter4fProc) (GLenum  target,
                                          GLuint  index,
                                          GLfloat x,
                                          GLfloat y,
                                          GLfloat z,
                                          GLfloat w);
typedef void (*GLGetProgramivProc) (GLenum target,
                                    GLenum pname,
                                    int    *params);

typedef void (*GLGenFramebuffersProc) (GLsizei n,
                                       GLuint  *framebuffers);
typedef void (*GLDeleteFramebuffersProc) (GLsizei n,
                                          GLuint  *framebuffers);
typedef void (*GLBindFramebufferProc) (GLenum target,
                                       GLuint framebuffer);
typedef GLenum (*GLCheckFramebufferStatusProc) (GLenum target);
typedef void (*GLFramebufferTexture2DProc) (GLenum target,
                                            GLenum attachment,
                                            GLenum textarget,
                                            GLuint texture,
                                            GLint  level);
typedef void (*GLGenerateMipmapProc) (GLenum target);

#define MAX_DEPTH 32

typedef void (*EnterShowDesktopModeProc) (CompScreen *screen);

typedef void (*LeaveShowDesktopModeProc) (CompScreen *screen,
                                          CompWindow *window);

typedef Bool (*DamageWindowRectProc) (CompWindow *w,
                                      Bool       initial,
                                      BoxPtr     rect);

typedef Bool (*DamageWindowRegionProc) (CompWindow *w,
                                        Region     region);

typedef Bool (*DamageCursorRectProc) (CompCursor *c,
                                      Bool       initial,
                                      BoxPtr     rect);


typedef void (*GetOutputExtentsForWindowProc) (CompWindow       *w,
                                              CompWindowExtents *output);

typedef void (*GetAllowedActionsForWindowProc) (CompWindow   *w,
                                                unsigned int *setActions,
                                                unsigned int *clearActions);

typedef Bool (*FocusWindowProc) (CompWindow *window);

typedef void (*ActivateWindowProc) (CompWindow *window);

typedef Bool (*PlaceWindowProc) (CompWindow *window,
                                 int        x,
                                 int        y,
                                 int        *newX,
                                 int        *newY);

typedef void (*ValidateWindowResizeRequestProc) (CompWindow     *window,
                                                 unsigned int   *mask,
                                                 XWindowChanges *xwc,
                                                 unsigned int   source);

typedef void (*WindowResizeNotifyProc) (CompWindow *window,
                                        int        dx,
                                        int        dy,
                                        int        dwidth,
                                        int        dheight);

typedef void (*WindowMoveNotifyProc) (CompWindow *window,
                                      int        dx,
                                      int        dy,
                                      Bool       immediate);

#define CompWindowGrabKeyMask         (1 << 0)
#define CompWindowGrabButtonMask      (1 << 1)
#define CompWindowGrabMoveMask        (1 << 2)
#define CompWindowGrabResizeMask      (1 << 3)
#define CompWindowGrabExternalAppMask (1 << 4)

typedef void (*WindowGrabNotifyProc) (CompWindow   *window,
                                      int      x,
                                      int      y,
                                      unsigned int state,
                                      unsigned int mask);

typedef void (*WindowUngrabNotifyProc) (CompWindow *window);

typedef void (*WindowStateChangeNotifyProc) (CompWindow   *window,
                                             unsigned int lastState);

typedef void (*WindowAddNotifyProc) (CompWindow *window);

typedef void (*OutputChangeNotifyProc) (CompScreen *screen);

typedef unsigned int (*AddSupportedAtomsProc) (CompScreen   *s,
                                               Atom         *atoms,
                                               unsigned int size);

typedef void (*InitWindowWalkerProc) (CompScreen *screen,
                                      CompWalker *walker);

#define COMP_SCREEN_DAMAGE_PENDING_MASK (1 << 0)
#define COMP_SCREEN_DAMAGE_REGION_MASK  (1 << 1)
#define COMP_SCREEN_DAMAGE_ALL_MASK     (1 << 2)

typedef struct _CompKeyGrab {
	int          keycode;
	unsigned int modifiers;
	int          count;
} CompKeyGrab;

typedef struct _CompButtonGrab {
	int          button;
	unsigned int modifiers;
	int          count;
} CompButtonGrab;

typedef struct _CompGrab {
	Bool       active;
	Cursor     cursor;
	const char *name;
} CompGrab;

typedef struct _CompGroup {
	struct _CompGroup *next;
	unsigned int      refCnt;
	Window            id;
} CompGroup;

typedef struct _CompStartupSequence {
	struct _CompStartupSequence *next;
	SnStartupSequence           *sequence;
	unsigned int                viewportX;
	unsigned int                viewportY;
} CompStartupSequence;

typedef struct _CompFBConfig {
	GLXFBConfig fbConfig;
	int         yInverted;
	int         mipmap;
	int         textureFormat;
	int         textureTargets;
} CompFBConfig;

#define NOTHING_TRANS_FILTER 0
#define SCREEN_TRANS_FILTER  1
#define WINDOW_TRANS_FILTER  2

#define SCREEN_EDGE_LEFT        0
#define SCREEN_EDGE_RIGHT       1
#define SCREEN_EDGE_TOP	        2
#define SCREEN_EDGE_BOTTOM      3
#define SCREEN_EDGE_TOPLEFT     4
#define SCREEN_EDGE_TOPRIGHT    5
#define SCREEN_EDGE_BOTTOMLEFT  6
#define SCREEN_EDGE_BOTTOMRIGHT 7
#define SCREEN_EDGE_NUM         8

typedef struct _CompScreenEdge {
	Window       id;
	unsigned int count;
} CompScreenEdge;

struct _CompIcon {
	CompTexture texture;
	int         width;
	int         height;
};

struct _CompOutput {
	char       *name;
	int        id;
	REGION     region;
	int        width;
	int        height;
	XRectangle workArea;
};

typedef struct _CompCursorImage {
	struct _CompCursorImage *next;

	unsigned long serial;
	Pixmap        pixmap;
	CompTexture   texture;
	int           xhot;
	int           yhot;
	int           width;
	int           height;
} CompCursorImage;

struct _CompCursor {
	struct _CompCursor *next;

	CompScreen      *screen;
	CompCursorImage *image;

	int     x;
	int     y;

	CompMatrix matrix;
};

#define ACTIVE_WINDOW_HISTORY_SIZE 64
#define ACTIVE_WINDOW_HISTORY_NUM  32

typedef struct _CompActiveWindowHistory {
	Window id[ACTIVE_WINDOW_HISTORY_SIZE];
	int    x;
	int    y;
	int    activeNum;
} CompActiveWindowHistory;

struct _CompScreen {
	CompPrivate    *privates;

	CompScreen  *next;

	CompWindow  *windows;
	CompWindow  *reverseWindows;

	char *windowPrivateIndices;
	int  windowPrivateLen;

	Colormap              colormap;
	int                   screenNum;
	int                   width;
	int                   height;
	int                   x;
	int                   y;
	int                   hsize;        /* Number of horizontal viewports */
	int                   vsize;        /* Number of vertical viewports */
	unsigned int        nDesktop;
	unsigned int        currentDesktop;
	REGION        region;
	Region        damage;
	unsigned long     damageMask;
	Window        root;
	Window        overlay;
	Window        output;
	XWindowAttributes attrib;
	Window        grabWindow;
	CompFBConfig      glxPixmapFBConfigs[MAX_DEPTH + 1];
	int                   textureRectangle;
	int                   textureNonPowerOfTwo;
	int                   textureEnvCombine;
	int                   textureEnvCrossbar;
	int                   textureBorderClamp;
	int                   textureCompression;
	GLint         maxTextureSize;
	int                   fbo;
	int                   fragmentProgram;
	int                   maxTextureUnits;
	Cursor        invisibleCursor;
	XRectangle        *exposeRects;
	int                   sizeExpose;
	int                   nExpose;
	CompTexture       backgroundTexture;
	Bool          backgroundLoaded;
	unsigned int      pendingDestroys;
	int                   desktopWindowCount;
	unsigned int      mapNum;
	unsigned int      activeNum;

	CompOutput *outputDev;
	int            nOutputDev;
	int            currentOutputDev;
	CompOutput fullscreenOutput;
	Bool       hasOverlappingOutputs;

	int windowOffsetX;
	int windowOffsetY;

	XRectangle lastViewport;

	CompActiveWindowHistory history[ACTIVE_WINDOW_HISTORY_NUM];
	int                         currentHistory;

	int overlayWindowCount;

	CompScreenEdge screenEdge[SCREEN_EDGE_NUM];

	SnMonitorContext    *snContext;
	CompStartupSequence *startupSequences;
	unsigned int        startupSequenceTimeoutHandle;

	int filter[3];

	CompGroup *groups;

	CompIcon *defaultIcon;

	Bool canDoSaturated;
	Bool canDoSlightlySaturated;

	Window wmSnSelectionWindow;
	Atom   wmSnAtom;
	Time   wmSnTimestamp;

	Cursor normalCursor;
	Cursor busyCursor;

	CompWindow **clientList;
	int        nClientList;

	CompButtonGrab *buttonGrab;
	int            nButtonGrab;
	CompKeyGrab    *keyGrab;
	int            nKeyGrab;

	CompGrab *grabs;
	int          grabSize;
	int          maxGrab;

	int                rasterX;
	int                rasterY;
	struct timeval lastRedraw;
	int                nextRedraw;
	int                redrawTime;
	int                optimalRedrawTime;
	int                frameStatus;
	int                timeMult;
	Bool       idle;
	int                timeLeft;
	Bool       pendingCommands;

	int lastFunctionId;

	CompFunction *fragmentFunctions;
	CompProgram  *fragmentPrograms;

	int saturateFunction[2][64];

	GLfloat projection[16];

	Bool clearBuffers;

	Bool lighting;
	Bool slowAnimations;

	XRectangle workArea;

	unsigned int showingDesktopMask;

	unsigned long *desktopHintData;
	int           desktopHintSize;

	CompCursor      *cursors;
	CompCursorImage *cursorImages;

	GLXGetProcAddressProc    getProcAddress;
	GLXBindTexImageProc      bindTexImage;
	GLXReleaseTexImageProc   releaseTexImage;
	GLXQueryDrawableProc     queryDrawable;
	GLXCopySubBufferProc     copySubBuffer;
	GLXGetVideoSyncProc      getVideoSync;
	GLXWaitVideoSyncProc     waitVideoSync;
	GLXGetFBConfigsProc      getFBConfigs;
	GLXGetFBConfigAttribProc getFBConfigAttrib;
	GLXCreatePixmapProc      createPixmap;
	GLXDestroyPixmapProc     destroyPixmap;

	GLActiveTextureProc       activeTexture;
	GLClientActiveTextureProc clientActiveTexture;
	GLMultiTexCoord2fProc     multiTexCoord2f;

	GLGenProgramsProc         genPrograms;
	GLDeleteProgramsProc     deletePrograms;
	GLBindProgramProc            bindProgram;
	GLProgramStringProc          programString;
	GLProgramParameter4fProc programEnvParameter4f;
	GLProgramParameter4fProc programLocalParameter4f;
	GLGetProgramivProc       getProgramiv;

	GLGenFramebuffersProc        genFramebuffers;
	GLDeleteFramebuffersProc     deleteFramebuffers;
	GLBindFramebufferProc        bindFramebuffer;
	GLCheckFramebufferStatusProc checkFramebufferStatus;
	GLFramebufferTexture2DProc   framebufferTexture2D;
	GLGenerateMipmapProc         generateMipmap;

	GLXContext ctx;

	PreparePaintScreenProc      preparePaintScreen;
	DonePaintScreenProc         donePaintScreen;
	PaintScreenProc	            paintScreen;
	PaintOutputProc	            paintOutput;
	PaintTransformedOutputProc          paintTransformedOutput;
	EnableOutputClippingProc            enableOutputClipping;
	DisableOutputClippingProc           disableOutputClipping;
	ApplyScreenTransformProc            applyScreenTransform;
	PaintWindowProc             paintWindow;
	DrawWindowProc              drawWindow;
	AddWindowGeometryProc       addWindowGeometry;
	DrawWindowTextureProc       drawWindowTexture;
	DamageWindowRectProc        damageWindowRect;
	GetOutputExtentsForWindowProc   getOutputExtentsForWindow;
	GetAllowedActionsForWindowProc  getAllowedActionsForWindow;
	FocusWindowProc            focusWindow;
	ActivateWindowProc              activateWindow;
	PlaceWindowProc                 placeWindow;
	ValidateWindowResizeRequestProc validateWindowResizeRequest;

	PaintCursorProc      paintCursor;
	DamageCursorRectProc damageCursorRect;

	WindowAddNotifyProc    windowAddNotify;
	WindowResizeNotifyProc windowResizeNotify;
	WindowMoveNotifyProc   windowMoveNotify;
	WindowGrabNotifyProc   windowGrabNotify;
	WindowUngrabNotifyProc windowUngrabNotify;

	EnterShowDesktopModeProc enterShowDesktopMode;
	LeaveShowDesktopModeProc leaveShowDesktopMode;

	WindowStateChangeNotifyProc windowStateChangeNotify;

	OutputChangeNotifyProc outputChangeNotify;
	AddSupportedAtomsProc  addSupportedAtoms;

	InitWindowWalkerProc initWindowWalker;

	CompMatch focus_prevention_match;

	void *reserved;
};

int
allocateScreenPrivateIndex (void);

void
freeScreenPrivateIndex (int         index);

void
screenChangeNotify (const char        *optionName,
                    BananaType        optionType,
                    const BananaValue *optionValue,
                    int               screenNum);

void
configureScreen (CompScreen      *s,
                 XConfigureEvent *ce);

void
setCurrentOutput (CompScreen *s,
                  int        outputNum);

void
setSupportedWmHints (CompScreen *s);

void
updateScreenBackground (CompScreen  *screen,
                        CompTexture *texture);

void
detectRefreshRateOfScreen (CompScreen *s);

void
showOutputWindow (CompScreen *s);

void
hideOutputWindow (CompScreen *s);

void
updateOutputWindow (CompScreen *s);

Bool
addScreen (int         screenNum,
           Window      wmSnSelectionWindow,
           Atom        wmSnAtom,
           Time        wmSnTimestamp);

void
removeScreen (CompScreen *s);

void
damageScreenRegion (CompScreen *screen,
                    Region     region);

void
damageScreen (CompScreen *screen);

void
damagePendingOnScreen (CompScreen *s);

void
insertWindowIntoScreen (CompScreen *s,
                        CompWindow *w,
                        Window     aboveId);

void
unhookWindowFromScreen (CompScreen *s,
                        CompWindow *w);

void
forEachWindowOnScreen (CompScreen        *screen,
                       ForEachWindowProc proc,
                       void              *closure);

CompWindow *
findWindowAtScreen (CompScreen *s,
                    Window     id);

CompWindow *
findTopLevelWindowAtScreen (CompScreen *s,
                            Window      id);

void
focusDefaultWindow (CompScreen *s);

int
pushScreenGrab (CompScreen *s,
                Cursor     cursor,
                const char *name);

void
updateScreenGrab (CompScreen *s,
                  int        index,
                  Cursor     cursor);

void
removeScreenGrab (CompScreen *s,
                  int        index,
                  XPoint     *restorePointer);

Bool
otherScreenGrabExist (CompScreen *s, ...);

Bool
addScreenKeyBinding (CompScreen     *s,
                     CompKeyBinding *key);

Bool
addScreenButtonBinding (CompScreen        *s,
                        CompButtonBinding *button);

void
removeScreenKeyBinding (CompScreen     *s,
                        CompKeyBinding *key);

void
removeScreenButtonBinding (CompScreen        *s,
                           CompButtonBinding *button);

void
updatePassiveGrabs (CompScreen *s);

void
updateWorkareaForScreen (CompScreen *s);

void
updateClientListForScreen (CompScreen *s);

Window
getActiveWindow (Window      root);

void
toolkitAction (CompScreen *s,
               Atom       toolkitAction,
               Time       eventTime,
               Window     window,
               long       data0,
               long       data1,
               long       data2);

void
runCommand (CompScreen *s,
            const char *command);

void
moveScreenViewport (CompScreen *s,
                    int        tx,
                    int        ty,
                    Bool       sync);

void
moveWindowToViewportPosition (CompWindow *w,
                              int        x,
                              int        y,
                              Bool       sync);

CompGroup *
addGroupToScreen (CompScreen *s,
                  Window     id);
void
removeGroupFromScreen (CompScreen *s,
                       CompGroup  *group);

CompGroup *
findGroupAtScreen (CompScreen *s,
                   Window     id);

void
applyStartupProperties (CompScreen *screen,
                        CompWindow *window);

void
sendWindowActivationRequest (CompScreen *s,
                             Window     id);

void
screenTexEnvMode (CompScreen *s,
                  GLenum     mode);

void
screenLighting (CompScreen *s,
                Bool       lighting);

void
enableScreenEdge (CompScreen *s,
                  int        edge);

void
disableScreenEdge (CompScreen *s,
                   int        edge);

Window
getTopWindow (CompScreen *s);

void
makeScreenCurrent (CompScreen *s);

void
finishScreenDrawing (CompScreen *s);

int
outputDeviceForPoint (CompScreen *s,
                      int        x,
                      int        y);

void
getCurrentOutputExtents (CompScreen *s,
                         int        *x1,
                         int        *y1,
                         int        *x2,
                         int        *y2);

void
getWorkareaForOutput (CompScreen *s,
                      int        output,
                      XRectangle *area);

void
setNumberOfDesktops (CompScreen   *s,
                     unsigned int nDesktop);

void
setCurrentDesktop (CompScreen   *s,
                   unsigned int desktop);

void
setDefaultViewport (CompScreen *s);

void
outputChangeNotify (CompScreen *s);

void
clearScreenOutput (CompScreen   *s,
                   CompOutput   *output,
                   unsigned int mask);

void
viewportForGeometry (CompScreen *s,
                     int        x,
                     int        y,
                     int        width,
                     int        height,
                     int        borderWidth,
                     int        *viewportX,
                     int        *viewportY);

int
outputDeviceForGeometry (CompScreen *s,
                         int        x,
                         int        y,
                         int        width,
                         int        height,
                         int        borderWidth);

Bool
updateDefaultIcon (CompScreen *screen);

CompCursor *
findCursorAtScreen (CompScreen *screen);

CompCursorImage *
findCursorImageAtScreen (CompScreen    *screen,
                        unsigned long serial);

void
setCurrentActiveWindowHistory (CompScreen *s,
                               int        x,
                               int        y);

void
addToCurrentActiveWindowHistory (CompScreen *s,
                                 Window     id);

void
setWindowPaintOffset (CompScreen *s,
                      int        x,
                      int        y);


/* window.c */

#define WINDOW_INVISIBLE(w)  \
	((w)->attrib.map_state != IsViewable                    || \
	 (!(w)->damaged)                                        || \
	 (w)->attrib.x + (w)->width  + (w)->output.right  <= 0  || \
	 (w)->attrib.y + (w)->height + (w)->output.bottom <= 0  || \
	 (w)->attrib.x - (w)->output.left >= (w)->screen->width || \
	 (w)->attrib.y - (w)->output.top >= (w)->screen->height)

typedef enum {
	CompStackingUpdateModeNone = 0,
	CompStackingUpdateModeNormal,
	CompStackingUpdateModeAboveFullscreen,
	CompStackingUpdateModeInitialMap,
	CompStackingUpdateModeInitialMapDeniedFocus
} CompStackingUpdateMode;

typedef enum {
	CompFocusAllowed = 0,
	CompFocusPrevent,
	CompFocusDenied
} CompFocusResult;

struct _CompWindowExtents {
	int left;
	int right;
	int top;
	int bottom;
};

typedef struct _CompStruts {
	XRectangle left;
	XRectangle right;
	XRectangle top;
	XRectangle bottom;
} CompStruts;

struct _CompWindow {
	CompPrivate    *privates;

	CompScreen *screen;
	CompWindow *next;
	CompWindow *prev;

	int           refcnt;
	Window        id;
	Window        frame;
	unsigned int      mapNum;
	unsigned int      activeNum;
	XWindowAttributes attrib;
	int                   serverX;
	int                   serverY;
	int                   serverWidth;
	int                   serverHeight;
	int                   serverBorderWidth;
	Window        transientFor;
	Window        clientLeader;
	XWMHints              *hints;
	XSizeHints            sizeHints;
	Pixmap         pixmap;
	CompTexture       *texture;
	CompMatrix        matrix;
	Damage        damage;
	Bool         inputHint;
	Bool         alpha;
	GLint        width;
	GLint        height;
	Region        region;
	Region        clip;
	unsigned int      wmType;
	unsigned int      type;
	unsigned int      state;
	unsigned int      actions;
	unsigned int      protocols;
	unsigned int      mwmDecor;
	unsigned int      mwmFunc;
	Bool          invisible;
	Bool          destroyed;
	Bool          damaged;
	Bool          redirected;
	Bool          managed;
	Bool          unmanaging;
	Bool          bindFailed;
	Bool          added;
	Bool          overlayWindow;
	int                   destroyRefCnt;
	int                   unmapRefCnt;

	unsigned int initialViewportX;
	unsigned int initialViewportY;

	Time initialTimestamp;
	Bool initialTimestampSet;

	Bool placed;
	Bool minimized;
	Bool inShowDesktopMode;
	Bool shaded;
	Bool hidden;
	Bool grabbed;

	unsigned int desktop;

	int pendingUnmaps;
	int pendingMaps;

	char *startupId;
	char *resName;
	char *resClass;

	char *title;
	char *role;

	CompGroup *group;

	unsigned int lastPong;
	Bool         alive;

	WindowPaintAttrib paint;
	WindowPaintAttrib lastPaint;

	unsigned int lastMask;

	CompWindowExtents input;
	CompWindowExtents output;

	CompStruts *struts;

	CompIcon **icon;
	int      nIcon;

	XRectangle iconGeometry;
	Bool       iconGeometrySet;

	XRectangle fullscreenMonitorRect;
	Bool       fullscreenMonitorsSet;

	XWindowChanges saveWc;
	int            saveMask;

	XSyncCounter  syncCounter;
	XSyncValue    syncValue;
	XSyncAlarm    syncAlarm;
	unsigned long syncAlarmConnection;
	unsigned int  syncWaitHandle;

	Bool syncWait;
	int      syncX;
	int      syncY;
	int      syncWidth;
	int      syncHeight;
	int      syncBorderWidth;

	Bool closeRequests;
	Time lastCloseRequestTime;

	XRectangle *damageRects;
	int            sizeDamage;
	int            nDamage;

	GLfloat  *vertices;
	int      vertexSize;
	int      vertexStride;
	GLushort *indices;
	int      indexSize;
	int      vCount;
	int      texUnits;
	int      texCoordSize;
	int      indexCount;

	/* must be set by addWindowGeometry */
	DrawWindowGeometryProc drawWindowGeometry;

	void *reserved;
};

int
allocateWindowPrivateIndex (CompScreen *screen);

void
freeWindowPrivateIndex (CompScreen *screen,
                        int        index);

unsigned int
windowStateMask (Atom        state);

unsigned int
windowStateFromString (const char *str);

unsigned int
getWindowState (Window      id);

void
setWindowState (unsigned int state,
                Window       id);

void
changeWindowState (CompWindow   *w,
                  unsigned int newState);

void
recalcWindowActions (CompWindow *w);

unsigned int
constrainWindowState (unsigned int state,
                      unsigned int actions);

unsigned int
windowTypeFromString (const char *str);

unsigned int
getWindowType (Window      id);

void
recalcWindowType (CompWindow *w);

void
getMwmHints (Window       id,
             unsigned int *func,
             unsigned int *decor);

unsigned int
getProtocols (Window      id);

unsigned int
getWindowProp (Window       id,
               Atom         property,
               unsigned int defaultValue);

void
setWindowProp (Window       id,
               Atom         property,
               unsigned int value);

Bool
readWindowProp32 (Window         id,
                  Atom           property,
                  unsigned short *returnValue);

unsigned short
getWindowProp32 (Window	        id,
                 Atom           property,
                 unsigned short defaultValue);

void
setWindowProp32 (Window         id,
                 Atom           property,
                 unsigned short value);

void
updateNormalHints (CompWindow *window);

void
updateWmHints (CompWindow *w);

void
updateWindowClassHints (CompWindow *window);

void
updateTransientHint (CompWindow *w);

void
updateIconGeometry (CompWindow *w);

Window
getClientLeader (CompWindow *w);

char *
getStartupId (CompWindow *w);

int
getWmState (Window      id);

void
setWmState (int         state,
            Window      id);

void
setWindowFullscreenMonitors (CompWindow               *w,
                             CompFullscreenMonitorSet *monitors);

void
setWindowFrameExtents (CompWindow        *w,
                       CompWindowExtents *input);

void
updateWindowOutputExtents (CompWindow *w);

void
updateWindowRegion (CompWindow *w);

Bool
updateWindowStruts (CompWindow *w);

void
addWindow (CompScreen *screen,
           Window     id,
           Window     aboveId);

void
removeWindow (CompWindow *w);

void
destroyWindow (CompWindow *w);

void
sendConfigureNotify (CompWindow *w);

void
mapWindow (CompWindow *w);

void
unmapWindow (CompWindow *w);

Bool
bindWindow (CompWindow *w);

void
releaseWindow (CompWindow *w);

void
moveWindow (CompWindow *w,
            int        dx,
            int        dy,
            Bool       damage,
            Bool       immediate);

void
configureXWindow (CompWindow     *w,
                  unsigned int   valueMask,
                  XWindowChanges *xwc);

unsigned int
adjustConfigureRequestForGravity (CompWindow     *w,
                                  XWindowChanges *xwc,
                                  unsigned int   xwcm,
                                  int            gravity,
                                  int            direction);

void
moveResizeWindow (CompWindow     *w,
                  XWindowChanges *xwc,
                  unsigned int   xwcm,
                  int            gravity,
                  unsigned int   source);

void
syncWindowPosition (CompWindow *w);

void
sendSyncRequest (CompWindow *w);

Bool
resizeWindow (CompWindow *w,
              int        x,
              int        y,
              int        width,
              int        height,
              int        borderWidth);

void
configureWindow (CompWindow      *w,
                 XConfigureEvent *ce);

void
circulateWindow (CompWindow      *w,
                 XCirculateEvent *ce);

void
addWindowDamageRect (CompWindow *w,
                     BoxPtr     rect);

void
getOutputExtentsForWindow (CompWindow        *w,
                           CompWindowExtents *output);

void
getAllowedActionsForWindow (CompWindow   *w,
                            unsigned int *setActions,
                            unsigned int *clearActions);

void
addWindowDamage (CompWindow *w);

void
damageWindowOutputExtents (CompWindow *w);

Bool
damageWindowRect (CompWindow *w,
                  Bool       initial,
                  BoxPtr     rect);

void
damageTransformedWindowRect (CompWindow *w,
                             float      xScale,
                             float      yScale,
                             float      xTranslate,
                             float      yTranslate,
                             BoxPtr     rect);

Bool
focusWindow (CompWindow *w);

Bool
placeWindow (CompWindow *w,
             int        x,
             int        y,
             int        *newX,
             int        *newY);

void
validateWindowResizeRequest (CompWindow     *w,
                             unsigned int   *mask,
                             XWindowChanges *xwc,
                             unsigned int   source);

void
windowResizeNotify (CompWindow *w,
                    int        dx,
                    int        dy,
                    int        dwidth,
                    int        dheight);

void
windowMoveNotify (CompWindow *w,
                  int        dx,
                  int        dy,
                  Bool       immediate);

void
windowGrabNotify (CompWindow   *w,
                  int          x,
                  int          y,
                  unsigned int state,
                  unsigned int mask);

void
windowUngrabNotify (CompWindow *w);

void
windowStateChangeNotify (CompWindow   *w,
                         unsigned int lastState);

void
moveInputFocusToWindow (CompWindow *w);

void
updateWindowSize (CompWindow *w);

void
raiseWindow (CompWindow *w);

void
lowerWindow (CompWindow *w);

void
restackWindowAbove (CompWindow *w,
                    CompWindow *sibling);

void
restackWindowBelow (CompWindow *w,
                    CompWindow *sibling);

void
updateWindowAttributes (CompWindow             *w,
                        CompStackingUpdateMode stackingMode);

void
activateWindow (CompWindow *w);

void
closeWindow (CompWindow *w,
             Time       serverTime);

Bool
constrainNewWindowSize (CompWindow *w,
                        int        width,
                        int        height,
                        int        *newWidth,
                        int        *newHeight);

void
hideWindow (CompWindow *w);

void
showWindow (CompWindow *w);

void
minimizeWindow (CompWindow *w);

void
unminimizeWindow (CompWindow *w);

void
maximizeWindow (CompWindow *w,
                int        state);

Bool
getWindowUserTime (CompWindow *w,
                   Time       *time);

void
setWindowUserTime (CompWindow *w,
                   Time       time);

CompFocusResult
allowWindowFocus (CompWindow   *w,
                 unsigned int noFocusMask,
                 unsigned int viewportX,
                 unsigned int viewportY,
                 Time         timestamp);

void
unredirectWindow (CompWindow *w);

void
redirectWindow (CompWindow *w);

void
defaultViewportForWindow (CompWindow *w,
                          int        *vx,
                          int        *vy);

CompIcon *
getWindowIcon (CompWindow *w,
               int        width,
               int        height);

void
freeWindowIcons (CompWindow *w);

int
outputDeviceForWindow (CompWindow *w);

Bool
onCurrentDesktop (CompWindow *w);

void
setDesktopForWindow (CompWindow   *w,
                     unsigned int desktop);

int
compareWindowActiveness (CompWindow *w1,
                         CompWindow *w2);

void
windowAddNotify (CompWindow *w);

Bool
windowOnAllViewports (CompWindow *w);

void
getWindowMovementForOffset (CompWindow *w,
                            int        offX,
                            int        offY,
                            int        *retX,
                            int        *retY);

char *
getWindowStringProperty (CompWindow *w,
                         Atom       propAtom,
                         Atom       formatAtom);

char *
getWindowTitle (CompWindow *w);

/* plugin.c */

#define HOME_PLUGINDIR ".fusilli/plugins"

typedef CompPluginVTable *(*PluginGetInfoProc) (void);

typedef Bool (*LoadPluginProc) (CompPlugin *p,
                                const char *path,
                                const char *name);

typedef void (*UnloadPluginProc) (CompPlugin *p);

typedef char **(*ListPluginsProc) (const char *path,
                                   int        *n);

extern LoadPluginProc   loaderLoadPlugin;
extern UnloadPluginProc loaderUnloadPlugin;
extern ListPluginsProc  loaderListPlugins;

struct _CompPlugin {
	CompPlugin       *next;
	CompPrivate      devPrivate;
	char             *devType;
	CompPluginVTable *vTable;
};

Bool
windowInitPlugins (CompWindow *w);

void
windowFiniPlugins (CompWindow *w);

CompPlugin *
findActivePlugin (const char *name);

CompPlugin *
loadPlugin (const char *plugin);

void
unloadPlugin (CompPlugin *p);

Bool
pushPlugin (CompPlugin *p);

CompPlugin *
popPlugin (void);

CompPlugin *
getPlugins (void);

char **
availablePlugins (int *n);

/* fragment.c */

#define MAX_FRAGMENT_FUNCTIONS 16

struct _FragmentAttrib {
	GLushort opacity;
	GLushort brightness;
	GLushort saturation;
	int      nTexture;
	int      function[MAX_FRAGMENT_FUNCTIONS];
	int      nFunction;
	int      nParam;
};

CompFunctionData *
createFunctionData (void);

void
destroyFunctionData (CompFunctionData *data);

Bool
addTempHeaderOpToFunctionData (CompFunctionData *data,
                               const char       *name);

Bool
addParamHeaderOpToFunctionData (CompFunctionData *data,
                                const char       *name);

Bool
addAttribHeaderOpToFunctionData (CompFunctionData *data,
                                 const char       *name);

#define COMP_FETCH_TARGET_2D   0
#define COMP_FETCH_TARGET_RECT 1
#define COMP_FETCH_TARGET_NUM  2

Bool
addFetchOpToFunctionData (CompFunctionData *data,
                          const char       *dst,
                          const char       *offset,
                          int              target);

Bool
addColorOpToFunctionData (CompFunctionData *data,
                          const char       *dst,
                          const char       *src);

Bool
addDataOpToFunctionData (CompFunctionData *data,
                         const char       *str,
                         ...);

Bool
addBlendOpToFunctionData (CompFunctionData *data,
                          const char       *str,
                          ...);

int
createFragmentFunction (CompScreen       *s,
                        const char       *name,
                        CompFunctionData *data);

void
destroyFragmentFunction (CompScreen *s,
                         int        id);

int
getSaturateFragmentFunction (CompScreen  *s,
                             CompTexture *texture,
                             int         param);

int
allocFragmentTextureUnits (FragmentAttrib *attrib,
                           int            nTexture);

int
allocFragmentParameters (FragmentAttrib *attrib,
                         int            nParam);

void
addFragmentFunction (FragmentAttrib *attrib,
                     int            function);

void
initFragmentAttrib (FragmentAttrib          *attrib,
                    const WindowPaintAttrib *paint);

Bool
enableFragmentAttrib (CompScreen     *s,
                      FragmentAttrib *attrib,
                      Bool           *blending);

void
disableFragmentAttrib (CompScreen     *s,
                       FragmentAttrib *attrib);


/* matrix.c */

void
matrixMultiply (CompTransform       *product,
                const CompTransform *transformA,
                const CompTransform *transformB);

void
matrixMultiplyVector (CompVector          *product,
                      const CompVector    *vector,
                      const CompTransform *transform);

void
matrixVectorDiv (CompVector *v);

void
matrixRotate (CompTransform *transform,
              float         angle,
              float         x,
              float         y,
              float         z);

void
matrixScale (CompTransform *transform,
             float         x,
             float         y,
             float         z);

void
matrixTranslate (CompTransform *transform,
                 float         x,
                 float         y,
                 float         z);

void
matrixGetIdentity (CompTransform *m);

/* cursor.c */

void
addCursor (CompScreen *s);

Bool
damageCursorRect (CompCursor *c,
                  Bool       initial,
                  BoxPtr     rect);

void
addCursorDamageRect (CompCursor *c,
                     BoxPtr     rect);

void
addCursorDamage (CompCursor *c);

void
updateCursor (CompCursor    *c,
             int            x,
             int            y,
             unsigned long serial);


/* match.c */

void
matchInit (CompMatch *match);

void
matchFini (CompMatch *match);

Bool
matchEqual (CompMatch *m1,
            CompMatch *m2);

Bool
matchCopy (CompMatch *dst,
           CompMatch *src);

Bool
matchAddGroup (CompMatch *match,
               int       flags,
               CompMatch *group);

Bool
matchAddExp (CompMatch  *match,
             int        flags,
             const char *value);

void
matchAddFromString (CompMatch  *match,
                    const char *str);

char *
matchToString (CompMatch *match);

void
matchUpdate (CompMatch   *match);

Bool
matchEval (CompMatch  *match,
           CompWindow *window);

void
matchPropertyChanged (CompWindow  *window);

/* png.c */
Bool
pngImageToFile (const char  *path,
                const char  *name,
                int         width,
                int         height,
                int         stride,
                void        *data);

Bool
pngFileToImage (const char  *path,
                const char  *name,
                int         *width,
                int         *height,
                int         *stride,
                void        **data);

/* jpeg.c */
Bool
JPEGImageToFile (const char  *path,
                 const char  *name,
                 int         width,
                 int         height,
                 int         stride,
                 void        *data);

Bool
JPEGFileToImage (const char  *path,
                 const char  *name,
                 int         *width,
                 int         *height,
                 int         *stride,
                 void        **data);

/* metadata.c */

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY (x)
#define MINTOSTRING(x) "<min>" TOSTRING (x) "</min>"
#define MAXTOSTRING(x) "<max>" TOSTRING (x) "</max>"
#define RESTOSTRING(min, max) MINTOSTRING (min) MAXTOSTRING (max)

#ifdef  __cplusplus
}
#endif

#endif
