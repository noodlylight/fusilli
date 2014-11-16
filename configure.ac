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
AM_SILENT_RULES([yes])
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

imagedir=$datadir/fusilli
AC_SUBST(imagedir)

metadatadir=$datadir/fusilli
AC_SUBST(metadatadir)

FUSILLI_REQUIRES="x11        \
                  xcomposite \
                  xext       \
                  xfixes     \
                  xdamage    \
                  xrandr     \
                  xinerama   \
                  ice        \
                  sm         \
                  libxml-2.0 \
                  libxslt    \
                  libstartup-notification-1.0 >= 0.7 \
                  cairo-xlib-xrender \
                  pangocairo \
                  cairo >= 1.0"

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
  AC_CHECK_HEADERS([sys/ioctl.h], [use_inotify=yes], [use_inotify=no])
fi

AM_CONDITIONAL(USE_INOTIFY, test "x$use_inotify" = "xyes")
if test "$use_inotify" = yes; then
  AC_DEFINE(USE_INOTIFY, 1, [Build with inotify])
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

AC_ARG_ENABLE(marco,
  [  --disable-marco      Disable marco theme support],
  [use_marco=$enableval], [use_marco=yes])

if test "x$use_gtk" = "xyes"; then
  PKG_CHECK_MODULES(GTK_WINDOW_DECORATOR,
                    xrender >= 0.8.4        \
                    gtk+-2.0 >= 2.8.0       \
                    libwnck-1.0 >= 2.19.4   \
                    libxml-2.0              \
                    pangocairo,
                    [use_gtk=yes], [use_gtk=no])
  if test "x$use_gtk" = "xyes"; then
    if test "x$use_marco" = "xyes"; then
      PKG_CHECK_MODULES(MARCO, 
          libmarco-private >= 1.6.0 \
          gio-2.0,
			[use_marco=yes], [use_marco=no])
    fi
    if test "x$use_marco" = "xyes"; then
      AC_DEFINE(USE_GSETTINGS, 1, [Enable GSettings])
    fi
  else
    use_marco="no"
  fi
else
  use_marco="no"
fi

LIBS="$LIBS -lm -ldl"

AM_CONDITIONAL(USE_GTK, test "x$use_gtk" = "xyes")
if test "$use_gtk" = yes; then
  AC_DEFINE(USE_GTK, 1, [Build fusilli-decorator-gtk])
fi

AM_CONDITIONAL(USE_MARCO, test "x$use_marco" = "xyes")
if test "$use_marco" = yes; then
  AC_DEFINE(USE_MARCO, 1, [Build marco theme support])
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
kde/Makefile
kde/window-decorator-kde4/Makefile
po/Makefile.in
metadata/Makefile
fsm/Makefile
])

echo ""
echo "the following optional plugins will be compiled:"
echo "  glib:     $use_glib"
echo "  svg:      $use_librsvg"
echo ""
echo "and the following optional features will be compiled:"
echo "  inotify:                 $use_inotify"
echo "  fusilli-decorator-gtk:   $use_gtk"
echo "       marco themes:       $use_marco"
echo "  kde4-window-decorator:   $use_kde4"
echo "  fsm:                     $use_fsm"
echo ""
