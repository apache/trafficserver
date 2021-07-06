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

use Apache::TS::Config::Records;

############################################################################
# Simple script, to show some minimum configuration changes typical for
# a forward proxy.
my $fn = $ARGV[0] || "/usr/local/etc/trafficserver/records.config";
my $recedit = new Apache::TS::Config::Records(file => $fn);

# Definitely tweak the memory config
$recedit->set(conf => "proxy.config.cache.ram_cache.size", val => "2048M");

# These puts the server in forward proxy mode only.
$recedit->set(conf => "proxy.config.url_remap.remap_required", val => "0");
$recedit->set(conf => "proxy.config.reverse_proxy.enabled",    val => "0");

# Fine tuning, you might or might not want these
$recedit->set(conf => "proxy.config.http.transaction_active_timeout_in", val => "1800");
$recedit->set(conf => "proxy.config.dns.dedicated_thread",               val => "1");
$recedit->set(conf => "proxy.config.http.normalize_ae_gzip",             val => "1");

# Write out the new config file (this won't overwrite your config
$recedit->write(file => "$fn.new");
