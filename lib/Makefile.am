AM_CXXFLAGS = $(SNDFILE_CFLAGS)

lib_LTLIBRARIES = libliquidsfz.la
libliquidsfz_la_SOURCES = liquidsfz.cc liquidsfz.hh loader.cc loader.hh log.hh log.cc \
			  synth.hh synth.cc voice.hh voice.cc utils.hh utils.cc samplecache.hh \
			  envelope.hh curve.hh hydrogenimport.cc hydrogenimport.hh \
			  pugixml.hh pugiconfig.hh midnam.cc midnam.hh filter.hh

EXTRA_DIST = pugixml.cc

liquidsfzincludedir = $(includedir)
liquidsfzinclude_HEADERS = liquidsfz.hh

libliquidsfz_la_LDFLAGS = -no-undefined -version-info $(LT_VERSION_INFO)
