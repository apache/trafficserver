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

.. include:: ../../common.defs
.. default-domain:: cpp

.. _cache_tool:

Cache Tool
**********

Internals
=========

.. type:: Megabytes

   A :class:`Scalar` type with a scale of 2~20~.

.. type:: CacheStripeBlocks

   A :class:`Scalar` type with a scale of 2~27~. This represents the units in which storage is
   allocated to stripes.

.. class:: VolumeConfig

   A container for parsed cache volume configuration.

   .. class:: Data

      Configuration information for a single cache volume.

      .. member:: int _idx

         The volume index, 0 if not specified in the configuration.

      .. member:: int _percent

         Volume size if specified as a percentage, otherwise 0 if not specified.

      .. member:: Megabytes _size

         Volume size if specified as an explicit size, otherwise 0 if not specified.

      .. member:: CacheStripeBlocks _alloc

         Already allocated size, used during allocation computations.

.. class:: VolumeAllocator

   This class provides handling for allocating storage space to stripes based on volume configuration.

   .. class:: V

      Cache volume data for a single cache volume.

      .. member:: VolumeConfig::Data const & _config

         A reference to the (parsed) volume configuration for a single volume to be used for allocating
         storage to that cache volume.

   .. member:: std::vector<V> _av

      Cache volume data for all the volumes in the configuration.

   .. member:: VolumeConfig _vols

      The parsed cache volume configuration.
