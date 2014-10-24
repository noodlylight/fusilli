SUBDIRS = include src libfusillidecoration plugins images gtk kde po metadata fsm

desktopfilesdir	      = $(DESTDIR)$(datadir)/applications
desktopfiles_in_files = fusilli.desktop.in
desktopfiles_files    = $(desktopfiles_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

install-data-local:
	if mkdir -p $(desktopfilesdir) && test -w $(desktopfilesdir); then ( \
		$(INSTALL) -m 644 $(desktopfiles_files) \
		$(desktopfilesdir) \
		); \
	fi

uninstall-local:
	if test -w $(desktopfilesdir); then ( \
		rm -f $(desktopfilesdir)/fusilli.desktop \
		); \
	fi

noinst_DATA =             \
	$(desktopfiles_files)

EXTRA_DIST =             \
	COPYING              \
	COPYING.GPL          \
	COPYING.LGPL         \
	COPYING.MIT          \
	fusilli.pc.in        \
	fusilli-cube.pc.in   \
	fusilli-scale.pc.in  \
	$(desktopfiles_in_files)

DISTCLEANFILES =             \
	po/stamp-it              \
	po/.intltool-merge-cache \
	$(noinst_DATA)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA =    \
	fusilli.pc      \
	fusilli-cube.pc \
	fusilli-scale.pc

