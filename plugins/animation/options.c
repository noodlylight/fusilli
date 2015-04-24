/*
 * Animation plugin for compiz/beryl
 *
 * animation.c
 *
 * Copyright : (C) 2006 Erkin Bahceci
 * E-mail    : erkinbah@gmail.com
 *
 * Based on Wobbly and Minimize plugins by
 *           : David Reveman
 * E-mail    : davidr@novell.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "animation-internal.h"

extern ExtensionPluginInfo animExtensionPluginInfo;
extern AnimBaseFunctions animBaseFunctions;

// =================  Option Related Functions  =================

AnimEvent win2AnimEventMap[WindowEventNum] =
{
    AnimEventOpen,
    AnimEventClose,
    AnimEventMinimize,
    AnimEventMinimize,
    AnimEventShade,
    AnimEventShade,
    AnimEventFocus
};

OPTION_GETTERS (&animBaseFunctions,
		&animExtensionPluginInfo, NUM_NONEFFECT_OPTIONS)

CompOptionValue *
animGetPluginOptVal (CompWindow *w,
		     ExtensionPluginInfo *pluginInfo,
		     int optionId)
{
    ANIM_WINDOW (w);
    ANIM_SCREEN (w->screen);

    OptionSet *os =
	&as->eventOptionSets[win2AnimEventMap[aw->com.curWindowEvent]].
	sets[aw->curAnimSelectionRow];
    IdValuePair *pair = os->pairs;

    int i;
    for (i = 0; i < os->nPairs; i++, pair++)
	if (pair->pluginInfo == pluginInfo &&
	    pair->optionId == optionId)
	    return &pair->value;
    return &pluginInfo->effectOptions[optionId].value;
}

static
void freeSingleEventOptionSets (OptionSets *oss)
{
    int j;
    for (j = 0; j < oss->nSets; j++)
	if (oss->sets[j].pairs)
	    free(oss->sets[j].pairs);
    free (oss->sets);
    oss->sets = NULL;
}

void
freeAllOptionSets (AnimScreen *as)
{
    AnimEvent e;
    for (e = 0; e < AnimEventNum; e++)
	freeSingleEventOptionSets (&as->eventOptionSets[e]);
}

static void
updateOptionSet(CompScreen *s, OptionSet *os, char *optNamesValuesOrig)
{
    ANIM_SCREEN(s);
    int len = strlen(optNamesValuesOrig);
    char *optNamesValues = calloc(len + 1, 1);

    // Find the first substring with no spaces in it
    sscanf(optNamesValuesOrig, " %s ", optNamesValues);
    if (strlen(optNamesValues) == 0)
    {
	free(optNamesValues);
	return;
    }
    // Backup original, since strtok is destructive
    strcpy(optNamesValues, optNamesValuesOrig);

    char *name;
    char *nameTrimmed = calloc(len + 1, 1);
    char *valueStr = NULL;
    char *betweenPairs = ",";
    char *betweenOptVal = "=";

    // Count number of pairs
    char *pairToken = optNamesValuesOrig;
    int nPairs = 1;
	
    while ((pairToken = strchr(pairToken, betweenPairs[0])))
    {
	pairToken++; // skip delimiter
	nPairs++;
    }

    if (os->pairs)
	free(os->pairs);
    os->pairs = calloc(nPairs, sizeof(IdValuePair));
    if (!os->pairs)
    {
	os->nPairs = 0;
	free(optNamesValues);
	free(nameTrimmed);
	compLogMessage ("animation", CompLogLevelError,
			"Not enough memory");
	return;
    }
    os->nPairs = nPairs;

    // Tokenize pairs
    name = strtok(optNamesValues, betweenOptVal);

    IdValuePair *pair = &os->pairs[0];
    int errorNo = -1;
    int i;
    for (i = 0; name && i < nPairs; i++, pair++)
    {
	errorNo = 0;
	if (strchr(name, betweenPairs[0])) // handle "a, b=4" case
	{
	    errorNo = 1;
	    break;
	}

	sscanf(name, " %s ", nameTrimmed);
	if (strlen(nameTrimmed) == 0)
	{
	    errorNo = 2;
	    break;
	}
	valueStr = strtok(NULL, betweenPairs);
	if (!valueStr)
	{
	    errorNo = 3;
	    break;
	}

	// TODO: Fix: Convert to "pluginname:option_name" format
	// Warning: Assumes that option names in different extension plugins
	// will be different.
	Bool matched = FALSE;
	const ExtensionPluginInfo *extensionPluginInfo;
	CompOption *o;
	int optId;
	int k;
	for (k = 0; k < as->nExtensionPlugins; k++)
	{
	    extensionPluginInfo = as->extensionPlugins[k];
	    unsigned int nOptions = extensionPluginInfo->nEffectOptions;
	    o = extensionPluginInfo->effectOptions;
	    for (optId = 0; optId < nOptions; optId++, o++)
	    {
		if (strcasecmp(nameTrimmed, o->name) == 0)
		{
		    matched = TRUE;
		    break;
		}
	    }
	    if (matched)
		break;
	}
	if (!matched)
	{
	    errorNo = 4;
	    break;
	}
	CompOptionValue v;

	pair->pluginInfo = extensionPluginInfo;
	pair->optionId = optId;
	int valueRead = -1;
	switch (o->type)
	{
	case CompOptionTypeBool:
	    valueRead = sscanf(valueStr, " %d ", &pair->value.b);
	    break;
	case CompOptionTypeInt:
	    valueRead = sscanf(valueStr, " %d ", &v.i);
	    if (valueRead > 0)
	    {
		// Store option's original value
		int backup = o->value.i;
		if (compSetIntOption (o, &v))
		    pair->value = v;
		else
		    errorNo = 7;
		// Restore value
		o->value.i = backup;
	    }
	    break;
	case CompOptionTypeFloat:
	    valueRead = sscanf(valueStr, " %f ", &v.f);
	    if (valueRead > 0)
	    {
		// Store option's original value
		float backup = o->value.f;
		if (compSetFloatOption (o, &v))
		    pair->value = v;
		else
		    errorNo = 7;
		// Restore value
		o->value.f = backup;
	    }
	    break;
	case CompOptionTypeString:
	    v.s = calloc (strlen(valueStr) + 1, 1); // TODO: not freed
	    if (!v.s)
	    {
		compLogMessage ("animation", CompLogLevelError,
				"Not enough memory");
		return;
	    }
	    strcpy(v.s, valueStr);
	    valueRead = 1;
	    break;
	case CompOptionTypeColor:
	{
	    unsigned int c[4];
	    valueRead = sscanf (valueStr, " #%2x%2x%2x%2x ",
				&c[0], &c[1], &c[2], &c[3]);
	    if (valueRead == 4)
	    {
		CompOptionValue * pv = &pair->value;
		int j;
		for (j = 0; j < 4; j++)
		    pv->c[j] = c[j] << 8 | c[j];
	    }
	    else
		errorNo = 6;
	    break;
	}
	default:
	    break;
	}
	if (valueRead == 0)
	    errorNo = 6;
	if (errorNo > 0)
	    break;
	// If valueRead is -1 here, then it must be a
	// non-(int/float/string) option, which is not supported yet.
	// Such an option doesn't currently exist anyway.

	errorNo = -1;
	name = strtok(NULL, betweenOptVal);
    }

    if (i < nPairs)
    {
	switch (errorNo)
	{
	case -1:
	case 2:
	    compLogMessage ("animation", CompLogLevelError,
			    "Option name missing in \"%s\"",
			    optNamesValuesOrig);
	    break;
	case 1:
	case 3:
	    compLogMessage ("animation", CompLogLevelError,
			    "Option value missing in \"%s\"",
			    optNamesValuesOrig);
	    break;
	case 4:
	    compLogMessage ("animation", CompLogLevelError,
			    "Unknown option \"%s\" in \"%s\"",
			    nameTrimmed, optNamesValuesOrig);
	    break;
	case 6:
	    compLogMessage ("animation", CompLogLevelError,
			    "Invalid value \"%s\" in \"%s\"",
			    valueStr, optNamesValuesOrig);
	    break;
	case 7:
	    compLogMessage ("animation", CompLogLevelError,
			    "Value \"%s\" out of range in \"%s\"",
			    valueStr, optNamesValuesOrig);
	    break;
	default:
	    break;
	}
	free(os->pairs);
	os->pairs = 0;
	os->nPairs = 0;
    }
    free(optNamesValues);
    free(nameTrimmed);
}

void
updateOptionSets (CompScreen *s,
		  AnimEvent e)
{
    ANIM_SCREEN (s);

    OptionSets *oss = &as->eventOptionSets[e];
    CompListValue *listVal = &as->opt[customOptionOptionIds[e]].value.list;
    int n = listVal->nValue;

    if (oss->sets)
	freeSingleEventOptionSets(oss);

    oss->sets = calloc(n, sizeof(OptionSet));
    if (!oss->sets)
    {
	compLogMessage ("animation", CompLogLevelError,
			"Not enough memory");
	return;
    }
    oss->nSets = n;

    int i;
    for (i = 0; i < n; i++)
	updateOptionSet(s, &oss->sets[i], listVal->value[i].s);
}

