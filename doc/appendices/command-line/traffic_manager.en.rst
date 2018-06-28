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

.. _traffic_manager:

traffic_manager
***************

.. program:: traffic_manager

Description
===========

.. option:: --aconfPort PORT
.. option:: --action TAGS
.. option:: --debug TAGS
.. option:: --groupAddr ADDRESS
.. option:: --help
.. option:: --nosyslog
.. option:: --path FILE
.. option:: --proxyBackDoor PORT
.. option:: --proxyOff
.. option:: --proxyPort PORT
.. option:: --recordsConf FILE
.. option:: --tsArgs ARGUMENTS
.. option:: --version

Signals
=======

SIGHUP
  This signal causes a reconfiguration event, equivalent to running :program:`traffic_ctl config reload`.

SIGINT, SIGTERM
  These signals cause :program:`traffic_manager` to exit after also shutting down :program:`traffic_server`.

See also
========

:manpage:`traffic_ctl(8)`,
:manpage:`traffic_server(8)`
