#!/bin/bash
set -e
source ./config.sh
cd ..
docker build -t liquidsfz -f static/Dockerfile .
docker run -v $PWD/static:/data -t liquidsfz tar Cczfv /usr/local/liquidsfz/lib/lv2 /data/liquidsfz-$PACKAGE_VERSION-x86_64.tar.gz liquidsfz.lv2
