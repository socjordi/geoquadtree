/*

jpg.c - GeoQuadTree JPEG interface

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

#include <stdlib.h>
#include <stdio.h>
#include <jpeglib.h>

#include "geoquadtree.h"
#include "jpg.h"
#include "fcgi.h"

#define JPEG_QUALITY 85
#define BUFFER_SIZE 8192

extern int verbose_level;

unsigned char *buffer;

/******************************************************************************/

static void jpg_init_destination(struct jpeg_compress_struct *cinfo)
{
  cinfo->dest->next_output_byte=buffer;
  cinfo->dest->free_in_buffer=BUFFER_SIZE;
}

/******************************************************************************/

boolean jpg_empty_output_buffer(struct jpeg_compress_struct *cinfo)
{
  output_buffer(buffer, BUFFER_SIZE);

  cinfo->dest->next_output_byte=buffer;
  cinfo->dest->free_in_buffer=BUFFER_SIZE;

  return TRUE;
}

/******************************************************************************/

static void jpg_term_destination(struct jpeg_compress_struct *cinfo)
{
  output_buffer(buffer, BUFFER_SIZE - cinfo->dest->free_in_buffer);
}

/******************************************************************************/

image *read_jpg(char *filename)
{
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  FILE *fp;
  unsigned char *s, *t;
  JSAMPARRAY line;
  int row_stride;
  unsigned i;
  image *im;

  if (verbose_level>1)
    printf("read_jpg %s\n", filename);

  cinfo.err = jpeg_std_error(&jerr);

  jpeg_create_decompress(&cinfo);

  if ((fp = fopen(filename, "rb")) == NULL)
  {
    fprintf(stderr, "read_jpg: fopen %s for reading\n", filename);
    return NULL;
  }

  jpeg_stdio_src(&cinfo, fp);

  jpeg_read_header(&cinfo, TRUE);
  
  im=malloc(sizeof(image));
  if (im==NULL) { fprintf(stderr, "read_jpg: malloc\n"); return NULL; }

  im->width=cinfo.image_width;
  im->height=cinfo.image_height;

  if (verbose_level>1)
    printf("read_jpg width=%li height=%li\n", im->width, im->height);
      
  im->buffer=malloc(im->width*im->height*4);
  if (im->buffer==NULL) { fprintf(stderr, "read_jpg: malloc\n"); return NULL; }

  jpeg_start_decompress(&cinfo);
  
  row_stride = cinfo.output_width * cinfo.output_components;
  line = (*cinfo.mem->alloc_sarray)
		((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

  t=im->buffer;
		
  while (cinfo.output_scanline < im->height)
  {
    jpeg_read_scanlines(&cinfo, line, 1);

    s=(unsigned char*)line;
    
    for (i=0; (i<im->width); i++)
    {
      *t++=*s++;
      *t++=*s++;
      *t++=*s++;
      *t++=255;
    }    
  }
  
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
    
  fclose(fp);
  
  return im;
}

/******************************************************************************/

int write_jpg(image *im, char *filename)
{
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  unsigned char *line;
  FILE *fp=NULL;
  int i;
  unsigned char *s, *t;
  JSAMPROW row_pointer[1];

  buffer=malloc(BUFFER_SIZE);
  if (buffer==NULL) { fprintf(stderr, "write_jpg: malloc\n"); return 1; }

  line=malloc(im->width*3);
  if (line==NULL) { fprintf(stderr, "write_jpg: malloc\n"); return 1; }

  cinfo.err=jpeg_std_error(&jerr);

  jpeg_create_compress(&cinfo);

  if (filename==NULL)
  {
    if (cinfo.dest==NULL)
      cinfo.dest=(struct jpeg_destination_mgr *)
                 malloc(sizeof(struct jpeg_destination_mgr));

    cinfo.dest->init_destination=jpg_init_destination;
    cinfo.dest->empty_output_buffer=jpg_empty_output_buffer;
    cinfo.dest->term_destination=jpg_term_destination;
  }
  else
  {
    if ((fp=fopen(filename, "wb"))==NULL)
    {
      fprintf(stderr, "write_jpg: open %s for writing\n", filename);
      return 1;
    }

    jpeg_stdio_dest(&cinfo, fp);
  }

  cinfo.image_width = im->width;
  cinfo.image_height = im->height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;

  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, JPEG_QUALITY, TRUE);

  jpeg_start_compress(&cinfo, TRUE);

  s=im->buffer;
  row_pointer[0]=line;
  while (cinfo.next_scanline < im->height)
  {
    t=line;

    for (i=0; (i<im->width); i++)
    {
      *t++=*s++;
      *t++=*s++;
      *t++=*s++;
      s++;
    }

    jpeg_write_scanlines(&cinfo, row_pointer, 1);
  }

  jpeg_finish_compress(&cinfo);

  jpeg_destroy_compress(&cinfo);
  free(line);
  free(buffer);

  if (filename!=NULL) fclose(fp);

  return 0;
}

/******************************************************************************/
