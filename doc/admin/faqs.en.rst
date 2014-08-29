.. _admin-faqs:

FAQ and Troubleshooting Tips
****************************

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

.. toctree::
   :maxdepth: 2

FAQs
====

How do you create a raw disk for the cache if all your disks have mounted file systems?
---------------------------------------------------------------------------------------

Create a large file on filesystem (with :manpage:`dd(1)`) and mount it as loopback device.
This is accomplished with :manpage:`losetup(8)` on Linux, :manpage:`lofiadm(1m)` on Solaris
and Illumos, and :manpage:`mdconfig(8)` on FreeBSD.

How do disk I/O errors affect the cache and what does Traffic Server do when a cache disk fails?
------------------------------------------------------------------------------------------------

If a disk drive fails five successive I/O operations, then Traffic
Server considers the drive inaccessible and removes the entire disk from
the cache. Normal cache operations continue for all other Traffic Server
disk drives.

If a client disconnects during the time that Traffic Server is downloading a large object, is any of the object saved in the cache?
-----------------------------------------------------------------------------------------------------------------------------------

When a client disconnects during an HTTP operation, Traffic Server
continues to download the object from the origin server for up to 10
seconds. If the transfer from the origin server completes successfully
within 10 seconds after the client disconnect, then Traffic Server
stores the object in cache. If the origin server download does *not*
complete successfully within 10 seconds, then Traffic Server disconnects
from the origin server and deletes the object from cache. Traffic Server
does not store partial documents in the cache.

Can Traffic Server cache Java applets, JavaScript programs, or other application files like VBScript?
-----------------------------------------------------------------------------------------------------

Yes, Traffic Server can store and serve Java applets, JavaScript
programs, VBScripts, and other executable objects from its cache
according to the freshness and cacheability rules for HTTP objects.
Traffic Server does not execute the applets, scripts, or programs,
however - these objects run only when the client system (ie, the one
that sent the request) loads them.

In Squid- and Netscape-format log files, what do the cache result codes mean?
-----------------------------------------------------------------------------

This is described in detail in the :ref:`log-formats-squid-format` documentation.

What is recorded by the ``cqtx`` field in a custom log file?
------------------------------------------------------------

-  In **forward proxy mode**, the cqtx field records the complete client
   request in the log file (for example, ``GET http://www.company.com HTTP/1.0``).
-  In **reverse proxy mode**, the cqtx field records the hostname or IP
   address of the origin server because Traffic Server first remaps the
   request as per map rules in the :file:`remap.config` file.

Does Traffic Server refresh entries in its host database after a certain period of time if they have not been used?
-------------------------------------------------------------------------------------------------------------------

By default, the Traffic Server host database observes the time-to-live
(``ttl``) values set by name servers. You can reconfigure Traffic Server
to ignore the ``ttl`` set by name servers and use a specific Traffic
Server setting instead. Alternatively, you can configure Traffic Server
to compare the ``ttl`` value set by the name server with the ``ttl``
value set by Traffic Server, and then use either the lower or the higher
value.

see :ts:cv:`proxy.config.hostdb.ttl_mode` for more info

Can you improve the look of your custom response pages by using images, animated .gifs, and Java applets?
---------------------------------------------------------------------------------------------------------

No, because Traffic Server can only respond to clients with a single
text or HTML document. As a workaround, however, you can provide
references on your custom response pages to images, animated .gifs, Java
applets, or objects other than text which are located on a web server.
Add links in the body_factory template files in the same way you would
for any image in an HTML document (i.e., with the full URL in the
``SRC`` attribute).

Can Traffic Server run in forward proxy and reverse proxy modes at the same time?
---------------------------------------------------------------------------------

Yes. When you enable reverse proxy mode, Traffic Server remaps incoming
requests according to the map rules in the :file:`remap.config` file. All
other requests that do not match a map rule are simply served in forward
proxy mode.

If you want to run in reverse proxy only mode (wherein Traffic Server
does *not* serve requests that fail to match a map rule), then you must
set the configuration variable :ts:cv:`proxy.config.url_remap.remap_required`
to ``1`` in the :file:`records.config` file.

How do I enable forward proxy mode
----------------------------------

Please refer to the :ref:`forward-proxy` documentation.

How do I interpret the Via: header code?
----------------------------------------

The ``Via`` header string can be decoded with the `Via Decoder Ring <http://trafficserver.apache.org/tools/via>`_.

Support for HTTP Expect: Header
-------------------------------

Traffic Server currently does not handle request Expect: headers
according to the HTTP/1.1 spec.

Note that clients such as cURL automatically send Expect: for POST
requests with large POST bodies, with a 1 second timeout if a 100
Continue response is not received. To avoid the timeout when using cURL
as a client to Traffic Server, you can turn off the Expect: header as
follows::

   curl -H"Expect:" http://www.example.com/

C (libcurl)::

   struct curl_slist *header_list=NULL;
   header_list = curl_slist_append(header_list, "Expect:");
   curl_easy_setopt(my_curlp, CURLOPT_HTTPHEADER, header_list);

php::

   curl_setopt($ch, CURLOPT_HTTPHEADER, array('Expect:'));

Troubleshooting Tips
====================

The throughput statistic is inaccurate
--------------------------------------

Traffic Server updates the throughput statistic after it has transferred
an entire object. For larger files, the byte count increases sharply at
the end of a transfer. The complete number of bytes transferred is
attributed to the last 10-second interval, although it can take several
minutes to transfer the object. This inaccuracy is more noticeable with
a light load. A heavier load yields a more accurate statistic.

You are unable to execute Traffic Line commands
-----------------------------------------------

Traffic Line commands do not execute under the following conditions:

- **When the traffic_manager process is not running** Check to see
  if the :program:`traffic_manager` process is running by entering the
  following command: ``pgrep -l traffic_manager``

  If the :program:`traffic_manager` process is not running, then enter the
  following command from the Traffic Server ``bin`` directory to start it:
  ``./traffic_manager``

  .. this is wrong

  You should always start and stop Traffic Server with the
  :program:`trafficserver start`` and :program:`trafficserver stop` commands to ensure
  that all the processes start and stop correctly. For more information,
  refer to :ref:`getting-started`.

- **When you are not executing the command from $TSHome/bin** If the Traffic Server
  ``bin`` directory is not in your path, then prepend the Traffic Line
  commands with ``./`` (for example, ``./traffic_line -h``). 

- **When multiple Traffic Server installations are present and you are not
  executing the Traffic Line command from the active Traffic Server path
  specified in ``/etc/trafficserver``**


You observe inconsistent behavior when one node obtains an object from another node in the cluster
--------------------------------------------------------------------------------------------------

As part of the initial system preparation, you must synchronize the
clocks on all nodes in your cluster. Minor time differences do not cause
problems, but differences of more than a few minutes can affect Traffic
Server operation.

You should run a clock synchronization daemon such as xntpd. To obtain
the latest version of xntpd, go to ``http://www.eecis.udel.edu/~ntp/``

Web browsers display an error document with a 'data missing' message
--------------------------------------------------------------------

A message similar to the following might display in web browsers: ::

      Data Missing

      This document resulted from a POST operation and has expired from the cache. You can repost the form data to recreate the document by pressing the Reload button.

This is a Web browser issue and not a problem specific to (or caused by)
Traffic Server. Because Web browsers maintain a separate local cache in
memory and/or disk on the client system, messages about documents that
have expired from cache refer to the browser's local cache and *not*
to the Traffic Server cache. There is no Traffic Server message or
condition that can cause such messages to appear in a web browser.

Traffic Server does not resolve any websites
--------------------------------------------

The browser indicates that it is contacting the host and then times out
with the following message: ::

        The document contains no data; Try again later, or contact the server's Administrator...

Make sure the system is configured correctly and that Traffic Server can
read the name resolution file:

-  Check if the server can resolve DNS lookups by issuing the nslookup
   command (for example, ``nslookup www.myhost.com``).
-  Check if the :manpage:`resolv.conf(5)` file contains valid IP addresses
   for your DNS servers.
-  On some systems, if the :manpage:`resolv.conf(5)` file is unreadable or
   has no name server entry, then the operating system uses
   ``localhost`` as a name server. Traffic Server, however, does not use
   this convention. If you want to use ``localhost`` as a name server,
   then you must add a name server entry for ``127.0.0.1`` or
   ``0.0.0.0`` in the :manpage:`resolv.conf(5)` file.
-  Check that the Traffic Server user account has permission to read the
   /etc/resolv.conf file. If it does not, then change the file
   permissions to ``rw-r--r--`` (``644``)

'Maximum document size exceeded' message in the system log file
---------------------------------------------------------------

The following message appears in the system log file: ::

         WARNING: Maximum document size exceeded

A requested object was larger than the maximum size allowed in the
Traffic Server cache, so Traffic Server provided proxy service for the
oversized object but did not cache it. To set the object size limit for
the cache, modify the :ts:cv:`proxy.config.cache.max_doc_size`
variable in the records.config file. If you do not want to limit the
size of objects in the cache, then set the document size
to ``0`` (zero).

'DrainIncomingChannel' message in the system log file
-----------------------------------------------------

The following messages may appear in the system log file: ::

     Feb 20 23:53:40 louis traffic_manager[4414]: ERROR ==> [drainIncomingChannel] Unknown message: 'GET http://www.telechamada.pt/ HTTP/1.0'
     Feb 20 23:53:46 louis last message repeated 1 time
     Feb 20 23:53:58 louis traffic_manager[4414]: ERROR ==> [drainIncomingChannel] Unknown message: 'GET http://www.ip.pt/ HTTP/1.0'

These error messages indicate that a browser is sending HTTP requests to
one of the Traffic Server cluster ports - either ``rsport`` (default
port 8088) or ``mcport`` (default port 8089). Traffic Server discards
the request; this error does not cause any Traffic Server problems. The
misconfigured browser must be reconfigured to use the correct proxy
port. Traffic Server clusters work best when configured to use a
separate network interface and cluster on a private subnet, so that
client machines have no access to the cluster ports.

'No cop file' message in the system log file
--------------------------------------------

The following message appears repeatedly in the system log file: ::

     traffic_cop[16056]: encountered "var/trafficserver/no_cop" file...exiting

The file ``var/trafficserver/no_cop`` acts as an administrative control
that instructs the :program:`traffic_cop` process to exit immediately without
starting :program:`traffic_manager` or performing any health checks. The
``no_cop`` file prevents Traffic Server from starting automatically when
it has been stopped with the option:`trafficserver stop` command. Without
this static control, Traffic Server would restart automatically upon
system reboot. The ``no_cop`` control keeps Traffic Server off until it
is explicitly restarted with the ::

   trafficserver start

command.


Warning in the system log file when manually editing vaddrs.config
------------------------------------------------------------------

If you manually edit the vaddrs.config file as a non-root user, then
Traffic Server issues a warning message in the system log file similar
to the following::

   WARNING: interface is ignored: Operation not permitted

You can safely ignore this message; Traffic Server *does* apply your
configuration edits.

Traffic Server is running but no log files are created
------------------------------------------------------

Traffic Server only writes event log files when there is information to
record. If Traffic Server is idle, then it's possible/probable that no
log files exist. In addition:

Make sure you're looking in the correct directory. By default, Traffic
Server creates log files in the logs directory. Check the location of
log files by checking the value of the variable
proxy.config.log.logfile_dir in the records.config file. Check that the
log directory has read/write permissions for the Traffic Server user
account. If the log directory does not have the correct permissions,
then the traffic_server process is unable to open or create log files.
Check that logging is enabled by checking the value of the
proxy.config.log.logging_enabled variable in the records.config file.
Check that a log format is enabled. In the records.config file, select
the standard or custom format by editing variables in the Logging Config
section.

Traffic Server shows an error indicating too many network connections
---------------------------------------------------------------------

By default, Traffic Server supports 8000 network connections: half of
this number is allocated for client connections and the remaining half
is for origin server connections. A **connection throttle event** occurs
when client or origin server connections reach 90% of half the
configured limit (3600 by default). When a connection throttle event
occurs, Traffic Server continues processing all existing connections but
will not accept new client connection requests until the connection
count falls below the limit.

Connection throttle events can occur under the following conditions:

-  If there is a **connection spike** (e.g., if thousands of client
   requests all reach Traffic Server at the same time). Such events are
   typically transient and require no corrective action.
-  If there is a **service overload** (e.g., if client requests
   continuously arrive faster than Traffic Server can service them).
   Service overloads often indicate network problems between Traffic
   Server and origin servers. Conversely, it may indicate that Traffic
   Server needs more memory, CPU, cache disks, or other resources to
   handle the client load.

If necessary, you can reset the maximum number of connections supported
by Traffic Server by editing the value of the
:ts:cv:`proxy.config.net.connections_throttle` configuration variable in
the records.config file. Do not increase the connection throttle limit
unless the system has adequate memory to handle the client connections
required. A system with limited RAM might need a throttle limit lower
than the default value. Do not set this variable below the minimum value
of 100.

Low memory symptoms
-------------------

Under heavy load, the Linux kernel can run out of RAM. This low memory
condition can cause slow performance and a variety of other system
problems. In fact, RAM exhaustion can occur even if the system has
plenty of free swap space.

Symptoms of extreme memory exhaustion include the following messages in
the system log files (``/var/log/messages``)::

   WARNING: errno 105 is ENOBUFS (low on kernel memory), consider a memory upgrade

   kernel: eth0: can't fill rx buffer (force 0)!

   kernel: recvmsg bug: copied E01BA916 seq E01BAB22

To avoid memory exhaustion, add more RAM to the system or reduce the
load on Traffic Server.

Connection timeouts with the origin server
------------------------------------------

Certain origin servers take longer than 30 seconds to post HTTP
requests, which results in connection timeouts with Traffic Server. To
prevent such connection timeouts, you must change the value of the
configuration variable proxy.config.http.connect_attempts_timeout in
the records.config file to 60 seconds or more.

