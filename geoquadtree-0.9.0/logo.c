/*

logo.c - GeoQuadTree OGC WMS Server (logo inclusion on served images)

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

#include "geoquadtree.h"
#include "wms.h"

#define LOGO_TRANSPARENCY 180  /* 0=Transparent, 255=Opaque */

image im_logo;

/******************************************************************************/

int init_logo(service *Service)
{
  FILE *fp;
  int number_of_passes;
  unsigned char *p_image_logo;
  char header[8];
  png_structp png_ptr;
  png_infop info_ptr;
  png_bytep * row_pointers;
  int y;
  int color_type, bit_depth;

  if (Service->Logo==NULL) return 0;

  fp=fopen(Service->Logo, "rb");
  if (fp==NULL) { fprintf(stderr, "init_logo: fopen\n"); return 1; }

  fread(header, 1, 8, fp);
  if (png_sig_cmp((png_bytep)header, 0, 8))
  { fprintf(stderr, "init_logo: logo is not a PNG file\n"); return 1; }

  png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr)
  { fprintf(stderr, "init_logo: png_create_read_struct\n"); return 1; }

  info_ptr=png_create_info_struct(png_ptr);
  if (info_ptr==NULL)
  {
    fprintf(stderr, "init_logo: png_create_info_struct\n");
    png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
    return 1;
  }

  if (setjmp(png_jmpbuf(png_ptr)))
  { fprintf(stderr, "init_logo: setjmp"); return 1; }

  png_init_io(png_ptr, fp);
  png_set_sig_bytes(png_ptr, 8);

  png_read_info(png_ptr, info_ptr);

  im_logo.width = info_ptr->width;
  im_logo.height = info_ptr->height;
  color_type = info_ptr->color_type;
  bit_depth = info_ptr->bit_depth;

  number_of_passes = png_set_interlace_handling(png_ptr);
  png_read_update_info(png_ptr, info_ptr);

  if (setjmp(png_jmpbuf(png_ptr)))
  { fprintf(stderr, "init_logo: setjmp"); return 1; }

  row_pointers = (png_bytep*)malloc(sizeof(png_bytep)*im_logo.height);
  for (y=0; y<im_logo.height; y++)
  {
    row_pointers[y]=(png_byte*) malloc(info_ptr->rowbytes);
    if (row_pointers[y]==NULL)
    { fprintf(stderr, "init_logo: malloc\n"); return 1; }
  }

  png_read_image(png_ptr, row_pointers);

  im_logo.buffer=malloc(im_logo.width*im_logo.height*4);
  if (im_logo.buffer==NULL)
  { fprintf(stderr, "init_logo: malloc\n"); return 1; }

  p_image_logo=im_logo.buffer;
  for (y=0; (y<im_logo.height); y++)
  {
    memcpy(p_image_logo, row_pointers[y], im_logo.width*4);
    p_image_logo+=(im_logo.width*4);
  }

  free(row_pointers);

  png_destroy_info_struct(png_ptr, &info_ptr);
  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

  fclose(fp);

  return 0;
}

/******************************************************************************/

int add_logo(image *im)
{
  unsigned char *p_image, *p_logo_image;
  unsigned char alpha;
  int x, y;

  p_image=im->buffer+4*(im->width*(im->height-im_logo.height)+(im->width-im_logo.width));
  p_logo_image=im_logo.buffer;

  for (y=0; (y<im_logo.height); y++)
  {
    for (x=0; (x<im_logo.width*4); )
    {
      alpha=p_logo_image[x+3]*LOGO_TRANSPARENCY/255;

      p_image[x]=((long)(p_logo_image[x])*(long)alpha+(long)p_image[x]*(255-(long)alpha))/255; x++;
      p_image[x]=((long)(p_logo_image[x])*(long)alpha+(long)p_image[x]*(255-(long)alpha))/255; x++;
      p_image[x]=((long)(p_logo_image[x])*(long)alpha+(long)p_image[x]*(255-(long)alpha))/255; x++;
      p_image[x]=((long)(p_logo_image[x])*(long)alpha+(long)p_image[x]*(255-(long)alpha))/255; x++;
    }

    p_logo_image+=(im_logo.width*4);
    p_image+=(im->width*4);
  }

  return 0;
}

/******************************************************************************/
