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

static CompKeyBinding main_menu_key;
static CompKeyBinding run_key;
static CompKeyBinding run_command_screenshot_key;
static CompKeyBinding run_command_window_screenshot_key;
static CompKeyBinding run_command_terminal_key;

typedef struct _MateDisplay {
	Atom panelActionAtom;
	Atom panelMainMenuAtom;
	Atom panelRunDialogAtom;
	HandleEventProc handleEvent;
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
	if (strcasecmp (optionName, "main_menu_key") == 0)
		updateKey (optionValue->s, &main_menu_key);

	else if (strcasecmp (optionName, "run_key") == 0)
		updateKey (optionValue->s, &run_key);

	else if (strcasecmp (optionName, "run_command_screenshot_key") == 0)
		updateKey (optionValue->s, &run_command_screenshot_key);

	else if (strcasecmp (optionName, "run_command_window_screenshot_key") == 0)
		updateKey (optionValue->s, &run_command_screenshot_key);

	else if (strcasecmp (optionName, "run_command_terminal_key") == 0)
		updateKey (optionValue->s, &run_command_terminal_key);
}

static Bool
runDispatch (BananaArgument     *arg,
             int                nArg)
{
	CompScreen *s;
	Window     xid;

	BananaValue *root = getArgNamed ("root", arg, nArg);

	if (root != NULL)
		xid = root->i;
	else
		xid = 0;

	s   = findScreenAtDisplay (xid);

	if (s)
	{
		BananaValue *command = getArgNamed ("command", arg, nArg);

		if (command != NULL && command->s != NULL)
			runCommand (s, command->s);
	}

	return TRUE;
}

static void
panelAction (BananaArgument     *arg,
             int                nArg,
             Atom               actionAtom)
{
	Window     xid;
	CompScreen *s;
	XEvent     event;
	Time       time;

	MATE_DISPLAY (&display);

	BananaValue *root = getArgNamed ("root", arg, nArg);

	if (root != NULL)
		xid = root->i;
	else
		xid = 0;

	s   = findScreenAtDisplay (xid);

	if (!s)
		return;

	BananaValue *arg_time = getArgNamed ("time", arg, nArg);

	if (arg_time != NULL)
		time = CurrentTime;
	else
		time = 0;

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
showMainMenu (BananaArgument     *arg,
              int                nArg)
{
	MATE_DISPLAY (&display);

	panelAction (arg, nArg, md->panelMainMenuAtom);

	return TRUE;
}

static Bool
showRunDialog (BananaArgument     *arg,
               int                nArg)
{
	MATE_DISPLAY (&display);

	panelAction (arg, nArg, md->panelRunDialogAtom);

	return TRUE;
}

static void
mateHandleEvent (XEvent      *event)
{
	MATE_DISPLAY (&display);

	switch (event->type) {
	case KeyPress:
		if (isKeyPressEvent (event, &main_menu_key))
		{
			BananaArgument arg[2];

			arg[0].name = "root";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xkey.root;

			arg[1].name = "time";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xkey.time;

			showMainMenu (&arg[0], 2);
		}
		else if (isKeyPressEvent (event, &run_key))
		{
			BananaArgument arg[2];

			arg[0].name = "root";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xkey.root;

			arg[1].name = "time";
			arg[1].type = BananaInt;
			arg[1].value.i = event->xkey.time;

			showRunDialog (&arg[0], 2);
		}
		else if (isKeyPressEvent (event, &run_command_screenshot_key))
		{
			BananaArgument arg[2];

			arg[0].name = "root";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xkey.root;

			const BananaValue *
			option_command_screenshot = bananaGetOption (
			      bananaIndex, "command_screenshot", -1);

			arg[1].name = "command";
			arg[1].type = BananaString;
			arg[1].value.s = option_command_screenshot->s;

			runDispatch (&arg[0], 2);
		}
		else if (isKeyPressEvent (event, &run_command_window_screenshot_key))
		{
			BananaArgument arg[2];

			arg[0].name = "root";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xkey.root;

			const BananaValue *
			option_command_window_screenshot = 
			     bananaGetOption (bananaIndex, "command_window_screenshot", -1);

			arg[1].name = "command";
			arg[1].type = BananaString;
			arg[1].value.s = option_command_window_screenshot->s;

			runDispatch (&arg[0], 2);
		}
		else if (isKeyPressEvent (event, &run_command_terminal_key))
		{
			BananaArgument arg[2];

			arg[0].name = "root";
			arg[0].type = BananaInt;
			arg[0].value.i = event->xkey.root;

			const BananaValue *
			option_command_terminal = bananaGetOption (
			      bananaIndex, "command_terminal", -1);

			arg[1].name = "command";
			arg[1].type = BananaString;
			arg[1].value.s = option_command_terminal->s;

			runDispatch (&arg[0], 2);
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

	registerKey (option_main_menu_key->s, &main_menu_key);

	registerKey (option_run_key->s, &run_key);

	registerKey (option_run_command_screenshot_key->s,
	             &run_command_screenshot_key);

	registerKey (option_run_command_window_screenshot_key->s,
	             &run_command_window_screenshot_key);

	registerKey (option_run_command_terminal_key->s,
	             &run_command_terminal_key);

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
