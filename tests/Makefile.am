LIQUIDSFZ_LIBS = $(top_builddir)/lib/libliquidsfz.la $(SNDFILE_LIBS)

AM_CXXFLAGS = $(JACK_CFLAGS) $(SNDFILE_CFLAGS) -I$(top_srcdir)/lib

noinst_PROGRAMS = testliquid testperf testxf testenvelope testcurve testhydrogen testmidnam testfilter

EXTRA_DIST = gen-upsample.py

testliquid_SOURCES = testliquid.cc
testliquid_LDADD = $(LIQUIDSFZ_LIBS)

testperf_SOURCES = testperf.cc
testperf_LDADD = $(LIQUIDSFZ_LIBS)

testxf_SOURCES = testxf.cc
testxf_LDADD = $(LIQUIDSFZ_LIBS)

testenvelope_SOURCES = testenvelope.cc
testenvelope_LDADD = $(LIQUIDSFZ_LIBS)

testcurve_SOURCES = testcurve.cc
testcurve_LDADD = $(LIQUIDSFZ_LIBS)

testhydrogen_SOURCES = testhydrogen.cc
testhydrogen_LDADD = $(LIQUIDSFZ_LIBS)

testmidnam_SOURCES = testmidnam.cc
testmidnam_LDADD = $(LIQUIDSFZ_LIBS)

testfilter_SOURCES = testfilter.cc
testfilter_LDADD = $(LIQUIDSFZ_LIBS)

if COND_WITH_FFTW
noinst_PROGRAMS += testupsample
testupsample_SOURCES = testupsample.cc
testupsample_LDADD = $(LIQUIDSFZ_LIBS) $(FFTW_LIBS)
endif
