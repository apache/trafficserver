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

sysconfdir = $(pkgsysconfdir)
dist_sysconf_DATA = english.dict

SUBDIRS = agent-installer charting configure images include monitor mrtg tune

install-data-local:
	$(INSTALL) -d -o $(pkgsysuser) -g $(pkgsysgroup) $(DESTDIR)$(pkgdatadir) \
	$(DESTDIR)$(pkgdatadir)/agent-installer \
	$(DESTDIR)$(pkgdatadir)/charting \
	$(DESTDIR)$(pkgdatadir)/configure \
	$(DESTDIR)$(pkgdatadir)/configure/helper \
	$(DESTDIR)$(pkgdatadir)/images \
	$(DESTDIR)$(pkgdatadir)/include \
	$(DESTDIR)$(pkgdatadir)/monitor \
	$(DESTDIR)$(pkgdatadir)/mrtg \
	$(DESTDIR)$(pkgdatadir)/tune

uninstall-hook:
	-rm -rf $(DESTDIR)$(pkgdatadir)

webuidir = $(pkgdatadir)
dist_webui_DATA = \
	checkCookies.ink \
	enableCookies.ink \
	index.ink \
	inktomi.css \
	inktomiLarge.css \
	navigation_tree.xml \
	plugin.ink \
	rename.ink \
	resize.js \
	restart.ink \
	ssl_redirect.ink
