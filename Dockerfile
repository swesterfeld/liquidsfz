# EXPERIMENTAL file to build statically linked LV2

FROM ubuntu:18.04

RUN apt-get update && apt-get install -y build-essential
RUN apt-get install -y autoconf-archive
RUN apt-get install -y libtool
RUN apt-get install -y libjack-jackd2-dev
RUN apt-get install -y lv2-dev
RUN apt-get install -y curl

ADD . /liquidsfz
WORKDIR /liquidsfz

ENV PKG_CONFIG_PATH=/liquidsfz/static/prefix/lib/pkgconfig
RUN echo $PKG_CONFIG_PATH
RUN cd static && ./build-deps.sh
RUN ./autogen.sh
RUN make
RUN make install

# docker build .
# docker run -v /tmp:/data -it ...
# docker cp ...:/usr/local/lib/lv2/liquidsfz.lv2/liquidsfz_lv2.so /tmp
