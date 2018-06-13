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

.. _traffic_wccp:

traffic_wccp
************

Description
===========

Front end to the wccp client library.  It is a stand alone program that speaks
the client side of the WCCP cache protocol.

It can be used instead of the built in WCCP feature in |TS|.
This can be beneficial if you have multiple programs running on the same
computer that are relying on WCCP to redirect traffic from the router to
the computer.

Since it relies on the wccp library, :program:`traffic_wccp` is only built if
|TS| is configured with ``--enable-wccp``.

The overall Apache Traffic Server WCCP configuration documentation is
at :ref:`WCCP Configuration <wccp-configuration>`

Options
=======

.. program:: traffic_wccp

.. option:: --address IP address to bind.

.. option:: --router Booststrap IP address for routers.

.. option:: --service Path to service group definitions.

.. option:: --debug Print debugging information.

.. option:: --daemon Run as daemon.

You need to run at least with the ``--service`` arguments. An example service
definition file, ``service-nogre-example.config``, is included in the
``src/traffic_wccp`` directory. In this file you define your MD5 security
password (highly recommended), and you define your service groups. The details
of the service file are defined at :ref:`admin-wccp-service-config`.

Limitations
===========

The current WCCP implementation associated with ATS only supports one cache
client per service group per router.  The cache assignment logic currently
assigns the current cache client to all buckets.
