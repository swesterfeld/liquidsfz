AM_CXXFLAGS = $(JACK_CFLAGS) $(SNDFILE_CFLAGS) -I$(top_srcdir)/lib

LIQUIDSFZ_LIBS = $(top_builddir)/lib/libliquidsfz.la $(SNDFILE_LIBS)

liquidsfz_lv2dir = $(libdir)/lv2/liquidsfz.lv2

plugindir = $(liquidsfz_lv2dir)
plugin_LTLIBRARIES = liquidsfz_lv2.la

liquidsfz_lv2_la_SOURCES = lv2plugin.cc
liquidsfz_lv2_la_LIBADD  = $(LIQUIDSFZ_LIBS)
liquidsfz_lv2_la_LDFLAGS = -module -avoid-version -shared -Wl,--version-script=$(srcdir)/ldscript.map

liquidsfz_lv2_DATA = manifest.ttl liquidsfz.ttl

CLEANFILES = liquidsfz.ttl
liquidsfz.ttl: $(top_builddir)/config.status

EXTRA_DIST = manifest.ttl

# we only want to install the .so file; not the .la file
install-data-hook:
	rm -f $(DESTDIR)$(liquidsfz_lv2dir)/liquidsfz_lv2.la
	rm -f $(DESTDIR)$(liquidsfz_lv2dir)/liquidsfz_lv2.a

uninstall-hook:
	rm -f $(DESTDIR)$(liquidsfz_lv2dir)/liquidsfz_lv2.so

liquidsfz_lv2_static.so: $(plugin_LTLIBRARIES)
	$(CXX) -shared -o liquidsfz_lv2_static.so .libs/lv2plugin.o $(AM_CXXFLAGS) \
	$(top_builddir)/lib/.libs/libliquidsfz.a -static-libstdc++ -static-libgcc $(SNDFILE_LIBS) -Wl,--version-script=$(srcdir)/ldscript.map
