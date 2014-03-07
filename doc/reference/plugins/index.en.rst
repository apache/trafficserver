Plugin Reference
****************

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

One of the key features of Apache Traffic Server is modularity.
Features that aren't needed in the core simply aren't there. This
is a good thing, because it guarantees that our core can remain
fast by concentrating on the things that we always provide: Caching
and proxying.

All other things can be moved into plugins, by opening up a consistent
C API, everyone can implement their own functionality, without
having to touch the core.

Stable plugins
==============

Plugins that are considered stable are installed by default in
Apache Traffic Server releases.

.. toctree::
  :maxdepth: 1

  cacheurl.en
  conf_remap.en
  gzip.en
  header_rewrite.en
  regex_remap.en
  stats_over_http.en

Experimental plugins
====================

Plugins that are considered experimental are located in the
`plugins/experimental <https://git-wip-us.apache.org/repos/asf?p=trafficserver.git;a=tree;f=plugins/experimental;hb=HEAD>`_
directory of the Apache Traffic Server source tree. Exmperimental plugins can be compiled by passing the
`--enable-experimental-plugins` option to `configure`::

    $ autoconf -i
    $ ./configure --enable-experimental-plugins
    $ make

.. toctree::
  :maxdepth: 1

  authproxy.en
  balancer.en
  buffer_upload.en
  combo_handler.en
  esi.en
  geoip_acl.en
  hipes.en
  metalink.en
  mysql_remap.en
  s3_auth.en
  stale_while_revalidate.en
  ts_lua.en
  xdebug.en

