.. _admin-plugins-maxmind-acl:

MaxMind ACL Plugin
******************

.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.

This remap plugin provides allow and deny functionality based on the libmaxminddb
library and GeoIP2 databases (mmdb format). It requires libmaxminddb to run
and the associated development headers in order to build. You can find a sample
mmdb-lite database on the maxmind website or provide your own.

Configuration
=============

The plugin takes a single pparam which is the location of the configuration yaml
file. This can either be relative to the ATS configuration directory or an absolute path ::

   map http://example.com/music http://music.example.com @plugin=maxmind_acl.so @pparam=maxmind.yaml

An example configuration ::

   maxmind:
    database: GeoIP2-City.mmdb
    allow:
     country:
      - US
     ip:
      - 127.0.0.1
      - 192.168.10.0/20
    deny:
     country:
      - DE
     ip:
      - 127.0.0.1

You can mix and match the allow rules and deny rules, however deny rules will always take precedence so in the above case ``127.0.0.1`` would be denied.
The IP rules can take either single IPs or cidr formatted rules. It will also accept IPv6 IP and ranges.

One other thing to note.  You can reverse the logic of the plugin, so that it will default to always allowing if you do not supply any ``allow`` rules.
In the case you supply no allow rules all connections will be allowed through except those that fall in to any of the deny rule lists. In the above example
the rule of denying ``DE`` would be a noop because there are allow rules set, so by default everything is blocked unless it is explicitly in an allow rule.

Currently the only rules available are ``country`` and ``ip``, though more can easily be added if needed. Each config file does require a top level
``maxmind`` entry as well as a ``database`` entry for the IP lookups.  You can supply a separate database for each remap used in case you use custom
ones and have specific needs per remap.
