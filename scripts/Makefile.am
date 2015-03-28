if USE_MARCO
APPS = fusilli-mate-gtk
	
appsdir = $(bindir)
apps_SCRIPTS = $(APPS)

desktopfilesdir       = $(DESTDIR)$(datadir)/applications
desktopfiles_in_files = fusilli-mate-gtk.desktop.in
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
		rm -f $(desktopfilesdir)/fusilli-mate-gtk.desktop \
		); \
	fi
endif

noinst_DATA =             \
	$(desktopfiles_files)

EXTRA_DIST =             \
	fusilli-mate-gtk          \
	fusilli-mate-gtk.desktop.in \
	$(desktopfiles_in_files)

