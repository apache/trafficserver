#
# Top-level Makefile.am for Traffic Server.
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

# Local Macros
# http://www.gnu.org/software/automake/manual/automake.html#Local-Macros
ACLOCAL_AMFLAGS = -I build

# TODO: There's still some weirdness in the dependencies between proxy
# and mgmt, hence we have to build proxy/hdrs first.

# proxy/api/ts has to be built first, since so much of libraries and "core
# depends on the generates ts/ts.h include file.
if STANDALONE_IOCORE
SUBDIRS =  proxy/api/ts iocore lib proxy/hdrs mgmt proxy cop plugins tools
else
SUBDIRS =  proxy/api/ts iocore lib proxy/hdrs mgmt proxy cop rc doc plugins tools
endif

DIST_BUILD_USER=`id -nu`
DIST_BUILD_USER_GROUP=`id -ng`
DISTCHECK_CONFIGURE_FLAGS=--with-user=${DIST_BUILD_USER} --with-group=${DIST_BUILD_USER_GROUP}

EXTRA_DIST=CHANGES INSTALL STATUS NOTICE LAYOUT LICENSE example contrib README-EC2 REVIEWERS

installcheck-local:
	$(DESTDIR)$(bindir)/traffic_server -R 1

doxygen:
	@cd doc && $(MAKE) $(AM_MAKEFLAGS) $@

asf-distdir:
	@$(am__remove_distdir)
	test -d .git && git clone . $(distdir) || svn export . $(distdir)
	cd $(distdir) && autoreconf -i
	rm -rf -- $(distdir)/autom4te.cache $(distdir)/.git $(distdir)/.gitignore

asf-dist: asf-distdir
	tardir=$(distdir) && $(am__tar) | bzip2 -9 -c >$(distdir).tar.bz2
	@$(am__remove_distdir)

asf-dist-sign: asf-dist
	md5sum -b $(distdir).tar.bz2 >$(distdir).tar.bz2.md5
	sha1sum -b $(distdir).tar.bz2 >$(distdir).tar.bz2.sha1
	gpg --armor --output $(distdir).tar.bz2.asc  --detach-sig $(distdir).tar.bz2

examples: all
	@cd example && $(MAKE) $(AM_MAKEFLAGS)

install-examples: examples
	@cd example && $(MAKE) $(AM_MAKEFLAGS) install pkglibdir=$(pkglibexecdir)

install-data-hook:
	@cd doc && $(MAKE) $(AM_MAKEFLAGS) install-man

help:
	@echo 'all              default target for building the package' && \
   echo 'check            run the test suite, if any' && \
   echo 'clean            remove whatever make created' && \
   echo 'distclean        remove whatever configure created' && \
   echo 'dist             DEPRECATED: recreate source package' && \
   echo 'examples         make examples' && \
   echo 'asf-dist         recreate source package' && \
   echo 'asf-dist-sign    recreate source package, with checksums and signature' && \
   echo 'distcheck        verify dist by performing VPATH build and then distclean' && \
   echo 'doxygen          generate doxygen docs in doc/html dir' && \
   echo 'help             display this list of make targets' && \
   echo 'install          install by copying the built files to system-wide dirs' && \
   echo 'install-strip    same as install but then strips debugging symbols' && \
   echo 'install-examples install examples by copying the built files to system-wide dirs'

