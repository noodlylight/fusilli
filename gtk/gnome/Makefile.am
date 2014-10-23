moduledir = $(DESTDIR)$(windowsettingslibdir)/window-manager-settings

if USE_GNOME
libfusilli_la_LDFLAGS = -export-dynamic -avoid-version -rpath $(moduledir)
libfusilli_la_LIBADD  = @GNOME_WINDOW_SETTINGS_LIBS@
libfusilli_la_SOURCES = 		\
	fusilli-window-manager.c	\
	fusilli-window-manager.h
libfusilli_module     = libfusilli.la

desktopfilesdir	      = $(DESTDIR)$(datadir)/applications
desktopfiles_in_files = fusilli.desktop.in
desktopfiles_files    = $(desktopfiles_in_files:.desktop.in=.desktop)

wmpropertiesdir	      = $(DESTDIR)$(windowsettingsdatadir)/gnome/wm-properties
wmproperties_in_files = fusilli-wm.desktop.in
wmproperties_files    = $(wmproperties_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

install-exec-local:
	if mkdir -p $(moduledir) && test -w $(moduledir); then ( \
		$(LIBTOOL) --mode=install $(INSTALL) $(libfusilli_module) \
		$(moduledir) \
		); \
	fi

install-data-local:
	if mkdir -p $(desktopfilesdir) && test -w $(desktopfilesdir); then ( \
		$(INSTALL) -m 644 $(desktopfiles_files) \
		$(desktopfilesdir) \
		); \
	fi
	if mkdir -p $(wmpropertiesdir) && test -w $(wmpropertiesdir); then ( \
		$(INSTALL) -m 644 $(wmproperties_files) \
		$(wmpropertiesdir) \
		); \
	fi

uninstall-local:
	if test -w $(moduledir); then ( \
		rm -f $(moduledir)/libfusilli.* \
		); \
	fi
	if test -w $(desktopfilesdir); then ( \
		rm -f $(desktopfilesdir)/fusilli.desktop \
		); \
	fi
	if test -w $(wmpropertiesdir); then ( \
		rm -f $(wmpropertiesdir)/fusilli-wm.desktop \
		); \
	fi

endif

AM_CPPFLAGS = @GNOME_WINDOW_SETTINGS_CFLAGS@			   \
	-DMETACITY_THEME_DIR=\""$(windowsettingsdatadir)/themes"\" \
	-DLOCALEDIR="\"@datadir@/locale"\"

noinst_LTLIBRARIES = \
	$(libfusilli_module)

noinst_DATA =		      \
	$(desktopfiles_files) \
	$(wmproperties_files)

EXTRA_DIST =			 \
	fusilli.desktop.in \
	fusilli-wm.desktop.in

DISTCLEANFILES = \
	$(noinst_DATA)
