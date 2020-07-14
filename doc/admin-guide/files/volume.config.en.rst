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
      /dev/disk2 volume=3 # <- exclusinve span

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
single-lock pressure for a machine with few drives.::

    volume=1 scheme=http size=20%
    volume=2 scheme=http size=20%
    volume=3 scheme=http size=20%
    volume=4 scheme=http size=20%
    volume=5 scheme=http size=20%
