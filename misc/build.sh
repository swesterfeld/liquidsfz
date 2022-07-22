#!/bin/bash
# This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
set -Eeuo pipefail

########## Build / Test with gcc / ASAN / Debug C++
CC=gcc CXX=g++
$CXX --version
./autogen.sh --enable-asan --enable-debug-cxx --with-fftw
make -j `nproc` V=1
make -j `nproc` check
########## Cleanup

make uninstall
make distclean

########## Build / Test with gcc / Debug C++
./autogen.sh --enable-debug-cxx --with-fftw
make -j `nproc` V=1
make -j `nproc` check
make install
lv2lint http://spectmorph.org/plugins/liquidsfz

########## Distcheck
make -j `nproc` distcheck

########## Cleanup
make uninstall
make distclean

########## Build / Test with clang

CC=clang CXX=clang++
$CXX --version
./autogen.sh --with-fftw
make -j `nproc` V=1
make -j `nproc` check
make install
lv2lint http://spectmorph.org/plugins/liquidsfz
