# liquidsfz

## DESCRIPTION

liquidsfz is a free and open source sampler that can load and play .sfz files.
It can also load and play Hydrogen drumkits. We support JACK and LV2.

The main goal is to provide a library that is easy to integrate into other
projects.

 * API documentation is available here: https://space.twc.de/~stefan/liquidsfz/api-0.2.3
 * There is also a list of [currently supported SFZ opcodes](OPCODES.md)

The API should be fairly stable at this point, so we try to maintain source
compatibility.

## COMPILING

To compile liquidsfz, use the usual

    ./configure
    make
    make install

You need the packages (on Debian/Ubuntu):

* libjack-jackd2-dev (JACK client)
* libreadline-dev (JACK client)
* lv2-dev (LV2 plugin)
* libsndfile1-dev

Some components can be disabled during configure, --without-jack will turn off
building the JACK client, --without-lv2 will turn off building the LV2 plugin.

If you are building from git, you also need the package (for autogen.sh to work):

* autoconf-archive

### WINDOWS

If you are a windows developer and need only the .dll (not the plugin or jack
client), there is [experimental support for building on windows](README_WINDOWS.md)
but this experimental and may or may not work.

## USING JACK

The liquidsfz command line program can be used like this:

    liquidsfz ~/sfz/SalamanderGrandPianoV3_44.1khz16bit/SalamanderGrandPianoV3.sfz

Or if you want to load a Hydrogen drumkit, like this:

    liquidsfz /usr/share/hydrogen/data/drumkits/GMRockKit/drumkit.xml

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

The current version of liquidsfz is liquidsfz-0.2.3, and can be downloaded
here:

* https://space.twc.de/~stefan/liquidsfz/liquidsfz-0.2.3.tar.bz2

## BINARY LV2 PLUGIN

The LV2 plugin (and only the LV2 plugin) is available as self-contained 64-bit
binary which should run on most recent linux distributions (newer than Ubuntu
16.04).

* https://space.twc.de/~stefan/liquidsfz/liquidsfz-0.2.3-x86_64.tar.gz

To install it, extract the archive and

    mkdir -p $HOME/.lv2
    cp -a liquidsfz.lv2  $HOME/.lv2/

## OLDER VERSIONS

* https://space.twc.de/~stefan/liquidsfz/liquidsfz-0.2.2.tar.bz2 (old version)
* https://space.twc.de/~stefan/liquidsfz/liquidsfz-0.2.1.tar.bz2 (old version)
* https://space.twc.de/~stefan/liquidsfz/liquidsfz-0.2.0.tar.bz2 (old version)
* https://space.twc.de/~stefan/liquidsfz/liquidsfz-0.1.0.tar.bz2 (old version)
