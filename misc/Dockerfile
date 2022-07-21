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

RUN misc/build.sh
