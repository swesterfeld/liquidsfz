liquidsfz
=========

# DESCRIPTION

liquidsfz is an attempt to implement a full sfz sampler based on fluidsynth. So
the code here merely contains a parser for sfz files, code to load the samples
and code to map the sfz parameters to fluidsynth. The actual synthesis is
performed using fluidsynth.

Right now liquidsfz is in the early development phase. Later on, it will have
a library API so that other programs can also support the sfz format.

# TESTING

Right now, liquidsfz is only available as command line program. You can use it
like this:

    liquidsfz ~/sfz/SalamanderGrandPianoV3_44.1khz16bit/SalamanderGrandPianoV3.sfz

Currently, liquidsfz will initialize the fluidsynth jack driver. If you connect
the jack midi input using a patchbay, you can send midi events to liquidsfz to
test the sfz loader.
