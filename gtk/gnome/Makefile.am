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

if USE_GNOME_KEYBINDINGS

xmldir       = $(DESTDIR)$(keybindingsdir)
xml_in_files = 50-fusilli-desktop-key.xml.in 50-fusilli-key.xml.in
xml_files    = $(xml_in_files:.xml.in=.xml)

@INTLTOOL_XML_NOMERGE_RULE@

endif

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

if USE_GNOME_KEYBINDINGS
	if mkdir -p $(xmldir) && test -w $(xmldir); then ( \
		$(INSTALL) -m 644 $(xml_files) \
		$(xmldir) \
		); \
	fi
endif

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
if USE_GNOME_KEYBINDINGS
	if test -w $(xmldir); then ( \
		rm -f $(xmldir)/50-fusilli*key.xml \
		); \
	fi
endif
endif

AM_CPPFLAGS = @GNOME_WINDOW_SETTINGS_CFLAGS@			   \
	-DMETACITY_THEME_DIR=\""$(windowsettingsdatadir)/themes"\" \
	-DLOCALEDIR="\"@datadir@/locale"\"

noinst_LTLIBRARIES = \
	$(libfusilli_module)

noinst_DATA =		      \
	$(desktopfiles_files) \
	$(wmproperties_files) \
	$(xml_files)

EXTRA_DIST =			 \
	$(desktopfiles_in_files) \
	$(wmproperties_in_files) \
	$(xml_in_files)

DISTCLEANFILES = \
	$(noinst_DATA)
