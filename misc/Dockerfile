# EXPERIMENTAL file to build statically linked LV2

FROM gcc:latest

RUN sed -Ei 's/^# deb-src /deb-src /' /etc/apt/sources.list

# kxstudio repos for lv2lint
RUN wget https://launchpad.net/~kxstudio-debian/+archive/kxstudio/+files/kxstudio-repos_11.1.0_all.deb
RUN dpkg -i kxstudio-repos_11.1.0_all.deb
RUN apt-get update && apt-get install -y build-essential
RUN apt-get install -y dpkg-dev
RUN apt-get install -y autoconf-archive
RUN apt-get install -y libtool
RUN apt-get install -y pkg-config
RUN apt-get install -y lv2-dev
RUN apt-get install -y libsndfile1-dev
RUN apt-get install -y libjack-jackd2-dev
RUN apt-get install -y curl
RUN apt-get install -y software-properties-common
RUN apt-get install -y libtool-bin
RUN apt-get install -y gettext
RUN apt-get install -y lv2lint
RUN apt-get install -y clang

ADD . /liquidsfz
WORKDIR /liquidsfz

########## Build / Test with gcc
ENV CC=gcc CXX=g++
RUN $CXX --version
RUN ./autogen.sh
RUN make -j `nproc` V=1
# only run make distcheck once (make check otherwise)
RUN make -j `nproc` distcheck
RUN make install
RUN lv2lint http://spectmorph.org/plugins/liquidsfz

########## Cleanup

RUN make uninstall
RUN make distclean

########## Build / Test with clang

ENV CC=clang CXX=clang++
RUN $CXX --version
RUN ./autogen.sh
RUN make -j `nproc` V=1
RUN make -j `nproc` check
RUN make install
RUN lv2lint http://spectmorph.org/plugins/liquidsfz
