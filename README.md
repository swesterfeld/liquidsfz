# liquidsfz

## DESCRIPTION

liquidsfz is an attempt to implement an free and open source sampler that can
load and play .sfz files. The main goal is to provide a library that is easy to
integrate into other projects. Right now the API is still changing, but in the
long term goal is to provide a library API that is stable.

API documentation is available here:
([LiquidSFZ API]http://space.twc.de/~stefan/liquidsfz/api/)

## TESTING

Right now, liquidsfz is only available as command line program. You can use it
like this:

    liquidsfz ~/sfz/SalamanderGrandPianoV3_44.1khz16bit/SalamanderGrandPianoV3.sfz

liquidsfz works as jack client with midi input and audio ouput. If you connect
the jack midi input / audio output using a patchbay, you can send midi events
to liquidsfz to test the sfz loader.

## LICENSE

The code is licensed under the LGPL 2.1 or later.
