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

all-local: Makefile-pl
	$(MAKE) -f Makefile-pl INSTALLDIRS=$(INSTALLDIRS) PREFIX=$(prefix) DESTDIR=$(DESTDIR)

install-exec-local: Makefile-pl
	$(MAKE) -f Makefile-pl INSTALLDIRS=$(INSTALLDIRS) PREFIX=$(prefix) DESTDIR=$(DESTDIR) install

# The perl build needs to have the source files in the current working directory, so we need to
# copy them to the build directory if we are building out of tree.
Makefile-pl: Makefile.PL
	test -f "$(top_builddir)/$(subdir)/Makefile.PL" || cp -rf "$(srcdir)/." "$(top_builddir)/$(subdir)/"
	$(PERL) Makefile.PL INSTALLDIRS=$(INSTALLDIRS) INSTALL_BASE=$(prefix) PREFIX=

distclean-local:
	-rm -rf Makefile-pl MYMETA.* blip

#test: check

#check-local: Makefile-pl
#	$(MAKE) -f Makefile-pl INSTALLDIRS=$(INSTALLDIRS) PREFIX=$(prefix) DESTDIR=$(DESTDIR) test
