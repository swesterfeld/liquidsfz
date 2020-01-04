LIQUIDSFZ_LIBS = $(top_builddir)/lib/libliquidsfz.la $(SNDFILE_LIBS)

AM_CXXFLAGS = $(JACK_CFLAGS) $(SNDFILE_CFLAGS) -I$(top_srcdir)/lib

noinst_PROGRAMS = testliquid testperf testxf testenvelope

testliquid_SOURCES = testliquid.cc
testliquid_LDADD = $(LIQUIDSFZ_LIBS)

testperf_SOURCES = testperf.cc
testperf_LDADD = $(LIQUIDSFZ_LIBS)

testxf_SOURCES = testxf.cc
testxf_LDADD = $(LIQUIDSFZ_LIBS)

testenvelope_SOURCES = testenvelope.cc
testenvelope_LDADD = $(LIQUIDSFZ_LIBS)
