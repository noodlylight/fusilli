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

#ifndef _FUSILLI_H
#define _FUSILLI_H

#ifdef  __cplusplus
extern "C" {
#endif

typedef int CompBool;
typedef int CompTimeoutHandle;
typedef int CompWatchFdHandle;

typedef union _CompOptionValue CompOptionValue;

typedef struct _CompCore     CompCore;
typedef struct _CompDisplay  CompDisplay;
typedef struct _CompPlugin   CompPlugin;
typedef struct _CompScreen   CompScreen;
typedef struct _CompWindow   CompWindow;

typedef CompBool (*CallBackProc) (void *closure);

CompTimeoutHandle
compAddTimeout (int          minTime,
                int          maxTime,
                CallBackProc callBack,
                void         *closure);

void *
compRemoveTimeout (CompTimeoutHandle handle);

CompWatchFdHandle
compAddWatchFd (int          fd,
                short int    events,
                CallBackProc callBack,
                void         *closure);

void
compRemoveWatchFd (CompWatchFdHandle handle);

short int
compWatchFdEvents (CompWatchFdHandle handle);


#ifdef  __cplusplus
}
#endif

#endif
