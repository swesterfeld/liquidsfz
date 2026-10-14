#!/bin/bash
set -e
source ./config.sh
cd ..
# Refresh the base and dependencies to include available security updates.
docker build --pull --no-cache -t liquidsfz -f static/Dockerfile .
docker run -v $PWD/static:/data -t liquidsfz tar Cczfv /usr/local/liquidsfz/lib/lv2 /data/liquidsfz-$PACKAGE_VERSION-x86_64.tar.gz liquidsfz.lv2
