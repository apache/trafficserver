:title: Apache Traffic Server Plugins

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


Overview
========

One of the key features of Apache Traffic Server is modularity. Features
that aren't needed in the core simply aren't there. This is a good
thing, because it guarantees that our core can remain fast by
concentrating on the things that we always provide: Caching and
proxying.

All other things can be moved into plugins, by opening up a consitent C
API, everyone can implement their own functionality, without having to
touch the core.

Download
========

Apache Traffic Server Plugins considered stable are released together
with the server. See `downloads </downloads>`_.

All plugins, whether they have received enough production testing from
our developers or feedback from our users to be consiered stable can
still be optained seperately in source form via git:

::
    git clone https://git-wip-us.apache.org/repos/asf/trafficserver.git/

Plugins considered experimental are located under
```plugins/experimental`` <https://git-wip-us.apache.org/repos/asf?p=trafficserver.git;a=tree;f=plugins/experimental;hb=HEAD>`_

Build
=====

Most plugins can be build by simply issueing

::
    make

in their source tree. Note that this requires you to have ``tsxs`` in
your ``PATH``

Plugins # {#plugins}
====================

.. toctree::
   :maxdepth: 2

   plugins/balancer.en
   plugins/buffer_upload.en
   plugins/cacheurl.en
   plugins/combo_handler.en
   plugins/esi.en
   plugins/geoip_acl.en
   plugins/header_filter.en
   plugins/hipes.en
   plugins/mysql_remap.en
   plugins/regex_remap.en
   plugins/stale_while_revalidate.en
   plugins/stats_over_http.en
   plugins/gzip.en

