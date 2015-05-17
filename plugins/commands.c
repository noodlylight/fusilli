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

#define NUM_COMMANDS 10

static int bananaIndex;

static int displayPrivateIndex;

typedef struct _CommandsDisplay {
	HandleEventProc handleEvent;

	CompKeyBinding    run_command_key[NUM_COMMANDS];
	CompButtonBinding run_command_button[NUM_COMMANDS];
} CommandsDisplay;

#define GET_COMMANDS_DISPLAY(d) \
        ((CommandsDisplay *) (d)->privates[displayPrivateIndex].ptr)

#define COMMANDS_DISPLAY(d) \
        CommandsDisplay *cd = GET_COMMANDS_DISPLAY (d)

static void
commandsChangeNotify (const char        *optionName,
                      BananaType        optionType,
                      const BananaValue *optionValue,
                      int               screenNum)
{
	COMMANDS_DISPLAY (&display);

	if (strstr (optionName, "run_command_key"))
	{
		int i = strlen ("run_command_key");
		int index = atoi (&optionName[i]);

		updateKey (optionValue->s, &cd->run_command_key[index]);
	}
	else if (strstr (optionName, "run_command_button"))
	{
		int i = strlen ("run_command_button");
		int index = atoi (&optionName[i]);

		updateButton (optionValue->s, &cd->run_command_button[index]);
	}
	//else if (strstr (optionName, "run_command_edge"))
	//{
	//	int i = strlen ("run_command_edge");
	//	int index = atoi (&optionName[i]);
	//}
}

static void
commandsHandleEvent (XEvent *event)
{
	CompScreen *s;

	COMMANDS_DISPLAY (&display);

	int i;

	switch (event->type) {
	case KeyPress:
		s = findScreenAtDisplay (event->xkey.root);
		for (i = 0; i <= NUM_COMMANDS - 1; i++)
		{
			if (isKeyPressEvent (event, &cd->run_command_key[i]))
			{
				char optionName[50];

				sprintf (optionName, "command%d", i);

				const BananaValue *
				c = bananaGetOption (bananaIndex, optionName, -1);

				runCommand (s, c->s);
			}
		}
		break;
	case ButtonPress:
		s = findScreenAtDisplay (event->xbutton.root);
		for (i = 0; i <= NUM_COMMANDS - 1; i++)
		{
			if (isButtonPressEvent (event, &cd->run_command_button[i]))
			{
				char optionName[50];

				sprintf (optionName, "command%d", i);

				const BananaValue *
				c = bananaGetOption (bananaIndex, optionName, -1);

				runCommand (s, c->s);

				//prevent click from reaching other apps
				//in compiz, it was in event.c
				if (!display.screens->maxGrab)
					XAllowEvents (display.display, AsyncPointer,
					              event->xbutton.time);
			}
		}

		break;

	default:
		break;
	}

	UNWRAP (cd, &display, handleEvent);
	(*display.handleEvent) (event);
	WRAP (cd, &display, handleEvent, commandsHandleEvent);
}

static CompBool
commandsInitDisplay (CompPlugin  *p,
                     CompDisplay *d)
{
	CommandsDisplay *cd;

	cd = malloc (sizeof (CommandsDisplay));
	if (!cd)
		return FALSE;

	WRAP (cd, d, handleEvent, commandsHandleEvent);

	int i;
	for (i = 0; i <= NUM_COMMANDS - 1; i++)
	{
		char optionName[50];
		const BananaValue *option;

		sprintf (optionName, "run_command_key%d", i);
		option = bananaGetOption (bananaIndex, optionName, -1);
		registerKey (option->s, &cd->run_command_key[i]);

		sprintf (optionName, "run_command_button%d", i);
		option = bananaGetOption (bananaIndex, optionName, -1);
		registerButton (option->s, &cd->run_command_button[i]);
	}

	d->privates[displayPrivateIndex].ptr = cd;

	return TRUE;
}

static void
commandsFiniDisplay (CompPlugin  *p,
                     CompDisplay *d)
{
	COMMANDS_DISPLAY (d);

	UNWRAP (cd, d, handleEvent);

	free (cd);
}

static Bool
commandsInit (CompPlugin *p)
{
	if (getCoreABI() != CORE_ABIVERSION)
	{
		compLogMessage ("commands", CompLogLevelError,
		                "ABI mismatch\n"
		                "\tPlugin was compiled with ABI: %d\n"
		                "\tFusilli Core was compiled with ABI: %d\n",
		                CORE_ABIVERSION, getCoreABI());

		return FALSE;
	}

	displayPrivateIndex = allocateDisplayPrivateIndex ();

	if (displayPrivateIndex < 0)
		return FALSE;

	bananaIndex = bananaLoadPlugin ("commands");

	if (bananaIndex == -1)
		return FALSE;

	bananaAddChangeNotifyCallBack (bananaIndex, commandsChangeNotify);

	return TRUE;
}

static void
commandsFini (CompPlugin *p)
{
	freeDisplayPrivateIndex (displayPrivateIndex);

	bananaUnloadPlugin (bananaIndex);
}

static CompPluginVTable commandsVTable = {
	"commands",
	commandsInit,
	commandsFini,
	commandsInitDisplay,
	commandsFiniDisplay,
	NULL, /* commandsInitScreen */
	NULL, /* commandsFiniScreen */
	NULL, /* commandsInitWindow */
	NULL  /* commandsFiniWindow */
};

CompPluginVTable *
getCompPluginInfo20141205 (void)
{
	return &commandsVTable;
}
