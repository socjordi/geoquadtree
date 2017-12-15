/*

gqt.c - GeoQuadTree command line utility

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
#include <gdal.h>

#include "geoquadtree.h"
#include "proj.h"
#include "xml.h"
#include "resample.h"

int verbose_level;

/******************************************************************************/

void usage(char *program)
{
  printf("Usage: %s -c Creates an empty GeoQuadTree image\n", program);
  printf("          -g path_GeoQuadTree_XML_file\n");
  printf("          -n name (default 'gqt')\n");
  printf("          -s SRS_type (WKT, EPSG or PROJ4)\n");
  printf("          -S SRS_definition\n");
  printf("          -l number_of_levels\n");
  printf("          -r resolution_x,resolution_y\n");
  printf("          -t tile_size_x,tile_size_y (default 256, 256)\n");
  printf("          -v verbose_level (optional)\n");
  printf("\n");

  printf("Usage: %s -i Imports a JPEG/PNG/TIFF image into a GeoQuadTree image\n", program);
  printf("          -g path_GeoQuadTree_XML_file\n");
  printf("          -f path_file_to_import\n");
  printf("          -d red,green,blue (color used for non-data, optional)\n");
  printf("          -m blur_factor (>1 for blurry, <1 for sharp, default 1.0)\n");
  printf("          -k resampling_filter\n");
  printf("               0 - Undefined, 1 - Point, 2 - Box\n");
  printf("               3 - Triangle, 4 - Hermite, 5 - Hanning\n");
  printf("               6 - Hamming, 7 - Blackman, 8 - Gaussian\n");
  printf("               9 - Quadratic, 10 - Cubic, 11 - Catrom\n");
  printf("              12 - Mitchell, 13 - Lanczos (default)\n");
  printf("              14 - Bessel, 15 - Sinc\n");
  printf("          -v verbose_level (optional)\n");

  printf("\n");

  printf("Usage: %s -o Exports part of a GeoQuadTree image to a JPEG/PNG/TIFF image\n", program);
  printf("          -g path_GeoQuadTree_XML_file\n");
  printf("          -f path_file_to_export\n");
  printf("          -s SRS_type (WKT, EPSG or PROJ4)\n");
  printf("          -S SRS_definition\n");
  printf("          -b min_x,min_y,max_x,max_y\n");
  printf("          -t width_in_pixels,height_in_pixels\n");
  printf("          -k resampling_filter\n");
  printf("               0 - Nearest Neighbour, 1 - Bicubic\n");
  printf("          -v verbose_level (optional)\n");
  printf("\n");
}

/******************************************************************************/

int main(int argc, char *argv[])
{
  int c;
  int f_create, f_import, f_export;
  char geoquadtree_xml[1024];
  char filename[1024];
  char srs_type[16];
  char srs_definition[1024];
  double bbox[4];
  int number_of_levels;
  double res[2];
  int tilesize[2];
  char name[128];
  gqt g;
  int filter;
  float blur;
  int b_nondatacolor, nondatacolor[3];
  char *wkt;
  srs p_srs;

  GDALAllRegister();

  init_resample();

  f_create=0;
  f_import=0;
  f_export=0;

  verbose_level=0;

  number_of_levels=0;
  
  strcpy(geoquadtree_xml, "");
  strcpy(filename, "");
  strcpy(name, "gqt"); /* default file name for tile images is "gqt.png" */

  filter=13; /* default filter is  Lanczos */
  blur=1.0;  /* default blur is 1.0 */

  b_nondatacolor=0;

  while ((c=getopt(argc, argv, "?b:cd:f:g:hik:l:m:n:or:s:S:t:v:"))>0)
  {
    switch (c)
    {
      case '?': usage(argv[0]); return 0;

      case 'b': scanfloats(optarg, bbox, 4); break;

      case 'c': f_create=1; break;

      case 'd': b_nondatacolor=1; scanints(optarg, nondatacolor, 3); break;

      case 'f': strcpy(filename, optarg); break;

      case 'g': strcpy(geoquadtree_xml, optarg); break;

      case 'h': usage(argv[0]); return 0;

      case 'i': f_import=1; break;

      case 'k': filter=atoi(optarg); break;

      case 'l': number_of_levels=atoi(optarg); break;

      case 'm': blur=atof(optarg); break;

      case 'n': strcpy(name, optarg); break;

      case 'o': f_export=1; break;

      case 'r': scanfloats(optarg, res, 2); break;

      case 's': strcpy(srs_type, optarg); break;

      case 'S': strcpy(srs_definition, optarg); break;

      case 't': scanints(optarg, tilesize, 2); break;

      case 'v': verbose_level=atoi(optarg); break;

      default: fprintf(stderr, "%s: invalid option %c\n\n", argv[0], c);
               usage(argv[0]);
               return 1;
    }
  }

  if (f_create+f_import+f_export!=1)
  {
    if (f_create+f_import+f_export==0)
    {
      usage(argv[0]);
      return 0;
    }

    fprintf(stderr, "%s: incompatible options\n", argv[0]);
    usage(argv[0]);
    return 1;
  }

  if      (f_create==1)
  {
    printf("Creating empty GeoQuadTree image\n");

    printf("  GeoQuadTree XML file: %s\n", geoquadtree_xml);
    printf("  Name: %s\n", name);
    printf("  SRS type: %s\n", srs_type);
    printf("  SRS definition: %s\n", srs_definition);
    printf("  Number of levels=%i\n", number_of_levels);
    printf("  Resolution X=%f, Resolution Y=%f\n", res[0], res[1]);
    printf("  Tile Size X=%i, Tile Size Y=%i\n", tilesize[0], tilesize[1]);
    printf("\n");

    if (srs_import(&(g.p_srs), srs_type, srs_definition)==1)
    { fprintf(stderr, "srs_import: error importing SRS\n"); exit(1); }

    g.name=malloc(strlen(name)+5); strcpy(g.name, name); strcat(g.name, ".png");
    g.levels=number_of_levels;
    g.resx=res[0];
    g.resy=res[1];
    g.tilesizex=tilesize[0];
    g.tilesizey=tilesize[1];
    g.bounding_box=0;

    gqt_write_metadata(geoquadtree_xml, &g);

    gqt_read_metadata(geoquadtree_xml, &g);

    if (OSRExportToWkt(g.p_srs, &wkt) != OGRERR_NONE)
    {
      fprintf(stderr, "Exporting dataset projection failed");
      return 1;
    }

    printf("  SRS definition: %s\n", wkt);
    free(wkt);
  }
  else if (f_import==1)
  {
    printf("Importing a JPEG/PNG/TIFF image into a GeoQuadTree image\n");

    printf("  GeoQuadTree XML file: %s\n", geoquadtree_xml);
    printf("  File to import: %s\n", filename);
    printf("  Blur factor=%f\n", blur);
    printf("  Resampling filter=");
    switch(filter)
    {
      case  0: printf("Undefined"); break;
      case  1: printf("Point"); break;
      case  2: printf("Box"); break;
      case  3: printf("Triangle"); break;
      case  4: printf("Hermite"); break;
      case  5: printf("Hanning"); break;
      case  6: printf("Hamming"); break;
      case  7: printf("Blackman"); break;
      case  8: printf("Gaussian"); break;
      case  9: printf("Quadratic"); break;
      case 10: printf("Cubic"); break;
      case 11: printf("Catrom"); break;
      case 12: printf("Mitchell"); break;
      case 13: printf("Lanczos"); break;
      case 14: printf("Bessel"); break;
      case 15: printf("Sinc"); break;
      default: printf("Undefined"); filter=0;
    }
    printf("\n");

    gqt_read_metadata(geoquadtree_xml, &g);

    gqt_import_file(&g, filename, filter, blur, b_nondatacolor, nondatacolor);

    gqt_write_metadata(geoquadtree_xml, &g);
  }
  else if (f_export==1)
  {
    if (filter==13) filter=0;

    printf("Exporting part of a GeoQuadTree image to a JPEG/PNG/TIFF image\n");

    printf("  GeoQuadTree XML file: %s\n", geoquadtree_xml);
    printf("  File to export: %s\n", filename);
    printf("  SRS type: %s\n", srs_type);
    printf("  SRS definition: %s\n", srs_definition);
    printf("  Min X=%f, Min Y=%f, Max X=%f, Max Y=%f\n",
              bbox[0], bbox[1], bbox[2], bbox[3]);
    printf("  Width=%i Height=%i\n", tilesize[0], tilesize[1]);
    if (filter==0)
      printf("  Filter=0 (Nearest Neighbour)\n");
    else
      printf("  Filter=1 (Bicubic)\n");

    gqt_read_metadata(geoquadtree_xml, &g);

    if (srs_import(&p_srs, srs_type, srs_definition)==1)
    { fprintf(stderr, "srs_import: error importing SRS\n"); exit(1); }

    gqt_export_file(&g, filename, p_srs, bbox, tilesize, filter);
  }

  return 0;
}

/******************************************************************************/
