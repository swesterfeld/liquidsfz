bin_PROGRAMS = liquidsfz

EXTRA_DIST = pantest.sh

AM_CXXFLAGS = $(JACK_CFLAGS) $(SNDFILE_CFLAGS) $(READLINE_LIBS) -I$(top_srcdir)/lib

LIQUIDSFZ_LIBS = $(top_builddir)/lib/libliquidsfz.la $(SNDFILE_LIBS)

liquidsfz_SOURCES = liquidsfz.cc cliparser.cc cliparser.hh
liquidsfz_LDADD = $(LIQUIDSFZ_LIBS) $(JACK_LIBS) $(READLINE_LIBS)
