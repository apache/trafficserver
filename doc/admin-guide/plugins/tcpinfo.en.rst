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
  rtt           Estimated round trip time
  ==========    ==================================================

  The following fields are logged when the log level is ``2``:

  ==============    ==================================================
  Field Name        Description
  ==============    ==================================================
  timestamp         Event timestamp
  event             Event name (one of the names listed above)
  client            Client IP address
  server            Server IP address
  rtt               Estimated round trip time
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
  ==============    ==================================================

--sample-rate=COUNT
  This is the number of times per 1000 requests that the data will
  be logged.  A pseudo-random number generator is used to determine if a
  request will be logged.  The default value is 1000 and this option is
  not required to be in the configuration file.  To achieve a log rate
  of 1% you would set this value to 10.

Examples:
---------

This example logs the simple TCP information to ``tcp-metrics.log``
at the start of a TCP connection and once for each HTTP
transaction thereafter::

  tcpinfo.so --log-file=tcp-metrics --log-level=1 --hooks=ssn_start,txn_start

The file ``tcp-metrics.log`` will contain the following log format::

  timestamp event client server rtt
  20140414.17h40m14s ssn_start 127.0.0.1 127.0.0.1 4000
  20140414.17h40m14s txn_start 127.0.0.1 127.0.0.1 4000
  20140414.17h40m16s ssn_start 127.0.0.1 127.0.0.1 4000
  20140414.17h40m16s txn_start 127.0.0.1 127.0.0.1 4000
  20140414.17h40m16s ssn_start 127.0.0.1 127.0.0.1 4000
  20140414.17h40m16s txn_start 127.0.0.1 127.0.0.1 4000
