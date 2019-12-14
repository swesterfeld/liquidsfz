#!/bin/bash

set -e

function autoconfconf {
echo "======= $(pwd) ======="
  CPPFLAGS="-I${PREFIX}/include${GLOBAL_CPPFLAGS:+ $GLOBAL_CPPFLAGS}" \
  CFLAGS="${GLOBAL_CFLAGS:+ $GLOBAL_CFLAGS}" \
  CXXFLAGS="${GLOBAL_CXXFLAGS:+ $GLOBAL_CXXFLAGS}" \
  LDFLAGS="${GLOBAL_LDFLAGS:+ $GLOBAL_LDFLAGS}" \
  ./configure --disable-dependency-tracking --prefix=$PREFIX $@
}

function autoconfbuild {
  autoconfconf $@
  make $MAKEFLAGS
  make install
}

SRCDIR=$PWD/src
BUILDD=$PWD/build

mkdir -p $SRCDIR $BUILDD

PREFIX=$PWD/prefix
GLOBAL_CFLAGS="-fPIC -DPIC"
GLOBAL_LDFLAGS="-L$PREFIX/lib"
MAKEFLAGS="-j16"
PATH=$PWD/prefix/bin:$PATH
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH

cd $SRCDIR
apt-get source libogg
apt-get source libvorbis
apt-get source flac
apt-get source libsndfile

cd $SRCDIR/libogg-1.3.2
autoconfbuild --disable-shared

cd $SRCDIR/libvorbis-1.3.5
autoconfbuild --disable-shared

cd $SRCDIR/flac-1.3.1
./autogen.sh
autoconfbuild --disable-shared

cd $SRCDIR/libsndfile-1.0.25
autoconfbuild --disable-shared
