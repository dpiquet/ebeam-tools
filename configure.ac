AC_PREREQ([2.57])
AC_CONFIG_MACRO_DIR([m4])

AC_INIT(ebeam_tools,[0.9], [https://sourceforge.net/p/ebeam/tickets/],, [https://sourceforge.net/p/ebeam])

AM_INIT_AUTOMAKE([foreign])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_LIBTOOL
AC_PROG_INSTALL

AC_PATH_X
AC_CHECK_HEADERS([stdlib.h string.h])
AC_HEADER_STDBOOL
AC_FUNC_STRTOD

PKG_CHECK_MODULES(X11, x11 xext)
AC_SUBST(X11_CFLAGS)
AC_SUBST(X11_LIBS)

PKG_CHECK_MODULES(XINPUT, [xi >= 1.4] [inputproto >= 2.1])
AC_SUBST(XINPUT_CFLAGS)
AC_SUBST(XINPUT_LIBS)

PKG_CHECK_MODULES(GSL, gsl)
AC_SUBST(GSL_CFLAGS)
AC_SUBST(GSL_LIBS)

PKG_CHECK_MODULES(XRANDR, [xrandr], AC_DEFINE(HAVE_X11_XRANDR, 1), foo="bar")
AC_SUBST(XRANDR_CFLAGS)
AC_SUBST(XRANDR_LIBS)

AC_SUBST(VERSION)

CXXFLAGS="$CXXFLAGS -Wno-long-long"

AC_OUTPUT([Makefile
           ebeam_tools.spec
           src/Makefile
           src/gui/Makefile
           man/Makefile
           res/Makefile])
