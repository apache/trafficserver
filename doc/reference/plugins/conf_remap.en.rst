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

.. _conf-remap-plugin:

conf_remap Plugin
=================

The `conf_remap` plugin allows you to override configuration
directives dependent on actual remapping rules. The plugin is built
and installed as part of the normal Apache Traffic Server installation
process.

The `conf_remap` plugin accepts configuration directives in the
arguments list or in a separate configuration file. In both cases,
only string and integer directives are supported.

When using a separate configuration file, the standard
:file:`records.config` syntax is used, for example::

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=conf_remap.so @pparam=/etc/trafficserver/cdn.conf

where `cdn.conf` contains::

    CONFIG proxy.config.url_remap.pristine_host_hdr INT 1

When using inline arguments, the `conf_remap` plugin accepts a
``key=value`` syntax, where the ``KEY`` is the name of the configuration
directive and ``VALUE`` is the desired value, for example::

    map http://cdn.example.com/ http://some-server.example.com \
      @plugin=conf_remap.so @pparam=proxy.config.url_remap.pristine_host_hdr=1

Doing this, you will override your global default configuration on
a per mapping rule. For more details on the APIs, functionality, and a
complete list of all overridable configurations, see :ref:`ts-overridable-config`.
