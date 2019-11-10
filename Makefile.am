SUBDIRS = lib src
ACLOCAL_AMFLAGS = -I m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liquidsfz.pc
$(pkgconfig_DATA): $(top_builddir)/config.status
CLEANFILES = liquidsfz.pc
EXTRA_DIST = liquidsfz.pc.in

rebuild-api-docs:
	doxygen Doxyfile
	rsync -av html/ stefan@space.twc.de:public_html/liquidsfz/api
