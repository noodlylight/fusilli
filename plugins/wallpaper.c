/*
 * Compiz wallpaper plugin
 *
 * wallpaper.c
 *
 * Copyright (c) 2014 Michail Bitzes <noodlylight@gmail.com>
 *                    Fusilli port
 *
 * Copyright (c) 2008 Dennis Kasprzyk <onestone@opencompositing.org>
 *
 * Rewrite of wallpaper.c
 * Copyright (c) 2007 Robert Carr <racarr@opencompositing.org>
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

#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>

#include <fusilli-core.h>

static int bananaIndex;

static int displayPrivateIndex;

#define BG_FILL_TYPE_SOLID_FILL            0
#define BG_FILL_TYPE_VERTICAL_GRADIENT     1
#define BG_FILL_TYPE_HORIZONTAL_GRADIENT   2

#define BG_IMAGE_POS_SCALE_AND_CROP  0
#define BG_IMAGE_POS_SCALED          1
#define BG_IMAGE_POS_CENTERED        2
#define BG_IMAGE_POS_TILED           3
#define BG_IMAGE_POS_CENTER_TILED    4

typedef struct _WallpaperBackground {
	char           *image;
	int            imagePos;
	int            fillType;
	unsigned short color1[4];
	unsigned short color2[4];

	CompTexture    imgTex;
	unsigned int   width;
	unsigned int   height;

	CompTexture    fillTex;
} WallpaperBackground;

typedef struct _WallpaperDisplay {
	HandleEventProc handleEvent;

	int screenPrivateIndex;

	/* _COMPIZ_WALLPAPER_SUPPORTED atom is used to indicate that
	 * the wallpaper plugin or a plugin providing similar functionality is
	 * active so that desktop managers can respond appropriately */
	Atom compizWallpaperAtom;
} WallpaperDisplay;

typedef struct _WallpaperScreen {
	PaintOutputProc      paintOutput;
	DrawWindowProc       drawWindow;
	DamageWindowRectProc damageWindowRect;

	WallpaperBackground  *backgrounds;
	unsigned int         nBackgrounds;

	Bool                 propSet;
	Window               fakeDesktop;

	CompWindow           *desktop;
} WallpaperScreen;

#define GET_WALLPAPER_DISPLAY(d) \
        ((WallpaperDisplay *) (d)->base.privates[displayPrivateIndex].ptr)

#define WALLPAPER_DISPLAY(d) \
        WallpaperDisplay *wd = GET_WALLPAPER_DISPLAY (d)

#define GET_WALLPAPER_SCREEN(s, wd) \
        ((WallpaperScreen *) (s)->base.privates[(wd)->screenPrivateIndex].ptr)

#define WALLPAPER_SCREEN(s) \
        WallpaperScreen *ws = GET_WALLPAPER_SCREEN (s, GET_WALLPAPER_DISPLAY (&display))

static Visual *
findArgbVisual (Display *dpy,
                int     screen)
{
	XVisualInfo       *xvi;
	XVisualInfo       template;
	int               nvi;
	int               i;
	XRenderPictFormat *format;
	Visual            *visual;

	template.screen = screen;
	template.depth  = 32;
	template.class  = TrueColor;

	xvi = XGetVisualInfo (dpy,
	                      VisualScreenMask |
	                      VisualDepthMask  |
	                      VisualClassMask,
	                      &template,
	                      &nvi);
	if (!xvi)
		return 0;

	visual = 0;
	for (i = 0; i < nvi; i++)
	{
		format = XRenderFindVisualFormat (dpy, xvi[i].visual);
		if (format->type == PictTypeDirect && format->direct.alphaMask)
		{
			visual = xvi[i].visual;
			break;
		}
	}

	XFree (xvi);

	return visual;
}

static void
createFakeDesktopWindow (CompScreen *s)
{
	Display              *dpy = display.display;
	XSizeHints           xsh;
	XWMHints             xwmh;
	XSetWindowAttributes attr;
	Visual               *visual;
	XserverRegion        region;

	WALLPAPER_SCREEN (s);

	visual = findArgbVisual (dpy, s->screenNum);
	if (!visual)
		return;

	xsh.flags       = PSize | PPosition | PWinGravity;
	xsh.width       = 1;
	xsh.height      = 1;
	xsh.win_gravity = StaticGravity;

	xwmh.flags = InputHint;
	xwmh.input = 0;

	attr.background_pixel = 0;
	attr.border_pixel     = 0;
	attr.colormap         = XCreateColormap (dpy, s->root, visual, AllocNone);

	ws->fakeDesktop = XCreateWindow (dpy, s->root, -1, -1, 1, 1, 0, 32,
	                                 InputOutput, visual,
	                                 CWBackPixel | CWBorderPixel | CWColormap,
	                                 &attr);

	XSetWMProperties (dpy, ws->fakeDesktop, NULL, NULL,
	                  programArgv, programArgc, &xsh, &xwmh, NULL);

	XChangeProperty (dpy, ws->fakeDesktop, display.winStateAtom,
	                 XA_ATOM, 32, PropModeReplace,
	                 (unsigned char *) &display.winStateSkipPagerAtom, 1);

	XChangeProperty (dpy, ws->fakeDesktop, display.winTypeAtom,
	                 XA_ATOM, 32, PropModeReplace,
	                 (unsigned char *) &display.winTypeDesktopAtom, 1);

	region = XFixesCreateRegion (dpy, NULL, 0);

	XFixesSetWindowShapeRegion (dpy, ws->fakeDesktop, ShapeInput, 0, 0, region);

	XFixesDestroyRegion (dpy, region);

	XMapWindow (dpy, ws->fakeDesktop);
	XLowerWindow (dpy, ws->fakeDesktop);
}

static void
destroyFakeDesktopWindow (CompScreen *s)
{
	WALLPAPER_SCREEN (s);

	if (ws->fakeDesktop != None)
		XDestroyWindow (display.display, ws->fakeDesktop);

	ws->fakeDesktop = None;
}

static void
updateProperty(CompScreen *s)
{
	WALLPAPER_SCREEN (s);

	if (!ws->nBackgrounds)
	{
		WALLPAPER_DISPLAY (&display);

		if (ws->propSet)
			XDeleteProperty (display.display,
			                 s->root, wd->compizWallpaperAtom);
		ws->propSet = FALSE;
	}
	else if (!ws->propSet)
	{
		WALLPAPER_DISPLAY (&display);
		unsigned char sd = 1;

		XChangeProperty (display.display, s->root,
		                 wd->compizWallpaperAtom, XA_CARDINAL,
		                 8, PropModeReplace, &sd, 1);
		ws->propSet = TRUE;
	}
}

static void
initBackground (WallpaperBackground *back,
                CompScreen          *s)
{
	unsigned int        c[2];
	unsigned short      *color;

	initTexture (s, &back->imgTex);
	initTexture (s, &back->fillTex);

	if (back->image && strlen (back->image))
	{
		if (!readImageToTexture (s, &back->imgTex, back->image,
		                         &back->width, &back->height))
		{
			compLogMessage ("wallpaper", CompLogLevelWarn,
			                "Failed to load image: %s", back->image);

			back->width  = 0;
			back->height = 0;

			finiTexture (s, &back->imgTex);
			initTexture (s, &back->imgTex);
		}
	}

	color = back->color1;
	c[0] = ((color[3] << 16) & 0xff000000) |
	    ((color[0] * color[3] >> 8) & 0xff0000) |
	    ((color[1] * color[3] >> 16) & 0xff00) |
	    ((color[2] * color[3] >> 24) & 0xff);

	color = back->color2;
	c[1] = ((color[3] << 16) & 0xff000000) |
	    ((color[0] * color[3] >> 8) & 0xff0000) |
	    ((color[1] * color[3] >> 16) & 0xff00) |
	    ((color[2] * color[3] >> 24) & 0xff);

	if (back->fillType == BG_FILL_TYPE_VERTICAL_GRADIENT)
	{
		imageBufferToTexture (s, &back->fillTex, (char *) &c, 1, 2);
		back->fillTex.matrix.xx = 0.0;
	}
	else if (back->fillType == BG_FILL_TYPE_HORIZONTAL_GRADIENT)
	{
		imageBufferToTexture (s, &back->fillTex, (char *) &c, 2, 1);
		back->fillTex.matrix.yy = 0.0;
	}
	else
	{
		imageBufferToTexture (s, &back->fillTex, (char *) &c, 1, 1);
		back->fillTex.matrix.xx = 0.0;
		back->fillTex.matrix.yy = 0.0;
	}
}

static void
finiBackground (WallpaperBackground *back,
                CompScreen          *s)
{
	finiTexture (s, &back->imgTex);
	finiTexture (s, &back->fillTex);
}

static void
freeBackgrounds (CompScreen *s)
{
	WALLPAPER_SCREEN (s);

	unsigned int i;

	if (!ws->backgrounds || !ws->nBackgrounds)
		return;

	for (i = 0; i < ws->nBackgrounds; i++)
		finiBackground (&ws->backgrounds[i], s);

	free (ws->backgrounds);

	ws->backgrounds  = NULL;
	ws->nBackgrounds = 0;
}

static void
updateBackgrounds (CompScreen *s)
{
	WALLPAPER_SCREEN (s);
	int min, i;

	freeBackgrounds (s);

	const BananaValue *
	option_bg_image = bananaGetOption (bananaIndex,
	                                   "bg_image",
	                                   s->screenNum);

	const BananaValue *
	option_bg_image_pos = bananaGetOption (bananaIndex,
	                                       "bg_image_pos",
	                                       s->screenNum);

	const BananaValue *
	option_bg_fill_type = bananaGetOption (bananaIndex,
	                                       "bg_fill_type",
	                                       s->screenNum);

	const BananaValue *
	option_bg_color1 = bananaGetOption (bananaIndex,
	                                    "bg_color1",
	                                    s->screenNum);

	const BananaValue *
	option_bg_color2 = bananaGetOption (bananaIndex,
	                                    "bg_color2",
	                                    s->screenNum);

	min = MIN (option_bg_image->list.nItem, option_bg_image_pos->list.nItem);
	min = MIN (min, option_bg_fill_type->list.nItem);
	min = MIN (min, option_bg_color1->list.nItem);
	min = MIN (min, option_bg_color2->list.nItem);

	ws->backgrounds = malloc (min * sizeof (WallpaperBackground));
	ws->nBackgrounds = min;

	for (i = 0; i < ws->nBackgrounds; i++)
	{
		ws->backgrounds[i].image = strdup (option_bg_image->list.item[i].s);
		ws->backgrounds[i].imagePos = option_bg_image_pos->list.item[i].i;
		ws->backgrounds[i].fillType = option_bg_fill_type->list.item[i].i;

		stringToColor (option_bg_color1->list.item[i].s,
		               ws->backgrounds[i].color1);

		stringToColor (option_bg_color2->list.item[i].s, 
		               ws->backgrounds[i].color2);

		initBackground (&ws->backgrounds[i], s);
	}
}

static WallpaperBackground *
getBackgroundForViewport (CompScreen *s)
{
	WALLPAPER_SCREEN(s);
	int x, y;

	if (!ws->nBackgrounds)
		return NULL;

	x = s->x - (s->windowOffsetX / s->width);
	x %= s->hsize;
	if (x < 0)
		x += s->hsize;

	y = s->y - (s->windowOffsetY / s->height);
	y %= s->vsize;
	if (y < 0)
		y += s->vsize;

	return &ws->backgrounds[(x + (y * s->hsize)) % ws->nBackgrounds];
}

static void
wallpaperHandleEvent (XEvent      *event)
{
	CompScreen *s;
	WALLPAPER_DISPLAY (&display);

	UNWRAP (wd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (wd, &display, handleEvent, wallpaperHandleEvent);

	for (s = display.screens; s; s = s->next)
	{
		WALLPAPER_SCREEN (s);

		if (!s->desktopWindowCount && ws->fakeDesktop == None
		    && ws->nBackgrounds)
			createFakeDesktopWindow (s);

		if ((s->desktopWindowCount > 1 || !ws->nBackgrounds)
		     && ws->fakeDesktop != None)
			destroyFakeDesktopWindow (s);
	}
}

static Bool
wallpaperPaintOutput (CompScreen              *s,
                      const ScreenPaintAttrib *sAttrib,
                      const CompTransform     *transform,
                      Region                  region,
                      CompOutput              *output,
                      unsigned int            mask)
{
	Bool status;

	WALLPAPER_SCREEN (s);

	ws->desktop = NULL;

	UNWRAP (ws, s, paintOutput);
	status = (*s->paintOutput) (s, sAttrib, transform, region, output, mask);
	WRAP (ws, s, paintOutput, wallpaperPaintOutput);

	return status;
}

static Bool
wallpaperDrawWindow (CompWindow           *w,
                     const CompTransform  *transform,
                     const FragmentAttrib *attrib,
                     Region               region,
                     unsigned int         mask)
{
	Bool           status;
	CompScreen     *s = w->screen;
	FragmentAttrib fA = *attrib;

	WALLPAPER_SCREEN (w->screen);

	if ((!ws->desktop || ws->desktop == w) && ws->nBackgrounds && w->alpha &&
	                                      w->type & CompWindowTypeDesktopMask)
	{
		REGION              tmpRegion;
		CompMatrix          tmpMatrix;
		int            saveFilter, filterIdx;
		WallpaperBackground *back = getBackgroundForViewport (s);

		if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
			region = &infiniteRegion;

		tmpRegion.rects    = &tmpRegion.extents;
		tmpRegion.numRects = 1;

		if (mask & PAINT_WINDOW_ON_TRANSFORMED_SCREEN_MASK)
			filterIdx = SCREEN_TRANS_FILTER;
		else if (mask & PAINT_WINDOW_TRANSFORMED_MASK)
			filterIdx = WINDOW_TRANS_FILTER;
		else
		filterIdx = NOTHING_TRANS_FILTER;

		saveFilter = s->filter[filterIdx];

		s->filter[filterIdx] = COMP_TEXTURE_FILTER_GOOD;

		if (attrib->opacity != OPAQUE)
			mask |= PAINT_WINDOW_BLEND_MASK;

		w->vCount = w->indexCount = 0;

		tmpMatrix = back->fillTex.matrix;

		if (back->fillType == BG_FILL_TYPE_VERTICAL_GRADIENT)
		{
			tmpMatrix.yy /= (float) s->height / 2.0;
		}
		else if (back->fillType == BG_FILL_TYPE_HORIZONTAL_GRADIENT)
		{
			tmpMatrix.xx /= (float) s->width / 2.0;
		}

		(*w->screen->addWindowGeometry) (w, &tmpMatrix, 1,
		                                 &s->region, region);

		if (w->vCount)
			(*w->screen->drawWindowTexture) (w, &back->fillTex, &fA, mask);

		mask |= PAINT_WINDOW_BLEND_MASK;

		if (back->width && back->height)
		{
			Region reg = &s->region;
			float  s1, s2;
			int    x, y, xi;

			w->vCount = w->indexCount = 0;
			tmpMatrix = back->imgTex.matrix;


			if (back->imagePos == BG_IMAGE_POS_SCALE_AND_CROP)
			{
				s1 = (float) s->width / back->width;
				s2 = (float) s->height / back->height;

				s1 = MAX (s1, s2);

				tmpMatrix.xx /= s1;
				tmpMatrix.yy /= s1;

				x = (s->width - ((int)back->width * s1)) / 2.0;
				tmpMatrix.x0 -= x * tmpMatrix.xx;
				y = (s->height - ((int)back->height * s1)) / 2.0;
				tmpMatrix.y0 -= y * tmpMatrix.yy;
			}
			else if (back->imagePos == BG_IMAGE_POS_SCALED)
			{
				s1 = (float) s->width / back->width;
				s2 = (float) s->height / back->height;
				tmpMatrix.xx /= s1;
				tmpMatrix.yy /= s2;
			}
			else if (back->imagePos == BG_IMAGE_POS_CENTERED)
			{
				x = (s->width - (int)back->width) / 2;
				y = (s->height - (int)back->height) / 2;
				tmpMatrix.x0 -= x * tmpMatrix.xx;
				tmpMatrix.y0 -= y * tmpMatrix.yy;

				reg = &tmpRegion;

				tmpRegion.extents.x1 = MAX (0, x);
				tmpRegion.extents.y1 = MAX (0, y);
				tmpRegion.extents.x2 = MIN (s->width, x + back->width);
				tmpRegion.extents.y2 = MIN (s->height, y + back->height);
			}

			if (back->imagePos == BG_IMAGE_POS_TILED ||
			    back->imagePos == BG_IMAGE_POS_CENTER_TILED)
			{
				if (back->imagePos == BG_IMAGE_POS_CENTER_TILED)
				{
					x = (s->width - (int)back->width) / 2;
					y = (s->height - (int)back->height) / 2;

					if (x > 0)
						x = (x % (int)back->width) - (int)back->width;
					if (y > 0)
						y = (y % (int)back->height) - (int)back->height;
				}
				else
				{
					x = 0;
					y = 0;
				}

				reg = &tmpRegion;

				while (y < s->height)
				{
					xi = x;
					while (xi < s->width)
					{
						tmpMatrix = back->imgTex.matrix;
						tmpMatrix.x0 -= xi * tmpMatrix.xx;
						tmpMatrix.y0 -= y * tmpMatrix.yy;
						
						tmpRegion.extents.x1 = MAX (0, xi);
						tmpRegion.extents.y1 = MAX (0, y);
						tmpRegion.extents.x2 = MIN (s->width, xi + back->width);
						tmpRegion.extents.y2 = MIN (s->height, y + back->height);

						(*w->screen->addWindowGeometry) (w, &tmpMatrix, 1,
						                                       reg, region);

						xi += (int)back->width;
					}
					y += (int)back->height;
				}
			}
			else
			{
				(*w->screen->addWindowGeometry) (w, &tmpMatrix, 1,
				                                       reg, region);
			}

			if (w->vCount)
				(*w->screen->drawWindowTexture) (w, &back->imgTex, &fA, mask);

			s->filter[filterIdx] = saveFilter;
		}

		ws->desktop = w;
		fA.opacity  = OPAQUE;
	}

	UNWRAP (ws, w->screen, drawWindow);
	status = (*w->screen->drawWindow) (w, transform, &fA, region, mask);
	WRAP (ws, w->screen, drawWindow, wallpaperDrawWindow);

	return status;
}

static Bool
wallpaperDamageWindowRect (CompWindow *w,
                           Bool       initial,
                           BoxPtr     rect)
{
	Bool status;

	WALLPAPER_SCREEN (w->screen);

	if (w->id == ws->fakeDesktop)
		damageScreen (w->screen);

	UNWRAP (ws, w->screen, damageWindowRect);
	status = (*w->screen->damageWindowRect) (w, initial, rect);
	WRAP (ws, w->screen, damageWindowRect, wallpaperDamageWindowRect);

	return status;
}

static Bool
wallpaperInitDisplay (CompPlugin * p,
                      CompDisplay *d)
{
	WallpaperDisplay *wd;

	wd = malloc (sizeof (WallpaperDisplay));
	if (!wd)
		return FALSE;

	wd->screenPrivateIndex = allocateScreenPrivateIndex ();
	if (wd->screenPrivateIndex < 0)
	{
		free (wd);
		return FALSE;
	}

	wd->compizWallpaperAtom = XInternAtom (display.display,
	                                       "_COMPIZ_WALLPAPER_SUPPORTED", 0);

	d->base.privates[displayPrivateIndex].ptr = wd;

	WRAP (wd, d, handleEvent, wallpaperHandleEvent);

	return TRUE;
}

static void 
wallpaperFiniDisplay (CompPlugin  *p,
                      CompDisplay *d)
{
	WALLPAPER_DISPLAY (d);

	freeScreenPrivateIndex (wd->screenPrivateIndex);

	UNWRAP (wd, d, handleEvent);

	free (wd);
}

static Bool
wallpaperInitScreen (CompPlugin *p,
                     CompScreen *s)
{
	WallpaperScreen *ws;
	WALLPAPER_DISPLAY (&display);

	ws = malloc (sizeof (WallpaperScreen));
	if (!ws)
		return FALSE;

	ws->backgrounds  = NULL;
	ws->nBackgrounds = 0;

	ws->propSet = FALSE;

	ws->fakeDesktop = None;

	s->base.privates[wd->screenPrivateIndex].ptr = ws;

	updateBackgrounds (s);
	updateProperty (s);
	damageScreen (s);

	if (!s->desktopWindowCount && ws->nBackgrounds)
		createFakeDesktopWindow (s);

	WRAP (ws, s, paintOutput, wallpaperPaintOutput);
	WRAP (ws, s, drawWindow, wallpaperDrawWindow);
	WRAP (ws, s, damageWindowRect, wallpaperDamageWindowRect);

	return TRUE;
}

static void 
wallpaperFiniScreen (CompPlugin *p,
                     CompScreen *s)
{
	WALLPAPER_SCREEN (s);
	WALLPAPER_DISPLAY (&display);

	if (ws->propSet)
		XDeleteProperty (display.display, s->root, wd->compizWallpaperAtom);

	if (ws->fakeDesktop != None)
		destroyFakeDesktopWindow (s);

	freeBackgrounds (s);

	UNWRAP (ws, s, paintOutput);
	UNWRAP (ws, s, drawWindow);
	UNWRAP (ws, s, damageWindowRect);

	free (ws);
}

static CompBool
wallpaperInitObject (CompPlugin *p,
                     CompObject *o)
{
	static InitPluginObjectProc dispTab[] = {
		(InitPluginObjectProc) 0, /* InitCore */
		(InitPluginObjectProc) wallpaperInitDisplay,
		(InitPluginObjectProc) wallpaperInitScreen
	};

	RETURN_DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), TRUE, (p, o));
}

static void
wallpaperFiniObject (CompPlugin *p,
                     CompObject *o)
{
	static FiniPluginObjectProc dispTab[] = {
		(FiniPluginObjectProc) 0, /* FiniCore */
		(FiniPluginObjectProc) wallpaperFiniDisplay,
		(FiniPluginObjectProc) wallpaperFiniScreen
	};

	DISPATCH (o, dispTab, ARRAY_SIZE (dispTab), (p, o));
}

static void
wallpaperChangeNotify (const char        *optionName,
                       BananaType        optionType,
                       const BananaValue *optionValue,
                       int               screenNum)
{
	CompScreen *s;

	if (screenNum != -1)
		s = getScreenFromScreenNum (screenNum);
	else
		return;

	updateBackgrounds (s);
	updateProperty (s);
	damageScreen (s);

}

static Bool
wallpaperInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("annotate", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("wallpaper");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, wallpaperChangeNotify);

	return TRUE;
}

static void
wallpaperFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable wallpaperVTable = {
	"wallpaper",
	wallpaperInit,
	wallpaperFini,
	wallpaperInitObject,
	wallpaperFiniObject,
};

CompPluginVTable*
getCompPluginInfo20140724 (void)
{
	return &wallpaperVTable;
}

