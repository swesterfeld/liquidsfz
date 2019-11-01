bin_PROGRAMS = liquidsfz

liquidsfz_SOURCES = liquidsfz.cc samplecache.hh loader.cc loader.hh
liquidsfz_LDFLAGS = $(SNDFILE_LIBS) $(JACK_LIBS) -lstdc++fs

AM_CXXFLAGS = $(SNDFILE_CFLAGS) $(JACK_CFLAGS)

noinst_PROGRAMS = testliquid

testliquid_SOURCES = testliquid.cc loader.cc
testliquid_LDFLAGS = $(SNDFILE_LIBS) -lstdc++fs
