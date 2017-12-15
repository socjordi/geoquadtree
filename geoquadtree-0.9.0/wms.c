/*

wms.c - GeoQuadTree OGC WMS Server

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
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcgi_stdio.h>

#include "geoquadtree.h"
#include "xml.h"
#include "proj.h"
#include "png.h"
#include "jpg.h"
#include "fcgi.h"
#include "resample.h"
#include "logo.h"

int verbose_level=0;

char configuration_file[1024];
char configuration_file_time[128];

int current_version;

srs p_srs_84;

/******************************************************************************/

char *strlwr(char *str)
{
  /* Converts a string to lower case */

  int i;

  for (i=0; (i<strlen(str)); i++) str[i]=tolower(str[i]);

  return str;
}

/******************************************************************************/

void exception(char *code, char *description)
{
  /* Writes to standard output a WMS service exception report  */

  char str[1024];

  if (current_version==10101)
  {
    strcpy(str, "Content-type: application/vnd.ogc.se_xml\r\n\r\n");
    strcat(str, "<?xml version='1.0' encoding=\"UTF-8\"?>");
    strcat(str, "<!DOCTYPE ServiceExceptionReport SYSTEM \"http://schemas.opengis.net/wms/1.1.1/exception_1_1_1.dtd\">");

    strcat(str, "<ServiceExceptionReport version=\"1.1.1\">");
  }
  else
  {
    strcpy(str, "Content-type: text/xml\r\n\r\n");
    strcat(str, "<?xml version='1.0' encoding=\"UTF-8\"?>");
    strcat(str, "<ServiceExceptionReport version=\"1.3.0\" xsi:schemaLocation=\"http://www.opengis.net/ogc http://schemas.opengis.net/wms/1.3.0/exception_1_3_0.xsd\">");
  }

  if (strlen(code)==0)
  {
    strcat(str, "<ServiceException>");
  }
  else
  {
    strcat(str, "<ServiceException code=\"");
    strcat(str, code);
    strcat(str, "\">");
  }

  strcat(str, description);
  strcat(str, "</ServiceException>");

  strcat(str, "</ServiceExceptionReport>");

  puts(str);
}

/******************************************************************************/

int get_current_time(char *str)
{
  struct tm tp;
  time_t clock;

  time(&clock);
  tp=*gmtime(&clock);

  sprintf(str, "%04d-%02d-%02dT%02d%02d%02dZ",
                tp.tm_year+1900, tp.tm_mon+1, tp.tm_mday,
                tp.tm_hour, tp.tm_min, tp.tm_sec);

  return 0;
}

/******************************************************************************/

int get_mtime(char *path, char *mtime)
{
  struct stat buf;
  struct tm tp;

  strcpy(mtime, "");
  
  if (stat(path, &buf)!=0) return 1;

  tp=*gmtime(&(buf.st_mtime));

  sprintf(mtime, "%04d-%02d-%02dT%02d%02d%02dZ",
                 tp.tm_year+1900, tp.tm_mon+1, tp.tm_mday,
                 tp.tm_hour, tp.tm_min, tp.tm_sec);

  return 0;
}

/******************************************************************************/

int version2numeric(char *version_str)
{
  int v, i;
  char *pch;

  /*
     The version number contains three non-negative integers, separated by
     decimal points, in the form "x.y.z".
     The numbers "y" and "z" shall not exceed 99
  */

  v=0;
  i=0;

  pch=strtok(version_str, ".");
  while (pch!=NULL)
  {
    v=v*100+atoi(pch);
    pch=strtok(NULL, ".");
    i++;
  }

  if (i!=3)
  { exception("", "Version format not understood");  return 0; }

  return v;
}

/******************************************************************************/

int geographic_bounding_box(layer *l, double *bb)
{
  raster *r;
  int bounding_box;
  double x[2], y[2];
  double xmin, ymin, xmax, ymax;

  xmin=0; ymin=0; xmax=0; ymax=0;
  r=l->raster_list;
  bounding_box=0;

  while (r!=NULL)
  {
    if (r->geoquadtree->bounding_box==1)
    {
      x[0]=r->geoquadtree->minx;
      y[0]=r->geoquadtree->miny;
      x[1]=r->geoquadtree->maxx;
      y[1]=r->geoquadtree->maxy;

      proj_transform(r->geoquadtree->p_srs, 2, x, y, p_srs_84);

      if (bounding_box==0) { xmin=x[0]; ymin=y[0]; xmax=x[1]; ymax=y[1]; }
      else
      {
        if (x[0]<xmin) xmin=x[0];
        if (x[1]<xmin) xmin=x[1];

        if (y[0]<ymin) ymin=y[0];
        if (y[1]<ymin) ymin=y[1];

        if (x[0]>xmax) xmax=x[0];
        if (x[1]>xmax) xmax=x[1];

        if (y[0]>ymax) ymax=y[0];
        if (y[1]>ymax) ymax=y[1];
      }

      bounding_box=1;
    }

    r=(raster *)r->next;
  }

  if (bounding_box==0) return 1;

  bb[0]=x[0];
  bb[1]=y[0];
  bb[2]=x[1];
  bb[3]=y[1];

  return 0;
}

/******************************************************************************/

int prepare_getcapabilities(service *Service,
                            xmlChar **buffer_capabilities,
                            int *buffer_capabilities_len)
{
  /* Writes to a buffer the WMS GetCapabilities message */

  xmlDocPtr doc;
  xmlNodePtr n_root, n_service, n_capability, n_onlineresource;
  xmlNodePtr n_contactinformation, n_contactpersonprimary, n_contactaddress;
  xmlNodePtr n_request, n_getcapabilities, n_dcptype, n_http, n_get, n_getmap;
  xmlNodePtr n_layer, n_layer_2, n_boundingbox, n_exception;
  char url[256], str[256];
  int i;
  layer *l;
  layer_srs *p_layer_srs;
  double gbb[4], x[2], y[2];

  /*
     This server supports WMS versions 1.1.1 and 1.3.0

     If the requested version is greater than or equal to 1.3.0,
     then the server will send 1.3.0

     If the requested version is less than 1.3.0,
     then the server will send 1.1.1
  */

  if (current_version>=10300) current_version=10300;
  else current_version=10101;

  doc=xmlNewDoc((xmlChar *)"1.0");

  if (current_version==10101)
  {
    xmlCreateIntSubset(doc, (xmlChar *)"WMT_MS_Capabilities", NULL,
                       (xmlChar *)"http://schemas.opengis.net/wms/1.1.1/capabilities_1_1_1.dtd");

    n_root=xmlNewNode(NULL, (xmlChar *)"WMT_MS_Capabilities");
    xmlDocSetRootElement(doc, n_root);
    xmlNewProp(n_root, (xmlChar *)"version", (xmlChar *)"1.1.1");
  }
  else
  {
    xmlCreateIntSubset(doc, (xmlChar *)"WMT_MS_Capabilities", NULL, NULL);

    n_root=xmlNewNode(NULL, (xmlChar *)"WMS_Capabilities");
    xmlDocSetRootElement(doc, n_root);
    xmlNewProp(n_root, (xmlChar *)"version", (xmlChar *)"1.3.0");
    xmlNewProp(n_root, (xmlChar *)"xmlns", (xmlChar *)"http://www.opengis.net/wms");
    xmlNewProp(n_root, (xmlChar *)"xmlns:xsi", (xmlChar *)"http://www.w3.org/2001/XMLSchema-instance");
    xmlNewProp(n_root, (xmlChar *)"xsi:schemaLocation", (xmlChar *)"http://www.opengis.net/wms http://schemas.opengis.net/wms/1.3.0/capabilities_1_3_0.xsd");
  }

  xmlNewProp(n_root, (xmlChar *)"updateSequence", (xmlChar *)configuration_file_time);

  sprintf(url, "http://%s%s", getenv("HTTP_HOST"), getenv("REQUEST_URI"));
  for (i=0; (i<strlen(url)); i++) if (url[i]=='?') url[i]=0;

  /* Define Service */

  n_service=xmlNewChild(n_root, NULL, (xmlChar *)"Service", NULL);
  xmlNewChild(n_service, NULL, (xmlChar *)"Name", (xmlChar *)"WMS"); /* Mandatory */
  xmlNewChild(n_service, NULL, (xmlChar *)"Title", (xmlChar *)Service->Title); /* Mandatory */
  xmlNewChild(n_service, NULL, (xmlChar *)"Abstract", (xmlChar *)Service->Abstract); /* 0 or 1 occurrences */

  /* n_keywordlist=xmlNewChild(n_service, NULL, "KeywordList", NULL); */ /* 0 or 1 occurrences */
  /* xmlNewChild(n_keywordlist, NULL, "Keyword", "Image Map Service"); */

  n_onlineresource=xmlNewChild(n_service, NULL, (xmlChar *)"OnlineResource", NULL); /* Mandatory */
  xmlNewProp(n_onlineresource, (xmlChar *)"xmlns:xlink", (xmlChar *)"http://www.w3.org/1999/xlink");
  xmlNewProp(n_onlineresource, (xmlChar *)"xlink:href", (xmlChar *)url);
  xmlNewProp(n_onlineresource, (xmlChar *)"xlink:type", (xmlChar *)"simple");

  n_contactinformation=xmlNewChild(n_service, NULL, (xmlChar *)"ContactInformation", NULL); /* 0 or 1 occurrences */
  n_contactpersonprimary=xmlNewChild(n_contactinformation, NULL, (xmlChar *)"ContactPersonPrimary", NULL); /* 0 or 1 occurrences */
  xmlNewChild(n_contactpersonprimary, NULL, (xmlChar *)"ContactPerson", (xmlChar *)Service->ContactPerson);
  xmlNewChild(n_contactpersonprimary, NULL, (xmlChar *)"ContactOrganization", (xmlChar *)Service->ContactOrganization);
  xmlNewChild(n_contactinformation, NULL, (xmlChar *)"ContactPosition", (xmlChar *)Service->ContactPosition); /* 0 or 1 occurrences */
  n_contactaddress=xmlNewChild(n_contactinformation, NULL, (xmlChar *)"ContactAddress", NULL); /* 0 or 1 occurrences */
  xmlNewChild(n_contactaddress, NULL, (xmlChar *)"AddressType", (xmlChar *)Service->AddressType);
  xmlNewChild(n_contactaddress, NULL, (xmlChar *)"Address", (xmlChar *)Service->Address);
  xmlNewChild(n_contactaddress, NULL, (xmlChar *)"City", (xmlChar *)Service->City);
  xmlNewChild(n_contactaddress, NULL, (xmlChar *)"StateOrProvince", (xmlChar *)Service->StateOrProvince);
  xmlNewChild(n_contactaddress, NULL, (xmlChar *)"PostCode", (xmlChar *)Service->PostCode);
  xmlNewChild(n_contactaddress, NULL, (xmlChar *)"Country", (xmlChar *)Service->Country);
  xmlNewChild(n_contactinformation, NULL, (xmlChar *)"ContactVoiceTelephone", (xmlChar *)Service->ContactVoiceTelephone); /* 0 or 1 occurrences */
  xmlNewChild(n_contactinformation, NULL, (xmlChar *)"ContactFacsimileTelephone", (xmlChar *)Service->ContactFacsimileTelephone); /* 0 or 1 occurrences */
  xmlNewChild(n_contactinformation, NULL, (xmlChar *)"ContactElectronicMailAddress", (xmlChar *)Service->ContactElectronicMailAddress); /* 0 or 1 occurrences */
  xmlNewChild(n_service, NULL, (xmlChar *)"Fees", (xmlChar *)Service->Fees); /* 0 or 1 occurrences */
  xmlNewChild(n_service, NULL, (xmlChar *)"AccessConstraints", (xmlChar *)Service->AccessConstraints); /* 0 or 1 occurrences */

  if (current_version==10303)   /* Only in WMS version 1.3.0 (not in 1.1.1) */
  {
    sprintf(str, "%i", Service->MaxWidth);
    xmlNewChild(n_service, NULL, (xmlChar *)"MaxWidth", (xmlChar *)str); /* 0 or 1 occurrences */

    sprintf(str, "%i", Service->MaxHeight);
    xmlNewChild(n_service, NULL, (xmlChar *)"MaxHeight", (xmlChar *)str); /* 0 or 1 occurrences */
  }

  /* Define Capability */

  n_capability=xmlNewChild(n_root, NULL, (xmlChar *)"Capability", NULL); /* Mandatory */
  n_request=xmlNewChild(n_capability, NULL, (xmlChar *)"Request", NULL); /* Mandatory */
  n_getcapabilities=xmlNewChild(n_request, NULL, (xmlChar *)"GetCapabilities", NULL); /* Mandatory */

  if (current_version==10101)
    xmlNewChild(n_getcapabilities, NULL, (xmlChar *)"Format", (xmlChar *)"application/vnd.ogc.wms_xml");
  else
    xmlNewChild(n_getcapabilities, NULL, (xmlChar *)"Format", (xmlChar *)"text/xml");

  n_dcptype=xmlNewChild(n_getcapabilities, NULL, (xmlChar *)"DCPType", NULL);
  n_http=xmlNewChild(n_dcptype, NULL, (xmlChar *)"HTTP", NULL);
  n_get=xmlNewChild(n_http, NULL, (xmlChar *)"Get", NULL);
  n_onlineresource=xmlNewChild(n_get, NULL, (xmlChar *)"OnlineResource", NULL);
  xmlNewProp(n_onlineresource, (xmlChar *)"xmlns:xlink", (xmlChar *)"http://www.w3.org/1999/xlink");
  xmlNewProp(n_onlineresource, (xmlChar *)"xlink:href", (xmlChar *)url);
  xmlNewProp(n_onlineresource, (xmlChar *)"xlink:type", (xmlChar *)"simple");

  n_getmap=xmlNewChild(n_request, NULL, (xmlChar *)"GetMap", NULL); /* Mandatory */
  xmlNewChild(n_getmap, NULL, (xmlChar *)"Format", (xmlChar *)"image/jpeg"); /* 1 or more times */
  xmlNewChild(n_getmap, NULL, (xmlChar *)"Format", (xmlChar *)"image/png");  /* 1 or more times */
  n_dcptype=xmlNewChild(n_getmap, NULL, (xmlChar *)"DCPType", NULL); /* 1 or more times */
  n_http=xmlNewChild(n_dcptype, NULL, (xmlChar *)"HTTP", NULL);
  n_get=xmlNewChild(n_http, NULL, (xmlChar *)"Get", NULL);
  n_onlineresource=xmlNewChild(n_get, NULL, (xmlChar *)"OnlineResource", NULL);
  xmlNewProp(n_onlineresource, (xmlChar *)"xmlns:xlink", (xmlChar *)"http://www.w3.org/1999/xlink");
  xmlNewProp(n_onlineresource, (xmlChar *)"xlink:href", (xmlChar *)url);
  xmlNewProp(n_onlineresource, (xmlChar *)"xlink:type", (xmlChar *)"simple");

  n_exception=xmlNewChild(n_capability, NULL, (xmlChar *)"Exception", NULL); /* Mandatory */

  if (current_version==10101)
    xmlNewChild(n_exception, NULL, (xmlChar *)"Format", (xmlChar *)"application/vnd.ogc.se_xml");
  else
    xmlNewChild(n_exception, NULL, (xmlChar *)"Format", (xmlChar *)"XML");

  n_layer=xmlNewChild(n_capability, NULL, (xmlChar *)"Layer", NULL); /* 0 or 1 occurrences */
  xmlNewChild(n_layer, NULL, (xmlChar *)"Title", (xmlChar *)Service->Title);

  /*
  if (current_version==10101)
  {
    xmlNewChild(n_layer, NULL, (xmlChar *)"SRS", (xmlChar *)"EPSG:23031");
    xmlNewChild(n_layer, NULL, (xmlChar *)"SRS", (xmlChar *)"EPSG:4326");
  }
  else
  {
    xmlNewChild(n_layer, NULL, (xmlChar *)"CRS", (xmlChar *)"EPSG:23031");
    xmlNewChild(n_layer, NULL, (xmlChar *)"CRS", (xmlChar *)"EPSG:4326");
  }

  if (current_version==10101)
  {
    n_boundingbox=xmlNewChild(n_layer, NULL, (xmlChar *)"LatLonBoundingBox", NULL);
    xmlNewProp(n_boundingbox, (xmlChar *)"maxx", (xmlChar *)"3.446");
    xmlNewProp(n_boundingbox, (xmlChar *)"maxy", (xmlChar *)"42.881");
    xmlNewProp(n_boundingbox, (xmlChar *)"minx", (xmlChar *)"0.037");
    xmlNewProp(n_boundingbox, (xmlChar *)"miny", (xmlChar *)"40.439");
  }
  else
  {
    n_boundingbox=xmlNewChild(n_layer, NULL, (xmlChar *)"EX_GeographicBoundingBox", NULL);
    xmlNewChild(n_boundingbox, NULL, (xmlChar *)"westBoundLongitude", (xmlChar *)"3.446");
    xmlNewChild(n_boundingbox, NULL, (xmlChar *)"eastBoundLongitude", (xmlChar *)"42.881");
    xmlNewChild(n_boundingbox, NULL, (xmlChar *)"southBoundLatitude", (xmlChar *)"0.037");
    xmlNewChild(n_boundingbox, NULL, (xmlChar *)"northBoundLatitude", (xmlChar *)"40.439");
  }
 
  n_boundingbox=xmlNewChild(n_layer, NULL, (xmlChar *)"BoundingBox", NULL);
  if (current_version==10101)
    xmlNewProp(n_boundingbox, (xmlChar *)"SRS", (xmlChar *)"EPSG:23031");
  else
    xmlNewProp(n_boundingbox, (xmlChar *)"CRS", (xmlChar *)"EPSG:23031");
  xmlNewProp(n_boundingbox, (xmlChar *)"maxx", (xmlChar *)"534957.0");
  xmlNewProp(n_boundingbox, (xmlChar *)"maxy", (xmlChar *)"4751992.0");
  xmlNewProp(n_boundingbox, (xmlChar *)"minx", (xmlChar *)"258007.0");
  xmlNewProp(n_boundingbox, (xmlChar *)"miny", (xmlChar *)"4485017.0");
  xmlNewProp(n_boundingbox, (xmlChar *)"resx", (xmlChar *)"0.5");
  xmlNewProp(n_boundingbox, (xmlChar *)"resy", (xmlChar *)"0.5");
  */

  /* For each layer */

  l=Service->layer_list;
  while (l!=NULL)
  {
    n_layer_2=xmlNewChild(n_layer, NULL, (xmlChar *)"Layer", NULL);
    xmlNewProp(n_layer_2, (xmlChar *)"opaque", (xmlChar *)"0");
    xmlNewProp(n_layer_2, (xmlChar *)"queryable", (xmlChar *)"0");
    xmlNewProp(n_layer_2, (xmlChar *)"noSubsets", (xmlChar *)"0");
    xmlNewChild(n_layer_2, NULL, (xmlChar *)"Name", (xmlChar *)l->name);
    xmlNewChild(n_layer_2, NULL, (xmlChar *)"Title", (xmlChar *)l->title);
    //n_keywordlist=xmlNewChild(n_layer_2, NULL, "KeywordList", NULL);
    //xmlNewChild(n_keywordlist, NULL, "Keyword", "orto");
    //xmlNewChild(n_keywordlist, NULL, "Keyword", "imatge");
    //xmlNewChild(n_keywordlist, NULL, "Keyword", "Catalunya");
    //xmlNewChild(n_keywordlist, NULL, "Keyword", "1:5.000");
    //xmlNewChild(n_keywordlist, NULL, "Keyword", "ICC");

    p_layer_srs=l->layer_srs_list;
    while (p_layer_srs!=NULL)
    {
      if (current_version==10101)
        xmlNewChild(n_layer_2, NULL, (xmlChar *)"SRS",
                    (xmlChar *)p_layer_srs->name);
      else
        xmlNewChild(n_layer_2, NULL, (xmlChar *)"CRS",
                    (xmlChar *)p_layer_srs->name);

      p_layer_srs=(layer_srs *)p_layer_srs->next;
    }

    gbb[0]=0; gbb[1]=0; gbb[2]=0; gbb[3]=0;
    geographic_bounding_box(l, gbb);

    if (current_version==10101)
    {
      n_boundingbox=xmlNewChild(n_layer_2, NULL,
                                (xmlChar *)"LatLonBoundingBox", NULL);

      sprintf(str, "%f", gbb[0]);
      xmlNewProp(n_boundingbox, (xmlChar *)"minx", (xmlChar *)str);

      sprintf(str, "%f", gbb[1]);
      xmlNewProp(n_boundingbox, (xmlChar *)"miny", (xmlChar *)str);

      sprintf(str, "%f", gbb[2]);
      xmlNewProp(n_boundingbox, (xmlChar *)"maxx", (xmlChar *)str);

      sprintf(str, "%f", gbb[3]);
      xmlNewProp(n_boundingbox, (xmlChar *)"maxy", (xmlChar *)str);
    }
    else
    {
      n_boundingbox=xmlNewChild(n_layer_2, NULL,
                                (xmlChar *)"EX_GeographicBoundingBox", NULL);

      sprintf(str, "%f", gbb[0]);
      xmlNewChild(n_boundingbox, NULL, (xmlChar *)"westBoundLongitude",
                  (xmlChar *)str);

      sprintf(str, "%f", gbb[2]);
      xmlNewChild(n_boundingbox, NULL, (xmlChar *)"eastBoundLongitude",
                  (xmlChar *)str);

      sprintf(str, "%f", gbb[1]);
      xmlNewChild(n_boundingbox, NULL, (xmlChar *)"southBoundLatitude",
                  (xmlChar *)str);

      sprintf(str, "%f", gbb[3]);
      xmlNewChild(n_boundingbox, NULL, (xmlChar *)"northBoundLatitude",
                  (xmlChar *)str);
    }

    p_layer_srs=l->layer_srs_list;
    while (p_layer_srs!=NULL)
    {
      x[0]=gbb[0]; y[0]=gbb[1];
      x[1]=gbb[2]; y[1]=gbb[3];
      proj_transform(p_srs_84, 2, x, y, p_layer_srs->p_srs);

      n_boundingbox=xmlNewChild(n_layer_2, NULL, (xmlChar *)"BoundingBox", NULL);
      if (current_version==10101)
        xmlNewProp(n_boundingbox, (xmlChar *)"SRS",
                   (xmlChar *)p_layer_srs->name);
      else
        xmlNewProp(n_boundingbox, (xmlChar *)"CRS",
                   (xmlChar *)p_layer_srs->name);

      sprintf(str, "%f", x[0]);
      xmlNewProp(n_boundingbox, (xmlChar *)"minx", (xmlChar *)str);

      sprintf(str, "%f", y[0]);
      xmlNewProp(n_boundingbox, (xmlChar *)"miny", (xmlChar *)str);

      sprintf(str, "%f", x[1]);
      xmlNewProp(n_boundingbox, (xmlChar *)"maxx", (xmlChar *)str);

      sprintf(str, "%f", y[1]);
      xmlNewProp(n_boundingbox, (xmlChar *)"maxy", (xmlChar *)str);

      p_layer_srs=(layer_srs *)p_layer_srs->next;
    }

    //n_attribution=xmlNewChild(n_layer_2, NULL, "Attribution", NULL);
    //xmlNewChild(n_attribution, NULL, "Title", "Institut Cartografic de Catalunya");
    //n_onlineresource=xmlNewChild(n_attribution, NULL, "OnlineResource", NULL);
    //xmlNewProp(n_onlineresource, "xmlns:xlink", "http://www.w3.org/1999/xlink");
    //xmlNewProp(n_onlineresource, "xlink:href", "http://www.icc.es/");
    //xmlNewProp(n_onlineresource, "xlink:type", "simple");
    //n_logourl=xmlNewChild(n_attribution, NULL, "LogoURL", NULL);
    //xmlNewProp(n_logourl, "width", "250");
    //xmlNewProp(n_logourl, "height", "164");
    //xmlNewChild(n_logourl, NULL, "Format", "image/gif");
    //n_onlineresource=xmlNewChild(n_logourl, NULL, "OnlineResource", NULL);
    //xmlNewProp(n_onlineresource, "xmlns:xlink", "http://www.w3.org/1999/xlink");
    //xmlNewProp(n_onlineresource, "xlink:href", "http://www.icc.es/images/logowms.gif");
    //xmlNewProp(n_onlineresource, "xlink:type", "simple");

    l=(layer *)(l->next);
  }

  xmlDocDumpFormatMemory(doc, buffer_capabilities, buffer_capabilities_len, 1);

  xmlFreeDoc(doc);

  xmlCleanupParser();
  xmlMemoryDump();

  return 0;
}

/******************************************************************************/

void init_service(service *Service)
{
  /* Initializes the Service structure, so that the optional parameters
     don't store old values */

  Service->Title=NULL;
  Service->Abstract=NULL;
  Service->Fees=NULL;
  Service->AccessConstraints=NULL;
  Service->ContactPerson=NULL;
  Service->ContactOrganization=NULL;
  Service->ContactPosition=NULL;
  Service->AddressType=NULL;
  Service->Address=NULL;
  Service->City=NULL;
  Service->StateOrProvince=NULL;
  Service->PostCode=NULL;
  Service->Country=NULL;
  Service->ContactVoiceTelephone=NULL;
  Service->ContactFacsimileTelephone=NULL;
  Service->ContactElectronicMailAddress=NULL;
  Service->MaxWidth=0;
  Service->MaxHeight=0;
  Service->layer_list=NULL;
}

/******************************************************************************/

int image_from_layers(service *Service, image *ima, char *layer_names,
                      char *str_srs)
{
  /* Writes into a buffer an image corresponding to a set of layers,
     a SRS, a bounding box in world units, and the size in pixels */

  char *layer_name;
  layer *l;
  raster *r;
  srs *p_srs;
  unsigned char alpha;
  image im;
  long i, length;
  int ret;

  im.width=ima->width;
  im.height=ima->height;

  im.minx=ima->minx;
  im.miny=ima->miny;
  im.maxx=ima->maxx;
  im.maxy=ima->maxy;

  length=(long)im.width*(long)im.height;

  im.buffer=malloc(length*4L);
  if (im.buffer==NULL) { fprintf(stderr, "image_from_layer malloc"); return 1; }

  layer_name=strtok(layer_names, ",");

  while (layer_name!=NULL)
  {
    /* For each raster composing this layer (buffer "im"),
       the function gqt_export is called.

       The returned images are fusioned into the buffer "image". */
    
    l=seek_layer(Service, layer_name);
    if (l==NULL)
    { exception("", "Layer unknown"); free(im.buffer); return 1; }

    r=l->raster_list;

    while (r!=NULL)
    {
      p_srs=seek_layer_srs(l, str_srs);
      if (p_srs==NULL)
      { exception("", "Invalid SRS"); free(im.buffer); return 1; }

      for (i=0; (i<length*4); )
      {
        im.buffer[i++]=255;
        im.buffer[i++]=0;
        im.buffer[i++]=0;
        im.buffer[i++]=0;
      }

      ret=gqt_export(r->geoquadtree, &im, p_srs, 1); // bicubic filter

      if (ret==0)
      {
        /* Puts im over image */

        for (i=0; (i<length*4); )
        {
          alpha=im.buffer[i+3]; /* This is the value of the alpha channel */

          ima->buffer[i]=((long)im.buffer[i]*(long)alpha+(long)ima->buffer[i]*(255-(long)alpha))/255; i++;
          ima->buffer[i]=((long)im.buffer[i]*(long)alpha+(long)ima->buffer[i]*(255-(long)alpha))/255; i++;
          ima->buffer[i]=((long)im.buffer[i]*(long)alpha+(long)ima->buffer[i]*(255-(long)alpha))/255; i++;
          ima->buffer[i]=((long)im.buffer[i]*(long)alpha+(long)ima->buffer[i]*(255-(long)alpha))/255; i++;
        }
      }

      r=(raster *)r->next;
    }

    layer_name=strtok(NULL, ",");
  }

  free(im.buffer);

  return 0;
}

/******************************************************************************/

int main(int argc, char *argv[])
{
  /* This is the main function of the WMS service */  

  char query_string[1024], *pch;
  char version[256], request[256], layers[256], format[256];
  char sbbox[256], swidth[256], sheight[256], styles[256], srs[256];
  char transparent[256], bgcolor[256], updatesequence[256], str[256];
  int red, green, blue;
  double bbox[4];
  int size[2];
  unsigned long length, i;
  image im;
  service Service;
  int buffer_capabilities_len;
  xmlChar *buffer_capabilities;
  int ret;

  buffer_capabilities=NULL;
  buffer_capabilities_len=0;

  strcpy(configuration_file, "/etc/geoquadtree/geoquadtreeserver.xml");

  init_service(&Service);
  read_configuration(&Service, configuration_file);
  list_configuration(&Service, "/tmp/geoquadtree.log");
  get_mtime(configuration_file, configuration_file_time); 
  init_logo(&Service);
  init_resample();
  srs_import(&p_srs_84, "EPSG", "EPSG:4326");

  /* This is the never ending loop in this FastCGI program */

  while (FCGI_Accept()>=0)
  {
    strncpy(query_string, getenv("QUERY_STRING"), 1024);
    query_string[1024]=0;

    strcpy(version, " ");
    strcpy(request, " ");
    strcpy(layers, " ");
    strcpy(styles, " ");
    strcpy(srs, " ");
    strcpy(sbbox, " ");
    strcpy(swidth, " ");
    strcpy(sheight, " ");
    strcpy(format, " ");
    strcpy(transparent, " ");
    strcpy(bgcolor, " ");
    strcpy(updatesequence, " ");

    pch=strtok(query_string, "&");
    while (pch!=NULL)
    {
      if (strncasecmp(pch, "VERSION=",      8)==0)
        strncpy(version,     &pch[ 8], 256);

      if (strncasecmp(pch, "REQUEST=",      8)==0)
        strncpy(request,     &pch[ 8], 256);

      if (strncasecmp(pch, "LAYERS=",       7)==0)
        strncpy(layers,      &pch[ 7], 256);

      if (strncasecmp(pch, "STYLES=",       7)==0)
        strncpy(styles,      &pch[ 7], 256);

      if (strncasecmp(pch, "SRS=",          4)==0)
        strncpy(srs,         &pch[ 4], 256);

      if (strncasecmp(pch, "BBOX=",         5)==0)
        strncpy(sbbox,       &pch[ 5], 256);

      if (strncasecmp(pch, "WIDTH=",        6)==0)
        strncpy(swidth,      &pch[ 6], 256);

      if (strncasecmp(pch, "HEIGHT=",       7)==0)
        strncpy(sheight,     &pch[ 7], 256);

      if (strncasecmp(pch, "FORMAT=",       7)==0)
        strncpy(format,      &pch[ 7], 256);

      if (strncasecmp(pch, "TRANSPARENT=", 12)==0)
        strncpy(transparent, &pch[12], 256);

      if (strncasecmp(pch, "BGCOLOR=",      8)==0)
        strncpy(bgcolor,     &pch[ 8], 256);

      if (strncasecmp(pch, "UPDATESEQUENCE=",     15)==0)
        strncpy(updatesequence, &pch[15], 256);

      pch=strtok(NULL, "&");
    }

    if (strlen(request)==0)
    {
      exception("", "Missing parameter: REQUEST");
      continue;
    }

    if ((strcmp(request, "GetCapabilities")==0)||
        (strcmp(request, "capabilities")==0))
    {
      if (strcmp(updatesequence, " ")!=0)
      {
        if (strcmp(updatesequence, configuration_file_time)==0)
        {
          exception("CurrentUpdateSequence", "Capabilities have not changed");
          continue;
        }

        if (strcmp(updatesequence, configuration_file_time)>0)
        {
          exception("InvalidUpdateSequence", "Capabilities older than requested");
          continue;
        }
      }

      /* In response to a GetCapabilities request that does not specify a
         version number, the server shall respond with the highest version
         it supports */

      if (strcmp(version, " ")==0) strcpy(version, "1.3.0");

      current_version=version2numeric(version);

      if (current_version>0)
      {
        if (prepare_getcapabilities(&Service,
                                    &buffer_capabilities,
                                    &buffer_capabilities_len)==0)
        {
          printf("Content-type: application/vnd.ogc.wms_xml\r\n\r\n");
          fwrite(buffer_capabilities, buffer_capabilities_len, 1, stdout);
        }
      }

      continue;
    }
    else if (strcmp(request, "GetMap")==0)
    {
      /* The VERSION parameter is madatory in requests
         other than GetCapabilities */

      if (strlen(version)==0)
      { exception("VersionNotDefined", "Missing parameter: VERSION"); continue; }

      current_version=version2numeric(version);
      if (current_version==0) continue;

      if ((current_version!=10101)&&(current_version!=10300))
      { exception("", "Version not supported"); continue; }
      
      if (strcmp(layers,  " ")==0)
      { exception("LayerNotDefined", "Missing parameter: LAYERS"); continue; }

      if (strcmp(styles,  " ")==0)
      { exception("StyleNotDefined", "Missing parameter: STYLES"); continue; }

      if (strcmp(srs,     " ")==0)
      { exception("CRSNotDefined", "Missing parameter: CRS"); continue; }

      if (strcmp(sbbox,   " ")==0)
      { exception("", "Missing parameter: SBBOX"); continue; }

      if (strcmp(swidth,  " ")==0)
      { exception("", "Missing parameter: WIDTH");  continue; }

      if (strcmp(sheight, " ")==0)
      { exception("", "Missing parameter: HEIGHT"); continue; }

      if (strcmp(format,  " ")==0)
      { exception("InvalidFormat", "Missing parameter: FORMAT"); continue; }

      if (strcmp(transparent, " ")==0)
      { strcpy(transparent, "FALSE"); }

      if (strcmp(bgcolor, " ")==0)
      { strcpy(bgcolor, "0xFFFFFF"); }

      pch=strtok(sbbox, ","); bbox[0]=atof(pch);
      pch=strtok(NULL,  ","); bbox[1]=atof(pch);
      pch=strtok(NULL,  ","); bbox[2]=atof(pch);
      pch=strtok(NULL,  ","); bbox[3]=atof(pch);

      size[0]=atoi(swidth);
      size[1]=atoi(sheight);

      if (size[0]<=0)
      { exception("", "Invalid width: must be positive"); continue; }

      if (size[1]<=0)
      { exception("", "Invalid height: must be positive"); continue; }

      if (size[0]>Service.MaxWidth)
      {
        sprintf(str, "Invalid width %i: cannot be more than MaxWidth (%i)",
                size[0], Service.MaxWidth);
        exception("", str);
        continue;
      }

      if (size[1]>Service.MaxHeight)
      {
        sprintf(str, "Invalid width %i: cannot be more than MaxHeight (%i)",
                size[1], Service.MaxHeight);
        exception("", str);
        continue;
      }

      if (bbox[0]>bbox[2])
      { exception("", "Invalid bounding box (xmin>xmax)"); continue; }

      if (bbox[1]>bbox[3])
      { exception("", "Invalid bounding box (ymin<ymax)"); continue; }

      strlwr(format); /* Converts format to lowercase */
      if ((strcmp(format, "jpeg")==0)||(strcmp(format, "image/jpeg")==0)||
          (strcmp(format, "jpg")==0)||(strcmp(format, "image/jpg")==0))
      { strcpy(format, "image/jpeg"); }
      else if ((strcmp(format, "png")==0)||(strcmp(format, "image/png")==0))
      { strcpy(format, "image/png"); }
      else
      { exception("", "Invalid image format"); continue; }

      im.width=size[0];
      im.height=size[1];

      im.minx=bbox[0];
      im.miny=bbox[1];
      im.maxx=bbox[2];
      im.maxy=bbox[3];

      length=im.width*im.height;
      im.buffer=malloc(length*4);
      if (im.buffer==NULL) { printf("image_from_layer malloc\n"); return 1; }

      if (strcmp(transparent, "FALSE")==0)
      {
        str[0]=bgcolor[2]; str[1]=bgcolor[3]; str[2]=0; sscanf(str, "%x", &red);
        str[0]=bgcolor[4]; str[1]=bgcolor[5]; str[2]=0; sscanf(str, "%x", &green);
        str[0]=bgcolor[6]; str[1]=bgcolor[7]; str[2]=0; sscanf(str, "%x", &blue);

        for (i=0; (i<length*4); )
        {
          im.buffer[i++]=red;
          im.buffer[i++]=green;
          im.buffer[i++]=blue;
          im.buffer[i++]=255;
        }
      }
      else
      {
        for (i=0; (i<length*4); i++) im.buffer[i]=0;
      }

      ret=image_from_layers(&Service, &im, layers, srs);
      if (ret==0)
      {
        add_logo(&im);

        if (strcmp(format, "image/jpeg")==0)
        {
          printf("Content-type: image/jpeg\r\n\r\n");
          write_jpg(&im, NULL);
        }
        else
        {
          printf("Content-type: image/png\r\n\r\n");
          write_png(&im, NULL);
        }
      }

      free(im.buffer);

      continue;
    }

    exception("", "Invalid request");
  }

  return 0;
}

/******************************************************************************/
