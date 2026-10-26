AUTOMAKE_OPTIONS = subdir-objects

LIQUIDSFZ_LIBS = $(top_builddir)/lib/libliquidsfz.la $(SNDFILE_LIBS)

AM_CXXFLAGS = $(FFTW_CFLAGS) $(SNDFILE_CFLAGS) -I$(top_srcdir)/lib

TESTS = testsynth testsfzreader testsfpool testflac testutils testtextscanner

noinst_PROGRAMS = $(TESTS) testliquid testperf testxf testenvelope testcurve testhydrogen testmidnam testfilter testrtmutex
check_PROGRAMS =

testutils_SOURCES = testutils.cc

testtextscanner_SOURCES = testtextscanner.cc testutils.hh
testtextscanner_LDADD = $(LIQUIDSFZ_LIBS)

EXTRA_DIST = gen-upsample.py testupsample.sfz repro-lfo-alloc.sfz fixtures/stereo.flac fixtures/flac.sfz fixtures/README.md

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

testrtmutex_SOURCES = testrtmutex.cc
testrtmutex_LDADD = $(LIQUIDSFZ_LIBS)

testsynth_SOURCES = testsynth.cc testutils.hh
testsynth_LDADD = $(LIQUIDSFZ_LIBS) $(FFTW_LIBS)

testsfzreader_SOURCES = testsfzreader.cc
testsfzreader_LDADD = $(LIQUIDSFZ_LIBS)

testsfpool_SOURCES = testsfpool.cc
testsfpool_LDADD = $(LIQUIDSFZ_LIBS)

testflac_SOURCES = testflac.cc
testflac_CPPFLAGS = $(AM_CPPFLAGS) '-DTEST_SRCDIR="$(srcdir)"'
testflac_LDADD = $(LIQUIDSFZ_LIBS)

if COND_WITH_FFTW
noinst_PROGRAMS += testupsample
testupsample_SOURCES = testupsample.cc
testupsample_LDADD = $(LIQUIDSFZ_LIBS) $(FFTW_LIBS)
endif

if COND_WITH_LV2
if COND_LINUX
# A standalone host: do not link the implementation or any audio codecs here.
check_PROGRAMS += testlv2output
testlv2output_SOURCES = testlv2output.cc
testlv2output_CXXFLAGS = $(LV2_CFLAGS)
testlv2output_LDADD = -ldl
endif
TESTS += testfiledialog testfiledialogenv testlv2worker
testfiledialog_SOURCES = testfiledialog.cc ../lv2/filedialog.cc ../lv2/filedialog.hh
testfiledialogenv_SOURCES = testfiledialogenv.cc
testlv2worker_SOURCES = testlv2worker.cc ../lv2/lv2plugin.cc ../lv2/lv2plugin.hh
testlv2worker_CXXFLAGS = $(AM_CXXFLAGS) $(LV2_CFLAGS)
testlv2worker_LDADD = $(LIQUIDSFZ_LIBS)
endif
