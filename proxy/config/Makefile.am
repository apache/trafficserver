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
  records.config.in \
  storage.config.in

sysconfdir = $(pkgsysconfdir)
nodist_sysconf_DATA = \
  records.config \
  storage.config

dist_sysconf_DATA =	\
  admin_access.config \
  ae_ua.config \
  cache.config \
  cluster.config \
  congestion.config \
  filter.config \
  hosting.config \
  icp.config \
  ip_allow.config \
  log_hosts.config \
  logs.config \
  logs_xml.config \
  mgmt_allow.config \
  mgr.cnf \
  parent.config \
  partition.config \
  plugin.config \
  plugin.db \
  remap.config \
  socks.config \
  splitdns.config \
  ssl_multicert.config \
  stats.config.xml \
  update.config \
  vaddrs.config

