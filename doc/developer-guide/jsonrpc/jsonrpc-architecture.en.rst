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


.. |str| replace:: ``string``
.. |arraynum| replace:: ``array[number]``
.. |arraystr| replace:: ``array[string]``
.. |num| replace:: *number*
.. |strnum| replace:: *string|number*
.. |object| replace:: *object*
.. |array| replace:: *array*
.. |optional| replace:: ``optional``

.. |arrayrecord| replace:: ``array[record]``
.. |arrayerror| replace:: ``array[errors]``

JSONRPC
*******

RPC Architecture
================


Protocol
^^^^^^^^

The RPC mechanism implements the  `JSONRPC`_ protocol. You can refer to this section `jsonrpc-protocol`_ for more information.

Server
^^^^^^

IPC
"""

The current server implementation runs on an IPC Socket(Unix Domain Socket). This server implements an iterative server style.
The implementation runs on a dedicated ``TSThread`` and as their style express, this performs blocking calls to all the registered handlers.
Configuration for this particular server style can be found in the admin section :ref:`admnin-jsonrpc-configuration`.


Using the JSONRPC mechanism
^^^^^^^^^^^^^^^^^^^^^^^^^^^

As a user, currently,  :program:`traffic_ctl` exercises this new protocol, please refer to the :ref:`traffic_ctl_jsonrpc` section.

As a developer, please refer to the :ref:`jsonrpc_development` for a more detailed guide.



JSON Parsing
^^^^^^^^^^^^

Our JSONRPC  protocol implementation uses lib yamlcpp for parsing incoming and outgoing requests,
this allows the server to accept either JSON or YAML format messages which then will be parsed by the protocol implementation. This seems handy
for user that want to feed |TS| with existing yaml configuration without the need to translate yaml into json.

.. note::

   :program:`traffic_ctl` have an option to read files from disc and push them into |TS| through the RPC server. Files should be a
   valid `JSONRPC`_ message. Please refer to :ref:`traffic_ctl rpc` for more details.


In order to programs communicate with |TS| , This one implements a simple RPC mechanism to expose all the registered API handlers.

You can check all current API by:

   .. code-block:: bash

      traffic_ctl rpc get-api

or by using the ``show_registered_handlers`` API method.


.. _jsonrpc-protocol:

JSONRPC 2.0 Protocol
====================

JSON-RPC is a stateless, light-weight remote procedure call (RPC) protocol. Primarily this specification defines several data structures
and the rules around their processing. It is transport agnostic in that the concepts can be used within the same process, over sockets,
over http, or in many various message passing environments. It uses JSON (RFC 4627) as data format.

Overview
========

.. note::

   Although most of the protocol specs are granted, we have implemented some exceptions. All the modifications will be properly documented.


There are a set  of mandatory fields that must be included in a `JSONRPC`_ message as well as some optional fields, all this is documented here,
you also can find this information in the `JSONRPC`_ link.

.. _jsonrpc-request:

Requests
^^^^^^^^

Please find the `jsonrpc 2.0 request` schema for reference ( `mgmt2/rpc/schema/jsonrpc_request_schema.json` ).

* Mandatory fields.


   ============ ====== =======================================================================================
   Field        Type   Description
   ============ ====== =======================================================================================
   ``jsonrpc``  |str|  Protocol version. |TS| follows the version 2.0 so this field should be **only** ``2.0``
   ``method``   |str|  Method name that is intended to be invoked.
   ============ ====== =======================================================================================


* Optional parameters:



   * ``params``:

      A Structured value that holds the parameter values to be used during the invocation of the method. This member
      **may** be omitted. If passed then a parameters for the rpc call **must** be provided as a Structured value.
      Either by-position through an Array or by-name through an Object.

      #. ``by-position`` |array|

         params **must** be an ``array``, containing the values in the server expected order.


         .. code-block:: json

            {
               "params": [
                  "apache", "traffic", "server"
               ]
            }


         .. code-block:: json

            {
               "params": [
                  1, 2, 3, 4
               ]
            }


         .. code-block:: json

            {
               "params": [{
                  "name": "Apache"
               },{
                  "name": "Traffic"
               },{
                  "name": "Server"
               }]
            }

      #. ``by-name``: |object|

         Params **must** be an ``object``, with member names that match the server expected parameter names.
         The absence of expected names **may** result in an error being generated by the server. The names **must**
         match exactly, including case, to the method's expected parameters.

         .. code-block:: json

            {
               "params": {
                  "name": "Apache"
               }
            }

   * ``id``: |str|.

      An identifier established by the Client. If present, the request will be treated as a jsonrpc method and a
      response should be expected from the server. If it is not present, the server will treat the request as a
      notification and the client should not expect any response back from the server.
      *Although a |number| can  be specified here we will convert this internally to a |str|. The response will be a |str|.*


.. _jsonrpc-response:

Responses
^^^^^^^^^

Although each individual API call will describe the response details and some specific errors, in this section we will describe a high
level protocol response, some defined by the `JSONRPC`_ specs and some by |TS|

Please find the `jsonrpc 2.0 response` schema for reference( `mgmt2/rpc/schema/jsonrpc_response_schema.json` ).



The responses have the following structure:


   ============ ======== ==============================================
   Field        Type     Description
   ============ ======== ==============================================
   ``jsonrpc``  |strnum| A Number that indicates the error type that occurred.
   ``result``            Result of the invoked operation. See `jsonrpc-result`_
   ``id``       |strnum| It will be the same as the value of the id member in the `jsonrpc-request`_ .
                         We will not be using `null` if the `id` could not be fetch from the request,
                         in that case the field will not be set.
   ``error``    |object| Error object, it will be present in case of an error. See `jsonrpc-error`_
   ============ ======== ==============================================

Example 1:

Request

   .. code-block:: json

      {
         "jsonrpc": "2.0",
         "result": ["hello", 5],
         "id": "9"
      }


Response

   .. code-block:: json

      {
         "jsonrpc":"2.0",
         "error":{
            "code":5,
            "message":"Missing method field"
         },
         "id":"9"
      }


As the protocol specifies |TS| have their own set of error, in the example above it's clear that the incoming request is missing
the method name, which |TS| sends a clear response error back.

.. _jsonrpc-result:

Result
""""""


* This member is required and will be present on success.
* This member will not exist if there was an error invoking the method.
* The value of this member is determined by the method invoked on the Server.

In |TS| a RPC method that does not report any error and have nothing to send back to the client will use the following format to
express that the call was successfully handled and the command was executed.


.. _success_response:


Example:

   .. code-block:: json
      :emphasize-lines: 4

      {
         "id": "89fc5aea-0740-11eb-82c0-001fc69cc946",
         "jsonrpc": "2.0",
         "result": "success"
      }

``"result": "success"`` will be set.

.. _jsonrpc-error:

Errors
""""""

The specs define the error fields that the client must expect to be sent back from the Server in case of any error.


=============== ======== ==============================================
Field           Type     Description
=============== ======== ==============================================
``code``        |num|    A Number that indicates the error type that occurred.
``message``     |str|    A String providing a short description of the error.
``data``        |object| This is an optional field that contains additional error data. Depending on the API this could contain data.
=============== ======== ==============================================

# data.

This can be used for nested error so |TS| can inform a detailed error.

   =============== ======== ==============================================
   Field           Type     Description
   =============== ======== ==============================================
   ``code``        |str|    The error code. Integer type.
   ``message``     |str|    The explanatory string for this error.
   =============== ======== ==============================================




Examples:

# Fetch a config record response from |TS|

Request:


   .. code-block:: json

      {
         "id":"0f0780a5-0758-4f51-a177-752facc7c0eb",
         "jsonrpc":"2.0",
         "method":"admin_lookup_records",
         "params":[
            {
               "record_name":"proxy.config.diags.debug.tags",
               "rec_types":[
                  "1",
                  "16"
               ]
            }
         ]
      }

Response:

   .. code-block:: json

      {
         "jsonrpc":"2.0",
         "result":{
            "recordList":[
               {
                  "record":{
                     "record_name":"proxy.config.diags.debug.tags",
                     "record_type":"3",
                     "version":"0",
                     "raw_stat_block":"0",
                     "order":"423",
                     "config_meta":{
                        "access_type":"0",
                        "update_status":"0",
                        "update_type":"1",
                        "checktype":"0",
                        "source":"3",
                        "check_expr":"null"
                     },
                     "record_class":"1",
                     "overridable":"false",
                     "data_type":"STRING",
                     "current_value":"rpc",
                     "default_value":"http|dns"
                  }
               }
            ]
         },
         "id":"0f0780a5-0758-4f51-a177-752facc7c0eb"
      }


# Getting errors from |TS|


Request an invalid record (invalid name)


   .. code-block:: json

      {
         "id":"f212932f-b260-4f01-9648-8332200524cc",
         "jsonrpc":"2.0",
         "method":"admin_lookup_records",
         "params":[
            {
               "record_name":"invalid.record",
               "rec_types":[
                  "1",
                  "16"
               ]
            }
         ]
      }


Response:

   .. code-block:: json

      {
         "jsonrpc":"2.0",
         "result":{
            "errorList":[
               {
                  "code":"2000",
                  "record_name":"invalid.record"
               }
            ]
         },
         "id":"f212932f-b260-4f01-9648-8332200524cc"
      }


Parse Error.

*Note that this is an incomplete request*

   .. code-block:: json

      {[[ invalid json


   .. code-block:: json

      {
         "jsonrpc":"2.0",
         "error":{
            "code":-32700,
            "message":"Parse error"
         }
      }



Invalid method invocation.

Request:

   .. code-block:: json

      {
         "id":"f212932f-b260-4f01-9648-8332200524cc",
         "jsonrpc":"2.0",
         "method":"some_non_existing_method",
         "params":{

         }
      }

Response:

   .. code-block::json

      {
         "error": {
            "code": -32601,
            "message": "Method not found"
         },
         "id": "ded7018e-0720-11eb-abe2-001fc69cc946",
         "jsonrpc": "2.0"
      }


.. _rpc-error-code:

Internally we have defined an ``enum`` class that keeps track of the errors that the server will inform in most of the cases.
Some of this errors are already defined by the `JSONRPC`_ specs and some (``>=1``) are defined by |TS|.

.. class:: RPCErrorCode

   Defines the API error codes that will be used in case of any RPC error.

   .. enumerator:: INVALID_REQUEST  = -32600
   .. enumerator:: METHOD_NOT_FOUND = -32601
   .. enumerator:: INVALID_PARAMS   = -32602
   .. enumerator:: INTERNAL_ERROR   = -32603
   .. enumerator:: PARSE_ERROR      = -32700

      `JSONRPC`_ defined errors.

   .. enumerator:: InvalidVersion     = 1

      The passed version is invalid. **must** be 2.0

   .. enumerator:: InvalidVersionType = 2

      The passed version field type is invalid. **must** be a **string**

   .. enumerator:: MissingVersion = 3

      Version field is missing from the request. This field is mandatory.

   .. enumerator:: InvalidMethodType = 4

      The passed method field type is invalid. **must** be a **string**

   .. enumerator:: MissingMethod = 5

      Method field is missing from the request. This field is mandatory.

   .. enumerator:: InvalidParamType = 6

      The passed parameter field type is not valid.

   .. enumerator:: InvalidIdType = 7

      The passed id field type is invalid.

   .. enumerator:: NullId = 8

      The passed if is ``null``

   .. enumerator:: ExecutionError = 9

      An error occurred during the execution of the RPC call. This error is used as a generic High level error. The details details about
      the error, in most cases are specified in the ``data`` field.


.. information:

   According to the JSONRPC 2.0 specs, if you get an error, the ``result`` field will **not** be set. |TS| will grant this.



Development Guide
=================

* For details on how to implement JSONRPC  handler and expose them through the rpc server, please refer to :ref:`jsonrpc_development`.
* If you need to call a new JSONRPC API through :program:`traffic_ctl`, please refer to :ref:`developer-guide-traffic_ctl-development`

See also
========

:ref:`admnin-jsonrpc-configuration`,
:ref:`traffic_ctl_jsonrpc`
