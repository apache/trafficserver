.. _configuring-the-cache:

Configuring the Cache
*********************

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

The Traffic Server cache consists of a high-speed object database called
the **object store** that indexes objects according to URLs and their
associated headers.

.. toctree::
   :maxdepth: 2

The Traffic Server Cache
========================

The Traffic Server cache consists of a high-speed object database called
the **object store**. The object store indexes objects according to URLs
and associated headers. This enables Traffic Server to store, retrieve,
and serve not only web pages, but also parts of web pages - which
provides optimum bandwidth savings. Using sophisticated object
management, the object store can cache alternate versions of the same
object (versions may differ because of dissimilar language or encoding
types). It can also efficiently store very small and very large
documents, thereby minimizing wasted space. When the cache is full,
Traffic Server removes stale data to ensure the most requested objects
are kept readily available and fresh.

Traffic Server is designed to tolerate total disk failures on any of the
cache disks. If the disk fails completely, then Traffic Server marks the
entire disk as corrupt and continues using the remaining disks. An alarm
is then created to indicate which disk failed. If all of the cache disks
fail, then Traffic Server goes into proxy-only mode.

You can perform the following cache configuration tasks:

-  Change the total amount of disk space allocated to the cache: refer
   to `Changing Cache Capacity`_.
-  Partition the cache by reserving cache disk space for specific
   protocols and origin servers/domains: refer to `Partitioning the Cache`_.
-  Delete all data in the cache: refer to `Clearing the Cache`_.
-  Override cache directives for a requested domain name, regex on a url,
   hostname or ip, with extra filters for time, port, method of the request
   (and more). ATS can be configured to never cache; always cache; 
   ignore no-cache directives, etc. These are configured in :file:`cache.config`.

The RAM Cache
=============

Traffic Server maintains a small RAM cache of extremely popular objects.
This RAM cache serves the most popular objects as quickly as possible
and reduces load on disks, especially during temporary traffic peaks.
You can configure the RAM cache size to suit your needs, as described in
:ref:`changing-the-size-of-the-ram-cache` below.

The RAM cache supports two cache eviction algorithms, a regular **LRU**
(*Least Recently Used*) and the more advanced **CLFUS** (*Clocked Least
Frequently Used by Size*, which balances recentness, frequency and size
to maximize hit rate -- similar to a most frequently used algorithm). 
The default is to use **CLFUS**, and this is controlled via 
:ts:cv:`proxy.config.cache.ram_cache.algorithm`.

Both the **LRU** and **CLFUS** RAM caches support a configuration to increase
scan resistance. In a typical **LRU**, if you request all possible objects in
sequence, you will effectively churn the cache on every request. The option
:ts:cv:`proxy.config.cache.ram_cache.use_seen_filter` can be set to add some
resistance against this problem.

In addition, **CLFUS** also supports compressing in the RAM cache itself.
This can be useful for content which is not compressed by itself (e.g.
images). This should not be confused with *Content-Encoding: gzip*, this
feature is only thereto save space internally in the RAM cache itself. As
such, it is completely transparent to the User-Agent. The RAM cache
compression is enabled with the option
:ts:cv:`proxy.config.cache.ram_cache.compress`. The default is 0, which means
no compression. Other possible values are 1 for **fastlz**, 2 for **libz** and
3 for **liblzma**.


.. _changing-the-size-of-the-ram-cache:

Changing the Size of the RAM Cache
==================================

Traffic Server provides a dedicated RAM cache for fast retrieval of
popular small objects. The default RAM cache size is automatically
calculated based on the number and size of the cache partitions you have
configured. If you've partitioned your cache according to protocol
and/or hosts, then the size of the RAM cache for each partition is
proportional to the size of that partition.

You can increase the RAM cache size for better cache hit performance.
However, if you increase the size of the RAM cache and observe a
decrease in performance (such as increased latencies), then it's
possible that the operating system requires more memory for network
resources. In such instances, you should return the RAM cache size to
its previous value.

To change the RAM cache size:

1. Stop Traffic Server.
2. Set the variable :ts:cv:`proxy.config.cache.ram_cache.size`
   to specify the size of the RAM cache. The default value of -1 means
   that the RAM cache is automatically sized at approximately 1MB per
   gigabyte of disk.
3. Restart Traffic Server. If you increase the RAM cache to a size of
   1GB or more, then restart with the :program:`trafficserver` command
   (refer to :ref:`start-traffic-server`).

 

Changing Cache Capacity
=======================

You can increase or reduce the total amount of disk space allocated to
the cache without clearing the content. To check the size of the cache
(in bytes), enter the command :option:`traffic_line -r` ``proxy.process.cache.bytes_total``.

Increasing Cache Capacity
-------------------------

To increase the total amount of disk space allocated to the cache on
existing disks or to add new disks to a Traffic Server node, follow the
steps below:

1. Stop Traffic Server.
2. Add hardware, if necessary.
3. Edit :file:`storage.config` to increase the amount of disk space allocated
   to the cache on existing disks or describe the new hardware you are adding.
4. Restart Traffic Server.

Reducing Cache Capacity
-----------------------

To reduce the total amount of disk space allocated to the cache on an
existing disk or to remove disks from a Traffic Server node, follow the
steps below:

1. Stop Traffic Server.
2. Remove hardware, if necessary.
3. Edit :file:`storage.config` to reduce the amount of disk space allocated
   to the cache on existing disks or delete the reference to the hardware you're removing.
4. Restart Traffic Server.

.. important:: In :file:`storage.config`, a formatted or raw disk must be at least 128 MB.

.. _partitioning-the-cache:

Partitioning the Cache
======================

You can manage your cache space more efficiently and restrict disk usage
by creating cache volumes with different sizes for specific protocols.
You can further configure these volumes to store data from specific
origin servers and/or domains. The volume configuration must be the same
on all nodes in a :ref:`cluster <traffic-server-cluster>`.

Creating Cache Partitions for Specific Protocols
------------------------------------------------

You can create separate volumes for your cache that vary in size to
store content according to protocol. This ensures that a certain amount
of disk space is always available for a particular protocol. Traffic
Server currently supports the **http** partition type for HTTP objects.

.. XXX: but not https?

To partition the cache according to protocol:

1. Enter a line in the :file:`volume.config` file for
   each volume you want to create
2. Restart Traffic Server.

Making Changes to Partition Sizes and Protocols
-----------------------------------------------

After you've configured your cache volumes based on protocol, you can
make changes to the configuration at any time. Before making changes,
note the following:

-  You must stop Traffic Server before you change the cache volume size
   and protocol assignment.
-  When you increase the size of a volume, the contents of the volume
   are *not* deleted. However, when you reduce the size of a volume, the
   contents of the volume *are* deleted.
-  When you change the volume number, the volume is deleted and then
   recreated, even if the size and protocol type remain the same.
-  When you add new disks to your Traffic Server node, volume sizes
   specified in percentages will increase proportionately.
-  A lot of changes to volume sizes might result in disk fragmentation,
   which affects performance and hit rate. You should clear the cache
   before making many changes to cache volume sizes (refer to `Clearing the Cache`_).

Partitioning the Cache According to Origin Server or Domain
-----------------------------------------------------------

After you have partitioned the cache according to size and protocol, you
can assign the volumes you created to specific origin servers and/or
domains. You can assign a volumes to a single origin server or to
multiple origin servers. However, if a volumes is assigned to multiple
origin servers, then there is no guarantee on the space available in the
volumes for each origin server. Content is stored in the volumes
according to popularity. In addition to assigning volumes to specific
origin servers and domains, you must assign a generic volume to store
content from all origin servers and domains that are not listed. This
generic volume is also used if the partitions for a particular origin
server or domain become corrupt. If you do not assign a generic volume,
then Traffic Server will run in proxy-only mode.

.. note::

    You do *not* need to stop Traffic Server before you assign
    volumes to particular hosts or domains. However, this type of
    configuration is time-consuming and can cause a spike in memory usage.
    Therefore, it's best to configure partition assignment during periods of
    low traffic.

To partition the cache according to hostname and domain:

1. Configure the cache volumes according to size and protocol, as
   described in `Creating Cache Partitions for Specific Protocols`_.
2. Create a separate volume based on protocol for each host and domain,
   as well as an additional generic partition to use for content that
   does not belong to these origin servers or domains. The volumes do
   not need to be the same size.
3. Enter a line in the :file:`hosting.config` file to
   allocate the volume(s) used for each origin server and/or domain
4. Assign a generic volume to use for content that does not belong to
   any of the origin servers or domains listed in the file. If all
   volumes for a particular origin server become corrupt, then Traffic
   Server will also use the generic volume to store content for that
   origin server as per :file:`hosting.config`.
5. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

Configuring the Cache Object Size Limit
=======================================

By default, Traffic Server allows objects of any size to be cached. You
can change the default behavior and specify a size limit for objects in
the cache via the steps below:

1. Set :ts:cv:`proxy.config.cache.max_doc_size`
   to specify the maximum size allowed for objects in the cache in
   bytes. ``0`` (zero) if you do not want a size limit.
2. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

.. _clearing-the-cache:

Clearing the Cache
==================

When you clear the cache, you remove all data from the entire cache -
including data in the host database. You should clear the cache before
performing certain cache configuration tasks, such as partitioning. You
cannot clear the cache when Traffic Server is running.

To clear the cache:

1. Stop Traffic Server (refer to :ref:`Stopping Traffic Server <stop-traffic-server>`)
2. Enter the following command to clear the cache: ::

        traffic_server -Cclear

   The ``clear`` command deletes all data in the object store and the
   host database. Traffic Server does not prompt you to confirm the
   deletion.

3. Restart Traffic Server (refer to :ref:`Starting Traffic Server <start-traffic-server>`).

Removing an Object From the Cache
=================================

Traffic Server accepts the custom HTTP request method ``PURGE`` when
removing a specific object from cache. If the object is found in the
cache and is successfully removed, then Traffic Server responds with a
``200 OK`` HTTP message; otherwise, a ``404 File Not Found`` message is
returned.

In the following example, Traffic Server is running on the domain
``example.com`` and you want to remove the image ``remove_me.jpg``
from cache. Because by default we do not permit ``PURGE`` requests from
any other IP, we connect to the daemon via localhost: ::

      $ curl -X PURGE -H 'Host: example.com' -v "http://localhost/remove_me.jpg"
      * About to connect() to localhost port 80 (#0)
      * Trying 127.0.0.1... connected
      * Connected to localhost (127.0.0.1) port 80 (#0)

      > PURGE /remove_me.jpg HTTP/1.1
      > User-Agent: curl/7.19.7
      > Host: example.com
      > Accept: */*
      >
      < HTTP/1.1 200 Ok
      < Date: Thu, 08 Jan 2010 20:32:07 GMT
      < Connection: keep-alive

The next time Traffic Server receives a request for the removed object,
it will contact the origin server to retrieve it (i.e., it has been
purged from the Traffic Server cache).

Note: The procedure above only removes an object from a *specific*
Traffic Server cache. Users may still see the old (removed) content if
it was cached by intermediary caches or by the end-users' web browser.

.. _inspecting-the-cache:

Inspecting the Cache
====================

Traffic Server provides a Cache Inspector utility that enables you to
view, delete, and invalidate URLs in the cache (HTTP only). The Cache
Inspector utility is a powerful tool that's capable of deleting *all*
the objects in your cache; therefore, make sure that only authorized
administrators are allowed to access this utility, see :ref:`controlling-client-access-to-cache` and the ``@src_ip`` option in :file:`remap.config`.

Accessing the Cache Inspector Utility
-------------------------------------

To access the Cache Inspector utility, follow the steps below:

#. Set :ts:cv:`proxy.config.http_ui_enabled` to ``1``.
#. To access the cache inspector in reverse proxy mode, you must add a
   remap rule to :file:`remap.config` to expose the URL. This should be
   restricted to a limited set of hosts using the ``@src_ip`` option.
   To restrict access to the network 172.28.56.0/24, use ::

      map http://yourhost.com/myCI http://{cache} @action=allow @src_ip=172.28.56.1-172.28.56.254

#. From the Traffic Server ``bin`` directory, enter the following
   command to re-read the configuration file: ``traffic_line -x``
#. Open your web browser and configure it to use your Traffic Server as
   a proxy server. Type the following URL::

      http://yourhost/myCI

#. The Cache page opens.

Using the Cache Page
--------------------

The **Cache page** provides several options that enable you to view and
delete the contents of your cache:

-  Click **Lookup url** to search for a particular URL in the cache.
   When Traffic Server finds the URL in the cache, it displays details
   about the object that corresponds to the URL (such as the header
   length and the number of alternates). From the display page, you can
   delete the URL from the cache.
-  Click **Delete url** to delete a particular URL or list of URLs from
   the cache. Traffic Server indicates if a delete is successful.
-  Click **Regex lookup** to search for URLs that match one or more
   regular expressions. From the display page, you can delete the URLs
   listed. For example, enter the following to search for all URLs that
   end in html and are prefixed with ``http://www.dianes.com``:
   ``http://www.dianes.com/.*\.html$``
-  Click **Regex delete** to delete all URLs that match a specified
   regular expression. For example, enter the following to delete all
   HTTP URLs that end in ``html``: ``http://.*\.html$``
-  Click **Regex invalidate** to invalidate URLs that match a specified
   regular expression. When you invalidate a URL, Traffic Server marks
   the object that corresponds to the URL as stale in the cache. Traffic
   Server then contacts the origin server to check if the object is
   still fresh (revalidates) before serving it from the cache.

.. note::

    Only one administrator should delete and invalidate cache
    entries from the Cache page at any point in time. Changes made by
    multiple administrators at the same time can lead to unpredictable
    results.
