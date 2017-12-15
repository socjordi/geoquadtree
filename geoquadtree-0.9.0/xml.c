/*

xml.c - GeoQuadTree XML interface

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
#include <libxml/xinclude.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "xml.h"
#include "proj.h"

/******************************************************************************/

int xmlprop(xmlNodePtr node, xmlChar *name, char **dst)
{
  xmlChar *prop;

  prop=xmlGetProp(node, name);
  if (prop==NULL) return 1;

  *dst=malloc(strlen((char *)prop)+1);
  strcpy(*dst, (char *)prop);
  xmlFree(prop);

  return 0;
}

/******************************************************************************/

void xmlvalue(xmlNodePtr node, xmlChar *name, char **dst)
{
  xmlChar *value;

  if ((!xmlStrcmp(node->name, name)))
  {
    value=xmlNodeGetContent(node);
    *dst=malloc(strlen((char *)value)+1);
    strcpy(*dst, (char *)value);
    xmlFree(value);
  }
}

/******************************************************************************/

int gqt_write_metadata(char *geoquadtree_xml, gqt *g)
{
  xmlDocPtr doc;
  xmlNodePtr n_root;
  char str[256];
  char *wkt;
  char *geoquadtree_prj=NULL;
  int i;
  FILE *fp;

  doc=xmlNewDoc((xmlChar *)"1.0");

  n_root=xmlNewNode(NULL, (xmlChar *)"GeoQuadTree");
  xmlDocSetRootElement(doc, n_root);

  xmlNewProp(n_root, (xmlChar *)"name", (xmlChar *)g->name);

  sprintf(str, "%i", g->levels);
  xmlNewProp(n_root, (xmlChar *)"levels", (xmlChar *)str);

  sprintf(str, "%0.15f", g->resx);
  xmlNewProp(n_root, (xmlChar *)"resx", (xmlChar *)str);

  sprintf(str, "%0.15f", g->resy);
  xmlNewProp(n_root, (xmlChar *)"resy", (xmlChar *)str);

  sprintf(str, "%i", g->tilesizex);
  xmlNewProp(n_root, (xmlChar *)"tilesizex", (xmlChar *)str);

  sprintf(str, "%i", g->tilesizey);
  xmlNewProp(n_root, (xmlChar *)"tilesizey", (xmlChar *)str);

  if (g->bounding_box==1)
  {
    sprintf(str, "%0.15f", g->minx);
    xmlNewProp(n_root, (xmlChar *)"minx", (xmlChar *)str);

    sprintf(str, "%0.15f", g->miny);
    xmlNewProp(n_root, (xmlChar *)"miny", (xmlChar *)str);

    sprintf(str, "%0.15f", g->maxx);
    xmlNewProp(n_root, (xmlChar *)"maxx", (xmlChar *)str);

    sprintf(str, "%0.15f", g->maxy);
    xmlNewProp(n_root, (xmlChar *)"maxy", (xmlChar *)str);
  }

  xmlSaveFile(geoquadtree_xml, doc);

  xmlFreeDoc(doc);

  xmlCleanupParser();
  xmlMemoryDump();

  geoquadtree_prj=malloc(strlen(geoquadtree_xml)+1);
  strcpy(geoquadtree_prj, geoquadtree_xml);

  for (i=strlen(geoquadtree_prj)-1; (i>=0); i--)
    if (geoquadtree_prj[i]=='.') break;
  geoquadtree_prj[i]=0;
  strcat(geoquadtree_prj, ".prj"); 

  if (OSRExportToWkt(g->p_srs, &wkt) != OGRERR_NONE)
  {
    fprintf(stderr, "Exporting dataset projection failed");
    return 1;
  }

  fp=fopen(geoquadtree_prj, "wt");
  fprintf(fp, "%s", wkt);
  fclose(fp);

  free(wkt);
  free(geoquadtree_prj);

  return 0;
}

/******************************************************************************/

void gqt_read_metadata(char *geoquadtree_xml, gqt *g)
{
  xmlParserCtxtPtr ctxt;
  xmlDocPtr doc;
  xmlNodePtr cur;
  char *str;
  char *geoquadtree_prj=NULL;
  int i;

  ctxt = xmlNewParserCtxt();
  if (ctxt == NULL)
  { fprintf(stderr, "Failed to allocate parser context\n"); exit(1); }

  doc = xmlCtxtReadFile(ctxt, geoquadtree_xml, NULL, 0);
  if (doc == NULL)
  { fprintf(stderr, "Failed to parse %s\n", geoquadtree_xml); exit(1); }

  xmlFreeParserCtxt(ctxt);

  cur = xmlDocGetRootElement(doc);
  if (cur == NULL)
  { fprintf(stderr,"Empty document\n"); xmlFreeDoc(doc); return; }

  if (xmlStrcmp(cur->name, (const xmlChar *)"GeoQuadTree"))
  {
    printf("get_read_metadata: not a valid GeoQuadTree image");
    xmlFreeDoc(doc);
    return;
  }

  xmlprop(cur, (xmlChar *)"name", &(g->name));
  xmlprop(cur, (xmlChar *)"levels", &str); g->levels=atoi(str); free(str);
  xmlprop(cur, (xmlChar *)"resx", &str); g->resx=atof(str); free(str);
  xmlprop(cur, (xmlChar *)"resy", &str); g->resy=atof(str); free(str);
  xmlprop(cur, (xmlChar *)"tilesizex", &str); g->tilesizex=atoi(str); free(str);
  xmlprop(cur, (xmlChar *)"tilesizey", &str); g->tilesizey=atoi(str); free(str);

  g->bounding_box=1;

  if (xmlprop(cur, (xmlChar *)"minx", &str)==1) g->bounding_box=0;
  else { g->minx=atof(str); free(str); }

  if (xmlprop(cur, (xmlChar *)"miny", &str)==1) g->bounding_box=0;
  else { g->miny=atof(str); free(str); }

  if (xmlprop(cur, (xmlChar *)"maxx", &str)==1) g->bounding_box=0;
  else { g->maxx=atof(str); free(str); }

  if (xmlprop(cur, (xmlChar *)"maxy", &str)==1) g->bounding_box=0;
  else { g->maxy=atof(str); free(str); }

  xmlFreeDoc(doc);

  geoquadtree_prj=malloc(strlen(geoquadtree_xml)+1);
  strcpy(geoquadtree_prj, geoquadtree_xml);

  for (i=strlen(geoquadtree_prj)-1; (i>=0); i--)
    if (geoquadtree_prj[i]=='.') break;
  geoquadtree_prj[i]=0;
  strcat(geoquadtree_prj, ".prj"); 

  srs_import_file(&(g->p_srs), geoquadtree_prj);

  for (i=strlen(geoquadtree_xml)-1; ((i>=0)&&(geoquadtree_xml[i]!='/')); i--);
  g->path=malloc(i+1);
  strncpy(g->path, geoquadtree_xml, i);
  g->path[i]=0;
}

/******************************************************************************/

void parse_service(service *Service, xmlDocPtr doc, xmlNodePtr cur)
{
  xmlNodePtr cur2, cur3;
  char *maxwidth, *maxheight;

  cur=cur->xmlChildrenNode;

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
  Service->MaxWidth=2048;
  Service->MaxHeight=2048;
  Service->Logo=NULL;

  while (cur!=NULL)
  {
    xmlvalue(cur, (xmlChar *)"Title", &(Service->Title));
    xmlvalue(cur, (xmlChar *)"Abstract", &(Service->Abstract));
    xmlvalue(cur, (xmlChar *)"Fees", &(Service->Fees));
    xmlvalue(cur, (xmlChar *)"AccessConstraints", &(Service->AccessConstraints));
    xmlvalue(cur, (xmlChar *)"MaxWidth", &maxwidth);
    xmlvalue(cur, (xmlChar *)"MaxHeight", &maxheight);
    xmlvalue(cur, (xmlChar *)"Logo", &(Service->Logo));

    if ((!xmlStrcmp(cur->name, (const xmlChar *)"ContactInformation")))
    {
      cur2=cur->xmlChildrenNode;

      while (cur2!=NULL)
      {
        xmlvalue(cur2, (xmlChar *)"ContactPosition", &(Service->ContactPosition));
        xmlvalue(cur2, (xmlChar *)"ContactVoiceTelephone", &(Service->ContactVoiceTelephone));
        xmlvalue(cur2, (xmlChar *)"ContactFacsimileTelephone", &(Service->ContactFacsimileTelephone));
        xmlvalue(cur2, (xmlChar *)"ContactElectronicMailAddress", &(Service->ContactElectronicMailAddress));

        if ((!xmlStrcmp(cur2->name, (const xmlChar *)"ContactPersonPrimary")))
        {
          cur3=cur2->xmlChildrenNode;

          while (cur3 != NULL)
          {
            xmlvalue(cur3, (xmlChar *)"ContactPerson", &(Service->ContactPerson));
            xmlvalue(cur3, (xmlChar *)"ContactOrganization", &(Service->ContactOrganization));

            cur3=cur3->next;
          }
        }

        if ((!xmlStrcmp(cur2->name, (const xmlChar *)"ContactAddress")))
        {
          cur3=cur2->xmlChildrenNode;

          while (cur3!=NULL)
          {
            xmlvalue(cur3, (xmlChar *)"AddressType", &(Service->AddressType));
            xmlvalue(cur3, (xmlChar *)"Address", &(Service->Address));
            xmlvalue(cur3, (xmlChar *)"City", &(Service->City));
            xmlvalue(cur3, (xmlChar *)"StateOrProvince", &(Service->StateOrProvince));
            xmlvalue(cur3, (xmlChar *)"PostCode", &(Service->PostCode));
            xmlvalue(cur3, (xmlChar *)"Country", &(Service->Country));

            cur3=cur3->next;
          }
        }

        cur2=cur2->next;
      }
    }

    cur=cur->next;
  }

  if (strlen(maxwidth)>0) Service->MaxWidth=atoi(maxwidth);
  if (strlen(maxheight)>0) Service->MaxHeight=atoi(maxheight);
}

/******************************************************************************/

layer *add_layer(service *Service, char *name, char *title)
{
  layer *l;

  l=malloc(sizeof(layer));
  if (l==NULL) {printf("add_layer malloc\n"); exit(1); }

  l->name=malloc(strlen(name)+1); strcpy(l->name, name);
  l->title=malloc(strlen(title)+1); strcpy(l->title, title);
  l->layer_srs_list=NULL;
  l->raster_list=NULL;
  l->next=(struct layer *)Service->layer_list;
  Service->layer_list=l;

  return l;
}

/******************************************************************************/

layer *seek_layer(service *Service, char *name)
{
  layer *l;

  l=Service->layer_list;
  while (l!=NULL)
  {
    if (strcmp(name, l->name)==0) return l;
    l=(layer *)(l->next);
  }

  return NULL;
}

/******************************************************************************/

void add_raster(layer *l, char *path,
                double minresx, double minresy, double maxresx, double maxresy)
{
  raster *r;
  gqt *g;

  if (l==NULL) { printf("add_raster: layer is null\n"); exit(1); }

  r=malloc(sizeof(raster));
  if (r==NULL) { printf("add_raster: malloc\n"); exit(1); }

  g=malloc(sizeof(gqt));
  if (g==NULL) { printf("add_raster: malloc\n"); exit(1); }

  gqt_read_metadata(path, g);

  r->geoquadtree=g;

  r->minresx=minresx;
  r->minresy=minresy;
  r->maxresx=maxresx;
  r->maxresy=maxresy;

  r->next=(struct raster *)(l->raster_list);
  l->raster_list=r;
}

/******************************************************************************/

void add_layer_srs(layer *l, char *name, char *path)
{
  layer_srs *ls;

  ls=malloc(sizeof(layer_srs));
  if (ls==NULL) { fprintf(stderr, "add_layer_srs malloc"); exit(1); }

  ls->name=malloc(strlen(name)+1);
  if (ls->name==NULL) { fprintf(stderr, "add_layer_srs malloc"); exit(1); }
  strcpy(ls->name, name);

  srs_import_file(&(ls->p_srs), path);

  ls->next=(struct layer_srs *)l->layer_srs_list;
  l->layer_srs_list=ls;
}

/******************************************************************************/

srs *seek_layer_srs(layer *l, char *srs)
{
  layer_srs *ls;

  ls=l->layer_srs_list;

  while (ls!=NULL)
  {
    if (strcmp(ls->name, srs)==0) return ls->p_srs;
    ls=(layer_srs *)ls->next;
  }

  return NULL;
}

/******************************************************************************/

void parse_layer(service *Service, xmlDocPtr doc, xmlNodePtr cur)
{
  layer *l;
  char *Name, *Title, *Path;
  char *MinResX, *MinResY, *MaxResX, *MaxResY;

  xmlprop(cur, (xmlChar *)"Name", &Name);
  xmlprop(cur, (xmlChar *)"Title", &Title);

  l=add_layer(Service, Name, Title);

  free(Name);
  free(Title);

  cur=cur->xmlChildrenNode;

  while (cur != NULL)
  {
    if ((!xmlStrcmp(cur->name, (const xmlChar *)"SRS")))
    {
      xmlprop(cur, (xmlChar *)"Name", &Name);
      xmlprop(cur, (xmlChar *)"Path", &Path);

      add_layer_srs(l, Name, Path);

      free(Name);
      free(Path);
    }

    if ((!xmlStrcmp(cur->name, (const xmlChar *)"GeoQuadTree")))
    {
      xmlprop(cur, (xmlChar *)"Path", &Path);

      xmlprop(cur, (xmlChar *)"MinResX", &MinResX);
      xmlprop(cur, (xmlChar *)"MaxResX", &MaxResX);
      xmlprop(cur, (xmlChar *)"MinResY", &MinResY);
      xmlprop(cur, (xmlChar *)"MaxResY", &MaxResY);
        
      add_raster(l, Path,
                 atof(MinResX), atof(MinResY), atof(MaxResX), atof(MaxResY));

      free(Path);
      free(MinResX);
      free(MinResY);
      free(MaxResX);
      free(MaxResY);
    }

    cur = cur->next;
  }
}

/******************************************************************************/

int read_configuration(service *Service, const char *filename)
{
  xmlParserCtxtPtr ctxt;
  xmlDocPtr doc;
  xmlNodePtr cur;

  ctxt=xmlNewParserCtxt();
  if (ctxt==NULL)
  {
    fprintf(stderr, "read_configuration: Failed to allocate parser context\n");
    return 1;
  }

  doc=xmlCtxtReadFile(ctxt, filename, NULL, XML_PARSE_DTDVALID);
  if (doc==NULL) { fprintf(stderr, "Failed to parse %s\n", filename); exit(1); }
  else
  {
    if (ctxt->valid==0)
    {
      fprintf(stderr, "read_configuration: Failed to validate %s\n", filename);
      xmlFreeDoc(doc);
      return 1;
    }
  }
  xmlFreeParserCtxt(ctxt);

  /* The XML file will be read two times, because in the first stage the Service
     structure will be created, and in the second stage the layers will be
     linked to the Service structure */

  /* First stage: process Server and Raster elements */

  cur=xmlDocGetRootElement(doc);
  if (cur == NULL)
  {
    fprintf(stderr,"read_configuration: empty document\n");
    xmlFreeDoc(doc);
    return 1;
  }

  if (xmlStrcmp(cur->name, (const xmlChar *)"GeoQuadTreeServer"))
  {
    printf("read_configuration: not a GeoQuadTreeServer document\n");
    xmlFreeDoc(doc);
    return 1;
  }

  cur=cur->xmlChildrenNode;

  while (cur!=NULL)
  {
    if ((!xmlStrcmp(cur->name, (const xmlChar *)"Service")))
      parse_service(Service, doc, cur);

    cur=cur->next;
  }

  /* Second stage: process Layer elements */

  cur=xmlDocGetRootElement(doc);
  cur=cur->xmlChildrenNode;

  while (cur != NULL)
  {
    if ((!xmlStrcmp(cur->name, (const xmlChar *)"Layer")))
      parse_layer(Service, doc, cur);

    cur=cur->next;
  }

  xmlFreeDoc(doc);

  return 0;
}

/******************************************************************************/

int list_configuration(service *Service, char *filename)
{
  layer *l;
  layer_srs *ls;
  raster *r;
  gqt *g;
  FILE *fp;

  fp=fopen(filename, "wt");
  if (fp==NULL) { fprintf(stderr, "list_configuration: fopen\n"); return 1; }

  fprintf(fp, "Service:\n\n");

  fprintf(fp, "\tTitle: %s\n", Service->Title);
  fprintf(fp, "\tAbstract: %s\n", Service->Abstract);
  fprintf(fp, "\tFees: %s\n", Service->Fees);
  fprintf(fp, "\tAccessConstraints: %s\n", Service->AccessConstraints);
  fprintf(fp, "\tContactPerson: %s\n", Service->ContactPerson);
  fprintf(fp, "\tContactOrganization: %s\n", Service->ContactOrganization);
  fprintf(fp, "\tContactPosition: %s\n", Service->ContactPosition);
  fprintf(fp, "\tAddressType: %s\n", Service->AddressType);
  fprintf(fp, "\tAddress: %s\n", Service->Address);
  fprintf(fp, "\tCity: %s\n", Service->City);
  fprintf(fp, "\tStateOrProvince: %s\n", Service->StateOrProvince);
  fprintf(fp, "\tPostCode: %s\n", Service->PostCode);
  fprintf(fp, "\tCountry: %s\n", Service->Country);
  fprintf(fp, "\tContactVoiceTelephone: %s\n", Service->ContactVoiceTelephone);
  fprintf(fp, "\tContactFacsimileTelephone: %s\n", Service->ContactFacsimileTelephone);
  fprintf(fp, "\tContactElectronicMailAddress: %s\n", Service->ContactElectronicMailAddress);
  fprintf(fp, "\tMaxWidth: %i\n", Service->MaxWidth);
  fprintf(fp, "\tMaxHeight: %i\n", Service->MaxHeight);
  fprintf(fp, "\tLogo: %s\n", Service->Logo);

  l=Service->layer_list;
  while (l!=NULL)
  {
    fprintf(fp, "Layer\n");
    fprintf(fp, "\tname: %s\n", l->name);
    fprintf(fp, "\ttitle: %s\n", l->title);

    ls=l->layer_srs_list;
    while (ls!=NULL)
    {
      fprintf(fp, "\tsrs: %s\n", ls->name);
      ls=(layer_srs *)ls->next;
    }

    r=l->raster_list;
    while (r!=NULL)
    {
      fprintf(fp, "\traster: minresx=%f minresy=%f maxresx=%f maxresy=%f\n",
              r->minresx, r->minresy, r->maxresx, r->maxresy);

      g=r->geoquadtree;

      fprintf(fp, "\t        path=%s levels=%i resx=%f resy=%f\n",
              g->path, g->levels, g->resx, g->resy);

      fprintf(fp, "\t        tilesizex=%i tilesizey=%i bounding_box=%i\n",
              g->tilesizex, g->tilesizey, g->bounding_box);

      fprintf(fp, "\t        minx=%f miny=%f maxx=%f maxy=%f\n",
              g->minx, g->miny, g->maxx, g->maxy);

      r=(raster *)(r->next);
    }

    printf("\n");

    l=(layer *)(l->next);
  }

  fclose(fp);

  return 0;
}

/******************************************************************************/
