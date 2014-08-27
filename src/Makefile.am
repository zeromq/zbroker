lib_LTLIBRARIES = libzbroker.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libzbroker.pc

include_HEADERS = \
	../include/zbroker.h \
	../include/zpipes_msg.h \
	../include/zpipes_server.h \
	../include/zpipes_client.h

libzbroker_la_SOURCES = \
	zpipes_msg.c \
	zpipes_client.c \
	zpipes_server.c \
	zpipes_server_engine.h

AM_CFLAGS = -g
AM_CPPFLAGS = -I$(top_srcdir)/include
bin_PROGRAMS = zbroker
noinst_PROGRAMS = zbroker_selftest zpipes_test_cluster zpipes_dump

zbroker_LDADD = libzbroker.la
zbroker_selftest_LDADD = libzbroker.la
zpipes_test_cluster_LDADD = libzbroker.la
zpipes_dump_LDADD  = libzbroker.la

libzbroker_la_LDFLAGS = -version-info @LTVER@

TESTS = zbroker_selftest zpipes_test_cluster

# Produce generated models; do this manually in src directory
code:
	gsl -q zpipes_msg.xml
	gsl -q zpipes_server.xml
