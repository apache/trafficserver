.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: cpp

TSLogFieldRegister
******************

Registers a custom log field, or modify an existing log field with a new definition.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSLogFieldRegister(std::string_view name, std::string_view symbol, TSLogType type, TSLogMarshalCallback marshal_cb, TSLogUnmarshalCallback unmarshal_cb, bool replace = false);

.. enum:: TSLogType

   Specify the type of a log field

   .. enumerator:: TS_LOG_TYPE_INT

      Integer field.

   .. enumerator:: TS_LOG_TYPE_STRING

      String field.

   .. enumerator:: TS_LOG_TYPE_ADDR

      Address field. It supports IPv4 address, IPv6 address, and Unix Domain Socket address (path).

.. type:: int (*TSLogMarshalCallback)(TSHttpTxn, char *);

   Callback sginature for functions to marshal log fields.

.. type:: std::tuple<int, int> (*TSLogUnmarshalCallback)(char **, char *, int);

   Callback sginature for functions to unmarshal log fields.

.. function:: int TSLogStringMarshal(char *buf, std::string_view str);
.. function:: int TSLogIntMarshal(char *buf, int64_t value);
.. function:: int TSLogAddrMarshal(char *buf, sockaddr *addr);
.. function:: std::tuple<int, int> TSLogStringUnmarshal(char **buf, char *dest, int len);
.. function:: std::tuple<int, int> TSLogIntUnmarshal(char **buf, char *dest, int len);
.. function:: std::tuple<int, int> TSLogAddrUnmarshal(char **buf, char *dest, int len);

   Predefined marshaling and unmarshaling functions.

Description
===========

The function registers or modify a log field for access log. This is useful if you want to log something that |TS| does not expose,
log plugin state, or redefine existing log fields.

The `name` is a human friendly name, and only used for debug log. The `symbol` is the keyword you'd want to use on logging.yaml for
the log field. It needs to be unique unless you are replacing an existing field by passing `true` to the optional argument
`replace`, otherwise the API call fails.

The `type` is the data type of a log field. You can log any data as a string value, but please note that aggregating function such
as AVG and SUM are only available for integer log fields.

In many cases, you don't need to write code for marshaling and unmarshaling from scratch. The predefined functions are provided for
your convenience, and you only needs to pass a value that you want to log,

Example:

    .. code-block:: cpp

       TSLogFieldRegister("Example", "exmpl", TS_LOG_TYPE_INT,
       [](TSHttpTxn txnp, char *buf) -> int {
         return TSLogIntMarshal(buf, 123);
       },
       TSLogIntUnmarshal);

Return Values
=============

:func:`TSLogFieldRegister` returns :enumerator:`TS_SUCCESS` if it successfully registeres a new field, or :enumerator:`TS_ERROR` if it
fails due to symbol conflict. If :arg:`replace` is set to `true`, the function resolve the conflict by replacing the existing
field definition with a new one, and returns :enumerator:`TS_SUCCESS`.
