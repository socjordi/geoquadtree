/*

resample.c - GeoQuadTree Resampling

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
#include <string.h>

#include "proj.h"

double r_resample(double);
double r_resample_calc(double);

double *r_buffer;

#define BICUBIC_TABLE 100000

extern int verbose_level;

/******************************************************************************/

void init_resample(void)
{
  int i;
  double x;

  r_buffer=malloc(BICUBIC_TABLE*sizeof(double));

  for (i=0; (i<BICUBIC_TABLE); i++)
  {
    x=4-(double)i*8/BICUBIC_TABLE;
    r_buffer[i]=r_resample_calc(x);
  }
}

/******************************************************************************/

double r_resample(double x)
{
  int i;

  if (x<-4) return r_resample_calc(x);
  if (x>4)  return r_resample_calc(x);

  i=(BICUBIC_TABLE/8)*(4-x);

  return r_buffer[i];
}

/******************************************************************************/

double r_resample_calc(double x)
{
  double r, d;

  r=0;
  d=x+2;

  if (d>0) r=r+d*d*d*0.166666666666666666666666;
  d-=1;

  if (d>0) r=r-d*d*d*0.666666666666666666666666;
  d-=1;

  if (d>0) r=r+d*d*d;
  d-=1;

  if (d>0) r=r-d*d*d*0.666666666666666666666666;

  return r;
}

/******************************************************************************/

int resample_bicubic(unsigned char *image_src,
             unsigned long width_src, unsigned long height_src,
             srs *srs_src,
             double xmin_src, double ymin_src, double xmax_src, double ymax_src,
             unsigned char *image_dst,
             unsigned long width_dst, unsigned long height_dst,
             srs *srs_dst,
             double xmin_dst, double ymin_dst, double xmax_dst, double ymax_dst)
{
  double pixel_width_src, pixel_height_src;
  double pixel_width_dst, pixel_height_dst;
  long col_src, row_src;
  long col_dst, row_dst;
  long i, j, m, n;
  double x_dst, y_dst;
  double dx, dy;
  double f_r, f_g, f_b, f_a;
  double ff;
  unsigned long p_src;
  unsigned long p_dst;
  double *x, *y;
  double xx, yy;

  pixel_width_src=(xmax_src-xmin_src)/width_src;
  pixel_height_src=(ymax_src-ymin_src)/height_src;

  pixel_width_dst=(xmax_dst-xmin_dst)/width_dst;
  pixel_height_dst=(ymax_dst-ymin_dst)/height_dst;

  x=malloc(width_dst*sizeof(double));
  if (x==NULL) { printf("malloc x\n"); exit(1); }

  y=malloc(width_dst*sizeof(double));
  if (y==NULL) { printf("malloc y\n"); exit(1); }

  p_dst=0;

  y_dst=ymax_dst;

  for (row_dst=height_dst-1; (row_dst>=0); row_dst--)
  {
    x_dst=xmin_dst;

    for (col_dst=0; (col_dst<width_dst); col_dst++)
    {
      x[col_dst]=x_dst;
      y[col_dst]=y_dst;

      x_dst+=pixel_width_dst;
    }

    if (srs_src!=srs_dst)
	  proj_transform(srs_dst, width_dst, x, y, srs_src);

    for (col_dst=0; (col_dst<width_dst); col_dst++)
    {
      xx=(x[col_dst]-xmin_src)/(xmax_src-xmin_src)*width_src;
      yy=(y[col_dst]-ymin_src)/(ymax_src-ymin_src)*height_src;

      i=(long)xx;
      j=(long)yy;

      dx=xx-(double)i;
      dy=yy-(double)j;

      //dx=x_dst-(xmin_src+i*pixel_width_src);
      //dy=y_dst-(ymin_src+j*pixel_height_src);

      f_r=0;
      f_g=0;
      f_b=0;
      f_a=0;

      for (m=-1; (m<=2); m++)
      for (n=-1; (n<=2); n++)
      {
        col_src=i+m;
        row_src=j+n;

        if (col_src<0) col_src=0;
        if (row_src<0) row_src=0;
        if (col_src>=width_src) col_src=width_src-1;
        if (row_src>=height_src) row_src=height_src-1;

	ff=r_resample(m-dx)*r_resample(dy-n);
		
        p_src=((height_src-row_src-1)*width_src+col_src)*4;

	f_r+=image_src[p_src++]*ff;
        f_g+=image_src[p_src++]*ff;
        f_b+=image_src[p_src++]*ff;
        f_a+=image_src[p_src++]*ff;
      }

      if (f_r<0) f_r=0;
      if (f_g<0) f_g=0;
      if (f_b<0) f_b=0;
      if (f_a<0) f_a=0;

      if (f_r>255) f_r=255;
      if (f_g>255) f_g=255;
      if (f_b>255) f_b=255;
      if (f_a>255) f_a=255;

      image_dst[p_dst++]=f_r;
      image_dst[p_dst++]=f_g;
      image_dst[p_dst++]=f_b;
      image_dst[p_dst++]=f_a;
    }

    y_dst-=pixel_height_dst;
  }
  
  free(x);
  free(y);

  return 0;
}

/******************************************************************************/

int resample_nearest(unsigned char *image_src,
             unsigned long width_src, unsigned long height_src,
             srs *srs_src,
             double xmin_src, double ymin_src, double xmax_src, double ymax_src,
             unsigned char *image_dst,
             unsigned long width_dst, unsigned long height_dst,
             srs *srs_dst,
             double xmin_dst, double ymin_dst, double xmax_dst, double ymax_dst)
{
  double pixel_width_src, pixel_height_src;
  double pixel_width_dst, pixel_height_dst;
  long col_dst, row_dst;
  long i, j;
  double x_dst, y_dst;
  double *x, *y;
  unsigned long p_src;
  unsigned long p_dst;
  double mx, my;
  double fx, fy;
  double fx_inc, fy_inc;

  pixel_width_src=(xmax_src-xmin_src)/width_src;
  pixel_height_src=(ymax_src-ymin_src)/height_src;

  pixel_width_dst=(xmax_dst-xmin_dst)/width_dst;
  pixel_height_dst=(ymax_dst-ymin_dst)/height_dst;

  mx=width_src/(xmax_src-xmin_src);
  my=height_src/(ymax_src-ymin_src);

  fx_inc=pixel_width_dst*mx;
  fy_inc=pixel_height_dst*my;

  if (verbose_level>1)
  {
    printf("resample_nearest pixel_width_src=%f pixel_height_src=%f\n",
           pixel_width_src, pixel_height_src);
    printf("resample_nearest pixel_width_dst=%f pixel_height_dst=%f\n",
           pixel_width_dst, pixel_height_dst);
    printf("resample_nearest mx=%f my=%f\n", mx, my);
    printf("resample_nearest fx_inc=%f fy_inc=%f\n", fx_inc, fy_inc);
  }

  p_dst=0;

  if (srs_src!=srs_dst)
  {
    fy=(ymax_dst-ymin_src)*my;

    for (row_dst=height_dst-1; (row_dst>=0); row_dst--)
    {
      fx=(xmin_dst-xmin_src)*mx;

      for (col_dst=0; (col_dst<width_dst); col_dst++)
      {
        p_src=((double)height_src-fy)*width_src+fx;
        p_src=p_src<<2;

        image_dst[p_dst++]=image_src[p_src++];
        image_dst[p_dst++]=image_src[p_src++];
        image_dst[p_dst++]=image_src[p_src++];
        image_dst[p_dst++]=image_src[p_src];

        fx+=fx_inc;
      }

      fy-=fy_inc;
    }
  }
  else
  {
    x=malloc(width_dst*sizeof(double));
    if (x==NULL) { printf("malloc x\n"); exit(1); }

    y=malloc(width_dst*sizeof(double));
    if (y==NULL) { printf("malloc x\n"); exit(1); }

    y_dst=ymax_dst;

    for (row_dst=height_dst-1; (row_dst>=0); row_dst--)
    {
      x_dst=xmin_dst;

      for (col_dst=0; (col_dst<width_dst); col_dst++)
      {
        x[col_dst]=x_dst;
        y[col_dst]=y_dst;

        x_dst+=pixel_width_dst;
      }

      proj_transform(srs_dst, width_dst, x, y, srs_src);

      for (col_dst=0; (col_dst<width_dst); col_dst++)
      {
        i=(x[col_dst]-xmin_src)*mx;
        j=(y[col_dst]-ymin_src)*my;

        p_src=((height_src-j-1)*width_src+i)*4;

        image_dst[p_dst++]=image_src[p_src++];
        image_dst[p_dst++]=image_src[p_src++];
        image_dst[p_dst++]=image_src[p_src++];
        image_dst[p_dst++]=image_src[p_src];
      }

      y_dst-=pixel_height_dst;
    }

    free(x);
    free(y);
  }

  return 0;
}

/******************************************************************************/

int resample(unsigned char *image_src,
             unsigned long width_src, unsigned long height_src,
             srs *srs_src,
             double xmin_src, double ymin_src, double xmax_src, double ymax_src,
             unsigned char *image_dst,
             unsigned long width_dst, unsigned long height_dst,
             srs *srs_dst,
             double xmin_dst, double ymin_dst, double xmax_dst, double ymax_dst,
             int filter)
{
  if (verbose_level>1)
  {
    printf("resample width_src=%lu height_src=%lu\n", width_src, height_src);

    printf("resample xmin_src=%f ymin_src=%f xmax_src=%f ymax_src=%f\n",
           xmin_src, ymin_src, xmax_src, ymax_src);

    printf("resample width_dst=%lu height_dst=%lu\n", width_dst, height_dst);

    printf("resample xmin_dst=%f ymin_dst=%f xmax_dst=%f ymax_dst=%f\n",
           xmin_dst, ymin_dst, xmax_dst, ymax_dst);
  }

  if (filter==0)
  {
    return resample_nearest(image_src, width_src, height_src,
                            srs_src, xmin_src, ymin_src, xmax_src, ymax_src,
                            image_dst, width_dst, height_dst,
                            srs_dst, xmin_dst, ymin_dst, xmax_dst, ymax_dst);
  }
  else
  {
    return resample_bicubic(image_src, width_src, height_src,
                            srs_src, xmin_src, ymin_src, xmax_src, ymax_src,
                            image_dst, width_dst, height_dst,
                            srs_dst, xmin_dst, ymin_dst, xmax_dst, ymax_dst);
  }
}

/******************************************************************************/
