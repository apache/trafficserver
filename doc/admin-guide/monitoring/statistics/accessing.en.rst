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

.. include:: ../../../common.defs

.. _admin-stats-accessing:

Accessing Statistics
********************

There are currently two methods provided with |TS| to view statistics:
:ref:`admin-stats-traffic-line` and :ref:`admin-stats-stats-over-http`.

.. _admin-stats-traffic-line:

Traffic Control
===============

The command line utility :program:`traffic_ctl` offers a text based interface
for viewing |TS| statistics. Invocation is simple and requires knowing the
specific name of the statistic you wish you view::

    traffic_ctl metric get <statistic name>

You may notice that this is the same utility, and argument, used for viewing
configuration variables of a running |TS| instance. Unlike configuration
variables, you cannot modify a statistic value with the :program:`traffic_ctl`
program.

This utility is enabled and built by default, and will be located in the
``bin/`` subdirectory of your |TS| installation. There are no required changes
to your configuration to allow :program:`traffic_ctl` to function, however it
may only be run by users with permissions to access the |TS| Unix socket. This
will typically limit use to root as well as the system user you have configured
|TS| to run under or any other system users which share the same group as you
have configured |TS| to use.

.. _admin-stats-stats-over-http:

Stats Over HTTP
===============

|TS| includes a stable plugin, :ref:`admin-plugins-stats-over-http`, which provides
HTTP access to all |TS| statistcs. The plugin returns a JSON object with all
statistics and their current values. It is not possible to return a subset of
the statistics. The plugin must be enabled before you may use it.

.. _admin-stats-stats-over-http-enabling:

Enabling Stats Over HTTP
------------------------

To enable the :ref:`admin-plugins-stats-over-http` plugin, you must add the
following to your :file:`plugin.config`::

    stats_over_http.so

Once the plugin is enabled and |TS| has reloaded, you can test that it is
working properly by issuing a simple HTTP request with ``curl``. Assuming your
|TS| installation is using the default interface and port bindings, running the
following command on the same host as |TS| should now work::

    curl http://localhost:8080/_stats

You should be presented with an HTTP response containing a single JSON object
which lists all the available statistics and their current values. If you have
configured |TS| to only listen on a specific interface, or to use a different
port, you may need to adjust the URL in the command above.

If you wish to have the stats made available at a non-default path, then that
path should be given as the sole argument to the plugin, as so::

    stats_over_http.so 81c075bc0cca1435ea899ba4ad72766b

The above :file:`plugin.config` entry will result in your |TS| statistics being
located at ``/81c075bc0cca1435ea899ba4ad72766b`` on any host and port on which
you have your |TS| instance listening.

.. _admin-stats-stats-over-http-security:

Statistics Security and Privacy
-------------------------------

Simply changing the path at which your statistics are available should be
considered very weak security. While cache objects themselves cannot be
accessed through the plugin's JSON output, and no modifications to the
configuration or operation of |TS| may be made through the plugin, the
statistics may reveal much more about your network's traffic and architecture
than you wish to be publicly available.

A better method is to use an :ref:`ACL Filter <remap-config-named-filters>` in
:file:`remap.config` to restrict access to clients. For instance, if your |TS|
host resides on a private network in the 10.1.1.0/24 IPv4 address space
listening on the address 10.1.1.10, separate from its public interface(s) used
to serve client requests, you could add the following remap configuration::

    map http://10.1.1.10/_stats http://10.1.1.10/_stats @action=deny @src_ip=0.0.0.0-255.255.255.255
    map http://10.1.1.10/_stats http://10.1.1.10/_stats @action=allow @src_ip=10.1.1.0-10.1.1.255

The above configuration sets the default policy for the entirety of IPv4 address
space to deny, but then exempts the 10.1.1.0/24 network by permitting their
requests to be processed by |TS|. If your monitoring infrastructure makes use
of locally-installed data collection agents, you may even wish to restrict
access to the Stats Over HTTP plugin to all but localhost.

