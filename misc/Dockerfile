FROM gcc:latest

# kxstudio repos for lv2lint
RUN wget https://launchpad.net/~kxstudio-debian/+archive/kxstudio/+files/kxstudio-repos_11.2.0_all.deb
RUN apt-get update && apt-get install -y \
    build-essential dpkg-dev autoconf-archive libtool pkg-config \
    lv2-dev libsndfile1-dev libjack-jackd2-dev libfftw3-dev \
    libtool-bin gettext clang ./kxstudio-repos_11.2.0_all.deb \
    libgl1-mesa-dev libxrandr-dev libxcursor-dev
RUN apt-get update && apt-get install -y lv2lint

ADD . /liquidsfz
WORKDIR /liquidsfz

RUN misc/build.sh
