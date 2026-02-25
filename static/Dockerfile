# EXPERIMENTAL file to build statically linked LV2

FROM ubuntu:18.04

RUN sed -Ei 's/^# deb-src /deb-src /' /etc/apt/sources.list

RUN apt-get update && apt-get install -y software-properties-common
RUN add-apt-repository ppa:ubuntu-toolchain-r/test
RUN apt-get update && apt-get install -y build-essential \
    dpkg-dev autoconf-archive libtool libtool-bin \
    pkg-config lv2-dev curl g++-11 gettext \
    libgl1-mesa-dev libxrandr-dev libxcursor-dev

ENV CC gcc-11
ENV CXX g++-11

ADD . /liquidsfz
WORKDIR /liquidsfz

ENV PKG_CONFIG_PATH=/liquidsfz/static/prefix/lib/pkgconfig
ENV PKG_CONFIG="pkg-config --static"
RUN cd static && ./build-deps.sh
RUN ./autogen.sh --prefix=/usr/local/liquidsfz --with-static-cxx --without-jack
RUN make clean
RUN make -j16
RUN make check
RUN make install

# docker build -t liquidsfz .
# docker run -v /tmp:/data -t liquidsfz tar Cczfv /usr/local /data/liquidsfz-static.tar.gz liquidsfz
# docker run -v /tmp:/data -it liquidsfz
# docker cp ...:/usr/local/lib/lv2/liquidsfz.lv2/liquidsfz_lv2.so /tmp
