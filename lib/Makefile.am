# lib Makefile.am
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

SUBDIRS = ts records . tsconfig bindings cppapi

if BUILD_PERL_LIB
SUBDIRS += perl
endif

if BUILD_WCCP
SUBDIRS += wccp
endif

LOCAL =

if BUILD_LUAJIT
LOCAL += all-luajit
endif

if BUILD_YAML_CPP
LOCAL += yamlcpp
endif

# Support verbose LuaJIT builds with "make V=1".
luajit__v_ = $(luajit__v_$(AM_DEFAULT_VERBOSITY))
luajit__v_0 = @
luajit__v_1 =

# Some special hacks around building the luajit.
#
#  - Copy the luajit source tree if we're doing out-of-source builds
#  - Upon clean, remove the luajit tree from the out-of-source build tree
#
# Note: The LUAJIT_LDFLAGS is a bit of a hack, since LuaJIT on OmniOS needs
#       the -m64 flag. See configure.ac.
#
all-local:	$(LOCAL)

all-luajit:
	@echo "Making all in luajit"
	test -d "$(top_builddir)/$(subdir)/luajit/src" || cp -rf "$(srcdir)/luajit" "$(top_builddir)/$(subdir)/"
	cd luajit && $(MAKE) $(AM_MAKEFLAGS) \
		BUILDMODE="static" \
		PREFIX="$(prefix)" \
		CC="$(CC)" \
		CFLAGS="$(LUAJIT_CFLAGS)" \
		XCFLAGS="" \
		LDFLAGS="$(LUAJIT_LDFLAGS)" \
		Q=$(luajit__v_$(V))
	$(MAKE) -C yamlcpp

clean-local:
	test "$(top_srcdir)" != "$(top_builddir)" || (cd "$(top_builddir)/$(subdir)/luajit" && $(MAKE) CC="$(CC)" clean)
	test "$(top_srcdir)" = "$(top_builddir)" || rm -rf "$(top_builddir)/$(subdir)/luajit"
	$(MAKE) -C yamlcpp clean
