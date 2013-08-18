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

where ``pathname`` is the name of a partition, directory or file, ``size``
is the size of the named partition, directory or file (in bytes), and
``volume`` is the volume number that is used in :file:`volume.config`
and :file:`hosting.config`. You must specify a size for directories or
files; size is optional for raw partitions. ``volume`` is optional.

You can use any partition of any size. For best performance:

-  Use raw disk partitions.
-  For each disk, make all partitions the same size.
-  For each node, use the same number of partitions on all disks.
-  Group similar kinds of storage into different volumes. For example
   split out SSD's or RAM drives into their own volume.

Specify pathnames according to your operating system requirements. See
the following examples. In the :file:`storage.config` file, a formatted or
raw disk must be at least 128 MB.

When using raw disk or partitions, you should make sure the admin user,
which is the traffic_server running at, have the read&write privileges.
The admin user_id is set in
```proxy.config.admin.user_id`` <records.config#proxy.config.admin.user_id>`_.
One good practice is if the disk set with g+rw, put the admin user into
the group which have the privileges.

Examples
========

The following basic example shows 64 MB of cache storage in the
``/big_dir`` directory::

    /big_dir 67108864

You can use the ``.`` symbol for the current directory. Here is an
example for 64 MB of cache storage in the current directory::

    . 67108864

Solaris Example
---------------

The following example is for the Solaris operating system::

    /dev/rdsk/c0t0d0s5
    /dev/rdsk/c0t0d1s5


.. note:: Size is optional. If not specified, the entire partition is used.

Linux Example
-------------

The following example will use an entire raw disk in the Linux operating
system:::

    /dev/sde volume=1
    /dev/sdf volume=2

In order to make sure :program:`traffic_server` will have access to this disk
you can use ``udev`` to persistently set the right permissions. The
following rules are targeted for an Ubuntu system, and stored in
``/etc/udev/rules.d/51-cache-disk.rules``::

    # Assign /dev/sde and /dev/sdf to the www group
    # make the assignment final, no later changes allowed to the group!
    SUBSYSTEM=="block", KERNEL=="sd[ef]", GROUP:="www"

FreeBSD Example ## {#LinuxExample}
----------------------------------

Starting with 5.1 FreeBSD dropped support for explicit raw devices. All
devices on FreeBSD can be accessed raw now.

The following example will use an entire raw disk in the FreeBSD
operating system::

    /dev/ada1
    /dev/ada2

In order to make sure :program:`traffic_server` will have access to this disk
you can use ``devfs`` to persistently set the right permissions. The
following rules are stored in ``/etc/devfs.conf``::

    # Assign /dev/ada1 and /dev/ada2 to the tserver user
    own    ada[12]  tserver:tserver

