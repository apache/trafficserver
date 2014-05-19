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

.. Referenced source files

.. |RecCore.cc| replace:: ``RecCore.cc``

.. _RecCore.cc: https://github.com/apache/trafficserver/blob/master/lib/records/RecCore.cc

.. |RecordsConfig.cc| replace:: ``RecordsConfig.cc``

.. _RecordsConfig.cc: https://github.com/apache/trafficserver/blob/master/mgmt/RecordsConfig.cc

.. |ts.h.in| replace:: ``ts.h.in``

.. _ts.h.in: https://github.com/apache/trafficserver/blob/master/proxy/api/ts/ts.h.in

.. |InkAPI.cc| replace:: ``InkAPI.cc``

.. _InkAPI.cc: https://github.com/apache/trafficserver/blob/master/proxy/api/InkAPI.cc

.. |InkAPITest.cc| replace:: ``InkAPITest.cc``

.. _InkAPITest.cc: https://github.com/apache/trafficserver/blob/master/proxy/api/InkAPITest.cc

.. Referenced enumeration values

.. |RECU_DYNAMIC| replace:: ``RECU_DYNAMIC``

.. _RECU_DYNAMIC: recu-dynamic_


=====================================
Configuration Variable Implementation
=====================================

Adding a new configuration variable in :file:`records.config` requires a number of steps which are mostly documented
here.

Before adding a new configuration variable, please discuss it on the mailing list. It will commonly be the case that a
better name will be suggested or a more general approach to the problem which solves several different issues.

=====================================
Defining the Variable
=====================================

To begin the new configuration variables must be added to |RecordsConfig.cc|_. This contains a long array of
configuration variable records. The fields for each record are

type:``RecT``
   Type of record. There valid values are

   ``RECT_NULL``
      Undefined record.

   ``RECT_CONFIG``
      General configuration variable.

   ``RECT_PROCESS``
      Process related statistic.

   ``RECT_NODE``
      Local statistic.

   ``RECT_CLUSTER``
      Cluster statistic.

   ``RECT_LOCAL``
      Configuration variable that is explicitly not shared across a cluster.

   ``RECT_PLUGIN``
      Plugin created statistic.

   In general ``RECT_CONFIG`` should be used unless it is required that the value not be shared among members of a
   cluster in which case ``RECT_LOCAL`` should be used.

name:string
   The fully qualified name of the configuration variable. Although there appears to be a hierarchial naming scheme,
   that's just a convention, it is not actually used by the code. Nonetheless new variables should adhere to the
   hierarchial scheme.

value_type:``RecDataT``
   The data type of the value. It should be one of ``RECD_INT``, ``RECD_STRING``, ``RECD_FLOAT`` as appropriate.

default:string
   The default value for the variable. This is always a string regardless of the *value_type*.

update:``RecUpdateT``
   Information about how the variable is updated. The valid values are

   ``RECU_NULL``
      Behavior is unknown or unspecified.

.. _recu-dynamic:

   ``RECU_DYNAMIC``
      This can be updated via command line tools.

   ``RECD_RESTART_TS``
      The :program:`traffic_server` process must be restarted for a new value to take effect.

   ``RECD_RESTART_TM``
      The :program:`traffic_manager`` process must be restarted for a new value to take effect.

   ``RECD_RESTART_TC``
      The :program:`traffic_cop`` process must be restarted for a new value to take effect.

required:``RecordRequiredType``
   Effectively a boolean that specifies if the record is required to be present, with ``RR_NULL`` meaning not required
   and ``RR_REQUIRED`` indicating that it is required. Given that using ``RR_REQUIRED`` would be a major
   incompatibility, ``RR_NULL`` is generally the better choice.

check:``RecCheckT``
   Additional type checking. It is unclear if this is actually implemented. The valid values are

   ``RECC_NULL``
      No additional checking.

   ``RECC_STR``
      Verify the value is a string.

   ``RECC_INT``
      Verify the value is an integer.

   ``RECC_IP``
      Verify the value is an IP address. Unknown if this checks for IPv6.

pattern:regular expression
   Even more validity checking. This provides a regular expressions (PCRE format) for validating the value. This can be
   ``NULL`` if there is no regular expression to use.

access:``RecAccessT``
   Access control. The valid values are

   ``RECA_NULL``
      The value is read / write.

   ``RECA_READ_ONLY``
      The value is read only.

   ``RECA_NO_ACCESS``
      No access to the value - only privileged levels parts of the ATS can access the value.

=====================================
Variable Infrastructure
=====================================

The primary effort in defining a configuration variable is handling updates, generally via :program:`traffic_line`. This
is handled in a generic way, as described in the next section, or in a :ref:`more specialized way
<http-config-var-impl>` (built on top of the generic mechanism) for HTTP related configuration variables. This is only
needed if the variable is marked as dynamically updateable (|RECU_DYNAMIC|_) although HTTP configuration variables
should be dynamic if possible.

------------------------------
Handling Updates
------------------------------

The simplest mechanism for handling updates is the ``REC_EstablishStaticConfigXXX`` family of functions. This mechanism
will cause the value in the indicated instance to be updated in place when an update to :file:`records.config` occurs.
This is done asynchronously using atomic operations. Use of these variables must keep that in mind.

If a variable requires additional handling when updated a callback can be registered which is called when the variable
is updated. This is what the ``REC_EstablishStaticConfigXXX`` calls do internally with a callback that simply reads the
new value and writes it to storage indicated by the call parameters. The functions used are the ``link_XXX`` static
functions in |RecCore.cc|_.

To register a configuration variable callback, call ``RecRegisterConfigUpdateCb`` with the arguments

``char const*`` *name*
   The variable name.

*callback*
   A function with the signature ``<int (char const* name, RecDataT type, RecData data, void* cookie)>``. The *name*
   value passed is the same as the *name* passed to the registration function as is the *cookie* argument. The *type* and
   *data* are the new value for the variable. The return value is currently ignored. For future compatibility return
   ``REC_ERR_OKAY``.

``void*`` *cookie*
   A value passed to the *callback*.

The *callback* is called under lock so it should be quick and not block. If that is necessary a continuation should be
scheduled to handle the required action.

.. note::
   The callback occurs asynchronously. For HTTP variables as described in the next section, this is handled by the more
   specialized HTTP update mechanisms. Otherwise it is the implementor's responsibility to avoid race conditions.

.. _http-config-var-impl:

------------------------
HTTP Configuation Values
------------------------

Variables used for HTTP processing should be declared as members of the ``HTTPConfigParams`` structure (but :ref:`see
<overridable-config-vars>`) and use the specialized HTTP update mechanisms which handle synchronization and
initialization issues.

The configuration logic maintains two copies of the ``HTTPConfigParams`` structure - the master copy and the current
copy. The master copy is kept in the ``m_master`` member of the ``HttpConfig`` singleton. The current copy is kept in
the ConfigProcessor. The goal is to provide a (somewhat) atomic update for configuration variables which are loaded
individually in to the master copy as updates are received and then bulk copied to a new instance which is then swapped
in as the current copy. The HTTP state machine interacts with this mechanism to avoid race conditions.

For each variable a mapping between the variable name and the appropriate member in the master copy should be
established between in the ``HTTPConfig::startup`` method. The ``HttpEstablishStaticConfigXXX`` functions should be used
unless there is an strong, explicit reason to not do so.

The ``HTTPConfig::reconfigure`` method handles the current copy of the HTTP configuration variables. Logic should be
added here to copy the value from the master copy to the current copy. Generally this will be a simple assignment. If
there are dependencies between variables those should be enforced / checked in this method.

.. _overridable-config-vars:

-----------------------
Overridable Variables
-----------------------

HTTP related variables that are changeable per transaction are stored in the ``OverridableHttpConfigParams`` structure,
an instance of which is the ``oride`` member of ``HTTPConfigParams`` and therefore the points in the previous section
still apply. The only difference for that is the further ``.oride`` in the structure references.

In addition the variable is required to be accessible from the transaction API. In addition to any custom API functions
used to access the value, the following items are required for generic access

#. Add a value to the ``TSOverridableConfigKey`` enumeration in |ts.h.in|_.

#. Augment the ``TSHttpTxnConfigFind`` function to return this enumeration value when given the name of the configuration
   variable. Be sure to count the charaters very carefully.

#. Augment the ``_conf_to_memberp`` function in |InkAPI.cc|_ to return a pointer to the appropriate member of
   ``OverridableHttpConfigParams`` and set the type if not a byte value.

#. Update the testing logic in |InkAPITest.cc|_ by adding the string name of the configuration variable to the
   ``SDK_Overridable_Configs`` array.
