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

.. _admin-graceful-shutdown:

Graceful Shutdown
*****************

Overview
========

|TS| provides a graceful shutdown feature that allows active client connections
to complete naturally while signaling to clients that they should close their
connections. This is useful when preparing a server for maintenance, removing it
from a load balancer rotation, or performing other operations where you want to
reduce active connections without abruptly terminating traffic.

The :ref:`traffic_ctl server drain <traffic-control-command-server-drain>`
command enables this draining mode.

.. _drain-command:

The Drain Command
=================

Basic Usage
-----------

To enable drain mode:

.. code-block:: bash

   traffic_ctl server drain

To disable drain mode and return to normal operation:

.. code-block:: bash

   traffic_ctl server drain --undo

How It Works
------------

When you run :ref:`traffic_ctl server drain
<traffic-control-command-server-drain>`, the following occurs:

1. A global drain flag is set internally (``TSSystemState::draining = true``).
2. The ``proxy.process.proxy.draining`` metric is set to 1.
3. All subsequent client responses are affected as described below.

Behavior by Protocol
--------------------

HTTP/1.1 Sessions
~~~~~~~~~~~~~~~~~

For HTTP/1.1 connections, |TS| automatically adds a ``Connection: close`` header
to all responses. When clients receive this header, compliant clients will:

- Complete the current request/response.
- Close the connection after receiving the response.
- Establish a new connection if additional requests are needed.

HTTP/2 Sessions
~~~~~~~~~~~~~~~

For HTTP/2 connections, |TS| initiates the HTTP/2 graceful shutdown procedure:

- Sends a GOAWAY frame to clients.
- Clients stop creating new streams on the connection.
- Existing streams are allowed to complete.
- Clients close the connection when all active streams finish.

Advantages Over Manual Methods
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The drain command is simpler and more complete than manually setting
``Connection: close`` headers (for example, via the header_rewrite plugin)
because it:

- Works automatically without requiring configuration file changes.
- Handles both HTTP/1.1 and HTTP/2 protocols properly.
- Can be easily toggled on and off with the ``--undo`` flag.
- Requires no server restart or configuration reload.

What Drain Does NOT Do
-----------------------

It's important to understand the limitations of drain mode. Namely, the drain
mode is something of a soft request to clients to drain current connections
without other explicit action by |TS|. Specifically:

- **Does NOT close listening sockets**: |TS| continues accepting new connections
  from clients.
- **Does NOT terminate existing connections**: Connections remain open until
  clients close them or they time out naturally.
- **Does NOT reject new requests**: New connections are accepted and processed
  normally, but they receive the drain signals described above.

Monitoring Drain Status
========================

You can check whether |TS| is in drain mode using:

.. code-block:: bash

   traffic_ctl server status

This will show a JSON response including:

.. code-block:: json

   {
      "initialized_done": "true",
      "is_ssl_handshaking_stopped": "false",
      "is_draining": "true",
      "is_event_system_shut_down": "false"
   }

You can also monitor the ``proxy.process.proxy.draining`` metric, which will
be 1 when drain mode is active and 0 otherwise.

Best Practices
==============

When using drain mode for maintenance or other operations:

1. Remove the server from your load balancer rotation (or otherwise reduce traffic to it).
2. Run :program:`traffic_ctl server drain`.
3. Monitor active connections (via metrics, the use of ``ss``, ``top``, etc.).
4. Wait for connections to drain naturally or until you reach an acceptable level.
5. Proceed with maintenance or other operations.

Additional recommendations:

- **Combine with load balancer controls**: Use drain mode in conjunction with
  your load balancer's session persistence and health check features for the
  smoothest transitions.

- **Monitor connection counts**: Watch your connection metrics to see the effect
  of drain mode and determine when enough connections have closed.

- **Use** ``--undo`` **if needed**: If you need to return a server to service
  quickly, use :program:`traffic_ctl server drain --undo` to disable drain mode.

- **Test in non-production first**: Test your drain procedures in a staging
  environment to understand timing and behavior with your specific traffic
  patterns.


See Also
========

- :ref:`traffic_ctl server drain <traffic-control-command-server-drain>`
- :ref:`traffic_ctl server status <traffic-control-command-server-status>`
- :ts:cv:`proxy.config.stop.shutdown_timeout`

