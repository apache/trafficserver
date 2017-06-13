#!/usr/bin/python
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

# Author: opensource@navyaprabha.com
# Description: Sample script to add keys to memcached for use with YTS/memcached_remap plugin

import memcache

# connect to local server
mc = memcache.Client(['127.0.0.1:11211'], debug=0)

# Add couple of keys
mc.set("http://127.0.0.1:80/", "http://127.0.0.1:8080");
mc.set("http://localhost:80/", "http://localhost:8080");

# Print the keys that are saved
print "response-1 is '%s'" % (mc.get("http://127.0.0.1:80/"))
print "response-2 is '%s'" % (mc.get("http://localhost:80/"))
