#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <compiz-core.h>
#include "compiz-animation.h"


typedef struct _WaveParam
{
    float halfWidth;
    float amp;
    float pos;
} WaveParam;

typedef enum
{
    ZoomFromCenterOff = 0,
    ZoomFromCenterMin,
    ZoomFromCenterCreate,
    ZoomFromCenterOn
} ZoomFromCenter;
#define LAST_ZOOM_FROM_CENTER 3

//TODO remove #define RANDOM_EFFECT_OFFSET 2 /* skip None and Random */

typedef struct _RestackInfo
{
    CompWindow *wRestacked, *wStart, *wEnd, *wOldAbove;
    Bool raised;
} RestackInfo;

typedef struct _IdValuePair
{
    const ExtensionPluginInfo *pluginInfo;
    int optionId;
    CompOptionValue value;
} IdValuePair;
    
typedef struct _OptionSet
{
    int nPairs;
    IdValuePair *pairs;
} OptionSet;

typedef struct _OptionSets
{
    int nSets;
    OptionSet *sets;
} OptionSets;

typedef struct _EffectSet
{
    int n;
    AnimEffect *effects;
} EffectSet;

extern int animDisplayPrivateIndex;
extern int animFunctionsPrivateIndex;
extern CompMetadata animMetadata;

extern AnimEffect AnimEffectNone;
extern AnimEffect AnimEffectRandom;
extern AnimEffect AnimEffectCurvedFold;
extern AnimEffect AnimEffectDodge;
extern AnimEffect AnimEffectDream;
extern AnimEffect AnimEffectFade;
extern AnimEffect AnimEffectFocusFade;
extern AnimEffect AnimEffectGlide1;
extern AnimEffect AnimEffectGlide2;
extern AnimEffect AnimEffectHorizontalFolds;
extern AnimEffect AnimEffectMagicLamp;
extern AnimEffect AnimEffectRollUp;
extern AnimEffect AnimEffectSidekick;
extern AnimEffect AnimEffectVacuum;
extern AnimEffect AnimEffectWave;
extern AnimEffect AnimEffectZoom;

#define NUM_EFFECTS 16

extern int customOptionOptionIds[AnimEventNum];

typedef enum _AnimDisplayOptions
{
    ANIM_DISPLAY_OPTION_ABI,
    ANIM_DISPLAY_OPTION_INDEX,
    ANIM_DISPLAY_OPTION_NUM
} AnimDisplayOptions;

typedef struct _AnimDisplay
{
    int screenPrivateIndex;
    HandleEventProc handleEvent;
    HandleCompizEventProc handleCompizEvent;
    int activeWindow;
    CompMatch neverAnimateMatch;

    CompOption opt[ANIM_DISPLAY_OPTION_NUM];
} AnimDisplay;

typedef struct _PluginEventInfo
{
    char *pluginName;
    char *activateEventName;
} PluginEventInfo;


#define NUM_SWITCHERS 6
#define NUM_WATCHED_PLUGINS (NUM_SWITCHERS + 2)

typedef enum
{
    // Event settings
    ANIM_SCREEN_OPTION_OPEN_EFFECTS = 0,
    ANIM_SCREEN_OPTION_OPEN_DURATIONS,
    ANIM_SCREEN_OPTION_OPEN_MATCHES,
    ANIM_SCREEN_OPTION_OPEN_OPTIONS,
    ANIM_SCREEN_OPTION_OPEN_RANDOM_EFFECTS,
    ANIM_SCREEN_OPTION_CLOSE_EFFECTS,
    ANIM_SCREEN_OPTION_CLOSE_DURATIONS,
    ANIM_SCREEN_OPTION_CLOSE_MATCHES,
    ANIM_SCREEN_OPTION_CLOSE_OPTIONS,
    ANIM_SCREEN_OPTION_CLOSE_RANDOM_EFFECTS,
    ANIM_SCREEN_OPTION_MINIMIZE_EFFECTS,
    ANIM_SCREEN_OPTION_MINIMIZE_DURATIONS,
    ANIM_SCREEN_OPTION_MINIMIZE_MATCHES,
    ANIM_SCREEN_OPTION_MINIMIZE_OPTIONS,
    ANIM_SCREEN_OPTION_MINIMIZE_RANDOM_EFFECTS,
    ANIM_SCREEN_OPTION_SHADE_EFFECTS,
    ANIM_SCREEN_OPTION_SHADE_DURATIONS,
    ANIM_SCREEN_OPTION_SHADE_MATCHES,
    ANIM_SCREEN_OPTION_SHADE_OPTIONS,
    ANIM_SCREEN_OPTION_SHADE_RANDOM_EFFECTS,
    ANIM_SCREEN_OPTION_FOCUS_EFFECTS,
    ANIM_SCREEN_OPTION_FOCUS_DURATIONS,
    ANIM_SCREEN_OPTION_FOCUS_MATCHES,
    ANIM_SCREEN_OPTION_FOCUS_OPTIONS,
    // Misc. settings
    ANIM_SCREEN_OPTION_ALL_RANDOM,
    ANIM_SCREEN_OPTION_TIME_STEP,
    // Effect settings
    ANIM_SCREEN_OPTION_CURVED_FOLD_AMP_MULT,
    ANIM_SCREEN_OPTION_CURVED_FOLD_Z2TOM,
    ANIM_SCREEN_OPTION_DODGE_GAP_RATIO,
    ANIM_SCREEN_OPTION_DREAM_Z2TOM,
    ANIM_SCREEN_OPTION_GLIDE1_AWAY_POS,
    ANIM_SCREEN_OPTION_GLIDE1_AWAY_ANGLE,
    ANIM_SCREEN_OPTION_GLIDE1_Z2TOM,
    ANIM_SCREEN_OPTION_GLIDE2_AWAY_POS,
    ANIM_SCREEN_OPTION_GLIDE2_AWAY_ANGLE,
    ANIM_SCREEN_OPTION_GLIDE2_Z2TOM,
    ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_AMP_MULT,
    ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_NUM_FOLDS,
    ANIM_SCREEN_OPTION_HORIZONTAL_FOLDS_Z2TOM,
    ANIM_SCREEN_OPTION_MAGIC_LAMP_MOVING_END,
    ANIM_SCREEN_OPTION_MAGIC_LAMP_GRID_RES,
    ANIM_SCREEN_OPTION_MAGIC_LAMP_MAX_WAVES,
    ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MIN,
    ANIM_SCREEN_OPTION_MAGIC_LAMP_WAVE_AMP_MAX,
    ANIM_SCREEN_OPTION_MAGIC_LAMP_OPEN_START_WIDTH,
    ANIM_SCREEN_OPTION_ROLLUP_FIXED_INTERIOR,
    ANIM_SCREEN_OPTION_SIDEKICK_NUM_ROTATIONS,
    ANIM_SCREEN_OPTION_SIDEKICK_SPRINGINESS,
    ANIM_SCREEN_OPTION_SIDEKICK_ZOOM_FROM_CENTER,
    ANIM_SCREEN_OPTION_VACUUM_MOVING_END,
    ANIM_SCREEN_OPTION_VACUUM_GRID_RES,
    ANIM_SCREEN_OPTION_VACUUM_OPEN_START_WIDTH,
    ANIM_SCREEN_OPTION_WAVE_WIDTH,
    ANIM_SCREEN_OPTION_WAVE_AMP_MULT,
    ANIM_SCREEN_OPTION_ZOOM_FROM_CENTER,
    ANIM_SCREEN_OPTION_ZOOM_SPRINGINESS,

    ANIM_SCREEN_OPTION_NUM
} AnimScreenOptions;

// This must have the value of the first "effect setting" above
// in AnimScreenOptions
#define NUM_NONEFFECT_OPTIONS ANIM_SCREEN_OPTION_CURVED_FOLD_AMP_MULT


typedef struct _AnimScreen
{
    int windowPrivateIndex;

    PreparePaintScreenProc preparePaintScreen;
    DonePaintScreenProc donePaintScreen;
    PaintOutputProc paintOutput;
    PaintWindowProc paintWindow;
    DamageWindowRectProc damageWindowRect;
    AddWindowGeometryProc addWindowGeometry;
    DrawWindowTextureProc drawWindowTexture;
    InitWindowWalkerProc initWindowWalker;

    WindowResizeNotifyProc windowResizeNotify;
    WindowMoveNotifyProc windowMoveNotify;
    WindowGrabNotifyProc windowGrabNotify;
    WindowUngrabNotifyProc windowUngrabNotify;

    CompOption opt[ANIM_SCREEN_OPTION_NUM];

    Bool aWinWasRestackedJustNow; // a window was restacked this paint round

    Bool pluginActive[NUM_WATCHED_PLUGINS];

    Window *lastClientListStacking; // to store last known stacking order
    int nLastClientListStacking;
    int startCountdown;
    // to mark windows as "created" if they were opened before compiz
    // was started and to prevent open animation happening for existing windows
    // at compiz startup

    Bool animInProgress;

    int walkerAnimCount; // count of how many windows are currently involved in
			 // animations that require walker (dodge & focus fade)

    EffectSet randomEffects[AnimEventNum];

    OptionSets eventOptionSets[AnimEventNum];

    // Effect extensions
    ExtensionPluginInfo **extensionPlugins;
    unsigned int nExtensionPlugins;
    unsigned int maxExtensionPlugins;

    // List of all possible effects for each event
    AnimEffect *eventEffectsAllowed[AnimEventNum];
    unsigned int nEventEffectsAllowed[AnimEventNum];
    unsigned int maxEventEffectsAllowed[AnimEventNum];

    // List of chosen effects for each event
    EffectSet eventEffects[AnimEventNum];

    CompOutput *output;
} AnimScreen;

typedef struct _AnimWindow
{
    AnimWindowCommon com;

    unsigned int state;
    unsigned int newState;

    Bool animInitialized;	// whether the animation effect (not the window) is initialized
    float remainderSteps;

    Bool nowShaded;
    Bool grabbed;

    int unmapCnt;
    int destroyCnt;

    Bool ignoreDamage;

    int curAnimSelectionRow;
    int prevAnimSelectionRow;	// For the case when one event interrupts another

    Box BB;       // Bounding box for damage region calc. of CompTransform fx
    Box lastBB;   // Last bounding box

    // for magic lamp
    Bool minimizeToTop;
    int magicLampWaveCount;
    WaveParam *magicLampWaves;

    // for glide effect
    float glideModRotAngle;	// The angle of rotation modulo 360

    // for zoom
    float numZoomRotations;

    // for focus fade & dodge
    RestackInfo *restackInfo;   // restack info if window was restacked this paint round
    CompWindow *winToBePaintedBeforeThis; // Window which should be painted before this
    CompWindow *winThisIsPaintedBefore; // the inverse relation of the above
    CompWindow *moreToBePaintedPrev; // doubly linked list for windows underneath that
    CompWindow *moreToBePaintedNext; //   raise together with this one
    Bool created;
    Bool configureNotified;     // was configureNotified before restack check
    CompWindow *winPassingThrough; // win. passing through this one during focus effect

    // for dodge
    Bool isDodgeSubject;	// TRUE if this window is the cause of dodging
    Bool skipPostPrepareScreen;
    CompWindow *dodgeSubjectWin;// The window being dodged
    float dodgeMaxAmount;	/* max # pixels it should dodge
				   (neg. values dodge left) */
    int dodgeOrder;		// dodge order (used temporarily)
    Bool dodgeDirection;	// 0: up, down, left, right

    CompWindow *dodgeChainStart;// for the subject window
    CompWindow *dodgeChainPrev;	// for dodging windows
    CompWindow *dodgeChainNext;	// for dodging windows
    Bool walkerOverNewCopy;     // whether walker is on the copy at the new pos.
    unsigned int walkerVisitCount; // how many times walker has visited this window
} AnimWindow;

#define GET_ANIM_DISPLAY(d)						\
    ((AnimDisplay *) (d)->base.privates[animDisplayPrivateIndex].ptr)

#define ANIM_DISPLAY(d)				\
    AnimDisplay *ad = GET_ANIM_DISPLAY (d)

#define GET_ANIM_SCREEN(s, ad)						\
    ((AnimScreen *) (s)->base.privates[(ad)->screenPrivateIndex].ptr)

#define ANIM_SCREEN(s)							\
    AnimScreen *as = GET_ANIM_SCREEN (s, GET_ANIM_DISPLAY (s->display))

#define GET_ANIM_WINDOW(w, as)						\
    ((AnimWindow *) (w)->base.privates[(as)->windowPrivateIndex].ptr)

#define ANIM_WINDOW(w)					     \
    AnimWindow *aw = GET_ANIM_WINDOW (w,                     \
		     GET_ANIM_SCREEN (w->screen,             \
		     GET_ANIM_DISPLAY (w->screen->display)))

// up, down, left, right
#define DODGE_AMOUNT(w, dw, dir)			\
    ((dir) == 0 ? BORDER_Y(w) - (BORDER_Y(dw) + BORDER_H(dw)) :	\
     (dir) == 1 ? (BORDER_Y(w) + BORDER_H(w)) - BORDER_Y(dw) :	\
     (dir) == 2 ? BORDER_X(w) - (BORDER_X(dw) + BORDER_W(dw)) :	\
     (BORDER_X(w) + BORDER_W(w)) - BORDER_X(dw))

// up, down, left, right
#define DODGE_AMOUNT_BOX(box, dw, dir)				\
    ((dir) == 0 ? (box).y - (BORDER_Y(dw) + BORDER_H(dw)) :		\
     (dir) == 1 ? ((box).y + (box).height) - BORDER_Y(dw) :	\
     (dir) == 2 ? (box).x - (BORDER_X(dw) + BORDER_W(dw)) :		\
     ((box).x + (box).width) - BORDER_X(dw))

// ratio of perceived length of animation compared to real duration
// to make it appear to have the same speed with other animation effects

#define DREAM_PERCEIVED_T 0.6f
#define ROLLUP_PERCEIVED_T 0.6f


/*
 * Function prototypes
 *
 */

/* animation.c*/
 
void
modelInitObjects (Model * model,
		  int x, int y,
		  int width, int height);

void
postAnimationCleanup (CompWindow * w);

float
defaultAnimProgress (CompWindow *w);

float
sigmoidAnimProgress (CompWindow *w);

float
decelerateProgressCustom (float progress,
			  float minx, float maxx);

float
decelerateProgress (float progress);

void
applyTransformToObject (Object *obj, GLfloat *mat);

AnimDirection
getActualAnimDirection (CompWindow * w,
			AnimDirection dir,
			Bool openDir);

void
defaultAnimStep (CompWindow * w,
		 float time);

Bool
defaultAnimInit (CompWindow * w);

void
defaultUpdateWindowTransform (CompWindow *w,
			      CompTransform *wTransform);

Bool
animZoomToIcon (CompWindow *w);

void
animDrawWindowGeometry(CompWindow * w);

Bool
getMousePointerXY(CompScreen * s, short *x, short *y);

void
expandBoxWithBox (Box *target, Box *source);

void
expandBoxWithPoint (Box *target, float fx, float fy);

void
updateBBWindow (CompOutput *output,
		CompWindow * w,
		Box *BB);

void
updateBBScreen (CompOutput *output,
		CompWindow * w,
		Box *BB);

void
compTransformUpdateBB (CompOutput *output,
		       CompWindow *w,
		       Box *BB);

void
prepareTransform (CompScreen *s,
		  CompOutput *output,
		  CompTransform *resultTransform,
		  CompTransform *transform);

void
perspectiveDistortAndResetZ (CompScreen *s,
			     CompTransform *wTransform);

void
applyPerspectiveSkew (CompOutput *output,
		      CompTransform *transform,
		      Point *center);

inline void
applyTransform (CompTransform *wTransform,
		CompTransform *transform);

float
getProgressAndCenter (CompWindow *w,
		      Point *center);

/* curvedfold.c */

void
fxCurvedFoldModelStep (CompWindow *w,
		       float time);

void
fxFoldUpdateWindowAttrib (CompWindow * w,
			  WindowPaintAttrib * wAttrib);

Bool
fxCurvedFoldZoomToIcon (CompWindow *w);

/* dodge.c */

void
fxDodgePostPreparePaintScreen (CompWindow *w);

void
fxDodgeUpdateWindowTransform (CompWindow *w,
			      CompTransform *wTransform);

void
fxDodgeAnimStep (CompWindow *w,
		 float time);

void
fxDodgeUpdateBB (CompOutput *output,
		 CompWindow * w,
		 Box *BB);

/* dream.c */

Bool
fxDreamAnimInit (CompWindow * w);

void
fxDreamModelStep (CompWindow * w,
		  float time);

void
fxDreamUpdateWindowAttrib (CompWindow * w,
			   WindowPaintAttrib * wAttrib);

Bool
fxDreamZoomToIcon (CompWindow *w);

/* fade.c */

void
fxFadeUpdateWindowAttrib (CompWindow * w,
			  WindowPaintAttrib *wAttrib);


/* focusfade.c */

void
fxFocusFadeUpdateWindowAttrib (CompWindow * w,
			       WindowPaintAttrib *wAttrib);

/* glide.c */

Bool
fxGlideInit (CompWindow *w);

void
fxGlideUpdateWindowAttrib (CompWindow * w,
			   WindowPaintAttrib *wAttrib);

void
fxGlideAnimStep (CompWindow *w,
		 float time);

float
fxGlideAnimProgress (CompWindow *w);

void
fxGlideUpdateWindowTransform (CompWindow *w,
			      CompTransform *wTransform);

void
fxGlidePrePaintWindow (CompWindow * w);

void
fxGlidePostPaintWindow (CompWindow * w);

Bool
fxGlideZoomToIcon (CompWindow *w);

/* horizontalfold.c */

void
fxHorizontalFoldsModelStep (CompWindow *w,
			    float time);

void
fxHorizontalFoldsInitGrid (CompWindow *w,
			   int *gridWidth,
			   int *gridHeight);

Bool
fxHorizontalFoldsZoomToIcon (CompWindow *w);

/* magiclamp.c */

void
fxMagicLampInitGrid (CompWindow *w,
		     int *gridWidth, 
		     int *gridHeight);

void
fxVacuumInitGrid (CompWindow *w,
		  int *gridWidth, 
		  int *gridHeight);

Bool
fxMagicLampInit (CompWindow * w);

void
fxMagicLampModelStep (CompWindow * w,
		      float time);

/* options.c */

void
updateOptionSets (CompScreen *s,
		  AnimEvent e);

void
freeAllOptionSets (AnimScreen *as);

CompOptionValue *
animGetPluginOptVal (CompWindow *w,
		     ExtensionPluginInfo *pluginInfo,
		     int optionId);

OPTION_GETTERS_HDR

/* rollup.c */
 
void
fxRollUpModelStep (CompWindow *w,
		   float time);
 
void fxRollUpInitGrid (CompWindow *w,
		       int *gridWidth,
		       int *gridHeight);
 
Bool
fxRollUpAnimInit (CompWindow * w);

/* wave.c */
 
void
fxWaveModelStep (CompWindow * w,
		 float time);


/* zoomside.c */

void
fxZoomUpdateWindowAttrib (CompWindow * w,
			  WindowPaintAttrib *wAttrib);

void
fxZoomAnimProgress (CompWindow *w,
		    float *moveProgress,
		    float *scaleProgress,
		    Bool neverSpringy);

Bool
fxSidekickInit (CompWindow *w);

Bool
fxZoomInit (CompWindow * w);

void
applyZoomTransform (CompWindow * w);

void
getZoomCenterScale (CompWindow *w,
		    Point *pCurCenter, Point *pCurScale);

