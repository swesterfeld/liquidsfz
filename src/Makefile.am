bin_PROGRAMS = liquidsfz

liquidsfz_SOURCES = liquidsfz.cc samplecache.hh
liquidsfz_LDFLAGS = $(SNDFILE_LIBS) $(FLUIDSYNTH_LIBS) $(JACK_LIBS) -lstdc++fs

AM_CXXFLAGS = $(SNDFILE_CFLAGS) $(FLUIDSYNTH_CFLAGS) $(JACK_CFLAGS)
