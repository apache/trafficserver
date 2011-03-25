# Makefile.am for config
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

SUBDIRS = body_factory
EXTRA_DIST = \
  records.config.default.in \
  storage.config.default.in

sysconfdir = $(pkgsysconfdir)
nodist_sysconf_DATA = \
  records.config.default \
  storage.config.default

webui_conf = \
  admin_access.config.default

dist_sysconf_DATA =	\
  ae_ua.config.default \
  cache.config.default \
  cluster.config.default \
  congestion.config.default \
  hosting.config.default \
  icp.config.default \
  ip_allow.config.default \
  log_hosts.config.default \
  logs_xml.config.default \
  mgmt_allow.config.default \
  mgr.cnf.default \
  parent.config.default \
  partition.config.default \
  plugin.config.default \
  plugin.db.default \
  remap.config.default \
  socks.config.default \
  splitdns.config.default \
  ssl_multicert.config.default \
  stats.config.xml.default \
  update.config.default \
  vaddrs.config.default

install-exec-hook:
	for dfltcfgfile in $(dist_sysconf_DATA) $(nodist_sysconf_DATA) ; \
	do \
		cfgfile=`echo $$dfltcfgfile | sed 's/\.default$$//'` ; \
		test -f $(sysconfdir)/$$cfgfile || mv $(sysconfdir)/$$dfltcfgfile $(sysconfdir)/$$cfgfile ; \
	done

if BUILD_WEBUI
  dist_sysconf_DATA += $(webui_conf)
endif
