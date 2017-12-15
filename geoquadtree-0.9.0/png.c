/*

png.c - GeoQuadTree PNG interface

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
#include <png.h>

#include "png.h"
#include "fcgi.h"

extern int verbose_level;

/******************************************************************************/

void my_png_write_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
  output_buffer(data, length);
}

/******************************************************************************/

void my_png_flush_data(png_structp png_ptr)
{
}

/******************************************************************************/

int readtile(gqt *g, char *filetile, unsigned char *tiles,
             unsigned numtilesx, unsigned numtilesy,
             unsigned coltile, unsigned rowtile)
{
  FILE *fp;
  unsigned row;
  png_bytep *row_pointers;
  png_structp png_ptr;
  png_infop info_ptr;

  if (verbose_level>1)
  {
    printf("readtile filetile=%s\n", filetile);
    printf("numtilesx=%u numtilesy=%u coltile=%u rowtile=%u\n",
           numtilesx, numtilesy, coltile, rowtile);
  }

  fp=fopen(filetile, "rb");
  if (fp==NULL) return 0;
  else
  {	
    tiles+=(numtilesx*g->tilesizex*(numtilesy-rowtile-1)+coltile)*g->tilesizey*4;

    row_pointers=malloc(g->tilesizey*sizeof(png_bytep *));
    if (row_pointers==NULL) { fprintf(stderr, "readtile: malloc\n"); exit(1); }

    for (row=0; (row<g->tilesizey); row++)
    {
      row_pointers[row]=tiles;
      tiles+=(numtilesx*g->tilesizex*4);
    }

    png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    { fprintf(stderr, "readtile: png_create_read_struct\n"); return 0; }

    info_ptr=png_create_info_struct(png_ptr);
    if (info_ptr==NULL)
    {
      fprintf(stderr, "readtile: png_create_info_struct\n");
      png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
      return 1;
    }

    png_init_io(png_ptr, fp);

    png_read_info(png_ptr, info_ptr);
    //png_set_strip_16(png_ptr);
    png_read_image(png_ptr, row_pointers);

    fclose(fp);

    png_destroy_info_struct(png_ptr, &info_ptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    free(row_pointers);
  }

  return 1;
}

/******************************************************************************/

image *read_png(char *filename)
{
  image *im;
  FILE *fp;
  png_structp png_ptr;
  png_infop info_ptr;
  png_bytep *row_pointers;
  png_byte color_type;
  png_byte bit_depth;
  png_byte interlace_type;
  unsigned char *image, *p;
  unsigned row;
  unsigned char *row_image, *p_src, *p_dst;
  unsigned long l;

  if (verbose_level>1)
    printf("read_png filename=%s\n", filename);

  fp=fopen(filename, "rb");
  if (fp==NULL)
  { printf("read_png: fopen %s for reading\n", filename); exit(1); }

  im=malloc(sizeof(image));
  if (im==NULL) { fprintf(stderr, "read_png: malloc image\n"); return NULL; }

  png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr)
  { fprintf(stderr, "read_png: png_create_read_struct\n"); return NULL; }

  info_ptr=png_create_info_struct(png_ptr);
  if (info_ptr==NULL)
  {
    fprintf(stderr, "read_png: png_create_info_struct\n");
    png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
    return NULL;
  }

  png_init_io(png_ptr, fp);

  png_read_info(png_ptr, info_ptr);
  //png_set_strip_16(png_ptr);

  im->width = info_ptr->width;
  im->height = info_ptr->height;
  color_type = info_ptr->color_type;
  bit_depth = info_ptr->bit_depth;
  interlace_type = info_ptr->interlace_type;

  if (interlace_type!=0)
  {
    fprintf(stderr, "read_png: interlace type %i not implemented\n",
            interlace_type);
    return NULL;
  }

  if (verbose_level>1)
  {
    printf("read_png ncols=%lu nrows=%lu", im->width, im->height);
    printf("color_type=%i bit_depth=%i interlace_type=%i\n",
           color_type, bit_depth, interlace_type);
  }

  im->buffer=malloc(im->width*im->height*4L);
  if (im==NULL) { fprintf(stderr, "read_png: malloc\n"); return NULL; }

  row_pointers=malloc(im->height*sizeof(png_bytep *));
  if (row_pointers==NULL) { fprintf(stderr, "read_png: malloc\n"); return NULL; }

  if (color_type==PNG_COLOR_TYPE_RGB)
  {
    row_image=malloc(im->width*3L);
    if (row_image==NULL) { fprintf(stderr, "read_png: malloc\n"); return NULL; }

    p_dst=im->buffer;

    for (row=0; (row<im->height); row++)
    {
      png_read_row(png_ptr, row_image, NULL);

      p_src=row_image;
      for (l=0; (l<im->width); l++)
      {
        *p_dst++=*p_src++;
        *p_dst++=*p_src++;
        *p_dst++=*p_src++;
        *p_dst++=255;
      }
    }

    free(row_image);
  }
  else if (color_type==PNG_COLOR_TYPE_RGBA)
  {
    p=im->buffer;
    for (row=0; (row<im->height); row++)
    {
      row_pointers[row]=p;
      p+=(im->width*4);
    }

    png_read_image(png_ptr, row_pointers);
  }
  
  fclose(fp);

  png_destroy_info_struct(png_ptr, &info_ptr);
  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

  free(row_pointers);
  
  return im;
}

/******************************************************************************/

int write_png(image *im, char *filename)
{
  FILE *fp=NULL;
  png_bytep *row_pointers;
  png_structp png_ptr;
  png_infop info_ptr;
  unsigned row;
  unsigned char *p_buffer;

  if (verbose_level>1)
    printf("write_png filename=%s\n", filename);

  if (filename!=NULL)
  {
    fp=fopen(filename, "wb");
    if (fp==NULL)
    {
      fprintf(stderr, "write_png: fopen %s for writing\n", filename);
      return 1;
    }
  }

  row_pointers=malloc(im->height*sizeof(png_bytep *));
  if (row_pointers==NULL) { fprintf(stderr, "write_png: malloc\n"); return 1; }

  p_buffer=im->buffer;
  for (row=0; (row<im->height); row++)
  {
    row_pointers[row]=p_buffer;
    p_buffer+=(im->width*4);
  }

  png_ptr=png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr) exit(1);

  info_ptr=png_create_info_struct(png_ptr);
  if (info_ptr==NULL)
  {
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    exit(1);
  }

  if (setjmp(png_jmpbuf(png_ptr)))
  { fprintf(stderr, "write_png: setjmp\n"); return 1; }

  if (filename==NULL)
    png_set_write_fn(png_ptr, NULL, my_png_write_data, my_png_flush_data);
  else
    png_init_io(png_ptr, fp);

  if (setjmp(png_jmpbuf(png_ptr)))
  { fprintf(stderr, "write_png: setjmp\n"); exit(1); }

  png_set_IHDR(png_ptr, info_ptr, im->width, im->height, 8,
      PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_write_info(png_ptr, info_ptr);

  if (setjmp(png_jmpbuf(png_ptr)))
  { fprintf(stderr, "write_png: setjmp\n"); return 1; }

  png_write_image(png_ptr, row_pointers);

  if (setjmp(png_jmpbuf(png_ptr)))
  { fprintf(stderr, "write_png: setjmp\n"); return 1; }

  png_write_end(png_ptr, info_ptr);

  if (filename!=NULL) fclose(fp);

  free(row_pointers);

  png_destroy_info_struct(png_ptr, &info_ptr);
  png_destroy_write_struct(&png_ptr, &info_ptr);

  return 0;
}

/******************************************************************************/
