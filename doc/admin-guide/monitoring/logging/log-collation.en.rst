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

Log Collation
*************

You can use the |TS| log file collation feature to collect all logged
information in one place. Log collation enables you to analyze a set of |TS|
clustered nodes as a whole (rather than as individual nodes) and to use a large
disk that might only be located on one of the nodes in the cluster.

|TS| collates log files by using one or more nodes as log collation servers and
all remaining nodes as log collation clients. When a |TS| node generates a
buffer of event log entries, it first determines if it is the collation server
or a collation client. The collation server node writes all log buffers to its
local disk, just as it would if log collation was not enabled.  Log collation
servers can be standalone or they can be part of a node running |TS|.

The collation client nodes prepare their log buffers for transfer across the
network and send the buffers to the log collation server. When the log
collation server receives a log buffer from a client, it writes it to its own
log file as if it was generated locally. For a visual representation of this,
see the figure below.

.. figure:: /static/images/admin/logcolat.jpg
   :align: center
   :alt: Log collation

   Log collation

If log clients cannot contact their log collation server, then they write their
log buffers to their local disks, into *orphan* log files. Orphaned log files
require manual collation.

.. note::

    Log collation can have an impact on network performance. Because all nodes
    are forwarding their log data buffers to the single collation server, a
    bottleneck can occur. In addition, collated log files contain timestamp
    information for each entry, but entries in the files do not appear in
    strict chronological order. You may want to sort collated log files before
    doing analysis.

.. _admin-configuring-traffic-server-to-be-a-collation-server:

Server Configuration
====================

To configure a |TS| node to be a collation server, perform the following
configuration adjustments in :file:`records.config`:

#. Set :ts:cv:`proxy.local.log.collation_mode` to ``1`` to indicate this node
   will be a server. ::

        CONFIG proxy.local.log.collation_mode INT 1

#. Configure the port on which the server will listen to incoming collation
   transfers from clients, using :ts:cv:`proxy.config.log.collation_port`. If
   omitted, this defaults to port ``8085``. ::

        CONFIG proxy.config.log.collation_port INT 8085

#. Configure the shared secret used by collation clients to authenticate their
   sessions, using :ts:cv:`proxy.config.log.collation_secret`. ::

        CONFIG proxy.config.log.collation_secret STRING "seekrit"

#. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

.. note::

    If you modify the ``collation_port`` or ``secret`` after connections
    between the collation server and collation clients have been established,
    then you must restart Traffic Server on all nodes.

.. _admin-using-a-standalone-collator:

Standalone Collator
-------------------

If you do not want the log collation server to be a |TS| node,
then you can install and configure a standalone collator (*SAC*) that will
dedicate more of its power to collecting, processing, and writing log
files.

To install and configure a standalone collator:

#. Configure your |TS| nodes as log collation clients based on the instructions
   in :ref:`admin-monitoring-logging-collation-client`.

#. Copy the :program:`traffic_sac` binary from the |TS| ``bin`` directory, and
   place in a suitable location on the system that will act as the standalone
   collator.

#. Copy the ``libtsutil.so`` libraries from the |TS| ``lib`` directory to the
   machine serving as the standalone collator.

#. Create a directory called ``config`` in the directory that contains
   the :program:`traffic_sac` binary.

#. Create a directory called ``internal`` in the ``config`` directory
   you created above. This directory is used internally by the standalone
   collator to store lock files.

#. Copy the :file:`records.config` file from a |TS| node configured to be a log
   collation client to the ``config`` directory you created on the standalone
   collator.

   The :file:`records.config` file contains the log collation secret and the
   port you specified when configuring |TS| nodes to be collation clients. The
   collation port and secret must be the same for all collation clients and
   servers.

#. Edit :ts:cv:`proxy.config.log.logfile_dir` in :file:`records.config` to
   specify a location on your standalone collator where the collected log files
   should be stored. ::

        CONFIG proxy.config.log.logfile_dir STRING "/var/log/trafficserver/"

#. Enter the following command to start the standalone collator process::

      traffic_sac -c config

You will likely want to configure this program to run at server startup, as
well as configure a service monitor in the event the process terminates
abnormally. Please consult your operating system's documentation for how to
achieve this.

.. _admin-monitoring-logging-collation-client:

Client Configuration
====================

To configure a |TS| node to be a collation client, follow the steps below. If
you modify the ``collation_port`` or ``secret`` after connections between the
collation clients and the collation server have been established, then you must
restart |TS|.

#. In the :file:`records.config` file, edit the following variables:

   -  :ts:cv:`proxy.local.log.collation_mode`: ``2`` to configure this node as
      log collation client and sen standard formatted log entries to the
      collation server. For XML-based formatted log entries, see
      :file:`logs_xml.config` file.
   -  :ts:cv:`proxy.config.log.collation_host`
   -  :ts:cv:`proxy.config.log.collation_port`
   -  :ts:cv:`proxy.config.log.collation_secret`
   -  :ts:cv:`proxy.config.log.collation_host_tagged`
   -  :ts:cv:`proxy.config.log.max_space_mb_for_orphan_logs`

#. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Collating Custom Logs
=====================

If you use custom event log files, then you must edit :file:`logs_xml.config`,
in addition to configuring a collation server and collation clients.

To collate custom event log files:

#. On each collation client, edit :file:`logs_xml.config` and add the
   :ref:`CollationHosts <logs-xml-logobject-collationhost>` attribute to the
   :ref:`LogObject` specification::

       <LogObject>
         <Format = "squid"/>
         <Filename = "squid"/>
         <CollationHosts="ipaddress:port"/>
       </LogObject>

   Where ``ipaddress`` is the hostname or IP address of the collation
   server to which all log entries (for this object) are forwarded, and
   ``port`` is the port number for communication between the collation
   server and collation clients.

#. Run the command :option:`traffic_line -L` to restart Traffic Server on the
   local node or :option:`traffic_line -M` to restart Traffic Server on all
   the nodes in a cluster.

