if USE_FSM

fsm: $(srcdir)/fsm.in Makefile
	sed -e 's,@version@,$(PACKAGE_VERSION),g' \
	    -e 's,@installprefix@,$(prefix),g' \
	    -e 's,@path_to_python_interpreter@,$(PYTHON),g' \
	    <$(srcdir)/fsm.in >fsm
	chmod +x fsm

desktopfilesdir = $(DESTDIR)$(datadir)/applications
desktopfiles_in_files = fsm.desktop.in
desktopfiles_files = $(desktopfiles_in_files:.desktop.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

install-data-local:
	if mkdir -p $(desktopfilesdir) && test -w $(desktopfilesdir); then ( \
		$(INSTALL) -m 644 $(desktopfiles_files) \
		$(desktopfilesdir) \
		); \
	fi

uninstall-local:
	if test -w $(desktopfilesdir); then ( \
		rm -f $(desktopfilesdir)/fsm.desktop \
		); \
	fi

noinst_DATA =             \
	$(desktopfiles_files)

bin_SCRIPTS = fsm
DISTCLEANFILES = $(bin_SCRIPTS) $(noinst_DATA)
EXTRA_DIST = $(srcdir)/fsm.in $(desktopfiles_in_files)

endif
