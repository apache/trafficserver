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

Dbg
***

Traffic Server Debugging APIs.

Synopsis
========

.. code-block:: c

    #include <ts/ts.h>

.. function:: void TSStatus(const char * format, ...)
.. function:: void TSNote(const char * format, ...)
.. function:: void TSWarning(const char * format, ...)
.. function:: void TSError(const char * format, ...)
.. function:: void TSFatal(const char * format, ...)
.. function:: void TSAlert(const char * format, ...)
.. function:: void TSEmergency(const char * format, ...)
.. cpp:type:: DbgCtl
.. cpp:function:: void Dbg(DbgCtl &ctl, const char * format, ...)
.. cpp:function:: bool DbgCtl::on()
.. cpp:function:: void SpecificDbg(bool debug_flag, DbgCtl &ctl, const char * format, ...)
.. cpp:function:: void DbgPrint(DbgCtl &ctl, const char * format, ...)
.. function:: void TSHttpTxnDebugSet(TSHttpTxn txnp, int on)
.. function:: void TSHttpSsnDebugSet(TSHttpSsn ssn, int on)
.. function:: int TSHttpTxnDebugGet(TSHttpTxn txnp)
.. function:: int TSHttpSsnDebugGet(TSHttpSsn ssn)
.. function:: const char* TSHttpServerStateNameLookup(TSServerState state)
.. function:: const char* TSHttpHookNameLookup(TSHttpHookID hook)
.. function:: const char* TSHttpEventNameLookup(TSEvent event)
.. macro:: TSAssert( ... )
.. macro:: TSReleaseAssert( ... )

diags.log
=========

The following methods print to diags.log with expected reactions as a coordinated outcome of
Traffic Server, AuTest, CI, and your log monitoring service/dashboard (e.g. Splunk)

.. csv-table::
   :header: "API", "Purpose", "AuTest+CI",  "LogMonitor"
   :widths: 15, 20, 10, 10

   ":func:`TSStatus`","basic information"
   ":func:`TSNote`","significant information"
   ":func:`TSWarning`","concerning information","","track"
   ":func:`TSError`","operational failure","FAIL","review"
   ":func:`TSFatal`","recoverable crash","FAIL","review"
   ":func:`TSAlert`","significant crash","FAIL","ALERT"
   ":func:`TSEmergency`","unrecoverable,misconfigured","FAIL","ALERT"

.. note::
    :func:`TSFatal`, :func:`TSAlert`, and :func:`TSEmergency` can be called within
    :func:`TSPluginInit`, such that Traffic Server can be shutdown promptly when
    the plugin fails to initialize properly.

trafficserver.out
=================

cpp:type:`DbgCtl` is a C++ class. Its constructor is ``DbgCtl::DbgCtl(const char *tag)``.  ``tag`` is
the debug tag for the control, as a null-terminated string.  The control is enabled/on when the tag is
enabled.

cpp:func:`Dbg` logs the debug message only if the given debug control referred to by
:arg:`ctl` is enabled.  It writes output to the Traffic Server debug log through stderr.

``ctl.on()`` (where ``ctl`` is an instance of ``DbgCtl``) returns true if ``ctl`` is on.

In debug mode, :macro:`TSAssert` Traffic Server to prints the file
name, line number and expression, and then aborts. In release mode,
the expression is not removed but the effects of printing an error
message and aborting are. :macro:`TSReleaseAssert` prints an error
message and aborts in both release and debug mode.

cpp:func:`SpecificDbg` emits a debug line even if the debug :arg:`tag`
is turned off, as long as debug flag is enabled. This can be used
in conjunction with :func:`TSHttpTxnDebugSet`, :func:`TSHttpSsnDebugSet`,
:func:`TSHttpTxnDebugGet` and :func:`TSHttpSsnDebugGet` to enable
debugging on specific session and transaction objects.

cpp:func:`DbgPrint` emits a debug line even if the debug :arg:`tag`
is turned off.

:func:`TSHttpServerStateNameLookup`, :func:`TSHttpHookNameLookup` and
:func:`TSHttpEventNameLookup` converts the respective internal state to a
string representation. This can be useful in debugging (cpp:func:`Dbg`),
logging and other types notifications.

(For an example of how to write a plugin with debug tracing, that can be
compiled with both |TS| Version 10 and older versions of ATS, see ``redirect_1``.)

Examples
========

This example uses cpp:func:`SpecificDbg` to log a message when a specific
debugging flag is enabled::

    #include <ts/ts.h>

    DbgCtl dbg_ctl{PLUGIN_NAME};

    // Produce information about a hook receiving an event
    Dbg(dbg_ctl, "Entering hook=%s, event=%s",
        TSHttpHookNameLookup(hook), TSHttpEventNameLookup(event));

    // Emit debug message if "dbg_ctl" is enabled or the txn debug
    // flag is set.
    SpecificDbg(TSHttpTxnDebugGet(txn), dbg_ctl ,
                "Hello World from transaction %p", txn);

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`printf(3)`
