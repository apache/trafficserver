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

.. _admin-plugins-tcpinfo:

TCPInfo Plugin
**************

This global plugin logs TCP metrics at various points in the HTTP
processing pipeline. The TCP information is retrieved by the
:manpage:`getsockopt(2)` function using the ``TCP_INFO`` option.
This is only supported on systems that support the ``TCP_INFO``
option, currently Linux and BSD.

Plugin Options
--------------

The following options may be specified in :file:`plugin.config`:

.. NOTE: if the option name is not long enough, docutils will not
   add the colspan attribute and the options table formatting will
   be all messed up. Just a trap for young players.

--hooks=NAMELIST
  This option specifies when TCP information should be logged. The
  argument is a comma-separated list of the event names listed
  below. TCP information will be sampled and logged each time the
  specified set of events occurs.

  ==============  ===============================================
   Event Name     Triggered when
  ==============  ===============================================
  send_resp_hdr   The server begins sending an HTTP response.
  ssn_close       The TCP connection closes.
  ssn_start       A new TCP connection is accepted.
  txn_close       A HTTP transaction is completed.
  txn_start       A HTTP transaction is initiated.
  ==============  ===============================================

--log-file=NAME
  This specifies the base name of the file where TCP information
  should be logged. If this option is not specified, the name
  ``tcpinfo`` is used. Traffic Server will automatically append
  the ``.log`` suffix.

--log-level=LEVEL
  The log level can be either ``1`` to log only the round trip
  time estimate, or ``2`` to log the complete set of TCP information.

  The following fields are logged when the log level is ``1``:

  ==========    ==================================================
  Field Name    Description
  ==========    ==================================================
  timestamp     Event timestamp
  event         Event name (one of the names listed above)
  client        Client IP address
  server        Server IP address
  rtt           Estimated round trip time in microseconds
  ==========    ==================================================

  The following fields are logged when the log level is ``2``:

  ==============    ==================================================
  Field Name        Description
  ==============    ==================================================
  timestamp         Event timestamp
  event             Event name (one of the names listed above)
  client            Client IP address
  server            Server IP address
  rtt               Estimated round trip time in microseconds
  rttvar
  last_sent
  last_recv
  snd_cwnd
  snd_ssthresh
  rcv_ssthresh
  unacked
  sacked
  lost
  retrans
  fackets
  all_retrans
  ==============    ==================================================

  In addition, on newer Linux system, the following two fields are appended
  in log level 2:
  ==============    ==================================================
  Field Name        Description
  ==============    ==================================================
  data_segs_in      Number of incoming data segments
  data_segs_out     Number of outgoing data segments

  Note: Features such as TSO (TCP Segment Offload) might skew the numbers
  here. That's true as well for e.g. the retransmit metrics, i.e. anything
  that deals with segments.

--sample-rate=COUNT
  This is the number of times per 1000 requests that the data will
  be logged.  A pseudo-random number generator is used to determine if a
  request will be logged.  The default value is 1000 and this option is
  not required to be in the configuration file.  To achieve a log rate
  of 1% you would set this value to 10.

--rolling-enabled=VALUE
  This logfile option allows you to set logfile rolling behaviour of
  the output log file  without making any changes to the global
  logging configurations.  This option overrides the
  :ts:cv:`proxy.config.output.logfile.rolling_enabled` setting in :file:`records.config`
  for the ``tcpinfo`` plugin.  The setting may range from ``0`` to ``3``.
  ``0`` disables logfile rolling.  ``1`` is the ``default`` and enables logfile
  rolling at specific intervals set by ``--rolling-interval-sec`` discussed
  below.  ``2`` enables logfile rolling by logfile size, see
  ``--rolling-size-mb`` below.  Finally a value of ``3`` enables logfile rolling
  at specific intervals or size, whichever occurs first using the interval or size
  settings discussed below.

--rolling-offset-hr=VALUE
  Set the hour ``0`` to ``23`` at which the output log file will roll when
  using interval rolling. Default value is ``0``.

--rolling-interval-sec=VALUE
  Set the rolling interval in seconds for the output log file. May be set
  from ``60`` to ``86400`` seconds, Defaults to ``86400``.

--rolling-size=VALUE
  Set the size in MB at which the output log file  will roll when using log size
  rolling.  Minimum value is ``10``, defaults to ``1024``. In your config file,
  you may use the K, M, or G suffix as in::

  --rolling-size=10M

Examples:
---------

This example logs the simple TCP information to ``tcp-metrics.log``
at the start of a TCP connection and once for each HTTP
transaction thereafter::

  tcpinfo.so --log-file=tcp-metrics --log-level=1 --hooks=ssn_start,txn_start

The file ``tcp-metrics.log`` will contain the following log format (with client and server both on 127.0.0.1)::

  timestamp event client server rtt
  20140414.17h40m14s ssn_start 127.0.0.1 127.0.0.1 153859
  20140414.17h40m14s txn_start 127.0.0.1 127.0.0.1 181018
  20140414.17h40m16s ssn_start 127.0.0.1 127.0.0.1 86869
  20140414.17h40m16s txn_start 127.0.0.1 127.0.0.1 19088
  20140414.17h40m16s ssn_start 127.0.0.1 127.0.0.1 85718
  20140414.17h40m16s txn_start 127.0.0.1 127.0.0.1 38059
