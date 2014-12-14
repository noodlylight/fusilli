if USE_KDE4

BUILDSOURCES =              \
	decorator.moc.cpp       \
	window.moc.cpp          \
	paintredirector.moc.cpp \
	kwinadaptor.moc.cpp     \
	kwinadaptor.cpp         \
	kwinadaptor.h

fusilli_decorator_kde4_DEPENDENCIES = \
	$(top_builddir)/libfusillidecoration/libfusillidecoration.la
fusilli_decorator_kde4_LDADD   =       \
	$(top_builddir)/libfusillidecoration/libfusillidecoration.la \
	@FUSILLI_DECORATOR_KDE4_LIBS@ @KDE4_LIBS@ -ldl

nodist_fusilli_decorator_kde4_SOURCES = \
	$(BUILDSOURCES)

dist_fusilli_decorator_kde4_SOURCES = \
	main.cpp                \
	utils.cpp               \
	utils.h                 \
	decorator.cpp           \
	decorator.h             \
	window.cpp              \
	window.h                \
	options.cpp             \
	options.h               \
	kdecoration_plugins.cpp \
	kdecoration_plugins.h   \
	switcher.cpp            \
	switcher.h              \
	paintredirector.cpp     \
	paintredirector.h       \
	org.kde.KWin.xml

fusilli_decorator_kde4_program = fusilli-decorator-kde4

AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include @FUSILLI_DECORATOR_KDE4_CFLAGS@ @KDE4_CFLAGS@

bin_PROGRAMS = $(fusilli_decorator_kde4_program)

%.moc.cpp: %.h
	$(QT4_MOC) $< > $@

kwinadaptor.h: kwinadaptor.cpp

kwinadaptor.cpp: org.kde.KWin.xml
	$(QDBUSXML2CPP) -a kwinadaptor -i decorator.h -l KWD::Decorator $<

# make sure kwinadaptor.h is built before any of the dist_SOURCES uses it
main.cpp: kwinadaptor.h

CLEANFILES = $(BUILDSOURCES)
EXTRA_DIST = org.kde.KWin.xml

endif
