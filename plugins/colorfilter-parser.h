/*
 * Compiz fragment program parser
 *
 * Author : Guillaume Seguin
 * Email : guillaume@segu.in
 *
 * Copyright (c) 2007 Guillaume Seguin <guillaume@segu.in>
 *
 * Copyright (c) 2015 Michail Bitzes <noodlylight@gmail.com>
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

enum {
	NoOp,
	DataOp,
	StoreDataOp,
	OffsetDataOp,
	BlendDataOp,
	FetchOp,
	ColorOp,
	LoadOp,
	TempOp,
	ParamOp,
	AttribOp,
} OpType;

typedef struct _FragmentOffset FragmentOffset;

struct _FragmentOffset {
	char            *name;
	char            *offset;

	FragmentOffset  *next;
};

char *
base_name (char *str);

int
buildFragmentProgram (char       *source,
                      char       *name,
                      CompScreen *s,
                      int        target);

int 
loadFragmentProgram (char       *file,
                     char       *name,
                     CompScreen *s,
                     int        target);
