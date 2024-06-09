#!/bin/bash
# This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
set -Eeuo pipefail -x

brew install autoconf-archive automake pkg-config libsndfile jack lv2 fftw libtool
export PKG_CONFIG_PATH="$(brew --prefix readline)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
./autogen.sh --with-fftw
make -j `sysctl -n hw.ncpu`
make check
