libfade_la_LDFLAGS = -module -avoid-version -no-undefined
libfade_la_SOURCES = fade.c

libcube_la_LDFLAGS = -module -avoid-version -no-undefined
libcube_la_SOURCES = cube.c

librotate_la_LDFLAGS = -module -avoid-version -no-undefined
librotate_la_SOURCES = rotate.c

libzoom_la_LDFLAGS = -module -avoid-version -no-undefined
libzoom_la_SOURCES = zoom.c

libscale_la_LDFLAGS = -module -avoid-version -no-undefined
libscale_la_SOURCES = scale.c

libwobbly_la_LDFLAGS = -module -avoid-version -no-undefined
libwobbly_la_SOURCES = wobbly.c

libminimize_la_LDFLAGS = -module -avoid-version -no-undefined
libminimize_la_SOURCES = minimize.c

libmove_la_LDFLAGS = -module -avoid-version -no-undefined
libmove_la_SOURCES = move.c

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

if USE_LIBRSVG
libsvg_la_DEPENDENCIES = $(top_builddir)/libfusillidecoration/libfusillidecoration.la
libsvg_la_LDFLAGS = -module -avoid-version -no-undefined
libsvg_la_LIBADD =				       \
	$(top_builddir)/libfusillidecoration/libfusillidecoration.la \
	@LIBRSVG_LIBS@
libsvg_la_SOURCES = svg.c
libsvg_module = libsvg.la
endif

if ANNOTATE_PLUGIN
libannotate_la_LDFLAGS = -module -avoid-version -no-undefined
libannotate_la_LIBADD = @ANNOTATE_LIBS@
libannotate_la_SOURCES = annotate.c
libannotate_module = libannotate.la
endif

if USE_GLIB
libglib_la_LDFLAGS = -module -avoid-version -no-undefined
libglib_la_LIBADD = @GLIB_LIBS@
libglib_la_SOURCES = glib.c
libglib_module = libglib.la
endif

AM_CPPFLAGS =					\
	@FUSILLI_CFLAGS@				\
	@LIBPNG_CFLAGS@				\
	@LIBRSVG_CFLAGS@			\
	@ANNOTATE_CFLAGS@			\
	@GLIB_CFLAGS@				\
	-DALL_LINGUAS="\"@ALL_LINGUAS@\""	\
	-DLOCALEDIR="\"@datadir@/locale\""	\
	-DPLUGINDIR=\"$(plugindir)\"		\
	-DIMAGEDIR=\"$(imagedir)\"		\
	-I$(top_srcdir)/include                 \
	-I$(top_builddir)/include               \
	-DMETADATADIR=\"$(metadatadir)\"

moduledir = $(plugindir)

module_LTLIBRARIES =		\
	$(libglib_module)	\
	libdecoration.la	\
	libwobbly.la		\
	libfade.la		\
	libminimize.la		\
	libcube.la		\
	librotate.la		\
	libzoom.la		\
	libscale.la		\
	libmove.la		\
	libresize.la		\
	libplace.la             \
	libswitcher.la		\
	libwater.la		\
	libscreenshot.la	\
	libclone.la		\
	libblur.la		\
	libobs.la		\
	libcommands.la		\
	libmatecompat.la	\
	$(libsvg_module)	\
	$(libannotate_module)	\
	$(libfs_module)

