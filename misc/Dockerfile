FROM gcc:latest

# kxstudio repos for lv2lint
RUN wget https://launchpad.net/~kxstudio-debian/+archive/kxstudio/+files/kxstudio-repos_11.1.0_all.deb
RUN dpkg -i kxstudio-repos_11.1.0_all.deb
RUN apt-get update && apt-get install -y \
    build-essential dpkg-dev autoconf-archive libtool pkg-config \
    lv2-dev libsndfile1-dev libjack-jackd2-dev libfftw3-dev \
    libtool-bin gettext lv2lint clang

ADD . /liquidsfz
WORKDIR /liquidsfz

RUN misc/build.sh
