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

.. default-domain:: c

.. _ts-overridable-config:

=======================
TSHttpOverridableConfig
=======================

Synopsis
========
`#include <ts/ts.h>`

.. type:: TSOverridableConfigKey

.. function:: TSReturnCode TSHttpTxnConfigIntSet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtInt value)
.. function:: TSReturnCode TSHttpTxnConfigIntGet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtInt* value)
.. function:: TSReturnCode TSHttpTxnConfigFloatSet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtFloat value)
.. function:: TSReturnCode TSHttpTxnConfigFloatGet(TSHttpTxn txnp, TSOverridableConfigKey key, TSMgmtFloat* value)
.. function:: TSReturnCode TSHttpTxnConfigStringSet(TSHttpTxn txnp, TSOverridableConfigKey key, const char* value, int length)
.. function:: TSReturnCode TSHttpTxnConfigStringGet(TSHttpTxn txnp, TSOverridableConfigKey key, const char** value, int* length)
.. function:: TSReturnCode TSHttpTxnConfigFind(const char* name, int length, TSOverridableConfigKey* key, TSRecordDataType* type)

Description
===========

Some of the values that are set in :file:`records.config` can be changed for a specific transaction. It is important to
note that these functions change the configuration values stored for the transation, which is not quite the same as
changing the actual operating values of the transaction. The critical effect is the value must be changed before it is
used by the transaction - after that, changes will not have any effect.

All of the ``...Get`` functions store the internal value in the storage indicated by the :arg:`value` argument. For strings :arg:`length*` will receive the length of the string.

The values are identified by the enumeration :type:`TSOverridableConfigKey`. String values can be used indirectly by
first passing them to :func:`TSHttpTxnConfigFind` which, if the string matches an overridable value, return the key and data
type.

Examples
========

Enable :ref:`transaction buffer control <transaction-buffering-control>` with a high water mark of 262144 and a low water mark of 65536. ::

   int callback(TSCont contp, TSEvent event, void* data)
   {
      TSHttpTxn txnp = static_cast<TSHttpTxn>(data);
      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_FLOW_CONTROL_ENABLED, 1);
      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_FLOW_CONTROL_HIGH_WATER_MARK, 262144);
      TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_FLOW_CONTROL_LOWER_WATER_MARK, 65536);
      return 0;
   }

See also
========
:manpage:`TSAPI(3ts)`
