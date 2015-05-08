#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <compiz-core.h>
#include <compiz-animation.h>
#include "compiz-animationaddon.h"

extern int animDisplayPrivateIndex;
extern CompMetadata animMetadata;


typedef struct _AirplaneEffectParameters
{
    /// added for airplane folding and flying
    // airplane fold phase.

    Vector3d rotAxisA;			// Rotation axis vector A
    Vector3d rotAxisB;			// Rotation axis vector B

    Point3d rotAxisOffsetA;		// Rotation axis translate amount A 
    Point3d rotAxisOffsetB; 	        // Rotation axis translate amount B

    float rotAngleA;			// Rotation angle A
    float finalRotAngA;			// Final rotation angle A

    float rotAngleB;			// Rotation angle B
    float finalRotAngB;			// Final rotation angle B

    // airplane fly phase:

    Vector3d centerPosFly;	// center position (offset) during the flying phases

    Vector3d flyRotation;	// airplane rotation during the flying phases
    Vector3d flyFinalRotation;	// airplane rotation during the flying phases 

    float flyScale;             // Scale for airplane flying effect 
    float flyFinalScale;        // Final Scale for airplane flying effect 
  
    float flyTheta;		// Theta parameter for fly rotations and positions

    float moveStartTime2;		// Movement starts at this time ([0-1] range)
    float moveDuration2;		// Movement lasts this long     ([0-1] range)

    float moveStartTime3;		// Movement starts at this time ([0-1] range)
    float moveDuration3;		// Movement lasts this long     ([0-1] range)

    float moveStartTime4;		// Movement starts at this time ([0-1] range)
    float moveDuration4;		// Movement lasts this long     ([0-1] range)

    float moveStartTime5;	        // Movement starts at this time ([0-1] range)
    float moveDuration5;		// Movement lasts this long     ([0-1] range)
} AirplaneEffectParameters;

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

#define NUM_EFFECTS 10

typedef enum
{
    // Misc. settings
    ANIMADDON_SCREEN_OPTION_TIME_STEP_INTENSE = 0,
    // Effect settings
    ANIMADDON_SCREEN_OPTION_AIRPLANE_PATHLENGTH,
    ANIMADDON_SCREEN_OPTION_AIRPLANE_FLY2TOM,
    ANIMADDON_SCREEN_OPTION_BEAMUP_SIZE,
    ANIMADDON_SCREEN_OPTION_BEAMUP_SPACING,
    ANIMADDON_SCREEN_OPTION_BEAMUP_COLOR,
    ANIMADDON_SCREEN_OPTION_BEAMUP_SLOWDOWN,
    ANIMADDON_SCREEN_OPTION_BEAMUP_LIFE,
    ANIMADDON_SCREEN_OPTION_DOMINO_DIRECTION,
    ANIMADDON_SCREEN_OPTION_RAZR_DIRECTION,
    ANIMADDON_SCREEN_OPTION_EXPLODE_THICKNESS,
    ANIMADDON_SCREEN_OPTION_EXPLODE_GRIDSIZE_X,
    ANIMADDON_SCREEN_OPTION_EXPLODE_GRIDSIZE_Y,
    ANIMADDON_SCREEN_OPTION_EXPLODE_TIERS,
    ANIMADDON_SCREEN_OPTION_EXPLODE_SPOKES,
    ANIMADDON_SCREEN_OPTION_EXPLODE_TESS,
    ANIMADDON_SCREEN_OPTION_FIRE_PARTICLES,
    ANIMADDON_SCREEN_OPTION_FIRE_SIZE,
    ANIMADDON_SCREEN_OPTION_FIRE_SLOWDOWN,
    ANIMADDON_SCREEN_OPTION_FIRE_LIFE,
    ANIMADDON_SCREEN_OPTION_FIRE_COLOR,
    ANIMADDON_SCREEN_OPTION_FIRE_DIRECTION,
    ANIMADDON_SCREEN_OPTION_FIRE_CONSTANT_SPEED,
    ANIMADDON_SCREEN_OPTION_FIRE_SMOKE,
    ANIMADDON_SCREEN_OPTION_FIRE_MYSTICAL,
    ANIMADDON_SCREEN_OPTION_FOLD_GRIDSIZE_X,
    ANIMADDON_SCREEN_OPTION_FOLD_GRIDSIZE_Y,
    ANIMADDON_SCREEN_OPTION_FOLD_DIR,
    ANIMADDON_SCREEN_OPTION_GLIDE3_AWAY_POS,
    ANIMADDON_SCREEN_OPTION_GLIDE3_AWAY_ANGLE,
    ANIMADDON_SCREEN_OPTION_GLIDE3_THICKNESS,
    ANIMADDON_SCREEN_OPTION_SKEWER_GRIDSIZE_X,
    ANIMADDON_SCREEN_OPTION_SKEWER_GRIDSIZE_Y,
    ANIMADDON_SCREEN_OPTION_SKEWER_THICKNESS,
    ANIMADDON_SCREEN_OPTION_SKEWER_DIRECTION,
    ANIMADDON_SCREEN_OPTION_SKEWER_TESS,
    ANIMADDON_SCREEN_OPTION_SKEWER_ROTATION,

    ANIMADDON_SCREEN_OPTION_NUM
} AnimAddonScreenOptions;

// This must have the value of the first "effect setting" above
// in AnimAddonScreenOptions
#define NUM_NONEFFECT_OPTIONS ANIMADDON_SCREEN_OPTION_AIRPLANE_PATHLENGTH

typedef enum _AnimAddonDisplayOptions
{
    ANIMADDON_DISPLAY_OPTION_ABI = 0,
    ANIMADDON_DISPLAY_OPTION_INDEX,
    ANIMADDON_DISPLAY_OPTION_NUM
} AnimAddonDisplayOptions;

typedef struct _AnimAddonDisplay
{
    int screenPrivateIndex;
    AnimBaseFunctions *animBaseFunctions;

    CompOption opt[ANIMADDON_DISPLAY_OPTION_NUM];
} AnimAddonDisplay;

typedef struct _AnimAddonScreen
{
    int windowPrivateIndex;

    CompOutput *output;

    CompOption opt[ANIMADDON_SCREEN_OPTION_NUM];
} AnimAddonScreen;

typedef struct _AnimAddonWindow
{
    AnimWindowCommon *com;
    AnimWindowEngineData eng;

    // for burn
    int animFireDirection;

    // for polygon engine
    int nDrawGeometryCalls;
    Bool deceleratingMotion;	// For effects that have decel. motion
    int nClipsPassed;	        /* # of clips passed to animAddWindowGeometry so far
				   in this draw step */
    Bool clipsUpdated;          // whether stored clips are updated in this anim step
} AnimAddonWindow;

#define GET_ANIMADDON_DISPLAY(d)						\
    ((AnimAddonDisplay *) (d)->base.privates[animDisplayPrivateIndex].ptr)

#define ANIMADDON_DISPLAY(d)				\
    AnimAddonDisplay *ad = GET_ANIMADDON_DISPLAY (d)

#define GET_ANIMADDON_SCREEN(s, ad)						\
    ((AnimAddonScreen *) (s)->base.privates[(ad)->screenPrivateIndex].ptr)

#define ANIMADDON_SCREEN(s)							\
    AnimAddonScreen *as = GET_ANIMADDON_SCREEN (s, GET_ANIMADDON_DISPLAY (s->display))

#define GET_ANIMADDON_WINDOW(w, as)						\
    ((AnimAddonWindow *) (w)->base.privates[(as)->windowPrivateIndex].ptr)

#define ANIMADDON_WINDOW(w)					     \
    AnimAddonWindow *aw = GET_ANIMADDON_WINDOW (w,                     \
		     GET_ANIMADDON_SCREEN (w->screen,             \
		     GET_ANIMADDON_DISPLAY (w->screen->display)))

// ratio of perceived length of animation compared to real duration
// to make it appear to have the same speed with other animation effects

#define DOMINO_PERCEIVED_T 0.8f
#define EXPLODE_PERCEIVED_T 0.7f
#define FOLD_PERCEIVED_T 0.55f
#define LEAFSPREAD_PERCEIVED_T 0.6f
#define SKEWER_PERCEIVED_T 0.6f

/*
 * Function prototypes
 *
 */

OPTION_GETTERS_HDR

int
getIntenseTimeStep (CompScreen *s);


/* airplane3d.c */

Bool
fxAirplaneInit (CompWindow *w);

void
fxAirplaneAnimStep (CompWindow *w,
		      float time);

void
fxAirplaneLinearAnimStepPolygon (CompWindow *w,
				   PolygonObject *p,
				   float forwardProgress);

void 
fxAirplaneDrawCustomGeometry (CompWindow *w);

void 
AirplaneExtraPolygonTransformFunc (PolygonObject * p);


/* beamup.c */

void
fxBeamupUpdateWindowAttrib (CompWindow *w,
			    WindowPaintAttrib *wAttrib);

void
fxBeamUpAnimStep (CompWindow *w,
		  float time);

Bool
fxBeamUpInit (CompWindow *w);


/* burn.c */

void
fxBurnAnimStep (CompWindow *w,
		float time);

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
fxFoldAnimStepPolygon (CompWindow *w,
			 PolygonObject *p,
			 float forwardProgress);

/* glide3.c */

Bool
fxGlide3Init (CompWindow * w);

/* leafspread.c */

Bool
fxLeafSpreadInit (CompWindow *w);

/* particle.c */

void
initParticles (int numParticles,
	       ParticleSystem * ps);

void
drawParticles (CompWindow * w,
	       ParticleSystem * ps);

void
updateParticles (ParticleSystem * ps,
		 float time);

void
finiParticles (ParticleSystem * ps);

void
drawParticleSystems (CompWindow *w);

void
particlesUpdateBB (CompOutput *output,
		   CompWindow * w,
		   Box *BB);

void
particlesCleanup (CompWindow * w);

Bool
particlesPrePrepPaintScreen (CompWindow * w,
			     int msSinceLastPaint);

/* polygon.c */

Bool
tessellateIntoRectangles (CompWindow * w,
			  int gridSizeX,
			  int gridSizeY,
			  float thickness);
 
Bool
tessellateIntoHexagons (CompWindow * w,
			int gridSizeX,
			int gridSizeY,
			float thickness);

Bool
tessellateIntoGlass (CompWindow * w, 
		     int spoke_num,
		     int tier_num,
		     float thickness);

void
polygonsStoreClips (CompWindow * w,
		    int nClip, BoxPtr pClip,
		    int nMatrix, CompMatrix * matrix);
 
void
polygonsDrawCustomGeometry (CompWindow * w);

void
polygonsPrePaintWindow (CompWindow * w);
 
void
polygonsPostPaintWindow (CompWindow * w);

void
freePolygonSet (AnimAddonWindow * aw);

void
freePolygonObjects (PolygonSet * pset);
 
void
polygonsLinearAnimStepPolygon (CompWindow * w,
			       PolygonObject *p,
			       float forwardProgress);

void
polygonsDeceleratingAnimStepPolygon (CompWindow * w,
				     PolygonObject *p,
				     float forwardProgress);

void
polygonsUpdateBB (CompOutput *output,
		  CompWindow * w,
		  Box *BB);

void
polygonsAnimStep (CompWindow *w,
		  float time);

Bool
polygonsPrePreparePaintScreen (CompWindow *w,
			       int msSinceLastPaint);

void
polygonsCleanup (CompWindow *w);

Bool
polygonsAnimInit (CompWindow *w);

void
polygonsPrePaintOutput (CompScreen *s, CompOutput *output);

void
polygonsRefresh (CompWindow *w,
		 Bool animInitialized);

/* skewer.c */

Bool
fxSkewerInit (CompWindow * w);

void
fxSkewerAnimStepPolygon (CompWindow *w,
			 PolygonObject *p,
			 float forwardProgress);

