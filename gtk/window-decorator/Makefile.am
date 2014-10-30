if USE_GTK

if USE_MARCO
marcolibs = @MARCO_LIBS@
endif

fusilli_decorator_gtk_DEPENDENCIES = \
	$(top_builddir)/libfusillidecoration/libfusillidecoration.la
fusilli_decorator_gtk_LDADD	  =		       \
	$(top_builddir)/libfusillidecoration/libfusillidecoration.la \
	@GTK_WINDOW_DECORATOR_LIBS@		       \
	$(marcolibs)

fusilli_decorator_gtk_SOURCES	  = \
	fusilli-decorator-gtk.c
fusilli_decorator_gtk_program = fusilli-decorator-gtk
endif

AM_CPPFLAGS = \
	-I$(top_srcdir)/include            \
	@GTK_WINDOW_DECORATOR_CFLAGS@      \
	@MARCO_CFLAGS@                     \
	-DALL_LINGUAS="\"@ALL_LINGUAS@\""  \
	-DLOCALEDIR="\"@datadir@/locale"\"

bin_PROGRAMS = $(fusilli_decorator_gtk_program)


