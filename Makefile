#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

TSXS?=tsxs
CXXFLAGS=-O3 -Wall

all:	header_filter.cc rules.cc
	$(TSXS) -v -C $? -o header_filter.so

install:
	$(TSXS) -i -o header_filter.so

clean:
	rm -f *.lo *.so *.bz2 *.asc *.md5 *.sha1


# Don't touch
PACKAGE=header_filter
VERSION=1.0
distdir = $(PACKAGE)-$(VERSION)
remove_distdir = \
  { test ! -d "$(distdir)" \
    || { find "$(distdir)" -type d ! -perm -200 -exec chmod u+w {} ';' \
         && rm -fr "$(distdir)"; }; }

asf-distdir:
	@$(remove_distdir)
	svn export . $(distdir)

asf-dist: asf-distdir
	tar chof - $(distdir) | bzip2 -9 -c >$(distdir).tar.bz2
	@$(remove_distdir)

asf-dist-sign: asf-dist
	md5sum -b $(distdir).tar.bz2 >$(distdir).tar.bz2.md5
	sha1sum -b $(distdir).tar.bz2 >$(distdir).tar.bz2.sha1
	gpg2 --armor --output $(distdir).tar.bz2.asc  --detach-sig $(distdir).tar.bz2
