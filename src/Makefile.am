bin_PROGRAMS = liquidsfz

COMMON_SOURCES = log.hh log.cc voice.hh voice.cc utils.hh synth.hh api.cc api.hh

liquidsfz_SOURCES = liquidsfz.cc samplecache.hh loader.cc loader.hh envelope.hh $(COMMON_SOURCES)
liquidsfz_LDFLAGS = $(SNDFILE_LIBS) $(JACK_LIBS) -lstdc++fs

AM_CXXFLAGS = $(SNDFILE_CFLAGS) $(JACK_CFLAGS)

noinst_PROGRAMS = testliquid testperf

testliquid_SOURCES = testliquid.cc loader.cc $(COMMON_SOURCES)
testliquid_LDFLAGS = $(SNDFILE_LIBS) -lstdc++fs

testperf_SOURCES = testperf.cc loader.cc $(COMMON_SOURCES)
testperf_LDFLAGS = $(SNDFILE_LIBS) -lstdc++fs
