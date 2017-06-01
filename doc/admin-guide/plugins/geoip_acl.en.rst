.. _admin-plugins-geoip-acl:

GeoIP ACLs Plugin
*****************

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

This is a simple ATS plugin for denying (or allowing) requests based on
the source IP geo-location. Currently only the Maxmind APIs are
supported, but we'd be happy to other other (open) APIs if you let us
know. This plugin comes with the standard distribution of Apache Traffic
Server, and should be installed as part of the normal build process.


Configuration
=============

Once installed, there are three primary use cases, which we will discuss
in details. Note that in all configurations, the first plugin parameter
must specify what the matches should be applied to. Currently, only one
rule set is supported, for Country ISO codes. This is specified with a
parameter of ::

    @pparam=country

Future additions to this plugin could include other regions, such as
city, state, continent etc.

The three typical use cases are as follows:

1. Per remap configurations, applicable to the entire remap rule. This
   is useful when you can partition your content so that entire prefix
   paths should be filtered. For example, lets assume that
   http://example.com/music is restricted to US customers only, and
   everything else is world wide accessible. In remap.config, you would
   have something like ::

    map http://example.com/music http://music.example.com \
      @plugin=geoip_acl.so @pparam=country @pparam=allow @pparam=US
    map http://example.com http://other.example.com

2. If you can not partition the data with a path prefix, you can specify
   a separate regex mapping filter. The remap.config file might then
   look like ::

    map http://example.com http://music.example.com \
      @plugin=geoip_acl.so @pparam=country \
      @pparam=regex::/etc/music.regex

where music.regex is a format with PCRE (perl compatible) regular
expressions, and unique rules for match. E.g.::

    .*\.mp3  allow  US
    .*\.ogg  deny   US

Note that the default in the case of no matches on the regular
expressions is to "allow" the request. This can be overriden, see next
use case.

3. You can also combine 1) and 2), and provide defaults in the
   remap.config configuration, which then applies for the cases where no
   regular expressions matches at all. This would be useful to override
   the default which is to allow all requests that don't match. For
   example ::

    map http://example.com http://music.example.com \
      @plugin=geoip_acl.so @pparam=country @pparam=allow @pparam=US \
      @pparam=regex::/etc/music.regex

This tells the plugin that in the situation where there is no matching
regular expression, only allow requests originating from the US.

Finally, there's one additional parameter option that can be used ::

    @pparam=html::/some/path.html

This will override the default response body for the denied responses
with a custom piece of HTML. This can be useful to explain to your users
why they are getting denied access to a particular piece of content.
This configuration can be used with any of the use cases described
above.
