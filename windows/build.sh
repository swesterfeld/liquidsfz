#!/bin/bash
set -Eeo pipefail

source static/config.sh
docker build -f "windows/Dockerfile" -t liquidsfz-dbuild-win .
docker run -v $PWD:/data -t liquidsfz-dbuild-win /bin/bash -c "
  cd /usr/local/liquidsfz-win/lib/lv2
  zip -r /data/windows/liquidsfz-$PACKAGE_VERSION-win64.zip liquidsfz.lv2"
