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

.. _faq:

Frequently Asked Questions
==========================

.. toctree::
   :maxdepth: 1

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
can continue to download the object from the origin server, using the :ref:`background fill feature <background_fill>`.
It will continue downloading based on the :ts:cv:`proxy.config.http.background_fill_active_timeout` and :ts:cv:`proxy.config.http.background_fill_completed_threshold` settings.

Can Traffic Server cache Java applets, JavaScript programs, or other application files like VBScript?
-----------------------------------------------------------------------------------------------------

Yes, Traffic Server can store and serve Java applets, JavaScript
programs, VBScripts, and other executable objects from its cache
according to the freshness and cacheability rules for HTTP objects.
Traffic Server does not execute the applets, scripts, or programs.
These objects run entirely client-side (the system which originated
the request for the objects), and do not execute on the server.

In Squid- and Netscape-format log files, what do the cache result codes mean?
-----------------------------------------------------------------------------

This is described in detail in the :ref:`admin-logging-cache-results` documentation.

What is recorded by the ``cqtx`` field in a custom log file?
------------------------------------------------------------

-  In *forward proxy mode*, the ``cqtx`` field records the complete client
   request in the log file (for example, ``GET http://www.company.com HTTP/1.0``).
-  In *reverse proxy mode*, the ``cqtx`` field records the hostname or IP
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

Refer to :ts:cv:`proxy.config.hostdb.ttl_mode` for more info.

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
requests according to the map rules in :file:`remap.config`. All
other requests that do not match a map rule are served in forward
proxy mode.

If you want to run in reverse proxy only mode (wherein Traffic Server
does not serve requests that fail to match a map rule), then you must
set the configuration variable :ts:cv:`proxy.config.url_remap.remap_required`
to ``1`` in :file:`records.config`.

How do I enable forward proxy mode
----------------------------------

Please refer to the :ref:`forward-proxy` documentation.

How do I interpret the Via: header code?
----------------------------------------

The ``Via`` header string can be decoded with the `Via Decoder Ring <https://trafficserver.apache.org/tools/via>`_.

The Via header is an optional HTTP header added by Traffic Server and other HTTP proxies. If a request goes through multiple proxies, each one appends its Via header content to the end of the existing Via header. Via header content is for general information and diagnostic use only and should not be used as a programmatic interface to Traffic Server. The header is cached by each intermediary with the object as received from its downstream node. Thus, the last node in the list to report a cache hit is the end of the transaction for that specific request. Nodes reported earlier were from a previous transaction.

The form of the Via header is

Via: <protocol> <proxyname> (<product/version> [<via-codes>])

================= ==========================
Value             Meaning
================= ==========================
<protocol>        the scheme and version of the HTTP request
<proxyname>       the configured name of the proxy server
<product/version> the Traffic Server product name and version
<via-codes>       a string of alphabetic codes presenting status information about the proxy handling of the HTTP request
================= ==========================

For example:
Via: HTTP/1.0 storm (Traffic-Server/4.0.0   [cMs f ])

- [u lH o  f  pS eN]     cache hit
- [u lM oS fF pS eN]     cache miss
- [uN l oS f  pS eN]     no-cache origin server fetch

The short status code shows the cache-lookup, server-info and cache-fill information as listed in the full status code description below. The long status code list provided in older, commercial versions of Traffic Server can be restored by setting the verbose_via_str config variable.
The character strings in the via-code show [<request results>:<proxy op>] where <request results> represents status information about the results of the client request and <proxy op> represent some information about the proxy operations performed during request processing. The full via-code status format is

[u<client-info> c<cache-lookup> s<server-info> f<cache-fill> p<proxy-info> e<error-codes> : t<tunnel-info>c<cache-type><cache-lookup-result> p<parent-proxy> s<server-conn-info>]


u client-info
^^^^^^^^^^^^^^^^^^^^^

Request headers received from client. Value is one of:

===== ==========================
Value             Meaning
===== ==========================
C     cookie
E     error in request
I     If Modified Since (IMS)
N     no-cache
S     simple request (not conditional)
===== ==========================

c cache-lookup
^^^^^^^^^^^^^^^^^^^^^
Result of Traffic Server cache lookup for URL. Value is one of:

===== ==========================
Value             Meaning
===== ==========================
A     in cache, not acceptable (a cache "MISS")
H     in cache, fresh (a cache "HIT")
M     miss (a cache "MISS")
R     in cache, fresh RAM hit (a cache "HIT")
S     in cache, stale (a cache "MISS")
blank no cache lookup performed
===== ==========================

s server-info
^^^^^^^^^^^^^^^^^^^^^
Response information received from origin server. Value is one of:

===== ==========================
Value             Meaning
===== ==========================
E     error in response
N     not-modified
S     served
blank no server connection needed
===== ==========================

f cache-fill
^^^^^^^^^^^^^^^^^^^^^
Result of document write to cache. Value is one of:

===== ==========================
Value             Meaning
===== ==========================
D     cached copy deleted
U     updated old cache copy
W     written into cache (new copy)
blank no cache write performed
===== ==========================

p proxy-info
^^^^^^^^^^^^^^^^^^^^^
Proxy operation result. Value is one of:

===== ==========================
Value             Meaning
===== ==========================
N     not-modified
R     origin server revalidated
S     served
===== ==========================

e error-codes
^^^^^^^^^^^^^^^^^^^^^

Value is one of:

===== ==========================
Value             Meaning
===== ==========================
A     authorization failure
C     connection to server failed
D     dns failure
L     loop detected
F     request forbidden
H     header syntax unacceptable
N     no error
R     cache read error
M     moved temporarily
S     server related error
T     connection timed out
===== ==========================

: = Separates proxy request result information from operation detail codes

t tunnel-info
^^^^^^^^^^^^^^^^^^^^^
Proxy-only service operation. Value is one of:

===== ==========================
Value             Meaning
===== ==========================
A     tunnel authorization
F     tunneling due to a header field (such as presence of If-Range header)
M     tunneling due to a method (e.g. CONNECT)
N     tunneling due to no forward
O     tunneling because cache is turned off
U     tunneling because of url (url suggests dynamic content)
blank no tunneling
===== ==========================

c cache-type and cache-lookup
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
cache result values (2 characters)

cache-type character value is one of

===== ==========================
Value             Meaning
===== ==========================
C     cache
L     cluster, (not used)
P     parent
S     server
blank cache miss or no cache lookup
===== ==========================

cache-lookup-result character value is one of:

===== ==========================
Value             Meaning
===== ==========================
C     cache hit, but config forces revalidate
D     cache hit, but method forces revalidated (e.g. ftp, not anonymous)
H     cache hit
I     conditional miss (client sent conditional, fresh in cache, returned 412)
K     cookie miss
M     cache miss (url not in cache)
N     conditional hit (client sent conditional, doc fresh in cache, returned 304)
S     cache hit, but expired
U     cache hit, but client forces revalidate (e.g. Pragma: no-cache)
blank no cache lookup
===== ==========================

p parent-proxy
^^^^^^^^^^^^^^^^^^^^^
parent proxy connection status

===== ==========================
Value             Meaning
===== ==========================
F     connection open failed
S     connection opened successfully
blank no parent proxy
===== ==========================

s server-conn-info
^^^^^^^^^^^^^^^^^^^^^
origin server connection status

===== ==========================
Value             Meaning
===== ==========================
F     connection open failed
S     connection opened successfully
blank no server connection
===== ==========================


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

You are unable to execute Traffic Control commands
--------------------------------------------------

:program:`traffic_ctl` commands do not execute under the following conditions:

**When the traffic_manager process is not running**
    Check to see if the :program:`traffic_manager` process is running by entering the
    following command::

        pgrep -l traffic_manager

    If the :program:`traffic_manager` process is not running, then enter the
    following command from the Traffic Server ``bin`` directory to start it::

        ./traffic_manager

.. XXX: this is wrong

    You should always start and stop Traffic Server with the
    :program:`trafficserver start` and :program:`trafficserver stop` commands to ensure
    that all the processes start and stop correctly. For more information,
    refer to :ref:`getting-started`.

**When you are not executing the command from $TSHome/bin**
    If the Traffic Server ``bin`` directory is not in your path, then prepend the
    Traffic Control commands with ``./`` (for example, ``./traffic_ctl -h``).

**When multiple Traffic Server installations are present and you are not
executing the Traffic Control command from the active Traffic Server path
specified in ``/etc/trafficserver``**


Web browsers display an error document with a 'data missing' message
--------------------------------------------------------------------

A message similar to the following might display in web browsers: ::

      Data Missing

      This document resulted from a POST operation and has expired from the cache. You can repost the form data to recreate the document by pressing the Reload button.

This is a Web browser issue and not a problem specific to (or caused by)
Traffic Server. Because Web browsers maintain a separate local cache in
memory and/or disk on the client system, messages about documents that
have expired from cache refer to the browser's local cache and not
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
   :manpage:`resolv.conf(5)` file. If it does not, then change the file
   permissions to ``rw-r--r--`` (``644``).

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

Traffic Server is running but no log files are created
------------------------------------------------------

Traffic Server only writes event log files when there is information to
record. If Traffic Server is idle, then it's possible that no log files
exist.

If Traffic Server is not idle, and you still do not see log files being
generated, verify the following:

- Make sure you're looking in the correct directory. By default, Traffic
  Server creates log files in the ``logs`` directory. This can be modified
  by changing :ts:cv:`proxy.config.log.logfile_dir` in :file:`records.config`.

- Check that the log directory has read/write permissions for the Traffic
  Server user account. If the log directory does not have the correct
  permissions, then the :program:`traffic_server` process will be unable to
  open or create log files.

- Check that logging is enabled by checking the value of the
  :ts:cv:`proxy.config.log.logging_enabled` variable in :file:`records.config`.

- Check that a log format is enabled. In :file:`records.config`, select
  the standard or custom format by editing variables in the Logging Config
  section.

Traffic Server shows an error indicating too many network connections
---------------------------------------------------------------------

By default, Traffic Server supports 8000 network connections. Half of
this number is allocated for client connections and the remaining half
is for origin server connections. A *connection throttle event* occurs
when either client or origin server connections reach 90% of half the
configured total limit (3600 by default). When a connection throttle event
occurs, Traffic Server continues processing all existing connections but
will not accept new client connection requests until the connection
count falls below the limit.

Connection throttle events can occur under the following conditions:

Connection Spike
    Too many client requests (enough to exceed your configured maximum connections)
    all reach Traffic Server at the same time. Such events are typically transient
    and require no corrective action if your connection limits are already
    configured appropriately for your Traffic Server and origin resources.

Service Overload
    Client requests are arriving at a rate faster than that which Traffic
    Server can service them. This may indicate network problems between Traffic
    Server and origin servers or that Traffic Server may require more memory, CPU,
    cache disks, or other resources to handle the client load.

If necessary, you can adjust the maximum number of connections supported
by Traffic Server by editing :ts:cv:`proxy.config.net.connections_throttle` in
:file:`records.config`.

.. note::

    Do not increase the connection throttle limit unless the system has adequate
    memory to handle the client connections required. A system with limited RAM
    might need a throttle limit lower than the default value. Do not set this
    variable below the minimum value of ``100``.

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

Config checker
--------------

Traffic Server supports the below command to validate the config offline, in order to
allow the config to be pre-checked for possible service disruptions due to syntax errors::

   traffic_server -Cverify_config -D<config_dir>

<config_dir> is the location of the config files to be validated.

Connection timeouts with the origin server
------------------------------------------

By default, Traffic Server will timeout after 30 seconds when contacting
origin servers. If you cannot avoid such timeouts by otherwise addressing the
performance on your origin servers, you may adjust the origin connection timeout
in Traffic Server by changing :ts:cv:`proxy.config.http.connect_attempts_timeout`
in :file:`records.config` to a larger value.
