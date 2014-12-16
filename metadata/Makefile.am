xmldir       = $(metadatadir)
xml_in_files =         \
	core.xml.in        \
	annotate.xml.in    \
	blur.xml.in        \
	clone.xml.in       \
	commands.xml.in    \
	cube.xml.in        \
	wall.xml.in        \
	decoration.xml.in  \
	fade.xml.in        \
	matecompat.xml.in  \
	minimize.xml.in    \
	move.xml.in        \
	neg.xml.in         \
	obs.xml.in         \
	place.xml.in       \
	resize.xml.in      \
	rotate.xml.in      \
	scale.xml.in       \
	screenshot.xml.in  \
	svg.xml.in         \
	switcher.xml.in    \
	wallpaper.xml.in   \
	water.xml.in       \
	wobbly.xml.in      \
	wsnames.xml.in     \
	zoom.xml.in

xml_files    = $(xml_in_files:.xml.in=.xml)
xml_DATA     = $(xml_files)

@INTLTOOL_XML_RULE@

EXTRA_DIST =            \
	$(xml_in_files)

DISTCLEANFILES =        \
	$(xml_files)
