#
# Copyright (c) 2013 LinkedIn Corp. All rights reserved. 
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
# except in compliance with the License. You may obtain a copy of the license at
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the
# License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
# either express or implied.
#

AM_CPPFLAGS = -I$(top_srcdir)/src/include

target=intercept.so
pkglibdir = ${pkglibexecdir}
pkglib_LTLIBRARIES = intercept.la
intercept_la_SOURCES = intercept.cc
intercept_la_LDFLAGS = -module -avoid-version -shared -L$(top_srcdir) -latscppapi

all:
	ln -sf .libs/$(target)

clean-local:
	rm -f $(target)
