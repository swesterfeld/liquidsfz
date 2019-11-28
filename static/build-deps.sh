#!/bin/bash

### EXPERIMENTAL helper script to build statically linked LV2

# build all dependency libs, based on a script written by Robin Gareus
# https://github.com/zynaddsubfx/zyn-build-osx/blob/master/01_compile.sh

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

function download {
  echo "--- Downloading.. $2"
  test -f ${SRCDIR}/$1 || curl -L -o ${SRCDIR}/$1 $2
}

function src {
  download ${1}.${2} $3
  cd ${BUILDD}
  rm -rf $1
  tar xf ${SRCDIR}/${1}.${2}
  cd $1
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

src libogg-1.3.3 tar.xz http://downloads.xiph.org/releases/ogg/libogg-1.3.3.tar.xz
autoconfbuild --disable-shared

src libvorbis-1.3.6 tar.xz http://downloads.xiph.org/releases/vorbis/libvorbis-1.3.6.tar.xz
autoconfbuild --disable-shared

src flac-1.3.2 tar.xz https://ftp.osuosl.org/pub/xiph/releases/flac/flac-1.3.2.tar.xz
autoconfbuild --disable-shared

src libsndfile-1.0.28 tar.gz http://www.mega-nerd.com/libsndfile/files/libsndfile-1.0.28.tar.gz
autoconfbuild --disable-shared
