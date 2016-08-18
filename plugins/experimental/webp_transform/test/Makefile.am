AM_CPPFLAGS = -I$(top_builddir)/lib/atscppapi/mocks/ -I../ -I.
bin_PROGRAMS = unit_test

unit_test_SOURCES = Magick++.cc ImageTransform.cc unit_test.cc
unit_test_LDADD = -L$(top_builddir)/lib/atscppapi/mocks/ -latscppapi_mock -lgtest -lgmock -lpthread

test::
	./$(bin_PROGRAMS)
