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
ENV PKG_CONFIG="pkg-config --static"
RUN cd static && ./build-deps.sh
RUN ./autogen.sh --prefix=/usr/local/liquidsfz
RUN make -j16
RUN make install

# docker build -t liquidsfz .
# docker run -v /tmp:/data -t liquidsfz tar Cczfv /usr/local /data/liquidsfz-static.tar.gz liquidsfz
# docker run -v /tmp:/data -it liquidsfz
# docker cp ...:/usr/local/lib/lv2/liquidsfz.lv2/liquidsfz_lv2.so /tmp
