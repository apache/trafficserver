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

.. highlight:: cpp
.. default-domain:: cpp

.. _JSONRPC: https://www.jsonrpc.org/specification
.. _JSON: https://www.json.org/json-en.html


.. _jsonrpc-node-errors:


JSON RPC errors
***************

A list of codes and descriptions of errors that could be send back from the JSONRPC server. JSONRPC response messages could contains
different set of errors in the following format:

.. note::

   Check  :ref:`jsonrpc-error` for details about the error structure.


.. code-block::json

   {
      "error": {
         "code": -32601,
         "message": "Method not found"
      },
      "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
      "jsonrpc": "2.0"
   }

In some cases the data field could be populated:

.. code-block:: json

   {
      "jsonrpc": "2.0",
      "error":{
         "code": 10,
         "message": "Unauthorized action",
         "data":[
            {
               "code": 2,
               "message":"Denied privileged API access for uid=XXX gid=XXX"
            }
         ]
      },
      "id":"5e273ec0-3e3b-4a81-90ec-aeee3d38073f"
   }


.. _jsonrpc-node-errors-severity:

Severity
========

When a method handler fails (``9``, ``Error during execution``), a ``data`` entry can also carry a
``severity``, the integer value of the ``DiagsLevel`` the handler gave that annotation. The field
is only present when the handler set one:

.. code-block:: json

   {
      "jsonrpc": "2.0",
      "error": {
         "code": 9,
         "message": "Error during execution",
         "data": [
            {
               "code": 3000,
               "severity": 4,
               "message": "Server already draining."
            }
         ]
      },
      "id": "a3b5ef20-4c2e-11ef-8d76-001fc69cc946"
   }

====  ===========
Code  Severity
====  ===========
0     Diag
1     Debug
2     Status
3     Note
4     Warn
5     Error
6     Fatal
7     Alert
8     Emergency
====  ===========

An entry without a ``severity`` counts as ``Error``, which is how the server classifies an
annotation without one, and a client should treat it the same way. Older servers never send the
field. Only the severity of the annotation itself is sent, not the one of the errata that holds it:
a handler that wants a level other than ``Error`` sets it on the annotation, for example
``note(ERRATA_FATAL, "...")``. A client should treat a ``severity`` that is not one of the integers
above as the highest level.

A handler that reports a condition which is not a failure, such as a state that is already in
place, marks it ``Warn``. It cannot go lower: the server only answers with an error when the
severity of the handler's errata is ``Warn`` or above, and otherwise returns a result and drops the
annotations. That severity is the highest one set explicitly, or ``Error`` when none is, so a
handler that adds a ``Warn`` next to a real failure must mark the failure ``Error`` explicitly.

:program:`traffic_ctl` uses the ``severity`` to pick its exit status, see
:option:`traffic_ctl --error-level`.


.. _jsonrpc-node-errors-standard-errors:

Standard errors
===============

=============== ========================================= =========================================================
Id              Message                                   Description
=============== ========================================= =========================================================
-32700          Parse error                               Invalid JSON was received by the server.
                                                          An error occurred on the server while parsing the JSON text.
-32600          Invalid Request                           The JSON sent is not a valid Request object.
-32601          Method not found                          The method does not exist / is not available.
-32602          Invalid params                            Invalid method parameter(s).
-32603          Internal error                            Internal `JSONRPC`_ error.
=============== ========================================= =========================================================

.. _jsonrpc-node-errors-custom-errors:

Custom errors
=============

The following error list are defined by the server.

=============== ========================================= =========================================================
Id              Message                                   Description
=============== ========================================= =========================================================
1               Invalid version, 2.0 only                 The server only accepts version field equal to `2.0`.
2               Invalid version type, should be a string  Version field should be a literal string.
3               Missing version field                     No version field present, version field is mandatory.
4               Invalid method type, should be a string   The method field should be a literal string.
5               Missing method field                      No method field present, method field is mandatory.
6               Invalid params type. A Structured value   Params field should be a structured type, list or structure.
                is expected                               This is similar to `-32602`
7               Invalid id type                           If field should be a literal string.
8               Use of null as id is discouraged          Id field value is null, as per the specs this is discouraged,
                                                          the server will not accept it.
9               Error during execution                    An error occurred during the execution of the RPC call.
                                                          This error is used as a generic High level error. The specifics
                                                          details about the error, in most cases are specified in the
                                                          ``data`` field.
10              Unauthorized action                       The rpc method will not be invoked because the action is not
                                                          permitted by some constraint or authorization issue.Check
                                                          :ref:`jsonrpc-node-errors-unauthorized-action` for mode details.
11              Use of an empty string as id is           An empty string "" as an id will not be accepted by the server.
                discouraged
=============== ========================================= =========================================================

.. _jsonrpc-node-errors-unauthorized-action:

Unauthorized action
-------------------

Under this error, the `data` field could be populated with the following errors, eventually more than one could be in set.

.. code-block:: json

   "data":[
      {
         "code":2,
         "message":"Denied privileged API access for uid=XXX gid=XXX"
      }
   ]

=============== ========================================= =========================================================
Id              Message                                   Description
=============== ========================================= =========================================================
1               Error getting peer credentials: {}        Something happened while trying to get the peers credentials.
                                                          The error string will show the error code(`errno`) returned by the
                                                          server.
2               Denied privileged API access for uid={}   Permission denied. Unix Socket credentials were checked and they haven't meet
                gid={}                                    the required policy. The handler was configured as restricted
                                                          and the socket credentials failed to validate. Check TBC for
                                                          more information.
=============== ========================================= =========================================================
