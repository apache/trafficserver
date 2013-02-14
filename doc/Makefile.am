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

dist_trafficshell_DATA = \
  man/config_alarms.1 \
  man/config_cache.1 \
  man/config_clock.1 \
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

man1_MANS = \
  man/traffic_shell.1

man3_MANS = \
  sdk/TSAPI.3 \
  sdk/TSDebug.3 \
  sdk/TSHttpHookAdd.3 \
  sdk/TSHttpParserCreate.3 \
  sdk/TSIOBufferCreate.3 \
  sdk/TSInstallDirGet.3 \
  sdk/TSMBufferCreate.3 \
  sdk/TSMalloc.3 \
  sdk/TSPluginInit.3 \
  sdk/TSRemap.3 \
  sdk/TSTrafficServerVersionGet.3 \
  sdk/TSUrlCreate.3

EXTRA_DIST = \
  Doxyfile.in \
  $(dist_trafficshell_DATA)

clean-local:
	-rm -rf html warn.log

doxygen: Doxyfile
	$(DOXYGEN)
