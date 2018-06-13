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

export CCACHE_BASEDIR

# TODO: There's still some weirdness in the dependencies between proxy
# and mgmt, hence we have to build proxy/hdrs first.

# proxy/api/ts has to be built first, since so much of libraries and "core
# depends on the generates ts/ts.h include file.
SUBDIRS = proxy/api/ts lib iocore proxy mgmt src plugins tools example rc

if BUILD_DOCS
SUBDIRS += doc
endif

DIST_BUILD_USER=`id -nu`
DIST_BUILD_USER_GROUP=`id -ng`
DISTCHECK_CONFIGURE_FLAGS=--with-user=${DIST_BUILD_USER} --with-group=${DIST_BUILD_USER_GROUP}

EXTRA_DIST=CHANGES INSTALL STATUS NOTICE LAYOUT LICENSE example contrib README-EC2 REVIEWERS

# Default value when creating release candidates
RC = 0

# igalic can't remember if this is make check or make test and neither should you.
test: check

installcheck-local:
	$(DESTDIR)$(bindir)/traffic_server -R 1

distclean-local:
	-rm -f config.nice

doxygen:
	@cd doc && $(MAKE) $(AM_MAKEFLAGS) $@

changelog:
	./tools/changelog.pl apache trafficserver $(VERSION) $(AUTHTOKEN) > CHANGELOG-$(VERSION)

asf-dist: asf-distdir
	tardir=$(distdir) && $(am__tar) --mtime=./configure.ac | bzip2 -9 -c >$(distdir).tar.bz2
	@$(am__remove_distdir)

asf-dist-rc: asf-distdir
	tardir=$(distdir) && $(am__tar) --mtime=./configure.ac | bzip2 -9 -c >$(distdir)-rc$(RC).tar.bz2
	@$(am__remove_distdir)

asf-distdir:
	@$(am__remove_distdir)
	test -d .git && git clone . $(distdir)
	cd $(distdir) && autoreconf -i
	$(RM) -r -- $(distdir)/autom4te.cache $(distdir)/ci `find $(distdir) -name .git -o -name .gitignore -o -name .gitmodules`

asf-dist-sign: asf-dist
	sha512sum -b $(distdir).tar.bz2 >$(distdir).tar.bz2.sha512
	gpg --armor --output $(distdir).tar.bz2.asc  --detach-sig $(distdir).tar.bz2

asf-dist-sign-rc: asf-dist-rc
	sha512sum -b $(distdir)-rc$(RC).tar.bz2 >$(distdir)-rc$(RC).tar.bz2.sha512
	gpg --armor --output $(distdir)-rc$(RC).tar.bz2.asc  --detach-sig $(distdir)-rc$(RC).tar.bz2

release: asf-dist-sign
	git tag -fs -m "Release $(VERSION)" $(VERSION)

rel-candidate: asf-dist-sign-rc
	git tag -fs -m "Release Candidate $(VERSION)-rc$(RC)" $(VERSION)-rc$(RC)

examples: all
	@cd example && $(MAKE) $(AM_MAKEFLAGS)

install-examples: examples
	@cd example && $(MAKE) $(AM_MAKEFLAGS) install pkglibdir=$(pkglibexecdir)

install-data-hook:
if BUILD_DOCS
	@cd doc && $(MAKE) $(AM_MAKEFLAGS) install-man
endif

TESTS = tools/check-unused-dependencies

rat:
	java -jar $(top_srcdir)/ci/apache-rat-0.13-SNAPSHOT.jar -E $(top_srcdir)/ci/rat-regex.txt  -d $(top_srcdir)

autopep8:
	@autopep8 -i -r $(top_srcdir)

#
# These are rules to make clang-format easy and fast to run. Run it with e.g.
#    make -j clang-format
#
# If you make changes to directory structures, you must update this as well.
#
.PHONY:	clang-format-cmd clang-format-example clang-format-iocore clang-format-lib clang-format-mgmt \
	clang-format-plugins clang-format-proxy clang-format-tools

clang-format: 	clang-format-cmd clang-format-example clang-format-iocore clang-format-lib clang-format-mgmt \
		clang-format-plugins clang-format-proxy clang-format-tools clang-format-tests

clang-format-cmd:
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/src

clang-format-example:
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/example

clang-format-iocore:
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/iocore

clang-format-lib:
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/lib/cppapi
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/lib/bindings
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/lib/records
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/lib/ts
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/lib/wccp

clang-format-mgmt:
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/mgmt

clang-format-plugins:
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/plugins

clang-format-proxy:
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/proxy

clang-format-tools:
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/tools

clang-format-tests:
	@$(top_srcdir)/tools/clang-format.sh $(top_srcdir)/tests

help:
	@echo 'all              default target for building the package'
	@echo 'asf-dist         recreate source package'
	@echo 'asf-dist-sign    recreate source package, with checksums and signature'
	@echo 'check            run the test suite, if any'
	@echo 'clang-format     run clang-format over most C and C++ files (not git subtrees)'
	@echo 'clang-tidy       run clang-tidy in fix-it mode'
	@echo 'clean            remove whatever make created'
	@echo 'dist             DEPRECATED: recreate source package'
	@echo 'distcheck        verify dist by performing VPATH build and then distclean'
	@echo 'distclean        remove whatever configure created'
	@echo 'doxygen          generate doxygen docs in doc/html dir'
	@echo 'examples         make examples'
	@echo 'help             display this list of make targets'
	@echo 'install          install by copying the built files to system-wide dirs'
	@echo 'install-examples install examples by copying the built files to system-wide dirs'
	@echo 'install-strip    same as install but then strips debugging symbols'
	@echo 'rat              produce a RAT licence compliance report of the source'
	@echo 'rel-candidate    recreate a signed relelease candidate source package and a signed git tag'
	@echo 'release          recreate a signed release source package and a signed git tag'
	@echo 'autopep8         run autopep8 over python files'
