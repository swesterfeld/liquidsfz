AM_CXXFLAGS = $(LV2_CFLAGS) $(SNDFILE_CFLAGS) -I$(top_srcdir)/lib \
	      -I$(top_srcdir)/3rdparty/pugl/include \
	      -I$(top_srcdir)/3rdparty/imgui -I$(top_srcdir)/3rdparty/imgui/backends

liquidsfz_lv2dir = $(libdir)/lv2/liquidsfz.lv2

liquidsfz_lv2_DATA = manifest.ttl liquidsfz.ttl

CLEANFILES = liquidsfz.ttl liquidsfz_lv2.$(PLUGIN_EXT)
liquidsfz.ttl: $(top_builddir)/config.status

FONT = dejavu-lgc-sans.ttf
EXTRA_DIST = manifest.ttl lv2plugin.cc lv2plugin.hh lv2ui.cc lv2_midnam.h ldscript.map macos_export_symbols $(FONT).xz

all-local: liquidsfz_lv2.$(PLUGIN_EXT)

install-exec-hook: liquidsfz_lv2.$(PLUGIN_EXT)
	mkdir -p $(DESTDIR)$(liquidsfz_lv2dir)
	cp liquidsfz_lv2.$(PLUGIN_EXT) $(DESTDIR)$(liquidsfz_lv2dir)

install-data-hook:
	mkdir -p $(DESTDIR)$(liquidsfz_lv2dir)
	xzcat $(srcdir)/$(FONT).xz > $(DESTDIR)$(liquidsfz_lv2dir)/$(FONT)

uninstall-hook:
	rm -f $(DESTDIR)$(liquidsfz_lv2dir)/liquidsfz_lv2.$(PLUGIN_EXT)
	rm -f $(DESTDIR)$(liquidsfz_lv2dir)/$(FONT)

if COND_MACOS
liquidsfz_lv2.$(PLUGIN_EXT): $(srcdir)/lv2plugin.cc $(srcdir)/lv2ui.cc $(top_builddir)/lib/libliquidsfz.la
	$(CXX) -fPIC -DPIC -shared -o liquidsfz_lv2.$(PLUGIN_EXT) $(srcdir)/lv2plugin.cc $(srcdir)/lv2ui.cc $(CXXFLAGS) $(AM_CXXFLAGS) \
	$(top_builddir)/lib/.libs/$(LIQUIDSFZ_PLUGIN_LIB) $(STATIC_CXX_LDFLAGS) $(LDFLAGS) $(SNDFILE_LIBS) \
	$(top_builddir)/3rdparty/.libs/libliquidsfzglui.a $(GL_LIBS) $(X11_LIBS) \
	-exported_symbols_list $(srcdir)/macos_export_symbols
else
liquidsfz_lv2.$(PLUGIN_EXT): $(srcdir)/lv2plugin.cc $(srcdir)/lv2ui.cc $(top_builddir)/lib/libliquidsfz.la
	$(CXX) -fPIC -DPIC -shared -o liquidsfz_lv2.$(PLUGIN_EXT) $(srcdir)/lv2plugin.cc $(srcdir)/lv2ui.cc $(CXXFLAGS) $(AM_CXXFLAGS) \
	$(top_builddir)/lib/.libs/$(LIQUIDSFZ_PLUGIN_LIB) $(STATIC_CXX_LDFLAGS) $(LDFLAGS) $(SNDFILE_LIBS) \
	$(top_builddir)/3rdparty/.libs/libliquidsfzglui.a $(GL_LIBS) $(X11_LIBS) \
	-Wl,-rpath=$(libdir) -Wl,--version-script=$(srcdir)/ldscript.map
endif
