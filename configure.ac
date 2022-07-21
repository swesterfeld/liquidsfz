AC_INIT([liquidsfz], [0.3.0])
AC_CONFIG_SRCDIR([src/liquidsfz.cc])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CXX
AC_DISABLE_SHARED
LT_INIT([pic-only])

dnl Version splitted

[MAJOR_VERSION=`echo $PACKAGE_VERSION | cut -d . -f 1`]
[MINOR_VERSION=`echo $PACKAGE_VERSION | cut -d . -f 2`]
[MICRO_VERSION=`echo $PACKAGE_VERSION | cut -d . -f 3`]

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
  PKG_CHECK_MODULES(JACK, [jack])
  PKG_CHECK_MODULES(READLINE, [readline])
])

dnl
dnl LV2
dnl
AC_DEFUN([AC_LV2_REQUIREMENTS],
[
  PKG_CHECK_MODULES([LV2], [[lv2 >= 1.2.0]])
])

dnl
dnl fftw (testing)
dnl
AC_DEFUN([AC_FFTW_REQUIREMENTS],
[
  PKG_CHECK_MODULES(FFTW, [fftw3 fftw3f])
])

AC_SNDFILE_REQUIREMENTS

dnl -------------------- jack support is optional ---------------------------
AC_ARG_WITH([jack], [AS_HELP_STRING([--with-jack], [build support for JACK])], [], [with_jack=yes])
if test "x$with_jack" != "xno"; then
  AC_JACK_REQUIREMENTS
fi
AM_CONDITIONAL([COND_WITH_JACK], [test "x$with_jack" != "xno"])
dnl -------------------------------------------------------------------------

dnl -------------------- lv2 support is optional ----------------------------
AC_ARG_WITH([lv2], [AS_HELP_STRING([--with-lv2], [build support for LV2])], [], [with_lv2=yes])
if test "x$with_lv2" != "xno"; then
  AC_LV2_REQUIREMENTS
fi
AM_CONDITIONAL([COND_WITH_LV2], [test "x$with_lv2" != "xno"])
dnl -------------------------------------------------------------------------

dnl -------------------- fftw is optional ----------------------------
AC_ARG_WITH([fftw], [AS_HELP_STRING([--with-fftw], [build against fftw library - testing])], [], [with_fftw=no])
if test "x$with_fftw" != "xno"; then
  AC_FFTW_REQUIREMENTS
fi
AM_CONDITIONAL([COND_WITH_FFTW], [test "x$with_fftw" != "xno"])

dnl -------------------- address sanitizer ----------------------------------
AC_ARG_ENABLE([asan], [AS_HELP_STRING([--enable-asan], [build using address sanitizer])],
[
  CXXFLAGS="$CXXFLAGS -fsanitize=address -fno-omit-frame-pointer -fstack-protector-all -fno-inline"
  LDFLAGS="$LDFLAGS -lasan"
],
[
  enable_asan=no
])
dnl -------------------------------------------------------------------------

dnl -------------------- glibcxx assertions ----------------------------------
AC_ARG_ENABLE(debug-cxx,AS_HELP_STRING([--enable-debug-cxx], [setup compiler flags to do C++ STL debug checks]),
[
  CXXFLAGS="$CXXFLAGS -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC"
],
[
  enable_debug_cxx=no
])
dnl -------------------------------------------------------------------------

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

# use -Wall / -pthread if available
AC_LANG_PUSH([C++])
AX_CHECK_COMPILE_FLAG([-Wall], [CXXFLAGS="$CXXFLAGS -Wall"])
AX_CHECK_COMPILE_FLAG([-pthread], [CXXFLAGS="$CXXFLAGS -pthread"])
AC_LANG_POP([C++])

# Less cluttered build output
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_CONFIG_FILES([Makefile lib/Makefile src/Makefile lv2/Makefile lv2/liquidsfz.ttl tests/Makefile static/config.sh liquidsfz.pc])
AC_OUTPUT

echo
echo "---------------------------------------------------------------------------"
echo "$PACKAGE_NAME $PACKAGE_VERSION"
echo "---------------------------------------------------------------------------"
echo "  Build static library . .: $enable_static"
echo "  Build shared library . .: $enable_shared"
echo "  LV2 plug-in support  . .: $with_lv2"
echo "  JACK support . . . . . .: $with_jack"
echo "  Use FFTW library . . . .: $with_fftw"
echo "  Use address sanitizer. .: $enable_asan"
echo "  Use stdc++ debug mode  .: $enable_debug_cxx"
echo "  Install prefix . . . . .: $prefix"
echo
if test "x$enable_shared" = "xyes"; then
  echo "NOTE: Shared libraries can cause problems if binaries created by different"
  echo "people are combined (for instance for an Plugin binary and a DAW binary"
  echo "which need different versions of liquidsfz)."
  echo ""
  echo "So using static libraries is the safer choice."
  echo ""
fi
