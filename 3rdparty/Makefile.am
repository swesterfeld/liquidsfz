EXTRA_DIST = imgui/imgui.cpp imgui/imgui.h imgui/imconfig.h imgui/imgui_internal.h \
             imgui/imgui_tables.cpp imgui/imgui_widgets.cpp imgui/imgui_draw.cpp \
             imgui/imstb_textedit.h imgui/imstb_rectpack.h imgui/imstb_truetype.h \
             imgui/backends/imgui_impl_opengl3.cpp imgui/backends/imgui_impl_opengl3.h \
             imgui/backends/imgui_impl_opengl3_loader.h \
             pugl/include/pugl/gl.h pugl/include/pugl/attributes.h pugl/include/pugl/pugl.h \
             pugl/src/attributes.h pugl/src/stub.h pugl/src/internal.h pugl/src/platform.h \
             pugl/src/x11.h pugl/src/macros.h pugl/src/types.h pugl/src/internal.c \
             pugl/src/x11.c pugl/src/common.c pugl/src/x11_gl.c

noinst_LTLIBRARIES = libliquidsfzglui.la

libliquidsfzglui_la_SOURCES = pugl_all.c imgui_all.cc
libliquidsfzglui_la_CFLAGS = -I$(srcdir)/pugl/include
libliquidsfzglui_la_CXXFLAGS = -I$(srcdir)/imgui
