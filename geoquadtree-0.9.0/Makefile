CC=gcc

CFLAGS=`xml2-config --cflags` `Wand-config --cflags --cppflags`
LIBS=`xml2-config --libs` `Wand-config --ldflags --libs` -lfcgi -lproj -ljpeg -lpng -lgeotiff -lgdal 

SRCS=geoquadtree.c fcgi.c png.c jpg.c tiff.c xml.c proj.c resample.c logo.c
SRCH=geoquadtree.h fcgi.h png.h jpg.h tiff.h xml.h proj.h resample.h logo.h
OBJS=geoquadtree.o fcgi.o png.o jpg.o tiff.o xml.o proj.o resample.o logo.o

all: gqt wms/wms.fcgi

gqt: gqt.c $(SRCS) $(SRCH)
	$(CC) gqt.c $(CFLAGS) $(LIBS) -Wall -o $@ $(SRCS)

wms/wms.fcgi: wms.c $(SRCS) $(SRCH)
	$(CC) wms.c $(CFLAGS) $(LIBS) -Wall -o $@ $(SRCS)

clean:
	rm -f *.o gqt wms/wms.fcgi
