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

.. _admin-cache-storage:

Cache Storage
*************

The Traffic Server cache consists of a high-speed object database called
the :term:`object store` that indexes :term:`cache objects <cache object>`
according to URLs and their associated headers.

.. toctree::
   :maxdepth: 2

The Traffic Server Cache
========================

The Traffic Server cache consists of a high-speed object database called
the :term:`object store`. The object store indexes
:term:`cache objects <cache object>` according to URLs and associated headers.
This enables Traffic Server to store, retrieve, and serve not only web pages,
but also parts of web pages - which provides optimum bandwidth savings. Using
sophisticated object management, the object store can cache
:term:`alternate` versions of the same object (versions may differ because of
dissimilar language or encoding types). It can also efficiently store very
small and very large documents, thereby minimizing wasted space. When the
cache is full, Traffic Server removes :term:`stale` data to ensure the most
requested objects are kept readily available and fresh.

Traffic Server is designed to tolerate total disk failures on any of the
cache disks. If the disk fails completely, then Traffic Server marks the
entire disk as corrupt and continues using the remaining disks. An alarm
is then created to indicate which disk failed. If all of the cache disks
fail, then Traffic Server goes into proxy-only mode.

You can perform the following cache configuration tasks:

-  Change the total amount of disk space allocated to the cache; refer
   to `Changing Cache Capacity`_.

-  Partition the cache by reserving cache disk space for specific
   protocols and :term:`origin servers/domains <origin server>`; refer to
   `Partitioning the Cache`_.

-  Delete all data in the cache; refer to `Clearing the Cache`_.

-  Override cache directives for a requested domain name, regex on a url,
   hostname or ip, with extra filters for time, port, method of the request,
   and more. ATS can be configured to never cache, always cache,
   ignore no-cache directives, etc. These are configured in :file:`cache.config`.

The RAM Cache
=============

Traffic Server maintains a small RAM cache of extremely popular objects.
This RAM cache serves the most popular objects as quickly as possible
and reduces load on disks, especially during temporary traffic peaks.
You can configure the RAM cache size to suit your needs, as described in
:ref:`changing-the-size-of-the-ram-cache` below.

The RAM cache supports two cache eviction algorithms, a regular *LRU*
(Least Recently Used) and the more advanced *CLFUS* (Clocked Least
Frequently Used by Size; which balances recentness, frequency, and size
to maximize hit rate, similar to a most frequently used algorithm).
The default is to use *LRU*, and this is controlled via
:ts:cv:`proxy.config.cache.ram_cache.algorithm`.

Both the *LRU* and *CLFUS* RAM caches support a configuration to increase
scan resistance. In a typical *LRU*, if you request all possible objects in
sequence, you will effectively churn the cache on every request. The option
:ts:cv:`proxy.config.cache.ram_cache.use_seen_filter` can be set to add some
resistance against this problem.

In addition, *CLFUS* also supports compressing in the RAM cache itself.
This can be useful for content which is not compressed by itself (e.g.
images). This should not be confused with ``Content-Encoding: gzip``, this
feature is only present to save space internally in the RAM cache itself. As
such, it is completely transparent to the User-Agent. The RAM cache
compression is enabled with the option
:ts:cv:`proxy.config.cache.ram_cache.compress`.

Possible values are:

======= =============================
Value   Meaning
======= =============================
0       No compression (*default*)
1       *fastlz* compression
2       *libz* compression
3       *liblzma* compression
======= =============================

.. _changing-the-size-of-the-ram-cache:

Changing the Size of the RAM Cache
==================================

Traffic Server provides a dedicated RAM cache for fast retrieval of
popular small objects. The default RAM cache size is automatically
calculated based on the number and size of the
:term:`cache partitions <cache partition>` you have configured. If you've
partitioned your cache according to protocol and/or hosts, then the size of
the RAM cache for each partition is proportional to the size of that partition.

You can increase the RAM cache size for better cache hit performance.
However, if you increase the size of the RAM cache and observe a
decrease in performance (such as increased latencies), then it's
possible that the operating system requires more memory for network
resources. In such instances, you should return the RAM cache size to
its previous value.

To change the RAM cache size:

#. Stop Traffic Server.

#. Set the variable :ts:cv:`proxy.config.cache.ram_cache.size`
   to specify the size of the RAM cache. The default value of ``-1`` means
   that the RAM cache is automatically sized at approximately 1MB per
   gigabyte of disk.

#. Restart Traffic Server. If you increase the RAM cache to a size of
   1GB or more, then restart with the :program:`trafficserver` command
   (refer to :ref:`start-traffic-server`).

Disabling the RAM Cache
-----------------------

It is possible to disable the RAM cache. If you have configured your
storage using the :file:`volume.config` you can add an optional directive
of ``ramcache=false`` to whichever volumes you wish to have it disabled on.
This may be desirable for volumes composed of storage like RAM disks where
you may want to avoid double RAM caching.

Changing Cache Capacity
=======================

You can increase or reduce the total amount of disk space allocated to
the cache without clearing the content. To check the size of the cache
(in bytes), enter the command :option:`traffic_ctl metric get`
``proxy.process.cache.bytes_total``.

Increasing Cache Capacity
-------------------------

To increase the total amount of disk space allocated to the cache on
existing disks, or to add new disks to a Traffic Server node:

#. Stop Traffic Server.

#. Add hardware, if necessary.

#. Edit :file:`storage.config` to increase the amount of disk space allocated
   to the cache on existing disks or describe the new hardware you are adding.

#. Restart Traffic Server.

Reducing Cache Capacity
-----------------------

To reduce the total amount of disk space allocated to the cache on an
existing disk, or to remove disks from a Traffic Server node:

#. Stop Traffic Server.

#. Remove hardware, if necessary.

#. Edit :file:`storage.config` to reduce the amount of disk space allocated
   to the cache on existing disks or delete the reference to the hardware you're removing.

#. Restart Traffic Server.

.. important:: In :file:`storage.config`, a formatted or raw disk must be at least 128 MB.

.. _partitioning-the-cache:

Partitioning the Cache
======================

You can manage your cache space and restrict disk usage from specific
:term:`origin servers <origin server>` and/or domains by creating
:term:`cache volumes <cache volume>`.

Partitioning the Cache According to Origin Server or Domain
-----------------------------------------------------------

.. XXX: rewrite to remove repetitious single-v-multiple points; break out global partition note for clarify; fix up plurality

You can assign the volumes you create to specific origin servers and/or
domains. You can assign a volume to a single origin server or to
multiple origin servers. However, if a volume is assigned to multiple
origin servers, then there is no guarantee on the space available in the
volumes for each origin server. Content is stored in the volumes
according to popularity. In addition to assigning volumes to specific
origin servers and domains, you must assign a generic volume to store
content from all origin servers and domains that are not listed. This
generic volume is also used if the partitions for a particular origin
server or domain become corrupt. If you do not assign a generic volume,
then Traffic Server will run in proxy-only mode. The volumes do
not need to be the same size.

.. note::

    You do not need to stop Traffic Server before you assign volumes
    to particular hosts or domains. However, this type of configuration
    is time-consuming and can cause a spike in memory usage.
    Therefore, it's best to configure partition assignment during
    periods of low traffic.

To partition the cache according to hostname and domain:

#. Enter a line in the :file:`hosting.config` file to
   allocate the volume(s) used for each origin server and/or domain.
#. Assign a generic volume to use for content that does not belong to
   any of the origin servers or domains listed in the file. If all
   volumes for a particular origin server become corrupt, then Traffic
   Server will also use the generic volume to store content for that
   origin server as per :file:`hosting.config`.
#. Run the command :option:`traffic_ctl config reload` to apply the configuration
   changes.

Configuring the Cache Object Size Limit
=======================================

By default, Traffic Server allows objects of any size to be cached. You
can change the default behavior and specify a size limit for objects in
the cache via the steps below:

#. Set :ts:cv:`proxy.config.cache.max_doc_size`
   to specify the maximum size in bytes allowed for objects in the cache.
   A setting of ``0`` (zero) will permit cache objects to be unlimited in size.
#. Run the command :option:`traffic_ctl config reload` to apply the configuration
   changes.

.. _clearing-the-cache:

Clearing the Cache
==================

When you clear the cache, you remove all data from the entire cache -
including data in the host database. You should clear the cache before
performing certain cache configuration tasks such as partitioning. You
cannot clear the cache when Traffic Server is running.

To clear the cache:

#. Stop Traffic Server (see :ref:`stop-traffic-server`)
#. Enter the following command to clear the cache::

        traffic_server -Cclear

   The ``clear`` command deletes all data in the object store and the
   host database. Traffic Server does not prompt you to confirm the
   deletion.

#. Restart Traffic Server (see :ref:`start-traffic-server`).

Removing an Object From the Cache
=================================

Traffic Server accepts the custom HTTP request method ``PURGE`` when
removing a specific object from cache. If the object is found in the
cache and is successfully removed, then Traffic Server responds with a
``200 OK`` HTTP message; otherwise, a ``404 File Not Found`` message is
returned.

.. note::

    By default, the PURGE request method is only processed if received on
    the localhost interface.

In the following example, Traffic Server is running on the domain
``example.com`` and you want to remove the image ``remove_me.jpg``
from cache. Because by default we do not permit ``PURGE`` requests
from any other IP, we connect to the daemon via localhost: ::

      $ curl -vX PURGE --resolve example.com:80:127.0.0.1 http://example.com/remove_me.jpg
      * About to connect() to example.com port 80 (#0)
      * Trying 127.0.0.1... connected
      * Connected to example.com (127.0.0.1) port 80 (#0)

      > PURGE /remove_me.jpg HTTP/1.1
      > User-Agent: curl/7.19.7
      > Host: example.com
      > Accept: */*
      >
      < HTTP/1.1 200 Ok
      < Date: Thu, 08 Jan 2010 20:32:07 GMT
      < Connection: keep-alive

The next time Traffic Server receives a request for the removed object,
it will contact the origin server to retrieve a new copy, which will replace
the previously cached version in Traffic Server.

This procedure only removes the index to the object from a specific Traffic Server
cache. While the object remains on disk, Traffic Server will no longer able to find
the object. The next request for that object will result in a fresh copy of the
object fetched. Users may still see the old (removed) content if it was cached by
intermediary caches or by the end-users' web browser.

Pushing an Object into the Cache
================================

Traffic Server accepts the custom HTTP request method ``PUSH`` to put an object
into the cache. If the object is successfully written to the cache, then
Traffic Server responds with a ``200 OK`` HTTP message; otherwise a
``400 Malformed Pushed Response Header`` message is returned.

To push an object, first save the object headers and body into a file. For instance: ::

      $ curl -s -i -o /path/to/file "http://example.com/push_me.html"
      $ cat /path/to/file
      HTTP/1.1 200 OK
      Date: Wed, 31 May 2017 16:01:59 GMT
      Access-Control-Allow-Origin: *
      Cache-Control: max-age=10800, public
      Last-Modified: Wed, 31 May 2017 16:01:59 GMT
      Content-Type: text/html
      Age: 0
      Content-Length: 176970

      <!DOCTYPE html><html id= ...

Then, to push the object, post the object using the PUSH method: ::

      $ curl -x -s -o /dev/null -X PUSH --data-binary /path/to/file "http://example.com/push_me.html"

.. _inspecting-the-cache:

Inspecting the Cache
====================

Traffic Server provides a Cache Inspector utility that enables you to
view, delete, and invalidate URLs in the cache (HTTP only). The Cache
Inspector utility is a powerful tool that's capable of deleting all
the objects in your cache. Therefore, make sure that only authorized
administrators are allowed to access this utility through proper use
of the ``@src_ip`` option in :file:`remap.config` and the instructions
detailed in :ref:`admin-controlling-access`.

Accessing the Cache Inspector Utility
-------------------------------------

To access the Cache Inspector utility:

#. Set :ts:cv:`proxy.config.http_ui_enabled` to ``1``.
#. To access the cache inspector in reverse proxy mode, you must add a
   remap rule to :file:`remap.config` to expose the URL. This should be
   restricted to a limited set of hosts using the ``@src_ip`` option.
   To restrict access to the network 172.28.56.0/24, use ::

      map http://yourhost.com/myCI/ http://{cache} @action=allow @src_ip=172.28.56.1-172.28.56.254

#. Reload the Traffic Server configuration by running :option:`traffic_ctl config reload`.
#. Open your web browser and go to the following URL::

      http://yourhost/myCI/

   You will now be presented with the Cache Inspector interface.

Using the Cache Inspector Utility
---------------------------------

The Cache Inspector Utility provides several options that enable you to view and
delete the contents of your cache.

Lookup URL
    Search for a particular URL in the cache. When Traffic Server finds the URL
    in the cache, it will display details of the object that corresponds to the
    URL (e.g. header length and number of alternates). The option to delete the
    URL from the cache will be presented.

Delete URL
    Delete the object from the cache which corresponds to the given URL. Success
    or failure will be indicated after a delete has been attempted.

Regex Lookup
    Search URLs within the cache using one or more regular expressions.

Regex Delete
    Deletes all objects from the cache which match the provided regular
    expressions.

Regex Invalidate
    Marks any objects in the cache which match the given regular expressions as
    stale. Traffic Server will contact the relevant origin server(s) to confirm
    the validity and freshness of the cached object, updating the cached object
    if necessary.

.. note::

    Only one administrator should delete and invalidate cache entries from the
    Cache Inspector at any point in time. Changes made by multiple
    administrators at the same time can lead to unpredictable results.

If-Modified-Since/If-None-Match
-------------------------------

Traffic Server will respond to matching If-Modified-Since/If-None-Match requests
with a ``304 Not Modified`` HTTP message.

This table describes how Traffic Server handles these types of requests: ::

    OS = Origin Server's response HTTP message
    IMS = A GET request w/ an If-Modified-Since header
    LMs = Last-Modified header date returned by server
    INM = A GET request w/ an If-None-Match header
    E   = Etag header present
    D, D' are Last modified dates returned by the origin server and are later used for IMS
    The D date is earlier than the D' date

+----------+-----------+----------+-----------+--------------+
| Client's | Cached    | Proxy's  |   Response to client     |
| Request  | State     | Request  +-----------+--------------+
|          |           |          | OS 200    |  OS 304      |
+==========+===========+==========+===========+==============+
|  GET     | Fresh     | N/A      |  N/A      |  N/A         |
+----------+-----------+----------+-----------+--------------+
|  GET     | Stale, D' | IMS  D'  | 200, new  | 200, cached  |
+----------+-----------+----------+-----------+--------------+
|  GET     | Stale, E  | INM  E   | 200, new  | 200, cached  |
+----------+-----------+----------+-----------+--------------+
|  INM E   | Stale, E  | INM  E   | 304       | 304          |
+----------+-----------+----------+-----------+--------------+
|  INM E + | Stale,    | INM E    | 200, new *| 304          |
|  IMS D'  | E + D'    | IMS D'   |           |              |
+----------+-----------+----------+-----------+--------------+
|  IMS D   | None      | GET      | 200, new *|  N/A         |
+----------+-----------+----------+-----------+--------------+
|  INM E   | None      | GET      | 200, new *|  N/A         |
+----------+-----------+----------+-----------+--------------+
|  IMS D   | Stale, D' | IMS D'   | 200, new  | Compare      |
+----------+-----------+----------+-----------+ LMs & D'.    |
|  IMS D'  | Stale, D' | IMS D'   | 200, new  | If match, 304|
+----------+-----------+----------+-----------+ If no match, |
|  IMS D'  | Stale D   | IMS D    | 200, new *| 200, cached  |
+----------+-----------+----------+-----------+--------------+
