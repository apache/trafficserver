#!/usr/bin/perl
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

use lib '/opt/ats/share/perl5';
use Apache::TS::Config::Records;
use File::Copy;

#chdir("/usr/local");
chdir("/opt/ats");

my $recedit = new Apache::TS::Config::Records(file => "etc/trafficserver/records.config.default");

$recedit->append(line => "");
$recedit->append(line => "# My local stuff");
#$recedit->append(line => "CONFIG proxy.config.crash_log_helper STRING /home/admin/bin/invoker_wrap.sh");

# Port setup
$recedit->set(conf => "proxy.config.http.server_ports", val => "80 80:ipv6 443:ssl 443:ipv6:ssl");

# Threads
$recedit->set(conf => "proxy.config.exec_thread.autoconfig.enabled", val => "0");
$recedit->set(conf => "proxy.config.exec_thread.limit", val => "8");
$recedit->set(conf => "proxy.config.cache.threads_per_disk", val => "8");
$recedit->set(conf => "proxy.config.accept_threads", val => "0");
$recedit->set(conf => "proxy.config.exec_thread.affinity", val => "1");

# TLS
$recedit->set(conf => "proxy.config.ssl.hsts_max_age", val => "17280000");
#$recedit->set(conf => "proxy.config.ssl.max_record_size", val => "-1");
$recedit->set(conf => "proxy.config.ssl.session_cache.value", val => "2");
$recedit->set(conf => "proxy.config.ssl.ocsp.enabled", val => "1");
$recedit->set(conf => "proxy.config.http2.stream_priority_enabled", val => "1");
$recedit->set(conf => "proxy.config.ssl.max_record_size", val => "-1");
$recedit->set(conf => "proxy.config.ssl.server.max_early_data", val => "16384");

# Cache setup
$recedit->set(conf => "proxy.config.cache.ram_cache.size", val => "1536M");
$recedit->set(conf => "proxy.config.cache.ram_cache_cutoff", val => "4M");
$recedit->set(conf => "proxy.config.cache.limits.http.max_alts", val => "4");
$recedit->set(conf => "proxy.config.cache.dir.sync_frequency", val => "600"); # 10 minutes intervals
$recedit->set(conf => "proxy.config.http.cache.ignore_client_cc_max_age", val => "1");
$recedit->set(conf => "proxy.config.allocator.hugepages", val => "1");

# HTTP caching related stuff
$recedit->set(conf => "proxy.config.http.normalize_ae", val => "2");
$recedit->set(conf => "proxy.config.http.cache.required_headers", val => "1");
$recedit->set(conf => "proxy.config.http.insert_request_via_str", val => "1");
$recedit->set(conf => "proxy.config.http.insert_response_via_str", val => "2");
$recedit->set(conf => "proxy.config.http.negative_caching_enabled", val => "1");
$recedit->set(conf => "proxy.config.http.negative_caching_lifetime", val => "60");
$recedit->set(conf => "proxy.config.http.chunking.size", val => "64k");
$recedit->set(conf => "proxy.config.url_remap.pristine_host_hdr", val => "1");
$recedit->set(conf => "proxy.config.http.cache.open_write_fail_action", val => "2");

# Timeouts
$recedit->set(conf => "proxy.config.http.keep_alive_no_activity_timeout_in", val => "300");
$recedit->set(conf => "proxy.config.http.keep_alive_no_activity_timeout_out", val => "300");
$recedit->set(conf => "proxy.config.http.transaction_no_activity_timeout_out", val => "180");
$recedit->set(conf => "proxy.config.http.transaction_no_activity_timeout_in", val => "180");
$recedit->set(conf => "proxy.config.http.transaction_active_timeout_in", val => "180");
$recedit->set(conf => "proxy.config.http.transaction_active_timeout_out", val => "180");
$recedit->set(conf => "proxy.config.http.accept_no_activity_timeout", val => "30");

# DNS / HostDB
$recedit->set(conf => "proxy.config.cache.hostdb.sync_frequency",  val => "0");

# Logging
$recedit->set(conf => "proxy.config.log.logging_enabled", val => "3");
$recedit->set(conf => "proxy.config.log.max_space_mb_for_logs",  val => "4096");
$recedit->set(conf => "proxy.config.log.max_space_mb_headroom",  val => "64");

# Network
$recedit->set(conf => "proxy.config.net.connections_throttle", val => "10000");
#$recedit->set(conf => "proxy.config.net.sock_send_buffer_size_in", val => "2M");
#$recedit->set(conf => "proxy.config.net.sock_recv_buffer_size_out", val => "2M");
$recedit->set(conf => "proxy.config.net.poll_timeout",  val => "30");

# Local additions (typically not found in the records.config.default)
$recedit->set(conf => "proxy.config.dns.dedicated_thread", val => "0");
$recedit->set(conf => "proxy.config.http_ui_enabled", val => "3");
$recedit->set(conf => "proxy.config.http.server_max_connections", val =>"250");
$recedit->set(conf => "proxy.config.http.allow_multi_range", val =>"0");

#$recedit->set(conf => "proxy.config.mlock_enabled", val => "2");

# Write it all out
$recedit->write(file => "etc/trafficserver/records.config");
