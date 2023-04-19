FROM ubuntu:22.04

RUN apt-get update
RUN apt-get -y install wget build-essential autoconf automake \
               libtool-bin pkg-config gettext autoconf-archive \
               xz-utils nsis meson zip

RUN wget -q https://github.com/swesterfeld/spectmorph-mxe/releases/download/2023-03-19/spectmorph-mxe-2023-03-19.tar.xz
RUN tar xf spectmorph-mxe-2023-03-19.tar.xz

ADD . /liquidsfz
WORKDIR /liquidsfz

ENV PATH /mxe/usr/bin:$PATH
RUN mkdir -p /usr/local/liquidsfz-win
RUN cd windows; ./build-deps.sh
ENV LV2_CFLAGS=-I/liquidsfz/windows/prefix/include
ENV LV2_LIBS=-L/liquidsfz/windows/prefix/lib
RUN ./autogen.sh --prefix=/usr/local/liquidsfz-win --host=x86_64-w64-mingw32.static --without-jack
RUN make clean
RUN make -j$(nproc)
RUN make -Clv2 install
