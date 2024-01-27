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


.. include:: ../../common.defs

.. _admin-plugins-stale_response:


Stale Response Plugin
*********************

This plugin adds ``stale-while-relavidate`` and ``stale-if-error``
``Cache-Control`` directive functionality.  The ``stale-while-revalidate``
directive specifies a number of seconds that an item can be stale and used as a
cached response while |TS| revalidates the cached item. The ``stale-if-error``
directive specifies how stale a cached response can be and still be used as a
response if the origin server replies with a 500, 502, 503, or 504 HTTP status.
The plugin currently only supports the ``stale-if-error`` directive in
responses. For more details, see `RFC 5861
<https://www.rfc-editor.org/rfc/rfc5861>`_ for the specification of these
directives.

Building and Installation
*************************

The Stale Response plugin is an experimental plugin. To build it, pass
``-DBUILD_EXPERIMENTAL_PLUGINS=ON`` to the ``cmake`` command when building |TS|.
By default, that will build the ``stale_response.so`` plugin and install it in
the ``libexec/trafficserver`` directory.

Configuration
*************

The Stale Response plugin supports being used as either a global plugin or as a
remap plugin. To configure |TS| to use the Stale Response plugin, simply add it
to either the :file:`plugin.config` to configure it as a global plugin and
restart traffic-server or add it to the desired remap lines in the
:file:`remap.config` and reload |TS|. The following configurations are available
for the plugin:

Default values can be specified for responses that do not contain the directives:

  * ``--stale-while-revalidate-default <time>`` set a default
    ``stale-while-revalidate`` directive with a value of ``time`` for all
    responses where the directive is not present.
  * ``--stale-if-error-default <time>`` set a default
    ``stale-if-err`` directive with a value of ``time`` for all
    responses where the directive is not present.

Also, minimum values can be set for responses that do or do not contain the
directives:

  * ``--force-stale-while-revalidate <time>`` set a minimum
    ``stale-while-revalidate`` time for all responses.
  * ``--force-stale-if-error <time>`` set a minimum ``stale-if-error`` time
    for all responses.

The plugin uses memory to temporarily store responses. By default, this is limited
to 1 GB, but can be configured with: ``--max-memory-usage <size>``

The plugin by default will only perform one asynchronous request at a time for
a given URL. This can be changed with the ``--force-parallel-async`` option.

Logging
*******

The plugin can log information about its behavior with respect to the
``stale-while-revalidate`` and ``stale-if-error`` directives.

  * ``--log-all`` enable logging of all stale responses for both ``stale-while-revalidate``
    and ``stale-if-error`` directives.
  * ``--log-stale-while-revalidate``enable logging of all stale responses due to
    ``stale-while-revalidate`` directives.
  * ``--log-stale-if-error`` enable logging of all stale responses due to
    ``stale-if-error`` directives.
  * ``--log-filename <name>`` set the Stale Response log to ``<name>.log``. The
    log will be in the :ts:cv:`proxy.config.log.logfile_dir` directory.


Statistics
**********

  * ``stale_response.swr.hit`` The number of times stale data was served for stale-while-relavidate.
  * ``stale_response.swr.locked_miss`` The number of times stale data could not be served with stale-while-relavidate because of a lock.
  * ``stale_response.sie.hit`` The number of times stale data was served for stale-if-error.
  * ``stale_response.memory.over`` The number of times stale data could not be served because of memory constraints.


Example
*******

To configure the plugin as a global plugin with a default
``stale-while-revalidate`` and a default ``stale-if-error`` of 30 seconds, add
the following to :file:`plugin.config`::

    stale_response.so --stale-while-revalidate-default 30 --stale-if-error-default 30

To configure the plugin the same way as a remap config plugin, add the following
to an appropriate remap entry in :file:`remap.config`::

    map http://example.com http://backend.example.com @plugin=stale_response.so \
      @pparam=--stale-while-revalidate-default @pparam=30 \
      @pparam=--stale-if-error-default @pparam=30
