#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <fusilli-core.h>

typedef struct _WaveParam {
	float halfWidth;
	float amp;
	float pos;
} WaveParam;

typedef enum {
	ZoomFromCenterOff = 0,
	ZoomFromCenterMin,
	ZoomFromCenterCreate,
	ZoomFromCenterOn
} ZoomFromCenter;

#define LAST_ZOOM_FROM_CENTER 3

//TODO remove #define RANDOM_EFFECT_OFFSET 2 /* skip None and Random */

typedef struct _RestackInfo {
	CompWindow *wRestacked, *wStart, *wEnd, *wOldAbove;
	Bool raised;
} RestackInfo;

extern int displayPrivateIndex;
extern int animFunctionsPrivateIndex;
extern int bananaIndex;

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

extern AnimEffect AnimEffectAirplane;
extern AnimEffect AnimEffectBeamUp;
extern AnimEffect AnimEffectBurn;
extern AnimEffect AnimEffectDomino;
extern AnimEffect AnimEffectExplode;
extern AnimEffect AnimEffectFold;
extern AnimEffect AnimEffectGlide3;
extern AnimEffect AnimEffectLeafSpread;
extern AnimEffect AnimEffectRazr;
extern AnimEffect AnimEffectSkewer;

#define NUM_EFFECTS 26

typedef struct _AnimDisplay
{
	int screenPrivateIndex;

	HandleEventProc handleEvent;
	HandleFusilliEventProc handleFusilliEvent;

	int activeWindow;

	CompMatch neverAnimateMatch;
} AnimDisplay;

typedef struct _PluginEventInfo
{
	char *pluginName;
	char *activateEventName;
} PluginEventInfo;

#define NUM_SWITCHERS 6
#define NUM_WATCHED_PLUGINS (NUM_SWITCHERS + 2)

// This must have the value of the first "effect setting" above
// in AnimScreenOptions
#define NUM_NONEFFECT_OPTIONS ANIM_SCREEN_OPTION_CURVED_FOLD_AMP_MULT

#define MAX_MATCHES 100

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

	CompOutput *output;
	CompOutput *animaddon_output;

	CompMatch open_match[MAX_MATCHES];
	int open_count;

	CompMatch close_match[MAX_MATCHES];
	int close_count;

	CompMatch minimize_match[MAX_MATCHES];
	int minimize_count;

	CompMatch shade_match[MAX_MATCHES];
	int shade_count;

	CompMatch focus_match[MAX_MATCHES];
	int focus_count;
} AnimScreen;

typedef struct _AnimWindow
{
	AnimWindowCommon com;
	AnimWindowEngineData eng;

	unsigned int state;
	unsigned int newState;

	Bool animInitialized;   // whether the animation effect (not the window) is initialized
	float remainderSteps;

	Bool nowShaded;
	Bool grabbed;

	int unmapCnt;
	int destroyCnt;

	Bool ignoreDamage;

	int curAnimSelectionRow;
	int prevAnimSelectionRow; // For the case when one event interrupts another

	Box BB;   // Bounding box for damage region calc. of CompTransform fx
	Box lastBB; // Last bounding box

	// for magic lamp
	Bool minimizeToTop;
	int magicLampWaveCount;
	WaveParam *magicLampWaves;

	// for glide effect
	float glideModRotAngle; // The angle of rotation modulo 360

	// for zoom
	float numZoomRotations;

	// for focus fade & dodge
	RestackInfo *restackInfo; // restack info if window was restacked this paint round
	CompWindow *winToBePaintedBeforeThis; // Window which should be painted before this
	CompWindow *winThisIsPaintedBefore; // the inverse relation of the above
	CompWindow *moreToBePaintedPrev; // doubly linked list for windows underneath that
	CompWindow *moreToBePaintedNext; //   raise together with this one
	Bool created;
	Bool configureNotified; // was configureNotified before restack check
	CompWindow *winPassingThrough; // win. passing through this one during focus effect

	// for dodge
	Bool isDodgeSubject;    // TRUE if this window is the cause of dodging
	Bool skipPostPrepareScreen;
	CompWindow *dodgeSubjectWin;// The window being dodged
	float dodgeMaxAmount;   /* max # pixels it should dodge
	                           (neg. values dodge left) */
	int dodgeOrder;         // dodge order (used temporarily)
	Bool dodgeDirection;    // 0: up, down, left, right

	// for burn
	int animFireDirection;

	// for polygon engine
	int nDrawGeometryCalls;
	Bool deceleratingMotion; // For effects that have decel. motion
	int nClipsPassed;       /* # of clips passed to animAddWindowGeometry so far
	                           in this draw step */
	Bool clipsUpdated;      // whether stored clips are updated in this anim step

	CompWindow *dodgeChainStart;// for the subject window
	CompWindow *dodgeChainPrev; // for dodging windows
	CompWindow *dodgeChainNext; // for dodging windows
	Bool walkerOverNewCopy; // whether walker is on the copy at the new pos.
	unsigned int walkerVisitCount; // how many times walker has visited this window
} AnimWindow;

#define GET_ANIM_DISPLAY(d)                                             \
	((AnimDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define ANIM_DISPLAY(d)                         \
	AnimDisplay *ad = GET_ANIM_DISPLAY (d)

#define GET_ANIM_SCREEN(s, ad)                                          \
	((AnimScreen *) (s)->privates[(ad)->screenPrivateIndex].ptr)

#define ANIM_SCREEN(s)                                                  \
	AnimScreen *as = GET_ANIM_SCREEN (s, GET_ANIM_DISPLAY (&display))

#define GET_ANIM_WINDOW(w, as)                                          \
	((AnimWindow *) (w)->privates[(as)->windowPrivateIndex].ptr)

#define ANIM_WINDOW(w)                                       \
	AnimWindow *aw = GET_ANIM_WINDOW (w,                     \
	                                  GET_ANIM_SCREEN (w->screen,             \
	                                  GET_ANIM_DISPLAY (&display)))

// up, down, left, right
#define DODGE_AMOUNT(w, dw, dir)                        \
	((dir) == 0 ? BORDER_Y (w) - (BORDER_Y (dw) + BORDER_H (dw)) : \
	 (dir) == 1 ? (BORDER_Y (w) + BORDER_H (w)) - BORDER_Y (dw) :  \
	 (dir) == 2 ? BORDER_X (w) - (BORDER_X (dw) + BORDER_W (dw)) : \
	 (BORDER_X (w) + BORDER_W (w)) - BORDER_X (dw))

// up, down, left, right
#define DODGE_AMOUNT_BOX(box, dw, dir)                          \
	((dir) == 0 ? (box).y - (BORDER_Y (dw) + BORDER_H (dw)) :             \
	 (dir) == 1 ? ((box).y + (box).height) - BORDER_Y (dw) :     \
	 (dir) == 2 ? (box).x - (BORDER_X (dw) + BORDER_W (dw)) :             \
	 ((box).x + (box).width) - BORDER_X (dw))

// ratio of perceived length of animation compared to real duration
// to make it appear to have the same speed with other animation effects

#define DREAM_PERCEIVED_T 0.6f
#define ROLLUP_PERCEIVED_T 0.6f
#define DOMINO_PERCEIVED_T 0.8f
#define EXPLODE_PERCEIVED_T 0.7f
#define FOLD_PERCEIVED_T 0.55f
#define LEAFSPREAD_PERCEIVED_T 0.6f
#define SKEWER_PERCEIVED_T 0.6f

typedef struct _AirplaneEffectParameters
{
	/// added for airplane folding and flying
	// airplane fold phase.

	Vector3d rotAxisA;              // Rotation axis vector A
	Vector3d rotAxisB;              // Rotation axis vector B

	Point3d rotAxisOffsetA;         // Rotation axis translate amount A
	Point3d rotAxisOffsetB;         // Rotation axis translate amount B

	float rotAngleA;                // Rotation angle A
	float finalRotAngA;             // Final rotation angle A

	float rotAngleB;                // Rotation angle B
	float finalRotAngB;             // Final rotation angle B

	// airplane fly phase:

	Vector3d centerPosFly;  // center position (offset) during the flying phases

	Vector3d flyRotation;   // airplane rotation during the flying phases
	Vector3d flyFinalRotation; // airplane rotation during the flying phases

	float flyScale;         // Scale for airplane flying effect
	float flyFinalScale;    // Final Scale for airplane flying effect

	float flyTheta;         // Theta parameter for fly rotations and positions

	float moveStartTime2;           // Movement starts at this time ([0-1] range)
	float moveDuration2;            // Movement lasts this long     ([0-1] range)

	float moveStartTime3;           // Movement starts at this time ([0-1] range)
	float moveDuration3;            // Movement lasts this long     ([0-1] range)

	float moveStartTime4;           // Movement starts at this time ([0-1] range)
	float moveDuration4;            // Movement lasts this long     ([0-1] range)

	float moveStartTime5;           // Movement starts at this time ([0-1] range)
	float moveDuration5;            // Movement lasts this long     ([0-1] range)
} AirplaneEffectParameters;

/*
 * Function prototypes
 *
 */

/* animation.c */

void
modelInitObjects (Model *model,
                  int   x,
                  int   y,
                  int   width,
                  int   height);

void
postAnimationCleanup (CompWindow *w);

float
defaultAnimProgress (CompWindow *w);

float
sigmoidAnimProgress (CompWindow *w);

float
decelerateProgressCustom (float progress,
                          float minx,
                          float maxx);

float
decelerateProgress (float progress);

void
applyTransformToObject (Object *obj,
                        GLfloat *mat);

AnimDirection
getActualAnimDirection (CompWindow    *w,
                        AnimDirection dir,
                        Bool          openDir);

void
defaultAnimStep (CompWindow *w,
                 float      time);

Bool
defaultAnimInit (CompWindow * w);

void
defaultUpdateWindowTransform (CompWindow    *w,
                              CompTransform *wTransform);

Bool
animZoomToIcon (CompWindow *w);

void
animDrawWindowGeometry (CompWindow * w);

Bool
getMousePointerXY (CompScreen *s,
                   short      *x,
                   short      *y);

void
expandBoxWithBox (Box *target,
                  Box *source);

void
expandBoxWithPoint (Box   *target,
                    float fx,
                    float fy);

void
updateBBWindow (CompOutput *output,
                CompWindow *w,
                Box        *BB);

void
updateBBScreen (CompOutput *output,
                CompWindow *w,
                Box        *BB);

void
compTransformUpdateBB (CompOutput *output,
                       CompWindow *w,
                       Box        *BB);

void
prepareTransform (CompScreen    *s,
                  CompOutput    *output,
                  CompTransform *resultTransform,
                  CompTransform *transform);

void
perspectiveDistortAndResetZ (CompScreen    *s,
                             CompTransform *wTransform);

void
applyPerspectiveSkew (CompOutput    *output,
                      CompTransform *transform,
                      Point         *center);

inline void
applyTransform (CompTransform *wTransform,
                CompTransform *transform);

float
getProgressAndCenter (CompWindow *w,
                      Point      *center);

/* curvedfold.c */

void
fxCurvedFoldModelStep (CompWindow *w,
                       float      time);

void
fxFoldUpdateWindowAttrib (CompWindow        *w,
                          WindowPaintAttrib *wAttrib);

Bool
fxCurvedFoldZoomToIcon (CompWindow *w);

/* dodge.c */

void
fxDodgePostPreparePaintScreen (CompWindow *w);

void
fxDodgeUpdateWindowTransform (CompWindow    *w,
                              CompTransform *wTransform);

void
fxDodgeAnimStep (CompWindow *w,
                 float      time);

void
fxDodgeUpdateBB (CompOutput *output,
                 CompWindow *w,
                 Box        *BB);

/* dream.c */

Bool
fxDreamAnimInit (CompWindow *w);

void
fxDreamModelStep (CompWindow *w,
                  float      time);

void
fxDreamUpdateWindowAttrib (CompWindow        *w,
                           WindowPaintAttrib *wAttrib);

Bool
fxDreamZoomToIcon (CompWindow *w);

/* fade.c */

void
fxFadeUpdateWindowAttrib (CompWindow        *w,
                          WindowPaintAttrib *wAttrib);


/* focusfade.c */

void
fxFocusFadeUpdateWindowAttrib (CompWindow        *w,
                               WindowPaintAttrib *wAttrib);

/* glide.c */

Bool
fxGlideInit (CompWindow *w);

void
fxGlideUpdateWindowAttrib (CompWindow        *w,
                           WindowPaintAttrib *wAttrib);

void
fxGlideAnimStep (CompWindow *w,
                 float      time);

float
fxGlideAnimProgress (CompWindow *w);

void
fxGlideUpdateWindowTransform (CompWindow    *w,
                              CompTransform *wTransform);

void
fxGlidePrePaintWindow (CompWindow *w);

void
fxGlidePostPaintWindow (CompWindow *w);

Bool
fxGlideZoomToIcon (CompWindow *w);

/* horizontalfold.c */

void
fxHorizontalFoldsModelStep (CompWindow *w,
                            float      time);

void
fxHorizontalFoldsInitGrid (CompWindow *w,
                           int        *gridWidth,
                           int        *gridHeight);

Bool
fxHorizontalFoldsZoomToIcon (CompWindow *w);

/* magiclamp.c */

void
fxMagicLampInitGrid (CompWindow *w,
                     int        *gridWidth,
                     int        *gridHeight);

void
fxVacuumInitGrid (CompWindow *w,
                  int        *gridWidth,
                  int        *gridHeight);

Bool
fxMagicLampInit (CompWindow *w);

void
fxMagicLampModelStep (CompWindow *w,
                      float      time);

/* rollup.c */

void
fxRollUpModelStep (CompWindow *w,
                   float      time);

void fxRollUpInitGrid (CompWindow *w,
                       int        *gridWidth,
                       int        *gridHeight);

Bool
fxRollUpAnimInit (CompWindow *w);

/* wave.c */

void
fxWaveModelStep (CompWindow *w,
                 float      time);


/* zoomside.c */

void
fxZoomUpdateWindowAttrib (CompWindow        *w,
                          WindowPaintAttrib *wAttrib);

void
fxZoomAnimProgress (CompWindow *w,
                    float      *moveProgress,
                    float      *scaleProgress,
                    Bool       neverSpringy);

Bool
fxSidekickInit (CompWindow *w);

Bool
fxZoomInit (CompWindow *w);

void
applyZoomTransform (CompWindow *w);

void
getZoomCenterScale (CompWindow *w,
                    Point      *pCurCenter,
                    Point      *pCurScale);

/* airplane3d.c */

Bool
fxAirplaneInit (CompWindow *w);

void
fxAirplaneAnimStep (CompWindow *w,
                    float      time);

void
fxAirplaneLinearAnimStepPolygon (CompWindow    *w,
                                 PolygonObject *p,
                                 float         forwardProgress);

void
fxAirplaneDrawCustomGeometry (CompWindow *w);

void
AirplaneExtraPolygonTransformFunc (PolygonObject *p);


/* beamup.c */

void
fxBeamupUpdateWindowAttrib (CompWindow        *w,
                            WindowPaintAttrib *wAttrib);

void
fxBeamUpAnimStep (CompWindow *w,
                  float      time);

Bool
fxBeamUpInit (CompWindow *w);


/* burn.c */

void
fxBurnAnimStep (CompWindow *w,
                float      time);

Bool
fxBurnInit (CompWindow *w);

/* domino.c */

Bool
fxDominoInit (CompWindow *w);

/* explode3d.c */

Bool
fxExplodeInit (CompWindow *w);

/* fold3d.c */

Bool
fxFoldInit (CompWindow *w);

void
fxFoldAnimStepPolygon (CompWindow    *w,
                       PolygonObject *p,
                       float         forwardProgress);

/* glide3.c */

Bool
fxGlide3Init (CompWindow *w);

/* leafspread.c */

Bool
fxLeafSpreadInit (CompWindow *w);

/* particle.c */

void
initParticles (int            numParticles,
               ParticleSystem *ps);

void
drawParticles (CompWindow     *w,
               ParticleSystem *ps);

void
updateParticles (ParticleSystem *ps,
                 float          time);

void
finiParticles (ParticleSystem *ps);

void
drawParticleSystems (CompWindow *w);

void
particlesUpdateBB (CompOutput *output,
                   CompWindow *w,
                   Box        *BB);

void
particlesCleanup (CompWindow *w);

Bool
particlesPrePrepPaintScreen (CompWindow *w,
                             int        msSinceLastPaint);

/* polygon.c */

Bool
tessellateIntoRectangles (CompWindow *w,
                          int        gridSizeX,
                          int        gridSizeY,
                          float      thickness);

Bool
tessellateIntoHexagons (CompWindow *w,
                        int        gridSizeX,
                        int        gridSizeY,
                        float      thickness);

Bool
tessellateIntoGlass (CompWindow *w,
                     int        spoke_num,
                     int        tier_num,
                     float      thickness);

void
polygonsStoreClips (CompWindow *w,
                    int        nClip,
                    BoxPtr     pClip,
                    int        nMatrix,
                    CompMatrix *matrix);

void
polygonsDrawCustomGeometry (CompWindow *w);

void
polygonsPrePaintWindow (CompWindow *w);

void
polygonsPostPaintWindow (CompWindow *w);

void
freePolygonSet (AnimWindow *aw);

void
freePolygonObjects (PolygonSet *pset);

void
polygonsLinearAnimStepPolygon (CompWindow    *w,
                               PolygonObject *p,
                               float         forwardProgress);

void
polygonsDeceleratingAnimStepPolygon (CompWindow    *w,
                                     PolygonObject *p,
                                     float         forwardProgress);

void
polygonsUpdateBB (CompOutput *output,
                  CompWindow *w,
                  Box        *BB);

void
polygonsAnimStep (CompWindow *w,
                  float      time);

Bool
polygonsPrePreparePaintScreen (CompWindow *w,
                               int        msSinceLastPaint);

void
polygonsCleanup (CompWindow *w);

Bool
polygonsAnimInit (CompWindow *w);

void
polygonsPrePaintOutput (CompScreen *s,
                        CompOutput *output);

void
polygonsRefresh (CompWindow *w,
                 Bool       animInitialized);

/* skewer.c */

Bool
fxSkewerInit (CompWindow *w);

void
fxSkewerAnimStepPolygon (CompWindow    *w,
                         PolygonObject *p,
                         float         forwardProgress);

