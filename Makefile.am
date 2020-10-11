SUBDIRS = lib lv2 tests

if COND_WITH_JACK
SUBDIRS += src
endif

AUTOMAKE_OPTIONS = 1.9 dist-bzip2 no-dist-gzip
ACLOCAL_AMFLAGS = -I m4 -Wno-portability

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = liquidsfz.pc
$(pkgconfig_DATA): $(top_builddir)/config.status
CLEANFILES = liquidsfz.pc
EXTRA_DIST = liquidsfz.pc.in TODO LICENSE README.md Doxyfile

rebuild-api-docs:
	doxygen Doxyfile
	rsync -av html/ stefan@space.twc.de:public_html/liquidsfz/api-$(VERSION)
