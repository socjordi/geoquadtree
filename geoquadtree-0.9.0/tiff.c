/*

tiff.c - GeoQuadTree TIFF interface

Copyright (C) 2006  Jordi Gilabert Vall <geoquadtree at gmail com>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/

/******************************************************************************/

// NOTE: Using TIFFReadRGBAImageOriented requires libtiff >= 3.6.0

#include <stdlib.h>
#include <stdio.h>

#include <xtiffio.h>
#include <geo_config.h>
#include <geokeys.h>

#include "geoquadtree.h"

/******************************************************************************/

image *read_tiff(char *filename)
{
  image *im;
  unsigned long width, height, length;
  TIFF *tiffimage;
  //GTIF *geotiffimage;
  int ret;

  if ((tiffimage=XTIFFOpen(filename, "rb")) == NULL)
  { printf("TIFFOpen\n"); exit(1); }

  TIFFGetField(tiffimage, TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tiffimage, TIFFTAG_IMAGELENGTH, &height);

  im=malloc(sizeof(image));
  if (im==NULL) { fprintf(stderr, "read_tiff: malloc\n"); return NULL; }

  im->width=width;
  im->height=height;

  length=(long)width*(long)height*4L;

  im->buffer=_TIFFmalloc(length);
  if (im->buffer==NULL)
  { fprintf(stderr, "read_tiff: _TIFFmalloc\n"); return NULL; }

  // This function requires that version of libtiff be >= 3.6.0

  ret=TIFFReadRGBAImageOriented(tiffimage, width, height, (uint32 *)im->buffer,
                                ORIENTATION_TOPLEFT, 0);

  //_TIFFfree(image);

  /*
  geotiffimage=GTIFNew(tiffimage);
  	
  GTIFKeyGet(geotiffimage,
	geokey_t key,
	void * val,
	int index,
	int count);

  GTIFFree(geotiffimage);
  */

  TIFFClose(tiffimage);
  
  return im;
}

/******************************************************************************/

int write_tiff(image *im, char *filename)
{
  TIFF *tiffimage;
  unsigned char *imagergb, *rgb, *rgba;
  unsigned col, row;

  imagergb=malloc(im->width*im->height*3L);
  if (imagergb==NULL) { fprintf(stderr, "write_tiff: malloc\n"); return 1; }
  
  rgb=imagergb;
  rgba=im->buffer;

  for (row=0; (row<im->height); row++)
  for (col=0; (col<im->width); col++)
  {
    *rgb++=*rgba++;
    *rgb++=*rgba++;
    *rgb++=*rgba++;
    rgba++;
  }

  if ((tiffimage=TIFFOpen(filename, "wb")) == NULL)
  { fprintf(stderr, "write_tiff: open %s for writing\n", filename); return 1; }

  TIFFSetField(tiffimage, TIFFTAG_IMAGEWIDTH, im->width);
  TIFFSetField(tiffimage, TIFFTAG_IMAGELENGTH, im->height);
  TIFFSetField(tiffimage, TIFFTAG_BITSPERSAMPLE, 8);
  TIFFSetField(tiffimage, TIFFTAG_SAMPLESPERPIXEL, 3);

  TIFFSetField(tiffimage, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
  //TIFFSetField(tiffimage, TIFFTAG_COMPRESSION, COMPRESSION_DEFLATE);
  TIFFSetField(tiffimage, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
  TIFFSetField(tiffimage, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);

  TIFFWriteEncodedStrip(tiffimage, 0, imagergb, im->width*im->height*4L);

  TIFFClose(tiffimage);

  free(imagergb);

  return 0;
}

/******************************************************************************/
