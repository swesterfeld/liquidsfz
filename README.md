# liquidsfz

## DESCRIPTION

liquidsfz is a free and open source sampler that can load and play .sfz files. We
support JACK and LV2.

The main goal is to provide a library that is easy to integrate into other
projects. Right now the API is still changing, but in the long term goal is to
provide a library API that is stable.

 * API documentation is available here: http://space.twc.de/~stefan/liquidsfz/api-0.2.0
 * There is also a list of [currently supported SFZ opcodes](OPCODES.md)

## COMPILING

To compile liquidsfz, use the usual

    ./configure
    make
    make install

You need the packages (on Debian/Ubuntu):

* libjack-jackd2-dev
* libsndfile1-dev
* lv2-dev

If you are building from git, you also need the package (for autogen.sh to work):

* autoconf-archive

## USING JACK

The liquidsfz command line program can be used like this:

    liquidsfz ~/sfz/SalamanderGrandPianoV3_44.1khz16bit/SalamanderGrandPianoV3.sfz

liquidsfz works as jack client with midi input and audio ouput. If you connect
the jack midi input / audio output using a patchbay, you can send midi events
to liquidsfz to test the sfz loader.

If you are interested in using a graphical front-end in conjunction with jack,
you can try [QLiquidSFZ](https://github.com/be1/qliquidsfz).

## LV2 PLUGIN

We provide a LV2 plugin for hosts like Ardour/Carla/Qtractor and others, which
is built and installed by default. It can also be downloaded as binary release
below.

## LICENSE

The code is licensed under the LGPL 2.1 or later.

## RELEASES

The current version of liquidsfz is liquidsfz-0.2.0, and can be downloaded
here:

* http://space.twc.de/~stefan/liquidsfz/liquidsfz-0.2.0.tar.bz2

## BINARY LV2 PLUGIN

The LV2 plugin (and only the LV2 plugin) is available as self-contained 64-bit
binary which should run on most recent linux distributions (newer than Ubuntu
16.04).

* http://space.twc.de/~stefan/liquidsfz/liquidsfz-0.2.0-x86_64.tar.gz

To install it, extract the archive and

    mkdir -p $HOME/.lv2
    cp -a liquidsfz.lv2  $HOME/.lv2/

## OLDER VERSIONS

* http://space.twc.de/~stefan/liquidsfz/liquidsfz-0.1.0.tar.bz2 (old version)
