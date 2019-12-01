# EXPERIMENTAL file to build statically linked LV2

FROM ubuntu:16.04

RUN apt-get update && apt-get install -y build-essential
RUN apt-get install -y autoconf-archive
RUN apt-get install -y libtool
RUN apt-get install -y libjack-jackd2-dev
RUN apt-get install -y lv2-dev
RUN apt-get install -y curl
RUN apt-get install -y software-properties-common
RUN add-apt-repository ppa:ubuntu-toolchain-r/test
RUN apt-get update
RUN apt-get install -y g++-9

ENV CC gcc-9
ENV CXX g++-9

ADD . /liquidsfz
WORKDIR /liquidsfz

ENV PKG_CONFIG_PATH=/liquidsfz/static/prefix/lib/pkgconfig
ENV PKG_CONFIG="pkg-config --static"
RUN cd static && ./build-deps.sh
RUN ./autogen.sh --prefix=/usr/local/liquidsfz
RUN make -j16
RUN cd lv2 && make liquidsfz_lv2_static.so
RUN make install
RUN cp lv2/liquidsfz_lv2_static.so /usr/local/liquidsfz/lib/lv2/liquidsfz.lv2/liquidsfz_lv2.so

# docker build -t liquidsfz .
# docker run -v /tmp:/data -t liquidsfz tar Cczfv /usr/local /data/liquidsfz-static.tar.gz liquidsfz
# docker run -v /tmp:/data -it liquidsfz
# docker cp ...:/usr/local/lib/lv2/liquidsfz.lv2/liquidsfz_lv2.so /tmp
