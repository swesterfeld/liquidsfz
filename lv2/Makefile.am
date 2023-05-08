AM_CXXFLAGS = $(LV2_CFLAGS) $(SNDFILE_CFLAGS) -I$(top_srcdir)/lib

liquidsfz_lv2dir = $(libdir)/lv2/liquidsfz.lv2

liquidsfz_lv2_DATA = manifest.ttl liquidsfz.ttl

CLEANFILES = liquidsfz.ttl liquidsfz_lv2.$(PLUGIN_EXT)
liquidsfz.ttl: $(top_builddir)/config.status

EXTRA_DIST = manifest.ttl lv2plugin.cc lv2_midnam.h ldscript.map

all-local: liquidsfz_lv2.$(PLUGIN_EXT)

install-exec-hook: liquidsfz_lv2.$(PLUGIN_EXT)
	mkdir -p $(DESTDIR)$(liquidsfz_lv2dir)
	cp liquidsfz_lv2.$(PLUGIN_EXT) $(DESTDIR)$(liquidsfz_lv2dir)

uninstall-hook:
	rm -f $(DESTDIR)$(liquidsfz_lv2dir)/liquidsfz_lv2.$(PLUGIN_EXT)

if COND_MACOS
liquidsfz_lv2.$(PLUGIN_EXT): $(srcdir)/lv2plugin.cc $(top_builddir)/lib/libliquidsfz.la
	$(CXX) -fPIC -DPIC -shared -o liquidsfz_lv2.$(PLUGIN_EXT) $(srcdir)/lv2plugin.cc $(CXXFLAGS) $(AM_CXXFLAGS) \
	$(top_builddir)/lib/.libs/$(LIQUIDSFZ_PLUGIN_LIB) $(STATIC_CXX_LDFLAGS) $(LDFLAGS) $(SNDFILE_LIBS) \
	-exported_symbols_list macos_export_symbols
else
liquidsfz_lv2.$(PLUGIN_EXT): $(srcdir)/lv2plugin.cc $(top_builddir)/lib/libliquidsfz.la
	$(CXX) -fPIC -DPIC -shared -o liquidsfz_lv2.$(PLUGIN_EXT) $(srcdir)/lv2plugin.cc $(CXXFLAGS) $(AM_CXXFLAGS) \
	$(top_builddir)/lib/.libs/$(LIQUIDSFZ_PLUGIN_LIB) $(STATIC_CXX_LDFLAGS) $(LDFLAGS) $(SNDFILE_LIBS) \
	-Wl,-rpath=$(libdir) -Wl,--version-script=$(srcdir)/ldscript.map
endif
