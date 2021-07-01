.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

TSRemapInit
***********

Traffic Server remap plugin entry points.

Synopsis
========

.. code-block:: c

    #include <ts/ts.h>
    #include <ts/remap.h>

.. function:: TSReturnCode TSRemapInit(TSRemapInterface * api_info, char * errbuff, int errbuff_size)
.. function:: void TSRemapPreConfigReload(void)
.. function:: void TSRemapPostConfigReload(TSReturnCode reloadStatus)
.. function:: void TSRemapDone(void)
.. function:: TSRemapStatus TSRemapDoRemap(void * ih, TSHttpTxn rh, TSRemapRequestInfo * rri)
.. function:: TSReturnCode TSRemapNewInstance(int argc, char * argv[], void ** ih, char * errbuff, int errbuff_size)
.. function:: void TSRemapDeleteInstance(void * )
.. function:: void TSRemapOSResponse(void * ih, TSHttpTxn rh, int os_response_type)

Description
===========

The Traffic Server remap interface provides a simplified mechanism for
plugins to manipulate HTTP transactions. A remap plugin is not global; it
is configured on a per-remap rule basis, which enables you to customize
how URLs are redirected based on individual rules in :file:`remap.config`.
Writing a remap plugin consists of implementing one or more of the
remap entry points and configuring :file:`remap.config` to
route the transaction through your plugin. Multiple remap plugins can be
specified for a single remap rule, resulting in a remap plugin chain
where each plugin is given an opportunity to examine the HTTP transaction.

:func:`TSRemapInit` is a required entry point. This function will be called once when Traffic Server
loads the plugin. If the optional :func:`TSRemapDone`
entry point is available, Traffic Server will call then when unloading
the remap plugin.

A remap plugin may be invoked for different remap rules. Traffic Server
will call the entry point each time a plugin is specified in a remap
rule. When a remap plugin instance is no longer required, Traffic Server
will call :func:`TSRemapDeleteInstance`. At that point, it's safe to remove
any data or continuations associated with that instance.

:func:`TSRemapDoRemap` is called for each HTTP transaction. This is a mandatory
entry point. In this function, the remap plugin may examine and modify
the HTTP transaction.

:func:`TSRemapPreConfigReload` is called *before* the parsing of a new remap configuration starts
to notify plugins of the coming configuration reload. It is called on all already loaded plugins,
invoked by current and all previous still used configurations. This is an optional entry point.

:func:`TSRemapPostConfigReload` is called to indicate the end of the new remap configuration
load. It is called on the newly and previously loaded plugins, invoked by the new, current and
previous still used configurations. It also indicates whether the configuration reload was successful
by passing :macro:`TSREMAP_CONFIG_RELOAD_FAILURE` in case of failure and to notify the plugins if they
are going to be part of the new configuration by passing :macro:`TSREMAP_CONFIG_RELOAD_SUCCESS_PLUGIN_USED`
or :macro:`TSREMAP_CONFIG_RELOAD_SUCCESS_PLUGIN_UNUSED`. This is an optional entry point.

Generally speaking, calls to these functions are mutually exclusive. The exception
is for functions which take an HTTP transaction as a parameter. Calls to these
transaction-specific functions for different transactions are not necessarily mutually exclusive
of each other.

For further information, see :ref:`developer-plugins-remap`.

Types
=====

.. enum:: TSRemapStatus

   Status return value for remap callback.

   .. enumerator:: TSREMAP_DID_REMAP

      The remap callback modified the request.

   .. enumerator:: TSREMAP_DID_REMAP_STOP

      The remap callback modified the request and that no more remapping callbacks should be invoked.

   .. enumerator:: TSREMAP_NO_REMAP

      The remap callback did not modify the request.

   .. enumerator:: TSREMAP_NO_REMAP_STOP

      The remap callback did not modify the request and that no further remapping
      callbacks should be invoked.

   .. enumerator:: TSREMAP_ERROR

      The remapping attempt in general failed and the transaction should fail with an
      error return to the user agent.

.. enum:: TSRemapReloadStatus

   .. enumerator:: TSREMAP_CONFIG_RELOAD_FAILURE

      Notify the plugin that configuration parsing failed.

   .. enumerator:: TSREMAP_CONFIG_RELOAD_SUCCESS_PLUGIN_USED

      Configuration parsing succeeded and plugin was used by the new configuration.

   .. enumerator:: TSREMAP_CONFIG_RELOAD_SUCCESS_PLUGIN_UNUSED

      Configuration parsing succeeded but plugin was NOT used by the new configuration.

Return Values
=============

:func:`TSRemapInit` and :func:`TSRemapNewInstance` should return
:data:`TS_SUCCESS` on success, and :data:`TS_ERROR` otherwise. A
return value of :data:`TS_ERROR` is unrecoverable.

:func:`TSRemapDoRemap` returns a status code that indicates whether the HTTP transaction has been
modified and whether Traffic Server should continue to evaluate the chain of remap plugins. If the
transaction was modified, the plugin should return :macro:`TSREMAP_DID_REMAP` or
:macro:`TSREMAP_DID_REMAP_STOP`; otherwise it should return :macro:`TSREMAP_NO_REMAP` or
:macro:`TSREMAP_NO_REMAP_STOP`. If Traffic Server should not send the transaction to subsequent
plugins in the remap chain, return :macro:`TSREMAP_NO_REMAP_STOP` or :macro:`TSREMAP_DID_REMAP_STOP`.
Returning :macro:`TSREMAP_ERROR` causes Traffic Server to stop evaluating the remap chain and respond
with an error.

See Also
========

:manpage:`TSAPI(3ts)`
