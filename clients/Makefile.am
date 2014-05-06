lib_LTLIBRARIES = libzbroker_cli.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libzbroker_cli.pc

include_HEADERS = \
	zpipes_client.h

libzbroker_cli_la_SOURCES = \
	zchunk.c \
	zchunk.h \
	zpipes_msg.h \
	zpipes_msg.c \
	zpipes_client.c

AM_CFLAGS = -g
AM_CPPFLAGS = 
noinst_PROGRAMS = selftest

selftest_SOURCES = selftest.c
selftest_LDADD = libzbroker_cli.la -lzmtp

libzbroker_cli_la_LDFLAGS = -version-info @LTVER@

# Selftest isn't working yet
# TESTS = selftest

# Produce generated models; do this manually in src directory
code:
	gsl -q zpipes_msg.xml
