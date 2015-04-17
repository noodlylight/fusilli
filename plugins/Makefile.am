libcolorfilter_la_LDFLAGS = -module -avoid-version -no-undefined
libcolorfilter_la_SOURCES = colorfilter.c colorfilter-parser.c colorfilter-parser.h

lib3d_la_LDFLAGS = -module -avoid-version -no-undefined
lib3d_la_SOURCES = 3d.c

libaddhelper_la_LDFLAGS = -module -avoid-version -no-undefined
libaddhelper_la_SOURCES = addhelper.c

libannotate_la_LDFLAGS = -module -avoid-version -no-undefined
libannotate_la_SOURCES = annotate.c

libezoom_la_LDFLAGS = -module -avoid-version -no-undefined
libezoom_la_SOURCES = ezoom.c

libexpo_la_LDFLAGS = -module -avoid-version -no-undefined
libexpo_la_SOURCES = expo.c
libexpo_la_LIBADD =  @FUSILLI_LIBS@ -lGLU

libfade_la_LDFLAGS = -module -avoid-version -no-undefined
libfade_la_SOURCES = fade.c

libfirepaint_la_LDFLAGS = -module -avoid-version -no-undefined
libfirepaint_la_SOURCES = firepaint.c firepaint_tex.h

libcube_la_LDFLAGS = -module -avoid-version -no-undefined
libcube_la_SOURCES = cube.c

libwall_la_LDFLAGS = -module -avoid-version -no-undefined
libwall_la_SOURCES = wall.c

librotate_la_LDFLAGS = -module -avoid-version -no-undefined
librotate_la_SOURCES = rotate.c

libgrid_la_LDFLAGS = -module -avoid-version -no-undefined
libgrid_la_SOURCES = grid.c

libscale_la_LDFLAGS = -module -avoid-version -no-undefined
libscale_la_SOURCES = scale.c

libsnow_la_LDFLAGS = -module -avoid-version -no-undefined
libsnow_la_SOURCES = snow.c

libsplash_la_LDFLAGS = -module -avoid-version -no-undefined
libsplash_la_SOURCES = splash.c

libshowmouse_la_LDFLAGS = -module -avoid-version -no-undefined
libshowmouse_la_SOURCES = showmouse.c showmouse_tex.h

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

libring_la_LDFLAGS = -module -avoid-version -no-undefined
libring_la_SOURCES = ring.c

libshift_la_LDFLAGS = -module -avoid-version -no-undefined
libshift_la_SOURCES = shift.c

libthumbnail_la_LDFLAGS = -module -avoid-version -no-undefined
libthumbnail_la_SOURCES = thumbnail.c thumbnail_tex.h

libtitleinfo_la_LDFLAGS = -module -avoid-version -no-undefined
libtitleinfo_la_SOURCES = titleinfo.c

libtrailfocus_la_LDFLAGS = -module -avoid-version -no-undefined
libtrailfocus_la_SOURCES = trailfocus.c

libstaticswitcher_la_LDFLAGS = -module -avoid-version -no-undefined
libstaticswitcher_la_SOURCES = staticswitcher.c

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

libopacify_la_LDFLAGS = -module -avoid-version -no-undefined
libopacify_la_SOURCES = opacify.c

libmag_la_LDFLAGS = -module -avoid-version -no-undefined
libmag_la_SOURCES = mag.c

libcommands_la_LDFLAGS = -module -avoid-version -no-undefined
libcommands_la_SOURCES = commands.c

libmatecompat_la_LDFLAGS = -module -avoid-version -no-undefined
libmatecompat_la_SOURCES = matecompat.c

libwsnames_la_LDFLAGS = -module -avoid-version -no-undefined
libwsnames_la_SOURCES = wsnames.c

libworkarounds_la_LDFLAGS = -module -avoid-version -no-undefined
libworkarounds_la_SOURCES = workarounds.c

libwinrules_la_LDFLAGS = -module -avoid-version -no-undefined
libwinrules_la_SOURCES = winrules.c

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
	-DDATADIR="\"@datadir@\""           \
	-DLOCALEDIR="\"@datadir@/locale\""  \
	-DPLUGINDIR=\"$(plugindir)\"        \
	-DIMAGEDIR=\"$(imagedir)\"          \
	-I$(top_srcdir)/include             \
	-I$(top_builddir)/include           \
	-DMETADATADIR=\"$(metadatadir)\"

moduledir = $(plugindir)

module_LTLIBRARIES =      \
	libcolorfilter.la     \
	lib3d.la              \
	libaddhelper.la       \
	libannotate.la        \
	libdecoration.la      \
	libexpo.la            \
	libezoom.la           \
	libwobbly.la          \
	libfade.la            \
	libfirepaint.la       \
	libminimize.la        \
	libcube.la            \
	libwall.la            \
	librotate.la          \
	libgrid.la            \
	libscale.la           \
	libsnow.la            \
	libsplash.la          \
	libshowmouse.la       \
	libneg.la             \
	libmove.la            \
	libmag.la             \
	libresize.la          \
	libplace.la           \
	libswitcher.la        \
	libring.la            \
	libshift.la           \
	libthumbnail.la       \
	libtitleinfo.la       \
	libtrailfocus.la      \
	libstaticswitcher.la  \
	libwallpaper.la       \
	libwinrules.la        \
	libworkarounds.la     \
	libwater.la           \
	libscreenshot.la      \
	libclone.la           \
	libblur.la            \
	libobs.la             \
	libopacify.la         \
	libcommands.la        \
	libmatecompat.la      \
	libwsnames.la         \
	$(libsvg_module)

