bin_PROGRAMS = liquidsfz

LIQUIDSFZ_LIBS = libliquidsfz.la $(SNDFILE_LIBS)

lib_LTLIBRARIES = libliquidsfz.la
libliquidsfz_la_SOURCES = api.cc api.hh loader.cc loader.hh log.hh log.cc \
			  synth.hh voice.hh voice.cc utils.hh utils.cc

liquidsfz_SOURCES = liquidsfz.cc
liquidsfz_LDADD = $(LIQUIDSFZ_LIBS) $(JACK_LIBS)

AM_CXXFLAGS = $(SNDFILE_CFLAGS) $(JACK_CFLAGS)

noinst_PROGRAMS = testliquid testperf

testliquid_SOURCES = testliquid.cc
testliquid_LDADD = $(LIQUIDSFZ_LIBS)

testperf_SOURCES = testperf.cc
testperf_LDADD = $(LIQUIDSFZ_LIBS)
