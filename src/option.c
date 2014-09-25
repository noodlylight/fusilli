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
 */

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>

#include <fusilli-core.h>

struct _Modifier {
	char *name;
	int  modifier;
} modifiers[] = {
	{ "<Shift>",      ShiftMask          },
	{ "<Control>",    ControlMask        },
	{ "<Mod1>",       Mod1Mask           },
	{ "<Mod2>",       Mod2Mask           },
	{ "<Mod3>",       Mod3Mask           },
	{ "<Mod4>",       Mod4Mask           },
	{ "<Mod5>",       Mod5Mask           },
	{ "<Alt>",        CompAltMask        },
	{ "<Meta>",       CompMetaMask       },
	{ "<Super>",      CompSuperMask      },
	{ "<Hyper>",      CompHyperMask      },
	{ "<ModeSwitch>", CompModeSwitchMask }
};

#define N_MODIFIERS (sizeof (modifiers) / sizeof (struct _Modifier))

struct _Edge {
	char *name;
	char *modifierName;
} edges[] = {
	{ "Left",        "<LeftEdge>"        },
	{ "Right",       "<RightEdge>"       },
	{ "Top",         "<TopEdge>"         },
	{ "Bottom",      "<BottomEdge>"      },
	{ "TopLeft",     "<TopLeftEdge>"     },
	{ "TopRight",    "<TopRightEdge>"    },
	{ "BottomLeft",  "<BottomLeftEdge>"  },
	{ "BottomRight", "<BottomRightEdge>" }
};

static char *
stringAppend (char       *s,
              const char *a)
{
	char *r;
	int  len;

	len = strlen (a);

	if (s)
		len += strlen (s);

	r = malloc (len + 1);
	if (r)
	{
		if (s)
		{
			sprintf (r, "%s%s", s, a);
			free (s);
		}
		else
		{
			sprintf (r, "%s", a);
		}

		s = r;
	}

	return s;
}

static char *
modifiersToString (CompDisplay  *d,
                   unsigned int modMask)
{
	char *binding = NULL;
	int  i;

	for (i = 0; i < N_MODIFIERS; i++)
	{
		if (modMask & modifiers[i].modifier)
			binding = stringAppend (binding, modifiers[i].name);
	}

	return binding;
}

static char *
edgeMaskToBindingString (CompDisplay  *d,
                         unsigned int edgeMask)
{
	char *binding = NULL;
	int  i;

	for (i = 0; i < SCREEN_EDGE_NUM; i++)
		if (edgeMask & (1 << i))
			binding = stringAppend (binding, edges[i].modifierName);

	return binding;
}

char *
keyBindingToString (CompDisplay    *d,
                    CompKeyBinding *key)
{
	char *binding;

	binding = modifiersToString (d, key->modifiers);

	if (key->keycode != 0)
	{
		KeySym keysym;
		char   *keyname;

		keysym  = XKeycodeToKeysym (d->display, key->keycode, 0);
		keyname = XKeysymToString (keysym);

		if (keyname)
		{
			binding = stringAppend (binding, keyname);
		}
		else
		{
			char keyCodeStr[256];

			snprintf (keyCodeStr, 256, "0x%x", key->keycode);
			binding = stringAppend (binding, keyCodeStr);
		}
	}

	return binding;
}

char *
buttonBindingToString (CompDisplay       *d,
                       CompButtonBinding *button)
{
	char *binding;
	char buttonStr[256];

	binding = modifiersToString (d, button->modifiers);

	snprintf (buttonStr, 256, "Button%d", button->button);
	binding = stringAppend (binding, buttonStr);

	return binding;
}

unsigned int
stringToModifiers (CompDisplay *d,
                   const char  *binding)
{
	unsigned int mods = 0;
	int          i;

	for (i = 0; i < N_MODIFIERS; i++)
	{
		if (strstr (binding, modifiers[i].name))
			mods |= modifiers[i].modifier;
	}

	return mods;
}

static unsigned int
bindingStringToEdgeMask (CompDisplay *d,
                         const char  *binding)
{
	unsigned int edgeMask = 0;
	int          i;

	for (i = 0; i < SCREEN_EDGE_NUM; i++)
		if (strstr (binding, edges[i].modifierName))
			edgeMask |= 1 << i;

	return edgeMask;
}

Bool
stringToKeyBinding (CompDisplay    *d,
                    const char     *binding,
                    CompKeyBinding *key)
{
	char          *ptr;
	unsigned int  mods;
	KeySym        keysym;

	mods = stringToModifiers (d, binding);

	ptr = strrchr (binding, '>');
	if (ptr)
		binding = ptr + 1;

	while (*binding && !isalnum (*binding))
		binding++;

	if (!*binding)
	{
		if (mods)
		{
			key->keycode   = 0;
			key->modifiers = mods;

			return TRUE;
		}

		return FALSE;
	}

	keysym = XStringToKeysym (binding);
	if (keysym != NoSymbol)
	{
		KeyCode keycode;

		keycode = XKeysymToKeycode (d->display, keysym);
		if (keycode)
		{
			key->keycode   = keycode;
			key->modifiers = mods;

			return TRUE;
		}
	}

	if (strncmp (binding, "0x", 2) == 0)
	{
		key->keycode   = strtol (binding, NULL, 0);
		key->modifiers = mods;

		return TRUE;
	}

	return FALSE;
}

Bool
stringToButtonBinding (CompDisplay       *d,
                       const char        *binding,
                       CompButtonBinding *button)
{
	char        *ptr;
	unsigned int mods;

	mods = stringToModifiers (d, binding);

	ptr = strrchr (binding, '>');
	if (ptr)
		binding = ptr + 1;

	while (*binding && !isalnum (*binding))
		binding++;

	if (strncmp (binding, "Button", strlen ("Button")) == 0)
	{
		int buttonNum;

		if (sscanf (binding + strlen ("Button"), "%d", &buttonNum) == 1)
		{
			button->button    = buttonNum;
			button->modifiers = mods;

			return TRUE;
		}
	}

	return FALSE;
}


const char *
edgeToString (unsigned int edge)
{
	return edges[edge].name;
} 

unsigned int
stringToEdgeMask (const char *edge)
{
	unsigned int edgeMask = 0;
	char         *needle;
	int          i;

	for (i = 0; i < SCREEN_EDGE_NUM; i++)
	{
		needle = strstr (edge, edgeToString (i));
		if (needle)
		{
			if (needle != edge && isalnum (*(needle - 1)))
				continue;

			needle += strlen (edgeToString (i));

			if (*needle && isalnum (*needle))
				continue;

			edgeMask |= 1 << i;
		}
	}

	return edgeMask;
}

char *
edgeMaskToString (unsigned int edgeMask)
{
	char *edge = NULL;
	int	 i;

	for (i = 0; i < SCREEN_EDGE_NUM; i++)
	{
		if (edgeMask & (1 << i))
		{
			if (edge)
				edge = stringAppend (edge, " | ");

			edge = stringAppend (edge, edgeToString (i));
		}
	}

	if (!edge)
		return strdup ("");

	return edge;
}

Bool
stringToColor (const char     *color,
               unsigned short *rgba)
{
	int c[4];

	if (sscanf (color, "#%2x%2x%2x%2x", &c[0], &c[1], &c[2], &c[3]) == 4)
	{
		rgba[0] = c[0] << 8 | c[0];
		rgba[1] = c[1] << 8 | c[1];
		rgba[2] = c[2] << 8 | c[2];
		rgba[3] = c[3] << 8 | c[3];

		return TRUE;
	}

	return FALSE;
}

char *
colorToString (unsigned short *rgba)
{
	char tmp[256];

	snprintf (tmp, 256, "#%.2x%.2x%.2x%.2x",
	          rgba[0] / 256, rgba[1] / 256, rgba[2] / 256, rgba[3] / 256);

	return strdup (tmp);
}

