bin_PROGRAMS = liquidsfz

EXTRA_DIST = pantest.sh

AM_CXXFLAGS = $(JACK_CFLAGS) $(SNDFILE_CFLAGS) -I$(top_srcdir)/lib

LIQUIDSFZ_LIBS = $(top_builddir)/lib/libliquidsfz.la $(SNDFILE_LIBS)

liquidsfz_SOURCES = liquidsfz.cc
liquidsfz_LDADD = $(LIQUIDSFZ_LIBS) $(JACK_LIBS)

noinst_PROGRAMS = testliquid testperf

testliquid_SOURCES = testliquid.cc
testliquid_LDADD = $(LIQUIDSFZ_LIBS)

testperf_SOURCES = testperf.cc
testperf_LDADD = $(LIQUIDSFZ_LIBS)
