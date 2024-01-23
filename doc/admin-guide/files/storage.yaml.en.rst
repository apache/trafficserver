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
storage.yaml
==============

.. configfile:: storage.yaml

The :file:`storage.yaml` file (by default, located in
``/usr/local/etc/trafficserver/``) lists all the files, directories, and/or
hard disk partitions that make up the Traffic Server cache. After you
modify the :file:`storage.yaml` file the new settings will not be effective until Traffic Server is restarted.

Format
======

The format of the :file:`storage.yaml` file is a series of lines of the form

.. code-block:: yaml

   cache:           # file level key
     spans:         #
       - id:        # identifier / name of the span
         path:      # path to storage
         size:      # size in bytes, required for file system storage, optional for raw device
         hash_seed: # optional, used to isolate lookup from path changes
     volumes:       # optional
       - id:        # identifier [1-255]
         size:      # optional, size in percentage
         scheme:    # optional, default to "http"
         ram-cache: # optional, default to "true"
         spans:     # optional
           - use:   # Span identifier
             size:  # size allocated to this volume

:code:`spans` lists the raw storage used for the cache. :code:`volumes` organizes the storage into locations for
storing cached objects. This is very similar to operating system partitions and file systems.

For :code:`spans` the keys are

+---------------+-------------+-------------------------------------------------------------+
| Key           | Type        | Meaning                                                     |
+===============+=============+=============================================================+
| id            | string      | Name of the span.                                           |
+---------------+-------------+-------------------------------------------------------------+
| path          | string      | File system of the storage. This must be a block device or  |
|               |             | directory.                                                  |
+---------------+-------------+-------------------------------------------------------------+
| size          | bytes       | Size in bytes. This is optional for devices but required    |
|               |             | for directories.                                            |
+---------------+-------------+-------------------------------------------------------------+
| hash_seed     | string      | Hashing for object location uses a seed to randomize the    |
|               |             | hash. By default this is the path for the span.             |
+---------------+-------------+-------------------------------------------------------------+

For :code:`volumes` the keys are

+---------------+-------------+-------------------------------------------------------------+
| Key           | Type        | Meaning                                                     |
+===============+=============+=============================================================+
| id            | integer     | Id of the volume. Range is [1-255]. This id can be referred |
|               |             | from  :file:`hosting.config`                                |
+---------------+-------------+-------------------------------------------------------------+
| size          | bytes       | Target size of the entire volume. This can be an absolute   |
|               | _or_        | number of bytes or a percentage.                            |
|               | percentage  |
+---------------+-------------+-------------------------------------------------------------+
| scheme        | enumeration | Protocol scheme, defaults to "http". Preserved for future   |
|               | string      | use.                                                        |
+---------------+-------------+-------------------------------------------------------------+
| ram-cache     | boolean     | Control of ram caching for this volume. Default is ``true`` |
+---------------+-------------+-------------------------------------------------------------+
| spans         | list        | Spans that provide storage for this volume. Defaults to     |
|               |             | all spans.                                                  |
+---------------+-------------+-------------------------------------------------------------+

For :code:`volumes:spans` the keys are

+---------------+-------------+-------------------------------------------------------------+
| Key           | Type        | Meaning                                                     |
+===============+=============+=============================================================+
| use           | string      | Name of the span to use.                                   |
+---------------+-------------+-------------------------------------------------------------+
| size          | bytes       | Amount of the span to use. The total across all uses of     |
|               | _or_        | this specific span must be less than 100% and less than the |
|               | percentage  | total size of the span.                                     |
+---------------+-------------+-------------------------------------------------------------+

.. important::

   Any change to this files can (and almost always will) invalidate the existing cache in its entirety.

You can use any partition of any size. For best performance:

-  Use raw disk partitions.
-  For each disk, make all partitions the same size.
-  Group similar kinds of storage into different volumes. For example
   split out SSD's or RAM drives into their own volume.

Specify pathnames according to your operating system requirements. See
the following examples. In the :file:`storage.yaml` file, a formatted or
raw disk must be at least 128 MB.

When using raw disk or partitions, you should make sure the :ts:cv:`Traffic
Server user <proxy.config.admin.user_id>` used by the Traffic Server process
has read and write privileges on the raw disk device or partition. One good
practice is to make sure the device file is set with 'g+rw' and the Traffic
Server user is in the group which owns the device file.  However, some
operating systems have stronger requirements - see the following examples for
more information.

As with standard ``records.yaml`` integers, human readable prefixes are also
supported. They include

   - ``K`` Kilobytes (1024 bytes)
   - ``M`` Megabytes (1024^2 or 1,048,576 bytes)
   - ``G`` Gigabytes (1024^3 or 1,073,741,824 bytes)
   - ``T`` Terabytes (1024^4 or 1,099,511,627,776 bytes)

.. _assignment-table:

Storage Allocation
------------------

Allocation of span storage to volumes is done in stages. Storage is always allocated in multiples of 128 megabytes,
rounded down.

*  Explicitly sized span storage (:code:`cache:volumes:spans:size`) is allocated to volumes. It is an error if the total allocated is larger than the span size.
   *  Absolute sizes are allocated first.
   *  Percentages are allocated from remaining space.
   *  Remaining storage from spans that are used without an explicit size is divided evenly among the volumes that use the span.
*  Span storage is allocated to volumes by the :code:`cache:volumes::size` values.
   *  Absolute sizes are allocated first.
   *  Percentages are applied to remaining space.
   *  Remaining storage is divided evenly among volumes without an explicit size.

Assignment Table
----------------

Each storage element defined in :file:`storage.yaml` is divided in to :term:`stripes <cache stripe>`. The
assignment table maps from an object URL to a specific stripe. The table is initialized based on a
pseudo-random process which is seeded by hashing a string for each stripe. This string is composed
of a base string, an offset (the start of the stripe on the storage element), and the length of the
stripe. By default the path for the storage is used as the base string. This ensures that each
stripe has a unique string for the assignment hash. This does make the assignment table very
sensitive to the path for the storage elements and changing even one can have a cascading effect
which will effectively clear most of the cache. This can be problem when drives fail and a system
reboot causes the path names to change.

The :arg:`id` option can be used to create a fixed string that an administrator can use to keep the
assignment table consistent by maintaining the mapping from physical device to base string even in the presence of hardware changes and failures.

Backwards Compatibility
-----------------------

In previous versions of |TS| it was possible to have "exclusive" spans which were used by only one volume. This is
now down by specifying the span in the volume and using a size of "100%". E.g. old configuration like ::

   /dev/disk2 volume=3 # storage.config
   volume=3 scheme=http size=512 # volume.config

The corresponding configuration would be

.. code-block:: yaml

   cache:
     spans:
       - id: disk.2
         path: /dev/disk2
     volumes:
       - id: 1
         spans:
           - use: disk.2
             size: 100%

Because volume sizes that are percentages are computed on span storage not already explicitly allocated, this will
leave none of "disk.2" for such allocation and therefore "disk.2" will be used only by volume "1". Note this
configuration is more flexible. If it was useful to have two linear volumes, each using exclusively half of the
span, this would be

.. code-block:: yaml

   cache:
     spans:
       - id: disk.2
         path: /dev/disk2
     volumes:
       - id: 1
         spans:
           - use: disk.2
             size: 50%
       - id: 2
         spans:
           - use: disk.2
             size: 50%

.. important::

   If a span is explicitly used by any volume its storage will be allocated to only volumes that explicitly use that span.

Examples
========

The following basic example shows 128 MB of cache storage in the "/big_dir" directory

.. code-block: yaml

   cache:
     spans:
       - id: store
         path: /big_dir
         size: 134217728

By default a volume uses all spans, therefore a volume uses all of span "store" because there are no other
volumes. It would be equivalent is using the spans explicitly, e.g.

.. code-block: yaml

   cache:
     spans:
       - id: store
         path: /big_dir
         size: 134217728
     volumes:
       - id: 1
         size: 100%
         spans:
           - id: store

You can use the ``.`` symbol for the current directory. Here is an example for 128 MB of cache storage in the current directory

.. code-block: yaml

   cache:
     spans:
       - id: store
         path: "."
         size: 134217728

.. note::
    When using on-filesystem cache disk storage, you can only have one such
    directory specified. This will be addressed in a future version.

Linux Example
-------------
.. note::

   Rather than refer to disk devices like ``/dev/sda``, ``/dev/sdb``, etc.,
   modern Linux supports `alternative symlinked names for disk devices
   <https://wiki.archlinux.org/index.php/persistent_block_device_naming#by-id_and_by-path>`_ in the ``/dev/disk``
   directory structure. As noted for the :ref:`assignment-table` the path used for the disk can effect
   the cache if it changes. This can be ameliorated in some cases by using one of the alternate paths
   in via ``/dev/disk``. Note that if the ``by-id`` or ``by-path`` style is used, replacing a failed drive will cause
   that path to change because the new drive will have a different physical ID or path.

   If this is not sufficient then the :arg:`hash_seed` key should be used to create a more permanent
   assignment table. An example would be

   .. code-block:: yaml

   cache:
     spans:
       - id: "span.0"
         path: "/dev/sde"
         hash-seed: "cache.disk.0"
       - id: "span.1"
         path: "/dev/sdg"
         hash-seed: "cache.disk.1"

The following example will use an entire raw disk in the Linux operating
system

.. code-block: yaml

   cache:
     spans:
       - id: a
         path: "/dev/disk/by-id/disk-A-id"
       - id: b
         path: "/dev/disk/by-id/disk-B-id"
     volumes:
       - id: 1
         spans:
           - use: a
             size: 100%
       - id: 2
         spans:
           - use: b
             size: 100%

In order to make sure :program:`traffic_server` will have access to this disk
you can use :manpage:`udev(7)` to persistently set the right permissions. The
following rules are targeted for an Ubuntu system, and stored in
``/etc/udev/rules.d/51-cache-disk.rules``::

   # Assign DiskA and DiskB to the tserver group
   # make the assignment final, no later changes allowed to the group!
   SUBSYSTEM=="block", KERNEL=="sd[ef]", GROUP:="tserver"

In order to apply these settings, trigger a reload with :manpage:`udevadm(8)`:::

   udevadm trigger --subsystem-match=block


FreeBSD Example
---------------

Starting with 5.1 FreeBSD dropped support for explicit raw devices. All
devices on FreeBSD can be accessed raw now.

The following example will use an entire raw disk in the FreeBSD
operating system

.. code-block: yaml

   cache:
     spans:
       - id: ada.1
         path: "/dev/ada1"
       - id: ada.2
         path: "/dev/ada2"
     volumes:
       - id: 1
         size: 100%

In order to make sure :program:`traffic_server` will have access to this disk
you can use :manpage:`devfs(8)` to persistently set the right permissions. The
following rules are stored in :manpage:`devfs.conf(5)`::

   # Assign /dev/ada1 and /dev/ada2 to the tserver user
   own    ada[12]  tserver:tserver

Advanced
--------

Because relative paths in :file:`storage.yaml` are relative to the base prefix, when using customized runroot
it may be necessary to adjust such paths in :file:`storage.yaml` or adjust ``runroot.yaml`` itself.
Despite the name, the cachedir value is not used for this file.

Examples
========

The following example partitions the cache across 5 volumes to decreasing single-lock pressure for a
machine with few drives. The last volume being an example of one that might be composed of purely
ramdisks so that the ram cache has been disabled.

.. code-block:: yaml

   cache:
     spans:
       - id: disk
         path: "/dev/sdb"
     volumes:
       - id: 1
         size: 20%
       - id: 2
         size: 20%
       - id: 3
         size: 20%
       - id: 4
         size: 20%
       - id: 5
         size: 20%
         ram-cache: false

This can be simplified by depending on the default allocation which splits unallocated span storage across volumes.

.. code-block:: yaml

   cache:
     spans:
       - id: disk
         path: "/dev/sdb"
     volumes:
       - id: 1
       - id: 2
       - id: 3
       - id: 4
       - id: 5
         ram-cache: false

For a host with a physical disk and two ram disks, where the ram disks should be split between two volumes, with a third
volume that uses the physical disk.

This depends on defaults. The spans "ram.1" and "ram.2" are split evenly between volume "1" and volume "2" because no
sizes are specified. Span "disk" is not used for volume "1" nor volume "2" because it is not listed in the ``spans``.
Volume "3" therefore gets all of span "disk".

.. code-block:: yaml

   cache:
     spans:
       - id: disk
         path: "/dev/sdb"
       - id: ram.1
         path: "/dev/ram.1"
       - id: ram.2
         path: "/dev/ram.2"
     volumes:
       - id: 1
           - spans:
               - use: ram.1
               - use: ram.2
       - id: 2
           - spans:
               - use: ram.1
               - use: ram.2
       - id: 3

If one of the ram disk based volumes should be larger, this could be done as follows by making volume "1" roughly twice
as large as volume "2".

.. code-block:: yaml

   cache:
      spans:
      - id: disk
        path: "/dev/sdb"
      - id: ram.1
        path: "/dev/ram.1"
      - id: ram.2
        path: "/dev/ram.2"
      volumes:
      - id: 1
          - spans:
              - use: ram.1
                size: 66%
              - use: ram.2
                size: 66%
      - id: 2
          - spans:
              - use: ram.1
              - use: ram.2
      - id: 3

Instead, suppose the physical spans ("disk.1" and "disk.2") should be split across volumes. This can be done by adding volumes
with only defaults, as the phisycal spans will be divided evenly among four volumes (3 - 6), each volume allocated 25% of
"disk.1" and 25% of "disk.2".

OTOH, the ram spans ("ram.1" and "ram.2") will be divided evenly among volume 1 and 2.


.. code-block:: yaml

   cache:
      spans:
        - id: disk.1
          path: "/dev/sdb"
        - id: disk.2
          path: "/dev/sde"
        - id: ram.1
          path: "/dev/ram.1"
        - id: ram.2
          path: "/dev/ram.2"
      volumes:
        - id: 1
            - spans:
                - use: ram.1
                - use: ram.2
        - id: 2
            - spans:
                - use: ram.1
                - use: ram.2
        - id: 3
        - id: 4
        - id: 5
        - id: 6
