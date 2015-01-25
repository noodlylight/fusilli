AM_CPPFLAGS =                     \
	@FUSILLI_CFLAGS@              \
	@GL_CFLAGS@                   \
	-I$(top_srcdir)/include       \
	-I$(top_builddir)/include     \
	-DPLUGINDIR=\"$(plugindir)\"  \
	-DIMAGEDIR=\"$(imagedir)\"    \
	-DMETADATADIR=\"$(metadatadir)\"

bin_PROGRAMS = fusilli

fusilli_LDADD = @FUSILLI_LIBS@ @LIBPNG_LIBS@ @GL_LIBS@ -lm -ldl -ljpeg
fusilli_LDFLAGS = -export-dynamic -pthread
fusilli_SOURCES =   \
	main.c     \
	privates.c \
	png.c      \
	jpeg.c     \
	core.c     \
	texture.c  \
	display.c  \
	screen.c   \
	window.c   \
	event.c    \
	paint.c    \
	option.c   \
	plugin.c   \
	session.c  \
	fragment.c \
	matrix.c   \
	mousepoll.c \
	cursor.c   \
	match.c    \
	banana.c   \
	text.c

