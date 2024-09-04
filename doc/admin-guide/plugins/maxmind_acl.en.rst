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
mmdb-lite database on the maxmind website or provide your own. You must provide a database
for any usages and specify it in the configuration file as shown below.

Configuration
=============

The plugin takes a single pparam which is the location of the configuration yaml
file. This can either be relative to the ATS configuration directory or an absolute path ::

   map http://example.com/music http://music.example.com @plugin=maxmind_acl.so @pparam=maxmind.yaml

An example configuration ::

   maxmind:
    database: GeoIP2-City.mmdb
    html: deny.html
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
     regex:
      - [US, ".*\\.txt"]  # Because these get parsed you must escape the escape of the ``.`` in order to have it be escaped in the regex, resulting in ".*\.txt"
      - [US, ".*\\.mp3"]

In order to load an updated configuration while ATS is running you will have to touch or modify the remap.config file in order to initiate a plugin reload to pull in any changes.

Rules
=====

You can mix and match the allow rules and deny rules, however deny rules will always take precedence so in the above case ``127.0.0.1`` would be denied.
The IP rules can take either single IPs or cidr formatted rules. It will also accept IPv6 IP and ranges.

The regex portion can be added to both the allow and deny sections for creating allowable or deniable regexes. Each regex takes a country code first and a regex second. The regex
operates on the entire original request URL, the pre-remapped fqdn and path.
In the above example all requests from the US would be allowed except for those on ``txt`` and ``mp3`` files. More rules should be added as pairs, not as additions to existing lists.

Currently the only rules available are ``country``, ``ip``, and ``regex``, though more can easily be added if needed. Each config file does require a top level
``maxmind`` entry as well as a ``database`` entry for the IP lookups.  You can supply a separate database for each remap used in case you use custom
ones and have specific needs per remap.

One other thing to note.  You can reverse the logic of the plugin, so that it will default to always allowing if you do not supply any ``allow`` rules.
In the case you supply no allow rules all connections will be allowed through except those that fall in to any of the deny rule lists. In the above example
the rule of denying ``DE`` would be a noop because there are allow rules set, so by default everything is blocked unless it is explicitly in an allow rule.
However in this case the regexes would still apply since they are based on an allowable country.

Optional
========

There is an optional ``html`` field which takes a html file that will be used as the body of the response for any denied requests if you wish to use a custom one.

Anonymous
=========

There is also an optional ``anonymous`` field. This allows you to use the MaxMind GeoIP2 Anonymous IP database and reference it's optional fields. These are ``ip``, ``vpn``,
``hosting``, ``public``, ``tor``, and ``residential``. Currently anonymous blocking cannot be combined with GeoIP blocking since it is considered a separate database.
However if you custom generate your own database that includes anonymous data then you could use them at the same time. Another solution is to have two instances
of the maxmind_acl plugin on a remap, one to handle geo blocking and another for anonymous blocking. A setting of ``true`` will cause that type to be blocked, ``false``
will not block and is effectively a no-op.

An example configuration ::

   maxmind:
    database: GeoIP2-Anonymous-IP.mmdb
    anonymous:
     hosting: false
     ip: true
     vpn: true
     tor: false
    allow:
     ip:
      - 127.0.0.1  # Can insert known anonymous IP you wish to allow here

This would block anonymous IPs and VPNs while allowing hosting and tor IPs. However if an IP has multiple designations then having a ``false`` set will not stop a ``true`` entry from being blocked.
For example in the above if an IP had both vpn and hosting true in the database then it would still be blocked due to the vpn designation.

The allow IP and deny IP fields also will work while using the anonymous blocking if you wish to allow specific known IPs or block specific IPs. Keep in mind that the same rule about reversing the logic
applies, so that even if you are only doing anonymous IP blocking, and then set allowable IPs to allow certain anonymous IP through (if desired), this will reverse the logic and default to blocking all
IPs unless they fall into a range in the allow list.

The plugin also supports optional fields from GeoGuard databases which includes:
``vpn_datacenter``
``relay_proxy``
``proxy_over_vpn``
``smart_dns_proxy``