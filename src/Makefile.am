AM_CPPFLAGS =			     \
	@FUSILLI_CFLAGS@		     \
	@GL_CFLAGS@		     \
	-I$(top_srcdir)/include	     \
	-I$(top_builddir)/include    \
	-DPLUGINDIR=\"$(plugindir)\" \
	-DIMAGEDIR=\"$(imagedir)\"   \
	-DMETADATADIR=\"$(metadatadir)\"

bin_PROGRAMS = fusilli

fusilli_LDADD = @FUSILLI_LIBS@ @GL_LIBS@ -lm -ldl
fusilli_LDFLAGS = -export-dynamic -pthread
fusilli_SOURCES =   \
	main.c	   \
	privates.c \
	object.c   \
	core.c	   \
	texture.c  \
	display.c  \
	screen.c   \
	window.c   \
	event.c    \
	paint.c	   \
	option.c   \
	plugin.c   \
	session.c  \
	fragment.c \
	matrix.c   \
	cursor.c   \
	match.c    \
	banana.c

