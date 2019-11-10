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

.. include:: ../common.defs

.. _upgrading:

Upgrading to ATS v9.x
======================

.. toctree::
   :maxdepth: 1

Remapping
---------

One of the biggest changes in ATS v9.0.0 is that URL rewrites, as specified in a :file:`remap.config`
rule, now always happens **before** all plugins are executed. This can have significant impact on
behavior, since plugins might now see a different URL than they did in prior versions. In particular,
plugins modifying the cache key could have serious problems (see the  section below for details).

YAML
----

We are moving configurations over to YAML, and thus far, the following configurations are now fully
migrated over to YAML:

* :file:`logging.yaml` (*was* `logging.config` or `logging.lua`)
* :file:`ip_allow.yaml` (*was* `ip_allow.config`)

In addition, a new file for TLS handhsake negotiation configuration is added:

* :file:`sni.yaml` (this was for a while named ssl_server_name.config in Github)

New records.config settings
----------------------------

These are the changes that are most likely to cause problems during an upgrade. Take special care
making sure you have updated your configurations accordingly.

Connection management
~~~~~~~~~~~~~~~~~~~~~
The old settings for origin connection management included the following settings:

* `proxy.config.http.origin_max_connections`
* `proxy.config.http.origin_max_connections_queue`
* `proxy.config.http.origin_min_keep_alive_connections`

These are all gone, and replaced with the following set of configurations:

* :ts:cv:`proxy.config.http.per_server.connection.max` (overridable)
* :ts:cv:`proxy.config.http.per_server.connection.match` (overridable)
* :ts:cv:`proxy.config.http.per_server.connection.alert_delay`
* :ts:cv:`proxy.config.http.per_server.connection.queue_size`
* :ts:cv:`proxy.config.http.per_server.connection.queue_delay`
* :ts:cv:`proxy.config.http.per_server.connection.min`

Removed records.config settings
-------------------------------

The following settings are simply gone, and have no purpose:

* `proxy.config.config_dir` (see PROXY_CONFIG_CONFIG_DIR environment variable)
* `proxy.config.cache.storage_filename` (see next section as well)

Deprecated records.config settings
----------------------------------
The following configurations still exist, and functions, but are considered
deprecated and will be removed in a future release. We **strongly** encourage
you to avoid using any of these:

  * :ts:cv:`proxy.config.socks.socks_config_file`
  * :ts:cv:`proxy.config.log.config.filename`
  * :ts:cv:`proxy.config.url_remap.filename`
  * :ts:cv:`proxy.config.ssl.server.multicert.filename`
  * :ts:cv:`proxy.config.ssl.servername.filename`
  * ``proxy.config.http.parent_proxy.file``
  * ``proxy.config.cache.control.filename``
  * ``proxy.config.cache.ip_allow.filename``
  * ``proxy.config.cache.hosting_filename``
  * ``proxy.config.cache.volume_filename``
  * ``proxy.config.dns.splitdns.filename``

Deprecated or Removed Features
------------------------------
The following features, configurations and plugins are either removed
or deprecated in this version of ATS. Deprecated features should be
avoided, with the expectation that they will be removed in the next major
release of ATS.


API Changes
-----------
Our APIs are guaranteed to be compatible within major versions, but we do
make changes for each new major release.

Removed APIs
~~~~~~~~~~~~
* ``TSHttpTxnRedirectRequest()``

Renamed or modified APIs
~~~~~~~~~~~~~~~~~~~~~~~~
* ``TSVConnSSLConnectionGet()`` is renamed to be :c:func:`TSVConnSslConnectionGet`

* ``TSHttpTxnServerPush()`` now returns a :c:type:`TSReturnCode`


Cache
-----
The cache in this releases of ATS is compatible with previous versions of ATS.
You would not expect to lose your cache, or have to reinitialize the cache when
upgrading.

However, due to changes in how remap plugins are processed, your cache key
*might* change. In versions to v9.0.0, the first plugin in a remap rule would
get the pristine URL, and subsequent plugins would get the remapped URL. As of
v9.0.0, **all** plugins now receive the remapped URL. If you are using a
plugin that modifies the cache key, e.g. :ref:`admin-plugins-cachekey`, if it
was evaluated first in a remap rule, the behavior (input) changes, and
therefore, cache keys can change!

The old ``v23`` cache is no longer supported, which means caches created with ATS
v2.x will no longer be possible to load with ATS v9.0.0 or later. We feel that
this is an unlikely scenario, but if you do run into this, clearing the cache
is required.

Plugins
-------
The following plugins have changes that might require you to change
configurations.

header_rewrite
~~~~~~~~~~~~~~
* The `%{PATH}` directive is now removed, and instead you want to use
  `%{CLIENT-URL:PATH}`. This was  done to unify the behavior of these
  operators, rather than having this one-off directive.

Platform specific
-----------------
Solaris is no longer a supported platform, but the code is still there.
However, it's unlikely to work, and unless someone takes on ownership of this
Platform, it will be removed from the source in ATS v10.0.0. For more details,
see issue #5553.
