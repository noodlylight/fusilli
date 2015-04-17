xmldir       = $(metadatadir)
xml_in_files =         \
	3d.xml.in          \
	core.xml.in        \
	addhelper.xml.in   \
	annotate.xml.in    \
	blur.xml.in        \
	clone.xml.in       \
	commands.xml.in    \
	colorfilter.xml.in \
	cube.xml.in        \
	expo.xml.in        \
	wall.xml.in        \
	decoration.xml.in  \
	ezoom.xml.in       \
	fade.xml.in        \
	firepaint.xml.in   \
	mag.xml.in         \
	matecompat.xml.in  \
	minimize.xml.in    \
	move.xml.in        \
	neg.xml.in         \
	titleinfo.xml.in   \
	thumbnail.xml.in   \
	trailfocus.xml.in  \
	obs.xml.in         \
	opacify.xml.in     \
	place.xml.in       \
	resize.xml.in      \
	rotate.xml.in      \
	ring.xml.in        \
	shift.xml.in       \
	grid.xml.in        \
	scale.xml.in       \
	screenshot.xml.in  \
	splash.xml.in      \
	snow.xml.in        \
	showmouse.xml.in   \
	svg.xml.in         \
	switcher.xml.in    \
	staticswitcher.xml.in \
	wallpaper.xml.in   \
	water.xml.in       \
	winrules.xml.in    \
	wobbly.xml.in      \
	workarounds.xml.in \
	wsnames.xml.in

xml_files    = $(xml_in_files:.xml.in=.xml)
xml_DATA     = $(xml_files)

@INTLTOOL_XML_RULE@

EXTRA_DIST =            \
	$(xml_in_files)

DISTCLEANFILES =        \
	$(xml_files)
