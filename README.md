# liquidsfz

## DESCRIPTION

liquidsfz is a free and open source sampler that can load and play .sfz files.

The main goal is to provide a library that is easy to integrate into other
projects. Right now the API is still changing, but in the long term goal is to
provide a library API that is stable.

 * API documentation is available here: http://space.twc.de/~stefan/liquidsfz/api-0.1.0
 * There is also a [List of currently supported SFZ opcodes](OPCODES.md)

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

## TESTING

Right now, liquidsfz is only available as command line program. You can use it
like this:

    liquidsfz ~/sfz/SalamanderGrandPianoV3_44.1khz16bit/SalamanderGrandPianoV3.sfz

liquidsfz works as jack client with midi input and audio ouput. If you connect
the jack midi input / audio output using a patchbay, you can send midi events
to liquidsfz to test the sfz loader.

## LICENSE

The code is licensed under the LGPL 2.1 or later.

## RELEASES

The current version of liquidsfz is liquidsfz-0.1.0, and can be downloaded
here:

* http://space.twc.de/~stefan/liquidsfz/liquidsfz-0.1.0.tar.bz2
