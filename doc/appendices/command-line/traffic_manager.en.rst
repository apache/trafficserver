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
.. option:: --proxyOff
.. option:: --listenOff
.. option:: --proxyPort PORT
.. option:: --recordsConf FILE
.. option:: --tsArgs ARGUMENTS
.. option:: --maxRecords RECORDS
.. option:: --bind_stdout FILE

The file to which the stdout stream for :program:`traffic_manager` will be bound.

.. option:: --bind_stderr FILE

The file to which the stderr stream for :program:`traffic_manager` will be bound.

.. option:: --version

Signals
=======

SIGHUP
  This signal causes a reconfiguration event, equivalent to running :program:`traffic_ctl config reload`.

SIGINT, SIGTERM
  These signals cause :program:`traffic_manager` to exit after also shutting down :program:`traffic_server`.

SIGUSR2
  This signal causes the :program:`traffic_manager` and :program:`traffic_server` processes to close
  and reopen their file descriptors for all of their log files. This allows the use of external
  tools to handle log rotation and retention. For instance, logrotate(8) can be configured to rotate
  the various |ATS| logs and, via the logrotate postrotate script, send a `-SIGUSR2` to the
  :program:`traffic_manager` process. After the signal is received, |ATS| will stop logging to the
  now-rolled files and will reopen log files with the originally configured log names.

Exponential Back-off Delay
==========================

  If :program:`traffic_server` has issues communicating with  :program:`traffic_manager` after a crash,
  :program:`traffic_manager` will retry to start  :program:`traffic_server` using an exponential back-off delay,
  which will make :program:`traffic_manager` to retry starting :program:`traffic_server` from ``1s`` until it
  reaches the max ceiling time. The ceiling time is configurable  as well as the number of times that
  :program:`traffic_manager` will keep trying to start :program:`traffic_server`. *A random variance will be
  added to the sleep time on every retry*

.. note::
  For more information about this configuration please check :file:`records.config`



See also
========

:manpage:`traffic_ctl(8)`,
:manpage:`traffic_server(8)`
