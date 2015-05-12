#ifndef _FUSILLI_ANIMATION_H
#define _FUSILLI_ANIMATION_H

typedef enum
{
	WindowEventOpen = 0,
	WindowEventClose,
	WindowEventMinimize,
	WindowEventUnminimize,
	WindowEventShade,
	WindowEventUnshade,
	WindowEventFocus,
	WindowEventNum,
	WindowEventNone
} WindowEvent;

typedef enum
{
	AnimEventOpen = 0,
	AnimEventClose,
	AnimEventMinimize,
	AnimEventShade,
	AnimEventFocus,
	AnimEventNum
} AnimEvent;

typedef struct _AnimEffectProperties
{
	void (*updateWindowAttribFunc) (CompWindow        *,
	                                WindowPaintAttrib *);
	void (*prePaintWindowFunc) (CompWindow *);
	void (*postPaintWindowFunc) (CompWindow *);
	void (*animStepFunc) (CompWindow *,
	                      float      time);
	Bool (*initFunc) (CompWindow *);
	void (*initGridFunc) (CompWindow *,
	                      int *,
	                      int *);
	void (*addCustomGeometryFunc) (CompWindow *,
	                               int, Box *,
	                               int, CompMatrix *);
	void (*drawCustomGeometryFunc) (CompWindow *);
	Bool (*letOthersDrawGeomsFunc) (CompWindow *);
	void (*updateWinTransformFunc) (CompWindow *,
	                                CompTransform *);
	Bool (*prePrepPaintScreenFunc) (CompWindow *, int msSinceLastPaint);
	void (*postPrepPaintScreenFunc) (CompWindow *);
	void (*updateBBFunc) (CompOutput *, CompWindow *, Box *);
	void (*cleanupFunc) (CompWindow *);
	void (*refreshFunc) (CompWindow *, Bool animInitialized);
	Bool (*zoomToIconFunc) (CompWindow *);
	Bool modelAnimIs3D;     // TRUE if anim uses model and 3d coords
	Bool useQTexCoord;      // TRUE if effect needs Q texture coordinates
	void *extraProperties;
} AnimEffectProperties;

typedef struct _AnimEffectInfo {
	char *name; // name of the animation effect, e.g. "animationpack:Implode"

	// to be set to TRUE for the window event animation list(s) that
	// the new animation (value) should be added to
	// (0: open, 1: close, 2: minimize, 3: shade, 4: focus)
	Bool usedForEvents[AnimEventNum];

	AnimEffectProperties properties;
} AnimEffectInfo;

typedef const AnimEffectInfo * AnimEffect;

typedef struct _xy_pair {
	float x, y;
} Point, Vector;

typedef struct {
	float x1, x2, y1, y2;
} Boxf;

typedef struct _xyz_tuple {
	float x, y, z;
} Point3d, Vector3d;

typedef struct _Object {
	Point gridPosition;     // position on window in [0,1] range
	Point3d position;       // position on screen

	// Texture x, y coordinates will be offset by given amounts
	// for quads that fall after and before this object in x and y directions.
	// Currently only y offset can be used.
	Point offsetTexCoordForQuadBefore;
	Point offsetTexCoordForQuadAfter;
} Object;

typedef struct _Model {
	Object *objects;
	int numObjects;
	int gridWidth;
	int gridHeight;

	int winWidth;                // keeps win. size when model was created
	int winHeight;

	Vector scale;
	Point scaleOrigin;

	WindowEvent forWindowEvent;
	float topHeight;
	float bottomHeight;
} Model;

// Window properties common to multiple animation effects
typedef struct _AnimWindowCommon {
	float animTotalTime;
	float animRemainingTime;
	float timestep;              // to be used in updateWindowAttribFunc

	int animOverrideProgressDir; // 0: default dir, 1: forward, 2: backward

	WindowEvent curWindowEvent;
	AnimEffect curAnimEffect;

	FragmentAttrib curPaintAttrib;

	Region drawRegion;
	Bool useDrawRegion;

	XRectangle icon;

	GLushort storedOpacity;

	CompTransform transform;
	Bool usingTransform;     // whether transform matrix is used for the current effect

	float transformStartProgress;
	float transformProgress;

	Model *model;   // for grid engine
} AnimWindowCommon;

typedef enum {
	AnimDirectionDown = 0,
	AnimDirectionUp,
	AnimDirectionLeft,
	AnimDirectionRight,
	AnimDirectionRandom,
	AnimDirectionAuto
} AnimDirection;
#define LAST_ANIM_DIRECTION 5

typedef void
(*UpdateBBProc) (CompOutput *output,
                 CompWindow *w,
                 Box        *BB);

// Base functions for extension plugins to call
typedef struct _AnimBaseFunctions {
	Bool (*getMousePointerXY) (CompScreen *s, short *x, short *y);
	UpdateBBProc    updateBBScreen;
	UpdateBBProc    updateBBWindow;
	UpdateBBProc    modelUpdateBB;
	UpdateBBProc    compTransformUpdateBB;
	Bool (*defaultAnimInit) (CompWindow * w);
	void (*defaultAnimStep) (CompWindow * w, float time);
	void (*defaultUpdateWindowTransform) (CompWindow *w,
	                                      CompTransform *wTransform);
	float (*getProgressAndCenter) (CompWindow *w,
	                               Point *center);

	float (*defaultAnimProgress) (CompWindow *w);
	float (*sigmoidAnimProgress) (CompWindow *w);
	float (*decelerateProgressCustom) (float progress,
	                                   float minx,
	                                   float maxx);
	float (*decelerateProgress) (float progress);
	AnimDirection (*getActualAnimDirection) (CompWindow * w,
	                                         AnimDirection dir,
	                                         Bool openDir);
	void (*expandBoxWithBox) (Box *target, Box *source);
	void (*expandBoxWithPoint) (Box *target, float fx, float fy);
	void (*prepareTransform) (CompScreen *s,
	                          CompOutput *output,
	                          CompTransform *resultTransform,
	                          CompTransform *transform);
	AnimWindowCommon * (*getAnimWindowCommon) (CompWindow *w);
	Bool (*returnTrue) (CompWindow *w);
	void (*postAnimationCleanup) (CompWindow *w);
	void (*fxZoomUpdateWindowAttrib) (CompWindow * w,
	                                  WindowPaintAttrib * wAttrib);
} AnimBaseFunctions;

extern AnimBaseFunctions animBaseFunctions;

#define WIN_X(w) ((w)->attrib.x - (w)->output.left)
#define WIN_Y(w) ((w)->attrib.y - (w)->output.top)
#define WIN_W(w) ((w)->width + (w)->output.left + (w)->output.right)
#define WIN_H(w) ((w)->height + (w)->output.top + (w)->output.bottom)

#define BORDER_X(w) ((w)->attrib.x - (w)->input.left)
#define BORDER_Y(w) ((w)->attrib.y - (w)->input.top)
#define BORDER_W(w) ((w)->width + (w)->input.left + (w)->input.right)
#define BORDER_H(w) ((w)->height + (w)->input.top + (w)->input.bottom)

#define RAND_FLOAT() ((float)rand() / RAND_MAX)

#define sigmoid(fx) (1.0f/(1.0f+exp(-5.0f*2*((fx)-0.5))))
#define sigmoid2(fx, s) (1.0f/(1.0f+exp(-(s)*2*((fx)-0.5))))

// ratio of perceived length of animation compared to real duration
// to make it appear to have the same speed with other animation effects

#define SPRINGY_ZOOM_PERCEIVED_T 0.55f
#define NONSPRINGY_ZOOM_PERCEIVED_T 0.6f
#define ZOOM_PERCEIVED_T 0.75f


// the following used to be part of compiz-animationaddon.h

// Polygon tesselation type: Rectangular, Hexagonal
typedef enum
{
	PolygonTessRect = 0,
	PolygonTessHex,
	PolygonTessGlass
} PolygonTess;
#define LAST_POLYGON_TESS 2

// This is intended to be a closed 3D piece of a window with convex polygon
// faces and quad-strip sides. Since decoration texture is separate from
// the window texture, it is more complicated than it would be with a single
// texture: we use glClipPlane with the rectangles (clips) to clip 3D space
// to the region falling within that clip.
// If the polygon is on an edge/corner, it also has 2D shadow quad(s)
// (to be faded out at the beginning of 3D animation if necessary).
typedef struct _PolygonObject
{
	int nVertices;                          // number of total vertices (front + back)
	int nSides;                             // number of sides
	GLfloat *vertices;              // Positions of vertices relative to center
	GLushort *sideIndices;          // Indices of quad strip for "sides"
	GLfloat *normals;               // Surface normals for 2+nSides faces

	Box boundingBox;                // Bound. box to test intersection with clips

	// Animation effect parameters

	Point3d centerPosStart;         // Starting position of center
	float rotAngleStart;            // Starting rotation angle

	Point3d centerPos;              // Position of center
	Vector3d rotAxis;               // Rotation axis vector
	float rotAngle;                         // Rotation angle
	Point3d rotAxisOffset;          // Rotation axis translate amount

	Point centerRelPos;             // Relative pos of center within the window

	Vector3d finalRelPos;           // Velocity factor for scripted movement
	float finalRotAng;              // Final rotation angle around rotAxis

	float moveStartTime;            // Movement starts at this time ([0-1] range)
	float moveDuration;             // Movement lasts this long     ([0-1] range)

	float fadeStartTime;            // Fade out starts at this time ([0,1] range)
	float fadeDuration;             // Duration of fade out         ([0,1] range)

	void *effectParameters;         /* Pointer to a struct that can contain
	                                   custom parameters for an individual effect */

	float boundSphereRadius;        // Radius of bounding sphere
} PolygonObject;

typedef struct _Clip4Polygons   // Rectangular clips
{                               // (to hold clips passed to AddWindowGeometry)
	int id;                 // clip id (what number this clip is among
	// passed clips)
	Box box;                // Coords
	Boxf boxf;              // Float coords (for small clipping adjustment)
	CompMatrix texMatrix;   // Corresponding texture coord. matrix
	int *intersectingPolygons;
	int nIntersectingPolygons; // Clips (in PolygonSet) that intersect
	GLfloat *polygonVertexTexCoords;
	// Tex coords for each intersecting polygon and for each vertex
	// ordered as p1.v1.x, p1.v1.y, p1.v2.x, p1.v2.y, p2.v1.x, p2.v1.y, ...
} Clip4Polygons;

typedef enum
{
	CorrectPerspectiveNone = 0,
	CorrectPerspectivePolygon,
	CorrectPerspectiveWindow
} CorrectPerspective;

typedef struct _PolygonSet      // Polygon objects with same thickness
{
	int nClips;             // Rect. clips collected in AddWindowGeometries
	Clip4Polygons *clips;   // List of clips
	int clipCapacity;       // # of clips this list can hold
	int firstNondrawnClip;
	int *lastClipInGroup;   // index of the last clip in each group of clips
	// drawn in drawGeometry func.

	Bool doDepthTest;       // whether depth testing should be used in the effect
	Bool doLighting;        // whether lighting should be used in the effect
	CorrectPerspective correctPerspective;
	PolygonObject *polygons; // The polygons in this set
	int nPolygons;
	float thickness;        // Window thickness (depth along z axis)
	int nTotalFrontVertices; // Total # of polygon vertices on front faces
	float backAndSidesFadeDur; // How long side and back faces should fade in/out
	float allFadeDuration;  /* Duration of fade out at the end in [0,1] range
	                           when all polygons fade out at the same time.
	                           If >-1, this overrides fadeDuration in PolygonObject */

	Bool includeShadows;    // include shadows in polygon

	void (*extraPolygonTransformFunc)(PolygonObject *);
} PolygonSet;

typedef struct _Particle {
	float life;             // particle life
	float fade;             // fade speed
	float width;            // particle width
	float height;           // particle height
	float w_mod;            // particle size modification during life
	float h_mod;            // particle size modification during life
	float r;                // red value
	float g;                // green value
	float b;                // blue value
	float a;                // alpha value
	float x;                // X position
	float y;                // Y position
	float z;                // Z position
	float xi;               // X direction
	float yi;               // Y direction
	float zi;               // Z direction
	float xg;               // X gravity
	float yg;               // Y gravity
	float zg;               // Z gravity
	float xo;               // orginal X position
	float yo;               // orginal Y position
	float zo;               // orginal Z position
} Particle;

typedef struct _ParticleSystem {
	int numParticles;
	Particle *particles;
	float slowdown;
	GLuint tex;
	Bool active;
	int x, y;
	float darken;
	GLuint blendMode;

	// Moved from drawParticles to get rid of spurious malloc's
	GLfloat *vertices_cache;
	int vertex_cache_count;
	GLfloat *coords_cache;
	int coords_cache_count;
	GLfloat *colors_cache;
	int color_cache_count;
	GLfloat *dcolors_cache;
	int dcolors_cache_count;
} ParticleSystem;

// Window properties for particle or polygon based animation effects
typedef struct _AnimWindowEngineData
{
	// for polygon engine
	PolygonSet *polygonSet;

	// for particle engine
	int numPs;
	ParticleSystem *ps;
} AnimWindowEngineData;


typedef Bool
(*tessellateProc) (CompWindow * w,
                   int gridSizeX,
                   int gridSizeY,
                   float thickness);

// Animaddon plugin functions for extension plugins to call
// (only for plugins with effects that use polygon or particle engines).
typedef struct _AnimAddonFunctions {
	AnimWindowEngineData * (*getAnimWindowEngineData)(CompWindow *w);

	// Particle engine functions
	void (*initParticles)(int numParticles,
	                      ParticleSystem * ps);
	void (*finiParticles)(ParticleSystem * ps);
	void (*drawParticleSystems)(CompWindow *w);
	UpdateBBProc particlesUpdateBB;
	void (*particlesCleanup)(CompWindow * w);
	Bool (*particlesPrePrepPaintScreen)(CompWindow * w,
	                                    int msSinceLastPaint);

	// Polygon engine functions
	Bool (*polygonsAnimInit)(CompWindow * w);
	void (*polygonsAnimStep)(CompWindow * w, float time);
	void (*polygonsPrePaintWindow)(CompWindow * w);
	void (*polygonsPostPaintWindow)(CompWindow * w);
	void (*polygonsStoreClips)(CompWindow * w,
	                           int nClip, BoxPtr pClip,
	                           int nMatrix, CompMatrix * matrix);
	void (*polygonsDrawCustomGeometry)(CompWindow * w);
	UpdateBBProc polygonsUpdateBB;
	Bool (*polygonsPrePreparePaintScreen)(CompWindow *w,
	                                      int msSinceLastPaint);
	void (*polygonsCleanup)(CompWindow *w);
	void (*polygonsRefresh)(CompWindow *w,
	                        Bool animInitialized);
	void (*polygonsDeceleratingAnimStepPolygon)(CompWindow * w,
	                                            PolygonObject *p,
	                                            float forwardProgress);
	void (*freePolygonObjects)(PolygonSet * pset);
	tessellateProc tessellateIntoRectangles;
	tessellateProc tessellateIntoHexagons;
	tessellateProc tessellateIntoGlass;
} AnimAddonFunctions;

typedef void (*AnimStepPolygonProc) (CompWindow *w,
                                     PolygonObject *p,
                                     float forwardProgress);

typedef struct _AnimAddonEffectProperties
{
	AnimStepPolygonProc animStepPolygonFunc;
} AnimAddonEffectProperties;

#endif

