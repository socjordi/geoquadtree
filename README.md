Project developed in 2006-10-29 (not maintained since then)

# geoquadtree
GeoQuadTree is an open format for storing georeferenced images.

What is GeoQuadTree ?

	GeoQuadTree is an open format for storing georeferenced images.
	It works on image tiles. The source image is partitioned into squared non-overlapping blocks in a process called tiling. Each of these tiles is compressed independently in PNG format, and organised in a hierarchical structure of folders.
	
What are the benefits of GeoQuadTree ?

	Scalability
	It allows any extent, any resolution, only limited by storage capacity. Multiple sub-images can be stored inside the same GeoQuadTree (for instance, we could store different versions of maps in the same GeoQuadTree).
	
	Simplicity.
	It's very easy to import any georreferenced image into the GeoQuadTree format, using the gqt command-line utility. Any image format supported by GDAL can be imported. This utility can also be used to export a part of a GeoQuadTree into any format supported by GDAL.
	
	High quality
	The images are stored in PNG format, i.e. lossless compression. The PNG format allows an
	alpha channel, this can be used to represent non-data pixels.

	Efficiency
	The compression used in PNG format requires low CPU usage. Other image formats can achieve higher compression factors, but at the expense of lossy compression, and higher CPU usage. Higher CPU usages implies higher response times, and lower performance on the same hardware.
	
	Multiple access
	This format can be accessed in different ways: by means of command line utilities, the GeoQuadTree Viewer, the GeoQuadTree WMS Server, or any software that can read GDAL sources.
	
	Freedom
	The GPL license gives the user freedom to use, copy, study, modify and redistribute without restriction.
	
	OGC WMS
	An OGC WMS service is available, featuring changes of Spatial Reference System (SRS) using the PROJ.4 library.
	
	GDAL driver
	A GDAL driver is available, so any software that can read from (or write to) GDAL sources can use images in this format.

What is a QuadTree?

	The QuadTree is a tree data structure in which each internal node has up to four children.
	QuadTrees are most often used to partition a two-dimensional space by recursively subdividing it into four equal quadrants.
	A GeoQuadTree image is stored on disk using a QuadTree, in which a node is a tile image. The tile image is stored as a PNG image file, and the children nodes are stored as subfolders. The four quadrants are named “1” (top left), “2” (top right), “3” (bottom right) and “4” (bottom left).
