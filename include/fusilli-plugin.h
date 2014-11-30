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

#ifndef _FUSILLI_PLUGIN_H
#define _FUSILLI_PLUGIN_H

#include <fusilli.h>

#ifdef  __cplusplus
extern "C" {
#endif

/* pluginInit, pluginFini */
typedef CompBool
(*InitPluginProc) (CompPlugin *plugin);

typedef void
(*FiniPluginProc) (CompPlugin *plugin);

/* pluginInitCore, pluginFiniCore */
typedef CompBool 
(*InitPluginCoreProc) (CompPlugin *p,
                       CompCore   *c);

typedef void 
(*FiniPluginCoreProc) (CompPlugin *p,
                       CompCore   *c);

/* pluginInitDisplay, pluginFiniDisplay */
typedef CompBool 
(*InitPluginDisplayProc) (CompPlugin  *p,
                          CompDisplay *d);

typedef void
(*FiniPluginDisplayProc) (CompPlugin  *p,
                          CompDisplay *d);

/* pluginInitScreen, pluginFiniScreen */
typedef CompBool
(*InitPluginScreenProc) (CompPlugin *p,
                         CompScreen *s);

typedef void
(*FiniPluginScreenProc) (CompPlugin *p,
                         CompScreen *s);

/* pluginInitWindow, pluginFiniWindow */
typedef CompBool
(*InitPluginWindowProc) (CompPlugin *p,
                         CompWindow *w);

typedef void
(*FiniPluginWindowProc) (CompPlugin *p,
                         CompWindow *w);

typedef struct _CompPluginVTable {
	const char *name;

	InitPluginProc init;
	FiniPluginProc fini;

	InitPluginCoreProc initCore;
	FiniPluginCoreProc finiCore;

	InitPluginDisplayProc initDisplay;
	FiniPluginDisplayProc finiDisplay;

	InitPluginScreenProc initScreen;
	FiniPluginScreenProc finiScreen;

	InitPluginWindowProc initWindow;
	FiniPluginWindowProc finiWindow;
} CompPluginVTable;

CompPluginVTable *
getCompPluginInfo20141130 (void);

#ifdef  __cplusplus
}
#endif

#endif
