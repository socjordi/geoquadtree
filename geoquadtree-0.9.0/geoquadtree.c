/*

geoquadtree.c - GeoQuadTree core

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
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <wand/magick-wand.h>
#include <gdal.h>
#include <gdal_version.h>
#include <cpl_string.h>
#include <cpl_port.h>
#include <cpl_error.h>

#include "geoquadtree.h"
#include "xml.h"
#include "png.h"
#include "jpg.h"
#include "tiff.h"
#include "proj.h"
#include "resample.h"

/******************************************************************************/

struct string
{
  char *name;
  struct string *next;
};

struct string *tiles;

extern int verbose_level;

/******************************************************************************/

#define ThrowWandException(wand) \
{ \
  char *description; \
  ExceptionType severity; \
  description=MagickGetException(wand,&severity); \
  (void) fprintf(stderr, "%s %s %ld %s\n", GetMagickModule(), description); \
  description=(char *) MagickRelinquishMemory(description); \
  exit(-1); \
}

/******************************************************************************/

struct string * addstring(struct string *s, char *name)
{
  struct string *p;

  p=malloc(sizeof(struct string));
  if (p==NULL) { printf("ERROR malloc addstring\n"); exit(1); }
  p->name=malloc(strlen(name)+1);
  if (p->name==NULL) { printf("ERROR malloc addstring\n"); exit(1); }
  strcpy(p->name, name);

  p->next=s;
  s=p;

  return s;
}

/******************************************************************************/

void delstrings(struct string *s)
{
  struct string *p;

  while (s!=NULL) { p=s->next; free(s); s=p; }
}

/******************************************************************************/

int exists(struct string *s, char *name)
{
  while (s!=NULL)
  {
    if (strcmp(s->name, name)==0) return 1;
    s=s->next;
  }

  return 0;
}

/******************************************************************************/

void scanfloats(char *str, double d[], int n)
{
  int stri, si, di;
  char c;
  char s[1024];

  stri=0; si=0; di=0;

  while (1)
  {
    c=str[stri++];

    if ((c==',')||(c==0))
      { s[si]=0; d[di++]=atof(s); si=0; if ((c==0)||(di==n)) break; }
    else
      s[si++]=c;
  }
}

/******************************************************************************/

void scanints(char *str, int d[], int n)
{
  int stri, si, di;
  char c;
  char s[1024];

  stri=0; si=0; di=0;

  while (1)
  {
    c=str[stri++];

    if ((c==',')||(c==0))
      { s[si]=0; d[di++]=atoi(s); si=0; if ((c==0)||(di==n)) break; }
    else
      s[si++]=c;
  }
}

/******************************************************************************/

void makedir(char *basepath, char *path)
{
  char *pch;
  char dir[1024];
  struct stat stats;

  strcpy(dir, basepath);
  pch=strtok(path, "/");

  while (pch!=NULL)
  {
    sprintf(dir, "%s/%s", dir, pch);
    if (stat(dir, &stats))
      mkdir(dir, S_IREAD | S_IWRITE | S_IEXEC);

    pch=strtok(NULL, "/");
  }
}

/******************************************************************************/

int pyramid_level(gqt *g, double pixel_width, double pixel_height,
                  unsigned *level,
                  double *raster_pixel_width, double *raster_pixel_height)
{
  *raster_pixel_width=g->resx;
  *raster_pixel_height=g->resy;

  for (*level=0; (*level<256); *level=(*level)+1)
  {
    if (verbose_level>1)
    {
      printf("\t\tlevel=%3i", *level);
      printf("\t\traster_pixel_width=%f", *raster_pixel_width);
      printf("\t\traster_pixel_height=%f\n", *raster_pixel_height);
    }

    if ((*raster_pixel_width*2>pixel_width)&&
        (*raster_pixel_height*2>pixel_height)) return 0;

    *raster_pixel_width=(*raster_pixel_width)*2;
    *raster_pixel_height=(*raster_pixel_height)*2;
  }
    
  return 1;
}

/******************************************************************************/

int xy2filetile(gqt *g, double x, double y, unsigned level, char *filetile)
{
  double xmin, ymin, xmax, ymax;
  double xm, ym;
  unsigned l;

  xmax=g->resx*g->tilesizex*(1<<(g->levels-1));
  ymax=g->resy*g->tilesizey*(1<<(g->levels-1));
  xmin=-xmax;
  ymin=-ymax;

  if (verbose_level>1)
  {
    printf("resx=%f tilesizex=%i levels=%i\n", g->resx, g->tilesizex, g->levels);
    printf("xy2filetile xmin=%f ymin=%f xmax=%f ymax=%f\n", xmin, ymin, xmax, ymax);
    printf("xy2filetile x=%f y=%f level=%u\n", x, y, level);
  }

  if (x<xmin) return 0;
  if (y<ymin) return 0;
  if (x>xmax) return 0;
  if (y>ymax) return 0;

  for (l=g->levels; (l>level); l--)
  {
    xm=(xmin+xmax)/2;
    ym=(ymin+ymax)/2;

    if (x<xm)
    {
      if (y>ym) { strcat(filetile, "/1"); xmax=xm; ymin=ym; }
      else      { strcat(filetile, "/4"); xmax=xm; ymax=ym; }
    }
    else
    {
      if (y>ym) { strcat(filetile, "/2"); xmin=xm; ymin=ym; }
      else      { strcat(filetile, "/3"); xmin=xm; ymax=ym; }
    }
  }

  return 1;
}

/******************************************************************************/

void extract(image *im, image *tile)
{
  long i, j;
  long x, y;
  long p_src, p_dst;
  long xmin, ymin, xmax, ymax;
  long xtmin, ytmin, xtmax, ytmax;

  /*
  if ((im->resx!=tile->resx)||(im->resy!=tile->resy))
  {
    fprintf(stderr, "extract: different resolutions not implemented\n");
    fprintf(stderr, "         im: %f %f tile: %f %f\n", im->resx, im->resy, tile->resx, tile->resy);
    return;
  }
  */

  if (verbose_level>1)
  {
    printf("extract im   xmin=%f ymin=%f xmax=%f ymax=%f\n",
           im->minx, im->miny, im->maxx, im->maxy);
    printf("        tile xmin=%f ymin=%f xmax=%f ymax=%f\n",
           tile->minx, tile->miny, tile->maxx, tile->maxy);
  }

  xmin=im->minx/im->resx;
  ymin=im->miny/im->resy;
  xmax=im->maxx/im->resx;
  ymax=im->maxy/im->resy;

  xtmin=tile->minx/tile->resx;
  ytmin=tile->miny/tile->resy;
  xtmax=tile->maxx/tile->resx;
  ytmax=tile->maxy/tile->resy;

  p_dst=0;

  y=ytmax;

  for (j=0; (j<tile->height); j++)
  {
    if ((ymax>=y)&&(y>=ymin))
    {
      if ((xmin<=xtmin)&&(xtmax<=xmax))
      {
        p_src=((ymax-y)*im->width+xtmin-xmin)*4;
        memcpy(&(tile->buffer[p_dst]), &(im->buffer[p_src]), tile->width*4);
        p_dst+=tile->width*4;
      }
      else
      {
        x=xtmin;

        p_src=((ymax-y)*im->width+xtmin-xmin)*4;

        for (i=0; (i<tile->width); i++)
        {
          if ((x>=xmin)&&(x<=xmax))
          {
            tile->buffer[p_dst++]=im->buffer[p_src++];
            tile->buffer[p_dst++]=im->buffer[p_src++];
            tile->buffer[p_dst++]=im->buffer[p_src++];
            tile->buffer[p_dst++]=im->buffer[p_src++];
          }
          else
          {
            tile->buffer[p_dst++]=0;
            tile->buffer[p_dst++]=0;
            tile->buffer[p_dst++]=0;
            tile->buffer[p_dst++]=0;

            p_src+=4;
          }

          x++;
        }
      }
    }
    else
    {
      for (i=0; (i<tile->width); i++)
      {
        tile->buffer[p_dst++]=0;
        tile->buffer[p_dst++]=0;
        tile->buffer[p_dst++]=0;
        tile->buffer[p_dst++]=0;
      }
    }

    y--;
  }
}

/******************************************************************************/

image *read_image(char *filename)
{
  GDALDatasetH hDataset;
  GDALRasterBandH hBand;
  int nbands, nband;
  unsigned long l, length;
  double padfTransform[6];
  image *im;

  if (verbose_level>1) printf("read_image %s\n", filename);

  hDataset=GDALOpen(filename, GA_ReadOnly);
  if (hDataset==NULL) { fprintf(stderr, "read_image: GDALOpen\n"); return NULL; }

  im=malloc(sizeof(image));
  if (im==NULL) { fprintf(stderr, "read_image: malloc im\n"); exit(1); }

  im->width=GDALGetRasterXSize(hDataset);
  im->height=GDALGetRasterYSize(hDataset);

  nbands=GDALGetRasterCount(hDataset);
  if ((nbands!=1)&&(nbands!=3))
  { fprintf(stderr, "%i bands not implemented\n", nbands); exit(1); }

  if (GDALGetGeoTransform(hDataset, padfTransform)==CE_Failure)
  { fprintf(stderr, "ERROR: The image is not georeferenced\n"); exit(1); }

  if ((padfTransform[2]!=0)||(padfTransform[4]!=0))
  {
    fprintf(stderr, "read_image: image is not North Up, not implemented yet\n");
    exit(1);
  }

  for (l=0; (l<6); l++) printf("\t%lu => %f\n", l, padfTransform[l]);

  im->resx=padfTransform[1];
  im->resy=-padfTransform[5];

  im->minx=padfTransform[0];
  im->maxy=padfTransform[3];
  im->maxx=im->minx+im->width*im->resx;
  im->miny=im->maxy-im->height*im->resy;

  length=(im->width)*(im->height)*4L;
  im->buffer=malloc(length);
  if (im->buffer==NULL)
  { fprintf(stderr, "read_image: malloc im buffer\n"); exit(1); }

  for (l=3; (l<length); l+=4) im->buffer[l]=255;

  for (nband=0; (nband<3); nband++)
  {
    if (nbands==1)
      hBand=GDALGetRasterBand(hDataset, 1);
    else
      hBand=GDALGetRasterBand(hDataset, nband+1);

    GDALRasterIO(hBand, GF_Read, 0, 0, im->width, im->height, 
                 im->buffer+nband, im->width, im->height, GDT_Byte,
                 4, (im->width)*4);
  }

  //write_png(im, "im.png");

  GDALClose(hDataset);

  return im;
}

/******************************************************************************/

void write_image(image *im, srs *p_srs, char *filename)
{
  GDALDatasetH hDataset;
  GDALDriverH hDriver;
  GDALRasterBandH hBand;
  char pszFormat[256];
  char **papszMetadata;
  char **papszOptions=NULL;
  double padfTransform[6];
  char *wkt;
  char *ext;
  int nband;

  ext=&(filename[strlen(filename)]);
  while ((ext>=filename)&&(*ext!='.')) ext--;

  if       (strcasecmp(ext, ".png")==0)
  { write_png(im, filename); return; }
  else if ((strcasecmp(ext, ".jpg")==0)||(strcasecmp(ext, ".jpeg")==0))
  { write_jpg(im, filename); return; }
  else if ((strcasecmp(ext, ".tif")==0)||(strcasecmp(ext, ".tiff")==0))
  {
    write_tiff(im, filename); return;
    // strcpy(pszFormat, "GTiff");
  }
  else
  {
    fprintf(stderr, "write_image: %s extension not implemented\n", ext);
    return;
  }

  hDriver = GDALGetDriverByName(pszFormat);
  if (hDriver==NULL)
  { fprintf(stderr, "write_image: GDALGetDriverByName\n"); exit(1); }

  papszMetadata=GDALGetMetadata(hDriver, NULL);
  if (CSLFetchBoolean(papszMetadata, GDAL_DCAP_CREATE, FALSE))
    printf("Driver %s supports Create() method.\n", pszFormat);

  if (CSLFetchBoolean( papszMetadata, GDAL_DCAP_CREATECOPY, FALSE ) )
    printf("Driver %s supports CreateCopy() method.\n", pszFormat);

  hDataset=GDALCreate(hDriver, filename, im->width, im->height, 4, GDT_Byte,
                      papszOptions);

  padfTransform[0]=im->minx;
  padfTransform[1]=im->resx;
  padfTransform[2]=0;
  padfTransform[3]=im->maxy;
  padfTransform[4]=0;
  padfTransform[5]=-im->resy;

  if (GDALSetGeoTransform(hDataset, padfTransform)==CE_Failure)
  { fprintf(stderr, "write_image: GDALSetGeoTransform\n"); return; }

  if (OSRExportToWkt(p_srs, &wkt) != OGRERR_NONE)
  { fprintf(stderr, "write_image: OSRExportToWkt"); return; }

  GDALSetProjection(hDataset, wkt);

  free(wkt);

  for (nband=0; (nband<3); nband++)
  {
    hBand=GDALGetRasterBand(hDataset, nband+1);

    GDALRasterIO(hBand, GF_Write, 0, 0, im->width, im->height, 
                 im->buffer+nband, im->width, im->height, GDT_Byte,
                 4, (im->width)*4);
  }

  GDALClose( hDataset );
}

/******************************************************************************/

void overview(gqt *g, char *tileid, unsigned char *image,
              int filter, float blur)
{
  MagickWand *magick_wand; 
  MagickBooleanType status;  
  int num_read_tiles;
  char tilefile[1024];
  unsigned width, height;
  unsigned long l;
  
  width=g->tilesizex;
  height=g->tilesizey;
  
  for (l=0; (l<(long)width*(long)height*16L); )
  { image[l++]=0; image[l++]=0; image[l++]=0; image[l++]=0; }
  
  // Load the 4 children tile files into image
  
  num_read_tiles=0;
    
  sprintf(tilefile, "%s%s/4/%s", g->path, tileid, g->name);
  num_read_tiles+=readtile(g, tilefile, image, 2, 2, 0, 0);

  sprintf(tilefile, "%s%s/1/%s", g->path, tileid, g->name);
  num_read_tiles+=readtile(g, tilefile, image, 2, 2, 0, 1);

  sprintf(tilefile, "%s%s/3/%s", g->path, tileid, g->name);
  num_read_tiles+=readtile(g, tilefile, image, 2, 2, 1, 0);

  sprintf(tilefile, "%s%s/2/%s", g->path, tileid, g->name);
  num_read_tiles+=readtile(g, tilefile, image, 2, 2, 1, 1);

  magick_wand=NewMagickWand();
 
  status=MagickSetFormat(magick_wand, "PNG");
  if (status==MagickFalse) ThrowWandException(magick_wand);
    
  status=MagickSetSize(magick_wand, width*2, height*2);
  if (status==MagickFalse) ThrowWandException(magick_wand);

  status=MagickReadImage(magick_wand, "xc:black");

  status=MagickSetType(magick_wand, TrueColorType);
  if (status==MagickFalse) ThrowWandException(magick_wand);

  status=MagickSetImageDepth(magick_wand, 8);
  if (status==MagickFalse) ThrowWandException(magick_wand);

  status=MagickSetImagePixels(magick_wand, 0, 0, width*2, height*2,
                              "RGBA", CharPixel, image);
  if (status==MagickFalse) ThrowWandException(magick_wand);
  
  status=MagickResizeImage(magick_wand, width, height, filter, blur);
  if (status==MagickFalse) ThrowWandException(magick_wand);
  
  sprintf(tilefile, "%s%s/%s", g->path, tileid, g->name);
  if (verbose_level>1) printf("Generating %s\n", tilefile);  

  status=MagickWriteImage(magick_wand, tilefile);
  if (status==MagickFalse) ThrowWandException(magick_wand);

  DestroyMagickWand(magick_wand);
}

/******************************************************************************/

void overviews(gqt *g, int filter, float blur)
{
  struct string *p, *s;
  int len;
  char tileid[1024];
  unsigned char *image;
  long l;

  if (tiles==NULL) return;

  l=(long)g->tilesizex*(long)g->tilesizey*16L;
  image=malloc(l);
  if (image==NULL) { printf("overview malloc\n"); exit(1); }

  for (l=0; (l<(long)g->tilesizex*(long)g->tilesizey*16L); l++) image[l]=0;
  
  len=strlen(tiles->name)-2;

  while (len>=0)
  {
    p=tiles;
    s=NULL;

    while (p!=NULL)
    {
      strcpy(tileid, p->name); tileid[len]=0;
      if (exists(s, tileid)==0) s=addstring(s, tileid);

      p=p->next;
    }

    p=s;
    while (p!=NULL)
    {
      overview(g, p->name, image, filter, blur);
	  
      p=p->next;
    }

    delstrings(s);

    len-=2;
  }

  free(image);
}

/******************************************************************************/

void write_tile(image *tile, char *filename)
{
  image *im, *im_over;
  image *filetile;
  long length, i;
  FILE *fp;
  unsigned char alfa;

  fp=fopen(filename, "rb");
  if (fp==NULL)
  {
    im=tile;
  }
  else
  {
    fclose(fp);

    if (verbose_level>1) printf("fusion %s\n", filename);

    filetile=read_png(filename);

    im=filetile;
    im_over=tile;  // image and image_over could be swapped

    length=(long)tile->width*(long)tile->height;
 
    for (i=0; (i<length*4); )
    {
      alfa=im_over->buffer[i+3];
	  
      im->buffer[i]=((long)im_over->buffer[i]*(long)alfa+(long)im->buffer[i]*(255-(long)alfa))/255; i++;
      im->buffer[i]=((long)im_over->buffer[i]*(long)alfa+(long)im->buffer[i]*(255-(long)alfa))/255; i++;
      im->buffer[i]=((long)im_over->buffer[i]*(long)alfa+(long)im->buffer[i]*(255-(long)alfa))/255; i++;
      im->buffer[i]=((long)im_over->buffer[i]*(long)alfa+(long)im->buffer[i]*(255-(long)alfa))/255; i++;
    }
  }

  write_png(im, filename);
}

/******************************************************************************/

int gqt_import_file(gqt *g, char *filename, int filter, float blur,
                    int b_nondatacolor, int *nondatacolor)
{
  image *im, *tile;
  long minx_px, miny_px, maxx_px, maxy_px;
  long minx_tile_num, miny_tile_num, maxx_tile_num, maxy_tile_num;
  long minx_tile_px, miny_tile_px, maxx_tile_px, maxy_tile_px;
  double minx_tile, miny_tile, maxx_tile, maxy_tile;
  long numtilesx, numtilesy, i, j;
  char tileid[1024], tiledir[1024], tilefile[1024];
  double x, y;
  unsigned long l, length;
  double minx, miny, maxx, maxy;
  double num;

  printf("Importing image...\n");

  minx=0; miny=0; maxx=0; maxy=0;

  im=read_image(filename);
  if (im==NULL)
  { fprintf(stderr, "gqt_import_file: read_image\n"); return 1; }

  /* minx, miny, maxx, maxy will be the minimum bounding box that contains
     image, so in order to calculate it we exclude the non-data color pixels,
     if defined */
   
  if (b_nondatacolor==0)
  {
    minx=im->minx;
    miny=im->miny;
    maxx=im->maxx;
    maxy=im->maxy;
  }
  else
  {
    num=0;
    l=0;
    y=im->maxy;

    for (j=0; (j<im->height); j++)
    {
      x=im->minx;

      for (i=0; (i<im->width); i++)
      {
        if ((im->buffer[l+0]==nondatacolor[0])&&
            (im->buffer[l+1]==nondatacolor[1])&&
            (im->buffer[l+2]==nondatacolor[2])) im->buffer[l+3]=0;
        else
        {
          if (num==0) { minx=x; miny=y; maxx=x; maxy=y; }
          else
          {
            if (x<minx) minx=x;
            if (y<miny) miny=y;
            if (x>maxx) maxx=x;
            if (y>maxy) maxy=y;
          }

          num++;
        }

        x+=im->resx;
        l+=4;
      }

      y-=im->resy;
    }
  }

  if (verbose_level>1)
  {
    printf("\n");
    printf("Raster path=%s\n", g->path);
    printf("       levels=%i\n", g->levels);
    printf("       resx=%f resy=%f\n", g->resx, g->resy);
    printf("       tilesizex=%i tilesizey=%i\n", g->tilesizex, g->tilesizey);

    printf("minx=%f miny=%f maxx=%f maxy=%f\n",
           im->minx, im->miny, im->maxx, im->maxy);
  }

  minx_px=(long)(im->minx/g->resx);
  miny_px=(long)(im->miny/g->resy);
  maxx_px=(long)(im->maxx/g->resx);
  maxy_px=(long)(im->maxy/g->resy);

  if (verbose_level>1)
  {
    printf("minx_px=%li miny_px=%li maxx_px=%li maxy_px=%li\n",
           minx_px, miny_px, maxx_px, maxy_px);
  }

  minx_tile_num=(long)((double)minx_px/(double)g->tilesizex);
  if (minx_px<0) minx_tile_num--;

  miny_tile_num=(long)((double)miny_px/(double)g->tilesizey);
  if (miny_px<0) miny_tile_num--;

  maxx_tile_num=(long)((double)maxx_px/(double)g->tilesizex);
  if (maxx_px>0) maxx_tile_num++;

  maxy_tile_num=(long)((double)maxy_px/(double)g->tilesizey);
  if (maxy_px>0) maxy_tile_num++;

  if (verbose_level>1)
  {
    printf("minx_tile_num=%li miny_tile_num=%li maxx_tile_num=%li maxy_tile_num=%li\n",
          minx_tile_num, miny_tile_num, maxx_tile_num, maxy_tile_num);
  }
  
  minx_tile_px=minx_tile_num*g->tilesizex;
  miny_tile_px=miny_tile_num*g->tilesizey;
  maxx_tile_px=maxx_tile_num*g->tilesizex;
  maxy_tile_px=maxy_tile_num*g->tilesizey;
  
  if (verbose_level>1)
  {
    printf("minx_tile_px=%li miny_tile_px=%li maxx_tile_px=%li maxy_tile_px=%li\n",
          minx_tile_px, miny_tile_px, maxx_tile_px, maxy_tile_px);
  }
        
  minx_tile=(double)minx_tile_px*g->resx;
  miny_tile=(double)miny_tile_px*g->resy;
  maxx_tile=(double)maxx_tile_px*g->resx;
  maxy_tile=(double)maxy_tile_px*g->resy;

  if (verbose_level>1)
  {
    printf("minx_tile=%f miny_tile=%f maxx_tile=%f maxy_tile=%f\n",
          minx_tile, miny_tile, maxx_tile, maxy_tile);
  }

  numtilesx=(maxx_tile_px-minx_tile_px)/g->tilesizex;
  numtilesy=(maxy_tile_px-miny_tile_px)/g->tilesizey;
  
  if (verbose_level>1)
    printf("numtilesx=%lu numtilesy=%lu\n", numtilesx, numtilesy);

  tile=malloc(sizeof(image));
  if (tile==NULL) { printf("gqt_import_file: malloc tile\n"); return 1; }

  tile->width=g->tilesizex;
  tile->height=g->tilesizey;
  tile->resx=g->resx;
  tile->resy=g->resy;

  length=(unsigned long)(tile->width)*(unsigned long)(tile->height)*4L;
  tile->buffer=malloc(length);
  if (tile->buffer==NULL) { printf("gqt_import_file: malloc tile\n"); exit(1); }

  for (l=0; (l<length); l++) tile->buffer[l++]=255;

  tiles=NULL;
  
  y=maxy_tile-tile->height*tile->resy;

  for (j=0; (j<numtilesy); j++)
  {
    x=minx_tile;

    for (i=0; (i<numtilesx); i++)
    {
      tile->minx=x;
      tile->miny=y;
      tile->maxx=x+tile->width*tile->resx;
      tile->maxy=y+tile->height*tile->resy;

      extract(im, tile);

      strcpy(tileid, "");

      xy2filetile(g,
                  (tile->minx+tile->maxx)/2,
                  (tile->miny+tile->maxy)/2,
                  0, tileid);

      if (strlen(tileid)>0)
      {
        tiles=addstring(tiles, tileid);

        sprintf(tiledir, "%s%s", g->path, tileid);
        makedir(g->path, tileid);

        sprintf(tilefile, "%s/%s", tiledir, g->name);

        if (verbose_level>1) printf("%3li %3li %s\n", i, j, tilefile);

        write_tile(tile, tilefile);
      }

      x+=tile->width*tile->resx;
    }

    y-=tile->height*tile->resy;
  }

  free(tile);
  free(im);

  printf("Generating overviews...\n");

  overviews(g, filter, blur);
  
  delstrings(tiles);

  if (g->bounding_box==0)
  {
    g->minx=minx; g->miny=miny; g->maxx=maxx; g->maxy=maxy;
    g->bounding_box=1;
  }
  else
  {
    if (minx<g->minx) g->minx=minx;
    if (miny<g->miny) g->miny=miny;
    if (maxx>g->maxx) g->maxx=maxx;
    if (maxy>g->maxy) g->maxy=maxy;
  }

  printf("Done.\n");
  
  return 0;
}

/******************************************************************************/

int gqt_export(gqt *g, image *im, srs *p_srs, int filter)
{
  double minx, miny, maxx, maxy;
  int width, height;
  double minx_g, miny_g, maxx_g, maxy_g;
  double px[4], py[4];
  int ret;
  double pixel_width, pixel_height;
  unsigned level;
  double pixel_width_t, pixel_height_t;
  long minx_t_px, miny_t_px, maxx_t_px, maxy_t_px;
  double minx_t, miny_t, maxx_t, maxy_t;
  int numtilesx, numtilesy;
  double tile_width, tile_height;
  long tile_width_px, tile_height_px;
  long l;
  int i, j;
  unsigned char *tiles;
  double x, y;
  int num_read_tiles;
  char filetile[1024];

  if (g->p_srs==NULL) return 1;
  if (p_srs==NULL) return 1;
  
  minx=im->minx;
  miny=im->miny;
  maxx=im->maxx;
  maxy=im->maxy;
  
  if (verbose_level>1)
    fprintf(stderr, "\tminx=%f miny=%f maxx=%f maxy=%f\n", minx, miny, maxx, maxy);
  
  width=im->width;
  height=im->height;
  
  if (p_srs==g->p_srs)
  {
    minx_g=minx;
    miny_g=miny;
    maxx_g=maxx;
    maxy_g=maxy;
  }
  else
  {
    px[0]=minx; py[0]=maxy;
    px[1]=maxx; py[1]=maxy;
    px[2]=maxx; py[2]=miny;
    px[3]=minx; py[3]=miny;

    ret=proj_transform(p_srs, 4, px, py, g->p_srs);
	
    minx_g=px[0]; miny_g=py[0];
    maxx_g=px[0]; maxy_g=py[0];

    for (i=1; (i<4); i++)
    {
      if (px[i]<minx_g) minx_g=px[i];
      if (py[i]<miny_g) miny_g=py[i];
      if (px[i]>maxx_g) maxx_g=px[i];
      if (py[i]>maxy_g) maxy_g=py[i];
    }
  }

  pixel_width=(maxx_g-minx_g)/width;
  pixel_height=(maxy_g-miny_g)/height;

  if (verbose_level>1)
    printf("\tpixel_width=%f pixel_height=%f\n", pixel_width, pixel_height);
  
  pyramid_level(g, pixel_width, pixel_height, &level, &pixel_width_t, &pixel_height_t);

  if (verbose_level>1)
    printf("\tpixel_width_g=%f pixel_height_g=%f\n",
           pixel_width_t, pixel_height_t);

  minx_t_px=(long)(minx_g/pixel_width_t);
  miny_t_px=(long)(miny_g/pixel_height_t);
  maxx_t_px=(long)(maxx_g/pixel_width_t);
  maxy_t_px=(long)(maxy_g/pixel_height_t);

  if (verbose_level>1)
    printf("minx_t_px=%li miny_t_px=%li maxx_t_px=%li maxy_t_px=%li\n",
           minx_t_px, miny_t_px, maxx_t_px, maxy_t_px);
		 
  minx_t_px=(double)minx_t_px/(double)g->tilesizex; if (minx_g<0) minx_t_px--;
  miny_t_px=(double)miny_t_px/(double)g->tilesizey; if (miny_g<0) miny_t_px--;
  maxx_t_px=(double)maxx_t_px/(double)g->tilesizex; if (maxx_g>0) maxx_t_px++;
  maxy_t_px=(double)maxy_t_px/(double)g->tilesizey; if (maxy_g>0) maxy_t_px++;

  minx_t_px*=g->tilesizex;
  maxx_t_px*=g->tilesizex;
  miny_t_px*=g->tilesizey;
  maxy_t_px*=g->tilesizey;
		 
  if (verbose_level>1)
    printf("\tminx_t_px=%li miny_t_px=%li maxx_t_px=%li maxy_t_px=%li\n",
           minx_t_px, miny_t_px, maxx_t_px, maxy_t_px);

  minx_t=pixel_width_t*(double)minx_t_px;
  miny_t=pixel_height_t*(double)miny_t_px;
  maxx_t=pixel_width_t*(double)maxx_t_px;
  maxy_t=pixel_height_t*(double)maxy_t_px;

  if (verbose_level>1)
    printf("\tminx_t=%0.9f miny_t=%0.9f maxx_t=%0.9f maxy_t=%0.9f\n",
           minx_t, miny_t, maxx_t, maxy_t);

  numtilesx=(maxx_t-minx_t)/(double)g->tilesizex/pixel_width_t+0.5;
  numtilesy=(maxy_t-miny_t)/(double)g->tilesizey/pixel_height_t+0.5;

  if (verbose_level>1)
  printf("\tnumtilesx=%i numtilesy=%i\n", numtilesx, numtilesy);

  tile_width=pixel_width_t*g->tilesizex;
  tile_height=pixel_height_t*g->tilesizey;

  if (verbose_level>1)
  printf("\ttile_width=%f tile_height=%f\n", tile_width, tile_height);

  tile_width_px=numtilesx*g->tilesizex;
  tile_height_px=numtilesy*g->tilesizey;

  if (verbose_level>1)
  printf("\ttile_width_px=%lu tile_height_px=%lu\n", tile_width_px, tile_height_px);

  l=(long)tile_width_px*(long)tile_height_px*4L;

  if (verbose_level>1)
  printf("malloc %lu\n", l);

  tiles=malloc(l);
  if (tiles==NULL) { printf("malloc tiles\n"); exit(1); }

  for (l=0; (l<(long)tile_width_px*(long)tile_height_px*4L); )
  { tiles[l++]=0; tiles[l++]=0; tiles[l++]=0; tiles[l++]=0; }

  y=miny_t+tile_height/2;

  num_read_tiles=0;

  for (j=0; (j<numtilesy); j++)
  {
    x=minx_t+tile_width/2;

    for (i=0; (i<numtilesx); i++)
    {
      strcpy(filetile, g->path);
      if (xy2filetile(g, x, y, level, filetile)==1)
      {
        if (verbose_level>1)
        printf("\txy2filetile x=%f y=%f level=%i filetile=%s\n", x, y, level, filetile);

        strcat(filetile, "/");
        strcat(filetile, g->name);

        if (verbose_level>1)
        fprintf(stderr, "readtile %s i=%i j=%i\n", filetile, i, j);

        num_read_tiles+=readtile(g, filetile, tiles, numtilesx, numtilesy, i, j);
      }

      x+=tile_width;
    }

    y+=tile_height;
  }

  if (verbose_level>1)
  fprintf(stderr, "num_read_tiles=%i\n", num_read_tiles); 

  if (num_read_tiles>0)
  {
    if (verbose_level>1)
    {
      printf("resample tile_width_px=%lu tile_height_px=%lu\n", tile_width_px, tile_height_px);
      printf("         minx_t=%f miny_t=%f maxx_t=%f maxy_t=%f\n", minx_t, miny_t, maxx_t, maxy_t);
      printf("         width=%lu height=%lu\n", (long)width, (long)height);
      printf("         minx=%f miny=%f maxx=%f maxy=%f\n", minx, miny, maxx, maxy);
    }

    resample(tiles, tile_width_px, tile_height_px,
             g->p_srs, minx_t, miny_t, maxx_t, maxy_t,
             im->buffer, width, height,
             p_srs, minx, miny, maxx, maxy,
             filter);
  }

  free(tiles);
  
  return 0;
}

/******************************************************************************/

int gqt_export_file(gqt *g, char *filename, srs *p_srs,
                    double *bbox, int *size, int filter)
{
  image im;

  printf("Exporting image...\n");

  if (verbose_level>1)
  {
    printf("\n");
    printf("Raster path=%s\n", g->path);
    printf("       levels=%i\n", g->levels);
    printf("       resx=%f resy=%f\n", g->resx, g->resy);
    printf("       tilesizex=%i tilesizey=%i\n", g->tilesizex, g->tilesizey);
  }
  
  im.width=size[0];
  im.height=size[1];

  im.minx=bbox[0];
  im.miny=bbox[1];
  im.maxx=bbox[2];
  im.maxy=bbox[3];

  im.buffer=malloc((long)im.width*(long)im.height*4L);
  if (im.buffer==NULL)
  { fprintf(stderr, "gqt_export_file malloc\n"); return 1; }
  
  gqt_export(g, &im, p_srs, filter);
  
  write_image(&im, p_srs, filename);
  
  free(im.buffer);

  printf("Done.\n");

  return 0;
}

/******************************************************************************/

