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

.PHONY: help dirhtml singlehtml epub latex man changes linkcheck doxygen

if BUILD_MANPAGES

man1_MANS = $(TS_MAN1_MANPAGES)
man3_MANS = $(TS_MAN3_MANPAGES)
man5_MANS = $(TS_MAN5_MANPAGES)
man8_MANS = $(TS_MAN8_MANPAGES)

$(man1_MANS) $(man3_MANS) $(man5_MANS) $(man8_MANS): man

# Hook the 'all' target so that the man pages get generated in the "all" target, prior
# to "make install". If we leave it to "make install" time, then the man pages are likely
# to me generated as root.
if BUILD_DOCS
SUBDIRS = uml
all-am: $(man1_MANS) $(man3_MANS) $(man5_MANS) $(man8_MANS)
endif

endif

clean-local:
	-rm -rf $(BUILDDIR)/* _build/html/* xml

doxygen: Doxyfile
	$(DOXYGEN)

# Makefile for Sphinx documentation
#

# You can set these variables from the command line.
PAPER         = letter
BUILDDIR      = docbuild

# [amc] LaTex apparently doesn't work as of Sphinx 1.6.1
# see https://media.readthedocs.org/pdf/sphinx/1.6.3/sphinx.pdf
# section 24.3.2 around page 247, third item for 'NotImplementedError', so this is kind of useless.

ALLSPHINXOPTS   = $(SPHINXOPTS)
# the i18n builder cannot share the environment and doctrees with the others
I18NSPHINXOPTS  = $(SPHINXOPTS)

# The PAPER setting variables requires recursive make variable expansion, which automake
# detects as non-portable. We bounce this through a shell script and do the expansion there.
SBUILD = PAPEROPT_a4="$(PAPEROPT_a4)" PAPEROPT_letter="$(PAPEROPT_letter)" PAPER="$(PAPER)" \
	PLANTUML_JAR="$(shell ext/plantuml_fetch.sh | tail -1)" \
	$(srcdir)/sbuild "$(SPHINXBUILD)" \
	-c $(srcdir) \
	$(ALLSPHINXOPTS)

help:
	@echo "Please use \`make <target>' where <target> is one of"
	@echo "  html       to make standalone HTML files"
	@echo "  dirhtml    to make HTML files named index.html in directories"
	@echo "  singlehtml to make a single large HTML file"
	@echo "  epub       to make an epub"
	@echo "  latex      to make LaTeX files, you can set PAPER=a4 or PAPER=letter"
	@echo "  latexpdf   to make LaTeX files and run them through pdflatex"
	@echo "  man        to make manual pages"
	@echo "  changes    to make an overview of all changed/added/deprecated items"
	@echo "  linkcheck  to check all external links for integrity"

html-local:
	$(SBUILD) -d $(BUILDDIR)/doctrees -b html $(srcdir) $(BUILDDIR)/html
	@echo
	@echo "Build finished. The HTML pages are in $(BUILDDIR)/html."

dirhtml:
	$(SBUILD) -d $(BUILDDIR)/doctrees -b dirhtml $(srcdir) $(BUILDDIR)/html
	@echo
	@echo "Build finished. The HTML pages are in $(BUILDDIR)/dirhtml."

singlehtml:
	$(SBUILD) -d $(BUILDDIR)/doctrees -b singlehtml $(srcdir) $(BUILDDIR)/singlehtml
	@echo
	@echo "Build finished. The HTML page is in $(BUILDDIR)/singlehtml."

epub:
	$(SBUILD) -d $(BUILDDIR)/doctrees -b epub $(srcdir) $(BUILDDIR)/epub
	@echo
	@echo "Build finished. The epub file is in $(BUILDDIR)/epub."

latex:
	$(SBUILD) -d $(BUILDDIR)/doctrees -b latex $(srcdir) $(BUILDDIR)/latex
	@echo
	@echo "Build finished. The epub file is in $(BUILDDIR)/latex."

man:
	$(SBUILD) -d $(BUILDDIR)/doctrees -b man $(srcdir) $(BUILDDIR)/man
	@echo
	@echo "Build finished. The man pages are in $(BUILDDIR)/man."

changes:
	@echo FIXME

linkcheck:
	@echo FIXME
