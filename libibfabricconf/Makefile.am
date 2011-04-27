INCLUDES = -I$(srcdir)/include -I$(includedir) -I$(includedir)/infiniband

lib_LTLIBRARIES = libibfabricconf.la
sbin_PROGRAMS = src/ibparsefabricconf
sbin_SCRIPTS = scripts/ibverifyfabricconfigs

if DEBUG
DBGFLAGS = -ggdb -D_DEBUG_
else
DBGFLAGS =
endif

if HAVE_LD_VERSION_SCRIPT
libibfabricconf_version_script = -Wl,--version-script=$(srcdir)/libibfabricconf.map
else
libibfabricconf_version_script =
endif

libibfabricconf_la_SOURCES = src/ibfabricconf.c
libibfabricconf_la_CFLAGS = $(XML_CFLAGS) -Wall $(DBGFLAGS)
libibfabricconf_la_LDFLAGS = -version-info $(ibfabricconf_api_version) \
	-export-dynamic $(libibfabricconf_version_script) \
	-libmad $(XML_LIBS) -lz -lm
libibfabricconf_la_DEPENDENCIES = $(srcdir)/libibfabricconf.map

libibfabricconfincludedir = $(includedir)/infiniband

libibfabricconfinclude_HEADERS = $(srcdir)/include/infiniband/ibfabricconf.h


src_ibparsefabricconf_SOURCES = src/ibparsefabricconf.c
src_ibparsefabricconf_CFLAGS = -I$(includedir)
src_ibparsefabricconf_LDFLAGS = -libfabricconf


man_MANS = man/ibfabricconf.3 \
			man/ibparsefabricconf.8

TESTS = tests/verifychassisconfigs.sh \
	tests/verifytestfabricconf.sh

EXTRA_DIST = $(srcdir)/libibfabricconf.map \
	$(srcdir)/libibfabricconf.ver $(man_MANS) \
	chassis_fabricconfs \
	$(srcdir)/schema/*

install-exec-hook:
	if test ! -d $(DESTDIR)/$(sysconfdir)/infiniband-diags; then \
		$(top_srcdir)/config/install-sh -m 755 -d $(DESTDIR)/$(sysconfdir)/infiniband-diags; \
	fi
	if test ! -d $(DESTDIR)/$(sysconfdir)/infiniband-diags/chassis_fabricconfs; then \
		$(top_srcdir)/config/install-sh -m 755 -d $(DESTDIR)/$(sysconfdir)/infiniband-diags/chassis_fabricconfs; \
	fi
	for file in $(top_builddir)/libibfabricconf/chassis_fabricconfs/*.xml; do \
		$(top_srcdir)/config/install-sh -m 644 $$file $(DESTDIR)/$(sysconfdir)/infiniband-diags/chassis_fabricconfs; \
	done
	if test ! -d $(DESTDIR)/$(sysconfdir)/infiniband-diags/schema; then \
		$(top_srcdir)/config/install-sh -m 755 -d $(DESTDIR)/$(sysconfdir)/infiniband-diags/schema; \
	fi
	for file in $(top_builddir)/libibfabricconf/schema/*.xsd; do \
		$(top_srcdir)/config/install-sh -m 644 $$file $(DESTDIR)/$(sysconfdir)/infiniband-diags/schema; \
	done
