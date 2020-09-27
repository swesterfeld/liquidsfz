AC_INIT([liquidsfz], [0.2.1])
AC_CONFIG_SRCDIR([src/liquidsfz.cc])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CXX
AC_DISABLE_SHARED
LT_INIT([pic-only])

dnl Version splitted

[MAJOR_VERSION=`echo $PACKAGE_VERSION | sed 's/\([0-9]\+\)\.\([0-9]\+\)\.\([0-9]\+\)\(.*\)/\1/'`]
[MINOR_VERSION=`echo $PACKAGE_VERSION | sed 's/\([0-9]\+\)\.\([0-9]\+\)\.\([0-9]\+\)\(.*\)/\2/'`]
[MICRO_VERSION=`echo $PACKAGE_VERSION | sed 's/\([0-9]\+\)\.\([0-9]\+\)\.\([0-9]\+\)\(.*\)/\3/'`]

AC_SUBST(MAJOR_VERSION)
AC_SUBST(MINOR_VERSION)
AC_SUBST(MICRO_VERSION)

dnl construct libtool version info (see also https://www.gnu.org/software/libtool/manual/html_node/Updating-version-info.html)
dnl
dnl  - every time we change $MAJOR_VERSION or $MINOR_VERSION, ABI can be totally different
dnl  - every time we change $MICRO_VERSION, only compatible ABI additions are possible
dnl
dnl  - for code which was compiled against the same $MAJOR_VERSION and $MINOR_VERSION of the library,
dnl    * old binaries will run with newer $MICRO_VERSION (lt_revision)
dnl    * new binaries will not run with older $MICRO_VERSION (lt_revision, lt_age == 0)
dnl
lt_current=`expr $MAJOR_VERSION \* 100 + $MINOR_VERSION`
lt_revision=$MICRO_VERSION
lt_age=0
LT_VERSION_INFO=$lt_current:$lt_revision:$lt_age
AC_SUBST(LT_VERSION_INFO)

dnl
dnl sndfile
dnl
AC_DEFUN([AC_SNDFILE_REQUIREMENTS],
[
    PKG_CHECK_MODULES(SNDFILE, [sndfile])
    AC_SUBST(SNDFILE_CFLAGS)
    AC_SUBST(SNDFILE_LIBS)
])

dnl
dnl JACK
dnl
AC_DEFUN([AC_JACK_REQUIREMENTS],
[
    dnl disable jack if not found using pkg-config
    PKG_CHECK_MODULES(JACK, [jack], [with_jack="yes"], [with_jack="no"])
])

dnl
dnl LV2
dnl
AC_DEFUN([AC_LV2_REQUIREMENTS],
[
  PKG_CHECK_MODULES([LV2], [[lv2 >= 1.2.0]], [with_lv2="yes"], [with_lv2="no"])
])

AC_SNDFILE_REQUIREMENTS
AC_JACK_REQUIREMENTS
AC_LV2_REQUIREMENTS

dnl determine static/shared library to use for lv2 plugin
if test "x$enable_static" = "xyes"; then
  LIQUIDSFZ_PLUGIN_LIB=libliquidsfz.a
else
  LIQUIDSFZ_PLUGIN_LIB=libliquidsfz.so
fi
AC_SUBST(LIQUIDSFZ_PLUGIN_LIB)

dnl link c++ libs statically if requested
AC_ARG_WITH([static-cxx], AS_HELP_STRING([--with-static-cxx], [link libstdc++ statically]))

if test "x$with_static_cxx" = "xyes"; then
  STATIC_CXX_LDFLAGS="-static-libstdc++ -static-libgcc"
else
  STATIC_CXX_LDFLAGS=""
fi
AC_SUBST(STATIC_CXX_LDFLAGS)

# need c++17 mode
AX_CXX_COMPILE_STDCXX([17], [noext], [mandatory])

# use -Wall
AC_LANG_PUSH([C++])
AX_CHECK_COMPILE_FLAG([-Wall],       [CXXFLAGS="$CXXFLAGS -Wall"])
AC_LANG_POP([C++])

AC_CONFIG_FILES([Makefile lib/Makefile src/Makefile lv2/Makefile lv2/liquidsfz.ttl tests/Makefile static/config.sh liquidsfz.pc])
AC_OUTPUT

echo
echo "---------------------------------------------------------------------------"
echo "$PACKAGE_NAME $PACKAGE_VERSION"
echo "---------------------------------------------------------------------------"
echo "  Build static library . .: $enable_static"
echo "  Build shared library . .: $enable_shared"
echo "  Install prefix . . . . .: $prefix"
echo
