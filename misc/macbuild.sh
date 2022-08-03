#!/bin/bash
# This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
set -Eeuo pipefail

brew install autoconf-archive automake libsndfile jack lv2 fftw
./autogen.sh --without-jack --without-lv2 --with-fftw
make
make check
