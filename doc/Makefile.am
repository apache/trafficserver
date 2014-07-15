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

trafficshelldir = $(pkgdocdir)/trafficshell

.PHONY: help dirhtml singlehtml epub latex man changes linkcheck doxygen

dist_trafficshell_DATA = \
  man/config_alarms.1 \
  man/config_cache.1 \
  man/config_dns.1 \
  man/config_get.1 \
  man/config_hard-restart.1 \
  man/config_hostdb.1 \
  man/config_http.1 \
  man/config_icp.1 \
  man/config_logging.1 \
  man/config_name.1 \
  man/config_network.1 \
  man/config_parent.1 \
  man/config_port-tunnels.1 \
  man/config_remap.1 \
  man/config_reset-stats.1 \
  man/config_restart.1 \
  man/config_root.1 \
  man/config_scheduled-update.1 \
  man/config_security.1 \
  man/config_set.1 \
  man/config_socks.1 \
  man/config_ssl.1 \
  man/config_start.1 \
  man/config_stop.1 \
  man/config_upgrade.1 \
  man/config_virtual-ip.1 \
  man/disable.1 \
  man/enable.1 \
  man/exit.1 \
  man/show_alarms.1 \
  man/show_cache.1 \
  man/show_cache-stats.1 \
  man/show_cluster.1 \
  man/show_dns-resolver.1 \
  man/show_dns-stats.1 \
  man/show_hostdb.1 \
  man/show_hostdb-stats.1 \
  man/show_http.1 \
  man/show_http-stats.1 \
  man/show_http-trans-stats.1 \
  man/show_icp.1 \
  man/show_icp-stats.1 \
  man/show_logging.1 \
  man/show_logging-stats.1 \
  man/show_network.1 \
  man/show_parent.1 \
  man/show_port-tunnels.1 \
  man/show_proxy.1 \
  man/show_proxy-stats.1 \
  man/show_remap.1 \
  man/show_scheduled-update.1 \
  man/show_security.1 \
  man/show_socks.1 \
  man/show_ssl.1 \
  man/show_status.1 \
  man/show_version.1 \
  man/show_virtual-ip.1

if BUILD_MANPAGES

man1_MANS = $(TS_MAN1_MANPAGES) man/traffic_shell.1
man3_MANS = $(TS_MAN3_MANPAGES)
man5_MANS = $(TS_MAN5_MANPAGES)
man8_MANS = $(TS_MAN8_MANPAGES)

$(man1_MANS) $(man3_MANS) $(man5_MANS) $(man8_MANS): man

# Hook the 'all' target so that the man pages get generated in the "all" target, prior
# to "make install". If we leave it to "make install" time, then the man pages are likely
# to me generated as root.
all-am: $(man1_MANS) $(man3_MANS) $(man5_MANS) $(man8_MANS)

endif

EXTRA_DIST = \
  Doxyfile.in \
  $(dist_trafficshell_DATA)

clean-local:
	-rm -rf html warn.log
	-rm -rf $(BUILDDIR)/doctrees $(BUILDDIR)/html $(BUILDDIR)/dirhtml $(BUILDDIR)/singlehtml $(BUILDDIR)/epub $(BUILDDIR)/latex $(BUILDDIR)/man

doxygen: Doxyfile
	$(DOXYGEN)

# Makefile for Sphinx documentation
#

# You can set these variables from the command line.
PAPER         = letter
BUILDDIR      = docbuild

# Internal variables.
PAPEROPT_a4     = -D latex_paper_size=a4
PAPEROPT_letter = -D latex_paper_size=letter
ALLSPHINXOPTS   = $(SPHINXOPTS)
# the i18n builder cannot share the environment and doctrees with the others
I18NSPHINXOPTS  = $(SPHINXOPTS)

# The PAPER setting variables requires recursive make variable expansion, which automake
# detects as non-portable. We bounce this through a shell script and do the expansion there.
SBUILD = PAPEROPT_a4="$(PAPEROPT_a4)" PAPEROPT_letter="$(PAPEROPT_letter)" PAPER="$(PAPER)" \
	 $(srcdir)/sbuild $(SPHINXBUILD) \
	 -D version=$(TS_VERSION_STRING) \
	 -D release=$(TS_VERSION_STRING) \
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
