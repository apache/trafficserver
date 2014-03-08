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
hosting.config
==============

.. configfile:: hosting.config

The :file:`hosting.config` file (by default, located in 
``/opt/trafficserver/etc/trafficserver/``) you to assign cache partitions to
specific origin servers and/or domains so that you can manage cache
space efficiently and restrict disk usage. For step-by-step instructions
on partitioning the cache according to origin servers and/or domains,
refer to `Partitioning the Cache <../configuring-cache#PartitoningCache>`_.

Before you can assign cache partitions to specific
origin servers and/or domains, you must first partition your cache
according to size and protocol in the :file:`volume.config`
file.

After you modify hosting.config, navigate to the Traffic Server bin
directory and run :option:`traffic_line -x` to apply your changes.

When you apply the changes to a node in a cluster, Traffic Server
automatically applies the changes to all other nodes in the cluster.

.. important::

    The :file:`volume.config` configuration must be the same on all nodes in a cluster.

Format
======

Each line in the :file:`hosting.config` file must have one of the following
formats::

    hostname=HOST partition=NUMBERS
    domain=DOMAIN partition=NUMBERS

where ``HOST`` is the fully-qualified hostname of the origin server
whose content you want to store on a particular partition (for example,
``www.myhost.com``); ``DOMAIN`` is the domain whose content you
want to store on a particular partition(for example, ``mydomain.com``);
and ``NUMBERS`` is a comma-separated list of the partitions on
which you want to store the content that belongs to the origin server or
domain listed. The partition numbers must be valid numbers listed in the
file:`volume.config`.

**Note:** To allocate more than one partition to an origin server or
domain, you must enter the partitions in a comma-separated list on one
line, as shown in the example below. The
:file:`hosting.config`  file cannot contain multiple entries
for the same origin server or domain.

Generic Partition
=================

When configuring the :file:`hosting.config` file, you must assign a generic
partition to use for content that does not belong to any of the origin
servers or domains listed. If all partitions for a particular origin
server become corrupt, Traffic Server will also use the generic
partition to store content for that origin server.

The generic partition must have the following format::

    hostname=* partition=NUMBERS

where ``NUMBERS`` is a comma-separated list of generic
partitions.

Examples
========

The following example configures Traffic Server to store content from
the domain ``mydomain.com`` in partition 1 and content from
``www.myhost.com`` in partition 2. Traffic Server stores content from
all other origin servers in partitions 3 and 4.

::

    domain=mydomain.com partition=1
    hostname=www.myhost.com partition=2
    hostname=* partition=3,4

