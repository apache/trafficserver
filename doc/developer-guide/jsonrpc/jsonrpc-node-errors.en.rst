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


.. _jsonrpc-node-errors-standard-errors:

Standard errors
===============

=============== ========================================= =========================================================
Field           Message                                   Description
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
Field           Message                                   Description
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
                                                          permitted by some constraint or authorization issue. Check
                                                          :ref:`jsonrpc-node-errors-unauthorized-action` for mode details.
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
Field           Message                                   Description
=============== ========================================= =========================================================
1               Error getting peer credentials: {}        Something happened while trying to get the peers credentials.
                                                          The error string will show the error code(`errno`) returned by the
                                                          server.
2               Denied privileged API access for uid={}   Permission denied. Unix Socket credentials were checked and they haven't meet
                gid={}                                    the required policy. The handler was configured as restricted
                                                          and the socket credentials failed to validate. Check TBC for
                                                          more information.
=============== ========================================= =========================================================
