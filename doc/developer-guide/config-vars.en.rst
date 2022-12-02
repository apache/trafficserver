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

.. include:: ../common.defs

.. Referenced source files

.. |RecCore.cc| replace:: ``RecCore.cc``

.. _RecCore.cc: https://github.com/apache/trafficserver/blob/master/src/records/RecCore.cc

.. |RecordsConfig.cc| replace:: ``RecordsConfig.cc``

.. _RecordsConfig.cc: https://github.com/apache/trafficserver/blob/master/mgmt/RecordsConfig.cc

.. |apidefs.h.in| replace:: ``apidefs.h.in``

.. _apidefs.h.in: https://github.com/apache/trafficserver/blob/master/include/ts/apidefs.h.in

.. |InkAPI.cc| replace:: ``InkAPI.cc``

.. _InkAPI.cc: https://github.com/apache/trafficserver/blob/master/src/traffic_server/InkAPI.cc

.. |InkAPITest.cc| replace:: ``InkAPITest.cc``

.. _InkAPITest.cc: https://github.com/apache/trafficserver/blob/master/src/traffic_server/InkAPITest.cc

.. |overridable_txn_vars.cc| replace:: ``overridable_txn_vars.cc``

.. _overridable_txn_vars.cc: https://github.com/apache/trafficserver/blob/master/src/shared/overridable_txn_vars.cc

.. |ts_lua_http_config.c| replace:: ``ts_lua_http_config.c``

.. _ts_lua_http_config.c: https://github.com/apache/trafficserver/blob/master/plugins/experimental/ts_lua/ts_lua_http_config.c

.. |TSHttpOverridableConfig.en.rst| replace:: ``TSHttpOverridableConfig.en.rst``

.. _TSHttpOverridableConfig.en.rst: https://github.com/apache/trafficserver/blob/master/doc/reference/api/TSHttpOverridableConfig.en.rst

.. Referenced enumeration values

.. |RECU_DYNAMIC| replace:: ``RECU_DYNAMIC``

.. _RECU_DYNAMIC: recu-dynamic_


Configuration Variable Implementation
*************************************

Adding a new configuration variable in :file:`records.yaml` requires a number
of steps which are mostly documented here.

Before adding a new configuration variable, please discuss it on the mailing
list. It will commonly be the case that a better name, or a more general
approach to the problem which solves several different issues, may be suggested.

Defining the Variable
=====================

To begin, the new configuration variables must be added to |RecordsConfig.cc|_.
This contains a long array of configuration variable records. The fields for
each record are:

type:``RecT``
   Type of record. The valid values are:

   ``RECT_NULL``
      Undefined record.

   ``RECT_CONFIG``
      General configuration variable.

   ``RECT_PROCESS``
      Process related statistic.

   ``RECT_NODE``
      Local statistic.

   ``RECT_PLUGIN``
      Plugin created statistic.

   In general, ``RECT_CONFIG`` should be used.

name:``char const*``
   The fully qualified name of the configuration variable. Although there
   appears to be a hierarchical naming scheme, that's just a convention, and it
   is not actually used by the code. Nonetheless, new variables should adhere
   to the hierarchical scheme.

value_type:``RecDataT``
   The data type of the value. It should be one of ``RECD_INT``,
   ``RECD_STRING``, ``RECD_FLOAT`` as appropriate.

default:``char const*``
   The default value for the variable. This is always a string regardless of
   the *value_type*.

update:``RecUpdateT``
   Information about how the variable is updated. The valid values are:

   ``RECU_NULL``
      Behavior is unknown or unspecified.

.. _recu-dynamic:

   ``RECU_DYNAMIC``
      This can be updated via command line tools.

   ``RECD_RESTART_TS``
      The :program:`traffic_server` process must be restarted for a new value to take effect.

   ``RECD_RESTART_TM``
      Deprecated.

required:``RecordRequiredType``
   Effectively a boolean that specifies if the record is required to be present,
   with ``RR_NULL`` meaning not required and ``RR_REQUIRED`` indicating that it
   is required. Given that using ``RR_REQUIRED`` would be a major
   incompatibility, ``RR_NULL`` is generally the better choice.

check:``RecCheckT``
   Additional type checking. It is unclear if this is actually implemented. The
   valid values are:

   ``RECC_NULL``
      No additional checking.

   ``RECC_STR``
      Verify the value is a string.

   ``RECC_INT``
      Verify the value is an integer.

   ``RECC_IP``
      Verify the value is an IP address. Unknown if this checks for IPv6.

.. XXX confirm RECC_IP & IPv6 behavior

pattern:``char const*``
   This provides a regular expressions (PCRE format) for validating the value,
   beyond the basic type validation performed by ``RecCheckT``. This can be
   ``NULL`` if there is no regular expression to use.

access:``RecAccessT``
   Access control. The valid values are:

   ``RECA_NULL``
      The value is read / write.

   ``RECA_READ_ONLY``
      The value is read only.

   ``RECA_NO_ACCESS``
      No access to the value; only privileged level parts of ATS can access the
      value.

Variable Infrastructure
=======================

The primary effort in defining a configuration variable is handling updates,
generally via :option:`traffic_ctl config reload`. This is handled in a generic way, as
described in the next section, or in a :ref:`more specialized way <http-config-var-impl>`
(built on top of the generic mechanism) for HTTP related configuration
variables. This is only needed if the variable is marked as dynamically
updatable (|RECU_DYNAMIC|_) although HTTP configuration variables should be
dynamic if possible.

Documentation and Defaults
--------------------------

A configuration variable should be documented in :file:`records.yaml`. There
are many examples in the file already that can be used for guidance. The general
format is to use the tag ::

   .. ts:cv:`variable.name.here`

The arguments to this are the same as for the configuration file. The
documentation generator will pick out key bits and use them to decorate the
entry. In particular if a value is present it will be removed and used as the
default value. You can attach some additional options to the variable. These
are:

reloadable
   The variable can be reloaded via command line on a running Traffic Server.

metric
   Specify the units for the value. This is critical for variables that use unexpected or non-obvious metrics, such as minutes instead of seconds, or disk sectors instead of bytes.

deprecated
   Mark a variable as deprecated.

.. topic:: Example

   \:ts\:cv\:\`custom.variable\`
      :reloadable:
      :units: minutes
      :deprecated:

If you need to refer to another configuration variable in the documentation, you
can use the form ::

   :ts:cv:`the.full.name.of.the.variable`

This will display the name as a link to the full definition.

In general, a new configuration variable should not be present in the default
:file:`records.yaml`. If it is added, such defaults should be added to the
file ``proxy/config/records.config.default.in``. This is used to generate the
default :file:`records.yaml`. Just add the variable to the file in an
appropriate place with a proper default as this will now override whatever
default you put in the code for new installs.

Handling Updates
----------------

The simplest mechanism for handling updates is the ``REC_EstablishStaticConfigXXX``
family of functions. This mechanism will cause the value in the indicated
instance to be updated in place when an update to :file:`records.yaml` occurs.
This is done asynchronously using atomic operations. Use of these variables must
keep that in mind.

If a variable requires additional handling when updated a callback can be
registered which is called when the variable is updated. This is what the
``REC_EstablishStaticConfigXXX`` calls do internally with a callback that simply
reads the new value and writes it to storage indicated by the call parameters.
The functions used are the ``link_XXX`` static functions in |RecCore.cc|_.

To register a configuration variable callback, call ``RecRegisterConfigUpdateCb``
with the arguments:

``char const*`` *name*
   The variable name.

*callback*
   A function with the signature ``<int (char const* name, RecDataT type, RecData data, void* cookie)>``.
   The :arg:`name` value passed is the same as the :arg:`name` passed to the
   registration function as is the :arg:`cookie` argument. The :arg:`type` and
   :arg:`data` are the new value for the variable. The return value is currently
   ignored. For future compatibility return ``REC_ERR_OKAY``.

``void*`` *cookie*
   A value passed to the *callback*. This is only for the callback, the
   internals simply store it and pass it on.

*callback* is called under lock so it should be quick and not block. If that is
necessary a :term:`continuation` should be scheduled to handle the required
action.

.. note::

   The callback occurs asynchronously. For HTTP variables as described in the
   next section, this is handled by the more specialized HTTP update mechanisms.
   Otherwise it is the implementer's responsibility to avoid race conditions.

.. _http-config-var-impl:

HTTP Configuration Values
-------------------------

Variables used for HTTP processing should be declared as members of the
``HTTPConfigParams`` structure (but see :ref:`overridable-config-vars` for
further details) and use the specialized HTTP update mechanisms which handle
synchronization and initialization issues.

The configuration logic maintains two copies of the ``HTTPConfigParams``
structure, the master copy and the current copy. The master copy is kept in the
``m_master`` member of the ``HttpConfig`` singleton. The current copy is kept in
the ConfigProcessor. The goal is to provide a (somewhat) atomic update for
configuration variables which are loaded individually in to the master copy as
updates are received and then bulk copied to a new instance which is then
swapped in as the current copy. The HTTP state machine interacts with this
mechanism to avoid race conditions.

For each variable, a mapping between the variable name and the appropriate
member in the master copy should be established between in the ``HTTPConfig::startup``
method. The ``HttpEstablishStaticConfigXXX`` functions should be used unless
there is a strong, explicit reason to not do so.

The ``HTTPConfig::reconfigure`` method handles the current copy of the HTTP
configuration variables. Logic should be added here to copy the value from the
master copy to the current copy. Generally this will be a simple assignment. If
there are dependencies between variables, those should be checked and enforced
in this method.

.. _overridable-config-vars:

Overridable Variables
---------------------

HTTP related variables that are changeable per transaction are stored in the
``OverridableHttpConfigParams`` structure, an instance of which is the ``oride``
member of ``HTTPConfigParams`` and therefore the points in the previous section
still apply. The only difference for that is the further ``.oride`` member specifier in the structure references.

The variable is required to be accessible from the transaction API. In addition
to any custom API functions used to access the value, the following items are
required for generic access:

#. Add a value to the ``TSOverridableConfigKey`` enumeration in |apidefs.h.in|_.

#. Augment ``Overridable_Map`` in |overridable_txn_vars.cc|_ to include configuration variable.

#. Update the function ``_conf_to_memberp`` in |InkAPI.cc|_ to have a case for the enumeration value
   in ``TSOverridableConfigKey``.

#. Update the testing logic in |InkAPITest.cc|_ by adding the string name of the
   configuration variable to the ``SDK_Overridable_Configs`` array.

#. Update the Lua plugin enumeration ``TSLuaOverridableConfigKey`` in |ts_lua_http_config.c|_.

#. Update the documentation of :ref:`ts-overridable-config` in |TSHttpOverridableConfig.en.rst|_.

API conversions
---------------

A relatively new feature for overridable variables is the ability to keep them in more natural data
types and convert as needed to the API types. This in turns enables defining the configuration
locally in a module and then "exporting" it to the API interface. Modules then do not have to
include headers for all types in all overridable configurations.

The conversion is done through an instance of :code:`MgmtConverter`. This has 6 points to
conversions, a load and store function for each of the types :code:`MgmtInt`, :code:`MgmtFloat`, and
:code:`MgmtInt`. The :code:`MgmtByte` type is handled by the :code:`MgmtInt` conversions. In general
each overridable variable will specify two of these, a load and store for a specific type, although
it is possible to provide other pairs, e.g. if a value is an enumeration can should be settable
as a string as well as an integer.

The module is responsible for creating an instance of :code:`MgmtConverter` with the appropriate
load / store function pairs set. The declaration must be visible in the :ts:git:`proxy/InkAPI.cc`
file. The function :code:`_conf_to_memberp` sets up the conversion. For the value of the enumeration
:c:type:`TSOverridableConfigKey` that specifies the overridable variable, code is added to specify
the member and the conversion. There are default converters for the API types and if the overridable
is one of those, it is only necessary to call :code:`_memberp_to_generic` passing in a pointer to
the variable. For a variable with conversion, :arg:`ret` should be set to point to the variable and
:arg:`conv` set to point to the converter for that variable. If multiple variables are of the same
type they can use the same converter because a pointer to the specific member is passed to the
converter.
