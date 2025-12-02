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

=============
volume.config
=============

.. configfile:: volume.config

The :file:`volume.config` file enables you to manage your cache space more
efficiently and restrict disk usage by creating cache volumes of
different sizes. By distributing the cache across multiple volumes,
you can help decrease single-lock pressure when there are not many hard drives
present. You can further configure these volumes to store data from certain
origin servers and/or domains in the :file:`hosting.config` file.

Format
======

For each volume you want to create, enter a line with the following
format: ::

    volume=volume_number  scheme=protocol_type  size=volume_size

where ``volume_number`` is a number between 1 and 255 (the maximum
number of volumes is 255) and ``protocol_type`` is ``http``. Traffic
Server supports ``http`` for HTTP volume types; ``volume_size`` is the
amount of cache space allocated to the volume. This value can be either
a percentage of the total cache space or an absolute value. The absolute
value must be a multiple of 128 MB, where 128 MB is the smallest value.
If you specify a percentage, then the size is rounded down to the
closest multiple of 128 MB.

Each volume is striped across several disks to achieve parallel I/O. For
example: if there are four disks, then a 1-GB volume will have 256 MB on
each disk (assuming each disk has enough free space available). If you
do not allocate all the disk space in the cache, then the extra disk
space is not used. You can use the extra space later to create new
volumes without deleting and clearing the existing volumes.

.. important::

   Changing this file to add, remove or modify volumes effectively invalidates
   the cache.


Optional ramcache setting
-------------------------

You can also add an option ``ramcache=true/false`` to the volume configuration
line.  True is the default setting and so not needed unless you want to explicitly
set it.  Setting ``ramcache=false`` will disable the ramcache that normally
sits in front of a volume.  This may be desirable if you are using something like
ramdisks, to avoid wasting RAM and cpu time on double caching objects.


Optional directory entry sizing
-------------------------------

You can also add an option ``avg_obj_size=<size>`` to the volume configuration
line. This overrides the global :ts:cv:`proxy.config.cache.min_average_object_size`
configuration for this volume. The size supports multipliers (K, M, G, T) for
convenience (e.g., ``avg_obj_size=64K`` or ``avg_obj_size=1M``). This is useful
if you have a volume that is dedicated for say very small objects, and you need
a lot of directory entries to store them.

Optional fragment size setting
------------------------------

You can also add an option ``fragment_size=<size>`` to the volume configuration
line. This overrides the global :ts:cv:`proxy.config.cache.target_fragment_size`
configuration for this volume. The size supports multipliers (K, M, G, T) for
convenience (e.g., ``fragment_size=512K`` or ``fragment_size=2M``). This allows
for a smaller, or larger, fragment size for a particular volume. This may be
useful together with ``avg_obj_size`` as well, since a larger fragment size could
reduce the number of directory entries needed for a large object.

Note that this setting has a maximum value of 4MB.

Optional RAM cache size allocation
-----------------------------------

You can add an option ``ram_cache_size=<size>`` to the volume configuration line
to allocate a dedicated RAM cache pool for this volume. The size supports
multipliers (K, M, G, T) for convenience (e.g., ``ram_cache_size=512M`` or
``ram_cache_size=2G``). Setting ``ram_cache_size=0`` disables the RAM cache
for this volume, which is equivalent to ``ramcache=false``.

When ``ram_cache_size`` is specified for a volume, that amount is **automatically
subtracted** from the global :ts:cv:`proxy.config.cache.ram_cache.size` setting,
and the remainder is shared among volumes without private allocations. This ensures
total RAM cache usage never exceeds the configured global limit.

For example, if the global RAM cache size is 4GB and you allocate 1GB to volume 1
and 512MB to volume 2, the remaining 2.5GB will be distributed among other volumes
using the normal proportional allocation based on disk space.

**Important notes:**

* If the sum of all ``ram_cache_size`` allocations exceeds the global RAM cache size,
  a warning is logged and the private allocations are disabled, falling back to
  the standard shared allocation.
* This setting only takes effect when :ts:cv:`proxy.config.cache.ram_cache.size`
  is set to a positive value (not ``-1`` for automatic sizing).

Optional RAM cache cutoff override
-----------------------------------

You can add an option ``ram_cache_cutoff=<size>`` to the volume configuration line
to override the global :ts:cv:`proxy.config.cache.ram_cache_cutoff` setting for
this specific volume. The size supports multipliers (K, M, G, T) for convenience
(e.g., ``ram_cache_cutoff=64K`` or ``ram_cache_cutoff=1M``).

This cutoff determines the maximum object size that will be stored in the RAM cache.
Objects larger than this size will only be stored on disk. Setting different cutoffs
per volume allows you to:

* Use larger cutoffs for volumes serving frequently accessed large objects
* Use smaller cutoffs for volumes with many small objects to maximize RAM cache hits
* Disable RAM caching entirely for certain objects by setting a very low cutoff

Exclusive spans and volume sizes
================================

In the following sample configuration 2 spans `/dev/disk1` and `/dev/disk2` are defined
in :file:`storage.config`, where span `/dev/disk2` is assigned to `volume 3` exclusively
(`volume 3` is forced to an "exclusive" span `/dev/disk2`).
In :file:`volume.config` there are 3 volumes defined, where `volume 1` and `volume 2`
occupy span `/dev/disk1` taking each 50% of its space and `volume 3` takes 100% of span
`/dev/disk2` exclusively.

storage.config::

      /dev/disk1
      /dev/disk2 volume=3 # <- exclusive span

volume.config::

      volume=1 scheme=http size=50%
      volume=2 scheme=http size=50%
      volume=3 scheme=http size=512 # <- volume forced to a specific exclusive span

It is important to note that when percentages are used to specify volume sizes
and "exclusive" spans are assigned (forced) to a particular volume (in this case `volume 3`),
the "exclusive" spans (in this case `/dev/disk2`) are excluded from the total cache
space when the "non-forced" volumes sizes are calculated (in this case `volume 1` and `volume 2`).


Examples
========

The following example partitions the cache across 5 volumes to decreasing
single-lock pressure for a machine with few drives. The last volume being
an example of one that might be composed of purely ramdisks so that the
ramcache has been disabled.::

    volume=1 scheme=http size=20%
    volume=2 scheme=http size=20%
    volume=3 scheme=http size=20%
    volume=4 scheme=http size=20% avg_obj_size=4K
    volume=5 scheme=http size=20% ramcache=false fragment_size=512K

The following example shows advanced RAM cache configuration with dedicated
allocations and custom cutoffs::

    # Volume 1: General content with 2GB dedicated RAM cache
    volume=1 scheme=http size=40% ram_cache_size=2G

    # Volume 2: Small API responses with custom cutoff and 512MB RAM cache
    volume=2 scheme=http size=20% ram_cache_size=512M ram_cache_cutoff=64K

    # Volume 3: Large media with higher cutoff for thumbnails
    volume=3 scheme=http size=40% ram_cache_cutoff=1M

In this example, assuming a global ``proxy.config.cache.ram_cache.size`` of 4GB:

* Volume 1 gets a dedicated 2GB RAM cache allocation
* Volume 2 gets a dedicated 512MB RAM cache allocation and only caches objects up to 64KB
* Volume 3 shares from the remaining 1.5GB pool (4GB - 2GB - 512MB) and caches objects up to 1MB
* The automatic subtraction ensures total RAM usage stays within the 4GB limit
