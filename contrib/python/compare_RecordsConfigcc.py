#!/usr/bin/env python3
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
    print(f"Usage: {sys.argv[0]} [trafficserver_source_dir]")
    print("Compares values in RecordsConfig.cc with the default records.config file")
    sys.exit(1)

# We expect these keys to differ between files, so ignore them
ignore_keys = {
    "proxy.config.ssl.server.cert.path": 1,
    "proxy.config.admin.user_id": 1,
    "proxy.config.ssl.client.cert.path": 1,
    "proxy.config.log.logfile_dir": 1,
    "proxy.config.ssl.CA.cert.path": 1,
    "proxy.config.ssl.client.CA.cert.path": 1,
    "proxy.config.ssl.server.private_key.path": 1,
    "proxy.config.proxy_name": 1,
    "proxy.config.ssl.client.private_key.path": 1,
    "proxy.config.net.defer_accept": 1  # Specified in RecordsConfig.cc funny
}

rc_cc = {}  # RecordsConfig.cc values
rc_in = {}  # records.config.in values
rc_doc = {}  # documented values

# Process RecordsConfig.cc
with open(f"{src_dir}/mgmt/RecordsConfig.cc") as fh:
    cc_re = re.compile(r'\{RECT_(?:CONFIG|LOCAL), "([^"]+)", RECD_([A-Z]+), (.+?), ')
    for line in fh:
        m = cc_re.search(line)
        if m:
            value = m.group(3)
            value = value.lstrip('"')
            value = value.rstrip('"')
            rc_cc[m.group(1)] = (m.group(2), value)

# Process records.config.default.in
with open(f"{src_dir}/configs/records.config.default.in") as fh:
    in_re = re.compile(r'(?:CONFIG|LOCAL) (\S+)\s+(\S+)\s+(\S+)')
    for line in fh:
        m = in_re.match(line)
        if m:
            rc_in[m.group(1)] = (m.group(2), m.group(3))

# Process records.config documentation.
# eg. .. ts:cv:: CONFIG proxy.config.proxy_binary STRING traffic_server
with open(f"{src_dir}/doc/admin-guide/files/records.config.en.rst") as fh:
    doc_re = re.compile(r'ts:cv:: CONFIG (\S+)\s+(\S+)\s+(\S+)')
    for line in fh:
        m = doc_re.search(line)
        if m:
            rc_doc[m.group(1)] = (m.group(2), m.group(3))
            rc_doc[m.group(1)] = (m.group(2), m.group(3))

# Compare the two
# If a value is in RecordsConfig.cc  and not records.config.default.in, it is
# ignored right now.
print("# Comparing RecordsConfig.cc -> records.config.default.in")
for key in rc_in:
    if key in ignore_keys:
        continue
    if key not in rc_cc:
        print("%s missing -> %s" % (key, "%s %s" % rc_in[key]))
        continue
    if rc_cc[key] != rc_in[key]:
        print("%s : %s -> %s" % (key, "%s %s" % rc_cc[key], "%s %s" % rc_in[key]))

# Search for undocumented variables ...
missing = [k for k in rc_cc if k not in rc_doc]
if len(missing) > 0:
    print()
    print("Undocumented configuration variables:")
    for m in sorted(missing):
        print("\t%s %s" % (m, "%s %s" % rc_cc[m]))

# Search for incorrectly documented default values ...
defaults = [k for k in rc_cc if k in rc_doc and rc_cc[k] != rc_doc[k]]
if len(defaults) > 0:
    print()
    print("Incorrectly documented defaults:")
    for d in sorted(defaults):
        print("\t%s %s -> %s" % (d, "%s %s" % rc_cc[d], "%s %s" % rc_doc[d]))

# Search for stale documentation ...
stale = [k for k in rc_doc if k not in rc_cc]
if (len(stale) > 0):
    print()
    print("Stale documentation:")
    for s in sorted(stale):
        print(f"\t{s}")
