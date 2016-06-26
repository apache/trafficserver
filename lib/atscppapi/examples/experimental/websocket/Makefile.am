AM_CPPFLAGS = -I$(top_srcdir)/lib/atscppapi/src/include

target = WebSocket.so
pkglibdir = ${pkglibexecdir}
pkglib_LTLIBRARIES = WebSocket.la

WebSocket_la_SOURCES = \
	WSBuffer.cc \
	WebSocket.cc

WebSocket_la_LDFLAGS = -module -avoid-version -shared -L$(top_srcdir)/lib/atscppapi/src/ -latscppapi

all:
	ln -sf .libs/$(target)

clean-local:
	rm -f $(target)
