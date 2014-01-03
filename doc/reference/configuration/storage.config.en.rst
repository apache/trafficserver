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

==============
storage.config
==============

.. configfile:: storage.config

The :file:`storage.config` file lists all the files, directories, and/or
hard disk partitions that make up the Traffic Server cache. After you
modify the :file:`storage.config` file, you must restart Traffic Server.

Format
======

The format of the :file:`storage.config` file is::

   pathname size volume=volume_number

where :arg:`pathname` is the name of a partition, directory or file, :arg:`size`
is the size of the named partition, directory or file (in bytes), and
:arg:`volume` is the volume number that is used in :file:`volume.config`
and :file:`hosting.config`. You must specify a size for directories or
files; size is optional for raw partitions. :arg:`volume` is optional.

You can use any partition of any size. For best performance:

-  Use raw disk partitions.
-  For each disk, make all partitions the same size.
-  For each node, use the same number of partitions on all disks.
-  Group similar kinds of storage into different volumes. For example
   split out SSD's or RAM drives into their own volume.

Specify pathnames according to your operating system requirements. See
the following examples. In the :file:`storage.config` file, a formatted or
raw disk must be at least 128 MB.

When using raw disk or partitions, you should make sure the :ts:cv:`Traffic
Server user <proxy.config.admin.user_id>` used by the Traffic Server process
has read and write privileges on the raw disk device or partition. One good
practice is to make sure the device file is set with 'g+rw' and the Traffic
Server user is in the group which owns the device file.  However, some
operating systems have stronger requirements - see the following examples for
more information.

As with standard ``records.config`` integers, human readable prefixes are also
supported. They include

   - ``K`` Kilobytes (1024 bytes)
   - ``M`` Megabytes (1024^2 or 1,048,576 bytes)
   - ``G`` Gigabytes (1024^3 or 1,073,741,824 bytes)
   - ``T`` Terabytes (1024^4 or 1,099,511,627,776 bytes)


Examples
========

The following basic example shows 128 MB of cache storage in the
``/big_dir`` directory::

   /big_dir 134217728

You can use the ``.`` symbol for the current directory. Here is an
example for 64 MB of cache storage in the current directory::

   . 134217728

As an alternative, using the human readable prefixes, you can express a 64GB
cache file with::

   /really_big_dir 64G


.. note::
    When using on-filesystem cache disk storage, you can only have one such
    directory specified. This will be address in a future version.
   

Solaris Example
---------------

The following example is for the Solaris operating system::

   /dev/rdsk/c0t0d0s5
   /dev/rdsk/c0t0d1s5

.. note:: Size is optional. If not specified, the entire partition is used.

Linux Example
-------------

The following example will use an entire raw disk in the Linux operating
system::

   /dev/sde volume=1
   /dev/sdf volume=2

In order to make sure :program:`traffic_server` will have access to this disk
you can use :manpage:`udev(7)` to persistently set the right permissions. The
following rules are targeted for an Ubuntu system, and stored in
``/etc/udev/rules.d/51-cache-disk.rules``::

   # Assign /dev/sde and /dev/sdf to the tserver group
   # make the assignment final, no later changes allowed to the group!
   SUBSYSTEM=="block", KERNEL=="sd[ef]", GROUP:="tserver"

In order to apply these settings, trigger a reload with :manpage:`udevadm(8)`:::

   udevadm trigger --subsystem-match=block


FreeBSD Example
---------------

Starting with 5.1 FreeBSD dropped support for explicit raw devices. All
devices on FreeBSD can be accessed raw now.

The following example will use an entire raw disk in the FreeBSD
operating system::

   /dev/ada1
   /dev/ada2

In order to make sure :program:`traffic_server` will have access to this disk
you can use :manpage:`devfs(8)` to persistently set the right permissions. The
following rules are stored in :manpage:`devfs.conf(5)`::

   # Assign /dev/ada1 and /dev/ada2 to the tserver user
   own    ada[12]  tserver:tserver

