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

Glossary
~~~~~~~~

.. include:: common.defs

.. glossary::
    :sorted:

    cache volume
        Persistent storage for the cache, defined and manipulable by the user. Cache volumes are defined in :file:`volume.config`. A cache volume is spread across :term:`storage unit`\ s to increase performance through parallel I/O. Storage units can be split across cache volumes. Each such part of a storage unit in a cache volume is a :term:`volume`.

        Implemented by the class :cpp:class:`CacheVol`.

    volume
        A homogenous persistent store for the cache. A volume always resides entirely on a single physical device and is treated as an undifferentiated span of bytes.

        Implemented by the class :cpp:class:`Vol`.

        See also :term:`storage unit`, :term:`cache volume`

    storage unit
        The physical storage described by a single line in :file:`storage.config`.
