AC_PREREQ(2.57)

dnl fusilli package version number
dnl An odd micro number indicates in-progress development.
dnl An even micro number indicates a released version.
m4_define(fusilli_version_major, 0)
m4_define(fusilli_version_minor, 0)
m4_define(fusilli_version_micro, 0)

AC_INIT([fusilli],
	[fusilli_version_major().fusilli_version_minor().fusilli_version_micro()],
	[noodlylight@gmail.com])

FUSILLI_VERSION_MAJOR=fusilli_version_major()
FUSILLI_VERSION_MINOR=fusilli_version_minor()
FUSILLI_VERSION_MICRO=fusilli_version_micro()
AC_SUBST(FUSILLI_VERSION_MAJOR)
AC_SUBST(FUSILLI_VERSION_MINOR)
AC_SUBST(FUSILLI_VERSION_MICRO)

AM_INIT_AUTOMAKE([1.7 foreign])
AC_CONFIG_HEADER([config.h])
AC_PROG_INTLTOOL([0.23])
AM_MAINTAINER_MODE

dnl decorator interface version
AC_DEFINE(DECOR_INTERFACE_VERSION, 20080529, [Decorator interface version])

AC_ISC_POSIX
AC_PROG_CC
AC_PROG_CPP
AC_PROG_CXX
AC_PROG_LIBTOOL
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h sys/time.h unistd.h])

ALL_LINGUAS="af ar bg bn bn_IN bs ca cs cy da de el en_GB en_US es eu et fi fr gl gu he hi hr hu id it ja ka km ko lo lt mk mr nb nl or pa pl pt pt_BR ro ru sk sl sr sv ta tr uk vi xh zh_CN zh_TW zu"
AC_SUBST(ALL_LINGUAS)
AM_GLIB_GNU_GETTEXT
GETTEXT_PACKAGE=fusilli
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])
AC_SUBST(GETTEXT_PACKAGE)

if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *[[\ \	]]-Wall[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wpointer-arith[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wpointer-arith" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wstrict-prototypes[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wstrict-prototypes" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wmissing-prototypes[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wmissing-prototypes" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wmissing-declarations[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wmissing-declarations" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-Wnested-externs[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -Wnested-externs" ;;
  esac

  case " $CFLAGS " in
  *[[\ \	]]-D_FORTIFY_SOURCE=2[[\ \	]]*) ;;
  *) CFLAGS="$CFLAGS -D_FORTIFY_SOURCE=2" ;;
  esac

  if test "x$enable_ansi" = "xyes"; then
    case " $CFLAGS " in
    *[[\ \	]]-ansi[[\ \	]]*) ;;
    *) CFLAGS="$CFLAGS -ansi" ;;
    esac

    case " $CFLAGS " in
    *[[\ \	]]-pedantic[[\ \	]]*) ;;
    *) CFLAGS="$CFLAGS -pedantic" ;;
    esac
  fi
fi

if test "x$GCC" = "xyes"; then
  case " $CXXFLAGS " in
  *[[\ \	]]-Wall[[\ \	]]*) ;;
  *) CXXFLAGS="$CXXFLAGS -Wall" ;;
  esac

  case " $CXXFLAGS " in
  *[[\ \	]]-D_FORTIFY_SOURCE=2[[\ \	]]*) ;;
  *) CXXFLAGS="$CXXFLAGS -D_FORTIFY_SOURCE=2" ;;
  esac
fi

AC_C_BIGENDIAN

plugindir=$libdir/fusilli
AC_SUBST(plugindir)

AC_ARG_WITH(default-plugins, [  --with-default-plugins=PLUGINS ],
		       [default_plugins="$withval"],
		       [default_plugins=""])
AC_SUBST(default_plugins)

imagedir=$datadir/fusilli
AC_SUBST(imagedir)

metadatadir=$datadir/fusilli
AC_SUBST(metadatadir)

FUSILLI_REQUIRES="x11        \
		 xcomposite \
		 xext       \
		 xfixes	    \
		 xdamage    \
		 xrandr	    \
		 xinerama   \
		 ice	    \
		 sm	    \
		 libxml-2.0 \
		 libxslt    \
		 libstartup-notification-1.0 >= 0.7"

PKG_CHECK_MODULES(FUSILLI, $FUSILLI_REQUIRES)
AC_SUBST(FUSILLI_REQUIRES)

DECORATION_REQUIRES="xrender"

PKG_CHECK_MODULES(DECORATION, $DECORATION_REQUIRES)
AC_SUBST(DECORATION_REQUIRES)

PKG_CHECK_EXISTS(xrender >= 0.9.3,
		 [have_xrender_0_9_3=yes], [have_xrender_0_9_3=no])

if test "$have_xrender_0_9_3" = yes; then
  AC_DEFINE(HAVE_XRENDER_0_9_3, 1,
	    [Define to 1 if xrender version >= 0.9.3])
fi

AC_MSG_CHECKING(for GL_CFLAGS)
AC_ARG_WITH(gl-cflags, [  --with-gl-cflags=CFLAGS ],
		       [GL_CFLAGS="$withval"],
		       [GL_CFLAGS=""])

AC_MSG_RESULT($GL_CFLAGS)
AC_MSG_CHECKING(for GL_LIBS)
AC_ARG_WITH(gl-libs, [  --with-gl-libs=LIBS ],
		     [GL_LIBS="$withval"],
		     [GL_LIBS="-lGL"])
AC_MSG_RESULT($GL_LIBS)

AC_SUBST(GL_CFLAGS)
AC_SUBST(GL_LIBS)

AC_CHECK_PROGS(XSLTPROC, xsltproc)
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([Couldn't find xsltproc])
fi

PKG_CHECK_MODULES(LIBPNG, libpng)

AC_ARG_ENABLE(glib,
  [  --disable-glib          Disable glib plugin],
  [use_glib=$enableval], [use_glib=yes])

if test "x$use_glib" = "xyes"; then
  PKG_CHECK_MODULES(GLIB, glib-2.0)
fi

AM_CONDITIONAL(USE_GLIB, test "x$use_glib" = "xyes")
if test "$use_glib" = yes; then
  AC_DEFINE(USE_GLIB, 1, [Build glib plugin])
fi

AC_ARG_ENABLE(inotify,
  [  --disable-inotify       Disable inotify support],
  [use_inotify=$enableval], [use_inotify=yes])

if test "x$use_inotify" = "xyes"; then
  AC_CHECK_HEADERS([sys/inotify.h], [use_inotify=yes], [use_inotify=no])
fi

AM_CONDITIONAL(USE_INOTIFY, test "x$use_inotify" = "xyes")
if test "$use_inotify" = yes; then
  AC_DEFINE(USE_INOTIFY, 1, [Build with inotify])
fi

AC_ARG_ENABLE(annotate,
  [  --disable-annotate      Disable annotate plugin],
  [use_annotate=$enableval], [use_annotate=yes])

if test "x$use_annotate" = "xyes"; then
  PKG_CHECK_MODULES(ANNOTATE, cairo-xlib-xrender, [use_annotate=yes], [use_annotate=no])
fi

AM_CONDITIONAL(ANNOTATE_PLUGIN, test "x$use_annotate" = "xyes")
if test "$use_annotate" = yes; then
  AC_DEFINE(USE_ANNOTATE, 1, [Build annotate plugin])
fi

AC_ARG_ENABLE(librsvg,
  [  --disable-librsvg       Disable svg support],
  [use_librsvg=$enableval], [use_librsvg=yes])

if test "x$use_librsvg" = "xyes"; then
  PKG_CHECK_MODULES(LIBRSVG, [cairo >= 1.0 librsvg-2.0 >= 2.14.0], [use_librsvg=yes], [use_librsvg=no])
fi

AM_CONDITIONAL(USE_LIBRSVG, test "x$use_librsvg" = "xyes")
if test "$use_librsvg" = yes; then
  AC_DEFINE(USE_LIBRSVG, 1, [librsvg for SVG support])
fi

AC_ARG_ENABLE(gtk,
  [  --disable-gtk           Disable gtk window decorator],
  [use_gtk=$enableval], [use_gtk=yes])

AC_ARG_ENABLE(metacity,
  [  --disable-metacity      Disable metacity theme support],
  [use_metacity=$enableval], [use_metacity=yes])

AC_ARG_ENABLE(gnome,
  [  --disable-gnome         Disable gnome settings module],
  [use_gnome=$enableval], [use_gnome=yes])

AC_ARG_ENABLE(gnome-keybindings,
  [  --disable-gnome-keybindings Disable gnome keybindings],
  [use_gnome_keybindings=$enableval], [use_gnome_keybindings=yes])

if test "x$use_gtk" = "xyes"; then
  PKG_CHECK_MODULES(GTK_WINDOW_DECORATOR,
		    xrender >= 0.8.4  \
		    gtk+-2.0 >= 2.8.0 \
		    libwnck-1.0	      \
		    pangocairo,
		    [use_gtk=yes], [use_gtk=no])
  if test "x$use_gtk" = "xyes"; then
    save_CFLAGS="$CFLAGS"
    save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $GTK_WINDOW_DECORATOR_CFLAGS"
    LIBS="$LIBS $GTK_WINDOW_DECORATOR_LIBS"
    AC_CHECK_FUNCS(wnck_window_has_name)
    CFLAGS="$save_CFLAGS"
    LIBS="$save_LIBS -lm -ldl"

    PKG_CHECK_EXISTS(libwnck-1.0 >= 2.18.1,
		     [have_libwnck_2_18_1=yes], [have_libwnck_2_18_1=no])

	PKG_CHECK_EXISTS(libwnck-1.0 >= 2.19.4,
		     [have_libwnck_2_19_4=yes], [have_libwnck_2_19_4=no])

    if test "x$use_metacity" = "xyes"; then
      PKG_CHECK_MODULES(METACITY, libmetacity-private,
			[use_metacity=yes], [use_metacity=no])
      PKG_CHECK_EXISTS(libmetacity-private >= 2.15.21,
		       [have_metacity_2_15_21=yes], [have_metacity_2_15_21=no])
      PKG_CHECK_EXISTS(libmetacity-private >= 2.17.0,
		       [have_metacity_2_17_0=yes], [have_metacity_2_17_0=no])
      PKG_CHECK_EXISTS(libmetacity-private >= 2.23.2,
		       [have_metacity_2_23_2=yes], [have_metacity_2_23_2=no])
    fi

    if test "x$use_gnome" = "xyes"; then
      PKG_CHECK_MODULES(GNOME_WINDOW_SETTINGS,
			gnome-window-settings-2.0 gnome-desktop-2.0 gconf-2.0,
			[use_gnome=yes], [use_gnome=no])
    fi

    if test "x$use_gnome_keybindings" = "xyes"; then
      PKG_CHECK_MODULES(GNOME_KEY_BINDINGS,
			gnome-keybindings,
			[use_gnome_keybindings=yes], [use_gnome_keybindings=no])
    fi

    windowsettingsdatadir=`pkg-config --variable=prefix gnome-window-settings-2.0`/share
    windowsettingslibdir=`pkg-config --variable=libdir gnome-window-settings-2.0`    keybindingsdir=`pkg-config --variable=keysdir gnome-keybindings`
  else
    use_metacity="no"
    use_gnome="no"
  fi
else
  use_metacity="no"
  use_gnome="no"
fi

AC_SUBST(windowsettingsdatadir)
AC_SUBST(windowsettingslibdir)
AC_SUBST(keybindingsdir)

AM_CONDITIONAL(USE_GTK, test "x$use_gtk" = "xyes")
if test "$use_gtk" = yes; then
  AC_DEFINE(USE_GTK, 1, [Build gtk window decorator])
  if test "$have_libwnck_2_18_1" = yes; then
    AC_DEFINE(HAVE_LIBWNCK_2_18_1, 1,
	      [Define to 1 if libwnck version >= 2_18_1])
  fi
  if test "$have_libwnck_2_19_4" = yes; then
    AC_DEFINE(HAVE_LIBWNCK_2_19_4, 1,
	      [Define to 1 if libwnck version >= 2_19_4])
  fi
fi

AM_CONDITIONAL(USE_METACITY, test "x$use_metacity" = "xyes")
if test "$use_metacity" = yes; then
  AC_DEFINE(USE_METACITY, 1, [Build metacity theme support])
  if test "$have_metacity_2_15_21" = yes; then
    AC_DEFINE(HAVE_METACITY_2_15_21, 1,
	      [Define to 1 if metacity version >= 2.15.21])
  fi
  if test "$have_metacity_2_17_0" = yes; then
    AC_DEFINE(HAVE_METACITY_2_17_0, 1,
	      [Define to 1 if metacity version >= 2.17.0])
  fi
  if test "$have_metacity_2_23_2" = yes; then
    AC_DEFINE(HAVE_METACITY_2_23_2, 1,
	      [Define to 1 if metacity version >= 2.23.2])
  fi
fi

AM_CONDITIONAL(USE_GNOME, test "x$use_gnome" = "xyes")
if test "$use_gnome" = yes; then
  AC_DEFINE(USE_GNOME, 1, [Build gnome settings module])
fi

AM_CONDITIONAL(USE_GNOME_KEYBINDINGS, test "x$use_gnome_keybindings" = "xyes")
if test "$use_gnome_keybindings" = yes; then
  AC_DEFINE(USE_GNOME_KEYBINDINGS, 1, [Install custom keybindings])
fi

AC_ARG_ENABLE(kde4,
  [  --disable-kde4          Disable kde4 window decorator],
  [use_kde4=$enableval], [use_kde4=yes])

if test "x$use_kde4" = "xyes"; then
  
  kde4dir=`kde4-config --prefix --expandvars 2>/dev/null`
  kde4libs=`kde4-config --install lib --expandvars 2>/dev/null`
  kde4incs=`kde4-config --install include --expandvars 2>/dev/null`

  kde4_test_includes="kdecoration.h kcommondecoration.h plasma/framesvg.h"

  dnl Check for KWin headers
  AC_MSG_CHECKING([for KWin headers])

  kde4_notfound=""
  for file in $kde4_test_includes ; do
    status=" $file"
    for it in $kde4incs ; do
      if test -r "$it/$file" ; then
        status=""
        break;
      fi
    done
    kde4_notfound="$kde4_notfound$status"
  done
  if test -z "$kde4_notfound" ; then
      kde4_notfound="yes"
  else
      kde4_notfound="$kde4_notfound not found"
      kde4libs=""
  fi

  AC_MSG_RESULT([$kde4_notfound])

  if test -d "$kde4libs"; then
    PKG_CHECK_MODULES(KDE4_WINDOW_DECORATOR,
		      dbus-1 xdamage xext xcomposite QtCore >= 4.5.0 QtGui QtDBus,
			[use_kde4=yes], [use_kde4=no])

    KDE4_CFLAGS="-I$kde4incs"
    KDE4_LIBS="-L$kde4libs -lkdecore -lkdecorations -lkdeui -lplasma"

    QT4_MOC=`pkg-config QtGui --variable=moc_location`
    QDBUSXML2CPP=`pkg-config QtGui --variable=exec_prefix`"/bin/qdbusxml2cpp"
  else
    use_kde4=no;
  fi
fi

AM_CONDITIONAL(USE_KDE4, test "x$use_kde4" = "xyes")
if test "$use_kde4" = yes; then
  AC_DEFINE(USE_KDE4, 1, [Build kde4 window decorator])
fi

AC_SUBST(KDE4_CFLAGS)
AC_SUBST(KDE4_LIBS)
AC_SUBST(QT4_MOC)
AC_SUBST(QDBUSXML2CPP)

AC_ARG_ENABLE(fsm,
  [  --disable-fsm      Disable fsm],
  [use_fsm=$enableval], [use_fsm=yes])

if test "x$use_fsm" = "xyes"; then
  dnl exclude python3 versions
  m4_define_default([_AM_PYTHON_INTERPRETER_LIST],[python2 python2.7 python2.6])
  AM_PATH_PYTHON([2.7], [use_fsm=yes], [use_fsm=no])
fi

AM_CONDITIONAL(USE_FSM, test "x$use_fsm" = "xyes")
if test "$use_fsm" = yes; then
  AC_DEFINE(USE_FSM, 1, [Build fsm])
fi

AC_ARG_WITH(max-desktops,
  [  --with-max-desktops     Max reasonable desktops],
  [MAX_DESKTOPS=$withval],
  [MAX_DESKTOPS=36])

if test -n "$MAX_DESKTOPS"; then
  AC_DEFINE_UNQUOTED(MAX_DESKTOPS, $MAX_DESKTOPS, [Max reasonable desktops])
fi

AC_OUTPUT([
fusilli.pc
fusilli-cube.pc
fusilli-scale.pc
Makefile
src/Makefile
libfusillidecoration/Makefile
libfusillidecoration/libfusillidecoration.pc
include/Makefile
plugins/Makefile
images/Makefile
gtk/Makefile
gtk/window-decorator/Makefile
gtk/gnome/Makefile
kde/Makefile
kde/window-decorator-kde4/Makefile
po/Makefile.in
metadata/Makefile
fsm/Makefile
])

echo ""
echo "the following optional plugins will be compiled:"
echo "  glib:     $use_glib"
echo "  annotate: $use_annotate"
echo "  svg:      $use_librsvg"
echo ""
echo "and the following optional features will be compiled:"
echo "  inotify:  $use_inotify"
echo "  gtk:      $use_gtk"
echo "  metacity: $use_metacity"
echo "  gnome:    $use_gnome"
echo "  kde4:     $use_kde4"
echo "  fsm:      $use_fsm"
echo ""
