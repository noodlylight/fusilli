lib_LTLIBRARIES = libfusillidecoration.la

libfusillidecoration_la_LIBADD  = @DECORATION_LIBS@ -lm
libfusillidecoration_la_LDFLAGS = -no-undefined
libfusillidecoration_la_SOURCES = decoration.c

AM_CPPFLAGS = @DECORATION_CFLAGS@ -I$(top_srcdir)/include

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libfusillidecoration.pc

EXTRA_DIST = libfusillidecoration.pc.in
