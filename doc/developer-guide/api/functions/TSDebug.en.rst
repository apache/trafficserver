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

TSDebug
*******

Traffic Server Debugging APIs.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSStatus(const char * format, ...)
.. function:: void TSNote(const char * format, ...)
.. function:: void TSWarning(const char * format, ...)
.. function:: void TSError(const char * format, ...)
.. function:: void TSFatal(const char * format, ...)
.. function:: void TSAlert(const char * format, ...)
.. function:: void TSEmergency(const char * format, ...)
.. function:: void TSDebug(const char * tag, const char * format, ...)
.. function:: int TSIsDebugTagSet(const char * tag)
.. function:: void TSDebugSpecific(int debug_flag, const char * tag, const char * format, ...)
.. function:: void TSHttpTxnDebugSet(TSHttpTxn txnp, int on)
.. function:: void TSHttpSsnDebugSet(TSHttpSsn ssn, int on)
.. function:: int TSHttpTxnDebugGet(TSHttpTxn txnp)
.. function:: int TSHttpSsnDebugGet(TSHttpSsn ssn)
.. function:: const char* TSHttpServerStateNameLookup(TSServerState state)
.. function:: const char* TSHttpHookNameLookup(TSHttpHookID hook)
.. function:: const char* TSHttpEventNameLookup(TSEvent event)
.. macro:: void TSAssert( ... )
.. macro:: void TSReleaseAssert( ... )

diags.log
=========

The following methods print to diags.log with expected reactions as a coordinated outcome of
Traffic Server, Traffic Manager, AuTest, CI, and your log monitoring service/dashboard (e.g. Splunk)

.. csv-table::
   :header: "API", "Purpose", "TrafficManager", "AuTest+CI",  "LogMonitor"
   :widths: 15, 20, 10, 10, 10

   ":func:`TSStatus`","basic information"
   ":func:`TSNote`","significant information"
   ":func:`TSWarning`","concerning information","","","track"
   ":func:`TSError`","operational failure","","FAIL","review"
   ":func:`TSFatal`","recoverable crash","restart","FAIL","review"
   ":func:`TSAlert`","significant crash","restart","FAIL","ALERT"
   ":func:`TSEmergency`","unrecoverable,misconfigured","EXIT","FAIL","ALERT"

.. note::
    :func:`TSFatal`, :func:`TSAlert`, and :func:`TSEmergency` can be called within
    :func:`TSPluginInit`, such that Traffic Server can be shutdown promptly when
    the plugin fails to initialize properly.

trafficserver.out
=================

:func:`TSDebug` logs the debug message only if the given debug :arg:`tag` is enabled.
It writes output to the Traffic Server debug log through stderr.

:func:`TSIsDebugTagSet` returns non-zero if the given debug :arg:`tag` is
enabled.

In debug mode, :macro:`TSAssert` Traffic Server to prints the file
name, line number and expression, and then aborts. In release mode,
the expression is not removed but the effects of printing an error
message and aborting are. :macro:`TSReleaseAssert` prints an error
message and aborts in both release and debug mode.

:func:`TSDebugSpecific` emits a debug line even if the debug :arg:`tag`
is turned off, as long as debug flag is enabled. This can be used
in conjunction with :func:`TSHttpTxnDebugSet`, :func:`TSHttpSsnDebugSet`,
:func:`TSHttpTxnDebugGet` and :func:`TSHttpSsnDebugGet` to enable
debugging on specific session and transaction objects.

:func:`TSHttpServerStateNameLookup`, :func:`TSHttpHookNameLookup` and
:func:`TSHttpEventNameLookup` converts the respective internal state to a
string representation. This can be useful in debugging (:func:`TSDebug`),
logging and other types notifications.

Examples
========

This example uses :func:`TSDebugSpecific` to log a message when a specific
debugging flag is enabled::

    #include <ts/ts.h>

    // Produce information about a hook receiving an event
    TSDebug(PLUGIN_NAME, "Entering hook=%s, event=%s",
            TSHttpHookNameLookup(hook), TSHttpEventNameLookup(event));

    // Emit debug message if "tag" is enabled or the txn debug
    // flag is set.
    TSDebugSpecifc(TSHttpTxnDebugGet(txn), "tag" ,
            "Hello World from transaction %p", txn);

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`printf(3)`
