/*
 * Copyright © 2014 Michael Bitches
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Michael Bitches not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Michael Bitches makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * MICHAEL BITCHES DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL MICHAEL BITCHES BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <sys/stat.h>
#include <string.h>
#include <libxml/parser.h>

#define EXTENSION ".xml"
#define MAX_NUM_PLUGINS        256
#define MAX_NUM_SCREENS        9

#include <fusilli-core.h>

static CompFileWatchHandle directoryWatch;

static char *bananaMetaDataDir;
static char *bananaConfigurationDir;
static char *bananaConfigurationFile;

typedef struct _BananaChangeNotifyCallBackNode BananaChangeNotifyCallBackNode;

struct _BananaChangeNotifyCallBackNode {
	BananaChangeNotifyCallBack     callback;
	BananaChangeNotifyCallBackNode *next;
};

typedef struct _BananaOption {
	char                    *name;
	BananaType              type;
	Bool                    isPerScreen;

	BananaValue             value;
	BananaValue             valuePerScreen[MAX_NUM_SCREENS];

	BananaValue             defaultValue;
} BananaOption;

typedef struct _BananaPlugin BananaPlugin;

struct _BananaPlugin {
	char         *name;

	BananaOption *option;
	int          nOption;
	size_t       bytes; //size of memory chunk pointed by option

	BananaChangeNotifyCallBackNode *list;
} bananaTree[MAX_NUM_PLUGINS];

void
initBananaValue (BananaValue *v,
                 BananaType  type)
{
	switch (type)
	{
	case BananaBool:
		v->b = FALSE;
		break;
	case BananaInt:
		v->i = 0;
		break;
	case BananaFloat:
		v->f = 0.0;
		break;
	case BananaString:
		v->s = strdup("");
		break;
	case BananaListBool:
	case BananaListInt:
	case BananaListFloat:
	case BananaListString:
		v->list.nItem = 0;
		v->list.bytes = sizeof (BananaValue) * 20;
		v->list.item = malloc (v->list.bytes);
		break;
	}
}

void
finiBananaValue (BananaValue *v,
                 BananaType  type)
{
	int i;

	switch (type)
	{
	case BananaBool:
	case BananaInt:
	case BananaFloat:
		break;
	case BananaString:
		free (v->s);
		break;

	case BananaListBool:
	case BananaListInt:
	case BananaListFloat:
		if (v->list.bytes != 0)
			free (v->list.item);
		v->list.item = NULL;
		v->list.nItem = 0;
		v->list.bytes = 0;
	case BananaListString:
		for (i = 0; i <= (v->list).nItem - 1; i++)
			free(v->list.item[i].s);

		if (v->list.bytes != 0)
			free (v->list.item);
		v->list.item = NULL;
		v->list.nItem = 0;
		v->list.bytes = 0;
		break;
	}
}

void 
copyBananaValue (BananaValue       *dest,
                 const BananaValue *src,
                 BananaType        type)
{
	int i;

	switch (type)
	{
	case BananaBool:
		dest->b = src->b;
		break;
	case BananaInt:
		dest->i = src->i;
		break;
	case BananaFloat:
		dest->f = src->f;
		break;
	case BananaString:
		dest->s = strdup (src->s);
		break;
	case BananaListBool:
		dest->list.nItem = src->list.nItem;
		dest->list.bytes = src->list.bytes;
		dest->list.item = malloc (dest->list.bytes);
		for (i = 0; i <= dest->list.nItem - 1; i++)
			dest->list.item[i].b = src->list.item[i].b;
		break;
	case BananaListInt:
		dest->list.nItem = src->list.nItem;
		dest->list.bytes = src->list.bytes;
		dest->list.item = malloc (dest->list.bytes);
		for (i = 0; i <= dest->list.nItem - 1; i++)
			dest->list.item[i].i = src->list.item[i].i;
		break;
	case BananaListFloat:
		dest->list.nItem = src->list.nItem;
		dest->list.bytes = src->list.bytes;
		dest->list.item = malloc (dest->list.bytes);
		for (i = 0; i <= dest->list.nItem - 1; i++)
			dest->list.item[i].f = src->list.item[i].f;
		break;
	case BananaListString:
		dest->list.nItem = src->list.nItem;
		dest->list.bytes = src->list.bytes;
		dest->list.item = malloc (dest->list.bytes);
		for (i = 0; i <= dest->list.nItem - 1; i++)
			dest->list.item[i].s = strdup (src->list.item[i].s);
		break;
	default:
		break;
	}
}

Bool
isEqualBananaValue (const BananaValue *a,
                    const BananaValue *b,
                    BananaType        type)
{
	Bool retval = TRUE;
	int i;

	switch(type)
	{
	case BananaBool:
		if (a->b != b->b)
			retval = FALSE;

		break;
	case BananaInt:
		if (a->i != b->i)
			retval = FALSE;

		break;
	case BananaFloat:
		if (a->f != b->f)
			retval = FALSE;

		break;
	case BananaString:
		if (strcmp (a->s, b->s) != 0)
			retval = FALSE;

		break;
	case BananaListBool:
		if (a->list.nItem != b->list.nItem)
		{
			retval = FALSE;
			break;
		}

		for (i = 0; i <= a->list.nItem - 1; i++)
			if (a->list.item[i].b != b->list.item[i].b)
			{
				retval = FALSE;
				break;
			}
		break;
	case BananaListInt:
		if (a->list.nItem != b->list.nItem)
		{
			retval = FALSE;
			break;
		}

		for (i = 0; i <= a->list.nItem - 1; i++)
			if (a->list.item[i].i != b->list.item[i].i)
			{
				retval = FALSE;
				break;
			}
		break;
	case BananaListFloat:
		if (a->list.nItem != b->list.nItem)
		{
			retval = FALSE;
			break;
		}

		for (i = 0; i <= a->list.nItem - 1; i++)
			if (a->list.item[i].f != b->list.item[i].f)
			{
				retval = FALSE;
				break;
			}
		break;
	case BananaListString:
		if (a->list.nItem != b->list.nItem)
		{
			retval = FALSE;
			break;
		}

		for (i = 0; i <= a->list.nItem - 1; i++)
			if (strcmp (a->list.item[i].s, b->list.item[i].s) != 0)
			{
				retval = FALSE;
				break;
			}

		break;
	}

	return retval;
}

void
addItemToBananaList (const char     *s,
                     BananaType     type,
                     BananaValue    *l)
{
	if (l->list.bytes <= l->list.nItem * sizeof (BananaValue))
	{
		l->list.bytes *= 2;
		l->list.item = realloc (l->list.item, l->list.bytes);
	}
	l->list.nItem++;

	BananaValue *v = &l->list.item[l->list.nItem - 1];

	switch (type)
	{
	case BananaListBool:
		if (strcmp(s, "true") == 0)
			v->b = TRUE;
		else
			v->b = FALSE;
		break;
	case BananaListInt:
		v->i = atoi (s);
		break;
	case BananaListFloat:
		v->f = atof (s);
		break;
	case BananaListString:
		v->s = strdup (s);
		break;

	default:
		break; //non-list handled in stringToBananaValue
	}
}

void
stringToBananaValue (const char        *s,
                     BananaType        type,
                     BananaValue       *value)
{
	switch (type)
	{
	case BananaBool:
		if (strcmp(s, "true") == 0)
			value->b = TRUE;
		else
			value->b = FALSE;
		break;
	case BananaInt:
		value->i = atoi (s);
		break;
	case BananaFloat:
		value->f = atof (s);
		break;
	case BananaString:
		free (value->s);
		value->s = strdup (s);
		break;

	default:
		break; //lists are handled in addItemToBananaList
	}
}

static BananaPlugin *
bananaIndexToBananaPlugin (int bananaIndex)
{
	if (bananaIndex < 0 || 
	    bananaIndex >= MAX_NUM_PLUGINS)
	{
		compLogMessage ("core", CompLogLevelError,
		                "Invalid bananaIndex: %d\n", bananaIndex);
		return NULL;
	}

	BananaPlugin *p = &bananaTree[bananaIndex];

	if (p->name == NULL)
	{
		compLogMessage ("core", CompLogLevelError, 
		                "bananaIndex not allocated\n");
		return NULL;
	}

	return p;
}

static void
finiBananaOption (BananaOption *o)
{
	int i;

	free (o->name);

	o->name = NULL;

	finiBananaValue (&o->defaultValue, o->type);

	if (o->isPerScreen)
		for (i = 0; i <= MAX_NUM_SCREENS - 1; i++)
			finiBananaValue (&(o->valuePerScreen[i]), o->type);
	else
		finiBananaValue (&o->value, o->type);
}

//<default></default>
static void
processDefaultNode (xmlDocPtr    doc,
                    xmlNodePtr   defaultNode,
                    BananaOption *o)
{
	if (o->type == BananaListBool || o->type == BananaListInt ||
	    o->type == BananaListFloat || o->type == BananaListString )
	{
		xmlNodePtr value;

		for (value = defaultNode->xmlChildrenNode; value; value = value->next)
			if (xmlStrcmp (value->name, BAD_CAST "item") == 0)
			{
				xmlChar *item = xmlNodeListGetString (doc,
				                                     value->xmlChildrenNode, 1);

				if (!item && o->type == BananaListString) //happens on empty strings
					addItemToBananaList ("", o->type, &o->defaultValue);

				if (item)
				{
					addItemToBananaList ((char*)item, o->type, &o->defaultValue);

					xmlFree (item);
				}
			}
	}
	else
	{
		xmlChar *value = xmlNodeListGetString (doc,
		                                       defaultNode->xmlChildrenNode, 1);

		if (!value && o->type == BananaString) //happens on empty strings
			stringToBananaValue ("", o->type, &o->defaultValue);

		if (value)
		{
			stringToBananaValue ((char *)value, o->type, &o->defaultValue);

			xmlFree (value);
		}
	}
}

//<option></option>
static void
processOptionNode (xmlDocPtr  doc,
                   xmlNodePtr optionNode,
                   int        bananaIndex)
{
	BananaPlugin *p = &bananaTree[bananaIndex];
	BananaOption *o;

	if (p->bytes <= p->nOption * sizeof (BananaOption))
	{
		p->bytes *= 2;
		p->option = realloc (p->option, p->bytes);
	}
	p->nOption++;
	o = &p->option[p->nOption - 1];

	xmlChar *optionName = xmlGetProp (optionNode, BAD_CAST "name");
	o->name = strdup ((char *)optionName);
	xmlFree (optionName);

	xmlChar *optionType = xmlGetProp (optionNode, BAD_CAST "type");
	if      (xmlStrcmp (optionType, BAD_CAST "bool"      ) == 0)
		o->type = BananaBool;
	else if (xmlStrcmp (optionType, BAD_CAST "int"       ) == 0)
		o->type = BananaInt;
	else if (xmlStrcmp (optionType, BAD_CAST "float"     ) == 0)
		o->type = BananaFloat;
	else if (xmlStrcmp (optionType, BAD_CAST "string"    ) == 0)
		o->type = BananaString;
	else if (xmlStrcmp (optionType, BAD_CAST "list_bool" ) == 0)
		o->type = BananaListBool;
	else if (xmlStrcmp (optionType, BAD_CAST "list_int"  ) == 0)
		o->type = BananaListInt;
	else if (xmlStrcmp (optionType, BAD_CAST "list_float") == 0)
		o->type = BananaListFloat;
	else if (xmlStrcmp (optionType, BAD_CAST "list_string") == 0)
		o->type = BananaListString;
	xmlFree (optionType);

	xmlChar *perScreen  = xmlGetProp (optionNode, BAD_CAST "per_screen");
	if (xmlStrcmp (perScreen, BAD_CAST "true") == 0)
	{
		o->isPerScreen = TRUE;
		xmlFree (perScreen);
	}
	else
		o->isPerScreen = FALSE;

	initBananaValue (&o->defaultValue, o->type);

	xmlNodePtr child;

	for (child = optionNode->xmlChildrenNode; child; child = child->next)
		if (xmlStrcmp (child->name, BAD_CAST "default") == 0)
			processDefaultNode (doc, child, o);

	int j;
	if (o->isPerScreen)
		for (j = 0; j <= MAX_NUM_SCREENS - 1; j++)
			copyBananaValue (&(o->valuePerScreen[j]),
			                 &(o->defaultValue),
			                 o->type);
	else
		copyBananaValue (&(o->value), &(o->defaultValue),
		                 o->type);
}

//<plugin></plugin> -- parse <group>s, <subgroup>s
static void
processPluginNode (xmlDocPtr  doc,
                   xmlNodePtr pluginNode,
                   int        bananaIndex)
{
	xmlNodePtr child;

	for (child = pluginNode->xmlChildrenNode; child; child = child->next)
	{
		if (xmlStrcmp (child->name, BAD_CAST "group") == 0)
		{
			xmlNodePtr groupChild;

			for (groupChild = child->xmlChildrenNode; groupChild;
			     groupChild = groupChild->next)
			{
				if (xmlStrcmp (groupChild->name,
				                    BAD_CAST "subgroup") == 0)
				{
					xmlNodePtr subChild;

					for (subChild = groupChild->xmlChildrenNode; subChild;
					     subChild = subChild->next)
					{
						if (xmlStrcmp (subChild->name, BAD_CAST "option") == 0)
							processOptionNode (doc, subChild, bananaIndex);
					}
				}
			}
		}
	}
}

//<fusiilli></fusilli>
static void
processFusilliNode (xmlDocPtr  doc,
                    int        bananaIndex)
{
	xmlNodePtr root = xmlDocGetRootElement (doc);

	xmlNodePtr child;

	for (child = root->xmlChildrenNode; child; child = child->next)
		if (xmlStrcmp (child->name, BAD_CAST "plugin") == 0)
			processPluginNode (doc, child, bananaIndex);
}

//<plugin></plugin> inside banana.xml
static void
processPluginNodeInConfigFile (xmlDocPtr    doc,
                               xmlNodePtr   pluginNode,
                               BananaPlugin *p,
                               Bool         FIRST)
{
	xmlNodePtr option;

	for (option = pluginNode->xmlChildrenNode; option; option = option->next)
	{
		if (!xmlStrcmp (option->name, BAD_CAST "option") == 0)
			continue;

		int i;

		xmlChar *name = xmlGetProp (option, BAD_CAST "name");
		xmlChar *screen = xmlGetProp (option, BAD_CAST "screen");

		if (!name)
			continue; //ignore <option> tags without name= attribute

		for(i = 0; i <= p->nOption - 1; i++)
			if (xmlStrcmp (BAD_CAST p->option[i].name, name) == 0)
				break;

		if (i == p->nOption)
			continue; //option not found in plugin

		xmlFree (name);

		BananaValue *v = NULL;
		if (!screen && !p->option[i].isPerScreen)
			v = &p->option[i].value;
		else if (screen && p->option[i].isPerScreen)
			v = &p->option[i].valuePerScreen[atoi((char*)screen)];

		if (!v)
			continue;

		BananaValue v_old;
		copyBananaValue (&v_old, v, p->option[i].type);

		if (p->option[i].type == BananaBool  ||
		    p->option[i].type == BananaInt   ||
		    p->option[i].type == BananaFloat ||
		    p->option[i].type == BananaString)
		{
			xmlChar *value = xmlNodeListGetString (doc,
			                                       option->xmlChildrenNode, 1);

			//happens on empty strings
			if (!value && p->option[i].type == BananaString) 
				stringToBananaValue ("", p->option[i].type, v);

			if (value)
			{
				stringToBananaValue ((char *)value, p->option[i].type, v);

				xmlFree (value);
			}
		}
		else if (p->option[i].type == BananaListBool  ||
		         p->option[i].type == BananaListInt   ||
		         p->option[i].type == BananaListFloat ||
		         p->option[i].type == BananaListString)
		{
			xmlNodePtr value;

			finiBananaValue (v, p->option[i].type);
			initBananaValue (v, p->option[i].type);

			for (value = option->xmlChildrenNode; value; 
			     value = value->next)
			{
				if (xmlStrcmp (value->name, BAD_CAST "item") == 0)
				{
					xmlChar *item = xmlNodeListGetString (doc,
					                                 value->xmlChildrenNode, 1);

					//happens on empty strings
					if (!item && p->option[i].type == BananaListString)
						addItemToBananaList ("", p->option[i].type, v);

					if (item)
					{
						addItemToBananaList ((char*)item, p->option[i].type,  v);

						xmlFree (item);
					}
				}
			}
		}

		if (!isEqualBananaValue(&v_old, v, p->option[i].type) && !FIRST)
		{
			BananaChangeNotifyCallBackNode *n = p->list;

			while (n != NULL)
			{
				if (screen)
					(n->callback) (p->option[i].name,
					               p->option[i].type,
					               v,
					               atoi((char*)screen));
				else
					(n->callback) (p->option[i].name,
					               p->option[i].type,
					               v,
					               -1);

				n = n->next;
			}
		}

		finiBananaValue (&v_old, p->option[i].type);

		if (screen)
			xmlFree (screen);
	}
}

static void
loadOptionsForPlugin (int       bananaIndex,
                      Bool      FIRST)
{
	xmlDocPtr doc;
	xmlNodePtr root, node;
	BananaPlugin *p;

	p = bananaIndexToBananaPlugin (bananaIndex);

	struct stat buf;
	if (stat (bananaConfigurationFile, &buf) == 0)
		doc = xmlParseFile (bananaConfigurationFile);
	else
		return;

	if (!doc)
		return;

	root = xmlDocGetRootElement (doc);

	for (node = root->xmlChildrenNode; node; node = node->next)
		if (xmlStrcmp (node->name, BAD_CAST "plugin") == 0)
		{
			xmlChar *pluginName = xmlGetProp (node, BAD_CAST "name");

			if (pluginName && 
			   xmlStrcmp(pluginName, BAD_CAST p->name) == 0)
			{
				processPluginNodeInConfigFile (doc, node, p, FIRST);
				xmlFree (pluginName);
				break;
			}

			if (pluginName)
				xmlFree (pluginName);

		}

	xmlFreeDoc (doc);
}

static int
loadMetadataForPlugin (const char* pluginName)
{
	xmlDocPtr doc;
	char *path;
	int bananaIndex = -1, i;

	path = malloc (sizeof (char) * 
	                                  (strlen (bananaMetaDataDir) + 1 +
	                                   strlen (pluginName) + 
	                                   strlen (EXTENSION) + 1));

	sprintf (path, "%s/%s%s", bananaMetaDataDir, pluginName, EXTENSION);

	doc = xmlParseFile (path);

	free (path);
	if (!doc)
		return -1;

	//find an empty slot in bananaTree
	for (i = 0; i <= MAX_NUM_PLUGINS - 1; i++)
	{
		if (!bananaTree[i].name)
		{
			bananaTree[i].nOption = 0;
			bananaTree[i].name = NULL;
			bananaTree[i].bytes = 20 * sizeof (BananaOption);
			bananaTree[i].option = malloc (bananaTree[i].bytes);
			bananaTree[i].name = strdup (pluginName);
			bananaTree[i].list = NULL;

			bananaIndex = i;
			break;
		}
	}

	if (bananaIndex == -1) //bananaTree is full
	{
		xmlFreeDoc (doc);
		return -1;
	}

	processFusilliNode (doc, bananaIndex);

	xmlFreeDoc (doc);

	return bananaIndex;
}

static void
confFileChanged (const char *name,
                 void       *closure)
{
	int i;

	if (strcmp(name, strrchr(bananaConfigurationFile, '/') + 1) == 0)
		for (i = 0; i <= MAX_NUM_PLUGINS - 1; i++)
			if (bananaTree[i].name)
				loadOptionsForPlugin (i, FALSE);

}
/********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************
*********************************************************************/



void
bananaInit (const char *metaDataDir,
            const char *configurationFile)
{
	xmlInitParser ();

	LIBXML_TEST_VERSION;

	int i;

	for (i = 0; i <= MAX_NUM_PLUGINS - 1; i++)
	{
		bananaTree[i].name    = NULL;
		bananaTree[i].option  = NULL;
		bananaTree[i].nOption = 0;
		bananaTree[i].bytes   = 0;
	}

	bananaMetaDataDir       = strdup (metaDataDir);
	int size = strlen(configurationFile) - 
	           strlen(strrchr(configurationFile, '/'));

	bananaConfigurationDir = malloc (sizeof (char) * size + 1);
	strncpy (bananaConfigurationDir, configurationFile, size);
	bananaConfigurationDir[size] = '\0';

	bananaConfigurationFile = strdup (configurationFile);

	//watch the directory, not the file
	//if the file gets deleted, the inode changes and the inotify handle is lost
	directoryWatch = addFileWatch (bananaConfigurationDir,
	                               NOTIFY_DELETE_MASK |
	                               NOTIFY_CREATE_MASK |
	                               NOTIFY_MODIFY_MASK |
	                               NOTIFY_MOVE_MASK,
	                               confFileChanged, 0);
}

void
bananaFini (void)
{
	int i, j;

	for (i = 0; i <= MAX_NUM_PLUGINS - 1; i++)
	{
		if (bananaTree[i].name)
		{
			for (j = 0; j <= bananaTree[i].nOption - 1; j++)
				finiBananaOption (&bananaTree[i].option[j]);
			free (bananaTree[i].option);
		}
	}

	if (directoryWatch)
		removeFileWatch (directoryWatch);

	xmlCleanupParser ();
}

int
bananaLoadPlugin (const char *pluginName)
{
	int bananaIndex = loadMetadataForPlugin (pluginName);

	if (bananaIndex >= 0)
		loadOptionsForPlugin (bananaIndex, TRUE);

	return bananaIndex;
}

int
bananaGetPluginIndex (const char *pluginName)
{
	int i;

	for (i = 0; i < MAX_NUM_PLUGINS; i++)
		if (bananaTree[i].name && strcmp (bananaTree[i].name, pluginName) == 0)
			return i;

	return -1; //plugin not present in bananaTree
}

void
bananaAddChangeNotifyCallBack (int                        bananaIndex,
                               BananaChangeNotifyCallBack callback)
{
	BananaPlugin *p = bananaIndexToBananaPlugin (bananaIndex);

	if (callback == NULL)
	{
		compLogMessage ("core", CompLogLevelError,
		              "Null callback given to bananaAddChangeNotifyCallBack\n");
		return;
	}

	if (!p)
		return;

	if (p->list == NULL)
	{
		p->list = malloc (sizeof (BananaChangeNotifyCallBackNode));
		p->list->callback = callback;
		p->list->next = NULL;
	}
	else
	{
		BananaChangeNotifyCallBackNode *i = p->list;

		while (i->next != NULL)
			i = i->next;

		i->next = malloc (sizeof (BananaChangeNotifyCallBackNode));
		i->next->callback = callback;
		i->next->next = NULL;
	}
}

void
bananaRemoveChangeNotifyCallBack (int                        bananaIndex,
                                  BananaChangeNotifyCallBack callback)
{
	BananaPlugin *p = bananaIndexToBananaPlugin (bananaIndex);

	if (!p)
		return;

	if (callback == NULL)
	{
		compLogMessage ("core", CompLogLevelError,
		              "Null callback given to bananaAddChangeNotifyCallBack\n");
		return;
	}

	BananaChangeNotifyCallBackNode *i = p->list, *prev = NULL;

	while (i != NULL && i->callback != callback)
	{
		prev = i;
		i = i->next;
	}

	if (i == NULL) //callback not found
	{
		compLogMessage ("core", CompLogLevelError, 
		                "Callback given to bananaRemoveChangeNotifyCallBack"
		                "not found\n");
		return;
	}

	if (prev != NULL)
		prev->next = i->next;

	if (i == p->list)
		i = NULL;

	free (i);
}

void
bananaUnloadPlugin (const int bananaIndex)
{
	BananaPlugin *p = bananaIndexToBananaPlugin (bananaIndex);

	if (!p)
		return;

	BananaChangeNotifyCallBackNode *n = p->list;

	while (n != NULL)
	{
		BananaChangeNotifyCallBackNode *keep = n->next;
		free (n);
		n = keep;
	}

	int i;
	for (i = 0; i <= p->nOption - 1; i++)
		finiBananaOption (&p->option[i]);

	p->nOption = 0;

	free (p->name);
	free (p->option);

	p->bytes  = 0;
	p->option = NULL;
	p->name   = NULL;

}

const BananaValue *
bananaGetOption (const int  bananaIndex,
                 const char *optionName,
                 const int  screenNum)
{
	BananaPlugin *p = bananaIndexToBananaPlugin (bananaIndex);

	if (!p)
		return NULL;

	BananaValue  *v = NULL;

	int i;
	for (i = 0; i <= p->nOption - 1; i++)
	{
		if (strcmp (p->option[i].name, optionName) == 0)
		{
			if (screenNum == -1)
				v = &p->option[i].value;
			else
				v = &(p->option[i].valuePerScreen[screenNum]);
		}
	}

	return v;
}

void
bananaSetOption (int         bananaIndex,
                 const char  *optionName,
                 int         screenNum,
                 BananaValue *value)
{
	BananaPlugin *p = bananaIndexToBananaPlugin (bananaIndex);

	if (!p)
		return;

	BananaValue  *v = NULL;

	int i;
	for (i = 0; i <= p->nOption - 1; i++)
	{
		if (strcmp (p->option[i].name, optionName) == 0)
		{
			if (screenNum == -1)
				v = &p->option[i].value;
			else
				v = &(p->option[i].valuePerScreen[screenNum]);
			break;
		}
	}

	if (!isEqualBananaValue(v, value, p->option[i].type))
	{
		finiBananaValue (v, p->option[i].type);
		copyBananaValue (v, value, p->option[i].type);

		BananaChangeNotifyCallBackNode *n = p->list;

		while (n != NULL)
		{
			(n->callback) (p->option[i].name,
			               p->option[i].type,
			               v,
			               screenNum);

			n = n->next;
		}
	}
}

BananaValue *
getArgNamed (const char         *name,
             BananaArgument     *arg,
             int                nArg)
{
	int i;

	for (i = 0; i <= nArg - 1; i++)
		if (strcmp(arg[i].name, name) == 0)
			return &arg[i].value;

	return NULL;
}



