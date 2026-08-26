#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

experimental_magick_magick_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBMAGICKCPP_CFLAGS)
experimental_magick_magick_la_CXXFLAGS = $(AM_CXXFLAGS) -Wno-unused-variable

pkglib_LTLIBRARIES += experimental/magick/magick.la

experimental_magick_magick_la_SOURCES = \
  experimental/magick/magick.cc \
  experimental/magick/magick_args.cc

experimental_magick_magick_la_LDFLAGS = \
  $(AM_LDFLAGS) \
  -L$(top_builddir)/src/tscpp/api

experimental_magick_magick_la_LIBADD = \
  -ltscppapi \
  $(LIBMAGICKCPP_LIBS)

# magick_args.cc depends only on <string>/<vector>, not on ImageMagick, so the
# argument validator is unit testable even where ImageMagick is absent.
check_PROGRAMS += experimental/magick/test_magick_args

experimental_magick_test_magick_args_CPPFLAGS = $(AM_CPPFLAGS) -I$(abs_top_srcdir)/tests/include

experimental_magick_test_magick_args_SOURCES = \
  experimental/magick/unit_tests/test_magick_args.cc \
  experimental/magick/magick_args.cc
