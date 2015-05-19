/*
 * Copyright © 2009 Danny Baumann
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Danny Baumann not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Danny Baumann makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * DANNY BAUMANN DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL DENNIS KASPRZYK BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: Danny Baumann <dannybaumann@web.de>
 *         Michail Bitzes <noodlylight@gmail.com>
 */
#include <string.h>

#include <fusilli-core.h>

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _MateDisplay {
	Atom panelActionAtom;
	Atom panelMainMenuAtom;
	Atom panelRunDialogAtom;
	HandleEventProc handleEvent;

	CompKeyBinding main_menu_key;
	CompKeyBinding run_key;
	CompKeyBinding run_command_screenshot_key;
	CompKeyBinding run_command_window_screenshot_key;
	CompKeyBinding run_command_terminal_key;
} MateDisplay;

#define GET_MATE_DISPLAY(d) \
        ((MateDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define MATE_DISPLAY(d) \
        MateDisplay *md = GET_MATE_DISPLAY (d)

static void
mateChangeNotify (const char        *optionName,
                  BananaType        optionType,
                  const BananaValue *optionValue,
                  int               screenNum)
{
	MATE_DISPLAY (&display);

	if (strcasecmp (optionName, "main_menu_key") == 0)
		updateKey (optionValue->s, &md->main_menu_key);

	else if (strcasecmp (optionName, "run_key") == 0)
		updateKey (optionValue->s, &md->run_key);

	else if (strcasecmp (optionName, "run_command_screenshot_key") == 0)
		updateKey (optionValue->s, &md->run_command_screenshot_key);

	else if (strcasecmp (optionName, "run_command_window_screenshot_key") == 0)
		updateKey (optionValue->s, &md->run_command_screenshot_key);

	else if (strcasecmp (optionName, "run_command_terminal_key") == 0)
		updateKey (optionValue->s, &md->run_command_terminal_key);
}

static Bool
runDispatch (Window     xid,
             const char *command)
{
	CompScreen *s;

	s   = findScreenAtDisplay (xid);

	if (s && command != NULL)
		runCommand (s, command);

	return TRUE;
}

static void
panelAction (Window             xid,
             Time               time,
             Atom               actionAtom)
{
	CompScreen *s;
	XEvent     event;

	MATE_DISPLAY (&display);

	s   = findScreenAtDisplay (xid);

	if (!s)
		return;

	/* we need to ungrab the keyboard here, otherwise the panel main
	   menu won't popup as it wants to grab the keyboard itself */
	XUngrabKeyboard (display.display, time);

	event.type                 = ClientMessage;
	event.xclient.window       = s->root;
	event.xclient.message_type = md->panelActionAtom;
	event.xclient.format       = 32;
	event.xclient.data.l[0]    = actionAtom;
	event.xclient.data.l[1]    = time;
	event.xclient.data.l[2]    = 0;
	event.xclient.data.l[3]    = 0;
	event.xclient.data.l[4]    = 0;

	XSendEvent (display.display, s->root, FALSE, StructureNotifyMask,
	            &event);
}

static Bool
showMainMenu (Window xid,
              Time   time)
{
	MATE_DISPLAY (&display);

	panelAction (xid, time, md->panelMainMenuAtom);

	return TRUE;
}

static Bool
showRunDialog (Window xid,
               Time   time)
{
	MATE_DISPLAY (&display);

	panelAction (xid, time, md->panelRunDialogAtom);

	return TRUE;
}

static void
mateHandleEvent (XEvent *event)
{
	MATE_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &md->main_menu_key))
		{
			showMainMenu (event->xkey.root, event->xkey.time);
		}
		else if (isKeyPressEvent (event, &md->run_key))
		{
			showRunDialog (event->xkey.root, event->xkey.time);
		}
		else if (isKeyPressEvent (event, &md->run_command_screenshot_key))
		{
			const BananaValue *
			option_command_screenshot = bananaGetOption (
			      bananaIndex, "command_screenshot", -1);

			runDispatch (event->xkey.root, option_command_screenshot->s);
		}
		else if (isKeyPressEvent (event, &md->run_command_window_screenshot_key))
		{
			const BananaValue *
			option_command_window_screenshot = 
			     bananaGetOption (bananaIndex, "command_window_screenshot", -1);

			runDispatch (event->xkey.root, option_command_window_screenshot->s);
		}
		else if (isKeyPressEvent (event, &md->run_command_terminal_key))
		{
			const BananaValue *
			option_command_terminal = bananaGetOption (
			      bananaIndex, "command_terminal", -1);

			runDispatch (event->xkey.root, option_command_terminal->s);
		}

	default:
		break;
	}

	UNWRAP (md, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (md, &display, handleEvent, mateHandleEvent);
}

static CompBool
mateInitDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	MateDisplay *md;

	md = malloc (sizeof (MateDisplay));
	if (!md)
		return FALSE;

	md->panelActionAtom =
	    XInternAtom (d->display, "_MATE_PANEL_ACTION", FALSE);
	md->panelMainMenuAtom =
	    XInternAtom (d->display, "_MATE_PANEL_ACTION_MAIN_MENU", FALSE);
	md->panelRunDialogAtom =
	    XInternAtom (d->display, "_MATE_PANEL_ACTION_RUN_DIALOG", FALSE);

	const BananaValue *
	option_main_menu_key = bananaGetOption (bananaIndex, "main_menu_key", -1);

	const BananaValue *
	option_run_key = bananaGetOption (bananaIndex, "run_key", -1);

	const BananaValue *
	option_run_command_screenshot_key = 
	     bananaGetOption (bananaIndex, "run_command_screenshot_key", -1);

	const BananaValue *
	option_run_command_window_screenshot_key = 
	     bananaGetOption (bananaIndex, "run_command_window_screenshot_key", -1);

	const BananaValue *
	option_run_command_terminal_key = 
	     bananaGetOption (bananaIndex, "run_command_terminal_key", -1);

	registerKey (option_main_menu_key->s, &md->main_menu_key);

	registerKey (option_run_key->s, &md->run_key);

	registerKey (option_run_command_screenshot_key->s,
	             &md->run_command_screenshot_key);

	registerKey (option_run_command_window_screenshot_key->s,
	             &md->run_command_window_screenshot_key);

	registerKey (option_run_command_terminal_key->s,
	             &md->run_command_terminal_key);

	WRAP (md, d, handleEvent, mateHandleEvent);

	d->privates[displayPrivateIndex].ptr = md;

	return TRUE;
}

static void
mateFiniDisplay (CompPlugin  *p,
                 CompDisplay *d)
{
	MATE_DISPLAY (d);

	UNWRAP (md, d, handleEvent);

	free (md);
}

static Bool
mateInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("matecompat", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("matecompat");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, mateChangeNotify);

	return TRUE;
}

static void
mateFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable mateVTable = {
	"matecompat",
	mateInit,
	mateFini,
	mateInitDisplay,
	mateFiniDisplay,
	NULL, /* mateInitScreen */
	NULL, /* mateFiniScreen */
	NULL, /* mateInitWindow */
	NULL  /* mateFiniWindow */
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &mateVTable;
}
