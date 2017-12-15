/*

geoquadtree.h - GeoQuadTree core

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

#if !defined(__GEOQUADTREE__)

#define __GEOQUADTREE__

#include "proj.h"

typedef struct
{
  srs p_srs;
  char *path;
  char *name;
  unsigned levels;
  double resx, resy;
  unsigned tilesizex, tilesizey;
  double minx, miny, maxx, maxy;
  int bounding_box;
  struct raster *next;
} gqt;

typedef struct
{
  unsigned long width;
  unsigned long height;
  unsigned char *buffer;
  double minx, miny, maxx, maxy;
  double resx, resy;
} image;

void scanstrs(char *, char **, int);
void scanfloats(char *, double *, int);
void scanints(char *, int *, int);

int gqt_import_file(gqt *, char *, int, float, int, int *);

int gqt_export(gqt *, image *, srs *, int);

int gqt_export_file(gqt *, char *, srs *, double *, int *, int);

#endif

/******************************************************************************/
