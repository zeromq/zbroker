lib_LTLIBRARIES = libzpipes.la
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libzpipes.pc

include_HEADERS = ../include/zpipes.h ../include/zpipes_client.h

libzpipes_la_SOURCES = zpipes_agent.c zpipes.c zpipes_client.c

AM_CFLAGS = -g
AM_CPPFLAGS = -I$(top_srcdir)/include
bin_PROGRAMS = zpipes_selftest

zpipes_selftest_LDADD = libzpipes.la
zpipes_selftest_SOURCES = zpipes_selftest.c

libzpipes_la_LDFLAGS = -version-info @LTVER@

TESTS = zpipes_selftest
