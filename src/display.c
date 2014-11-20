/*
 * Copyright © 2005 Novell, Inc.
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

#ifdef HAVE_CONFIG_H
#  include "../config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <assert.h>

#define XK_MISCELLANY
#include <X11/keysymdef.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>

#include <fusilli-core.h>

CompDisplay display;

static unsigned int virtualModMask[] = {
	CompAltMask, CompMetaMask, CompSuperMask, CompHyperMask,
	CompModeSwitchMask, CompNumLockMask, CompScrollLockMask
};

static CompScreen *targetScreen = NULL;
static CompOutput *targetOutput;

static Bool inHandleEvent = FALSE;

static const CompTransform identity = {
	{
		1.0, 0.0, 0.0, 0.0,
		0.0, 1.0, 0.0, 0.0,
		0.0, 0.0, 1.0, 0.0,
		0.0, 0.0, 0.0, 1.0
	}
};

int lastPointerX = 0;
int lastPointerY = 0;
int pointerX     = 0;
int pointerY     = 0;

static char *displayPrivateIndices = 0;
static int  displayPrivateLen = 0;

static int
reallocDisplayPrivate (int  size,
                       void *closure)
{
	void        *privates;

	privates = realloc (display.base.privates, size * sizeof (CompPrivate));
	if (!privates)
		return FALSE;

	display.base.privates = (CompPrivate *) privates;

	return TRUE;
}

int
allocDisplayObjectPrivateIndex (CompObject *parent)
{
	return allocatePrivateIndex (&displayPrivateLen,
	                             &displayPrivateIndices,
	                             reallocDisplayPrivate,
	                             0);
}

void
freeDisplayObjectPrivateIndex (CompObject *parent,
                               int        index)
{
	freePrivateIndex (displayPrivateLen, displayPrivateIndices, index);
}

CompBool
forEachDisplayObject (CompObject         *parent,
                      ObjectCallBackProc proc,
                      void               *closure)
{
	if (parent->type == COMP_OBJECT_TYPE_CORE)
	{
		if (display.screens != NULL) //HACK: Verify that display is initialized
			if (!(*proc) (&display.base, closure))
				return FALSE;

	}

	return TRUE;
}

char *
nameDisplayObject (CompObject *object)
{
	return NULL;
}

CompObject *
findDisplayObject (CompObject *parent,
                   const char *name)
{
	if (parent->type == COMP_OBJECT_TYPE_CORE)
	{
		if (!name || !name[0])
			return &display.base;
	}

	return NULL;
}

int
allocateDisplayPrivateIndex (void)
{
	return compObjectAllocatePrivateIndex (NULL, COMP_OBJECT_TYPE_DISPLAY);
}

void
freeDisplayPrivateIndex (int index)
{
	compObjectFreePrivateIndex (NULL, COMP_OBJECT_TYPE_DISPLAY, index);
}


static void
setAudibleBell (Bool        audible)
{
	if (display.xkbExtension)
		XkbChangeEnabledControls (display.display,
		                          XkbUseCoreKbd,
		                          XkbAudibleBellMask,
		                          audible ? XkbAudibleBellMask : 0);
}

static Bool
pingTimeout (void *closure)
{
	CompDisplay *d = closure;
	CompScreen  *s;
	CompWindow  *w;
	XEvent      ev;
	int         ping = d->lastPing + 1;

	ev.type                 = ClientMessage;
	ev.xclient.window       = 0;
	ev.xclient.message_type = d->wmProtocolsAtom;
	ev.xclient.format       = 32;
	ev.xclient.data.l[0]    = d->wmPingAtom;
	ev.xclient.data.l[1]    = ping;
	ev.xclient.data.l[2]    = 0;
	ev.xclient.data.l[3]    = 0;
	ev.xclient.data.l[4]    = 0;

	for (s = d->screens; s; s = s->next)
	{
		for (w = s->windows; w; w = w->next)
		{
			if (w->attrib.map_state != IsViewable)
				continue;

			if (!(w->type & CompWindowTypeNormalMask))
				continue;

			if (w->protocols & CompWindowProtocolPingMask)
			{
				if (w->transientFor)
					continue;

				if (w->lastPong < d->lastPing)
				{
					if (w->alive)
					{
						w->alive = FALSE;

						if (w->closeRequests)
						{
							toolkitAction (s,
							           d->toolkitActionForceQuitDialogAtom,
							           w->lastCloseRequestTime,
							           w->id,
							           TRUE,
							           0,
							           0);

							w->closeRequests = 0;
						}

						addWindowDamage (w);
					}
				}

				ev.xclient.window    = w->id;
				ev.xclient.data.l[2] = w->id;

				XSendEvent (d->display, w->id, FALSE, NoEventMask, &ev);
			}
		}
	}

	d->lastPing = ping;

	return TRUE;
}

void
displayChangeNotify (const char        *optionName,
                     BananaType        optionType,
                     const BananaValue *optionValue,
                     int               screenNum)
{
	if (strcasecmp (optionName, "active_plugins") == 0)
	{
		display.dirtyPluginList = TRUE;
	}
	else if (strcasecmp (optionName, "texture_filter") == 0)
	{
		CompScreen *s;

		for (s = display.screens; s; s = s->next)
			damageScreen (s);

		if (!optionValue->i)
			display.textureFilter = GL_NEAREST;
		else
			display.textureFilter = GL_LINEAR;
	}
	else if (strcasecmp (optionName, "ping_delay") == 0)
	{
		if (display.pingHandle)
			compRemoveTimeout (display.pingHandle);

		display.pingHandle =
		    compAddTimeout (optionValue->i, optionValue->i + 500,
		                    pingTimeout, &display);
	}
	else if (strcasecmp (optionName, "audible_bell") == 0)
	{
		setAudibleBell (optionValue->b);
	}
	else if (strcasecmp (optionName, "close_window_key") == 0)
	{
		updateKey (optionValue->s, &display.close_window_key);
	}
	else if (strcasecmp (optionName, "raise_window_key") == 0)
	{
		updateKey (optionValue->s, &display.raise_window_key);
	}
	else if (strcasecmp (optionName, "lower_window_key") == 0)
	{
		updateKey (optionValue->s, &display.lower_window_key);
	}
	else if (strcasecmp (optionName, "unmaximize_window_key") == 0)
	{
		updateKey (optionValue->s, &display.unmaximize_window_key);
	}
	else if (strcasecmp (optionName, "minimize_window_key") == 0)
	{
		updateKey (optionValue->s, &display.minimize_window_key);
	}
	else if (strcasecmp (optionName, "maximize_window_key") == 0)
	{
		updateKey (optionValue->s, &display.maximize_window_key);
	}
	else if (strcasecmp (optionName, "maximize_window_horizontally_key") == 0)
	{
		updateKey (optionValue->s, 
		           &display.maximize_window_horizontally_key);
	}
	else if (strcasecmp (optionName, "maximize_window_vertically_key") == 0)
	{
		updateKey (optionValue->s, 
		           &display.maximize_window_vertically_key);
	}
	else if (strcasecmp (optionName, "window_menu_key") == 0)
	{
		updateKey (optionValue->s, &display.window_menu_key);
	}
	else if (strcasecmp (optionName, "show_desktop_key") == 0)
	{
		updateKey (optionValue->s, &display.show_desktop_key);
	}
	else if (strcasecmp (optionName, "toggle_window_maximized_key") == 0)
	{
		updateKey (optionValue->s, &display.toggle_window_maximized_key);
	}
	else if (strcasecmp (optionName, 
	                     "toggle_window_maximized_horizontally_key") == 0)
	{
		updateKey (optionValue->s, 
		           &display.toggle_window_maximized_horizontally_key);
	}
	else if (strcasecmp (optionName, 
	                     "toggle_window_maximized_vertically_key") == 0)
	{
		updateKey (optionValue->s, 
		           &display.toggle_window_maximized_vertically_key);
	}
	else if (strcasecmp (optionName, "toggle_window_shaded_key") == 0)
	{
		updateKey (optionValue->s, &display.toggle_window_shaded_key);
	}
	else if (strcasecmp (optionName, "slow_animations_key") == 0)
	{
		updateKey (optionValue->s, &display.slow_animations_key);
	}
	else if (strcasecmp (optionName, "close_window_button") == 0)
	{
		updateButton (optionValue->s, &display.close_window_button);
	}
	else if (strcasecmp (optionName, "raise_window_button") == 0)
	{
		updateButton (optionValue->s, &display.raise_window_button);
	}
	else if (strcasecmp (optionName, "lower_window_button") == 0)
	{
		updateButton (optionValue->s, &display.lower_window_button);
	}
	else if (strcasecmp (optionName, "minimize_window_button") == 0)
	{
		updateButton (optionValue->s, &display.minimize_window_button);
	}
	else if (strcasecmp (optionName, "window_menu_button") == 0)
	{
		updateButton (optionValue->s, &display.window_menu_button);
	}
	else if (strcasecmp (optionName, "toggle_window_maximized_button") == 0)
	{
		updateButton (optionValue->s, 
		              &display.toggle_window_maximized_button);
	}
}

static void
updatePlugins (void)
{
	//pop and unload all plugins *except core*
	int i;
	for (i = 0; i < display.plugin.list.nItem - 1; i++)
	{
		CompPlugin *p;
		p = popPlugin ();
		unloadPlugin (p);
	}

	finiBananaValue (&display.plugin, BananaListString);

	//load and push all plugins
	initBananaValue (&display.plugin, BananaListString);

	//core was not popped/unloaded, so adding it to the list is enough
	addItemToBananaList ("core", BananaListString, &display.plugin);

	const BananaValue *
	option_active_plugins = bananaGetOption (coreBananaIndex,
	                                         "active_plugins",
	                                         -1);


	for (i = 0; i < option_active_plugins->list.nItem; i++)
	{
		CompPlugin *p;
		p = loadPlugin (option_active_plugins->list.item[i].s);
		
		if (p)
		{
			if (pushPlugin (p))
			{
				addItemToBananaList (option_active_plugins->list.item[i].s,
				                     BananaListString,
				                     &display.plugin);
			}
			else
			{
				unloadPlugin (p);
			}
		}
	}

	display.dirtyPluginList = FALSE;
}

static void
addTimeout (CompTimeout *timeout)
{
	CompTimeout *p = 0, *t;

	for (t = core.timeouts; t; t = t->next)
	{
		if (timeout->minTime < t->minLeft)
			break;

		p = t;
	}

	timeout->next = t;
	timeout->minLeft = timeout->minTime;
	timeout->maxLeft = timeout->maxTime;

	if (p)
		p->next = timeout;
	else
		core.timeouts = timeout;
}

CompTimeoutHandle
compAddTimeout (int          minTime,
                int          maxTime,
                CallBackProc callBack,
                void         *closure)
{
	CompTimeout *timeout;

	timeout = malloc (sizeof (CompTimeout));
	if (!timeout)
		return 0;

	timeout->minTime  = minTime;
	timeout->maxTime  = (maxTime >= minTime) ? maxTime : minTime;
	timeout->callBack = callBack;
	timeout->closure  = closure;
	timeout->handle   = core.lastTimeoutHandle++;

	if (core.lastTimeoutHandle == MAXSHORT)
		core.lastTimeoutHandle = 1;

	addTimeout (timeout);

	return timeout->handle;
}

void *
compRemoveTimeout (CompTimeoutHandle handle)
{
	CompTimeout *p = 0, *t;
	void        *closure = NULL;

	for (t = core.timeouts; t; t = t->next)
	{
		if (t->handle == handle)
			break;

		p = t;
	}

	if (t)
	{
		if (p)
			p->next = t->next;
		else
			core.timeouts = t->next;

		closure = t->closure;

		free (t);
	}

	return closure;
}

CompWatchFdHandle
compAddWatchFd (int          fd,
                short int    events,
                CallBackProc callBack,
                void         *closure)
{
	CompWatchFd *watchFd;

	watchFd = malloc (sizeof (CompWatchFd));
	if (!watchFd)
		return 0;

	watchFd->fd       = fd;
	watchFd->callBack = callBack;
	watchFd->closure  = closure;
	watchFd->handle   = core.lastWatchFdHandle++;

	if (core.lastWatchFdHandle == MAXSHORT)
		core.lastWatchFdHandle = 1;

	watchFd->next = core.watchFds;
	core.watchFds = watchFd;

	core.nWatchFds++;

	core.watchPollFds = realloc (core.watchPollFds,
	                             core.nWatchFds * sizeof (struct pollfd));

	core.watchPollFds[core.nWatchFds - 1].fd     = fd;
	core.watchPollFds[core.nWatchFds - 1].events = events;

	return watchFd->handle;
}

void
compRemoveWatchFd (CompWatchFdHandle handle)
{
	CompWatchFd *p = 0, *w;
	int i;

	for (i = core.nWatchFds - 1, w = core.watchFds; w; i--, w = w->next)
	{
		if (w->handle == handle)
			break;

		p = w;
	}

	if (w)
	{
		if (p)
			p->next = w->next;
		else
			core.watchFds = w->next;

		core.nWatchFds--;

		if (i < core.nWatchFds)
			memmove (&core.watchPollFds[i], &core.watchPollFds[i + 1],
			        (core.nWatchFds - i) * sizeof (struct pollfd));

		free (w);
	}
}

short int
compWatchFdEvents (CompWatchFdHandle handle)
{
	CompWatchFd *w;
	int          i;

	for (i = core.nWatchFds - 1, w = core.watchFds; w; i--, w = w->next)
		if (w->handle == handle)
			return core.watchPollFds[i].revents;

	return 0;
}

#define TIMEVALDIFF(tv1, tv2)                                                  \
        ((tv1)->tv_sec == (tv2)->tv_sec || (tv1)->tv_usec >= (tv2)->tv_usec) ? \
        ((((tv1)->tv_sec - (tv2)->tv_sec) * 1000000) +                         \
        ((tv1)->tv_usec - (tv2)->tv_usec)) / 1000 :                           \
        ((((tv1)->tv_sec - 1 - (tv2)->tv_sec) * 1000000) +                     \
        (1000000 + (tv1)->tv_usec - (tv2)->tv_usec)) / 1000

static int
getTimeToNextRedraw (CompScreen     *s,
                     struct timeval *tv,
                     struct timeval *lastTv,
                     Bool           idle)
{
	int diff, next;

	diff = TIMEVALDIFF (tv, lastTv);

	/* handle clock rollback */
	if (diff < 0)
		diff = 0;

	const BananaValue *
	option_sync_to_vblank = bananaGetOption (coreBananaIndex,
	                                         "sync_to_vblank",
	                                         s->screenNum);

	if (idle ||
		(s->getVideoSync && option_sync_to_vblank->b))
	{
		if (s->timeMult > 1)
		{
			s->frameStatus = -1;
			s->redrawTime = s->optimalRedrawTime;
			s->timeMult--;
		}
	}
	else
	{
		if (diff > s->redrawTime)
		{
			if (s->frameStatus > 0)
				s->frameStatus = 0;

			next = s->optimalRedrawTime * (s->timeMult + 1);
			if (diff > next)
			{
				s->frameStatus--;
				if (s->frameStatus < -1)
				{
					s->timeMult++;
					s->redrawTime = diff = next;
				}
			}
		}
		else if (diff < s->redrawTime)
		{
			if (s->frameStatus < 0)
				s->frameStatus = 0;

			if (s->timeMult > 1)
			{
				next = s->optimalRedrawTime * (s->timeMult - 1);
				if (diff < next)
				{
					s->frameStatus++;
					if (s->frameStatus > 4)
					{
						s->timeMult--;
						s->redrawTime = next;
					}
				}
			}
		}
	}

	if (diff > s->redrawTime)
		return 0;

	return s->redrawTime - diff;
}

static const int maskTable[] = {
	ShiftMask, LockMask, ControlMask, Mod1Mask,
	Mod2Mask, Mod3Mask, Mod4Mask, Mod5Mask
};
static const int maskTableSize = sizeof (maskTable) / sizeof (int);

void
updateModifierMappings (void)
{
	unsigned int    modMask[CompModNum];
	int             i, minKeycode, maxKeycode, keysymsPerKeycode = 0;
	KeySym*         key;

	for (i = 0; i < CompModNum; i++)
		modMask[i] = 0;

	XDisplayKeycodes (display.display, &minKeycode, &maxKeycode);
	key = XGetKeyboardMapping (display.display,
	                           minKeycode, (maxKeycode - minKeycode + 1),
	                           &keysymsPerKeycode);

	if (display.modMap)
		XFreeModifiermap (display.modMap);

	display.modMap = XGetModifierMapping (display.display);
	if (display.modMap && display.modMap->max_keypermod > 0)
	{
		KeySym keysym;
		int    index, size, mask;

		size = maskTableSize * display.modMap->max_keypermod;

		for (i = 0; i < size; i++)
		{
			if (!display.modMap->modifiermap[i])
				continue;

			index = 0;
			do
			{
				//convert keycode to keysym
				keysym = key[(display.modMap->modifiermap[i] - minKeycode) *
				             keysymsPerKeycode + index];

				index++;
			} while (!keysym && index < keysymsPerKeycode);

			if (keysym)
			{
				mask = maskTable[i / display.modMap->max_keypermod];

				if (keysym == XK_Alt_L ||
				    keysym == XK_Alt_R)
				{
					modMask[CompModAlt] |= mask;
				}
				else if (keysym == XK_Meta_L ||
				         keysym == XK_Meta_R)
				{
					modMask[CompModMeta] |= mask;
				}
				else if (keysym == XK_Super_L ||
				         keysym == XK_Super_R)
				{
					modMask[CompModSuper] |= mask;
				}
				else if (keysym == XK_Hyper_L ||
				         keysym == XK_Hyper_R)
				{
					modMask[CompModHyper] |= mask;
				}
				else if (keysym == XK_Mode_switch)
				{
					modMask[CompModModeSwitch] |= mask;
				}
				else if (keysym == XK_Scroll_Lock)
				{
					modMask[CompModScrollLock] |= mask;
				}
				else if (keysym == XK_Num_Lock)
				{
					modMask[CompModNumLock] |= mask;
				}
			}
		}

		for (i = 0; i < CompModNum; i++)
		{
			if (!modMask[i])
				modMask[i] = CompNoMask;
		}

		if (memcmp (modMask, display.modMask, sizeof (modMask)))
		{
			CompScreen *s;

			memcpy (display.modMask, modMask, sizeof (modMask));

			display.ignoredModMask = LockMask |
			                    (modMask[CompModNumLock]    & ~CompNoMask) |
			                    (modMask[CompModScrollLock] & ~CompNoMask);

			for (s = display.screens; s; s = s->next)
				updatePassiveGrabs (s);
		}
	}

	if (key)
		XFree (key);
}

unsigned int
virtualToRealModMask (unsigned int modMask)
{
	int i;

	for (i = 0; i < CompModNum; i++)
	{
		if (modMask & virtualModMask[i])
		{
			modMask &= ~virtualModMask[i];
			modMask |= display.modMask[i];
		}
	}

	return modMask;
}

unsigned int
keycodeToModifiers (int         keycode)
{
	unsigned int mods = 0;
	int mod, k;

	for (mod = 0; mod < maskTableSize; mod++)
	{
		for (k = 0; k < display.modMap->max_keypermod; k++)
		{
			if (display.modMap->modifiermap[mod * display.modMap->max_keypermod + k] == 
			    keycode)
				mods |= maskTable[mod];
		}
	}

	return mods;
}

static int
doPoll (int timeout)
{
	int rv;

	rv = poll (core.watchPollFds, core.nWatchFds, timeout);
	if (rv)
	{
		CompWatchFd *w;
		int         i;

		for (i = core.nWatchFds - 1, w = core.watchFds; w; i--, w = w->next)
		{
			if (core.watchPollFds[i].revents != 0 && w->callBack)
				(*w->callBack) (w->closure);
		}
	}

	return rv;
}

static void
handleTimeouts (struct timeval *tv)
{
	CompTimeout *t;
	int         timeDiff;

	timeDiff = TIMEVALDIFF (tv, &core.lastTimeout);

	/* handle clock rollback */
	if (timeDiff < 0)
		timeDiff = 0;

	for (t = core.timeouts; t; t = t->next)
	{
		t->minLeft -= timeDiff;
		t->maxLeft -= timeDiff;
	}

	while (core.timeouts && core.timeouts->minLeft <= 0)
	{
		t = core.timeouts;
		if ((*t->callBack) (t->closure))
		{
			core.timeouts = t->next;
			addTimeout (t);
		}
		else
		{
			core.timeouts = t->next;
			free (t);
		}
	}

	core.lastTimeout = *tv;
}

static void
waitForVideoSync (CompScreen *s)
{
	unsigned int sync;

	const BananaValue *
	option_sync_to_vblank = bananaGetOption (coreBananaIndex,
	                                         "sync_to_vblank",
	                                         s->screenNum);

	if (!option_sync_to_vblank->b)
		return;

	if (s->getVideoSync)
	{
		glFlush ();

		(*s->getVideoSync) (&sync);
		(*s->waitVideoSync) (2, (sync + 1) % 2, &sync);
	}
}


void
paintScreen (CompScreen   *s,
             CompOutput   *outputs,
             int          numOutput,
             unsigned int mask)
{
	XRectangle r;
	int        i;

	for (i = 0; i < numOutput; i++)
	{
		targetScreen = s;
		targetOutput = &outputs[i];

		r.x      = outputs[i].region.extents.x1;
		r.y      = s->height - outputs[i].region.extents.y2;
		r.width  = outputs[i].width;
		r.height = outputs[i].height;

		if (s->lastViewport.x      != r.x     ||
		    s->lastViewport.y      != r.y     ||
		    s->lastViewport.width  != r.width ||
		    s->lastViewport.height != r.height)
		{
			glViewport (r.x, r.y, r.width, r.height);
			s->lastViewport = r;
		}

		if (mask & COMP_SCREEN_DAMAGE_ALL_MASK)
		{
			(*s->paintOutput) (s,
			                   &defaultScreenPaintAttrib,
			                   &identity,
			                   &outputs[i].region, &outputs[i],
			                   PAINT_SCREEN_REGION_MASK |
			                   PAINT_SCREEN_FULL_MASK);
		}
		else if (mask & COMP_SCREEN_DAMAGE_REGION_MASK)
		{
			XIntersectRegion (core.tmpRegion,
			                  &outputs[i].region,
			                  core.outputRegion);

			if (!(*s->paintOutput) (s,
			                        &defaultScreenPaintAttrib,
			                        &identity,
			                        core.outputRegion, &outputs[i],
			                        PAINT_SCREEN_REGION_MASK))
			{
				(*s->paintOutput) (s,
				                   &defaultScreenPaintAttrib,
				                   &identity,
				                   &outputs[i].region, &outputs[i],
				                   PAINT_SCREEN_FULL_MASK);

				XUnionRegion (core.tmpRegion,
				              &outputs[i].region,
				              core.tmpRegion);

			}
		}
	}
}

void
eventLoop (void)
{
	XEvent         event;
	int            timeDiff;
	struct timeval tv;
	CompDisplay    *d;
	CompScreen     *s;
	CompWindow     *w;
	CompTimeout    *t;
	int            time, timeToNextRedraw = 0;
	unsigned int   damageMask, mask;

	d = &display;

	d->watchFdHandle = compAddWatchFd (ConnectionNumber (d->display),
	                                   POLLIN, NULL, NULL);

	for (;;)
	{
		if (restartSignal || shutDown)
			break;

		if (d->dirtyPluginList)
			updatePlugins ();

		while (XPending (d->display))
		{
			XNextEvent (d->display, &event);

			switch (event.type) {
			case ButtonPress:
			case ButtonRelease:
				pointerX = event.xbutton.x_root;
				pointerY = event.xbutton.y_root;
				break;
			case KeyPress:
			case KeyRelease:
				pointerX = event.xkey.x_root;
				pointerY = event.xkey.y_root;
				break;
			case MotionNotify:
				pointerX = event.xmotion.x_root;
				pointerY = event.xmotion.y_root;
				break;
			case EnterNotify:
			case LeaveNotify:
				pointerX = event.xcrossing.x_root;
				pointerY = event.xcrossing.y_root;
				break;
			case ClientMessage:
				if (event.xclient.message_type == d->xdndPositionAtom)
				{
					pointerX = event.xclient.data.l[2] >> 16;
					pointerY = event.xclient.data.l[2] & 0xffff;
				}
			default:
				break;
			}
			sn_display_process_event (d->snDisplay, &event);

			inHandleEvent = TRUE;

			(*d->handleEvent) (&event);

			inHandleEvent = FALSE;

			lastPointerX = pointerX;
			lastPointerY = pointerY;
		}

		for (s = d->screens; s; s = s->next)
		{
			if (s->damageMask)
			{
				finishScreenDrawing (s);
			}
			else
			{
				s->idle = TRUE;
			}
		}

		damageMask       = 0;
		timeToNextRedraw = MAXSHORT;

		for (s = d->screens; s; s = s->next)
		{
			if (!s->damageMask)
				continue;

			if (!damageMask)
			{
				gettimeofday (&tv, 0);
				damageMask |= s->damageMask;
			}

			s->timeLeft = getTimeToNextRedraw (s, &tv, &s->lastRedraw,
			                                   s->idle);
			if (s->timeLeft < timeToNextRedraw)
				timeToNextRedraw = s->timeLeft;
		}

		if (damageMask)
		{
			time = timeToNextRedraw;
			if (time)
				time = doPoll (time);

			if (time == 0)
			{
				gettimeofday (&tv, 0);

				if (core.timeouts)
					handleTimeouts (&tv);

				for (s = d->screens; s; s = s->next)
				{
					if (!s->damageMask || s->timeLeft > timeToNextRedraw)
						continue;

					targetScreen = s;

					timeDiff = TIMEVALDIFF (&tv, &s->lastRedraw);

					/* handle clock rollback */
					if (timeDiff < 0)
					    timeDiff = 0;

					makeScreenCurrent (s);

					if (s->slowAnimations)
					{
						(*s->preparePaintScreen) (s,
						                          s->idle ? 2 :
						                          (timeDiff * 2) /
						                          s->redrawTime);
					}
					else
						(*s->preparePaintScreen) (s,
						                          s->idle ? s->redrawTime :
						                          timeDiff);

					/* substract top most overlay window region */
					if (s->overlayWindowCount)
					{
						for (w = s->reverseWindows; w; w = w->prev)
						{
							if (w->destroyed || w->invisible)
								continue;

							if (!w->redirected)
								XSubtractRegion (s->damage, w->region,
								                 s->damage);

							break;
						}

						if (s->damageMask & COMP_SCREEN_DAMAGE_ALL_MASK)
						{
							s->damageMask &= ~COMP_SCREEN_DAMAGE_ALL_MASK;
							s->damageMask |= COMP_SCREEN_DAMAGE_REGION_MASK;
						}
					}

					if (s->damageMask & COMP_SCREEN_DAMAGE_REGION_MASK)
					{
						XIntersectRegion (s->damage, &s->region,
						                  core.tmpRegion);

						if (core.tmpRegion->numRects  == 1        &&
						    core.tmpRegion->rects->x1 == 0        &&
						    core.tmpRegion->rects->y1 == 0        &&
						    core.tmpRegion->rects->x2 == s->width &&
						    core.tmpRegion->rects->y2 == s->height)
						damageScreen (s);
					}

					EMPTY_REGION (s->damage);

					mask = s->damageMask;
					s->damageMask = 0;

					if (s->clearBuffers)
					{
						if (mask & COMP_SCREEN_DAMAGE_ALL_MASK)
							glClear (GL_COLOR_BUFFER_BIT);
					}

					const BananaValue *
					option_force_independent_output_painting =
					  bananaGetOption (coreBananaIndex, 
					  "force_independent_output_painting", s->screenNum);

					if (option_force_independent_output_painting->b
					    || !s->hasOverlappingOutputs)
						(*s->paintScreen) (s, s->outputDev,
						                   s->nOutputDev,
						                   mask);
					else
						(*s->paintScreen) (s, &s->fullscreenOutput, 1, mask);

					targetScreen = NULL;
					targetOutput = &s->outputDev[0];

					waitForVideoSync (s);

					if (mask & COMP_SCREEN_DAMAGE_ALL_MASK)
					{
						glXSwapBuffers (d->display, s->output);
					}
					else
					{
						BoxPtr pBox;
						int    nBox, y;

						pBox = core.tmpRegion->rects;
						nBox = core.tmpRegion->numRects;

						if (s->copySubBuffer)
						{
							while (nBox--)
							{
								y = s->height - pBox->y2;

								(*s->copySubBuffer) (d->display,
								                     s->output,
								                     pBox->x1, y,
								                     pBox->x2 - pBox->x1,
								                     pBox->y2 - pBox->y1);

								pBox++;
							}
						}
						else
						{
							glEnable (GL_SCISSOR_TEST);
							glDrawBuffer (GL_FRONT);

							while (nBox--)
							{
								y = s->height - pBox->y2;

								glBitmap (0, 0, 0, 0,
								          pBox->x1 - s->rasterX,
								          y - s->rasterY,
								          NULL);

								s->rasterX = pBox->x1;
								s->rasterY = y;

								glScissor (pBox->x1, y,
								           pBox->x2 - pBox->x1,
								           pBox->y2 - pBox->y1);

								glCopyPixels (pBox->x1, y,
								              pBox->x2 - pBox->x1,
								              pBox->y2 - pBox->y1,
								              GL_COLOR);

								pBox++;
							}

							glDrawBuffer (GL_BACK);
							glDisable (GL_SCISSOR_TEST);
							glFlush ();
						}
					}

					s->lastRedraw = tv;

					(*s->donePaintScreen) (s);

					/* remove destroyed windows */
					while (s->pendingDestroys)
					{
						CompWindow *w;

						for (w = s->windows; w; w = w->next)
						{
							if (w->destroyed)
							{
								addWindowDamage (w);
								removeWindow (w);
								break;
							}
						}

						s->pendingDestroys--;
					}

					s->idle = FALSE;
				}
			}
		}
		else
		{
			if (core.timeouts)
			{
				if (core.timeouts->minLeft > 0)
				{
					t = core.timeouts;
					time = t->maxLeft;
					while (t && t->minLeft <= time)
					{
						if (t->maxLeft < time)
							time = t->maxLeft;
						t = t->next;
					}
					doPoll (time);
				}

				gettimeofday (&tv, 0);

				handleTimeouts (&tv);
			}
			else
			{
				doPoll (-1);
			}
		}
	}

	compRemoveWatchFd (d->watchFdHandle);
}

static int errors = 0;

static int
errorHandler (Display     *dpy,
              XErrorEvent *e)
{

#ifdef DEBUG
	char str[128];
#endif

	errors++;

#ifdef DEBUG
	XGetErrorDatabaseText (dpy, "XlibMessage", "XError", "", str, 128);
	fprintf (stderr, "%s", str);

	XGetErrorText (dpy, e->error_code, str, 128);
	fprintf (stderr, ": %s\n  ", str);

	XGetErrorDatabaseText (dpy, "XlibMessage", "MajorCode", "%d", str, 128);
	fprintf (stderr, str, e->request_code);

	sprintf (str, "%d", e->request_code);
	XGetErrorDatabaseText (dpy, "XRequest", str, "", str, 128);
	if (strcmp (str, ""))
		fprintf (stderr, " (%s)", str);
	fprintf (stderr, "\n  ");

	XGetErrorDatabaseText (dpy, "XlibMessage", "MinorCode", "%d", str, 128);
	fprintf (stderr, str, e->minor_code);
	fprintf (stderr, "\n  ");

	XGetErrorDatabaseText (dpy, "XlibMessage", "ResourceID", "%d", str, 128);
	fprintf (stderr, str, e->resourceid);
	fprintf (stderr, "\n");

	/* abort (); */
#endif

	return 0;
}

int
compCheckForError (Display *dpy)
{
	int e;

	XSync (dpy, FALSE);

	e = errors;
	errors = 0;

	return e;
}

void
addScreenToDisplay (CompScreen  *s)
{
	CompScreen *prev;

	for (prev = display.screens; prev && prev->next; prev = prev->next);

	if (prev)
		prev->next = s;
	else
		display.screens = s;
}

static void
freeDisplay (void)
{
	finiBananaValue (&display.plugin, BananaListString);

	if (display.modMap)
		XFreeModifiermap (display.modMap);

	if (display.screenInfo)
		XFree (display.screenInfo);

	if (display.screenPrivateIndices)
		free (display.screenPrivateIndices);

	if (display.base.privates)
		free (display.base.privates);
}

static Bool
aquireSelection (int         screen,
                 const char  *name,
                 Atom        selection,
                 Window      owner,
                 Time        timestamp)
{
	Display *dpy = display.display;
	Window  root = XRootWindow (dpy, screen);
	XEvent  event;

	XSetSelectionOwner (dpy, selection, owner, timestamp);

	if (XGetSelectionOwner (dpy, selection) != owner)
	{
		compLogMessage ("core", CompLogLevelError,
		                "Could not acquire %s manager "
		                "selection on screen %d display \"%s\"",
		                name, screen, DisplayString (dpy));

		return FALSE;
	}

	/* Send client message indicating that we are now the manager */
	event.xclient.type         = ClientMessage;
	event.xclient.window       = root;
	event.xclient.message_type = display.managerAtom;
	event.xclient.format       = 32;
	event.xclient.data.l[0]    = timestamp;
	event.xclient.data.l[1]    = selection;
	event.xclient.data.l[2]    = 0;
	event.xclient.data.l[3]    = 0;
	event.xclient.data.l[4]    = 0;

	XSendEvent (dpy, root, FALSE, StructureNotifyMask, &event);

	return TRUE;
}

Bool
addDisplay (const char *name)
{
	CompDisplay *d = &display;
	CompPrivate *privates;
	Display     *dpy;
	Window	    focus;
	int         revertTo, i;
	int         compositeMajor, compositeMinor;
	int         fixesMinor;
	int         xkbOpcode;
	int         firstScreen, lastScreen;

	if (displayPrivateLen)
	{
		privates = malloc (displayPrivateLen * sizeof (CompPrivate));
		if (!privates)
		{
			return FALSE;
		}
	}
	else
		privates = 0;

	compObjectInit (&d->base, privates, COMP_OBJECT_TYPE_DISPLAY);

	d->screens = NULL;

	d->watchFdHandle = 0;

	d->screenPrivateIndices = 0;
	d->screenPrivateLen     = 0;

	d->edgeDelayHandle = 0;

	d->modMap = 0;

	for (i = 0; i < CompModNum; i++)
		d->modMask[i] = CompNoMask;

	d->ignoredModMask = LockMask;

	initBananaValue (&d->plugin, BananaListString);

	addItemToBananaList ("core", BananaListString, &d->plugin);

	d->dirtyPluginList = TRUE;

	d->textureFilter = GL_LINEAR;
	d->below         = None;

	d->activeWindow = 0;

	d->autoRaiseHandle = 0;
	d->autoRaiseWindow = None;

	d->display = dpy = XOpenDisplay (name);
	if (!d->display)
	{
		compLogMessage ("core", CompLogLevelFatal,
		                "Couldn't open display %s", XDisplayName (name));
		return FALSE;
	}

	snprintf (d->displayString, 255, "DISPLAY=%s", DisplayString (dpy));

#ifdef DEBUG
	XSynchronize (dpy, TRUE);
#endif

	XSetErrorHandler (errorHandler);

	updateModifierMappings ();

	d->handleEvent       = handleEvent;
	d->handleFusilliEvent = handleFusilliEvent;

	d->fileToImage = fileToImage;
	d->imageToFile = imageToFile;

	d->matchPropertyChanged   = matchPropertyChanged;

	d->supportedAtom         = XInternAtom (dpy, "_NET_SUPPORTED", 0);
	d->supportingWmCheckAtom = XInternAtom (dpy, "_NET_SUPPORTING_WM_CHECK", 0);

	d->utf8StringAtom = XInternAtom (dpy, "UTF8_STRING", 0);

	d->wmNameAtom = XInternAtom (dpy, "_NET_WM_NAME", 0);

	d->winTypeAtom        = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE", 0);
	d->winTypeDesktopAtom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", 0);

	d->winTypeDockAtom    = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DOCK", 0);
	d->winTypeToolbarAtom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_TOOLBAR", 0);

	d->winTypeMenuAtom    = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_MENU", 0);
	d->winTypeUtilAtom    = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_UTILITY", 0);

	d->winTypeSplashAtom  = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_SPLASH", 0);
	d->winTypeDialogAtom  = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DIALOG", 0);
	d->winTypeNormalAtom  = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_NORMAL", 0);

	d->winTypeDropdownMenuAtom =
	        XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", 0);
	d->winTypePopupMenuAtom    =
	        XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", 0);
	d->winTypeTooltipAtom      =
	        XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", 0);
	d->winTypeNotificationAtom =
	        XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", 0);
	d->winTypeComboAtom        =
	        XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_COMBO", 0);
	d->winTypeDndAtom          =
	        XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DND", 0);

	d->winOpacityAtom    = XInternAtom (dpy, "_NET_WM_WINDOW_OPACITY", 0);
	d->winBrightnessAtom = XInternAtom (dpy, "_NET_WM_WINDOW_BRIGHTNESS", 0);
	d->winSaturationAtom = XInternAtom (dpy, "_NET_WM_WINDOW_SATURATION", 0);

	d->winActiveAtom = XInternAtom (dpy, "_NET_ACTIVE_WINDOW", 0);

	d->winDesktopAtom = XInternAtom (dpy, "_NET_WM_DESKTOP", 0);

	d->workareaAtom = XInternAtom (dpy, "_NET_WORKAREA", 0);

	d->desktopViewportAtom  = XInternAtom (dpy, "_NET_DESKTOP_VIEWPORT", 0);
	d->desktopGeometryAtom  = XInternAtom (dpy, "_NET_DESKTOP_GEOMETRY", 0);
	d->currentDesktopAtom   = XInternAtom (dpy, "_NET_CURRENT_DESKTOP", 0);
	d->numberOfDesktopsAtom = XInternAtom (dpy, "_NET_NUMBER_OF_DESKTOPS", 0);

	d->roleAtom        = XInternAtom (d->display, "WM_WINDOW_ROLE", 0);
	d->visibleNameAtom = XInternAtom (d->display, "_NET_WM_VISIBLE_NAME", 0);

	d->winStateAtom	            = XInternAtom (dpy, "_NET_WM_STATE", 0);
	d->winStateModalAtom        =
	        XInternAtom (dpy, "_NET_WM_STATE_MODAL", 0);
	d->winStateStickyAtom       =
	        XInternAtom (dpy, "_NET_WM_STATE_STICKY", 0);
	d->winStateMaximizedVertAtom    =
	        XInternAtom (dpy, "_NET_WM_STATE_MAXIMIZED_VERT", 0);
	d->winStateMaximizedHorzAtom    =
	        XInternAtom (dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", 0);
	d->winStateShadedAtom       =
	        XInternAtom (dpy, "_NET_WM_STATE_SHADED", 0);
	d->winStateSkipTaskbarAtom          =
	        XInternAtom (dpy, "_NET_WM_STATE_SKIP_TASKBAR", 0);
	d->winStateSkipPagerAtom            =
	        XInternAtom (dpy, "_NET_WM_STATE_SKIP_PAGER", 0);
	d->winStateHiddenAtom               =
	        XInternAtom (dpy, "_NET_WM_STATE_HIDDEN", 0);
	d->winStateFullscreenAtom           =
	        XInternAtom (dpy, "_NET_WM_STATE_FULLSCREEN", 0);
	d->winStateAboveAtom                =
	        XInternAtom (dpy, "_NET_WM_STATE_ABOVE", 0);
	d->winStateBelowAtom                =
	        XInternAtom (dpy, "_NET_WM_STATE_BELOW", 0);
	d->winStateDemandsAttentionAtom     =
	        XInternAtom (dpy, "_NET_WM_STATE_DEMANDS_ATTENTION", 0);
	d->winStateDisplayModalAtom         =
	        XInternAtom (dpy, "_NET_WM_STATE_DISPLAY_MODAL", 0);

	d->winActionMoveAtom      = XInternAtom (dpy, "_NET_WM_ACTION_MOVE", 0);
	d->winActionResizeAtom    =
	        XInternAtom (dpy, "_NET_WM_ACTION_RESIZE", 0);
	d->winActionStickAtom     =
	        XInternAtom (dpy, "_NET_WM_ACTION_STICK", 0);
	d->winActionMinimizeAtom  =
	        XInternAtom (dpy, "_NET_WM_ACTION_MINIMIZE", 0);
	d->winActionMaximizeHorzAtom  =
	        XInternAtom (dpy, "_NET_WM_ACTION_MAXIMIZE_HORZ", 0);
	d->winActionMaximizeVertAtom  =
	        XInternAtom (dpy, "_NET_WM_ACTION_MAXIMIZE_VERT", 0);
	d->winActionFullscreenAtom    =
	        XInternAtom (dpy, "_NET_WM_ACTION_FULLSCREEN", 0);
	d->winActionCloseAtom     =
	        XInternAtom (dpy, "_NET_WM_ACTION_CLOSE", 0);
	d->winActionShadeAtom     =
	        XInternAtom (dpy, "_NET_WM_ACTION_SHADE", 0);
	d->winActionChangeDesktopAtom =
	        XInternAtom (dpy, "_NET_WM_ACTION_CHANGE_DESKTOP", 0);
	d->winActionAboveAtom     =
	        XInternAtom (dpy, "_NET_WM_ACTION_ABOVE", 0);
	d->winActionBelowAtom     =
	        XInternAtom (dpy, "_NET_WM_ACTION_BELOW", 0);

	d->wmAllowedActionsAtom = XInternAtom (dpy, "_NET_WM_ALLOWED_ACTIONS", 0);

	d->wmStrutAtom        = XInternAtom (dpy, "_NET_WM_STRUT", 0);
	d->wmStrutPartialAtom = XInternAtom (dpy, "_NET_WM_STRUT_PARTIAL", 0);

	d->wmUserTimeAtom = XInternAtom (dpy, "_NET_WM_USER_TIME", 0);

	d->wmIconAtom         = XInternAtom (dpy,"_NET_WM_ICON", 0);
	d->wmIconGeometryAtom = XInternAtom (dpy, "_NET_WM_ICON_GEOMETRY", 0);

	d->clientListAtom         = XInternAtom (dpy, "_NET_CLIENT_LIST", 0);
	d->clientListStackingAtom =
	        XInternAtom (dpy, "_NET_CLIENT_LIST_STACKING", 0);

	d->frameExtentsAtom = XInternAtom (dpy, "_NET_FRAME_EXTENTS", 0);
	d->frameWindowAtom  = XInternAtom (dpy, "_NET_FRAME_WINDOW", 0);

	d->wmStateAtom	      = XInternAtom (dpy, "WM_STATE", 0);
	d->wmChangeStateAtom  = XInternAtom (dpy, "WM_CHANGE_STATE", 0);
	d->wmProtocolsAtom    = XInternAtom (dpy, "WM_PROTOCOLS", 0);
	d->wmClientLeaderAtom = XInternAtom (dpy, "WM_CLIENT_LEADER", 0);

	d->wmDeleteWindowAtom = XInternAtom (dpy, "WM_DELETE_WINDOW", 0);
	d->wmTakeFocusAtom    = XInternAtom (dpy, "WM_TAKE_FOCUS", 0);
	d->wmPingAtom         = XInternAtom (dpy, "_NET_WM_PING", 0);

	d->wmSyncRequestAtom  = XInternAtom (dpy, "_NET_WM_SYNC_REQUEST", 0);
	d->wmSyncRequestCounterAtom =
	        XInternAtom (dpy, "_NET_WM_SYNC_REQUEST_COUNTER", 0);

	d->wmFullscreenMonitorsAtom =
	        XInternAtom (dpy, "_NET_WM_FULLSCREEN_MONITORS", 0);

	d->closeWindowAtom      = XInternAtom (dpy, "_NET_CLOSE_WINDOW", 0);
	d->wmMoveResizeAtom     = XInternAtom (dpy, "_NET_WM_MOVERESIZE", 0);
	d->moveResizeWindowAtom = XInternAtom (dpy, "_NET_MOVERESIZE_WINDOW", 0);
	d->restackWindowAtom    = XInternAtom (dpy, "_NET_RESTACK_WINDOW", 0);

	d->showingDesktopAtom = XInternAtom (dpy, "_NET_SHOWING_DESKTOP", 0);

	d->xBackgroundAtom[0] = XInternAtom (dpy, "_XSETROOT_ID", 0);
	d->xBackgroundAtom[1] = XInternAtom (dpy, "_XROOTPMAP_ID", 0);

	d->toolkitActionAtom            =
	        XInternAtom (dpy, "_FUSILLI_TOOLKIT_ACTION", 0);
	d->toolkitActionWindowMenuAtom  =
	        XInternAtom (dpy, "_FUSILLI_TOOLKIT_ACTION_WINDOW_MENU", 0);
	d->toolkitActionForceQuitDialogAtom  =
	        XInternAtom (dpy, "_FUSILLI_TOOLKIT_ACTION_FORCE_QUIT_DIALOG", 0);

	d->mwmHintsAtom = XInternAtom (dpy, "_MOTIF_WM_HINTS", 0);

	d->xdndAwareAtom    = XInternAtom (dpy, "XdndAware", 0);
	d->xdndEnterAtom    = XInternAtom (dpy, "XdndEnter", 0);
	d->xdndLeaveAtom    = XInternAtom (dpy, "XdndLeave", 0);
	d->xdndPositionAtom = XInternAtom (dpy, "XdndPosition", 0);
	d->xdndStatusAtom   = XInternAtom (dpy, "XdndStatus", 0);
	d->xdndDropAtom     = XInternAtom (dpy, "XdndDrop", 0);

	d->managerAtom   = XInternAtom (dpy, "MANAGER", 0);
	d->targetsAtom   = XInternAtom (dpy, "TARGETS", 0);
	d->multipleAtom  = XInternAtom (dpy, "MULTIPLE", 0);
	d->timestampAtom = XInternAtom (dpy, "TIMESTAMP", 0);
	d->versionAtom   = XInternAtom (dpy, "VERSION", 0);
	d->atomPairAtom  = XInternAtom (dpy, "ATOM_PAIR", 0);

	d->startupIdAtom = XInternAtom (dpy, "_NET_STARTUP_ID", 0);

	d->snDisplay = sn_display_new (dpy, NULL, NULL);
	if (!d->snDisplay)
		return FALSE;

	d->lastPing = 1;

	if (!XQueryExtension (dpy,
	                      COMPOSITE_NAME,
	                      &d->compositeOpcode,
	                      &d->compositeEvent,
	                      &d->compositeError))
	{
		compLogMessage ("core", CompLogLevelFatal,
		                "No composite extension");
		return FALSE;
	}

	XCompositeQueryVersion (dpy, &compositeMajor, &compositeMinor);
	if (compositeMajor == 0 && compositeMinor < 2)
	{
		compLogMessage ("core", CompLogLevelFatal,
		                "Old composite extension");
		return FALSE;
	}

	if (!XDamageQueryExtension (dpy, &d->damageEvent, &d->damageError))
	{
		compLogMessage ("core", CompLogLevelFatal,
		                "No damage extension");
		return FALSE;
	}

	if (!XSyncQueryExtension (dpy, &d->syncEvent, &d->syncError))
	{
		compLogMessage ("core", CompLogLevelFatal,
		                "No sync extension");
		return FALSE;
	}

	if (!XFixesQueryExtension (dpy, &d->fixesEvent, &d->fixesError))
	{
		compLogMessage ("core", CompLogLevelFatal,
		                "No fixes extension");
		return FALSE;
	}

	XFixesQueryVersion (dpy, &d->fixesVersion, &fixesMinor);
	/*
	if (d->fixesVersion < 5)
	{
		fprintf (stderr, "%s: Need fixes extension version 5 or later "
				 "for client-side cursor\n", programName);
	}
	*/

	d->randrExtension = XRRQueryExtension (dpy,
	                                       &d->randrEvent,
	                                       &d->randrError);

	d->shapeExtension = XShapeQueryExtension (dpy,
	                                          &d->shapeEvent,
	                                          &d->shapeError);

	d->xkbExtension = XkbQueryExtension (dpy,
	                                     &xkbOpcode,
	                                     &d->xkbEvent,
	                                     &d->xkbError,
	                                     NULL, NULL);
	if (d->xkbExtension)
	{
		XkbSelectEvents (dpy,
		                 XkbUseCoreKbd,
		                 XkbBellNotifyMask | XkbStateNotifyMask,
		                 XkbAllEventsMask);
	}
	else
	{
		compLogMessage ("core", CompLogLevelFatal,
		                "No XKB extension");

		d->xkbEvent = d->xkbError = -1;
	}

	d->screenInfo  = NULL;
	d->nScreenInfo = 0;

	d->xineramaExtension = XineramaQueryExtension (dpy,
	                                               &d->xineramaEvent,
	                                               &d->xineramaError);

	if (d->xineramaExtension)
		d->screenInfo = XineramaQueryScreens (dpy, &d->nScreenInfo);

	d->escapeKeyCode = XKeysymToKeycode (dpy, XStringToKeysym ("Escape"));
	d->returnKeyCode = XKeysymToKeycode (dpy, XStringToKeysym ("Return"));

	/* TODO: bailout properly when objectInitPlugins fails */
	assert (objectInitPlugins (&d->base));

	(*core.objectAdd) (&core.base, &d->base);

	if (onlyCurrentScreen)
	{
		firstScreen = DefaultScreen (dpy);
		lastScreen  = DefaultScreen (dpy);
	}
	else
	{
		firstScreen = 0;
		lastScreen  = ScreenCount (dpy) - 1;
	}

	for (i = firstScreen; i <= lastScreen; i++)
	{
		Window               newWmSnOwner = None, newCmSnOwner = None;
		Atom                 wmSnAtom = 0, cmSnAtom = 0;
		Time                 wmSnTimestamp = 0;
		XEvent               event;
		XSetWindowAttributes attr;
		Window               currentWmSnOwner, currentCmSnOwner;
		char                 buf[128];
		Window               rootDummy, childDummy;
		unsigned int         uDummy;
		int                  x, y, dummy;

		sprintf (buf, "WM_S%d", i);
		wmSnAtom = XInternAtom (dpy, buf, 0);

		currentWmSnOwner = XGetSelectionOwner (dpy, wmSnAtom);

		if (currentWmSnOwner != None)
		{
			if (!replaceCurrentWm)
			{
				compLogMessage ("core", CompLogLevelError,
				                "Screen %d on display \"%s\" already "
				                "has a window manager; try using the "
				                "--replace option to replace the current "
				                "window manager.",
				                i, DisplayString (dpy));

				continue;
			}

			XSelectInput (dpy, currentWmSnOwner,
			              StructureNotifyMask);
		}

		sprintf (buf, "_NET_WM_CM_S%d", i);
		cmSnAtom = XInternAtom (dpy, buf, 0);

		currentCmSnOwner = XGetSelectionOwner (dpy, cmSnAtom);

		if (currentCmSnOwner != None)
		{
			if (!replaceCurrentWm)
			{
				compLogMessage ("core", CompLogLevelError,
				                "Screen %d on display \"%s\" already "
				                "has a compositing manager; try using the "
				                "--replace option to replace the current "
				                "compositing manager.",
				                i, DisplayString (dpy));

				continue;
			}
		}

		attr.override_redirect = TRUE;
		attr.event_mask        = PropertyChangeMask;

		newCmSnOwner = newWmSnOwner =
		             XCreateWindow (dpy, XRootWindow (dpy, i),
		                            -100, -100, 1, 1, 0,
		                            CopyFromParent, CopyFromParent,
		                            CopyFromParent,
		                            CWOverrideRedirect | CWEventMask,
		                            &attr);

		XChangeProperty (dpy,
		                 newWmSnOwner,
		                 d->wmNameAtom,
		                 d->utf8StringAtom, 8,
		                 PropModeReplace,
		                 (unsigned char *) PACKAGE,
		                 strlen (PACKAGE));

		XWindowEvent (dpy,
		              newWmSnOwner,
		              PropertyChangeMask,
		              &event);

		wmSnTimestamp = event.xproperty.time;

		if (!aquireSelection (i, "window", wmSnAtom, newWmSnOwner,
		                      wmSnTimestamp))
		{
			XDestroyWindow (dpy, newWmSnOwner);

			continue;
		}

		/* Wait for old window manager to go away */
		if (currentWmSnOwner != None)
		{
			do {
				XWindowEvent (dpy, currentWmSnOwner,
				              StructureNotifyMask, &event);
			} while (event.type != DestroyNotify);
		}

		compCheckForError (dpy);

		XCompositeRedirectSubwindows (dpy, XRootWindow (dpy, i),
		                              CompositeRedirectManual);

		if (compCheckForError (dpy))
		{
			compLogMessage ("core", CompLogLevelError,
			                "Another composite manager is already "
			                "running on screen: %d", i);

			continue;
		}

		if (!aquireSelection (i, "compositing", cmSnAtom,
		                      newCmSnOwner, wmSnTimestamp))
		{
			continue;
		}

		XGrabServer (dpy);

		XSelectInput (dpy, XRootWindow (dpy, i),
		              SubstructureRedirectMask |
		              SubstructureNotifyMask   |
		              StructureNotifyMask      |
		              PropertyChangeMask       |
		              LeaveWindowMask          |
		              EnterWindowMask          |
		              KeyPressMask             |
		              KeyReleaseMask           |
		              ButtonPressMask          |
		              ButtonReleaseMask        |
		              FocusChangeMask          |
		              ExposureMask);

		if (compCheckForError (dpy))
		{
			compLogMessage ("core", CompLogLevelError,
			                "Another window manager is "
			                "already running on screen: %d", i);

			XUngrabServer (dpy);
			continue;
		}

		if (!addScreen (i, newWmSnOwner, wmSnAtom, wmSnTimestamp))
		{
			compLogMessage ("core", CompLogLevelError,
			                "Failed to manage screen: %d", i);
		}

		if (XQueryPointer (dpy, XRootWindow (dpy, i),
		                   &rootDummy, &childDummy,
		                   &x, &y, &dummy, &dummy, &uDummy))
		{
			lastPointerX = pointerX = x;
			lastPointerY = pointerY = y;
		}

		XUngrabServer (dpy);
	}

	if (!d->screens)
	{
		compLogMessage ("core", CompLogLevelFatal,
		                "No manageable screens found on display %s",
		                XDisplayName (name));
		return FALSE;
	}

	const BananaValue *
	option_audible_bell = bananaGetOption (coreBananaIndex, "audible_bell", -1);

	setAudibleBell (option_audible_bell->b);

	XGetInputFocus (dpy, &focus, &revertTo);

	/* move input focus to root window so that we get a FocusIn event when
	   moving it to the default window */
	XSetInputFocus (dpy, d->screens->root, RevertToPointerRoot, CurrentTime);

	if (focus == None || focus == PointerRoot)
	{
		focusDefaultWindow (d->screens);
	}
	else
	{
		CompWindow *w;

		w = findWindowAtDisplay (focus);
		if (w)
		{
			moveInputFocusToWindow (w);
		}
		else
			focusDefaultWindow (d->screens);
	}

	const BananaValue *
	option_ping_delay = bananaGetOption (coreBananaIndex, "ping_delay", -1);

	d->pingHandle =
	      compAddTimeout (option_ping_delay->i,
	                      option_ping_delay->i + 500,
	                      pingTimeout, d);

	const BananaValue *
	option_close_window_key =
	    bananaGetOption (coreBananaIndex,
	                     "close_window_key", -1);

	registerKey (option_close_window_key->s, &d->close_window_key);

	const BananaValue *
	option_raise_window_key =
	    bananaGetOption (coreBananaIndex,
	                     "raise_window_key", -1);

	registerKey (option_raise_window_key->s, &d->raise_window_key);

	const BananaValue *
	option_lower_window_key =
	    bananaGetOption (coreBananaIndex,
	                     "lower_window_key", -1);

	registerKey (option_lower_window_key->s, &d->lower_window_key);

	const BananaValue *
	option_unmaximize_window_key =
	    bananaGetOption (coreBananaIndex,
	                     "unmaximize_window_key", -1);

	registerKey (option_unmaximize_window_key->s, &d->unmaximize_window_key);

	const BananaValue *
	option_minimize_window_key =
	    bananaGetOption (coreBananaIndex,
	                     "minimize_window_key", -1);

	registerKey (option_minimize_window_key->s, &d->minimize_window_key);

	const BananaValue *
	option_maximize_window_key =
	    bananaGetOption (coreBananaIndex,
	                     "maximize_window_key", -1);

	registerKey (option_maximize_window_key->s, &d->maximize_window_key);

	const BananaValue *
	option_maximize_window_horizontally_key =
	    bananaGetOption (coreBananaIndex,
	                     "maximize_window_horizontally_key", -1);

	registerKey (option_maximize_window_horizontally_key->s, 
	             &d->maximize_window_horizontally_key);

	const BananaValue *
	option_maximize_window_vertically_key =
	    bananaGetOption (coreBananaIndex,
	                     "maximize_window_vertically_key", -1);

	registerKey (option_maximize_window_vertically_key->s,
	             &d->maximize_window_vertically_key);

	const BananaValue *
	option_window_menu_key =
	    bananaGetOption (coreBananaIndex,
	                     "window_menu_key", -1);

	registerKey (option_window_menu_key->s,
	             &d->window_menu_key);

	const BananaValue *
	option_show_desktop_key =
	    bananaGetOption (coreBananaIndex,
	                     "show_desktop_key", -1);

	registerKey (option_show_desktop_key->s,
	             &d->show_desktop_key);

	const BananaValue *
	option_toggle_window_maximized_key =
	    bananaGetOption (coreBananaIndex,
	                     "toggle_window_maximized_key", -1);

	registerKey (option_toggle_window_maximized_key->s,
	             &d->toggle_window_maximized_key);

	const BananaValue *
	option_toggle_window_maximized_horizontally_key =
	    bananaGetOption (coreBananaIndex,
	                     "toggle_window_maximized_horizontally_key", -1);

	registerKey (option_toggle_window_maximized_horizontally_key->s,
	             &d->toggle_window_maximized_horizontally_key);

	const BananaValue *
	option_toggle_window_maximized_vertically_key =
	    bananaGetOption (coreBananaIndex,
	                     "toggle_window_maximized_vertically_key", -1);

	registerKey (option_toggle_window_maximized_vertically_key->s,
	             &d->toggle_window_maximized_vertically_key);

	const BananaValue *
	option_toggle_window_shaded_key =
	    bananaGetOption (coreBananaIndex,
	                     "toggle_window_shaded_key", -1);

	registerKey (option_toggle_window_shaded_key->s,
	             &d->toggle_window_shaded_key);

	const BananaValue *
	option_slow_animations_key =
	    bananaGetOption (coreBananaIndex,
	                     "slow_animations_key", -1);

	registerKey (option_slow_animations_key->s,
	             &d->slow_animations_key);

	const BananaValue *
	option_close_window_button =
	    bananaGetOption (coreBananaIndex,
	                     "close_window_button", -1);

	registerButton (option_close_window_button->s,
	                &d->close_window_button);

	const BananaValue *
	option_raise_window_button =
	    bananaGetOption (coreBananaIndex,
	                     "raise_window_button", -1);

	registerButton (option_raise_window_button->s,
	                &d->raise_window_button);

	const BananaValue *
	option_lower_window_button =
	    bananaGetOption (coreBananaIndex,
	                     "lower_window_button", -1);

	registerButton (option_lower_window_button->s,
	                &d->lower_window_button);

	const BananaValue *
	option_minimize_window_button =
	    bananaGetOption (coreBananaIndex,
	                     "minimize_window_button", -1);

	registerButton (option_minimize_window_button->s,
	                &d->minimize_window_button);

	const BananaValue *
	option_window_menu_button =
	    bananaGetOption (coreBananaIndex,
	                     "window_menu_button", -1);

	registerButton (option_window_menu_button->s,
	                &d->window_menu_button);

	const BananaValue *
	option_toggle_window_maximized_button =
	    bananaGetOption (coreBananaIndex,
	                     "toggle_window_maximized_button", -1);

	registerButton (option_toggle_window_maximized_button->s,
	                &d->toggle_window_maximized_button);

	bananaAddChangeNotifyCallBack (coreBananaIndex, displayChangeNotify);
	bananaAddChangeNotifyCallBack (coreBananaIndex, screenChangeNotify);

	return TRUE;
}

void
removeDisplay (void)
{
	while (display.screens)
		removeScreen (display.screens);

	(*core.objectRemove) (&core.base, &display.base);

	objectFiniPlugins (&display.base);

	if (display.edgeDelayHandle)
	{
		void *closure;

		closure = compRemoveTimeout (display.edgeDelayHandle);
		if (closure)
			free (closure);
	}

	if (display.autoRaiseHandle)
		compRemoveTimeout (display.autoRaiseHandle);

	compRemoveTimeout (display.pingHandle);

	if (display.snDisplay)
		sn_display_unref (display.snDisplay);

	XSync (display.display, False);

	XCloseDisplay (display.display);

	freeDisplay ();
}

Time
getCurrentTimeFromDisplay (void)
{
	XEvent event;

	XChangeProperty (display.display, display.screens->grabWindow,
	                 XA_PRIMARY, XA_STRING, 8,
	                 PropModeAppend, NULL, 0);
	XWindowEvent (display.display, display.screens->grabWindow,
	              PropertyChangeMask,
	              &event);

	return event.xproperty.time;
}

CompScreen *
findScreenAtDisplay (Window      root)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
	{
		if (s->root == root)
			return s;
	}

	return 0;
}

CompScreen *
getScreenFromScreenNum (int screenNum)
{
	CompScreen *screen;

	for (screen = display.screens; screen; screen = screen->next)
		if (screen->screenNum == screenNum)
			return screen;

	return NULL;
}

void
forEachWindowOnDisplay (ForEachWindowProc proc,
                        void              *closure)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
		forEachWindowOnScreen (s, proc, closure);
}

CompWindow *
findWindowAtDisplay (Window      id)
{
	CompScreen *s;
	CompWindow *w;

	for (s = display.screens; s; s = s->next)
	{
		w = findWindowAtScreen (s, id);
		if (w)
			return w;
	}

	return 0;
}

CompWindow *
findTopLevelWindowAtDisplay (Window      id)
{
	CompScreen *s;
	CompWindow *w;

	for (s = display.screens; s; s = s->next)
	{
		w = findTopLevelWindowAtScreen (s, id);
		if (w)
			return w;
	}

	return 0;
}

static CompScreen *
findScreenForSelection (Window       owner,
                        Atom         selection)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
	{
		if (s->wmSnSelectionWindow == owner && s->wmSnAtom == selection)
			return s;
	}

	return NULL;
}

/* from fvwm2, Copyright Matthias Clasen, Dominik Vogt */
static Bool
convertProperty (CompScreen  *screen,
                 Window      w,
                 Atom        target,
                 Atom        property)
{

#define N_TARGETS 4

	Atom conversionTargets[N_TARGETS];
	long icccmVersion[] = { 2, 0 };

	conversionTargets[0] = display.targetsAtom;
	conversionTargets[1] = display.multipleAtom;
	conversionTargets[2] = display.timestampAtom;
	conversionTargets[3] = display.versionAtom;

	if (target == display.targetsAtom)
		XChangeProperty (display.display, w, property,
		                 XA_ATOM, 32, PropModeReplace,
		                 (unsigned char *) conversionTargets, N_TARGETS);
	else if (target == display.timestampAtom)
		XChangeProperty (display.display, w, property,
		                 XA_INTEGER, 32, PropModeReplace,
		                 (unsigned char *) &screen->wmSnTimestamp, 1);
	else if (target == display.versionAtom)
		XChangeProperty (display.display, w, property,
		                 XA_INTEGER, 32, PropModeReplace,
		                 (unsigned char *) icccmVersion, 2);
	else
		return FALSE;

	/* Be sure the PropertyNotify has arrived so we
	 * can send SelectionNotify
	 */
	XSync (display.display, FALSE);

	return TRUE;
}

/* from fvwm2, Copyright Matthias Clasen, Dominik Vogt */
void
handleSelectionRequest (XEvent      *event)
{
	XSelectionEvent reply;
	CompScreen      *screen;

	screen = findScreenForSelection (event->xselectionrequest.owner,
	                                 event->xselectionrequest.selection);
	if (!screen)
		return;

	reply.type      = SelectionNotify;
	reply.display   = display.display;
	reply.requestor = event->xselectionrequest.requestor;
	reply.selection = event->xselectionrequest.selection;
	reply.target    = event->xselectionrequest.target;
	reply.property  = None;
	reply.time      = event->xselectionrequest.time;

	if (event->xselectionrequest.target == display.multipleAtom)
	{
		if (event->xselectionrequest.property != None)
		{
			Atom    type, *adata;
			int     i, format;
			unsigned long num, rest;
			unsigned char *data;

			if (XGetWindowProperty (display.display,
			                        event->xselectionrequest.requestor,
			                        event->xselectionrequest.property,
			                        0, 256, FALSE,
			                        display.atomPairAtom,
			                        &type, &format, &num, &rest,
			                        &data) != Success)
				return;

			/* FIXME: to be 100% correct, should deal with rest > 0,
			 * but since we have 4 possible targets, we will hardly ever
			 * meet multiple requests with a length > 8
			 */
			adata = (Atom *) data;
			i = 0;
			while (i < (int) num)
			{
				if (!convertProperty (screen,
				                      event->xselectionrequest.requestor,
				                      adata[i], adata[i + 1]))
					adata[i + 1] = None;

				i += 2;
			}

			XChangeProperty (display.display,
			                 event->xselectionrequest.requestor,
			                 event->xselectionrequest.property,
			                 display.atomPairAtom,
			                 32, PropModeReplace, data, num);

			if (data)
				XFree (data);
		}
	}
	else
	{
		if (event->xselectionrequest.property == None)
			event->xselectionrequest.property = event->xselectionrequest.target;

		if (convertProperty (screen,
		                     event->xselectionrequest.requestor,
		                     event->xselectionrequest.target,
		                     event->xselectionrequest.property))
			reply.property = event->xselectionrequest.property;
	}

	XSendEvent (display.display,
	            event->xselectionrequest.requestor,
	            FALSE, 0L, (XEvent *) &reply);
}

void
handleSelectionClear (XEvent      *event)
{
	/* We need to unmanage the screen on which we lost the selection */
	CompScreen *screen;

	screen = findScreenForSelection (event->xselectionclear.window,
	                                 event->xselectionclear.selection);

	if (screen)
		shutDown = TRUE;
}

void
warpPointer (CompScreen *s,
             int        dx,
             int        dy)
{
	XEvent      event;

	pointerX += dx;
	pointerY += dy;

	if (pointerX >= s->width)
		pointerX = s->width - 1;
	else if (pointerX < 0)
		pointerX = 0;

	if (pointerY >= s->height)
		pointerY = s->height - 1;
	else if (pointerY < 0)
		pointerY = 0;

	XWarpPointer (display.display,
	              None, s->root,
	              0, 0, 0, 0,
	              pointerX, pointerY);

	XSync (display.display, FALSE);

	while (XCheckMaskEvent (display.display,
	                        LeaveWindowMask |
	                        EnterWindowMask |
	                        PointerMotionMask,
	                        &event));

	if (!inHandleEvent)
	{
		lastPointerX = pointerX;
		lastPointerY = pointerY;
	}
}

Bool
addDisplayKeyBinding (CompKeyBinding *key)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
		if (!addScreenKeyBinding (s, key))
			break;

	if (s)
	{
		CompScreen *failed = s;

		for (s = display.screens; s && s != failed; s = s->next)
			removeScreenKeyBinding (s, key);

		return FALSE;
	}

	return TRUE;
}

Bool
addDisplayButtonBinding (CompButtonBinding *button)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
		if (!addScreenButtonBinding (s, button))
			break;

	if (s)
	{
		CompScreen *failed = s;

		for (s = display.screens; s && s != failed; s = s->next)
			removeScreenButtonBinding (s, button);

		return FALSE;
	}

	return TRUE;
}

void
removeDisplayKeyBinding (CompKeyBinding *key)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
		removeScreenKeyBinding (s, key);
}

void
removeDisplayButtonBinding (CompButtonBinding *button)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
		removeScreenButtonBinding (s, button);
}

void
registerButton (const char        *s,
                CompButtonBinding *button)
{
	if (strstr(s, "<ClickOnDesktop>") == s)
	{
		button->clickOnDesktop = TRUE;
		s += strlen ("<ClickOnDesktop>");
	}
	else
		button->clickOnDesktop = FALSE;

	if (s)
	{
		if (stringToButtonBinding (s, button))
			button->active = addDisplayButtonBinding (button);
		else
			button->active = FALSE;
	}
	else
		button->active = FALSE;
}

void
registerKey (const char        *s,
             CompKeyBinding    *key)
{
	if (s)
	{
		if (stringToKeyBinding (s, key))
			key->active = addDisplayKeyBinding (key);
		else
			key->active = FALSE;
	}
	else
		key->active = FALSE;
}

void 
updateButton (const char        *s,
              CompButtonBinding *button)
{
	if (strstr(s, "<ClickOnDesktop>") == s)
	{
		button->clickOnDesktop = TRUE;
		s += strlen ("<ClickOnDesktop>");
	}
	else
		button->clickOnDesktop = FALSE;

	if (button->active)
		removeDisplayButtonBinding (button);

	if (stringToButtonBinding (s, button))
		button->active = addDisplayButtonBinding (button);
	else
		button->active = FALSE;
}

void
updateKey (const char     *s,
           CompKeyBinding *key)
{
	if (key->active)
		removeDisplayKeyBinding (key);

	if (stringToKeyBinding (s, key))
		key->active = addDisplayKeyBinding (key);
	else
		key->active = FALSE;
}

Bool
isKeyPressEvent (XEvent         *event,
                 CompKeyBinding *key)
{
	if (key->active)
	{
		unsigned int modMask = REAL_MOD_MASK & ~display.ignoredModMask;
		unsigned int bindMods = virtualToRealModMask (key->modifiers);

		if (key->keycode == event->xkey.keycode    &&
			(bindMods & modMask) == (event->xkey.state & modMask))
			return TRUE;
	}

	return FALSE;
}

Bool
isButtonPressEvent (XEvent            *event,
                    CompButtonBinding *button)
{
	if (button->active)
	{
		if (button->clickOnDesktop)
		{
			/* copied from GET_DATA macro from vpswitch.c
			 * Copyright (c) 2007 Dennis Kasprzyk <onestone@opencompositing.org>
			 * Copyright (c) 2007 Robert Carr <racarr@opencompositing.org>
			 * Copyright (c) 2007 Danny Baumann <maniac@opencompositing.org>
			 * Copyright (c) 2007 Michael Vogt <mvo@ubuntu.com>
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
			CompScreen *s;
			CompWindow *w;
			Window     xid;

			xid = event->xbutton.root;
			s = findScreenAtDisplay (xid);
			if (!s)
				return FALSE;

			xid = event->xbutton.window;
			if (xid == s->grabWindow)
				xid = display.below;

			w = findWindowAtDisplay (xid);
			if ((!w || (w->type & CompWindowTypeDesktopMask) == 0) &&
			    xid != s->root)
				return FALSE;
			/* end copying */
		}

		unsigned int modMask = REAL_MOD_MASK & ~display.ignoredModMask;
		unsigned int bindMods = virtualToRealModMask (button->modifiers);

		if (button->button == event->xbutton.button    &&
			(bindMods & modMask) == (event->xbutton.state & modMask))
			return TRUE;
	}

	return FALSE;
}

void
clearTargetOutput (unsigned int mask)
{
	if (targetScreen)
		clearScreenOutput (targetScreen,
		                   targetOutput,
		                   mask);
}

#define HOME_IMAGEDIR ".fusilli/images"

Bool
readImageFromFile (const char  *name,
                   int         *width,
                   int	       *height,
                   void	       **data)
{
	Bool status;
	int  stride;

	status = (*display.fileToImage) (NULL, name, width, height,
	                                  &stride, data);
	if (!status)
	{
		char *home;

		home = getenv ("HOME");
		if (home)
		{
			char *path;

			path = malloc (strlen (home) + strlen (HOME_IMAGEDIR) + 2);
			if (path)
			{
				sprintf (path, "%s/%s", home, HOME_IMAGEDIR);
				status = (*display.fileToImage) (path, name,
				                                  width, height, &stride,
				                                  data);

				free (path);

				if (status)
					return TRUE;
			}
		}

		status = (*display.fileToImage) (IMAGEDIR, name,
		                                  width, height, &stride, data);
	}

	return status;
}

Bool
writeImageToFile (const char  *path,
                  const char  *name,
                  const char  *format,
                  int         width,
                  int         height,
                  void        *data)
{
	return (*display.imageToFile) (path, name, format, width, height,
	                                width * 4, data);
}

Bool
fileToImage (const char  *path,
             const char  *name,
             int         *width,
             int         *height,
             int         *stride,
             void        **data)
{
	if (pngFileToImage (path, name, width, height, stride, data))
		return TRUE;
	else if (JPEGFileToImage (path, name, width, height, stride, data))
		return TRUE;
	else
		return FALSE;
}

Bool
imageToFile (const char  *path,
             const char  *name,
             const char  *format,
             int         width,
             int         height,
             int         stride,
             void        *data)
{
	if (strcasecmp (format, "png") == 0)
		return pngImageToFile (path, name, width, height, stride, data);
	else if (strcasecmp (format, "jpeg") == 0 || 
	         strcasecmp (format, "jpg") == 0)
		return JPEGImageToFile (path, name, width, height, stride, data);
	else
		return FALSE;
}

CompCursor *
findCursorAtDisplay (void)
{
	CompScreen *s;

	for (s = display.screens; s; s = s->next)
		if (s->cursors)
			return s->cursors;

	return NULL;
}
