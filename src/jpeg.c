/*
 * beryl-plugins::jpeg.c - adds JPEG image support to beryl.
 * Copyright: (C) 2006 Nicholas Thomas
 *                Danny Baumann (JPEG writing, option stuff)
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
 *
 */

/* this file is based on compiz plugin imgjpeg */
#define _GNU_SOURCE
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

#include <fusilli-core.h>

#include <X11/Xarch.h>
#include <jpeglib.h>

struct jpegErrorMgr
{
	struct  jpeg_error_mgr pub; /* "public" fields */
	jmp_buf setjmp_buffer;      /* for return to caller */
};

static Bool
rgbToBGRA (const JSAMPLE *source,
           void          **data,
           int           height,
           int           width,
           int           alpha)
{
	int  h, w;
	char *dest;

	dest = malloc (height * width * 4);
	if (!dest)
		return FALSE;

	*data = dest;

	for (h = 0; h < height; h++)
		for (w = 0; w < width; w++)
		{
			int pos = h * width + w;
#if __BYTE_ORDER == __BIG_ENDIAN
			dest[(pos * 4) + 3] = source[(pos * 3) + 2];    /* blue */
			dest[(pos * 4) + 2] = source[(pos * 3) + 1];    /* green */
			dest[(pos * 4) + 1] = source[(pos * 3) + 0];    /* red */
			dest[(pos * 4) + 0] = alpha;
#else
			dest[(pos * 4) + 0] = source[(pos * 3) + 2];    /* blue */
			dest[(pos * 4) + 1] = source[(pos * 3) + 1];    /* green */
			dest[(pos * 4) + 2] = source[(pos * 3) + 0];    /* red */
			dest[(pos * 4) + 3] = alpha;
#endif
	}

	return TRUE;
}

static Bool
rgbaToRGB (char    *source,
           JSAMPLE **dest,
           int     height,
           int     width,
           int     stride)
{
	int     h, w;
	int     ps = stride / width;   /* pixel size */
	JSAMPLE *d;

	d = malloc (height * width * 3 * sizeof (JSAMPLE));
	if (!d)
		return FALSE;

	*dest = d;

	for (h = 0; h < height; h++)
		for (w = 0; w < width; w++)
		{
			int pos = h * width + w;
#if __BYTE_ORDER == __BIG_ENDIAN
			d[(pos * 3) + 0] = source[(pos * ps) + 3];  /* red */
			d[(pos * 3) + 1] = source[(pos * ps) + 2];  /* green */
			d[(pos * 3) + 2] = source[(pos * ps) + 1];  /* blue */
#else
			d[(pos * 3) + 0] = source[(pos * ps) + 0];  /* red */
			d[(pos * 3) + 1] = source[(pos * ps) + 1];  /* green */
			d[(pos * 3) + 2] = source[(pos * ps) + 2];  /* blue */
#endif
	}

	return TRUE;
}

static void
jpegErrorExit (j_common_ptr cinfo)
{
	char                buffer[JMSG_LENGTH_MAX];
	struct jpegErrorMgr *err = (struct jpegErrorMgr *) cinfo->err;

	/* Format the message */
	(*cinfo->err->format_message) (cinfo, buffer);

	printf("%s\n", buffer);

	/* Return control to the setjmp point */
	longjmp (err->setjmp_buffer, 1);
}

static Bool
readJPEGFileToImage (FILE *file,
                     int  *width,
                     int  *height,
                     void **data)
{
	struct jpeg_decompress_struct cinfo;
	struct jpegErrorMgr           jerr;
	JSAMPLE                       *buf;
	JSAMPROW                      *rows;
	int                           i;
	Bool                          result;

	if (!file)
		return FALSE;

	cinfo.err = jpeg_std_error (&jerr.pub);
	jerr.pub.error_exit = jpegErrorExit;

	if (setjmp (jerr.setjmp_buffer))
	{
		/* this is called on decompression errors */
		jpeg_destroy_decompress (&cinfo);
		return FALSE;
	}

	jpeg_create_decompress (&cinfo);

	jpeg_stdio_src (&cinfo, file);

	jpeg_read_header (&cinfo, TRUE);

	cinfo.out_color_space = JCS_RGB;

	jpeg_start_decompress (&cinfo);

	*height = cinfo.output_height;
	*width = cinfo.output_width;

	buf = calloc (cinfo.output_height * cinfo.output_width *
	              cinfo.output_components, sizeof (JSAMPLE));
	if (!buf)
	{
		jpeg_finish_decompress (&cinfo);
		jpeg_destroy_decompress (&cinfo);
		return FALSE;
	}

	rows = malloc (cinfo.output_height * sizeof (JSAMPROW));
	if (!rows)
	{
		free (buf);
		jpeg_finish_decompress (&cinfo);
		jpeg_destroy_decompress (&cinfo);
		return FALSE;
	}

	for (i = 0; i < cinfo.output_height; i++)
		rows[i] = &buf[i * cinfo.output_width * cinfo.output_components];

	while (cinfo.output_scanline < cinfo.output_height)
		jpeg_read_scanlines (&cinfo, &rows[cinfo.output_scanline],
		                     cinfo.output_height - cinfo.output_scanline);

	jpeg_finish_decompress (&cinfo);
	jpeg_destroy_decompress (&cinfo);

	/* convert the rgb data into BGRA format */
	result = rgbToBGRA (buf, data, *height, *width, 255);

	free (rows);
	free(buf);
	return result;
}

static Bool
writeJPEG (void        *buffer,
           FILE        *file,
           int         width,
           int         height,
           int         stride)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr       jerr;
	JSAMPROW                    row_pointer[1];
	JSAMPLE                     *data;

	/* convert the rgb data into BGRA format */
	if (!rgbaToRGB (buffer, &data, height, width, stride))
		return FALSE;

	cinfo.err = jpeg_std_error (&jerr);
	jpeg_create_compress (&cinfo);

	jpeg_stdio_dest (&cinfo, file);

	cinfo.image_width      = width;
	cinfo.image_height     = height;
	cinfo.input_components = 3;
	cinfo.in_color_space   = JCS_RGB;

	const BananaValue *
	option_jpeg_quality = bananaGetOption (coreBananaIndex,
	                                       "jpeg_quality",
	                                       -1);

	jpeg_set_defaults (&cinfo);
	jpeg_set_quality (&cinfo, option_jpeg_quality->i, TRUE);
	jpeg_start_compress (&cinfo, TRUE);

	while (cinfo.next_scanline < cinfo.image_height)
	{
		row_pointer[0] =
		    &data[(cinfo.image_height - cinfo.next_scanline - 1) * width * 3];
		jpeg_write_scanlines (&cinfo, row_pointer, 1);
	}

	jpeg_finish_compress (&cinfo);
	jpeg_destroy_compress (&cinfo);

	free (data);

	return TRUE;
}

/* Turns the path & name into a real, absolute path
   No extensions jiggery-pokery here, as JPEGs can be
   .jpg or .jpeg - or, indeed, whatever.
   Deals with the path, regardless of what it's passed. */
static char*
createFilename (const char *path,
                const char *name)
{
	char *filename = NULL;
	int retval;

	if (path && !name)
		retval = asprintf (&filename, "%s", path);
	else if (!path && name)
		retval = asprintf (&filename, "%s", name);
	else
		retval = asprintf (&filename, "%s/%s", path, name);

	if (retval != -1)
		return filename;
	else
		return NULL;
}

Bool
JPEGImageToFile (const char  *path,
                 const char  *name,
                 int         width,
                 int         height,
                 int         stride,
                 void        *data)
{
	Bool status = FALSE;
	char *fileName;
	FILE *file;

	fileName = createFilename (path, name);
	if (!fileName)
		return FALSE;

	file = fopen (fileName, "wb");
	if (file)
	{
		status = writeJPEG (data, file, width, height, stride);
		fclose (file);
	}

	free (fileName);
	return status;
}

Bool
JPEGFileToImage (const char  *path,
                 const char  *name,
                 int         *width,
                 int         *height,
                 int         *stride,
                 void        **data)
{
	Bool status = FALSE;
	char *fileName, *extension;

	fileName = createFilename (path, name);
	if (!fileName)
		return FALSE;

	/* Do some testing here to see if it's got a .jpg or .jpeg extension */
	extension = strrchr (fileName, '.');
	if (extension)
	{
		if (strcasecmp (extension, ".jpeg") == 0 ||
		    strcasecmp (extension, ".jpg") == 0)
		{
			FILE *file;

			file = fopen (fileName, "rb");
			if (file)
			{
				status = readJPEGFileToImage (file, width, height, data);
				fclose (file);

				if (status)    /* Success! */
				{
					free (fileName);
					*stride = *width * 4;
					return TRUE;
				}
			}
		}
	}
	free (fileName);

	return status;
}


