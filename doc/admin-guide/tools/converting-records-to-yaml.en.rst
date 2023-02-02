.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. _rec-config-to-yaml:


Converting records.config to records.yaml
*****************************************


Since ATS 10 ``records.config`` is not longer used and instead we have migrated to
:file:`records.yaml`.
This is a guide on how to migrate from your legacy ``records.config`` style file to a
new :file:`records.yaml`.

This document will give details about the change and how to convert files from the
legacy style to the new YAML.


New YAML structure
==================


#. We are introducing a root node ``ts`` for the :file:`records.yaml`, so all existing records
will belong to the new ``ts`` node.
#. From the current ``records.config`` structure we have dropped the prefix ``proxy.config``
and ``proxy.local`` and use the rest to build a YAML structure.

The logic around this is basically to walk down the record name separated by the dots and build
a new YAML map from each name, so for instance, with the following records:


.. code-block:: bash
   :linenos:

   CONFIG proxy.config.exec_thread.autoconfig.scale FLOAT 1.0
   CONFIG proxy.config.exec_thread.limit INT 2
   CONFIG proxy.config.accept_threads INT 1
   CONFIG proxy.config.task_threads INT 2
   CONFIG proxy.config.cache.threads_per_disk INT 8
   CONFIG proxy.config.exec_thread.affinity INT 1
   CONFIG proxy.config.diags.debug.enabled INT 0
   CONFIG proxy.config.diags.debug.tags STRING http|dns


we first drop the ``proxy.config.`` and walk down each field, so we will end up with
the following:

.. code-block:: yaml
   :linenos:

      ts:
      accept_threads: 1
      cache:
         threads_per_disk: 8
      diags:
         debug:
            enabled: 0
            tags: http|dns
      exec_thread:
         affinity: 1
         autoconfig:
            scale: 1.0
         limit: 2
      task_threads: 2

There were a few things that were done on the names and types that are described next.

Records renaming
----------------

There are few changes that I had to perform in order to be able to keep the similar
structure used in the ``records.config`` into a YAML based configuration.

An example of this would be a record key holding a value which also contains children fields:

.. code-block:: bash

   proxy.config.exec_thread.autoconfig INT 1
   proxy.config.exec_thread.autoconfig.scale FLOAT 1.0 << children of autoconfig


As this cannot be done in YAML we need to rename ``proxy.config.exec_thread.autoconfig``
to ``proxy.config.exec_thread.autoconfig.enabled`` so we no longer have a map with
values other than some children's fields. So in YAML we can go from something like this:


.. code-block:: bash

   proxy.config.exec_thread.autoconfig INT 1
   proxy.config.exec_thread.autoconfig.scale FLOAT 1.0

to

.. code-block:: bash

   exec_thread:
      autoconfig:
         enabled: 1 # new field.
         scale: 1.0


Naming convention
-----------------

All this renamed records are subject to the following reasoning:

If a value is meant to be used as a toggle on/off, We have used the name ``enabled``
If a value is meant to be used a an enumeration, We have used the name ``value``

Possible issue with this is that if we scale the field meaning and we need to move
from a toggle to an enumeration then this logic will make no sense.
Should we just use one or the other? Current diags.debug.enabled is an example of
an enable field holding more than 0,1 value.




Converting files
================

We have provided a tool script that will help converting any ``records.config`` style
file into a YAML format file. The script will not only convert record by record but
it will also work around the names that were renamed.

Just run the script on your existing ``records.config`` file and that should be it.

Example 1
---------

Converting a file with a detailed output.

.. code-block:: bash
   :linenos:

   $ python3 convert2yaml.py -f records.config -S records.yaml -y
   [████████████████████████████████████████] 494/494

   ┌■ 8 Renamed records:
   └┬──» #1 : proxy.config.output.logfile -> proxy.config.output.logfile.name
    ├──» #2 : proxy.config.exec_thread.autoconfig -> proxy.config.exec_thread.autoconfig.enabled
    ├──» #3 : proxy.config.hostdb -> proxy.config.hostdb.enabled
    ├──» #4 : proxy.config.tunnel.prewarm -> proxy.config.tunnel.prewarm.enabled
    ├──» #5 : proxy.config.ssl.TLSv1_3 -> proxy.config.ssl.TLSv1_3.enabled
    ├──» #6 : proxy.config.ssl.client.TLSv1_3 -> proxy.config.ssl.client.TLSv1_3.enabled
    ├──» #7 : proxy.config.ssl.origin_session_cache -> proxy.config.ssl.origin_session_cache.enabled
    └──» #8 : proxy.config.ssl.session_cache -> proxy.config.ssl.session_cache.value


There are a few things to note here:

Line 2. A total of ``494`` from ``494`` records were converted.
Line 4. A total of ``8`` records were renamed.

Example 2
---------

Converting a file with no output. If any, errors are displayed.

.. code-block:: bash

   $ convert2yaml.py -f records.config -S records.yaml -y -m

.. note::

   Use -m, --mute to mute the output.


Non core records
================

With non core records the problem arises when a plugin register a new field which
is not registered in the core, the legacy implementation would read the record type
from the config file and then once the record is registered by a plugin it will be
properly registered inside ATS( * mismatched type gets the current value reset to
the default one*)

As said before in ATS when using YAML without explicit saying the type we have no
way to know the type till the registration happens(after reading the config).
We need to overcome this and to me there seems to be two options here:

Expect non core records to set the type (!!int, !!float, etc).

.. code-block:: bash

   ts:
      plugin_x:
         my_field_1: !!int '1'
         my_field_2: !!float '1.2'
         my_field_3: 'my string'


The convert2yaml tool can deal with this:

Example 1
---------

Use a type representation (``-t, --typerepr``) when converting a file.

Non core records expect the type to be specified as the YAML library used Internally
cannot assume the type. So in for cases like this you should set the type on the
non core records, for instance:

.. code-block:: bash

   CONFIG proxy.config.http.my_own_record_1 FLOAT 1.0
   CONFIG proxy.config.http.some_core_record INT 1


.. code-block:: bash

   $ convert2yaml.py -f records.config -S records.yaml -y -t float,int

   $ cat records.yaml
   ts:
      http:
         my_own_record_1: !!float '1.0'
         my_own_record_2: !!int '1'

Now the records parser knows the type of the non core records.

.. important::

   The issue with using type representer when converting a file is that this applies
   to all the records and not only the non core one. This will change in a future
   script update.



Final Notes
===========

Internally ATS still uses the record style names, querying any record can still
be done as usual. Either by using the ATS API or by using :program:`traffic_ctl`.


This implementation will only accept a ``records.yaml`` config file, if not found the
defaults will be used. If a ``record.config`` file is found in the config directory
a warning will be shown, the legacy file should be removed.
This is done to avoid someone having the legacy file thinking it is the current one.

.. code-block:: bash

   traffic_server WARNING: Found a legacy config file. /home/to/ats/config/records.config

