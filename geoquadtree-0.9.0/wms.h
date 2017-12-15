/*

wms.h - GeoQuadTree OGC WMS Server

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

#if !defined(__WMS__)

#define __WMS__

#include "geoquadtree.h"
#include "proj.h"

typedef struct
{
  gqt *geoquadtree;
  double minresx, minresy, maxresx, maxresy;
  struct raster *next;
} raster;

typedef struct
{
  char *name;
  srs p_srs;  
  struct layer_srs *next;
} layer_srs;

typedef struct
{
  char *name;
  char *title;
  layer_srs *layer_srs_list;
  raster *raster_list;
  struct layer *next;
} layer;

typedef struct
{
  char *Title;
  char *Abstract;
  char *Fees;
  char *AccessConstraints;
  char *ContactPerson;
  char *ContactOrganization;
  char *ContactPosition;
  char *AddressType;
  char *Address;
  char *City;
  char *StateOrProvince;
  char *PostCode;
  char *Country;
  char *ContactVoiceTelephone;
  char *ContactFacsimileTelephone;
  char *ContactElectronicMailAddress;
  int MaxWidth;
  int MaxHeight;
  char *Logo;
  layer *layer_list;
} service;

#endif

/******************************************************************************/
