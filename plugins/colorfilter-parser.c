/*
 * Compiz fragment program parser
 *
 * Author : Guillaume Seguin
 * Email : guillaume@segu.in
 *
 * Copyright (c) 2007 Guillaume Seguin <guillaume@segu.in>
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

#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <compiz-core.h>
#include "parser.h"

/* Internal prototypes ------------------------------------------------------ */

static char *
programFindOffset (FragmentOffset *offset, char *name);

/* General helper functions ------------------------------------------------- */

/*
 * Helper function to get the basename of file from its path
 * e.g. basename ("/home/user/blah.c") == "blah.c"
 * special case : basename ("/home/user/") == "user"
 */
char *
base_name (char *str)
{
    char *current = str;
    int length;
    while (*current)
    {
	if (*current == '/')
	{
	    /* '/' found, check if it is the latest char of the string,
	     * if not update result string pointer */
	    current++;
	    if (!*current) break;
	    str = current;
	}
	else
	    current++;
    }
    length = strlen (str);
    /* Duplicate result string for trimming */
    current = strdup (str);
    if (!current)
	return NULL;
    /* Trim terminating '/' if needed */
    if (length > 0 && current[(length - 1)] == '/')
	current[(length - 1)] = 0;
    return current;
}

/*
 * Left trimming function
 */
static char *
ltrim (char *string)
{
    while (*string && (*string == ' ' || *string == '\t'))
	string++;
    return string;
}

/* General fragment program related functions ------------------------------- */

/*
 * Clean program name string
 */
static char *
programCleanName (char *name)
{
    char *dest, *current;

    current = dest = strdup (name);
    
    /* Replace every non alphanumeric char by '_' */
    while (*current)
    {
	if (!isalnum (*current))
	    *current = '_';
	current++;
    }

    return dest;
}

/*
 * File reader function
 */
static char *
programReadSource (char *fname)
{
    FILE   *fp;
    char   *data, *path = NULL, *home = getenv ("HOME");
    int     length;

    /* Try to open file fname as is */
    fp = fopen (fname, "r");

    /* If failed, try as user filter file (in ~/.compiz/data/filters) */
    if (!fp && home && strlen (home))
    {
	asprintf (&path, "%s/.compiz/data/filters/%s", home, fname);
	fp = fopen (path, "r");
	free (path);
    }

    /* If failed again, try as system wide data file 
     * (in PREFIX/share/compiz/filters) */
    if (!fp)
    {
	asprintf (&path, "%s/filters/%s", DATADIR, fname);
	fp = fopen (path, "r");
	free (path);
    }

    /* If failed again & again, abort */
    if (!fp)
	return NULL;

    /* Get file length */
    fseek (fp, 0L, SEEK_END);
    length = ftell (fp);
    rewind (fp);

    /* Alloc memory */
    data = malloc (sizeof (char) * (length + 1));
    if (!data)
    {
	fclose (fp);
	return NULL;
    }

    /* Read file */
    fread (data, length, 1, fp);

    data[length] = 0;

    /* Close file */
    fclose (fp);

    return data;
}

/*
 * Get the first "argument" in the given string, trimmed
 * and move source string pointer after the end of the argument.
 * For instance in string " foo, bar" this function will return "foo".
 *
 * This function returns NULL if no argument found
 * or a malloc'ed string that will have to be freed later.
 */
static char *
getFirstArgument (char **source)
{
    char *next, *arg, *temp;
    char *string, *orig;
    int length;

    if (!**source)
	return NULL;

    /* Left trim */
    orig = string = ltrim (*source);

    /* Find next comma or semicolon (which isn't that useful since we
     * are working on tokens delimited by semicolons) */
    if ((next = strstr (string, ",")) || (next = strstr (string, ";")))
    {
	length = next - string;
	if (!length)
	{
	    (*source)++;
	    return getFirstArgument (source);
	}
	if ((temp = strstr (string, "{")) && temp < next &&
	    (temp = strstr (string, "}")) && temp > next)
	{
	    if ((next = strstr (temp, ",")) || (next = strstr (temp, ";")))
		length = next - string;
	    else
		length = strlen (string);
	}
    }
    else
    {
	length = strlen (string);
    }

    /* Allocate, copy and end string */
    arg = malloc (sizeof (char) * (length + 1));
    if (!arg) return NULL;

    strncpy (arg, string, length);
    arg[length] = 0;

    /* Increment source pointer */
    if (string - orig + strlen (arg) + 1 <= strlen (*source))
	*source += string - orig + strlen (arg) + 1;
    else
	**source = 0;

    return arg;
}

/* Texture offset related functions ----------------------------------------- */

/*
 * Add a new fragment offset to the offsets stack from an ADD op string
 */
static FragmentOffset *
programAddOffsetFromAddOp (FragmentOffset *offsets, char *source)
{
    FragmentOffset  *offset;
    char	    *op, *orig_op;
    char	    *name;
    char	    *offset_string;
    char	    *temp;

    if (strlen (source) < 5)
	return offsets;

    orig_op = op = strdup (source);

    op += 3;
    if (!(name = getFirstArgument (&op)))
    {
	free (orig_op);
	return offsets;
    }

    /* If an offset with the same name is already registered, skeep this one */
    if (programFindOffset (offsets, name) || !(temp = getFirstArgument (&op)))
    {
	free (name);
	free (orig_op);
	return offsets;
    }

    /* We don't need this, let's free it immediately */
    free (temp);

    /* Just use the end of the op as the offset */
    op += 1;
    offset_string = strdup (ltrim (op));
    if (!offset_string)
    {
	free (name);
	free (orig_op);
	return offsets;
    }

    offset = malloc (sizeof (FragmentOffset));
    if (!offset)
    {
	free (offset_string);
	free (name);
	free (orig_op);
	return offsets;
    }

    offset->name =  strdup (name);
    offset->offset = strdup (offset_string);
    offset->next = offsets;

    free (offset_string);
    free (name);
    free (orig_op);

    return offset;
}

/*
 * Find an offset according to its name
 */
static char *
programFindOffset (FragmentOffset *offset, char *name)
{
    if (!offset)
	return NULL;

    if (strcmp (offset->name, name) == 0)
	return strdup (offset->offset);

    return programFindOffset (offset->next, name);
}

/*
 * Recursively free offsets stack
 */
static void
programFreeOffset (FragmentOffset *offset)
{
    if (!offset)
	return;

    programFreeOffset (offset->next);

    free (offset->name);
    free (offset->offset);
    free (offset);
}

/* Actual parsing/loading functions ----------------------------------------- */

/*
 * Parse the source buffer op by op and add each op to function data
 */
/* FIXME : I am more than 200 lines long, I feel so heavy! */
static void
programParseSource (CompFunctionData *data,
		    int target, char *source)
{
    char *line, *next, *current;
    char *strtok_ptr;
    int   length, oplength, type;
    FragmentOffset *offsets = NULL;

    char *arg1, *arg2, *temp;

    /* Find the header, skip it, and start parsing from there */
    while (*source)
    {
	if (strncmp (source, "!!ARBfp1.0", 10) == 0)
	{
	    source += 10;
	    break;
	}
	source++;
    }

    /* Strip linefeeds */
    next = source;
    while ((next = strstr (next, "\n")))
	*next = ' ';

    line = strtok_r (source, ";", &strtok_ptr);
    /* Parse each instruction */
    while (line)
    {
	line = strdup (line);
	current = ltrim (line);

	/* Find instruction type */
	type = NoOp;

	/* Comments */
	if (strncmp (current, "#", 1) == 0)
	{
	    free (line);
	    line = strtok_r (NULL, ";", &strtok_ptr);
	    continue;
	}
	if ((next = strstr (current, "#")))
	    *next = 0;

	/* Data ops */
	if (strncmp (current, "END", 3) == 0)
	    type = NoOp;
	else if (!strncmp (current, "ABS", 3) || !strncmp (current, "CMP", 3) ||
	         !strncmp (current, "COS", 3) || !strncmp (current, "DP3", 3) ||
	         !strncmp (current, "DP4", 3) || !strncmp (current, "EX2", 3) ||
	         !strncmp (current, "FLR", 3) || !strncmp (current, "FRC", 3) ||
	         !strncmp (current, "KIL", 3) || !strncmp (current, "LG2", 3) ||
	         !strncmp (current, "LIT", 3) || !strncmp (current, "LRP", 3) ||
	         !strncmp (current, "MAD", 3) || !strncmp (current, "MAX", 3) ||
	         !strncmp (current, "MIN", 3) || !strncmp (current, "POW", 3) ||
	         !strncmp (current, "RCP", 3) || !strncmp (current, "RSQ", 3) ||
	         !strncmp (current, "SCS", 3) || !strncmp (current, "SIN", 3) ||
	         !strncmp (current, "SGE", 3) || !strncmp (current, "SLT", 3) ||
	         !strncmp (current, "SUB", 3) || !strncmp (current, "SWZ", 3) ||
	         !strncmp (current, "TXB", 3) || !strncmp (current, "TXP", 3) ||
	         !strncmp (current, "XPD", 3))
		type = DataOp;
	else if (strncmp (current, "TEMP", 4) == 0)
	    type = TempOp;
	else if (strncmp (current, "PARAM", 5) == 0)
	    type = ParamOp;
	else if (strncmp (current, "ATTRIB", 6) == 0)
	    type = AttribOp;
	else if (strncmp (current, "TEX", 3) == 0)
	    type = FetchOp;
	else if (strncmp (current, "ADD", 3) == 0)
	{
	    if (strstr (current, "fragment.texcoord"))
		offsets = programAddOffsetFromAddOp (offsets, current);
	    else
		type = DataOp;
	}
	else if (strncmp (current, "MUL", 3) == 0)
	{
	    if (strstr (current, "fragment.color"))
		type = ColorOp;
	    else
		type = DataOp;
	}
	else if (strncmp (current, "MOV", 3) == 0)
	{
	    if (strstr (current, "result.color"))
		type = ColorOp;
	    else
		type = DataOp;
	}
	switch (type)
	{
	    /* Data op : just copy paste the whole instruction plus a ";" */
	    case DataOp:
		asprintf (&arg1, "%s;", current);
		addDataOpToFunctionData (data, arg1);
		free (arg1);
		break;
	    /* Parse arguments one by one */
	    case TempOp:
	    case AttribOp:
	    case ParamOp:
		if (type == TempOp) oplength = 4;
		else if (type == ParamOp) oplength = 5;
		else if (type == AttribOp) oplength = 6;
		length = strlen (current);
		if (length < oplength + 2) break;
		current += oplength + 1;
		while (current && *current &&
		       (arg1 = getFirstArgument (&current)))
		{
		    /* "output" is a reserved word, skip it */
		    if (strncmp (arg1, "output", 6) == 0)
		    {
			free (arg1);
			continue;
		    }
		    /* Add ops */
		    if (type == TempOp)
			addTempHeaderOpToFunctionData (data, arg1);
		    else if (type == ParamOp)
			addParamHeaderOpToFunctionData (data, arg1);
		    else if (type == AttribOp)
			addAttribHeaderOpToFunctionData (data, arg1);
		    free (arg1);
		}
		break;
	    case FetchOp:
		/* Example : TEX tmp, coord, texture[0], RECT;
		 * "tmp" is dest name, while "coord" is either 
		 * fragment.texcoord[0] or an offset */
		current += 3;
		if ((arg1 = getFirstArgument (&current)))
		{
		    if (!(temp = getFirstArgument (&current)))
		    {
			free (arg1);
			break;
		    }
		    if (strcmp (temp, "fragment.texcoord[0]") == 0)
			addFetchOpToFunctionData (data, arg1, NULL, target);
		    else
		    {
			arg2 = programFindOffset (offsets, temp); 
			if (arg2)
			{
			    addFetchOpToFunctionData (data, arg1, arg2, target);
			    free (arg2);
			}
		    }
		    free (arg1);
		    free (temp);
		}
		break;
	    case ColorOp:
		if (strncmp (current, "MUL", 3) == 0) /* MUL op, 2 ops */
		{
		    /* Example : MUL output, fragment.color, output;
		     * MOV arg1, fragment.color, arg2 */
		    current += 3;
		    if  (!(arg1 = getFirstArgument (&current)))
			break;

		    if (!(temp = getFirstArgument (&current)))
		    {
			free (arg1);
			break;
		    }

		    free (temp);

		    if (!(arg2 = getFirstArgument (&current)))
		    {
			free (arg1);
			break;
		    }

		    addColorOpToFunctionData (data, arg1, arg2);
		    free (arg1);
		    free (arg2);
		}
		else /* MOV op, 1 op */
		{
		    /* Example : MOV result.color, output;
		     * MOV result.color, arg1; */
		    current = strstr (current, ",") + 1;
		    if ((arg1 = getFirstArgument (&current)))
		    {
			addColorOpToFunctionData (data, "output", arg1);
			free (arg1);
		    }
		}
		break;
	    default:
		break;
	}
	free (line);
	line = strtok_r (NULL, ";", &strtok_ptr);
    }
    programFreeOffset (offsets);
    offsets = NULL;
}

/*
 * Build a Compiz Fragment Function from a source string
 */
int
buildFragmentProgram (char *source, char *name,
		      CompScreen *s, int target)
{
    CompFunctionData *data;
    int handle;
    /* Create the function data */
    data = createFunctionData ();
    if (!data)
	return 0;
    /* Parse the source and fill the function data */
    programParseSource (data, target, source);
    /* Create the function */
    handle = createFragmentFunction (s, name, data);
    /* Clean things */
    destroyFunctionData (data);
    return handle;
}

/*
 * Load a source file and build a Compiz Fragment Function from it
 */
int
loadFragmentProgram (char *file, char *name,
		     CompScreen *s, int target)
{
    char *source;
    int handle;
    /* Clean fragment program name */
    name = programCleanName (name);
    /* Read the source file */
    source = programReadSource (file);
    if (!source)
    {
	free (name);
	return 0;
    }
    /* Build the Compiz Fragment Program */
    handle = buildFragmentProgram (source, name, s, target);
    free (name);
    free (source);
    return handle;
}
