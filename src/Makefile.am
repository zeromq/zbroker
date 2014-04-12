lib_LTLIBRARIES = libzbroker.la libzpipesclient.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libzbroker.pc libzpipesclient.pc

include_HEADERS = \
	../include/zbroker.h \
	../include/zpipes_msg.h \
	../include/zpipes_server.h \
	../include/zpipes_client.h

libzbroker_la_SOURCES = \
	zpipes_msg.c \
	zpipes_server.c \
	zpipes_server_engine.h
	
libzpipesclient_la_SOURCES = \
	zpipes_client.c

AM_CFLAGS = -g
AM_CPPFLAGS = -I$(top_srcdir)/include
bin_PROGRAMS = zbroker zbroker_selftest zpipes_test_cluster

zbroker_selftest_LDADD = libzbroker.la libzpipesclient.la
zpipes_test_cluster_LDADD = libzbroker.la libzpipesclient.la

zbroker_LDADD = libzbroker.la

libzbroker_la_LDFLAGS = -version-info @LTVER@
libzpipesclient_la_LDFLAGS = -version-info @CLIENT_LTVER@

TESTS = zbroker_selftest zpipes_test_cluster

# Produce generated models; do this manually in src directory
code:
	gsl -q zpipes_msg.xml
	gsl -q -animate:1 zpipes_server.xml
