#!/bin/bash

set -e

SRCDIR=$PWD/src
BUILDD=$PWD/build

mkdir -p $SRCDIR $BUILDD

PREFIX=$PWD/prefix

## we just need a bunch of headers from LV2, so we don't cross compile this package
cd $SRCDIR
wget -q https://github.com/lv2/lv2/archive/refs/tags/v1.18.10.tar.gz
cd $BUILDD
tar xf $SRCDIR/v1.18.10.tar.gz
cd lv2-1.18.10/
meson setup build --prefix $PREFIX
cd build
meson compile
meson install
