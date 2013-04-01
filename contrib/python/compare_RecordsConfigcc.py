#!/usr/bin/env python
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
#
import re
import sys

try:
    src_dir = sys.argv[1]
except IndexError:
    print "Usage: %s [trafficserver_source_dir]" % sys.argv[0]
    print "Compares values in RecordsConfig.cc with the default records.config file"
    sys.exit(1)

cc_re = re.compile(r'\{RECT_(?:CONFIG|LOCAL), "([^"]+)", RECD_([A-Z]+), "([^"]+)",')
cc_re_2 = re.compile(r'\{RECT_(?:CONFIG|LOCAL), "([^"]+)", RECD_([A-Z]+), (NULL),')
in_re = re.compile(r'(?:CONFIG|LOCAL) (\S+)\s+(\S+)\s+(\S+)')

# We expect these keys to differ between files, so ignore them
ignore_keys = {
    "proxy.config.ssl.server.cert.path": 1,
    "proxy.config.admin.user_id": 1,
    "proxy.config.ssl.client.cert.path": 1,
    "proxy.config.alarm_email": 1,
    "proxy.config.log.logfile_dir": 1,
    "proxy.config.ssl.CA.cert.path": 1,
    "proxy.config.ssl.server.private_key.path": 1,
    "proxy.config.ssl.client.CA.cert.path": 1,
    "proxy.config.ssl.server.private_key.path": 1,
    "proxy.config.ssl.client.CA.cert.path": 1,
    "proxy.config.config_dir": 1,
    "proxy.config.proxy_name": 1,
    "proxy.config.cluster.ethernet_interface": 1,
    "proxy.config.ssl.client.private_key.path": 1,
    "proxy.config.net.defer_accept": 1 # Specified in RecordsConfig.cc funny
}

# RecordsConfig.cc values
rc_cc = {}
# records.config.in values
rc_in = {}

# Process RecordsConfig.cc
fh = open("%s/mgmt/RecordsConfig.cc" % src_dir)
for line in fh:
    m = cc_re.search(line)
    if not m:
        m = cc_re_2.search(line)
    if m:
        rc_cc[m.group(1)] = (m.group(2), m.group(3))

fh.close()

# Process records.config.default.in
fh = open("%s/proxy/config/records.config.default.in" % src_dir)
for line in fh:
    m = in_re.match(line)
    if m:
        rc_in[m.group(1)] = (m.group(2), m.group(3))
fh.close()

# Compare the two
# If a value is in RecordsConfig.cc  and not records.config.default.in, it is
# ignored right now.
print "# RecordsConfig.cc -> records.config.default.in"
for key in rc_in:
    if key in ignore_keys:
        continue
    if key not in rc_cc:
        print "%s missing -> %s" % (key, "%s %s" % rc_in[key])
        continue
    if rc_cc[key] != rc_in[key]:
        print "%s : %s -> %s" % (key, "%s %s" % rc_cc[key], "%s %s" % rc_in[key])
