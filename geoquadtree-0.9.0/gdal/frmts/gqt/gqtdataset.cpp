/******************************************************************************
 *
 * Project:  GeoQuadTree Reader
 * Purpose:  All code for GeoQuadTree Reader
 * Author:   Jordi Gilabert Vall, geoquadtree@gmail.com
 *
 ******************************************************************************
 * Copyright (c) 2006, Jordi Gilabert Vall <geoquadtree@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ******************************************************************************
*/

#include "gdal_pam.h"
#include "ogr_srs_api.h"
#include "cpl_minixml.h"
#include "cpl_string.h"
#include "png.h"

CPL_C_START
void	GDALRegister_GQT(void);
CPL_C_END

#define BICUBIC_TABLE 100000

double *r_buffer;

/************************************************************************/
/* ==================================================================== */
/*				GQTDataset			        */
/* ==================================================================== */
/************************************************************************/

class GQTRasterBand;

class GQTDataset : public GDALPamDataset
{
    friend class GQTRasterBand;

    double minx, miny;
    int levels;
    double resx, resy;
    int tilesizex, tilesizey;
    char name[256];
    char path[1024];
    OGRSpatialReferenceH srs;
    char srs_wkt[4096];

  public:

    ~GQTDataset();
           
    static GDALDataset *Open(GDALOpenInfo *);

    CPLErr 	GetGeoTransform( double * padfTransform );
    const char *GetProjectionRef();
};

/************************************************************************/
/* ==================================================================== */
/*                            GQTRasterBand                             */
/* ==================================================================== */
/************************************************************************/

class GQTRasterBand : public GDALPamRasterBand
{
    friend class GQTDataset;

    GDALColorInterp eBandInterp;

    virtual CPLErr IRasterIO( GDALRWFlag, int, int, int, int,
                              void *, int, int, GDALDataType,
                              int, int *, int, int, int );

  public:

    GQTRasterBand( GQTDataset *, int );

    virtual CPLErr IReadBlock( int, int, void * );
    virtual int HasArbitraryOverviews() { return TRUE; }
    virtual GDALColorInterp GetColorInterpretation();

  private:

    int readtile(char *, unsigned char *,
                                unsigned, unsigned, unsigned, unsigned);

    CPLErr xy2filetile(double, double, unsigned, char *);

    CPLErr resample(
             unsigned char *, unsigned long, unsigned long,
             double, double, double, double,
             void *, unsigned long, unsigned long,
             double, double, double, double,
             GDALDataType);

};

/************************************************************************/
/*                           GQTRasterBand()                            */
/************************************************************************/

GQTRasterBand::GQTRasterBand( GQTDataset *poDS, int nBand )
{
    this->poDS = poDS;
    this->nBand = nBand;

    eDataType = GDT_Byte;

    nBlockXSize = poDS->tilesizex;
    nBlockYSize = poDS->tilesizey;

    if     ( nBand == 1 ) eBandInterp = GCI_RedBand;
    else if( nBand == 2 ) eBandInterp = GCI_GreenBand;
    else if( nBand == 3 ) eBandInterp = GCI_BlueBand;
    else if( nBand == 4 ) eBandInterp = GCI_AlphaBand;
    else                  eBandInterp = GCI_Undefined;
}

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr GQTRasterBand::IReadBlock( int nBlockXOff, int nBlockYOff,
                                  void * pImage )
{
    int i, mx, my, xmin, ymin, xmax, ymax;
    char tilefile[1024];
    FILE *fp;
    GQTDataset	*poGDS = (GQTDataset *) poDS;
    int nBlockXSize, nBlockYSize;
    png_bytep *row_pointers;
    unsigned char *p;
    png_structp png_ptr;
    png_infop info_ptr;
    long col, row;
    GByte *p_dst;

    GetBlockSize(&nBlockXSize, &nBlockYSize);

    xmin=0;
    ymin=0;
    xmax=(1<<poGDS->levels);
    ymax=xmax;

    strcpy(tilefile, poGDS->path);

    for (i=0; (i<poGDS->levels); i++)
    {
      mx=(xmin+xmax)/2;
      my=(ymin+ymax)/2;

      if (nBlockXOff<mx)
      {
        if (nBlockYOff<my) { strcat(tilefile, "/1"); xmax=mx; ymax=my; }
        else               { strcat(tilefile, "/4"); xmax=mx; ymin=my; }
      }
      else
      {
        if (nBlockYOff<my) { strcat(tilefile, "/2"); xmin=mx; ymax=my; }
        else               { strcat(tilefile, "/3"); xmin=mx; ymin=my; }
      }
    }

    strcat(tilefile, "/");
    strcat(tilefile, poGDS->name);

    fp=fopen(tilefile, "rb");
    if (fp==NULL)
    {
      for (i=0; (i<nBlockXSize*nBlockYSize); i++)
        ((GByte *)pImage)[i] = 0;
    }
    else
    {
      row_pointers=(png_bytep *)malloc(nBlockYSize*sizeof(png_bytep *));
      if (row_pointers==NULL) return CE_Failure;

      p=(unsigned char *)malloc(nBlockXSize*nBlockYSize*4);
      for (row=0; (row<nBlockYSize); row++)
      {
        row_pointers[row]=(png_bytep)p;
        p+=(nBlockXSize*4);
      }

      png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
      if (!png_ptr) return CE_Failure;

      info_ptr=png_create_info_struct(png_ptr);
      if (info_ptr==NULL)
      {
        png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
        return CE_Failure;
      }

      png_init_io(png_ptr, fp);

      png_read_info(png_ptr, info_ptr);
      png_read_image(png_ptr, row_pointers);

      fclose(fp);

      png_destroy_info_struct(png_ptr, &info_ptr);
      png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

      p_dst=(GByte *)pImage;
      for (row=0; (row<nBlockYSize); row++)
      {
        p=row_pointers[row];

        for (col=0; (col<nBlockXSize); col++)
          *p_dst++ = p[col*4+nBand-1];

        p+=nBlockXSize;
      }

      free(row_pointers[0]);

      free(row_pointers);
   }
    
    return CE_None;
}

/************************************************************************/
/*                             xy2filetile()                            */
/************************************************************************/

CPLErr GQTRasterBand::xy2filetile(double x, double y, unsigned level, char *filetile)
{
  GQTDataset *poGQTDS = (GQTDataset *) poDS;
  double xmin, ymin, xmax, ymax;
  double xm, ym;
  unsigned l;

  xmax=poGQTDS->resx*poGQTDS->tilesizex*(1<<(poGQTDS->levels-1));
  ymax=poGQTDS->resy*poGQTDS->tilesizey*(1<<(poGQTDS->levels-1));
  xmin=-xmax;
  ymin=-ymax;

  if ((x<xmin)||(y<ymin)||(x>xmax)||(y>ymax)) return CE_Failure;

  for (l=poGQTDS->levels; (l>level); l--)
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

  return CE_None;
}

/************************************************************************/
/*                             readtile()                              */
/************************************************************************/

int GQTRasterBand::readtile(char *filetile, unsigned char *tiles,
                            unsigned numtilesx, unsigned numtilesy,
                            unsigned coltile, unsigned rowtile)
{
  GQTDataset *poGQTDS = (GQTDataset *) poDS;
  FILE *fp;
  int row;
  png_bytep *row_pointers;
  png_structp png_ptr;
  png_infop info_ptr;

  fp=fopen(filetile, "rb");
  if (fp==NULL) return 0;
  else
  {
    tiles+=(numtilesx*poGQTDS->tilesizex*(numtilesy-rowtile-1) + coltile)*poGQTDS->tilesizey*4;

    row_pointers=(png_bytep *)malloc(poGQTDS->tilesizey*sizeof(png_bytep *));
    if (row_pointers==NULL) return CE_Failure;

    for (row=0; (row<poGQTDS->tilesizey); row++)
    {
      row_pointers[row]=tiles;
      tiles+=(numtilesx*poGQTDS->tilesizex*4);
    }

    png_ptr=png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) return CE_Failure;

    info_ptr=png_create_info_struct(png_ptr);
    if (info_ptr==NULL)
    {
      png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
      return CE_Failure;
    }

    png_init_io(png_ptr, fp);

    png_read_info(png_ptr, info_ptr);
    png_read_image(png_ptr, row_pointers);

    fclose(fp);

    png_destroy_info_struct(png_ptr, &info_ptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    free(row_pointers);
  }

  return 1;
}


/************************************************************************/
/*                             r_resample_calc()                        */
/************************************************************************/

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


/************************************************************************/
/*                             init_resample()                          */
/************************************************************************/

void init_resample(void)
{
  int i;
  double x;

  r_buffer=(double *)malloc(BICUBIC_TABLE*sizeof(double));

  for (i=0; (i<BICUBIC_TABLE); i++)
  {
    x=4-(double)i*8/BICUBIC_TABLE;
    r_buffer[i]=r_resample_calc(x);
  }
}



/************************************************************************/
/*                             r_resample()                             */
/************************************************************************/

double r_resample(double x)
{
  int i;

  if (x<-4) return r_resample_calc(x);
  if (x>4)  return r_resample_calc(x);

  i=(BICUBIC_TABLE/8)*(4-(int)x);

  return r_buffer[i];
}


/************************************************************************/
/*                             resample()                               */
/************************************************************************/

CPLErr GQTRasterBand::resample(unsigned char *image_src,
             unsigned long width_src, unsigned long height_src,
             double xmin_src, double ymin_src, double xmax_src, double ymax_src,
             void *image_dst,
             unsigned long width_dst, unsigned long height_dst,
             double xmin_dst, double ymin_dst, double xmax_dst, double ymax_dst,
             GDALDataType eBufType)
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

  x=(double *)malloc(width_dst*sizeof(double));
  if (x==NULL) { printf("malloc x\n"); exit(1); }

  y=(double *)malloc(width_dst*sizeof(double));
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

//    /*! Eight bit unsigned integer */           GDT_Byte = 1,
//    /*! Sixteen bit unsigned integer */         GDT_UInt16 = 2,
//    /*! Sixteen bit signed integer */           GDT_Int16 = 3,
//    /*! Thirty two bit unsigned integer */      GDT_UInt32 = 4,
//    /*! Thirty two bit signed integer */        GDT_Int32 = 5,
//    /*! Thirty two bit floating point */        GDT_Float32 = 6,
//    /*! Sixty four bit floating point */        GDT_Float64 = 7,
//    /*! Complex Int16 */                        GDT_CInt16 = 8,
//    /*! Complex Int32 */                        GDT_CInt32 = 9,
//    /*! Complex Float32 */                      GDT_CFloat32 = 10,
//    /*! Complex Float64 */                      GDT_CFloat64 = 11,

      switch(eBufType)
      {
        case GDT_Byte:

          if (f_r<0) f_r=0;
          if (f_g<0) f_g=0;
          if (f_b<0) f_b=0;
          if (f_a<0) f_a=0;

          if (f_r>255) f_r=255;
          if (f_g>255) f_g=255;
          if (f_b>255) f_b=255;
          if (f_a>255) f_a=255;

          ((unsigned char *)image_dst)[p_dst++]=(unsigned char)f_r;
          ((unsigned char *)image_dst)[p_dst++]=(unsigned char)f_g;
          ((unsigned char *)image_dst)[p_dst++]=(unsigned char)f_b;
          ((unsigned char *)image_dst)[p_dst++]=(unsigned char)f_a;

          break;

        case GDT_UInt16:

          ((unsigned int *)image_dst)[p_dst++]=(unsigned int)f_r;
          ((unsigned int *)image_dst)[p_dst++]=(unsigned int)f_g;
          ((unsigned int *)image_dst)[p_dst++]=(unsigned int)f_b;
          ((unsigned int *)image_dst)[p_dst++]=(unsigned int)f_a;

          break;

        case GDT_Int16:

          ((int *)image_dst)[p_dst++]=(int)f_r;
          ((int *)image_dst)[p_dst++]=(int)f_g;
          ((int *)image_dst)[p_dst++]=(int)f_b;
          ((int *)image_dst)[p_dst++]=(int)f_a;

          break;

        case GDT_UInt32:

          ((unsigned long *)image_dst)[p_dst++]=(unsigned long)f_r;
          ((unsigned long *)image_dst)[p_dst++]=(unsigned long)f_g;
          ((unsigned long *)image_dst)[p_dst++]=(unsigned long)f_b;
          ((unsigned long *)image_dst)[p_dst++]=(unsigned long)f_a;

          break;

        case GDT_Int32:

          ((long *)image_dst)[p_dst++]=(long)f_r;
          ((long *)image_dst)[p_dst++]=(long)f_g;
          ((long *)image_dst)[p_dst++]=(long)f_b;
          ((long *)image_dst)[p_dst++]=(long)f_a;

          break;

        case GDT_Float32:

          ((float *)image_dst)[p_dst++]=(float)f_r;
          ((float *)image_dst)[p_dst++]=(float)f_g;
          ((float *)image_dst)[p_dst++]=(float)f_b;
          ((float *)image_dst)[p_dst++]=(float)f_a;

          break;

        case GDT_Float64:

          ((double *)image_dst)[p_dst++]=(double)f_r;
          ((double *)image_dst)[p_dst++]=(double)f_g;
          ((double *)image_dst)[p_dst++]=(double)f_b;
          ((double *)image_dst)[p_dst++]=(double)f_a;

          break;

        case GDT_CInt16: break;

        case GDT_CInt32: break;

        case GDT_CFloat32: break;

        case GDT_CFloat64: break;

        default: break;
      }
    }

    y_dst-=pixel_height_dst;
  }
  
  free(x);
  free(y);

  return CE_None;
}

/************************************************************************/
/*                             IRasterIO()                              */
/************************************************************************/

CPLErr GQTRasterBand::IRasterIO( GDALRWFlag eRWFlag,
                                 int nXOff, int nYOff, int nXSize, int nYSize,
                                 void *pData, int nBufXSize, int nBufYSize,
                                 GDALDataType eBufType,
                                 int nBandCount, int *panBandMap,
                                 int nPixelSpace, int nLineSpace,
                                 int nBandSpace )
{
    GQTDataset *poGQTDS = (GQTDataset *) poDS;
    double pixel_width, pixel_height;
    double t_pixel_width, t_pixel_height;
    int level;
    long t_min_x_px, t_min_y_px, t_max_x_px, t_max_y_px;
    double min_x, min_y, max_x, max_y;
    double t_min_x, t_min_y, t_max_x, t_max_y;
    long i, j, l;
    unsigned char *tiles;
    double x, y;
    char filetile[1024];
    long numtilesx, numtilesy;
    double tile_width, tile_height;
    long tile_width_px, tile_height_px;
    int num_read_tiles;

    /* pixel_width, pixel_height: size of the desired pixel in world units */

    pixel_width=(double)nXSize/(double)nBufXSize*poGQTDS->resx;
    pixel_height=(double)nYSize/(double)nBufYSize*poGQTDS->resy;

    /* t_pixel_width, t_pixel_height: size of the pixel in world units
                                      on the needed overview level */

    t_pixel_width=poGQTDS->resx;
    t_pixel_height=poGQTDS->resy;

    for (level=0; (level<256); level++)
    {
      if ((t_pixel_width*2>pixel_width)&&(t_pixel_height*2>pixel_height)) break;

      t_pixel_width*=2;
      t_pixel_height*=2;
    }

    /* min_x, min_y, max_x, max_y: the desired region in world units */

    min_x=poGQTDS->minx+(double)nXOff*poGQTDS->resx;
    max_y=poGQTDS->miny+poGQTDS->resy*(double)((1<<poGQTDS->levels)*poGQTDS->tilesizey-nYOff);
    max_x=min_x+nXSize*poGQTDS->resx;
    min_y=max_y-nYSize*poGQTDS->resy;

    /* t_min_x_px, t_min_y_px, t_max_x_px, t_max_y_px: 
       extent (exceeded on tiles) in pixels */

    t_min_x_px=((long)((min_x-poGQTDS->minx)/t_pixel_width));
    t_min_y_px=((long)((min_y-poGQTDS->miny)/t_pixel_height));
    t_max_x_px=((long)((max_x-poGQTDS->minx)/t_pixel_width));
    t_max_y_px=((long)((max_y-poGQTDS->miny)/t_pixel_height));

    t_min_x_px=(long)((double)t_min_x_px/(double)(poGQTDS->tilesizex));
    if (min_x<poGQTDS->minx) t_min_x_px--;

    t_min_y_px=(long)((double)t_min_y_px/(double)(poGQTDS->tilesizey));
    if (min_y<poGQTDS->miny) t_min_y_px--;

    t_max_x_px=(long)((double)t_max_x_px/(double)(poGQTDS->tilesizex));
    if (max_x>poGQTDS->minx) t_max_x_px++;

    t_max_y_px=(long)((double)t_max_y_px/(double)(poGQTDS->tilesizey));
    if (max_y>poGQTDS->miny) t_max_y_px++;

    t_min_x_px*=poGQTDS->tilesizex;
    t_max_x_px*=poGQTDS->tilesizex;
    t_min_y_px*=poGQTDS->tilesizey;
    t_max_y_px*=poGQTDS->tilesizey;

    /* t_min_x, t_min_y, t_max_x, t_maxy: 
       the region (exceeded on tiles) in pixels */
		 
    t_min_x=poGQTDS->minx+t_pixel_width*(double)t_min_x_px;
    t_min_y=poGQTDS->miny+t_pixel_height*(double)t_min_y_px;
    t_max_x=poGQTDS->minx+t_pixel_width*(double)t_max_x_px;
    t_max_y=poGQTDS->miny+t_pixel_height*(double)t_max_y_px;

    /* numtilesx, numtilesy: number of tiles to load */

    numtilesx=(long)((t_max_x-t_min_x)/(double)poGQTDS->tilesizex/t_pixel_width+0.5);
    numtilesy=(long)((t_max_y-t_min_y)/(double)poGQTDS->tilesizey/t_pixel_height+0.5);

    /* tile_width, tile_height: extent of the tiles (in world units) */

    tile_width=t_pixel_width*(double)poGQTDS->tilesizex;
    tile_height=t_pixel_height*(double)poGQTDS->tilesizey;

    /* tile_width_px, tile_height_px: extent of the tiles (in pixels) */

    tile_width_px=numtilesx*poGQTDS->tilesizex;
    tile_height_px=numtilesy*poGQTDS->tilesizey;

    /* memory allocation for tiles to load */

    l=(long)tile_width_px*(long)tile_height_px*4L;

    tiles=(unsigned char *)malloc(l);
    if (tiles==NULL)
    { 
           CPLError( CE_Failure, CPLE_AppDefined, "malloc" );
           return( CE_Failure );
    }

    /* initialize tile space to transparent */

    for (l=0; (l<(long)tile_width_px*(long)tile_height_px*4L); )
    { tiles[l++]=0; tiles[l++]=0; tiles[l++]=0; tiles[l++]=0; }

    /* tiles loading */

    y=t_min_y+tile_height/2;

    num_read_tiles=0;

    for (j=0; (j<numtilesy); j++)
    {
      x=t_min_x+tile_width/2;

      for (i=0; (i<numtilesx); i++)
      {
        strcpy(filetile, poGQTDS->path);

        if (xy2filetile(x, y, level, filetile)==CE_None)
        {
          strcat(filetile, "/");
          strcat(filetile, poGQTDS->name);

          num_read_tiles+=readtile(filetile, tiles, numtilesx, numtilesy, i, j);
        }

        x+=tile_width;
      }

      y+=tile_height;
    }

    if (num_read_tiles>0)
    {
      resample(tiles, tile_width_px, tile_height_px,
               t_min_x, t_min_y, t_max_x, t_max_y,
               (unsigned char *)pData, nBufXSize, nBufYSize,
               min_x, min_y, max_x, max_y,
               eBufType);
    }

    free(tiles);

/*
for (i=0; (i<nBufXSize); i++)
for (j=0; (j<nBufYSize); j++)
((unsigned char *)pData)[j*nBufXSize+i]=i%255;
*/
  
    return CE_None;
}

/************************************************************************/
/*                       GetColorInterpretation()                       */
/************************************************************************/

GDALColorInterp GQTRasterBand::GetColorInterpretation()
{
    return eBandInterp;
}

/************************************************************************/
/* ==================================================================== */
/*				GQTDataset				*/
/* ==================================================================== */
/************************************************************************/

/************************************************************************/
/*                            ~GQTDataset()                     	*/
/************************************************************************/

GQTDataset::~GQTDataset()
{
}

/************************************************************************/
/*                          GetGeoTransform()                           */
/************************************************************************/

CPLErr GQTDataset::GetGeoTransform( double * padfTransform )
{
    /* the GeoTransform refer to the top left corner of the top left pixel */

    padfTransform[0] = -tilesizex*resx*(1<<(levels-1));
    padfTransform[3] = -padfTransform[0];

    padfTransform[1] = resx;
    padfTransform[2] = 0.0;
        
    padfTransform[4] = 0.0;
    padfTransform[5] = -resy;

    return CE_None;
}

/************************************************************************/
/*                          GetProjectionRef()                          */
/************************************************************************/

const char *GQTDataset::GetProjectionRef()
{
    return( srs_wkt );
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *GQTDataset::Open( GDALOpenInfo *poOpenInfo )
{
/* -------------------------------------------------------------------- */
/*      Create a corresponding GDALDataset.                             */
/* -------------------------------------------------------------------- */
    char path[1024], *p_str;
    int i;
    GQTDataset *poDS;
    CPLXMLNode *psTree = NULL, *psGQT = NULL;
    FILE *fp;

    poDS = new GQTDataset();

// -------------------------------------------------------------------- 
//      Read the header.                                                
// -------------------------------------------------------------------- 

    psTree = CPLParseXMLFile( poOpenInfo->pszFilename );
    if( psTree == NULL ) return NULL;

    psGQT = CPLGetXMLNode( psTree, "=GeoQuadTree" );
    if( psGQT == NULL ) return NULL;

    if ( CPLGetXMLValue( psGQT, "name", NULL) == NULL )
    {
      CPLError( CE_Failure, CPLE_FileIO,
                "Failed to read name from GeoQuadTree file." );
      return NULL;
    }
    else
      strcpy(poDS->name, CPLGetXMLValue(psGQT, "name", NULL));

    strcat(poDS->name, ".png");

    if ( CPLGetXMLValue( psGQT, "minx", NULL) == NULL )
    {
      CPLError( CE_Failure, CPLE_FileIO,
                "Failed to read minx from GeoQuadTree file." );
      return NULL;
    }
    else
      poDS->minx=atof(CPLGetXMLValue(psGQT, "minx", NULL));

    if ( CPLGetXMLValue( psGQT, "miny", NULL) == NULL )
    {
      CPLError( CE_Failure, CPLE_FileIO,
                "Failed to read miny from GeoQuadTree file." );
      return NULL;
    }
    else
      poDS->miny=atof(CPLGetXMLValue(psGQT, "miny", NULL));

    if ( CPLGetXMLValue( psGQT, "levels", NULL) == NULL )
    {
      CPLError( CE_Failure, CPLE_FileIO,
                "Failed to read levels from GeoQuadTree file." );
      return NULL;
    }
    else
      poDS->levels=atoi(CPLGetXMLValue(psGQT, "levels", NULL));

    if ( CPLGetXMLValue( psGQT, "resx", NULL) == NULL )
    {
      CPLError( CE_Failure, CPLE_FileIO,
                "Failed to read resx from GeoQuadTree file." );
      return NULL;
    }
    else
      poDS->resx=atof(CPLGetXMLValue(psGQT, "resx", NULL));

    if ( CPLGetXMLValue( psGQT, "resy", NULL) == NULL )
    {
      CPLError( CE_Failure, CPLE_FileIO,
                "Failed to read resy from GeoQuadTree file." );
      return NULL;
    }
    else
      poDS->resy=atof(CPLGetXMLValue(psGQT, "resy", NULL));

    if ( CPLGetXMLValue( psGQT, "tilesizex", NULL) == NULL )
    {
      CPLError( CE_Failure, CPLE_FileIO,
                "Failed to read tilesizex from GeoQuadTree file." );
      return NULL;
    }
    else
      poDS->tilesizex=atoi(CPLGetXMLValue(psGQT, "tilesizex", NULL));

    if ( CPLGetXMLValue( psGQT, "tilesizey", NULL) == NULL )
    {
      CPLError( CE_Failure, CPLE_FileIO,
                "Failed to read tilesizey from GeoQuadTree file." );
      return NULL;
    }
    else
      poDS->tilesizey=atoi(CPLGetXMLValue(psGQT, "tilesizey", NULL));

    CPLDestroyXMLNode( psTree );

    strcpy(path, poOpenInfo->pszFilename);
    for (i=strlen(path)-1; ((i>=0)&&(path[i]!='.')); i--);
    path[i]=0;
    strcat(path, ".prj");

    fp=fopen(path, "rt");
    fgets(poDS->srs_wkt, 4096, fp);
    fclose(fp);

    poDS->srs=OSRNewSpatialReference(NULL);
    p_str=(char *)poDS->srs_wkt;

    if (OSRImportFromWkt(poDS->srs, &p_str) != OGRERR_NONE)
    {
      CPLError( CE_Failure, CPLE_AppDefined,
                "Importing dataset projection failed" );
      return FALSE;
    }

    poDS->nRasterXSize = poDS->tilesizex*(1<<poDS->levels);
    poDS->nRasterYSize = poDS->tilesizey*(1<<poDS->levels);
    poDS->nBands = 4;

    //poDS->nBitDepth = 8;
    //poDS->bInterlaced = ?;
    //poDS->nColorType = ?;

    strcpy(path, poOpenInfo->pszFilename);
    for (i=strlen(path)-1; ((i>=0)&&(path[i]!='/')); i--);
    path[i]=0;
    strcpy(poDS->path, path);

/* -------------------------------------------------------------------- */
/*      Create band information objects.                                */
/* -------------------------------------------------------------------- */

    for( int iBand = 0; iBand < 4; iBand++ )
        poDS->SetBand( iBand+1, new GQTRasterBand( poDS, iBand+1 ) );

    return poDS;
}

/************************************************************************/
/*                         GDALRegister_GQT()                   	*/
/************************************************************************/

void GDALRegister_GQT()
{
    GDALDriver	*poDriver;

    if( GDALGetDriverByName( "GQT" ) == NULL )
    {
        poDriver = new GDALDriver();
        
        poDriver->SetDescription( "GQT" );
        poDriver->SetMetadataItem( GDAL_DMD_LONGNAME, 
                                   "GeoQuadTree" );
        poDriver->SetMetadataItem( GDAL_DMD_HELPTOPIC, 
                                   "frmt_geoquadtree.html" );
        poDriver->SetMetadataItem( GDAL_DMD_EXTENSION, "gqt" );
        poDriver->SetMetadataItem( GDAL_DMD_MIMETYPE, "image/png" );

        poDriver->pfnOpen = GQTDataset::Open;
	//poDriver->pfnCreateCopy = GQTCreateCopy;

        GetGDALDriverManager()->RegisterDriver( poDriver );

        init_resample();
    }
}
