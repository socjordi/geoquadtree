/*

proj.c - GeoQuadTree interface to OGR SRS API

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

#include <string.h>
#include <stdio.h>

#include "proj.h"

#define SRS_MAX_LENGTH 8192

/******************************************************************************/

int srs_import_file(srs *p_srs, char *srs_filename)
{
  char *str, *str0;
  FILE *fp;

  str=malloc(SRS_MAX_LENGTH);
  str0=str;
  fp=fopen(srs_filename, "rt");
  fgets(str, SRS_MAX_LENGTH, fp);
  fclose(fp);

  *p_srs=OSRNewSpatialReference(NULL);
  if (OSRImportFromWkt(*p_srs, &str) != OGRERR_NONE)
  { fprintf(stderr, "Importing dataset projection failed"); return 1; }

  free(str0);

  return 0;
}

/******************************************************************************/

int srs_import(srs *p_srs, char *srs_type, char *srs_definition)
{
  char epsg[256];

  *p_srs=OSRNewSpatialReference(NULL);

  if      (strcmp(srs_type, "WKT")==0)
  {
    if (OSRImportFromWkt(*p_srs, (char **)&srs_definition) != OGRERR_NONE)
    {
      fprintf(stderr, "Importing dataset projection failed");
      return 1;
    }
  }
  else if (strcmp(srs_type, "PROJ4")==0)
  {
    if (OSRImportFromProj4(*p_srs, srs_definition) != OGRERR_NONE)
    {
      fprintf(stderr, "Importing dataset projection failed");
      return 1;
    }
  }
  else if (strcmp(srs_type, "EPSG")==0)
  {
    strcpy(epsg, &(srs_definition[5]));

    if (OSRImportFromEPSG(*p_srs, atoi(epsg)) != OGRERR_NONE)
    {
      fprintf(stderr, "Importing dataset projection failed" );
      return 1;
    }
  }
  else
  {
    fprintf(stderr, "Unknown SRS type <%s>\n", srs_type);
    return 1;
  }

  return 0;
}

/******************************************************************************/

int proj_transform(srs *p_src, long count, double *x, double *y, srs *p_dst)
{
  OGRCoordinateTransformationH *p_ct;
  //long i;

  p_ct=OCTNewCoordinateTransformation(p_src, p_dst);

  if (p_ct==NULL)
  { fprintf(stderr, "OGRCreateCoordinateTransformation\n"); return 1; }

  /*
  if (p_src->in_degrees==1)
  {
    for (i=0; (i<count); i++)
    {
      x[i]*=DEG_TO_RAD;
      y[i]*=DEG_TO_RAD;
    }
  }
  */

  if (!(OCTTransform(p_ct, count, x, y, NULL)))
  { fprintf(stderr, "OCTTransform\n"); return 1; }

  /*
  if (p_dst->in_degrees==1)
  {
    for (i=0; (i<count); i++)
    {
      x[i]*=RAD_TO_DEG;
      y[i]*=RAD_TO_DEG;
    }
  }
  */

  OCTDestroyCoordinateTransformation(p_ct);

  return 0;
}

/******************************************************************************/
