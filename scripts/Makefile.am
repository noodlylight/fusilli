APPS = fusilli-gtk
	
appsdir = $(bindir)
apps_SCRIPTS = $(APPS)

desktopfilesdir       = $(DESTDIR)$(datadir)/applications
desktopfiles_in_files = fusilli-gtk.desktop.in
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
		rm -f $(desktopfilesdir)/fusilli-gtk.desktop \
		); \
	fi

noinst_DATA =             \
	$(desktopfiles_files)

EXTRA_DIST =             \
	fusilli-gtk          \
	$(desktopfiles_in_files)

