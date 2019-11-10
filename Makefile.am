SUBDIRS = lib src
ACLOCAL_AMFLAGS = -I m4

rebuild-api-docs:
	doxygen Doxyfile
	rsync -av html/ stefan@space.twc.de:public_html/liquidsfz/api
