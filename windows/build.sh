#!/bin/bash
set -Eeo pipefail

docker build -f "windows/Dockerfile" -t liquidsfz-dbuild-win .
docker run -v $PWD:/data -t liquidsfz-dbuild-win /bin/bash -c '
  source static/config.sh
  cd /usr/local/liquidsfz-win/lib/lv2
  zip -r /data/windows/liquidsfz-$PACKAGE_VERSION-win64.zip liquidsfz.lv2'
