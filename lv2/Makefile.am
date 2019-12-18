AM_CXXFLAGS = $(JACK_CFLAGS) $(SNDFILE_CFLAGS) -I$(top_srcdir)/lib

liquidsfz_lv2dir = $(libdir)/lv2/liquidsfz.lv2

liquidsfz_lv2_DATA = manifest.ttl liquidsfz.ttl

CLEANFILES = liquidsfz.ttl liquidsfz_lv2.so
liquidsfz.ttl: $(top_builddir)/config.status

EXTRA_DIST = manifest.ttl lv2plugin.cc ldscript.map

all-local: liquidsfz_lv2.so

install-exec-hook: liquidsfz_lv2.so
	mkdir -p $(DESTDIR)$(liquidsfz_lv2dir)
	cp liquidsfz_lv2.so $(DESTDIR)$(liquidsfz_lv2dir)

uninstall-hook:
	rm -f $(DESTDIR)$(liquidsfz_lv2dir)/liquidsfz_lv2.so

liquidsfz_lv2.so: $(srcdir)/lv2plugin.cc $(top_builddir)/lib/libliquidsfz.la
	$(CXX) -fPIC -DPIC -shared -o liquidsfz_lv2.so $(srcdir)/lv2plugin.cc $(CXXFLAGS) $(AM_CXXFLAGS) \
	$(top_builddir)/lib/.libs/libliquidsfz.a $(STATIC_CXX_LDFLAGS) $(SNDFILE_LIBS) -Wl,--version-script=$(srcdir)/ldscript.map
