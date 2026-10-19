AM_CXXFLAGS = $(LV2_CFLAGS) $(SNDFILE_CFLAGS) $(GL_CFLAGS) $(X11_CFLAGS) -I$(top_srcdir)/lib \
	      -I$(top_srcdir)/3rdparty/pugl/include \
	      -I$(top_srcdir)/3rdparty/imgui -I$(top_srcdir)/3rdparty/imgui/backends

liquidsfz_lv2dir = $(libdir)/lv2/liquidsfz.lv2

liquidsfz_lv2_DATA = manifest.ttl liquidsfz.ttl

CLEANFILES = liquidsfz.ttl liquidsfz_lv2.$(PLUGIN_EXT)
liquidsfz.ttl: $(top_builddir)/config.status

FONT = dejavu-lgc-sans.ttf
EXTRA_DIST = manifest.ttl lv2plugin.cc lv2plugin.hh lv2ui.cc filedialog.cc filedialog.hh lv2_midnam.h ldscript.map macos_export_symbols $(FONT).xz

all-local: liquidsfz_lv2.$(PLUGIN_EXT)

install-exec-hook: liquidsfz_lv2.$(PLUGIN_EXT)
	mkdir -p $(DESTDIR)$(liquidsfz_lv2dir)
	$(INSTALL_PROGRAM) liquidsfz_lv2.$(PLUGIN_EXT) "$(DESTDIR)$(liquidsfz_lv2dir)"

install-data-hook:
	mkdir -p $(DESTDIR)$(liquidsfz_lv2dir)
	xzcat $(srcdir)/$(FONT).xz > $(DESTDIR)$(liquidsfz_lv2dir)/$(FONT)

uninstall-hook:
	rm -f $(DESTDIR)$(liquidsfz_lv2dir)/liquidsfz_lv2.$(PLUGIN_EXT)
	rm -f $(DESTDIR)$(liquidsfz_lv2dir)/$(FONT)

# Library builds track their own headers and run before this directory.
# This manual compile/link rule must track the plugin's own sources / headers.
liquidsfz_lv2_deps = $(srcdir)/lv2plugin.cc $(srcdir)/lv2ui.cc \
	$(srcdir)/filedialog.cc $(srcdir)/filedialog.hh \
	$(srcdir)/lv2plugin.hh $(srcdir)/lv2_midnam.h \
	$(top_builddir)/lib/libliquidsfz.la \
	$(top_builddir)/3rdparty/libliquidsfzglui.la \
	$(top_builddir)/config.h Makefile

if COND_MACOS
liquidsfz_lv2.$(PLUGIN_EXT): $(liquidsfz_lv2_deps) $(srcdir)/macos_export_symbols
	$(CXX) -fPIC -DPIC -shared -o liquidsfz_lv2.$(PLUGIN_EXT) $(srcdir)/lv2plugin.cc $(srcdir)/lv2ui.cc $(srcdir)/filedialog.cc $(CXXFLAGS) $(AM_CXXFLAGS) \
	$(top_builddir)/lib/.libs/$(LIQUIDSFZ_PLUGIN_LIB) $(STATIC_CXX_LDFLAGS) $(LDFLAGS) $(SNDFILE_LIBS) \
	$(top_builddir)/3rdparty/.libs/libliquidsfzglui.a $(GL_LIBS) $(X11_LIBS) \
	-exported_symbols_list $(srcdir)/macos_export_symbols
else
liquidsfz_lv2.$(PLUGIN_EXT): $(liquidsfz_lv2_deps) $(srcdir)/ldscript.map
	$(CXX) -fPIC -DPIC -shared -o liquidsfz_lv2.$(PLUGIN_EXT) $(srcdir)/lv2plugin.cc $(srcdir)/lv2ui.cc $(srcdir)/filedialog.cc $(CXXFLAGS) $(AM_CXXFLAGS) \
	$(top_builddir)/lib/.libs/$(LIQUIDSFZ_PLUGIN_LIB) $(STATIC_CXX_LDFLAGS) $(LDFLAGS) $(SNDFILE_LIBS) \
	$(top_builddir)/3rdparty/.libs/libliquidsfzglui.a $(GL_LIBS) $(X11_LIBS) \
	-Wl,-rpath=$(libdir) -Wl,--version-script=$(srcdir)/ldscript.map
endif
