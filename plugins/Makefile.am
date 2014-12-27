libannotate_la_LDFLAGS = -module -avoid-version -no-undefined
libannotate_la_SOURCES = annotate.c

libfade_la_LDFLAGS = -module -avoid-version -no-undefined
libfade_la_SOURCES = fade.c

libfirepaint_la_LDFLAGS = -module -avoid-version -no-undefined
libfirepaint_la_SOURCES = firepaint.c

libcube_la_LDFLAGS = -module -avoid-version -no-undefined
libcube_la_SOURCES = cube.c

libwall_la_LDFLAGS = -module -avoid-version -no-undefined
libwall_la_SOURCES = wall.c

librotate_la_LDFLAGS = -module -avoid-version -no-undefined
librotate_la_SOURCES = rotate.c

libzoom_la_LDFLAGS = -module -avoid-version -no-undefined
libzoom_la_SOURCES = zoom.c

libscale_la_LDFLAGS = -module -avoid-version -no-undefined
libscale_la_SOURCES = scale.c

libsnow_la_LDFLAGS = -module -avoid-version -no-undefined
libsnow_la_SOURCES = snow.c

libwobbly_la_LDFLAGS = -module -avoid-version -no-undefined
libwobbly_la_SOURCES = wobbly.c

libminimize_la_LDFLAGS = -module -avoid-version -no-undefined
libminimize_la_SOURCES = minimize.c

libmove_la_LDFLAGS = -module -avoid-version -no-undefined
libmove_la_SOURCES = move.c

libneg_la_LDFLAGS = -module -avoid-version -no-undefined
libneg_la_SOURCES = neg.c

libresize_la_LDFLAGS = -module -avoid-version -no-undefined
libresize_la_SOURCES = resize.c

libplace_la_LDFLAGS = -module -avoid-version -no-undefined
libplace_la_SOURCES = place.c

libdecoration_la_DEPENDENCIES = $(top_builddir)/libfusillidecoration/libfusillidecoration.la
libdecoration_la_LDFLAGS = -module -avoid-version -no-undefined
libdecoration_la_LIBADD = $(top_builddir)/libfusillidecoration/libfusillidecoration.la
libdecoration_la_SOURCES = decoration.c

libswitcher_la_LDFLAGS = -module -avoid-version -no-undefined
libswitcher_la_SOURCES = switcher.c

libwallpaper_la_LDFLAGS = -module -avoid-version -no-undefined
libwallpaper_la_SOURCES = wallpaper.c

libwater_la_LDFLAGS = -module -avoid-version -no-undefined
libwater_la_SOURCES = water.c

libscreenshot_la_LDFLAGS = -module -avoid-version -no-undefined
libscreenshot_la_SOURCES = screenshot.c

libclone_la_LDFLAGS = -module -avoid-version -no-undefined
libclone_la_SOURCES = clone.c

libblur_la_DEPENDENCIES = $(top_builddir)/libfusillidecoration/libfusillidecoration.la
libblur_la_LDFLAGS = -module -avoid-version -no-undefined
libblur_la_LIBADD = $(top_builddir)/libfusillidecoration/libfusillidecoration.la -lGLU
libblur_la_SOURCES = blur.c

libobs_la_LDFLAGS = -module -avoid-version -no-undefined
libobs_la_SOURCES = obs.c

libcommands_la_LDFLAGS = -module -avoid-version -no-undefined
libcommands_la_SOURCES = commands.c

libmatecompat_la_LDFLAGS = -module -avoid-version -no-undefined
libmatecompat_la_SOURCES = matecompat.c

libwsnames_la_LDFLAGS = -module -avoid-version -no-undefined
libwsnames_la_SOURCES = wsnames.c

if USE_LIBRSVG
libsvg_la_DEPENDENCIES = $(top_builddir)/libfusillidecoration/libfusillidecoration.la
libsvg_la_LDFLAGS = -module -avoid-version -no-undefined
libsvg_la_LIBADD =                                               \
	$(top_builddir)/libfusillidecoration/libfusillidecoration.la \
	@LIBRSVG_LIBS@
libsvg_la_SOURCES = svg.c
libsvg_module = libsvg.la
endif

AM_CPPFLAGS =                           \
	@FUSILLI_CFLAGS@                    \
	@LIBRSVG_CFLAGS@                    \
	-DALL_LINGUAS="\"@ALL_LINGUAS@\""   \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DPLUGINDIR=\"$(plugindir)\"        \
	-DIMAGEDIR=\"$(imagedir)\"          \
	-I$(top_srcdir)/include             \
	-I$(top_builddir)/include           \
	-DMETADATADIR=\"$(metadatadir)\"

moduledir = $(plugindir)

module_LTLIBRARIES =      \
	libannotate.la        \
	libdecoration.la      \
	libwobbly.la          \
	libfade.la            \
	libfirepaint.la       \
	libminimize.la        \
	libcube.la            \
	libwall.la            \
	librotate.la          \
	libzoom.la            \
	libscale.la           \
	libsnow.la            \
	libneg.la             \
	libmove.la            \
	libresize.la          \
	libplace.la           \
	libswitcher.la        \
	libwallpaper.la       \
	libwater.la           \
	libscreenshot.la      \
	libclone.la           \
	libblur.la            \
	libobs.la             \
	libcommands.la        \
	libmatecompat.la      \
	libwsnames.la         \
	$(libsvg_module)

